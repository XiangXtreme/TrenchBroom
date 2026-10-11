/*
 Copyright (C) 2026 Jackson Palmer
 Copyright (C) 2026 Kristian Duske

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

#include "mdl/SurfaceAttributes.h"
#include "mdl/UvAttributes.h"
#include "mdl/UvCoordSystem.h"

#include "kd/flat_map.h"
#include "kd/reflection_decl.h"

#include "vm/plane.h"
#include "vm/polygon.h"
#include "vm/quat.h"
#include "vm/vec.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tb
{
namespace mdl
{
class BrushNode;
class Map;
class Node;
} // namespace mdl

namespace ui
{

enum class SweepAlignment : std::uint8_t
{
  Integer,
  Free,
};

std::ostream& operator<<(std::ostream& lhs, SweepAlignment rhs);

enum class SweepPathMode
{
  Arc,
  Straight,
  SBend,
};

std::ostream& operator<<(std::ostream& lhs, SweepPathMode rhs);

enum class SweepUvMode : std::uint8_t
{
  Preserve,
  Continuous,
};

std::ostream& operator<<(std::ostream& lhs, SweepUvMode rhs);

enum class SweepConstructionMode : std::uint8_t
{
  Sweep,
  Bridge,
};

struct SweepFaceAttributes
{
  std::string materialName;
  mdl::UvAttributes uvAttributes;
  mdl::SurfaceAttributes surfaceAttributes;
  std::optional<mdl::UvCoordSystemSnapshot> uvCoordSystemSnapshot;
  vm::plane3d sourceBoundary;

  kdl_reflect_decl(
    SweepFaceAttributes,
    materialName,
    uvAttributes,
    surfaceAttributes,
    uvCoordSystemSnapshot,
    sourceBoundary);
};

struct SweepFace
{
  vm::polygon3d polygon;
  mdl::Node* parent = nullptr;
  std::optional<SweepFaceAttributes> capAttributes = std::nullopt;
  std::vector<std::optional<SweepFaceAttributes>> sideAttributes = {};

  kdl_reflect_decl(SweepFace, polygon, parent, capAttributes, sideAttributes);
};

struct SweepSource
{
  std::vector<SweepFace> faces;
  vm::vec3d center;
  vm::vec3d normal;
  vm::vec3d scaleBaseVector;

  kdl_reflect_decl(SweepSource, faces, center, normal, scaleBaseVector);
};

struct SweepTarget
{
  std::vector<SweepFace> faces;
  vm::vec3d center;
  vm::vec3d normal;

  kdl_reflect_decl(SweepTarget, faces, center, normal);
};

struct SweepTransform
{
  vm::vec3d translation = vm::vec3d{0, 0, 0};
  vm::quatd rotation = vm::quatd{vm::vec3d{0, 0, 1}, 0.0};
  vm::vec3d scale = vm::vec3d{1, 1, 1};

  vm::vec3d destinationCenter(const SweepSource& source) const;
  vm::quatd effectiveRotation() const;
  bool isNoOp() const;

  kdl_reflect_decl(SweepTransform, translation, rotation, scale);
};

struct SweepParameters
{
  size_t segments = 8;
  size_t iterations = 1;
  SweepPathMode pathMode = SweepPathMode::Arc;
  SweepAlignment alignment = SweepAlignment::Integer;
  SweepUvMode uvMode = SweepUvMode::Preserve;

  kdl_reflect_decl(SweepParameters, segments, iterations, pathMode, alignment, uvMode);
};

// Keep the allocation footprint of the pre-continuous-UV layout. SweepTool is allocated
// across library boundaries, and a stale constructor object must never under-allocate it.
static_assert(
  sizeof(SweepParameters)
  == ((2u * sizeof(size_t) + 2u * sizeof(int) + alignof(size_t) - 1u) / alignof(size_t) * alignof(size_t)));

using SweepBrushMap =
  kdl::flat_map<mdl::Node*, std::vector<std::unique_ptr<mdl::BrushNode>>>;

struct SweepIssue
{
  size_t sourceFaceIndex = 0;
  size_t iteration = 0;
  size_t segment = 0;
  std::string message;

  kdl_reflect_decl(SweepIssue, sourceFaceIndex, iteration, segment, message);
};

struct SweepResult
{
  SweepBrushMap brushes;
  std::vector<SweepIssue> issues;
};

/**
 * Computes the transform for a single station (cross-section) along one iteration of the
 * sweep, at fractional position `t` (0 at the source face, 1 at the destination).
 *
 * `rotationFull` is the rotation for a complete iteration (t = 1); this function scales
 * its angle by `t` so that intermediate stations rotate proportionally. In arc mode, the
 * source is revolved about a pivot derived from `source` and `transform`, with the
 * translation along the rotation axis applied as a helical lift; if no usable pivot
 * exists, this falls through to the straight-path behavior. In straight/S-bend mode, the
 * source is scaled and rotated about its center and then translated, following an S-curve
 * offset when `parameters.pathMode` is `SweepPathMode::SBend` and `source.normal` is
 * non-zero.
 *
 * The result does not include the transforms of preceding iterations; callers that sweep
 * multiple iterations compose this with the accumulated transform of prior iterations.
 */
vm::mat4x4d stationTransform(
  const SweepSource& source,
  const SweepTransform& transform,
  const SweepParameters& parameters,
  double t,
  const vm::quatd& rotationFull);

/**
 * Generates the brushes that make up a swept shape, by tessellating each source face into
 * `parameters.segments` stations per iteration (via `stationTransform`) and building one
 * brush per pair of adjacent stations.
 *
 * Source faces are swept independently, and the resulting brushes are grouped by the
 * parent node captured on each `SweepFace` (falling back to the map's default insertion
 * parent if that node no longer exists). Station vertices are rounded to integer
 * coordinates when `parameters.alignment` is `SweepAlignment::Integer`, except at the
 * very first station of the first iteration, which is kept exact since it corresponds to
 * the original source face. Invalid or collapsed segments are reported in the returned
 *
 * result. Valid brushes are still returned for preview, but callers must not commit a
 *
 * result that contains issues. Generated faces inherit material, UV and surface
 *
 * attributes from the selected face and its adjacent source faces.
 */
SweepResult generateSweepBrushes(
  mdl::Map& map,
  const SweepSource& source,
  const SweepTransform& transform,
  const SweepParameters& parameters);

/**
 * Connects two topologically equivalent face components. Shared vertices are matched
 * as
 * one component, so adjacent brushes reuse identical station coordinates. The end

 * * stations use the selected vertices verbatim and remain flush with existing brushes.

 */
SweepResult generateBridgeBrushes(
  mdl::Map& map,
  const SweepSource& source,
  const SweepTarget& target,
  const SweepParameters& parameters);

} // namespace ui
} // namespace tb
