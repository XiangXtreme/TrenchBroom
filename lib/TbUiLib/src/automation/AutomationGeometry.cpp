/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.
 */

#include "ui/automation/AutomationGeometry.h"

#include "mdl/Brush.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Geometry.h"
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

bool validCsgSelection(
  const mdl::Selection& selection,
  const AutomationCsgOperation operation,
  std::string& error,
  std::string& requiredSelection)
{
  if (operation == AutomationCsgOperation::ConvexMerge)
  {
    const auto canMerge =
      (selection.hasBrushFaces() && selection.brushFaces.size() > 1u)
      || (selection.hasOnlyBrushes() && selection.brushes.size() > 1u);
    if (!canMerge)
    {
      error = "convex_merge requires at least two selected brushes or brush faces";
      requiredSelection =
        "at least two selected brushes, or at least two selected brush faces";
      return false;
    }
    return true;
  }

  if (!selection.hasOnlyBrushes())
  {
    error = automationCsgOperationName(operation)
            + " requires the current selection to contain only brushes";
    requiredSelection = "selected brush nodes only";
    return false;
  }
  if (operation == AutomationCsgOperation::Intersect && selection.brushes.size() < 2u)
  {
    error = "intersect requires at least two selected brushes";
    requiredSelection = "at least two selected brushes";
    return false;
  }
  if (
    (operation == AutomationCsgOperation::Subtract
     || operation == AutomationCsgOperation::Hollow)
    && selection.brushes.empty())
  {
    error =
      automationCsgOperationName(operation) + " requires at least one selected brush";
    requiredSelection = "at least one selected brush";
    return false;
  }
  return true;
}

bool executeCsgOperation(
  mdl::Map& map,
  const AutomationCsgOperation operation,
  const std::string& transactionName)
{
  switch (operation)
  {
  case AutomationCsgOperation::ConvexMerge:
    return mdl::csgConvexMerge(map, transactionName);
  case AutomationCsgOperation::Subtract:
    return mdl::csgSubtract(map, transactionName);
  case AutomationCsgOperation::Intersect:
    return mdl::csgIntersect(map, transactionName);
  case AutomationCsgOperation::Hollow:
    return mdl::csgHollow(map, transactionName);
  }
  return false;
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

std::optional<AutomationCsgOperation> automationCsgOperationFromString(
  const std::string& operation)
{
  if (operation == "convex_merge")
  {
    return AutomationCsgOperation::ConvexMerge;
  }
  if (operation == "subtract")
  {
    return AutomationCsgOperation::Subtract;
  }
  if (operation == "intersect")
  {
    return AutomationCsgOperation::Intersect;
  }
  if (operation == "hollow")
  {
    return AutomationCsgOperation::Hollow;
  }
  return std::nullopt;
}

std::string automationCsgOperationName(const AutomationCsgOperation operation)
{
  switch (operation)
  {
  case AutomationCsgOperation::ConvexMerge:
    return "convex_merge";
  case AutomationCsgOperation::Subtract:
    return "subtract";
  case AutomationCsgOperation::Intersect:
    return "intersect";
  case AutomationCsgOperation::Hollow:
    return "hollow";
  }
  return {};
}

std::string automationCsgTransactionName(const AutomationCsgOperation operation)
{
  switch (operation)
  {
  case AutomationCsgOperation::ConvexMerge:
    return "CSG Convex Merge";
  case AutomationCsgOperation::Subtract:
    return "CSG Subtract";
  case AutomationCsgOperation::Intersect:
    return "CSG Intersect";
  case AutomationCsgOperation::Hollow:
    return "CSG Hollow";
  }
  return {};
}

AutomationCsgSelectionResult applySelectionCsg(
  mdl::Map& map,
  const AutomationCsgOperation operation,
  const std::string& transactionName)
{
  const auto& selection = map.selection();
  auto result = AutomationCsgSelectionResult{
    .selectedBrushCountBefore = selection.brushes.size(),
    .selectedBrushFaceCountBefore = selection.brushFaces.size(),
    .operation = automationCsgOperationName(operation),
    .transactionName = transactionName,
  };
  if (!validCsgSelection(selection, operation, result.error, result.requiredSelection))
  {
    result.selectionFailure = true;
    return result;
  }

  result.deletedBrushCount = selection.brushes.size();
  if (!executeCsgOperation(map, operation, transactionName))
  {
    result.error = "CSG " + result.operation + " did not produce a mutation";
    result.requiredSelection = "valid native CSG brush selection";
    return result;
  }
  result.selectedBrushes = map.selection().brushes;
  return result;
}

} // namespace tb::ui::automation
