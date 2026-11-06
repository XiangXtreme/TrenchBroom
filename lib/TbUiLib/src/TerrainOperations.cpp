/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/TerrainOperations.h"

#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/EditorContext.h"
#include "mdl/Group.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/LinkedGroupUtils.h"
#include "mdl/Map.h"
#include "mdl/Map_Groups.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/NodeContents.h"
#include "mdl/Transaction.h"
#include "mdl/WorldNode.h"
#include "ui/TerrainChunks.h"
#include "ui/TerrainHeightField.h"
#include "ui/TerrainRegion.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace tb::ui
{
namespace
{
std::optional<std::pair<int, int>> terrainChunkCells(const double spacing)
{
  const auto columns =
    std::floor(TerrainRegion::Width * TerrainRegion::UnitsPerTexel / spacing);
  const auto rows =
    std::floor(TerrainRegion::Height * TerrainRegion::UnitsPerTexel / spacing);
  if (
    !std::isfinite(columns) || !std::isfinite(rows) || columns < 1 || rows < 1
    || columns > std::numeric_limits<int>::max()
    || rows > std::numeric_limits<int>::max())
    return std::nullopt;
  const auto paintStep = TerrainRegion::Step * TerrainRegion::UnitsPerTexel;
  if (
    std::abs(columns * spacing / paintStep - std::round(columns * spacing / paintStep))
      > 1e-6
    || std::abs(rows * spacing / paintStep - std::round(rows * spacing / paintStep))
         > 1e-6)
    return std::nullopt;
  return std::pair{int(columns), int(rows)};
}

Result<void> checkTerrainEditable(mdl::Map& map, mdl::GroupNode& group)
{
  if (!group.isDescendantOf(map.worldNode()))
    return Error{"The terrain does not belong to this map."};
  const auto editable = [&](const auto& self, const mdl::Node& node) -> bool {
    return map.editorContext().editable(node) && map.editorContext().visible(node)
           && std::ranges::all_of(
             node.children(), [&](const auto* child) { return self(self, *child); });
  };
  if (!editable(editable, group))
    return Error{"The terrain or one of its brushes is locked or hidden."};
  // Updating the geometry of linked groups also updates their siblings. Until the
  // height field and paint metadata can be synchronized with those siblings in the
  // same transaction, reject the conversion rather than leaving mismatched data.
  for (auto* node = static_cast<mdl::Node*>(&group); node; node = node->parent())
    if (const auto* linked = dynamic_cast<const mdl::GroupNode*>(node);
        linked
        && mdl::collectNodesWithLinkId({&map.worldNode()}, linked->linkId()).size() > 1)
      return Error{"Unlink the terrain group before editing its height field."};
  return {};
}

void copyTerrainFaceAttributes(mdl::BrushFace& destination, const mdl::BrushFace& source)
{
  destination.copyAttributes(source);
  if (const auto snapshot = source.takeUvCoordSystemSnapshot())
    destination.restoreUvCoordSystemSnapshot(*snapshot);
}

Result<void> convertTerrainToChunks(
  mdl::Map& map,
  mdl::GroupNode& group,
  const TerrainRegion& region,
  std::vector<mdl::Brush> brushes)
{
  const auto& field = *region.heightField;
  const auto cells = terrainChunkCells(field.spacing);
  if (!cells)
    return Error{"Grid spacing must align terrain chunk paint boundaries."};
  const auto [chunkColumns, chunkRows] = *cells;
  auto owned = std::vector<std::unique_ptr<mdl::GroupNode>>{};
  auto nodes = std::vector<mdl::Node*>{};
  for (auto row = 0; row < field.rows; row += chunkRows)
    for (auto column = 0; column < field.columns; column += chunkColumns)
    {
      const auto columns = std::min(chunkColumns, field.columns - column);
      const auto rows = std::min(chunkRows, field.rows - row);
      auto part = std::make_unique<TerrainRegion>(region);
      part->origin = {
        region.origin.x() + column * field.spacing,
        region.origin.y() - row * field.spacing};
      auto created = TerrainHeightField::create(
        columns, rows, field.spacing, field.bottom, field.bottom + 1.0);
      if (created.is_error())
        return std::get<Error>(created.error());
      auto partField = std::move(created).value();
      for (auto y = 0; y <= rows; ++y)
        for (auto x = 0; x <= columns; ++x)
          partField.heights[partField.index(x, y)] =
            field.heights[field.index(column + x, row + y)];
      part->heightField = std::move(partField);
      auto content = mdl::Group{"Terrain Chunk"};
      content.setTransformation(group.group().transformation());
      content.setTerrainPaint(part->encode());
      auto child = std::make_unique<mdl::GroupNode>(std::move(content));
      for (auto y = 0; y < rows; ++y)
        for (auto x = 0; x < columns; ++x)
          for (auto triangle = size_t{0}; triangle < 2; ++triangle)
          {
            const auto index =
              (size_t(row + y) * size_t(field.columns) + size_t(column + x)) * 2
              + triangle;
            child->addChild(new mdl::BrushNode{std::move(brushes[index])});
          }
      nodes.push_back(child.get());
      owned.push_back(std::move(child));
    }

  const auto sources = group.children();
  auto transaction = mdl::Transaction{map, "Convert Terrain to Height Field"};
  for (auto& child : owned)
    child.release(); // The add command owns the nodes, including on failure.
  if (mdl::addNodes(map, {{&group, nodes}}).empty())
  {
    transaction.cancel();
    return Error{"Could not add the terrain geometry."};
  }
  mdl::removeNodes(map, sources);
  auto content = group.group();
  content.setTerrainPaint(terrainChunkGroupMarker());
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
} // namespace

Result<void> convertTerrainToHeightField(
  mdl::Map& map, mdl::GroupNode& group, const double spacing)
{
  const auto editable = checkTerrainEditable(map, group);
  if (editable.is_error())
    return std::get<Error>(editable.error());
  const auto unpainted = group.group().terrainPaint().empty();
  auto regionResult = unpainted ? createTerrainRegion(group, true)
                                : TerrainRegion::decode(group.group().terrainPaint());
  if (regionResult.is_error())
    return std::get<Error>(regionResult.error());
  auto region = std::move(regionResult).value();
  auto height = createTerrainHeightField(group, region, spacing, unpainted);
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
        copyTerrainFaceAttributes(face, *match);
    }
  }

  region.heightField = std::move(height).value();
  if (
    region.heightField->columns * region.heightField->spacing
      > TerrainRegion::Width * TerrainRegion::UnitsPerTexel
    || region.heightField->rows * region.heightField->spacing
         > TerrainRegion::Height * TerrainRegion::UnitsPerTexel)
    return convertTerrainToChunks(map, group, region, std::move(brushes));
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

Result<void> convertSelectedTerrainToHeightField(mdl::Map& map, const double spacing)
{
  const auto& selection = map.selection();
  if (selection.nodes.size() == 1 && selection.groups.size() == 1)
    return convertTerrainToHeightField(map, *selection.groups.front(), spacing);
  if (selection.nodes.empty())
  {
    if (auto* group = map.editorContext().currentGroup())
      return convertTerrainToHeightField(map, *group, spacing);
    return Error{"Select a ground brush or group to create terrain."};
  }
  if (selection.brushes.empty() || selection.nodes.size() != selection.brushes.size())
    return Error{"Select only ground brushes or a single brush group."};
  if (std::ranges::any_of(selection.brushes, [&](const auto* brush) {
        return !map.editorContext().editable(*brush)
               || !map.editorContext().visible(*brush);
      }))
    return Error{"The selected ground is locked or hidden."};

  auto transaction = mdl::Transaction{map, "Create Sculptable Terrain"};
  auto* group = mdl::groupSelectedNodes(map, "Terrain");
  if (!group)
  {
    transaction.cancel();
    return Error{"Could not group the selected ground."};
  }
  const auto converted = convertTerrainToHeightField(map, *group, spacing);
  if (converted.is_error())
  {
    transaction.cancel();
    return std::get<Error>(converted.error());
  }
  if (!transaction.commit())
    return Error{"Could not commit the terrain conversion."};
  return {};
}

Result<void> createTerrainRectangle(
  mdl::Map& map,
  const vm::vec2d& first,
  const vm::vec2d& second,
  const double height,
  const double thickness,
  const double spacing)
{
  const auto minX = std::min(first.x(), second.x());
  const auto maxX = std::max(first.x(), second.x());
  const auto minY = std::min(first.y(), second.y());
  const auto maxY = std::max(first.y(), second.y());
  const auto width = maxX - minX;
  const auto depth = maxY - minY;
  if (
    !std::isfinite(minX) || !std::isfinite(maxX) || !std::isfinite(minY)
    || !std::isfinite(maxY) || !std::isfinite(height) || !std::isfinite(thickness)
    || !std::isfinite(spacing) || spacing <= 0 || thickness < 1 || width <= 0
    || depth <= 0)
    return Error{"Invalid terrain rectangle, height, thickness or grid spacing."};
  if (
    width > TerrainRegion::Width * TerrainRegion::UnitsPerTexel
    || depth > TerrainRegion::Height * TerrainRegion::UnitsPerTexel)
    return Error{"Terrain rectangle exceeds the 1280 by 640 unit region."};
  const auto columns = std::round(width / spacing);
  const auto rows = std::round(depth / spacing);
  if (
    columns < 1 || rows < 1 || columns > 4096 || rows > 4096
    || std::abs(width - columns * spacing) > 1e-6
    || std::abs(depth - rows * spacing) > 1e-6)
    return Error{"Terrain rectangle dimensions must align with the grid spacing."};
  if (columns * rows * 2.0 > MaxTerrainBrushes)
    return Error{"Terrain would create too many brushes; increase the grid spacing."};
  auto field = TerrainHeightField::create(
    int(columns), int(rows), spacing, height - thickness, height);
  if (field.is_error())
    return std::get<Error>(field.error());

  auto* parent = map.editorContext().currentGroup()
                   ? static_cast<mdl::Node*>(map.editorContext().currentGroup())
                   : static_cast<mdl::Node*>(map.editorContext().currentLayer());
  if (
    !parent || !map.editorContext().editable(*parent)
    || !map.editorContext().visible(*parent))
    return Error{"The current layer or group is locked or hidden."};
  for (auto* node = parent; node; node = node->parent())
    if (const auto* linked = dynamic_cast<const mdl::GroupNode*>(node);
        linked
        && mdl::collectNodesWithLinkId({&map.worldNode()}, linked->linkId()).size() > 1)
      return Error{"Unlink the terrain group before editing its height field."};

  auto region = TerrainRegion{{minX, maxY}};
  region.heightField = std::move(field).value();
  auto brushes = createTerrainBrushes(
    *region.heightField,
    region.origin,
    vm::mat4x4d::identity(),
    map.worldNode().mapFormat(),
    map.worldBounds(),
    "null");
  if (brushes.is_error())
    return std::get<Error>(brushes.error());
  auto material = map.currentMaterialName();
  if (material.empty())
    material = "null";
  auto content = mdl::Group{"Terrain"};
  content.setTerrainPaint(region.encode());
  auto* group = new mdl::GroupNode{std::move(content)};
  auto generated = std::move(brushes).value();
  for (auto& brush : generated)
  {
    for (auto& face : brush.faces())
    {
      const auto vertices = face.vertexPositions();
      const auto onBoundary = [&](const size_t axis, const double coordinate) {
        return std::ranges::all_of(vertices, [&](const auto& vertex) {
          return std::abs(vertex[axis] - coordinate) < 1e-6;
        });
      };
      if (
        std::abs(face.normal().z()) > 1e-6 || onBoundary(0, minX) || onBoundary(0, maxX)
        || onBoundary(1, minY) || onBoundary(1, maxY))
        face.setMaterialName(material);
    }
    group->addChild(new mdl::BrushNode{std::move(brush)});
  }
  auto transaction = mdl::Transaction{map, "Create Terrain"};
  mdl::deselectAll(map);
  if (mdl::addNodes(map, {{parent, {group}}}).empty())
  {
    transaction.cancel();
    return Error{"Could not add the terrain geometry."};
  }
  mdl::selectNodes(map, {group});
  if (!transaction.commit())
    return Error{"Could not commit the terrain creation."};
  return {};
}

Result<void> createTerrainChunkRectangle(
  mdl::Map& map,
  const vm::vec2d& first,
  const vm::vec2d& second,
  const double height,
  const double thickness,
  const double spacing)
{
  const auto minX = std::min(first.x(), second.x());
  const auto maxX = std::max(first.x(), second.x());
  const auto minY = std::min(first.y(), second.y());
  const auto maxY = std::max(first.y(), second.y());
  if (
    !std::isfinite(minX) || !std::isfinite(maxX) || !std::isfinite(minY)
    || !std::isfinite(maxY) || !std::isfinite(spacing) || spacing <= 0.0
    || !std::isfinite(height) || !std::isfinite(thickness) || thickness < 1.0)
    return Error{"Invalid terrain rectangle, height, thickness or grid spacing."};
  const auto columns = (maxX - minX) / spacing;
  const auto rows = (maxY - minY) / spacing;
  if (
    !std::isfinite(columns) || !std::isfinite(rows) || columns < 1 || rows < 1
    || columns > 4096 || rows > 4096 || std::abs(columns - std::round(columns)) > 1e-6
    || std::abs(rows - std::round(rows)) > 1e-6)
    return Error{"Terrain rectangle dimensions must align with the grid spacing."};
  if (columns * rows * 2.0 > MaxTerrainBrushes)
    return Error{"Terrain would create too many brushes; increase the grid spacing."};
  const auto cells = terrainChunkCells(spacing);
  if (!cells)
    return Error{"Grid spacing must align terrain chunk paint boundaries."};
  const auto [chunkColumns, chunkRows] = *cells;

  auto transaction = mdl::Transaction{map, "Create Terrain"};
  auto groups = std::vector<mdl::Node*>{};
  const auto width = int(std::round(columns));
  const auto depth = int(std::round(rows));
  for (auto row = 0; row < depth; row += chunkRows)
    for (auto column = 0; column < width; column += chunkColumns)
    {
      const auto left = minX + column * spacing;
      const auto right = minX + std::min(column + chunkColumns, width) * spacing;
      const auto top = maxY - row * spacing;
      const auto bottom = maxY - std::min(row + chunkRows, depth) * spacing;
      const auto result = createTerrainRectangle(
        map, {left, bottom}, {right, top}, height, thickness, spacing);
      if (result.is_error())
      {
        transaction.cancel();
        return std::get<Error>(result.error());
      }
      if (map.selection().groups.size() != 1)
      {
        transaction.cancel();
        return Error{"Could not create the terrain chunks."};
      }
      groups.push_back(map.selection().groups.front());
    }
  mdl::deselectAll(map);
  mdl::selectNodes(map, groups);
  auto* root = mdl::groupSelectedNodes(map, "Terrain");
  if (!root)
  {
    transaction.cancel();
    return Error{"Could not group the terrain chunks."};
  }
  auto content = root->group();
  content.setTerrainPaint(terrainChunkGroupMarker());
  if (!mdl::updateNodeContents(
        map, "Create Terrain", {{root, mdl::NodeContents{std::move(content)}}}))
  {
    transaction.cancel();
    return Error{"Could not save the terrain data."};
  }
  if (!transaction.commit())
    return Error{"Could not commit the terrain creation."};
  return {};
}

namespace
{
using TerrainChanges = std::vector<std::pair<mdl::Node*, mdl::NodeContents>>;

Result<TerrainChanges> terrainHeightFieldChanges(
  mdl::Map& map, mdl::GroupNode& group, const TerrainHeightField& heights)
{
  const auto editable = checkTerrainEditable(map, group);
  if (editable.is_error())
    return std::get<Error>(editable.error());
  auto decoded = TerrainRegion::decode(group.group().terrainPaint());
  if (decoded.is_error())
    return std::get<Error>(decoded.error());
  auto region = std::move(decoded).value();
  if (!region.heightField || !heights.valid())
    return Error{"Invalid sculptable terrain height field."};
  const auto& previous = *region.heightField;
  if (
    previous.columns != heights.columns || previous.rows != heights.rows
    || previous.spacing != heights.spacing || previous.bottom != heights.bottom)
    return Error{"Changing the terrain grid requires recreating the terrain."};
  const auto childIndices = terrainHeightFieldBrushIndices(group, region);
  if (childIndices.is_error())
    return std::get<Error>(childIndices.error());

  auto triangles = std::vector<size_t>{};
  const auto changed = [&](const int column, const int row) {
    const auto index = previous.index(column, row);
    return previous.heights[index] != heights.heights[index];
  };
  for (auto row = 0; row < heights.rows; ++row)
    for (auto column = 0; column < heights.columns; ++column)
    {
      const auto a = changed(column, row);
      const auto b = changed(column + 1, row);
      const auto c = changed(column + 1, row + 1);
      const auto d = changed(column, row + 1);
      const auto first = (size_t(row) * size_t(heights.columns) + size_t(column)) * 2;
      if (a || b || c)
        triangles.push_back(first);
      if (a || c || d)
        triangles.push_back(first + 1);
    }
  if (triangles.empty())
    return TerrainChanges{};
  auto generated = createTerrainBrushes(
    heights,
    region.origin,
    group.group().transformation(),
    map.worldNode().mapFormat(),
    map.worldBounds(),
    "null",
    triangles);
  if (generated.is_error())
    return std::get<Error>(generated.error());
  auto brushes = std::move(generated).value();
  auto changes = TerrainChanges{};
  changes.reserve(brushes.size() + 1);
  for (auto i = size_t{0}; i < triangles.size(); ++i)
  {
    auto* node =
      static_cast<mdl::BrushNode*>(group.children()[childIndices.value()[triangles[i]]]);
    const auto& source = node->brush();
    const auto oldTop = std::ranges::find_if(
      source.faces(), [](const auto& face) { return face.normal().z() > 1e-6; });
    if (oldTop == source.faces().end())
      return Error{"Terrain brush is missing its top face."};
    for (auto& face : brushes[i].faces())
    {
      const auto match =
        face.normal().z() > 1e-6
          ? oldTop
          : std::ranges::find_if(source.faces(), [&](const auto& candidate) {
              return vm::is_equal(face.boundary(), candidate.boundary(), 1e-6);
            });
      if (match != source.faces().end())
        copyTerrainFaceAttributes(face, *match);
    }
    changes.emplace_back(node, mdl::NodeContents{std::move(brushes[i])});
  }
  region.heightField = heights;
  auto content = group.group();
  content.setTerrainPaint(region.encode());
  changes.emplace_back(&group, mdl::NodeContents{std::move(content)});
  return changes;
}
} // namespace

Result<void> updateTerrainHeightField(
  mdl::Map& map, mdl::GroupNode& group, const TerrainHeightField& heights)
{
  auto changes = TerrainChanges{};
  if (isTerrainChunkGroup(group))
  {
    const auto editable = checkTerrainEditable(map, group);
    if (editable.is_error())
      return std::get<Error>(editable.error());
    auto decoded = readTerrainChunks(group);
    if (decoded.is_error())
      return std::get<Error>(decoded.error());
    const auto& combined = *decoded.value().combined->heightField;
    if (
      !heights.valid() || combined.columns != heights.columns
      || combined.rows != heights.rows || combined.spacing != heights.spacing
      || combined.bottom != heights.bottom)
      return Error{"Changing the terrain grid requires recreating the terrain."};
    for (const auto& chunk : decoded.value().chunks)
    {
      auto field = *chunk.region.heightField;
      for (auto row = 0; row <= field.rows; ++row)
        for (auto column = 0; column <= field.columns; ++column)
          field.heights[field.index(column, row)] =
            heights.heights[heights.index(chunk.column + column, chunk.row + row)];
      if (field.heights == chunk.region.heightField->heights)
        continue;
      auto* child = static_cast<mdl::GroupNode*>(group.children()[chunk.childIndex]);
      auto prepared = terrainHeightFieldChanges(map, *child, field);
      if (prepared.is_error())
        return std::get<Error>(prepared.error());
      auto part = std::move(prepared).value();
      changes.insert(
        changes.end(),
        std::make_move_iterator(part.begin()),
        std::make_move_iterator(part.end()));
    }
  }
  else
  {
    auto prepared = terrainHeightFieldChanges(map, group, heights);
    if (prepared.is_error())
      return std::get<Error>(prepared.error());
    changes = std::move(prepared).value();
  }
  if (changes.empty())
    return {};
  if (!mdl::updateNodeContents(map, "Sculpt Terrain", std::move(changes)))
    return Error{"Could not update the terrain geometry."};
  return {};
}
} // namespace tb::ui
