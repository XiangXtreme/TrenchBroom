/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/TerrainExport.h"

#include <QFile>
#include <QJSEngine>

#include "base/Logger.h"
#include "base/SimpleParserStatus.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/Entity.h"
#include "mdl/GroupNode.h"
#include "mdl/MapFormat.h"
#include "mdl/NodeWriter.h"
#include "mdl/WorldNode.h"
#include "mdl/WorldReader.h"
#include "project/GoldSrcWad.h"
#include "ui/TerrainRegion.h"

#include "kd/task_manager.h"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <sstream>
#include <string_view>

static void initTerrainResources()
{
  Q_INIT_RESOURCE(terrain_baker);
}

namespace tb::ui
{
namespace
{
struct BakeFace
{
  QPolygonF polygon;
  vm::vec3d u;
  vm::vec3d v;
  size_t material;
};

struct TerrainBake
{
  std::vector<BakeFace> faces;
  std::vector<project::WadTextureEntry> textures;
};

Result<TerrainBake> runBaker(const TerrainRegion& region)
{
  static const auto initialized = [] {
    initTerrainResources();
    return true;
  }();
  (void)initialized;
  auto js = QJSEngine{};
  js.evaluate(QStringLiteral("var globalThis = this;"));
  for (const auto* name : {":/terrain/bake.js", ":/terrain/multi.js"})
  {
    auto file = QFile{QString::fromLatin1(name)};
    if (!file.open(QIODevice::ReadOnly))
      return Error{"Cannot open the built-in terrain baker."};
    const auto result = js.evaluate(QString::fromUtf8(file.readAll()), file.fileName());
    if (result.isError())
      return Error{result.toString().toStdString()};
  }
  auto weights = js.newArray(uint(region.weights.size()));
  for (auto i = size_t{0}; i < region.weights.size(); ++i)
  {
    auto vertex = js.newArray(3);
    for (auto k = uint{0}; k < 3; ++k)
      vertex.setProperty(k, region.weights[i][k]);
    weights.setProperty(uint(i), vertex);
  }
  js.globalObject().setProperty(QStringLiteral("terrainWeights"), weights);
  const auto result =
    js.evaluate(QStringLiteral("TerrainMulti.bake(terrainWeights, false).shared"));
  if (result.isError())
    return Error{result.toString().toStdString()};

  const auto bytes = result.property(QStringLiteral("wad"))
                       .property(QStringLiteral("buffer"))
                       .toVariant()
                       .toByteArray();
  const auto* begin = reinterpret_cast<const std::uint8_t*>(bytes.constData());
  auto wad = project::readGoldSrcWad({begin, begin + bytes.size()});
  if (wad.is_error())
    return std::get<Error>(wad.error());

  const auto faces = result.property(QStringLiteral("faces"));
  auto plan = TerrainBake{{}, std::move(wad).value().textures};
  const auto count = faces.property(QStringLiteral("length")).toUInt();
  plan.faces.reserve(count);
  const auto uv = [](const QJSValue& row) {
    return vm::vec3d{
      row.property(0u).toNumber(),
      row.property(1u).toNumber(),
      row.property(2u).toNumber()};
  };
  for (auto i = uint{0}; i < count; ++i)
  {
    const auto face = faces.property(i);
    const auto points = face.property(QStringLiteral("points"));
    auto polygon = QPolygonF{};
    for (auto j = uint{0}; j < points.property(QStringLiteral("length")).toUInt(); ++j)
    {
      const auto p = points.property(j);
      polygon.append(QPointF{p.property(0u).toNumber(), p.property(1u).toNumber()});
    }
    plan.faces.push_back(
      {std::move(polygon),
       uv(face.property(QStringLiteral("u"))),
       uv(face.property(QStringLiteral("v"))),
       face.property(QStringLiteral("material")).toUInt()});
  }
  return plan;
}

Result<std::unique_ptr<mdl::Node>> makePiece(
  const TerrainSurface& surface,
  const BakeFace& face,
  const TerrainRegion& region,
  const vm::mat4x4d& transform,
  const vm::mat4x4d& inverse,
  const vm::bbox3d& worldBounds,
  const std::string& material)
{
  auto faces = surface.brush->brush().faces();
  const auto& top = faces[surface.top];
  const auto& p = top.points();
  const auto axis = [&](const vm::vec3d& uv) {
    const auto origin = region.project(inverse * vm::vec3d{0, 0, 0});
    const auto gradient = vm::vec2d{uv.x(), uv.y()};
    return std::pair{
      vm::vec3d{
        vm::dot(region.project(inverse * vm::vec3d{1, 0, 0}) - origin, gradient),
        vm::dot(region.project(inverse * vm::vec3d{0, 1, 0}) - origin, gradient),
        vm::dot(region.project(inverse * vm::vec3d{0, 0, 1}) - origin, gradient)},
      float(vm::dot(origin, gradient) + uv.z())};
  };
  const auto [u, du] = axis(face.u);
  const auto [v, dv] = axis(face.v);
  auto textured = mdl::BrushFace::createFromValve(
    p[0],
    p[1],
    p[2],
    material,
    mdl::UvAttributes{vm::vec2f{du, dv}},
    top.surfaceAttributes(),
    u,
    v,
    mdl::MapFormat::Valve);
  if (textured.is_error())
    return std::get<Error>(textured.error());
  faces[surface.top] = std::move(textured).value();

  // A triangle's bbox center lies on its diagonal. Use the vertex centroid.
  auto centroid = QPointF{};
  for (const auto& point : face.polygon)
    centroid += point;
  const auto middle =
    transform * region.unproject(centroid / double(face.polygon.size()));
  for (auto i = qsizetype{0}; i < face.polygon.size(); ++i)
  {
    const auto a = transform * region.unproject(face.polygon[i]);
    const auto b =
      transform * region.unproject(face.polygon[(i + 1) % face.polygon.size()]);
    const auto c = transform * region.unproject(face.polygon[i], 1.0);
    const auto plane = vm::from_points(a, b, c);
    if (!plane)
      return Error{"Terrain generated an invalid cutting plane."};
    const auto reverse = plane->point_distance(middle) > 0;
    auto cut = mdl::BrushFace::create(
      a, reverse ? c : b, reverse ? b : c, "null", {}, {}, mdl::MapFormat::Valve);
    if (cut.is_error())
      return std::get<Error>(cut.error());
    // Prefer the existing side (and its texture) when an edge coincides with it.
    if (std::ranges::none_of(faces, [&](const auto& f) {
          return vm::is_equal(f.boundary(), cut.value().boundary(), 1e-6);
        }))
      faces.push_back(std::move(cut).value());
  }
  auto brush = mdl::Brush::create(worldBounds, std::move(faces));
  if (brush.is_error())
    return std::get<Error>(brush.error());
  return std::unique_ptr<mdl::Node>{
    std::make_unique<mdl::BrushNode>(std::move(brush).value())};
}

Result<void> bakeGroup(
  mdl::GroupNode& group,
  const vm::bbox3d& bounds,
  TerrainSnapshot& snapshot,
  const size_t groupNumber)
{
  const auto region = TerrainRegion::decode(group.group().terrainPaint());
  if (region.is_error())
    return std::get<Error>(region.error());
  const auto surfaces = terrainSurfaces(group, region.value());
  if (surfaces.is_error())
    return std::get<Error>(surfaces.error());
  auto bake = runBaker(region.value());
  if (bake.is_error())
    return std::get<Error>(bake.error());
  auto plan = std::move(bake).value();
  const auto& transform = group.group().transformation();
  const auto inverse = *vm::invert(transform);
  auto pieces = std::vector<std::unique_ptr<mdl::Node>>{};
  auto used = std::vector<bool>(plan.textures.size());
  for (const auto& surface : surfaces.value())
  {
    auto coverage = 0.0;
    for (const auto& face : plan.faces)
    {
      const auto area = terrainArea(surface.outline.intersected(face.polygon));
      if (area <= 1e-7)
        continue;
      const auto name = fmt::format("tbt{:x}_{:x}", groupNumber, face.material);
      auto piece =
        makePiece(surface, face, region.value(), transform, inverse, bounds, name);
      if (piece.is_error())
        return std::get<Error>(piece.error());
      coverage += area;
      used[face.material] = true;
      pieces.push_back(std::move(piece).value());
    }
    if (std::abs(coverage - terrainArea(surface.outline)) > 1e-4)
      return Error{"The terrain bake did not cover a source brush."};
  }
  for (auto i = size_t{0}; i < used.size(); ++i)
  {
    if (!used[i])
      continue;
    const auto name = fmt::format("tbt{:x}_{:x}", groupNumber, i);
    auto& texture = plan.textures[i];
    if (!texture.valid || name.size() > 15)
      return Error{"Invalid generated terrain texture."};
    std::fill_n(texture.miptex.begin(), 16, std::uint8_t{0});
    std::copy(name.begin(), name.end(), texture.miptex.begin());
    snapshot.textures.emplace(name, std::move(texture.miptex));
  }
  snapshot.topFaces += pieces.size();
  group.replaceChildren(std::move(pieces));
  auto content = group.group();
  content.setTerrainPaint({});
  group.setGroup(std::move(content));
  return {};
}
} // namespace

Result<std::optional<TerrainSnapshot>> bakeTerrainSnapshot(
  const std::vector<std::uint8_t>& source)
{
  const auto text =
    std::string_view{reinterpret_cast<const char*>(source.data()), source.size()};
  if (text.find("\"_tb_terrain_paint\"") == std::string_view::npos)
    return std::optional<TerrainSnapshot>{};

  auto logger = NullLogger{};
  auto status = SimpleParserStatus{logger};
  auto tasks = kdl::task_manager{2};
  const auto bounds = vm::bbox3d{65536.0};
  auto parsed =
    mdl::WorldReader{text, mdl::MapFormat::Valve, {}}.read(bounds, status, tasks);
  if (parsed.is_error())
    return std::get<Error>(parsed.error());

  auto& world = *parsed.value();
  auto groups = std::vector<mdl::GroupNode*>{};
  const auto collect = [&](const auto& self, mdl::Node& node) -> void {
    if (auto* group = dynamic_cast<mdl::GroupNode*>(&node);
        group && !group->group().terrainPaint().empty())
      groups.push_back(group);
    for (auto* child : node.children())
      self(self, *child);
  };
  collect(collect, world);
  if (groups.empty())
    return std::optional<TerrainSnapshot>{};
  auto snapshot = TerrainSnapshot{};
  for (auto i = size_t{0}; i < groups.size(); ++i)
  {
    const auto result = bakeGroup(*groups[i], bounds, snapshot, i);
    if (result.is_error())
      return Error{fmt::format(
        "Terrain group '{}': {}",
        groups[i]->name(),
        std::get<Error>(result.error()).msg)};
  }
  auto entity = world.entity();
  entity.addOrUpdateProperty(mdl::EntityPropertyKeys::ValveVersion, "220");
  entity.addOrUpdateProperty(mdl::EntityPropertyKeys::Wad, "textures.wad");
  world.setEntity(std::move(entity));
  auto output = std::ostringstream{};
  output << "// Game: Half-Life\n// Format: Valve\n";
  auto writer = mdl::NodeWriter{world, output};
  writer.writeMap(tasks);
  const auto map = output.str();
  snapshot.map.assign(map.begin(), map.end());
  return std::optional<TerrainSnapshot>{std::move(snapshot)};
}
} // namespace tb::ui
