/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/TerrainPreviewRenderer.h"

#include <QFuture>
#include <QFutureWatcher>
#include <QTimer>
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
#include "render/LightmapData.h"
#include "render/RenderBatch.h"
#include "render/RenderTiming.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/ProjectSession.h"
#include "ui/TerrainBaker.h"
#include "ui/TerrainChunks.h"
#include "ui/TerrainInspector.h"
#include "ui/TerrainMaterial.h"
#include "ui/TerrainRegion.h"

#include "vm/mat_ext.h"
#include "vm/plane.h"

#include <algorithm>
#include <array>
#include <cmath>
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
using PreviewMaterials = std::vector<std::shared_ptr<gl::Material>>;

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
  struct VertexBlock
  {
    render::AllocationTracker::Block* allocation;
    std::vector<vm::vec3f> positions;
  };

  std::shared_ptr<PreviewMaterials> materials;
  std::shared_ptr<render::BrushVertexArray> vertices =
    std::make_shared<render::BrushVertexArray>();
  std::shared_ptr<FaceIndices> indices = std::make_shared<FaceIndices>();
  render::FaceRenderer renderer{vertices, indices, RgbaF{1, 1, 1, 1}};
  std::vector<VertexBlock> vertexBlocks;
  vm::mat4x4d initialTransform;

  PreviewScene(std::shared_ptr<PreviewMaterials> materials_, const vm::mat4x4d& transform)
    : materials{std::move(materials_)}
    , initialTransform{transform}
  {
  }

  bool ready() const
  {
    return std::ranges::all_of(*materials, [](const auto& material) {
      return !material || (material->texture() && material->texture()->isReady());
    });
  }

  bool translateTo(const vm::mat4x4d& transform)
  {
    const auto inverse = vm::invert(initialTransform);
    if (!inverse)
      return false;
    const auto delta = transform * *inverse;
    const auto translation = delta * vm::vec3d{};
    if (!vm::is_equal(delta, vm::translation_matrix(translation), 1e-6))
      return false;
    for (const auto& block : vertexBlocks)
    {
      auto* destination = vertices->getPointerToUpdateVerticesAt(*block.allocation);
      for (auto i = size_t{0}; i < block.positions.size(); ++i)
        destination[i].attr = block.positions[i] + vm::vec3f{translation};
    }
    render::RenderTiming::count("terrain.scene_translations", 1);
    return true;
  }
};

// The bake grid has fixed bounds. Index face bounds once so each source brush only
// checks nearby baked polygons, preserving their original order for clipping.
class TerrainFaceIndex
{
  int m_columns;
  int m_rows;
  std::vector<QRectF> m_bounds;
  std::vector<std::vector<size_t>> m_cells;

  std::array<int, 4> cellsFor(const QRectF& bounds) const
  {
    const auto cell = [](const double value, const double extent, const int count) {
      return int(
        std::clamp(std::floor(value * double(count) / extent), 0.0, double(count - 1)));
    };
    const auto width = TerrainRegion::Width;
    const auto height = TerrainRegion::Height;
    return {
      cell(bounds.left(), width, m_columns),
      cell(bounds.top(), height, m_rows),
      cell(bounds.right(), width, m_columns),
      cell(bounds.bottom(), height, m_rows)};
  }

public:
  explicit TerrainFaceIndex(const TerrainBake& bake)
    : m_columns{std::clamp(int(std::sqrt(double(bake.faces.size()) * 2.0)), 1, 40)}
    , m_rows{std::clamp(
        int(std::ceil(double(bake.faces.size()) / double(m_columns))), 1, 20)}
    , m_cells(size_t(m_columns * m_rows))
  {
    m_bounds.reserve(bake.faces.size());
    for (auto i = size_t{0}; i < bake.faces.size(); ++i)
    {
      const auto bounds = bake.faces[i].polygon.boundingRect();
      m_bounds.push_back(bounds);
      const auto [left, top, right, bottom] = cellsFor(bounds);
      for (auto y = top; y <= bottom; ++y)
        for (auto x = left; x <= right; ++x)
          m_cells[size_t(y * m_columns + x)].push_back(i);
    }
  }

  std::vector<size_t> intersecting(const QRectF& bounds) const
  {
    auto result = std::vector<size_t>{};
    auto seen = std::vector<bool>(m_bounds.size());
    const auto [left, top, right, bottom] = cellsFor(bounds);
    for (auto y = top; y <= bottom; ++y)
      for (auto x = left; x <= right; ++x)
        for (const auto i : m_cells[size_t(y * m_columns + x)])
          if (!seen[i] && m_bounds[i].intersects(bounds))
          {
            seen[i] = true;
            result.push_back(i);
          }
    std::ranges::sort(result);
    return result;
  }
};

