/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 */

#pragma once

#include "vm/bbox.h"

#include <optional>

namespace tb::mdl
{
class Map;
} // namespace tb::mdl

namespace tb::ui
{

struct AutomationMapSnapshot
{
  int pointEntityCount = 0;
  int brushCount = 0;
  int patchCount = 0;
  int nodeCount = 0;
  int selectedNodeCount = 0;
  int selectedEntityCount = 0;
  int selectedBrushCount = 0;
  int selectedFaceCount = 0;
  std::optional<vm::bbox3d> contentBounds;
  double gridSize = 0.0;
  double gridActualSize = 0.0;
  bool gridSnap = false;
  bool gridVisible = false;
};

/** Collects map facts without adapting them for an MCP or Python transport. */
AutomationMapSnapshot collectAutomationMapSnapshot(const mdl::Map& map);

} // namespace tb::ui
