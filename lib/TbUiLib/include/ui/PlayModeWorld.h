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

#include "ui/PlayModeJumpGuide.h"

#include "vm/bbox.h"
#include "vm/vec.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct scs_world;

namespace tb
{
namespace mdl
{
class Map;
class WorldNode;
} // namespace mdl

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
  std::vector<PlayModeJumpSurface> jumpSurfaces;
  size_t worldBrushCount = 0;
  size_t entityCount = 0;
  size_t teleportCount = 0;
  std::vector<std::string> warnings;
  size_t skippedBrushCount = 0;
  double buildMilliseconds = 0.0;
  int clipType = 0;  // SCS_CLIPTYPE_* the hulls were built with
  std::string error; // non-empty if the world could not be built
};

struct PlayModeWorldOptions
{
  int compiler = 0; // SCS_COMPILER_VHLT, ZHLT, or HLTOOLS
  int clipType = 4; // SCS_CLIPTYPE_LEGACY, what most CS 1.6 maps were compiled with
  // Play Mode only needs the point, standing, and ducked player hulls by default.
  // The large hull remains available to the generic SCS API, but is omitted from
  // this editor-only collision world unless explicitly requested. The hltools
  // backend always builds all four hulls through its original CSG/BSP stages.
  uint32_t hullMask = 0b1011u;
};

/**
 * Compiles the visible brushes of the given map into a TbCsLib collision world, using
 * the same rules a GoldSrc compile would: worldspawn, func_group and func_detail become
 * world geometry, func_ladder and func_water become special brush entities, and
 * trigger_teleport becomes a non-blocking touch volume. Other non-solid brush entities
 * are ignored, and CLIP / ORIGIN / HINT / SKIP / !liquid materials determine brush
 * contents. The hltools backend serializes a detached MAP snapshot and runs the
 * shared hltools CSG/BSP compiler, including legacytrim and per-entity settings.
 * The plane-based backends expand clip hulls with VHLT's or ZHLT's
 * hlcsg rules for the chosen cliptype, including their tool textures (BEVEL, and for
 * VHLT CLIPHULLn / NOCLIP / ...).
 */
PlayModeWorld buildPlayModeWorld(
  const mdl::Map& map, const PlayModeWorldOptions& options = {});

/** Copies the map without editor-owned resource references for background work. */
std::unique_ptr<mdl::WorldNode> makePlayModeWorldSnapshot(const mdl::Map& map);

/** Builds from a detached world, which may be compiled while the live map is edited. */
PlayModeWorld buildPlayModeWorld(
  const mdl::WorldNode& worldNode,
  const vm::bbox3d& worldBounds,
  const PlayModeWorldOptions& options = {});

} // namespace ui
} // namespace tb
