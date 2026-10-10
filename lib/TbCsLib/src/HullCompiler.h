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

// In-memory "micro BSP" compiler.
//
// Turns convex brushes straight into GoldSrc clip hulls (clipnodes + planes),
// i.e. the subset of hlcsg + hlbsp output that player movement consumes:
//
//   hlcsg  : per-hull brush expansion (VHLT's or ZHLT's ExpandBrush, every
//            -cliptype),
//            contents rules (CLIP, CLIPHULLn, NOCLIP, liquids), BEVEL faces
//   hlbsp  : solid/empty BSP over the expanded brushes -> dclipnode_t tree
//
// Skipped on purpose: CSG face clipping (only needed for drawing / hull 0
// render nodes), portals, vis, lighting, texinfo.
#pragma once

#include "BspTypes.h"

#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <stdexcept>

namespace tb::cs {

struct Vec3d
{
  double x = 0.0, y = 0.0, z = 0.0;

  constexpr double operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
  constexpr double& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
};

// dot(normal, p) <= dist  <=>  p is inside (normals point out of the brush).
struct BrushPlane
{
  Vec3d normal;
  double dist = 0.0;
  // hlcsg BEVEL / CLIPBEVEL face: not pushed out when expanding clip hulls.
  bool bevel = false;
};

constexpr unsigned kAllHulls = (1u << kNumHulls) - 1;

struct BrushDesc
{
  std::span<const BrushPlane> planes;
  int contents = kContentsSolid;
  // Bit h set: the brush exists in BSP hull h (before the contents rules).
  // CLIPHULLn brushes use 1 << n, NOCLIP brushes 1 << 0.
  unsigned hulls = kAllHulls;
};

// hlcsg -cliptype. Values match VHLT's enum and info_compile_parameters.
enum class ClipType
{
  Smallest = 0,   // offset non-axial faces by the box corner, no edge bevels
  Normalized = 1, // legacy offsets plus edge bevels
  Simple = 2,     // smallest plus edge bevels (VHLT default)
  Precise = 3,    // simple, but floors (normal z > 0.7) only move up
  Legacy = 4,     // offset scaled by the normal (ZHLT default, most CS maps)
};

// Whose hlcsg to imitate. Values match SCS_COMPILER_*.
enum class Compiler
{
  Vhlt = 0,
  Zhlt = 1,
};

struct HullCompileOptions
{
  Compiler compiler = Compiler::Vhlt;
  ClipType clipType = ClipType::Smallest;

  // BSP hull boxes: 0 point, 1 standing, 2 large, 3 ducked (hlcsg defaults).
  std::array<Vec3d, kNumHulls> hullMins{
    {{0, 0, 0}, {-16, -16, -36}, {-32, -32, -32}, {-16, -16, -18}}};
  std::array<Vec3d, kNumHulls> hullMaxs{
    {{0, 0, 0}, {16, 16, 36}, {32, 32, 32}, {16, 16, 18}}};
};

struct HullCompileStats
{
  std::size_t skippedBrushes = 0;
  std::array<std::size_t, kNumHulls> clipnodes{};
};

class HullLimitError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

// Compiles one BSP model. Brushes with contents other than SOLID / CLIP /
// WATER / SLIME / LAVA are treated as SOLID. Throws HullLimitError when a hull
// exceeds kMaxClipNodes (a runaway-memory guard, far above GoldSrc's limit).
std::unique_ptr<BrushModel> compileBrushModel(
  std::span<const BrushDesc> brushes,
  const HullCompileOptions& options,
  HullCompileStats& stats);

} // namespace tb::cs