std::unique_ptr<PreviewScene> makeScene(
  mdl::Map& map,
  const mdl::GroupNode& group,
  const TerrainRegion& region,
  const std::vector<TerrainSurface>& surfaces,
  const TerrainBake& bake,
  std::shared_ptr<PreviewMaterials> materials,
  const render::LightmapAssembly* lightmap)
{
  const auto timing = render::RenderTimingScope{"cpu.terrain.scene_build"};
  render::RenderTiming::count("terrain.scene_builds", 1);
  const auto& transform = group.group().transformation();
  const auto inverse = vm::invert(transform);
  if (!inverse)
    return nullptr;
  auto scene = std::make_unique<PreviewScene>(std::move(materials), transform);
  const auto faceIndex = TerrainFaceIndex{bake};
  for (const auto& surface : surfaces)
  {
    const auto& face = surface.brush->brush().face(surface.top);
    if (!map.editorContext().visible(*surface.brush, face))
      continue;

    const auto& plane = face.boundary();
    const auto surfaceBounds = surface.outline.boundingRect();
    const auto lightmapBlocks =
      lightmap ? lightmap->terrainBlocksFor(plane, face.vertexPositions())
               : std::vector<const render::TerrainLightmapBlock*>{};
    for (const auto index : faceIndex.intersecting(surfaceBounds))
    {
      const auto& bakedFace = bake.faces[index];
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
      auto& material = (*scene->materials)[bakedFace.material];
      if (!material)
      {
        render::RenderTiming::count("terrain.texture_resources_created", 1);
        const auto* begin = reinterpret_cast<const char*>(texture.miptex.data());
        auto reader = fs::Reader::from(begin, begin + texture.miptex.size());
        auto decoded = mdl::loadHlMipTexture(reader, false);
        if (decoded.is_error())
          continue;
        auto resource = gl::createTextureResource(std::move(decoded).value());
        map.resourceManager().addResource(resource);
        material = std::make_shared<gl::Material>(texture.name, std::move(resource));
      }

      auto positions = std::vector<vm::vec3d>{};
      positions.reserve(size_t(polygon.size()));
      for (const auto& pixel : polygon)
      {
        const auto xy = transform * region.unproject(pixel);
        positions.emplace_back(xy.x(), xy.y(), plane.zAt(vm::vec2d{xy.x(), xy.y()}));
      }
      auto fragments =
        lightmap
          ? lightmap->terrainFragments(lightmapBlocks, positions)
          : std::vector<render::TerrainLightmapFragment>{{std::move(positions), nullptr}};
      for (auto& fragment : fragments)
      {
        auto& fragmentPositions = fragment.vertices;
        // The face renderer uses clockwise front faces, matching vm::from_points.
        auto winding = std::optional<vm::plane3d>{};
        for (auto i = size_t{1}; i + 1 < fragmentPositions.size() && !winding; ++i)
          winding = vm::from_points(
            fragmentPositions[0], fragmentPositions[i], fragmentPositions[i + 1]);
        if (!winding)
          continue;
        if (vm::dot(winding->normal, plane.normal) < 0)
          std::ranges::reverse(fragmentPositions);

        auto [vertexBlock, vertexDest] =
          scene->vertices->getPointerToInsertVerticesAt(fragmentPositions.size());
        auto basePositions = std::vector<vm::vec3f>{};
        basePositions.reserve(fragmentPositions.size());
        for (auto i = size_t{0}; i < fragmentPositions.size(); ++i)
        {
          const auto& position = fragmentPositions[i];
          const auto pixel = region.project(*inverse * position);
          const auto uv = [&](const vm::vec3d& axis, const double dimension) {
            return float(
              (axis.x() * pixel.x() + axis.y() * pixel.y() + axis.z()) / dimension);
          };
          const auto lightmapUv = lightmap ? lightmap->terrainUv(fragment.block, position)
                                           : vm::vec2f{0.5f, 0.5f};
          const auto renderPosition = vm::vec3f{position + plane.normal * 0.1};
          vertexDest[i] = Vertex{
            renderPosition,
            vm::vec3f{plane.normal},
            vm::vec2f{uv(bakedFace.u, texture.width), uv(bakedFace.v, texture.height)},
            lightmapUv};
          basePositions.push_back(renderPosition);
        }
        scene->vertexBlocks.push_back({vertexBlock, std::move(basePositions)});
        auto& holder = (*scene->indices)[material.get()];
        if (!holder)
          holder = std::make_shared<render::BrushIndexArray>();
        auto [indexBlock, indexDest] =
          holder->getPointerToInsertElementsAt((fragmentPositions.size() - 2) * 3);
        static_cast<void>(indexBlock);
        const auto start = GLuint(vertexBlock->pos);
        for (auto i = size_t{0}; i + 2 < fragmentPositions.size(); ++i)
        {
          *indexDest++ = start;
          *indexDest++ = start + GLuint(i + 1);
          *indexDest++ = start + GLuint(i + 2);
        }
      }
    }
  }
  return scene;
}
} // namespace

