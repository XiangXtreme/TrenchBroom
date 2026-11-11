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

// Runtime collision data in the exact memory layout GoldSrc uses.
//
// pm_shared.c receives `struct hull_s*` / `void*` hulls from the engine and
// dereferences them as its own hull_t (it reads firstclipnode in PM_Ladder),
// so Hull's members up to clip_maxs must stay layout-compatible with:
//
//   typedef struct { int planenum; short children[2]; } dclipnode_t;
//   typedef struct mplane_s { vec3_t normal; float dist; byte type, signbits, pad[2]; } mplane_t;
//   typedef struct hull_s { dclipnode_t* clipnodes; mplane_t* planes;
//                           int firstclipnode, lastclipnode;
//                           vec3_t clip_mins, clip_maxs; } hull_t;
//
// pm_shared never walks clipnodes itself (only through PM_HullPointContents,
// which TbCsLib implements), so ClipNode uses 32-bit children like Xash's BSP2
// clipnodes32. That lifts the 32767-clipnode limit of the .bsp format while
// producing identical traces.
//
// This header deliberately does not include any HLSDK header.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tb::cs {

inline constexpr int kNumHulls = 4; // MAX_MAP_HULLS
inline constexpr int kMaxClipNodes = 1 << 24; // sanity cap, not a format limit
inline constexpr int kGoldSrcMaxClipNodes = 32767; // MAX_MAP_CLIPNODES

// GoldSrc contents values (const.h)
inline constexpr int kContentsEmpty = -1;
inline constexpr int kContentsSolid = -2;
inline constexpr int kContentsWater = -3;
inline constexpr int kContentsSlime = -4;
inline constexpr int kContentsLava = -5;
inline constexpr int kContentsClip = -8;
inline constexpr int kContentsLadder = -16;

// mplane_t::type
inline constexpr std::uint8_t kPlaneX = 0;
inline constexpr std::uint8_t kPlaneY = 1;
inline constexpr std::uint8_t kPlaneZ = 2;
inline constexpr std::uint8_t kPlaneAnyX = 3;

struct ClipNode
{
  std::int32_t planenum;
  std::int32_t children[2]; // >= 0 node index, < 0 contents
};

struct ClipPlane
{
  float normal[3];
  float dist;
  std::uint8_t type;
  std::uint8_t signbits;
  std::uint8_t pad[2];
};

struct Hull
{
  ClipNode* clipnodes;
  ClipPlane* planes;
  int firstclipnode;
  int lastclipnode;
  float clip_mins[3];
  float clip_maxs[3];
};

static_assert(sizeof(ClipNode) == 12);
static_assert(sizeof(ClipPlane) == 20);
static_assert(offsetof(Hull, firstclipnode) == 2 * sizeof(void*));
static_assert(offsetof(Hull, clip_mins) == 2 * sizeof(void*) + 2 * sizeof(int));

// One BSP model (world or brush entity) with its four clip hulls.
// Owns the arrays the Hull pointers refer to, hence non-copyable.
struct BrushModel
{
  BrushModel() = default;
  BrushModel(const BrushModel&) = delete;
  BrushModel& operator=(const BrushModel&) = delete;

  std::array<Hull, kNumHulls> hulls{};
  std::array<std::vector<ClipNode>, kNumHulls> clipnodes;
  std::array<std::vector<ClipPlane>, kNumHulls> planes;
  float mins[3]{};
  float maxs[3]{};

  // Engine-side entity properties (what SV_CopyEdictToPhysEnt reads).
  int solid = 4; // SOLID_BSP
  int skin = 0;  // physent skin: special contents for SOLID_NOT brush ents
  std::string name;
};

} // namespace tb::cs
