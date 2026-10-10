/*
 Copyright (C) 2026 KreedzBroom contributors

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "vm/vec.h"

#include <memory>
#include <string>
#include <vector>

struct scs_world;

namespace tb
{
namespace mdl
{
class Map;
}

namespace ui
{

struct ScsWorldDeleter
{
  void operator()(scs_world* world) const;
};

using ScsWorldPtr = std::unique_ptr<scs_world, ScsWorldDeleter>;

struct PlayModeSpawnPoint
{
  std::string classname;
  vm::vec3f origin;
  vm::vec3f angles; // pitch, yaw, roll in degrees
};

struct PlayModeWorld
{
  ScsWorldPtr world;
  std::vector<PlayModeSpawnPoint> spawnPoints;
  size_t worldBrushCount = 0;
  size_t entityCount = 0;
  size_t skippedBrushCount = 0;
  double buildMilliseconds = 0.0;
  int clipType = 0;             // SCS_CLIPTYPE_* the hulls were built with
  bool clipTypeFromMap = false; // set by an info_compile_parameters entity
  std::string error;            // non-empty if the world could not be built
};

struct PlayModeWorldOptions
{
  int compiler = 0; // SCS_COMPILER_VHLT; also decides which tool textures exist
  int clipType = 4; // SCS_CLIPTYPE_LEGACY, what most CS 1.6 maps were compiled with
  // Like hlcsg, let an info_compile_parameters entity's "cliptype" key win.
  bool useMapClipType = true;
};

/**
 * Compiles the visible brushes of the given map into a TbCsLib collision world, using
 * the same rules a GoldSrc compile would: worldspawn, func_group and func_detail become
 * world geometry, func_ladder and func_water become special brush entities, triggers and
 * other non-solid brush entities are ignored, and CLIP / ORIGIN / HINT / SKIP / !liquid
 * materials determine brush contents. Clip hulls are expanded with VHLT's or ZHLT's
 * hlcsg rules for the chosen cliptype, including their tool textures (BEVEL, and for
 * VHLT CLIPHULLn / NOCLIP / ...).
 */
PlayModeWorld buildPlayModeWorld(
  const mdl::Map& map, const PlayModeWorldOptions& options = {});

} // namespace ui
} // namespace tb