class TerrainPreviewRenderer::Impl
{
  struct Display
  {
    std::string key;
    std::shared_ptr<PreparedBake> bake;
    TerrainRegion region;
    std::shared_ptr<PreviewMaterials> materials;
    std::unique_ptr<PreviewScene> scene;
    size_t geometryRevision = 0;
    const render::LightmapAssembly* lightmap = nullptr;
    std::optional<size_t> strokeRevision;
  };

  struct Entry
  {
    std::string encodedRegion;
    std::optional<TerrainRegion> region;
    std::string wanted;
    std::string running;
    std::string failed;
    QFuture<std::shared_ptr<PreparedBake>> future;
    std::unique_ptr<QFutureWatcher<std::shared_ptr<PreparedBake>>> watcher;
    std::optional<TerrainRegion> runningRegion;
    size_t runningMaterialRevision = 0;
    std::optional<size_t> strokeRevision;
    std::optional<size_t> runningStrokeRevision;
    size_t geometryRevision = 1;
    size_t surfaceRevision = 0;
    std::optional<vm::vec2d> surfaceOrigin;
    std::optional<std::vector<TerrainSurface>> surfaces;
    vm::mat4x4d observedTransform;
    bool observed = false;
    std::shared_ptr<Display> active;
    std::shared_ptr<Display> strokeBase;
    std::shared_ptr<Display> pending;
  };

  AppController& m_app;
  MapDocument& m_document;
  Notifier<>& m_previewDidChange;
  QTimer m_timer;
  NotifierConnection m_connection;
  QMetaObject::Connection m_projectConnection;
  std::weak_ptr<ProjectSession> m_project;
  size_t m_materialRevision = 1;
  bool m_groupsDirty = true;
  std::map<std::string, std::shared_ptr<const TerrainMaterial>> m_materials;
  std::unordered_map<const mdl::GroupNode*, Entry> m_entries;
  std::vector<const mdl::GroupNode*> m_groups;

public:
  Impl(AppController& app, MapDocument& document, Notifier<>& previewDidChange)
    : m_app{app}
    , m_document{document}
    , m_previewDidChange{previewDidChange}
  {
    // GPU resources are processed independently on a 20 ms timer.
    m_timer.setInterval(20);
    m_timer.setSingleShot(true);
    QObject::connect(&m_timer, &QTimer::timeout, &m_timer, [&] { m_previewDidChange(); });
    m_connection += document.nodesDidChangeNotifier.connect(
      [this](const auto& nodes) { nodesChanged(nodes); });
    m_connection += document.nodesWereAddedNotifier.connect([this](const auto& nodes) {
      invalidateAffected(nodes);
      m_groupsDirty = true;
    });
    m_connection += document.nodesWillBeRemovedNotifier.connect(
      [this](const auto& nodes) { nodesWillBeRemoved(nodes); });
    m_connection += document.nodesWereRemovedNotifier.connect(
      [this](const auto&) { m_groupsDirty = true; });
    m_connection += document.nodeVisibilityDidChangeNotifier.connect(
      [this](const auto& nodes) { invalidateAffected(nodes); });
    m_connection += document.editorContextDidChangeNotifier.connect([this] {
      for (auto& [group, entry] : m_entries)
        ++entry.geometryRevision;
    });
    m_connection += document.documentWasLoadedNotifier.connect([this] {
      m_groups.clear();
      m_entries.clear();
      m_groupsDirty = true;
    });
  }

  ~Impl() { QObject::disconnect(m_projectConnection); }

