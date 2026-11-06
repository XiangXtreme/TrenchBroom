/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include "base/Result.h"
#include "mdl/Brush.h"

#include "vm/bbox.h"
#include "vm/mat.h"
#include "vm/vec.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace tb::mdl
{
enum class MapFormat;
} // namespace tb::mdl

namespace tb::ui
{
inline constexpr auto MaxTerrainBrushes = 8192;

enum class TerrainSculptMode
{
  Raise,
  Lower,
  Smooth,
  Flatten
};

struct TerrainSculptResult
{
  bool changed = false;
  bool reachedBottom = false;
};

/** Heights on a rectangular, regularly spaced grid in terrain-local coordinates. */
struct TerrainHeightField
{
  int columns = 0;
  int rows = 0;
  double spacing = 0.0;
  double bottom = 0.0;
  std::vector<double> heights;

  static Result<TerrainHeightField> create(
    int columns, int rows, double spacing, double bottom, double height);

  bool valid() const;
  size_t index(int column, int row) const;
  std::optional<double> sample(const vm::vec2d& position) const;
  /** Sample several positions while validating the field only once. */
  std::vector<std::optional<double>> sampleMany(
    std::span<const vm::vec2d> positions) const;

  /** Apply one stamp. Position and radius are in terrain-local map units. */
  TerrainSculptResult sculpt(
    TerrainSculptMode mode,
    const vm::vec2d& position,
    double radius,
    double strength,
    double hardness,
    double amount,
    double targetHeight,
    bool lockEdges);

private:
  std::optional<double> sampleValidated(const vm::vec2d& position) const;
};

/** Build closed convex prisms from the same triangles used by height sampling. */
Result<std::vector<mdl::Brush>> createTerrainBrushes(
  const TerrainHeightField& field,
  const vm::vec2d& origin,
  const vm::mat4x4d& transform,
  mdl::MapFormat mapFormat,
  const vm::bbox3d& worldBounds,
  const std::string& materialName);

/** Build only the requested row-major triangles, in the requested order. */
Result<std::vector<mdl::Brush>> createTerrainBrushes(
  const TerrainHeightField& field,
  const vm::vec2d& origin,
  const vm::mat4x4d& transform,
  mdl::MapFormat mapFormat,
  const vm::bbox3d& worldBounds,
  const std::string& materialName,
  std::span<const size_t> triangleIndices);
} // namespace tb::ui
