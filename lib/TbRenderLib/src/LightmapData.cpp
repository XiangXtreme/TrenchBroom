/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "render/LightmapData.h"

#include "mdl/BrushFace.h"
#include "mdl/BrushGeometry.h"
#include "mdl/BrushNode.h"
#include "mdl/EntityNode.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/Node.h"
#include "mdl/PatchNode.h"
#include "mdl/WorldNode.h"

#include "kd/overload.h"

#include "vm/intersection.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>

namespace tb::render
{

namespace
{

constexpr float LuxelSize = 16.0f;
constexpr int AtlasWidth = 1024;
constexpr int AtlasPadding = 1;
constexpr int BleedRounds = 2;
constexpr float PlaneDotThreshold = 0.9999f;
constexpr float PlaneDistThreshold = 0.5f;
constexpr float VecsEpsilon = 1e-3f;
constexpr double TerrainClipEpsilon = 1e-7;

float projectPoint(const vm::vec3f& point, const vm::vec4f& vec)
{
  return vm::dot(point, vec.xyz()) + vec.w();
}

vm::vec2f luxelCoord(const vm::vec3f& point, const vm::vec4f vecs[2])
{
  return {
    projectPoint(point, vecs[0]) / LuxelSize, projectPoint(point, vecs[1]) / LuxelSize};
}

bool sameVecs(const vm::vec4f lhs[2], const vm::vec4f rhs[2])
{
  for (size_t i = 0; i < 2; ++i)
  {
    for (size_t c = 0; c < 4; ++c)
    {
      if (std::abs(lhs[i][c] - rhs[i][c]) > VecsEpsilon)
      {
        return false;
      }
    }
  }
  return true;
}

bool matchesPlane(
  const LightmapFaceRecord& record, const vm::vec3f& normal, const float dist)
{
  return vm::dot(record.normal, normal) > PlaneDotThreshold
         && std::abs(record.dist - dist) < PlaneDistThreshold;
}

/**
 * Crossing number test. The polygon is in luxel units, so callers sample at luxel
 * centers.
 */
bool containsPoint(const std::vector<vm::vec2f>& poly, const float u, const float v)
{
  if (poly.size() < 3)
  {
    return false;
  }

  auto inside = false;
  for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
  {
    const auto& a = poly[i];
    const auto& b = poly[j];
    if ((a.y() > v) != (b.y() > v))
    {
      const auto crossU = (b.x() - a.x()) * (v - a.y()) / (b.y() - a.y()) + a.x();
      if (u < crossU)
      {
        inside = !inside;
      }
    }
  }
  return inside;
}

vm::vec2d terrainLuxelCoord(const vm::vec3d& point, const TerrainLightmapBlock& block)
{
  const auto project = [&](const vm::vec4f& axis) {
    return (vm::dot(point, vm::vec3d{axis.xyz()}) + double(axis.w())) / double(LuxelSize);
  };
  return {project(block.vecs[0]), project(block.vecs[1])};
}

bool hasArea(const std::vector<vm::vec3d>& vertices)
{
  if (vertices.size() < 3)
    return false;

  auto area = vm::vec3d{};
  for (auto i = size_t{1}; i + 1 < vertices.size(); ++i)
    area = area + vm::cross(vertices[i] - vertices[0], vertices[i + 1] - vertices[0]);
  return vm::length(area) > TerrainClipEpsilon;
}

std::optional<double> terrainBlockWinding(
  const TerrainLightmapBlock& block, const std::vector<vm::vec3d>& vertices)
{
  if (block.poly.size() < 3 || vertices.empty())
    return std::nullopt;
  constexpr auto high = std::numeric_limits<double>::max();
  constexpr auto low = std::numeric_limits<double>::lowest();
  auto signedArea = 0.0;
  auto blockMin = vm::vec2d{high, high};
  auto blockMax = vm::vec2d{low, low};
  for (auto i = size_t{0}; i < block.poly.size(); ++i)
  {
    const auto& a = block.poly[i];
    const auto& b = block.poly[(i + 1) % block.poly.size()];
    signedArea += double(a.x()) * double(b.y()) - double(a.y()) * double(b.x());
    blockMin = vm::min(blockMin, vm::vec2d{a});
    blockMax = vm::max(blockMax, vm::vec2d{a});
  }
  if (std::abs(signedArea) < TerrainClipEpsilon)
    return std::nullopt;
  auto faceMin = vm::vec2d{high, high};
  auto faceMax = vm::vec2d{low, low};
  for (const auto& vertex : vertices)
  {
    const auto coord = terrainLuxelCoord(vertex, block);
    faceMin = vm::min(faceMin, coord);
    faceMax = vm::max(faceMax, coord);
  }
  if (
    faceMax.x() <= blockMin.x() || faceMin.x() >= blockMax.x()
    || faceMax.y() <= blockMin.y() || faceMin.y() >= blockMax.y())
    return std::nullopt;
  return signedArea > 0.0 ? 1.0 : -1.0;
}

/** Splits a world-space polygon at an edge in the block's lightmap coordinates. */
std::pair<std::vector<vm::vec3d>, std::vector<vm::vec3d>> splitTerrainPolygon(
  const std::vector<vm::vec3d>& vertices,
  const TerrainLightmapBlock& block,
  const vm::vec2d& edgeStart,
  const vm::vec2d& edge,
  const double winding)
{
  auto inside = std::vector<vm::vec3d>{};
  auto outside = std::vector<vm::vec3d>{};
  const auto edgeLength = vm::length(edge);
  const auto distance = [&](const vm::vec3d& point) {
    const auto coord = terrainLuxelCoord(point, block) - edgeStart;
    const auto value =
      winding * (edge.x() * coord.y() - edge.y() * coord.x()) / edgeLength;
    return std::abs(value) < TerrainClipEpsilon ? 0.0 : value;
  };

  for (auto i = size_t{0}; i < vertices.size(); ++i)
  {
    const auto& a = vertices[i];
    const auto& b = vertices[(i + 1) % vertices.size()];
    const auto da = distance(a);
    const auto db = distance(b);
    if (da >= 0.0)
      inside.push_back(a);
    if (da <= 0.0)
      outside.push_back(a);
    if ((da < 0.0 && db > 0.0) || (da > 0.0 && db < 0.0))
    {
      const auto t = da / (da - db);
      const auto intersection = a + (b - a) * t;
      inside.push_back(intersection);
      outside.push_back(intersection);
    }
  }
  return {std::move(inside), std::move(outside)};
}

vm::polygon3f bakedPolygon(const LightmapFaceRecord& record)
{
  auto outline = std::vector<vm::vec3f>{};
  outline.reserve(record.poly.size());
  for (const auto& coord : record.poly)
  {
    outline.emplace_back(coord.x(), coord.y(), 0.0f);
  }
  return vm::polygon3f{std::move(outline)};
}

bool overlapsFace(
  const LightmapFaceRecord& record,
  const FaceLightmapInput& face,
  const vm::polygon3f& polygon)
{
  auto outline = std::vector<vm::vec3f>{};
  outline.reserve(face.outline.size());
  for (const auto& point : face.outline)
  {
    const auto coord = luxelCoord(point, record.vecs);
    outline.emplace_back(coord.x(), coord.y(), 0.0f);
  }

  return vm::polygons_overlap(
    vm::polygon3f{std::move(outline)}, polygon, vm::vec3f{0.0f, 0.0f, 1.0f});
}

/**
 * Fills unlit luxels from their lit neighbours so that linear filtering does not pick up
 * the surrounding background around the edges of a face.
 */
void bleed(
  std::vector<std::uint8_t>& pixels,
  std::vector<bool>& valid,
  const int width,
  const int height)
{
  for (auto round = 0; round < BleedRounds; ++round)
  {
    auto nextValid = valid;
    for (auto y = 0; y < height; ++y)
    {
      for (auto x = 0; x < width; ++x)
      {
        const auto index =
          static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
        if (valid[index])
        {
          continue;
        }

        auto sum = std::array<int, 3>{0, 0, 0};
        auto count = 0;
        const auto add = [&](const int nx, const int ny) {
          if (nx < 0 || ny < 0 || nx >= width || ny >= height)
          {
            return;
          }
          const auto n = static_cast<size_t>(ny) * static_cast<size_t>(width)
                         + static_cast<size_t>(nx);
          if (!valid[n])
          {
            return;
          }
          for (auto c = 0; c < 3; ++c)
          {
            sum[static_cast<size_t>(c)] += pixels[n * 3 + static_cast<size_t>(c)];
          }
          ++count;
        };
        add(x - 1, y);
        add(x + 1, y);
        add(x, y - 1);
        add(x, y + 1);

        if (count > 0)
        {
          for (auto c = 0; c < 3; ++c)
          {
            pixels[index * 3 + static_cast<size_t>(c)] =
              static_cast<std::uint8_t>(sum[static_cast<size_t>(c)] / count);
          }
          nextValid[index] = true;
        }
      }
    }
    valid = std::move(nextValid);
  }
}

std::vector<FaceLightmapInput> collectBrushFaces(mdl::Map& map)
{
  auto result = std::vector<FaceLightmapInput>{};

  const auto visit = [&](const auto& self, mdl::Node& node) -> void {
    node.accept(kdl::overload(
      [&](mdl::WorldNode& n) {
        for (auto* child : n.children())
        {
          self(self, *child);
        }
      },
      [&](mdl::LayerNode& n) {
        if (n.layer().omitFromExport())
        {
          return;
        }
        for (auto* child : n.children())
        {
          self(self, *child);
        }
      },
      [&](mdl::GroupNode& n) {
        if (!n.group().terrainPaint().empty())
        {
          return;
        }
        for (auto* child : n.children())
        {
          self(self, *child);
        }
      },
      [&](mdl::EntityNode& n) {
        for (auto* child : n.children())
        {
          self(self, *child);
        }
      },
      [&](mdl::BrushNode& n) {
        for (const auto& face : n.brush().faces())
        {
          auto input = FaceLightmapInput{};
          input.plane = face.boundary();
          input.face = &face;
          for (const auto* halfEdge : face.geometry()->boundary())
          {
            input.outline.push_back(vm::vec3f{halfEdge->origin()->position()});
          }
          result.push_back(std::move(input));
        }
      },
      [&](mdl::PatchNode&) {}));
  };

  visit(visit, map.worldNode());
  return result;
}

} // namespace

const LightmapBlock* LightmapAssembly::blockFor(const mdl::BrushFace& face) const
{
  const auto it = std::ranges::lower_bound(
    blocks,
    &face,
    std::less<const mdl::BrushFace*>{},
    &decltype(blocks)::value_type::first);
  return it != blocks.end() && it->first == &face ? &it->second : nullptr;
}

std::vector<const TerrainLightmapBlock*> LightmapAssembly::terrainBlocksFor(
  const vm::plane3d& plane, const std::vector<vm::vec3d>& vertices) const
{
  auto candidates = std::vector<const TerrainLightmapBlock*>{};
  for (const auto& block : terrainBlocks)
    if (
      vm::dot(vm::vec3d{block.normal}, plane.normal) > 0.999
      && std::abs(double(block.dist) - plane.distance) < 0.1
      && terrainBlockWinding(block, vertices))
      candidates.push_back(&block);
  return candidates;
}

std::vector<TerrainLightmapFragment> LightmapAssembly::terrainFragments(
  const vm::plane3d& plane, const std::vector<vm::vec3d>& vertices) const
{
  return terrainFragments(terrainBlocksFor(plane, vertices), vertices);
}

std::vector<TerrainLightmapFragment> LightmapAssembly::terrainFragments(
  const std::vector<const TerrainLightmapBlock*>& candidates,
  const std::vector<vm::vec3d>& vertices) const
{
  auto fragments = std::vector<TerrainLightmapFragment>{};
  if (!hasArea(vertices))
    return fragments;

  // Each BSP face is convex. Subtract it from the remaining convex pieces so that
  // unmatched areas retain their geometry and overlapping records are not drawn twice.
  auto remaining = std::vector<std::vector<vm::vec3d>>{vertices};
  for (const auto* candidate : candidates)
  {
    const auto& block = *candidate;
    const auto winding = terrainBlockWinding(block, vertices);
    if (!winding)
      continue;

    auto next = std::vector<std::vector<vm::vec3d>>{};
    for (auto& piece : remaining)
    {
      auto inside = piece;
      auto outside = std::vector<std::vector<vm::vec3d>>{};
      for (auto i = size_t{0}; i < block.poly.size() && hasArea(inside); ++i)
      {
        const auto a = vm::vec2d{block.poly[i]};
        const auto edge = vm::vec2d{block.poly[(i + 1) % block.poly.size()]} - a;
        if (vm::length(edge) < TerrainClipEpsilon)
          continue;
        auto [clipped, cutOff] = splitTerrainPolygon(inside, block, a, edge, *winding);
        if (hasArea(cutOff))
          outside.push_back(std::move(cutOff));
        inside = std::move(clipped);
      }
      if (hasArea(inside))
      {
        fragments.push_back({std::move(inside), &block});
        for (auto& cutOff : outside)
          next.push_back(std::move(cutOff));
      }
      else
        next.push_back(std::move(piece));
    }
    remaining = std::move(next);
    if (remaining.empty())
      break;
  }

  for (auto& piece : remaining)
    fragments.push_back({std::move(piece), nullptr});
  return fragments;
}

vm::vec2f LightmapAssembly::terrainUv(
  const TerrainLightmapBlock* block, const vm::vec3d& point) const
{
  if (width <= 0 || height <= 0)
    return {0.5f, 0.5f};
  if (!block || block->size.x() <= 0 || block->size.y() <= 0)
    return {0.5f / float(width), 0.5f / float(height)};

  const auto coord = terrainLuxelCoord(point, *block);
  const auto u =
    std::clamp(coord.x() - double(block->mins.x()), 0.0, double(block->size.x() - 1));
  const auto v =
    std::clamp(coord.y() - double(block->mins.y()), 0.0, double(block->size.y() - 1));
  return {
    float((double(block->tile.x()) + u + 0.5) / double(width)),
    float((double(block->tile.y()) + v + 0.5) / double(height))};
}

std::optional<LightmapBlock> buildFaceLightmap(
  const FaceLightmapInput& face,
  const LightmapBake& bake,
  std::vector<std::uint8_t>& pixels)
{
  if (
    bake.width <= 0 || bake.height <= 0 || bake.pixels.empty() || face.outline.size() < 3)
  {
    return std::nullopt;
  }

  const auto normal = vm::vec3f{face.plane.normal};
  const auto dist = static_cast<float>(face.plane.distance);

  auto candidates = std::vector<const LightmapFaceRecord*>{};
  for (const auto& record : bake.faces)
  {
    if (matchesPlane(record, normal, dist))
    {
      candidates.push_back(&record);
    }
  }
  if (candidates.empty())
  {
    return std::nullopt;
  }

  // Prefer the record at the center. The BSP can clip that part of the brush face away,
  // so fall back to a record that overlaps the remaining surface.
  auto centroid = vm::vec3f{};
  for (const auto& point : face.outline)
  {
    centroid = centroid + point;
  }
  centroid = centroid / static_cast<float>(face.outline.size());

  const LightmapFaceRecord* reference = nullptr;
  for (const auto* candidate : candidates)
  {
    const auto coord = luxelCoord(centroid, candidate->vecs);
    if (containsPoint(candidate->poly, coord.x(), coord.y()))
    {
      reference = candidate;
      break;
    }
  }

  if (!reference)
  {
    const auto overlapping = std::ranges::find_if(candidates, [&](const auto* candidate) {
      return candidate->poly.size() >= 3
             && overlapsFace(*candidate, face, bakedPolygon(*candidate));
    });
    if (overlapping == candidates.end())
    {
      return std::nullopt;
    }
    reference = *overlapping;
  }

  auto group = std::vector<const LightmapFaceRecord*>{};
  for (const auto* candidate : candidates)
  {
    if (sameVecs(candidate->vecs, reference->vecs))
    {
      group.push_back(candidate);
    }
  }

  auto minU = std::numeric_limits<float>::max();
  auto minV = std::numeric_limits<float>::max();
  auto maxU = std::numeric_limits<float>::lowest();
  auto maxV = std::numeric_limits<float>::lowest();
  for (const auto& point : face.outline)
  {
    const auto coord = luxelCoord(point, reference->vecs);
    minU = std::min(minU, coord.x());
    minV = std::min(minV, coord.y());
    maxU = std::max(maxU, coord.x());
    maxV = std::max(maxV, coord.y());
  }

  auto block = LightmapBlock{};
  block.vecs[0] = reference->vecs[0];
  block.vecs[1] = reference->vecs[1];
  block.mins =
    vm::vec2i{static_cast<int>(std::floor(minU)), static_cast<int>(std::floor(minV))};
  const auto maxs =
    vm::vec2i{static_cast<int>(std::ceil(maxU)), static_cast<int>(std::ceil(maxV))};
  block.size = maxs - block.mins + vm::vec2i{1, 1};
  if (block.size.x() <= 0 || block.size.y() <= 0)
  {
    return std::nullopt;
  }

  const auto luxelCount =
    static_cast<size_t>(block.size.x()) * static_cast<size_t>(block.size.y());
  pixels.assign(luxelCount * 3, 0);
  auto valid = std::vector<bool>(luxelCount, false);

  const auto copySamples = [&](
                             const LightmapFaceRecord& record, const auto& coversLuxel) {
    const auto start = vm::max(block.mins, record.mins);
    const auto end = vm::min(block.mins + block.size, record.mins + record.size);
    for (auto y = start.y(); y < end.y(); ++y)
    {
      for (auto x = start.x(); x < end.x(); ++x)
      {
        const auto index =
          static_cast<size_t>(y - block.mins.y()) * static_cast<size_t>(block.size.x())
          + static_cast<size_t>(x - block.mins.x());
        if (valid[index] || !coversLuxel(static_cast<float>(x), static_cast<float>(y)))
        {
          continue;
        }

        const auto source = (static_cast<size_t>(record.tile.y() + y - record.mins.y())
                               * static_cast<size_t>(bake.width)
                             + static_cast<size_t>(record.tile.x() + x - record.mins.x()))
                            * 3;
        if (source + 2 >= bake.pixels.size())
        {
          continue;
        }

        pixels[index * 3 + 0] = bake.pixels[source + 0];
        pixels[index * 3 + 1] = bake.pixels[source + 1];
        pixels[index * 3 + 2] = bake.pixels[source + 2];
        valid[index] = true;
      }
    }
  };

  for (const auto* record : group)
  {
    copySamples(*record, [&](const float u, const float v) {
      return containsPoint(record->poly, u + 0.5f, v + 0.5f);
    });
  }

  // Fill missing coverage for narrow fragments first. Then keep the surrounding
  // baked samples used by bilinear filtering along a BSP polygon's edges.
  auto overlapping = std::vector<std::pair<const LightmapFaceRecord*, vm::polygon3f>>{};
  for (const auto* record : group)
  {
    if (record->poly.size() >= 3)
    {
      auto polygon = bakedPolygon(*record);
      if (overlapsFace(*record, face, polygon))
      {
        overlapping.emplace_back(record, std::move(polygon));
      }
    }
  }

  for (const auto minOffset : {0.0f, -1.0f})
  {
    for (const auto& [record, polygon] : overlapping)
    {
      copySamples(*record, [&](const float u, const float v) {
        const auto cell = vm::polygon3f{
          {u + minOffset, v + minOffset, 0.0f},
          {u + 1.0f, v + minOffset, 0.0f},
          {u + 1.0f, v + 1.0f, 0.0f},
          {u + minOffset, v + 1.0f, 0.0f}};
        return vm::polygons_overlap(cell, polygon, vm::vec3f{0.0f, 0.0f, 1.0f});
      });
    }
  }

  if (std::ranges::none_of(valid, [](const bool value) { return value; }))
  {
    return std::nullopt;
  }

  bleed(pixels, valid, block.size.x(), block.size.y());

  return block;
}

void buildLightmapAssembly(
  const std::vector<FaceLightmapInput>& faces,
  const LightmapBake& bake,
  LightmapAssembly& result)
{
  result = LightmapAssembly{};
  if (bake.width <= 0 || bake.height <= 0 || bake.pixels.empty())
  {
    return;
  }

  struct PendingBlock
  {
    const mdl::BrushFace* face = nullptr;
    LightmapBlock block;
    std::vector<std::uint8_t> pixels;
    const LightmapFaceRecord* terrainRecord = nullptr;
  };

  auto pending = std::vector<PendingBlock>{};
  pending.reserve(faces.size());
  for (const auto& face : faces)
  {
    auto pixels = std::vector<std::uint8_t>{};
    if (auto block = buildFaceLightmap(face, bake, pixels))
    {
      pending.push_back({face.face, *block, std::move(pixels), nullptr});
    }
  }

  for (const auto& record : bake.faces)
  {
    // A compiled face may cover both an ordinary brush and terrain. Keep every
    // upward-facing record available to the terrain renderer even if a brush used it.
    if (
      record.normal.z() <= 0.0f || record.size.x() <= 0 || record.size.y() <= 0
      || record.poly.size() < 3)
    {
      continue;
    }

    auto pixels = std::vector<std::uint8_t>(
      static_cast<size_t>(record.size.x()) * static_cast<size_t>(record.size.y()) * 3u);
    for (auto row = 0; row < record.size.y(); ++row)
    {
      const auto source =
        (static_cast<size_t>(record.tile.y() + row) * static_cast<size_t>(bake.width)
         + static_cast<size_t>(record.tile.x()))
        * 3u;
      const auto destination =
        static_cast<size_t>(row) * static_cast<size_t>(record.size.x()) * 3u;
      if (source + static_cast<size_t>(record.size.x()) * 3u <= bake.pixels.size())
      {
        std::copy_n(
          bake.pixels.begin() + static_cast<std::ptrdiff_t>(source),
          static_cast<size_t>(record.size.x()) * 3u,
          pixels.begin() + static_cast<std::ptrdiff_t>(destination));
      }
    }

    auto block = LightmapBlock{};
    block.vecs[0] = record.vecs[0];
    block.vecs[1] = record.vecs[1];
    block.mins = record.mins;
    block.size = record.size;
    pending.push_back({nullptr, block, std::move(pixels), &record});
  }

  if (pending.empty())
  {
    return;
  }

  auto widest = 0;
  for (const auto& block : pending)
  {
    widest = std::max(widest, block.block.size.x());
  }
  result.width = std::max(AtlasWidth, widest + (2 * AtlasPadding));

  std::ranges::stable_sort(pending, [](const auto& lhs, const auto& rhs) {
    return lhs.block.size.y() > rhs.block.size.y();
  });

  auto x = AtlasPadding;
  auto y = AtlasPadding;
  auto rowHeight = 0;
  for (auto& block : pending)
  {
    if (x + block.block.size.x() + AtlasPadding > result.width)
    {
      x = AtlasPadding;
      y += rowHeight + AtlasPadding;
      rowHeight = 0;
    }
    block.block.tile = vm::vec2i{x, y};
    x += block.block.size.x() + AtlasPadding;
    rowHeight = std::max(rowHeight, block.block.size.y());
  }
  result.height = y + rowHeight + AtlasPadding;

  result.pixels.assign(
    static_cast<size_t>(result.width) * static_cast<size_t>(result.height) * 3, 0);
  // Packing starts one pixel in, so the first texel belongs to no block. Leave it white
  // and faces the bake does not cover can point at it to stay unlit.
  result.pixels[0] = 255;
  result.pixels[1] = 255;
  result.pixels[2] = 255;

  for (const auto& block : pending)
  {
    for (auto row = 0; row < block.block.size.y(); ++row)
    {
      const auto destination = (static_cast<size_t>(block.block.tile.y() + row)
                                  * static_cast<size_t>(result.width)
                                + static_cast<size_t>(block.block.tile.x()))
                               * 3;
      const auto source =
        static_cast<size_t>(row) * static_cast<size_t>(block.block.size.x()) * 3;
      std::copy_n(
        block.pixels.begin() + static_cast<std::ptrdiff_t>(source),
        static_cast<size_t>(block.block.size.x()) * 3,
        result.pixels.begin() + static_cast<std::ptrdiff_t>(destination));
    }
    if (block.terrainRecord)
    {
      result.terrainBlocks.push_back(TerrainLightmapBlock{
        block.terrainRecord->normal,
        block.terrainRecord->dist,
        {block.block.vecs[0], block.block.vecs[1]},
        block.block.mins,
        block.block.size,
        block.block.tile,
        block.terrainRecord->poly});
    }
    else
    {
      result.blocks.emplace_back(block.face, block.block);
    }
  }

  std::ranges::sort(
    result.blocks,
    std::less<const mdl::BrushFace*>{},
    &decltype(result.blocks)::value_type::first);
}

void buildLightmapAssembly(
  const mdl::Map& map, const LightmapBake& bake, LightmapAssembly& result)
{
  // The map is only read, but walking its nodes needs the non-const node interface.
  auto& mutableMap = const_cast<mdl::Map&>(map);
  buildLightmapAssembly(collectBrushFaces(mutableMap), bake, result);
}

} // namespace tb::render
