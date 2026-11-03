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

float projectPoint(const vm::vec3f& point, const vm::vec4f& vec)
{
  return vm::dot(point, vec.xyz()) + vec.w();
}

vm::vec2f luxelCoord(const vm::vec3f& point, const vm::vec4f vecs[2])
{
  return {projectPoint(point, vecs[0]) / LuxelSize,
          projectPoint(point, vecs[1]) / LuxelSize};
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
 * Crossing number test. The polygon is in luxel units, so callers sample at luxel centers.
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

bool overlapsFace(const LightmapFaceRecord& record, const FaceLightmapInput& face)
{
  auto outline = std::vector<vm::vec3f>{};
  outline.reserve(face.outline.size());
  for (const auto& point : face.outline)
  {
    const auto coord = luxelCoord(point, record.vecs);
    outline.emplace_back(coord.x(), coord.y(), 0.0f);
  }

  return vm::polygons_overlap(
    vm::polygon3f{std::move(outline)},
    bakedPolygon(record),
    vm::vec3f{0.0f, 0.0f, 1.0f});
}

/**
 * Fills unlit luxels from their lit neighbours so that linear filtering does not pick up
 * the surrounding background around the edges of a face.
 */
void bleed(std::vector<std::uint8_t>& pixels, std::vector<bool>& valid, const int width,
           const int height)
{
  for (auto round = 0; round < BleedRounds; ++round)
  {
    auto nextValid = valid;
    for (auto y = 0; y < height; ++y)
    {
      for (auto x = 0; x < width; ++x)
      {
        const auto index = static_cast<size_t>(y) * static_cast<size_t>(width)
                           + static_cast<size_t>(x);
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
        for (auto* child : n.children())
        {
          self(self, *child);
        }
      },
      [&](mdl::GroupNode& n) {
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
  for (const auto& [blockFace, block] : blocks)
  {
    if (blockFace == &face)
    {
      return &block;
    }
  }
  return nullptr;
}

std::optional<LightmapBlock> buildFaceLightmap(
  const FaceLightmapInput& face,
  const LightmapBake& bake,
  std::vector<std::uint8_t>& pixels)
{
  if (bake.width <= 0 || bake.height <= 0 || bake.pixels.empty()
      || face.outline.size() < 3)
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
      return candidate->poly.size() >= 3 && overlapsFace(*candidate, face);
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

  const auto copySamples = [&](const LightmapFaceRecord& record, const auto& coversLuxel) {
    const auto start = vm::max(block.mins, record.mins);
    const auto end = vm::min(block.mins + block.size, record.mins + record.size);
    for (auto y = start.y(); y < end.y(); ++y)
    {
      for (auto x = start.x(); x < end.x(); ++x)
      {
        const auto index = static_cast<size_t>(y - block.mins.y())
                             * static_cast<size_t>(block.size.x())
                           + static_cast<size_t>(x - block.mins.x());
        if (valid[index] || !coversLuxel(static_cast<float>(x), static_cast<float>(y)))
        {
          continue;
        }

        const auto source =
          (static_cast<size_t>(record.tile.y() + y - record.mins.y())
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

  // A narrow BSP fragment can cross a luxel without containing its center. Fill
  // uncovered samples after checking every record for center coverage.
  for (const auto* record : group)
  {
    if (record->poly.size() < 3 || !overlapsFace(*record, face))
    {
      continue;
    }

    const auto polygon = bakedPolygon(*record);
    copySamples(*record, [&](const float u, const float v) {
      const auto cell = vm::polygon3f{
        {u, v, 0.0f},
        {u + 1.0f, v, 0.0f},
        {u + 1.0f, v + 1.0f, 0.0f},
        {u, v + 1.0f, 0.0f}};
      return vm::polygons_overlap(cell, polygon, vm::vec3f{0.0f, 0.0f, 1.0f});
    });
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
  };

  auto pending = std::vector<PendingBlock>{};
  pending.reserve(faces.size());
  for (const auto& face : faces)
  {
    auto pixels = std::vector<std::uint8_t>{};
    if (auto block = buildFaceLightmap(face, bake, pixels))
    {
      pending.push_back({face.face, *block, std::move(pixels)});
    }
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
      const auto destination =
        (static_cast<size_t>(block.block.tile.y() + row)
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
    result.blocks.emplace_back(block.face, block.block);
  }
}

void buildLightmapAssembly(
  const mdl::Map& map, const LightmapBake& bake, LightmapAssembly& result)
{
  // The map is only read, but walking its nodes needs the non-const node interface.
  auto& mutableMap = const_cast<mdl::Map&>(map);
  buildLightmapAssembly(collectBrushFaces(mutableMap), bake, result);
}

} // namespace tb::render