  void render(
    render::RenderBatch& batch,
    const TerrainInspector* inspector,
    const bool visible,
    const render::LightmapAssembly* lightmap)
  {
    if (!visible)
    {
      m_timer.stop();
      return;
    }
    const auto timing = render::RenderTimingScope{"cpu.terrain.preview"};
    const auto project = m_app.projectFor(m_document);
    if (m_project.lock() != project)
    {
      QObject::disconnect(m_projectConnection);
      m_project = project;
      ++m_materialRevision;
      m_materials.clear();
      if (project)
        m_projectConnection = QObject::connect(
          project.get(), &ProjectSession::contentsChanged, &m_timer, [this] {
            ++m_materialRevision;
            m_materials.clear();
            m_previewDidChange();
          });
    }

    auto& map = m_document.map();
    if (m_groupsDirty)
    {
      m_groups.clear();
      const auto visit = [&](const auto& self, const mdl::Node& node) -> void {
        if (const auto* group = dynamic_cast<const mdl::GroupNode*>(&node);
            group && !group->group().terrainPaint().empty()
            && !isTerrainChunkGroup(*group))
          m_groups.push_back(group);
        for (const auto* child : node.children())
          self(self, *child);
      };
      visit(visit, map.worldNode());
      const auto seen =
        std::unordered_set<const mdl::GroupNode*>{m_groups.begin(), m_groups.end()};
      std::erase_if(
        m_entries, [&](const auto& item) { return !seen.contains(item.first); });
      m_groupsDirty = false;
    }
    auto needsTimer = false;
    for (const auto* group : m_groups)
    {
      auto& entry = m_entries[group];
      if (!entry.observed)
      {
        entry.observedTransform = group->group().transformation();
        entry.observed = true;
      }
      const auto* stroke = inspector ? inspector->previewStroke(*group) : nullptr;
      const auto& encoded = group->group().terrainPaint();
      if (entry.encodedRegion != encoded)
      {
        entry.encodedRegion = encoded;
        auto decoded = TerrainRegion::decode(encoded);
        entry.region = decoded.is_success()
                         ? std::optional<TerrainRegion>{std::move(decoded).value()}
                         : std::nullopt;
      }
      const auto* region = stroke ? stroke : (entry.region ? &*entry.region : nullptr);
      if (!region)
      {
        entry.active.reset();
        entry.pending.reset();
        continue;
      }
      auto saved = entry.region ? entry.region->paintCacheKey() : std::string{};
      auto wanted = stroke ? stroke->paintCacheKey() : saved;
      if (!region->materialAssetId.empty())
      {
        const auto suffix = ":" + std::to_string(m_materialRevision);
        wanted += suffix;
        saved += suffix;
      }
      updateEntry(
        *group,
        entry,
        *region,
        std::move(wanted),
        saved,
        stroke ? std::optional{inspector->strokeRevision()} : std::nullopt,
        project.get(),
        batch,
        needsTimer,
        lightmap);
    }
    if (needsTimer && !m_timer.isActive())
      m_timer.start();
    else if (!needsTimer)
      m_timer.stop();
  }

private:
  static bool intersects(const mdl::GroupNode& group, const mdl::Node& node)
  {
    return &group == &node || group.isAncestorOf(node) || node.isAncestorOf(group);
  }

  void invalidateAffected(const std::vector<mdl::Node*>& nodes)
  {
    for (const auto* group : m_groups)
    {
      if (std::ranges::any_of(
            nodes, [&](const auto* node) { return intersects(*group, *node); }))
      {
        if (const auto entry = m_entries.find(group); entry != m_entries.end())
          ++entry->second.geometryRevision;
      }
    }
  }

  void nodesWillBeRemoved(const std::vector<mdl::Node*>& nodes)
  {
    // Remove cached pointers while their nodes and parent relationships are still valid.
    std::erase_if(m_groups, [&](const auto* group) {
      const auto removed = std::ranges::any_of(nodes, [&](const auto* node) {
        return node == group || node->isAncestorOf(*group);
      });
      if (removed)
        m_entries.erase(group);
      else if (std::ranges::any_of(
                 nodes, [&](const auto* node) { return group->isAncestorOf(*node); }))
        if (const auto entry = m_entries.find(group); entry != m_entries.end())
          ++entry->second.geometryRevision;
      return removed;
    });
    m_groupsDirty = true;
  }

