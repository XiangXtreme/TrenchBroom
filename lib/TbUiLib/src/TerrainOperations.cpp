/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/TerrainOperations.h"

#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/EditorContext.h"
#include "mdl/GroupNode.h"
#include "mdl/LinkedGroupUtils.h"
#include "mdl/Map.h"
#include "mdl/Map_Nodes.h"
#include "mdl/NodeContents.h"
#include "mdl/Transaction.h"
#include "mdl/WorldNode.h"
#include "ui/TerrainHeightField.h"
#include "ui/TerrainRegion.h"

#include <algorithm>
#include <vector>

namespace tb::ui
{
Result<void> convertTerrainToHeightField(
  mdl::Map& map, mdl::GroupNode& group, const double spacing)
{
  if (
    !map.editorContext().editable(group) || !map.editorContext().visible(group)
    || std::ranges::any_of(group.children(), [&](const auto* child) {
         return !map.editorContext().editable(*child)
                || !map.editorContext().visible(*child);
       }))
    return Error{"The terrain or one of its brushes is locked or hidden."};
  // Updating the geometry of linked groups also updates their siblings. Until the
  // height field and paint metadata can be synchronized with those siblings in the
  // same transaction, reject the conversion rather than leaving mismatched data.
  for (auto* node = static_cast<mdl::Node*>(&group); node; node = node->parent())
    if (const auto* linked = dynamic_cast<const mdl::GroupNode*>(node);
        linked
        && mdl::collectNodesWithLinkId({&map.worldNode()}, linked->linkId()).size() > 1)
      return Error{"Unlink the terrain group before converting it to a height field."};
  auto regionResult = group.group().terrainPaint().empty()
                        ? createTerrainRegion(group)
                        : TerrainRegion::decode(group.group().terrainPaint());
  if (regionResult.is_error())
    return std::get<Error>(regionResult.error());
  auto region = std::move(regionResult).value();
  auto height = createTerrainHeightField(group, region, spacing);
  if (height.is_error())
    return std::get<Error>(height.error());
  auto generated = createTerrainBrushes(
    height.value(),
    region.origin,
    group.group().transformation(),
    map.worldNode().mapFormat(),
    map.worldBounds(),
    "null");
  if (generated.is_error())
    return std::get<Error>(generated.error());
  auto brushes = std::move(generated).value();

  // Exterior faces keep the original material, surface attributes and Valve UV axes.
  // New internal faces use null. Sampling already proved that every generated top is
  // coplanar with one of the source tops.
  const auto sources = group.children();
  for (auto& brush : brushes)
  {
    auto center = vm::vec3d{};
    const auto vertices = brush.vertexPositions();
    for (const auto& vertex : vertices)
      center = center + vertex;
    center = center / double(vertices.size());
    const auto sourceNode = std::ranges::find_if(sources, [&](const auto* node) {
      return static_cast<const mdl::BrushNode*>(node)->brush().containsPoint(center);
    });
    if (sourceNode == sources.end())
      return Error{"Could not match generated terrain to its source brush."};
    const auto& source = static_cast<const mdl::BrushNode*>(*sourceNode)->brush();
    for (auto& face : brush.faces())
    {
      const auto match = std::ranges::find_if(source.faces(), [&](const auto& candidate) {
        return vm::is_equal(face.boundary(), candidate.boundary(), 1e-6);
      });
      if (match != source.faces().end())
      {
        face.copyAttributes(*match);
        if (const auto snapshot = match->takeUvCoordSystemSnapshot())
          face.restoreUvCoordSystemSnapshot(*snapshot);
      }
    }
  }

  region.heightField = std::move(height).value();
  auto content = group.group();
  content.setTerrainPaint(region.encode());
  auto nodes = std::vector<mdl::Node*>{};
  nodes.reserve(brushes.size());
  for (auto& brush : brushes)
    nodes.push_back(new mdl::BrushNode{std::move(brush)});

  auto transaction = mdl::Transaction{map, "Convert Terrain to Height Field"};
  if (mdl::addNodes(map, {{&group, nodes}}).empty())
  {
    transaction.cancel();
    return Error{"Could not add the terrain geometry."};
  }
  mdl::removeNodes(map, sources);
  if (!mdl::updateNodeContents(
        map,
        "Convert Terrain to Height Field",
        {{&group, mdl::NodeContents{std::move(content)}}}))
  {
    transaction.cancel();
    return Error{"Could not save the terrain data."};
  }
  if (!transaction.commit())
    return Error{"Could not commit the terrain conversion."};
  return {};
}
} // namespace tb::ui
