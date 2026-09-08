/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.
 */

#pragma once

#include "vm/bbox.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace tb::mdl
{
class BrushNode;
class Map;
} // namespace tb::mdl

namespace tb::ui::automation
{

/** Protocol-neutral facts about one brush in the current map selection. */
struct AutomationBrushGeometryFact
{
  mdl::BrushNode* brush = nullptr;
  size_t vertexCount = 0u;
  size_t edgeCount = 0u;
  bool closed = false;
  bool convex = false;
  bool gridAligned = false;
  std::vector<std::string> materials;
};

/** Compact native facts used by both MCP and Python geometry adapters. */
struct AutomationSelectionGeometryAnalysis
{
  double grid = 1.0;
  std::vector<AutomationBrushGeometryFact> brushes;
  size_t invalidBrushCount = 0u;
  size_t nonGridAlignedCount = 0u;
  std::vector<std::string> materials;
  std::optional<vm::bbox3d> bounds;
};

/** Native CSG operation names shared by editor automation adapters. */
enum class AutomationCsgOperation
{
  ConvexMerge,
  Subtract,
  Intersect,
  Hollow,
};

/** Result of a native CSG operation on the current map selection. */
struct AutomationCsgSelectionResult
{
  std::vector<mdl::BrushNode*> selectedBrushes;
  size_t deletedBrushCount = 0u;
  size_t selectedBrushCountBefore = 0u;
  size_t selectedBrushFaceCountBefore = 0u;
  std::string operation;
  std::string transactionName;
  std::string error;
  std::string requiredSelection;
  bool selectionFailure = false;

  [[nodiscard]] bool ok() const { return error.empty(); }
};

std::optional<AutomationSelectionGeometryAnalysis> analyzeSelectionGeometry(
  mdl::Map& map, double grid, std::string& error);

std::optional<AutomationCsgOperation> automationCsgOperationFromString(
  const std::string& operation);
std::string automationCsgOperationName(AutomationCsgOperation operation);
std::string automationCsgTransactionName(AutomationCsgOperation operation);
AutomationCsgSelectionResult applySelectionCsg(
  mdl::Map& map, AutomationCsgOperation operation, const std::string& transactionName);

} // namespace tb::ui::automation