  void nodesChanged(const std::vector<mdl::Node*>& nodes)
  {
    for (const auto* node : nodes)
    {
      if (const auto* group = dynamic_cast<const mdl::GroupNode*>(node))
      {
        const auto known = std::ranges::find(m_groups, group) != m_groups.end();
        if (
          known
          != (!group->group().terrainPaint().empty() && !isTerrainChunkGroup(*group)))
          m_groupsDirty = true;
      }
    }

    for (const auto* group : m_groups)
    {
      const auto entryIt = m_entries.find(group);
      if (entryIt == m_entries.end())
        continue;
      auto& entry = entryIt->second;
      const auto groupChanged = std::ranges::find(nodes, group) != nodes.end();
      const auto relatedGeometry = std::ranges::any_of(nodes, [&](const auto* node) {
        return node != group
               && (group->isAncestorOf(*node) || node->isAncestorOf(*group));
      });
      const auto& transform = group->group().transformation();
      const auto transformChanged =
        entry.observed && !vm::is_equal(transform, entry.observedTransform, 1e-6);
      auto translated = false;
      if (
        groupChanged && transformChanged && relatedGeometry && !group->children().empty())
      {
        const auto allChildrenChanged =
          std::ranges::all_of(group->children(), [&](const auto* child) {
            return std::ranges::find(nodes, child) != nodes.end();
          });
        if (allChildrenChanged)
        {
          const auto inverse = vm::invert(entry.observedTransform);
          if (inverse)
          {
            const auto delta = transform * *inverse;
            const auto offset = delta * vm::vec3d{};
            translated = vm::is_equal(delta, vm::translation_matrix(offset), 1e-6);
          }
          const auto translateDisplay = [&](Display* display) {
            if (translated && display && display->scene)
              translated = display->scene->translateTo(transform);
          };
          translateDisplay(entry.active.get());
          translateDisplay(entry.pending.get());
          if (entry.strokeBase != entry.active && entry.strokeBase != entry.pending)
            translateDisplay(entry.strokeBase.get());
        }
      }
      if ((relatedGeometry || transformChanged) && !translated)
        ++entry.geometryRevision;
      entry.observedTransform = transform;
      entry.observed = true;
    }
  }

  void updateDisplay(
    const mdl::GroupNode& group,
    Entry& entry,
    Display& display,
    const TerrainRegion& geometryRegion,
    const render::LightmapAssembly* lightmap)
  {
    if (
      display.geometryRevision != entry.geometryRevision || display.lightmap != lightmap)
    {
      if (
        entry.surfaceRevision != entry.geometryRevision || !entry.surfaceOrigin
        || !vm::is_equal(*entry.surfaceOrigin, geometryRegion.origin, 1e-6))
      {
        render::RenderTiming::count("terrain.surface_extractions", 1);
        auto surfaces = terrainSurfaces(group, geometryRegion);
        entry.surfaces = surfaces.is_success()
                           ? std::optional{std::move(surfaces).value()}
                           : std::nullopt;
        entry.surfaceRevision = entry.geometryRevision;
        entry.surfaceOrigin = geometryRegion.origin;
      }
      display.scene = entry.surfaces ? makeScene(
                                         m_document.map(),
                                         group,
                                         display.region,
                                         *entry.surfaces,
                                         display.bake->bake,
                                         display.materials,
                                         lightmap)
                                     : nullptr;
      display.geometryRevision = entry.geometryRevision;
      display.lightmap = lightmap;
    }
  }

