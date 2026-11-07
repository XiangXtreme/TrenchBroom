/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include "base/Result.h"

#include "vm/vec.h"

#include <string>

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

/** Create a flat terrain rectangle in map XY coordinates as one undo step. */
Result<void> createTerrainRectangle(
  mdl::Map& map,
  const vm::vec2d& first,
  const vm::vec2d& second,
  double height,
  double thickness,
  double spacing,
  const std::string& brushEntityClass = {});

/** Create a rectangle as a single logical terrain made of paint-sized chunks. */
Result<void> createTerrainChunkRectangle(
  mdl::Map& map,
  const vm::vec2d& first,
  const vm::vec2d& second,
  double height,
  double thickness,
  double spacing,
  const std::string& brushEntityClass = {});

/** Update the edited heights and only the affected proxy brushes atomically. */
Result<void> updateTerrainHeightField(
  mdl::Map& map, mdl::GroupNode& group, const TerrainHeightField& heights);
} // namespace tb::ui
