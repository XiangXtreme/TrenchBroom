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
/** Replace a compatible source group with an editable height field as one undo step. */
Result<void> convertTerrainToHeightField(
  mdl::Map& map, mdl::GroupNode& group, double spacing);
} // namespace tb::ui