  void updateEntry(
    const mdl::GroupNode& group,
    Entry& entry,
    const TerrainRegion& region,
    std::string wanted,
    const std::string& saved,
    const std::optional<size_t> strokeRevision,
    const ProjectSession* project,
    render::RenderBatch& batch,
    bool& needsTimer,
    const render::LightmapAssembly* lightmap)
  {
    if (entry.strokeRevision != strokeRevision)
    {
      // A stroke may end and another start before the next frame. Restore the
      // committed preview when available, then record the new stroke's baseline.
      if (entry.strokeBase && entry.strokeBase->key == saved)
        entry.active = entry.strokeBase;
      entry.strokeBase.reset();
      if (strokeRevision && entry.active && entry.active->key == saved)
        entry.strokeBase = entry.active;
      entry.strokeRevision = strokeRevision;
    }
    entry.wanted = std::move(wanted);
    if (
      entry.pending && entry.pending->key != entry.wanted
      && (!strokeRevision || entry.pending->strokeRevision != strokeRevision
          || (entry.active && entry.active->key == entry.wanted)))
      entry.pending.reset();

    const auto updatePending = [&] {
      if (!entry.pending)
        return;
      updateDisplay(group, entry, *entry.pending, region, lightmap);
      if (!entry.pending->scene)
      {
        if (entry.pending->key == entry.wanted)
          entry.failed = entry.wanted;
        entry.pending.reset();
      }
      else if (entry.pending->scene->ready())
        entry.active = std::move(entry.pending);
      else
        needsTimer = true;
    };

    // Do not replace an upload in progress with every newer bake. Otherwise fast
    // continuous input can prevent any of the intermediate previews from appearing.
    updatePending();
    if (!entry.pending && !entry.running.empty() && entry.future.isFinished())
    {
      auto bake = entry.future.result();
      if (
        bake && bake->material && entry.runningRegion
        && entry.runningMaterialRevision == m_materialRevision)
        m_materials[entry.runningRegion->materialAssetId] = bake->material;
      const auto current = entry.running == entry.wanted;
      const auto intermediate = strokeRevision
                                && entry.runningStrokeRevision == strokeRevision
                                && entry.runningMaterialRevision == m_materialRevision
                                && (!entry.active || entry.active->key != entry.wanted)
                                && (!entry.pending || entry.pending->key != entry.wanted);
      if (current || intermediate)
      {
        if (current)
          entry.failed = bake ? std::string{} : entry.running;
        if (bake && entry.runningRegion)
        {
          auto materials = std::make_shared<PreviewMaterials>(bake->bake.textures.size());
          if (entry.active)
          {
            const auto& previous = entry.active->bake->bake.textures;
            auto previousByName = std::unordered_map<std::string, size_t>{};
            for (auto i = size_t{0}; i < previous.size(); ++i)
              previousByName.emplace(previous[i].name, i);
            for (auto i = size_t{0}; i < bake->bake.textures.size(); ++i)
            {
              const auto& texture = bake->bake.textures[i];
              const auto found = previousByName.find(texture.name);
              if (
                found != previousByName.end()
                && previous[found->second].miptex == texture.miptex)
              {
                (*materials)[i] = (*entry.active->materials)[found->second];
                if ((*materials)[i])
                  render::RenderTiming::count("terrain.texture_resources_reused", 1);
              }
            }
          }
          entry.pending = std::make_shared<Display>(Display{
            entry.running,
            std::move(bake),
            *entry.runningRegion,
            std::move(materials),
            nullptr,
            0,
            nullptr,
            entry.runningStrokeRevision});
        }
      }
      entry.running.clear();
      entry.runningRegion.reset();
    }
    if (
      entry.running.empty() && entry.wanted != entry.failed
      && (!entry.active || entry.active->key != entry.wanted)
      && (!entry.pending || entry.pending->key != entry.wanted))
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
        entry.runningStrokeRevision = strokeRevision;
        if (!entry.watcher)
        {
          entry.watcher =
            std::make_unique<QFutureWatcher<std::shared_ptr<PreparedBake>>>();
          QObject::connect(
            entry.watcher.get(),
            &QFutureWatcher<std::shared_ptr<PreparedBake>>::finished,
            &m_timer,
            [this] { m_previewDidChange(); });
        }
        render::RenderTiming::count("terrain.bake_starts", 1);
        // The task owns its inputs and never accesses the live document or store.
        entry.future = QtConcurrent::run([region,
                                          material = std::move(material),
                                          sources = std::move(sources)]() mutable {
          return prepareBake(region, std::move(material), std::move(sources));
        });
        entry.watcher->setFuture(entry.future);
      }
      else
      {
        entry.failed = entry.wanted;
        entry.pending.reset();
        entry.active.reset();
      }
    }

    updatePending();
    if (entry.active)
    {
      updateDisplay(group, entry, *entry.active, region, lightmap);
      if (entry.active->scene && entry.active->scene->ready())
        entry.active->scene->renderer.render(batch);
      else if (entry.active->scene)
        needsTimer = true;
    }
  }
};

TerrainPreviewRenderer::TerrainPreviewRenderer(AppController& app, MapDocument& document)
  : m_impl{std::make_unique<Impl>(app, document, previewDidChangeNotifier)}
{
}

TerrainPreviewRenderer::~TerrainPreviewRenderer() = default;

void TerrainPreviewRenderer::render(
  render::RenderBatch& batch,
  const TerrainInspector* inspector,
  const bool visible,
  const render::LightmapAssembly* lightmap)
{
  m_impl->render(batch, inspector, visible, lightmap);
}
} // namespace tb::ui
