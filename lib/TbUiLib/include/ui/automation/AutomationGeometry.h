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

std::optional<AutomationSelectionGeometryAnalysis> analyzeSelectionGeometry(
  mdl::Map& map, double grid, std::string& error);

} // namespace tb::ui::automation
