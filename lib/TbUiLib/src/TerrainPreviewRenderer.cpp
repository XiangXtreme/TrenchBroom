/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/TerrainPreviewRenderer.h"

#include <QFuture>
#include <QTimer>
#include <QWidget>
#include <QtConcurrent/QtConcurrentRun>

#include "base/Color.h"
#include "base/NotifierConnection.h"
#include "fs/Reader.h"
#include "gl/Material.h"
#include "gl/ResourceManager.h"
#include "gl/Texture.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/EditorContext.h"
#include "mdl/GroupNode.h"
#include "mdl/LoadMipTexture.h"
#include "mdl/Map.h"
#include "mdl/WorldNode.h"
#include "render/BrushRendererArrays.h"
#include "render/FaceRenderer.h"
#include "render/RenderBatch.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/ProjectSession.h"
#include "ui/TerrainBaker.h"
#include "ui/TerrainInspector.h"
#include "ui/TerrainMaterial.h"
#include "ui/TerrainRegion.h"

#include "vm/plane.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tb::ui
{
namespace
{
using Vertex = gl::VertexTypes::P3NT2T2::Vertex;
using FaceIndices =
  std::unordered_map<const gl::Material*, std::shared_ptr<render::BrushIndexArray>>;

struct MaterialSources
{
  std::array<QImage, 3> images;
  std::vector<std::uint8_t> package;
};

struct PreparedBake
{
  TerrainBake bake;
  std::shared_ptr<const TerrainMaterial> material;
};

std::shared_ptr<PreparedBake> prepareBake(
  const TerrainRegion& region,
  std::shared_ptr<const TerrainMaterial> material,
  std::optional<MaterialSources> sources)
{
  if (!material && sources)
  {
    auto normalized = normalizeTerrainMaterialImages(std::move(sources->images));
    if (normalized.is_error())
      return nullptr;
    auto package = decodeTerrainMaterialPackage(sources->package, normalized.value());
    if (package)
      material = std::make_shared<TerrainMaterial>(std::move(*package));
    else
    {
      auto prepared = prepareTerrainMaterial(std::move(normalized).value());
      if (prepared.is_error())
        return nullptr;
      material = std::make_shared<TerrainMaterial>(std::move(prepared).value());
    }
  }
  auto bake = runTerrainBaker(region, material.get(), TerrainBakeMode::Shared);
  return bake.is_success() ? std::make_shared<PreparedBake>(
                               PreparedBake{std::move(bake).value(), std::move(material)})
                           : nullptr;
}

struct PreviewScene
{
  std::vector<std::unique_ptr<gl::Material>> materials;
  std::shared_ptr<render::BrushVertexArray> vertices =
    std::make_shared<render::BrushVertexArray>();
  std::shared_ptr<FaceIndices> indices = std::make_shared<FaceIndices>();
  render::FaceRenderer renderer{vertices, indices, RgbaF{1, 1, 1, 1}};

  bool ready() const
  {
    return std::ranges::all_of(materials, [](const auto& material) {
      return material->texture() && material->texture()->isReady();
    });
  }
};

std::unique_ptr<PreviewScene> makeScene(
  mdl::Map& map,
  const mdl::GroupNode& group,
  const TerrainRegion& region,
  const TerrainBake& bake)
{
  const auto surfaces = terrainSurfaces(group, region);
  if (surfaces.is_error())
    return nullptr;

  auto scene = std::make_unique<PreviewScene>();
  scene->materials.resize(bake.textures.size());
  const auto& transform = group.group().transformation();
  for (const auto& surface : surfaces.value())
  {
    const auto& face = surface.brush->brush().face(surface.top);
    if (!map.editorContext().visible(*surface.brush, face))
      continue;

    const auto& plane = face.boundary();
    for (const auto& bakedFace : bake.faces)
    {
      if (bakedFace.material >= bake.textures.size())
        continue;
      auto polygon = surface.outline.intersected(bakedFace.polygon);
      if (terrainArea(polygon) <= 1e-7)
        continue;
      if (polygon.size() > 1 && polygon.front() == polygon.back())
        polygon.removeLast();
      if (polygon.size() < 3)
        continue;

      const auto& texture = bake.textures[bakedFace.material];
      auto& material = scene->materials[bakedFace.material];
      if (!material)
      {
        const auto* begin = reinterpret_cast<const char*>(texture.miptex.data());
        auto reader = fs::Reader::from(begin, begin + texture.miptex.size());
        auto decoded = mdl::loadHlMipTexture(reader, false);
        if (decoded.is_error())
          continue;
        auto resource = gl::createTextureResource(std::move(decoded).value());
        map.resourceManager().addResource(resource);
        material = std::make_unique<gl::Material>(texture.name, std::move(resource));
      }

      auto positions = std::vector<vm::vec3d>{};
      positions.reserve(size_t(polygon.size()));
      for (const auto& pixel : polygon)
      {
        const auto xy = transform * region.unproject(pixel);
        positions.push_back(
          vm::vec3d{xy.x(), xy.y(), plane.zAt(vm::vec2d{xy.x(), xy.y()})}
          + plane.normal * 0.1);
      }
      // The face renderer uses clockwise front faces, matching vm::from_points.
      auto winding = std::optional<vm::plane3d>{};
      for (auto i = size_t{1}; i + 1 < positions.size() && !winding; ++i)
        winding = vm::from_points(positions[0], positions[i], positions[i + 1]);
      if (!winding)
        continue;
      if (vm::dot(winding->normal, plane.normal) < 0)
      {
        std::ranges::reverse(positions);
        std::reverse(polygon.begin(), polygon.end());
      }

      auto [vertexBlock, vertexDest] =
        scene->vertices->getPointerToInsertVerticesAt(positions.size());
      for (auto i = size_t{0}; i < positions.size(); ++i)
      {
        const auto& pixel = polygon[qsizetype(i)];
        const auto uv = [&](const vm::vec3d& axis, const double dimension) {
          return float(
            (axis.x() * pixel.x() + axis.y() * pixel.y() + axis.z()) / dimension);
        };
        vertexDest[i] = Vertex{
          vm::vec3f{positions[i]},
          vm::vec3f{plane.normal},
          vm::vec2f{uv(bakedFace.u, texture.width), uv(bakedFace.v, texture.height)},
          vm::vec2f{0.5f, 0.5f}};
      }
      auto& holder = (*scene->indices)[material.get()];
      if (!holder)
        holder = std::make_shared<render::BrushIndexArray>();
      auto [indexBlock, indexDest] =
        holder->getPointerToInsertElementsAt((positions.size() - 2) * 3);
      static_cast<void>(indexBlock);
      const auto start = GLuint(vertexBlock->pos);
      for (auto i = size_t{0}; i + 2 < positions.size(); ++i)
      {
        *indexDest++ = start;
        *indexDest++ = start + GLuint(i + 1);
        *indexDest++ = start + GLuint(i + 2);
      }
    }
  }
  // Unused bake textures do not need GPU resources.
  std::erase(scene->materials, nullptr);
  return scene;
}
} // namespace

class TerrainPreviewRenderer::Impl
{
  using Clock = std::chrono::steady_clock;
  struct Entry
  {
    std::string wanted;
    std::string running;
    std::string baked;
    std::string failed;
    Clock::time_point changedAt{};
    QFuture<std::shared_ptr<PreparedBake>> future;
    std::shared_ptr<PreparedBake> bake;
    std::optional<TerrainRegion> bakedRegion;
    std::optional<TerrainRegion> runningRegion;
    size_t runningMaterialRevision = 0;
    std::unique_ptr<PreviewScene> scene;
    size_t geometryRevision = 0;
  };

  AppController& m_app;
  MapDocument& m_document;
  QWidget& m_view;
  QTimer m_timer;
  NotifierConnection m_connection;
  QMetaObject::Connection m_projectConnection;
  std::weak_ptr<ProjectSession> m_project;
  size_t m_materialRevision = 1;
  size_t m_geometryRevision = 1;
  size_t m_groupsRevision = 0;
  std::map<std::string, std::shared_ptr<const TerrainMaterial>> m_materials;
  std::unordered_map<const mdl::GroupNode*, Entry> m_entries;
  std::vector<const mdl::GroupNode*> m_groups;

public:
  Impl(AppController& app, MapDocument& document, QWidget& view)
    : m_app{app}
    , m_document{document}
    , m_view{view}
  {
    m_timer.setInterval(100);
    QObject::connect(&m_timer, &QTimer::timeout, &m_view, [&] { m_view.update(); });
    const auto geometryChanged = [this](const auto&...) { ++m_geometryRevision; };
    m_connection += document.nodesDidChangeNotifier.connect(geometryChanged);
    m_connection += document.nodesWereAddedNotifier.connect(geometryChanged);
    m_connection += document.nodesWillBeRemovedNotifier.connect(geometryChanged);
    m_connection += document.nodesWereRemovedNotifier.connect(geometryChanged);
    m_connection += document.nodeVisibilityDidChangeNotifier.connect(geometryChanged);
    m_connection += document.editorContextDidChangeNotifier.connect(geometryChanged);
    m_connection += document.documentWasLoadedNotifier.connect(geometryChanged);
  }

  ~Impl() { QObject::disconnect(m_projectConnection); }

  void render(render::RenderBatch& batch, const TerrainInspector* inspector)
  {
    const auto project = m_app.projectFor(m_document);
    if (m_project.lock() != project)
    {
      QObject::disconnect(m_projectConnection);
      m_project = project;
      ++m_materialRevision;
      m_materials.clear();
      if (project)
        m_projectConnection = QObject::connect(
          project.get(), &ProjectSession::contentsChanged, &m_view, [this] {
            ++m_materialRevision;
            m_materials.clear();
            m_view.update();
          });
    }

    auto& map = m_document.map();
    if (m_groupsRevision != m_geometryRevision)
    {
      m_groups.clear();
      const auto visit = [&](const auto& self, const mdl::Node& node) -> void {
        if (const auto* group = dynamic_cast<const mdl::GroupNode*>(&node);
            group && !group->group().terrainPaint().empty())
          m_groups.push_back(group);
        for (const auto* child : node.children())
          self(self, *child);
      };
      visit(visit, map.worldNode());
      const auto seen =
        std::unordered_set<const mdl::GroupNode*>{m_groups.begin(), m_groups.end()};
      std::erase_if(
        m_entries, [&](const auto& item) { return !seen.contains(item.first); });
      m_groupsRevision = m_geometryRevision;
    }
    auto needsTimer = false;
    for (const auto* group : m_groups)
    {
      const auto* stroke = inspector ? inspector->previewStroke(*group) : nullptr;
      auto decoded = stroke ? Result<TerrainRegion>{*stroke}
                            : TerrainRegion::decode(group->group().terrainPaint());
      if (decoded.is_success())
        updateEntry(*group, decoded.value(), project.get(), batch, needsTimer);
    }
    if (needsTimer && !m_timer.isActive())
      m_timer.start();
    else if (!needsTimer)
      m_timer.stop();
  }

private:
  void updateEntry(
    const mdl::GroupNode& group,
    const TerrainRegion& region,
    const ProjectSession* project,
    render::RenderBatch& batch,
    bool& needsTimer)
  {
    auto& entry = m_entries[&group];
    const auto wanted =
      region.encode()
      + (region.materialAssetId.empty() ? std::string{} : ":" + std::to_string(m_materialRevision));
    if (entry.wanted != wanted)
    {
      entry.wanted = wanted;
      entry.changedAt = Clock::now();
    }
    if (!entry.running.empty() && entry.future.isFinished())
    {
      auto bake = entry.future.result();
      if (
        bake && bake->material && entry.runningRegion
        && entry.runningMaterialRevision == m_materialRevision)
        m_materials[entry.runningRegion->materialAssetId] = bake->material;
      if (entry.running == entry.wanted)
      {
        entry.bake = std::move(bake);
        entry.baked = entry.bake ? entry.running : std::string{};
        entry.failed = entry.bake ? std::string{} : entry.running;
        entry.bakedRegion = entry.bake ? entry.runningRegion : std::nullopt;
        entry.scene.reset();
        entry.geometryRevision = 0;
      }
      entry.running.clear();
      entry.runningRegion.reset();
    }
    if (
      entry.wanted != entry.baked && entry.wanted != entry.failed
      && entry.running.empty())
    {
      if (Clock::now() - entry.changedAt >= std::chrono::milliseconds{150})
      {
        auto material = std::shared_ptr<const TerrainMaterial>{};
        auto sources = std::optional<MaterialSources>{};
        auto valid = true;
        if (!region.materialAssetId.empty())
        {
          if (const auto it = m_materials.find(region.materialAssetId);
              it != m_materials.end())
            material = it->second;
          else if (project)
          {
            auto images =
              loadTerrainMaterialImages(project->store(), region.materialAssetId);
            if (images.is_success())
            {
              auto package = project->store().readAsset(
                region.materialAssetId, project::ContentKind::Compatible);
              sources = MaterialSources{
                std::move(images).value(),
                package.is_success() ? std::move(package).value()
                                     : std::vector<std::uint8_t>{}};
            }
          }
          valid = bool(material) || sources.has_value();
        }
        if (valid)
        {
          entry.running = entry.wanted;
          entry.runningRegion = region;
          entry.runningMaterialRevision = m_materialRevision;
          // The task owns its inputs and never accesses the live document or store.
          entry.future = QtConcurrent::run([region,
                                            material = std::move(material),
                                            sources = std::move(sources)]() mutable {
            return prepareBake(region, std::move(material), std::move(sources));
          });
        }
        else
        {
          entry.failed = entry.wanted;
          entry.scene.reset();
          entry.bake.reset();
        }
      }
      else
        needsTimer = true;
    }
    if (!entry.running.empty())
      needsTimer = true;

    if (entry.bake && entry.bakedRegion)
    {
      if (entry.geometryRevision != m_geometryRevision)
      {
        entry.scene =
          makeScene(m_document.map(), group, *entry.bakedRegion, entry.bake->bake);
        entry.geometryRevision = m_geometryRevision;
      }
      if (entry.scene)
      {
        if (entry.scene->ready())
          entry.scene->renderer.render(batch);
        else
          needsTimer = true;
      }
    }
  }
};

TerrainPreviewRenderer::TerrainPreviewRenderer(
  AppController& app, MapDocument& document, QWidget& view)
  : m_impl{std::make_unique<Impl>(app, document, view)}
{
}

TerrainPreviewRenderer::~TerrainPreviewRenderer() = default;

void TerrainPreviewRenderer::render(
  render::RenderBatch& batch, const TerrainInspector* inspector)
{
  m_impl->render(batch, inspector);
}
} // namespace tb::ui
