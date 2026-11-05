/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/TerrainExport.h"

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
#include "project/ProjectStore.h"
#include "ui/TerrainBaker.h"
#include "ui/TerrainMaterial.h"
#include "ui/TerrainRegion.h"

#include "kd/task_manager.h"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <sstream>
#include <string_view>

namespace tb::ui
{
namespace
{
size_t terrainWadBytes(const TerrainBake& plan, const std::vector<bool>& used)
{
  auto bytes = size_t{12}; // WAD header
  for (auto i = size_t{0}; i < used.size(); ++i)
    if (used[i])
      bytes += 32 + plan.textures[i].miptex.size(); // directory entry and lump
  return bytes;
}

struct FaceWidth
{
  double width;
  vm::vec2d center;
};

FaceWidth faceWidth(
  const QPolygonF& polygon, const TerrainRegion& region, const vm::mat4x4d& transform)
{
  auto points = std::vector<vm::vec2d>{};
  points.reserve(size_t(polygon.size()));
  for (const auto& point : polygon)
  {
    const auto world = transform * region.unproject(point);
    points.emplace_back(world.x(), world.y());
  }
  if (points.size() > 1 && vm::is_equal(points.front(), points.back(), 1e-9))
    points.pop_back();
  auto center = vm::vec2d{};
  for (const auto& point : points)
    center = center + point;
  center = center / double(points.size());

  // The clipped top face is convex. Its minimum horizontal width occurs along
  // a supporting line parallel to one of its edges.
  auto width = std::numeric_limits<double>::infinity();
  for (auto i = size_t{0}; i < points.size(); ++i)
  {
    const auto edge = points[(i + 1) % points.size()] - points[i];
    const auto length = vm::length(edge);
    if (length <= 1e-9)
      continue;
    auto widest = 0.0;
    for (const auto& point : points)
    {
      const auto delta = point - points[i];
      widest =
        std::max(widest, std::abs(edge.x() * delta.y() - edge.y() * delta.x()) / length);
    }
    width = std::min(width, widest);
  }
  return {width, center};
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

struct PlanScore
{
  size_t faces = 0;
  size_t wadBytes = 0;
  bool hasShared = false;
  bool withinBudget = true;
};

std::optional<PlanScore> scorePlan(
  const TerrainBake& plan,
  const std::vector<TerrainSurface>& surfaces,
  const TerrainRegion& region,
  const vm::mat4x4d& transform)
{
  auto score = PlanScore{};
  auto used = std::vector<bool>(plan.textures.size());
  for (const auto& surface : surfaces)
  {
    auto coverage = 0.0;
    for (const auto& face : plan.faces)
    {
      const auto clipped = surface.outline.intersected(face.polygon);
      const auto area = terrainArea(clipped);
      if (area <= 1e-7)
        continue;
      if (face.material >= used.size())
        return std::nullopt;
      coverage += area;
      ++score.faces;
      used[face.material] = true;
      score.hasShared |= face.kind == TerrainFaceKind::Shared;
      if (
        region.minFaceWidth > 0
        && faceWidth(clipped, region, transform).width + 1e-5 < region.minFaceWidth)
        score.withinBudget = false;
    }
    if (std::abs(coverage - terrainArea(surface.outline)) > 1e-4)
      return std::nullopt;
  }
  score.wadBytes = terrainWadBytes(plan, used);
  const auto addedFaces =
    score.faces > surfaces.size() ? score.faces - surfaces.size() : 0;
  if (region.maxAddedFaces >= 0 && addedFaces > size_t(region.maxAddedFaces))
    score.withinBudget = false;
  if (region.maxWadKiB >= 0 && score.wadBytes > size_t(region.maxWadKiB) * 1024)
    score.withinBudget = false;
  return score;
}

bool validPieces(
  const TerrainBake& plan,
  const std::vector<TerrainSurface>& surfaces,
  const TerrainRegion& region,
  const vm::mat4x4d& transform)
{
  const auto inverse = vm::invert(transform);
  if (!inverse)
    return false;
  for (const auto& surface : surfaces)
    for (const auto& face : plan.faces)
      if (
        terrainArea(surface.outline.intersected(face.polygon)) > 1e-7
        && makePiece(
             surface, face, region, transform, *inverse, vm::bbox3d{65536.0}, "tbt0_0")
             .is_error())
        return false;
  return true;
}

Result<TerrainBake> runBaker(
  const TerrainRegion& region,
  const std::vector<TerrainSurface>& surfaces,
  const vm::mat4x4d& transform,
  const project::ProjectStore* store)
{
  if (region.materialAssetId.empty())
    return runTerrainBaker(region);
  if (!store)
    return Error{"The terrain material requires an open project."};
  auto material = loadTerrainMaterial(*store, region.materialAssetId);
  if (material.is_error())
    return std::get<Error>(material.error());
  auto shared = runTerrainBaker(region, &material.value(), TerrainBakeMode::Shared);
  if (shared.is_error())
    return std::get<Error>(shared.error());
  if (std::ranges::none_of(shared.value().faces, [](const auto& face) {
        return face.kind == TerrainFaceKind::Shared;
      }))
    return shared;

  auto local = runTerrainBaker(region, &material.value(), TerrainBakeMode::Local);
  if (local.is_error())
    return std::get<Error>(local.error());
  const auto sharedScore = scorePlan(shared.value(), surfaces, region, transform);
  const auto localScore = scorePlan(local.value(), surfaces, region, transform);
  if (
    sharedScore && localScore && sharedScore->hasShared && sharedScore->withinBudget
    && sharedScore->wadBytes < localScore->wadBytes
    && validPieces(shared.value(), surfaces, region, transform))
    return shared;
  return local;
}

Result<void> bakeGroup(
  mdl::GroupNode& group,
  const vm::bbox3d& bounds,
  TerrainSnapshot& snapshot,
  const size_t groupNumber,
  const project::ProjectStore* store)
{
  const auto region = TerrainRegion::decode(group.group().terrainPaint());
  if (region.is_error())
    return std::get<Error>(region.error());
  const auto surfaces = terrainSurfaces(group, region.value());
  if (surfaces.is_error())
    return std::get<Error>(surfaces.error());
  auto bake =
    runBaker(region.value(), surfaces.value(), group.group().transformation(), store);
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
      const auto clipped = surface.outline.intersected(face.polygon);
      const auto area = terrainArea(clipped);
      if (area <= 1e-7)
        continue;
      if (face.material >= used.size())
        return Error{"Terrain generated an invalid texture reference."};
      if (region.value().minFaceWidth > 0)
      {
        const auto metrics = faceWidth(clipped, region.value(), transform);
        if (metrics.width + 1e-5 < region.value().minFaceWidth)
          return Error{fmt::format(
            "Terrain top face near ({:.1f}, {:.1f}) is {:.1f} units wide, below the "
            "minimum of {}.",
            metrics.center.x(),
            metrics.center.y(),
            metrics.width,
            region.value().minFaceWidth)};
      }
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
  const auto addedFaces = pieces.size() > surfaces.value().size()
                            ? pieces.size() - surfaces.value().size()
                            : size_t{0};
  if (
    region.value().maxAddedFaces >= 0
    && addedFaces > size_t(region.value().maxAddedFaces))
    return Error{fmt::format(
      "Terrain generates {} extra top faces, exceeding the limit of {}.",
      addedFaces,
      region.value().maxAddedFaces)};
  const auto wadBytes = terrainWadBytes(plan, used);
  if (region.value().maxWadKiB >= 0 && wadBytes > size_t(region.value().maxWadKiB) * 1024)
    return Error{fmt::format(
      "Terrain WAD requires {} bytes, exceeding the limit of {} KiB.",
      wadBytes,
      region.value().maxWadKiB)};
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

Result<TerrainEstimate> estimateTerrainGroup(
  const mdl::GroupNode& group, const project::ProjectStore* store)
{
  const auto region = TerrainRegion::decode(group.group().terrainPaint());
  if (region.is_error())
    return std::get<Error>(region.error());
  const auto surfaces = terrainSurfaces(group, region.value());
  if (surfaces.is_error())
    return std::get<Error>(surfaces.error());
  const auto bake =
    runBaker(region.value(), surfaces.value(), group.group().transformation(), store);
  if (bake.is_error())
    return std::get<Error>(bake.error());
  auto result = TerrainEstimate{};
  result.sourceFaces = surfaces.value().size();
  auto used = std::vector<bool>(bake.value().textures.size());
  for (auto surfaceIndex = size_t{0}; surfaceIndex < surfaces.value().size();
       ++surfaceIndex)
  {
    const auto& surface = surfaces.value()[surfaceIndex];
    auto coverage = 0.0;
    for (const auto& face : bake.value().faces)
    {
      const auto clipped = surface.outline.intersected(face.polygon);
      const auto area = terrainArea(clipped);
      if (area <= 1e-7)
        continue;
      if (face.material >= used.size())
        return Error{"Terrain generated an invalid texture reference."};
      const auto metrics =
        faceWidth(clipped, region.value(), group.group().transformation());
      if (result.generatedFaces == 0 || metrics.width < result.narrowestFaceWidth)
      {
        result.narrowestFaceWidth = metrics.width;
        result.narrowestFaceX = metrics.center.x();
        result.narrowestFaceY = metrics.center.y();
      }
      if (metrics.width + 1e-5 < region.value().minFaceWidth)
        ++result.facesBelowMinWidth;
      result.cuts.push_back(
        {surfaceIndex,
         clipped,
         face.kind,
         metrics.width + 1e-5 < region.value().minFaceWidth});
      coverage += area;
      ++result.generatedFaces;
      used[face.material] = true;
    }
    if (std::abs(coverage - terrainArea(surface.outline)) > 1e-4)
      return Error{"The terrain bake did not cover a source brush."};
  }
  result.textures = size_t(std::count(used.begin(), used.end(), true));
  result.wadBytes = terrainWadBytes(bake.value(), used);
  return result;
}

Result<std::optional<TerrainSnapshot>> bakeTerrainSnapshot(
  const std::vector<std::uint8_t>& source,
  const project::ProjectStore* store,
  std::vector<std::string>* dependencies)
{
  if (dependencies)
    dependencies->clear();
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
  if (store && dependencies)
  {
    for (const auto* group : groups)
    {
      const auto region = TerrainRegion::decode(group->group().terrainPaint());
      if (region.is_error() || region.value().materialAssetId.empty())
        continue;
      const auto& id = region.value().materialAssetId;
      dependencies->push_back(id);
      const auto edges = store->dependencies(id);
      if (edges.is_error())
        return std::get<Error>(edges.error());
      for (const auto& edge : edges.value())
        if (edge.kind.starts_with("terrain-slot-"))
          dependencies->push_back(edge.dependencyId);
    }
    std::ranges::sort(*dependencies);
    dependencies->erase(
      std::unique(dependencies->begin(), dependencies->end()), dependencies->end());
  }
  auto snapshot = TerrainSnapshot{};
  for (auto i = size_t{0}; i < groups.size(); ++i)
  {
    const auto result = bakeGroup(*groups[i], bounds, snapshot, i, store);
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
