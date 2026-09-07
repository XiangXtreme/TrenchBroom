/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.
 */

#include "ui/automation/AutomationGeometry.h"

#include "mdl/Brush.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Selection.h"
#include "mdl/Node.h"

#include <algorithm>
#include <cmath>

namespace tb::ui::automation
{
namespace
{

bool gridAligned(const double value, const double grid)
{
  return std::abs(value / grid - std::round(value / grid)) <= 0.01;
}

bool brushGridAligned(const mdl::Brush& brush, const double grid)
{
  return std::ranges::all_of(brush.vertexPositions(), [&](const auto& vertex) {
    return gridAligned(vertex.x(), grid) && gridAligned(vertex.y(), grid)
           && gridAligned(vertex.z(), grid);
  });
}

void collectBrushNodes(mdl::Node& node, std::vector<mdl::BrushNode*>& brushes)
{
  if (auto* brush = dynamic_cast<mdl::BrushNode*>(&node))
  {
    brushes.push_back(brush);
    return;
  }
  for (auto* child : node.children())
  {
    if (child != nullptr)
    {
      collectBrushNodes(*child, brushes);
    }
  }
}

std::vector<mdl::BrushNode*> selectedBrushNodes(mdl::Map& map)
{
  auto result = std::vector<mdl::BrushNode*>{};
  for (auto* node : map.selection().nodes)
  {
    if (node != nullptr)
    {
      collectBrushNodes(*node, result);
    }
  }
  std::ranges::sort(result);
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

void appendUnique(std::vector<std::string>& values, const std::string& value)
{
  if (std::ranges::find(values, value) == values.end())
  {
    values.push_back(value);
  }
}

} // namespace

std::optional<AutomationSelectionGeometryAnalysis> analyzeSelectionGeometry(
  mdl::Map& map, const double grid, std::string& error)
{
  if (!std::isfinite(grid) || grid <= 0.0)
  {
    error = "grid must be greater than zero";
    return std::nullopt;
  }

  auto result = AutomationSelectionGeometryAnalysis{};
  result.grid = grid;
  for (auto* brushNode : selectedBrushNodes(map))
  {
    const auto& brush = brushNode->brush();
    auto fact = AutomationBrushGeometryFact{
      .brush = brushNode,
      .vertexCount = brush.vertexCount(),
      .edgeCount = brush.edgeCount(),
      .closed = brush.closed(),
      .convex = brush.closed() && brush.fullySpecified(),
      .gridAligned = brushGridAligned(brush, grid),
    };
    if (!fact.convex)
    {
      ++result.invalidBrushCount;
    }
    if (!fact.gridAligned)
    {
      ++result.nonGridAlignedCount;
    }
    for (const auto& face : brush.faces())
    {
      appendUnique(fact.materials, face.materialName());
      appendUnique(result.materials, face.materialName());
    }
    result.bounds = result.bounds ? vm::merge(*result.bounds, brushNode->logicalBounds())
                                  : brushNode->logicalBounds();
    result.brushes.push_back(std::move(fact));
  }
  return result;
}

} // namespace tb::ui::automation
