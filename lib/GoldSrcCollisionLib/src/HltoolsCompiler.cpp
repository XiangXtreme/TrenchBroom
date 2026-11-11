/*
 Copyright (C) 2026 KreedzBroom contributors

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

#include "HltoolsCompiler.h"

#include "collision/collision.h"
#include "format/bsp/entity_lump.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <stdexcept>
#include <unordered_map>

namespace tb::cs
{
namespace
{

constexpr std::array<std::array<float, 3>, 4> HullMaxs = {
  {{0, 0, 0}, {16, 16, 36}, {32, 32, 32}, {16, 16, 18}}};

template <typename Nodes, typename LeafContents>
void importHull(
  BrushModel& model,
  const size_t hullIndex,
  const int head,
  const Nodes& source,
  const std::vector<format::dplane_t>& sourcePlanes,
  const LeafContents& leafContents)
{
  auto& nodes = model.clipnodes[hullIndex];
  auto& planes = model.planes[hullIndex];
  auto planeIndices = std::unordered_map<int, int>{};
  auto indices = std::unordered_map<int, int>{};
  auto pending = std::vector<int>{};
  const auto remap = [&](const int index) {
    if (index < 0)
      return leafContents(index);
    if (index >= static_cast<int>(source.size()))
      throw std::runtime_error("hltools returned an invalid collision node");
    const auto [it, added] = indices.emplace(index, static_cast<int>(indices.size()));
    if (added)
      pending.push_back(index);
    return it->second;
  };
  const int root = remap(head);
  for (size_t i = 0; i < pending.size(); ++i)
  {
    // remap may append to pending; retain values across its reallocation.
    const auto& node = source[static_cast<size_t>(pending[i])];
    auto [it, added] =
      planeIndices.emplace(node.planenum, static_cast<int>(planes.size()));
    if (added)
    {
      const auto& p = sourcePlanes.at(static_cast<size_t>(node.planenum));
      auto plane = ClipPlane{};
      std::copy_n(p.normal, 3, plane.normal);
      plane.dist = p.dist;
      plane.type = static_cast<std::uint8_t>(p.type);
      for (int axis = 0; axis < 3; ++axis)
        if (plane.normal[axis] < 0)
          plane.signbits = static_cast<std::uint8_t>(plane.signbits | (1u << axis));
      planes.push_back(plane);
    }
    const int planeIndex = it->second;
    const int front = remap(node.children[0]);
    const int back = remap(node.children[1]);
    nodes.push_back({planeIndex, {front, back}});
  }
  if (root < 0)
  {
    planes.push_back({{0, 0, 1}, 0, kPlaneZ, 0, {0, 0}});
    nodes.push_back({0, {root, root}});
  }
  // The movement engine treats firstclipnode >= lastclipnode as an absent hull.
  if (nodes.size() == 1)
    nodes.push_back(nodes.front());
  auto& hull = model.hulls[hullIndex];
  hull.clipnodes = nodes.data();
  hull.planes = planes.data();
  hull.firstclipnode = 0;
  hull.lastclipnode = static_cast<int>(nodes.size()) - 1;
  for (size_t axis = 0; axis < 3; ++axis)
  {
    hull.clip_mins[axis] = -HullMaxs[hullIndex][axis];
    hull.clip_maxs[axis] = HullMaxs[hullIndex][axis];
  }
}

void setEntityProperties(BrushModel& model, const format::entity& entity)
{
  std::sscanf(
    entity.value("origin"),
    "%f %f %f",
    &model.origin[0],
    &model.origin[1],
    &model.origin[2]);
  const auto classname = std::string_view{entity.value("classname")};
  if (classname == "func_ladder")
  {
    model.solid = 0;
    model.skin = kContentsLadder;
  }
  else if (classname == "func_water")
  {
    model.solid = 0;
    const auto skin = std::string_view{entity.value("skin")};
    int contents = kContentsWater;
    std::from_chars(skin.data(), skin.data() + skin.size(), contents);
    model.skin =
      contents >= kContentsLava && contents <= kContentsWater ? contents : kContentsWater;
  }
  else if (
    classname.starts_with("trigger_") || classname == "func_illusionary"
    || classname == "func_buyzone" || classname == "func_bomb_target"
    || classname == "func_hostage_rescue" || classname == "func_escapezone"
    || classname == "func_vip_safetyzone" || classname == "func_weaponcheck"
    || classname == "env_bubbles")
  {
    model.solid = 0;
  }
}

} // namespace

HltoolsCompileResult compileHltoolsWorld(
  const std::string_view mapText, const int clipType)
{
  auto compiled = hltools::collision::compile(
    std::string{mapText}, static_cast<hltools::collision::clip_type>(clipType));
  const auto& bsp = compiled.bsp;
  auto result = HltoolsCompileResult{};
  result.warnings = std::move(compiled.warnings);
  for (size_t i = 0; i < bsp.models.size(); ++i)
  {
    const auto& source = bsp.models[i];
    auto model = std::make_unique<BrushModel>();
    model->name = i == 0 ? "world" : "*" + std::to_string(i);
    std::copy_n(source.mins, 3, model->mins);
    std::copy_n(source.maxs, 3, model->maxs);
    importHull(*model, 0, source.headnode[0], bsp.nodes, bsp.planes, [&](const int leaf) {
      return bsp.leafs.empty() ? kContentsEmpty
                               : bsp.leafs.at(static_cast<size_t>(-leaf - 1)).contents;
    });
    for (size_t h = 1; h < 4; ++h)
      importHull(
        *model, h, source.headnode[h], bsp.clipnodes, bsp.planes, [](const int contents) {
          return contents;
        });
    result.models.push_back(std::move(model));
  }
  for (const auto& entity : format::parse_entities(bsp.entities))
  {
    const auto name = std::string_view{entity.value("model")};
    if (name.size() < 2 || name.front() != '*')
      continue;
    size_t index = 0;
    const auto parsed =
      std::from_chars(name.data() + 1, name.data() + name.size(), index);
    if (parsed.ec == std::errc{} && index > 0 && index < result.models.size())
      setEntityProperties(*result.models[index], entity);
  }
  return result;
}

} // namespace tb::cs
