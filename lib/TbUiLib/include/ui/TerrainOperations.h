/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include "base/Result.h"

namespace tb::mdl
{
class GroupNode;
class Map;
} // namespace tb::mdl

namespace tb::ui
{
struct TerrainHeightField;

/** Replace a compatible source group with an editable height field as one undo step. */
Result<void> convertTerrainToHeightField(
  mdl::Map& map, mdl::GroupNode& group, double spacing);

/** Convert the selected brush group or group selected brushes and convert them. */
Result<void> convertSelectedTerrainToHeightField(mdl::Map& map, double spacing);

/** Update the edited heights and only the affected proxy brushes atomically. */
Result<void> updateTerrainHeightField(
  mdl::Map& map, mdl::GroupNode& group, const TerrainHeightField& heights);
} // namespace tb::ui
