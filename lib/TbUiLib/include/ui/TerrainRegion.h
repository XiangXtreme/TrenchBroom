/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <QPolygonF>

#include "base/Result.h"

#include "vm/mat.h"
#include "vm/vec.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace tb::mdl
{
class BrushNode;
class GroupNode;
class Map;
} // namespace tb::mdl

namespace tb::ui
{
/** Group-local XY coordinates match the GoldSrc terrain baker's texel grid. */
struct TerrainRegion
{
  using Weight = std::array<double, 3>;
  static constexpr auto Columns = 40;
  static constexpr auto Rows = 20;
  static constexpr auto Step = 24.0;
  static constexpr auto UnitsPerTexel = 4.0 / 3.0;
  static constexpr auto Width = Columns * Step;
  static constexpr auto Height = Rows * Step;

  vm::vec2d origin;
  std::array<Weight, (Columns + 1) * (Rows + 1)> weights;

  explicit TerrainRegion(vm::vec2d origin);
  vm::vec2d project(const vm::vec3d& local) const;
  vm::vec3d unproject(const QPointF& pixel, double z = 0) const;
  void paint(const vm::vec2d& point, double radius, size_t material);
  const Weight& weight(int col, int row) const;
  std::string encode() const;
  static Result<TerrainRegion> decode(std::string_view text);
};

struct TerrainSurface
{
  const mdl::BrushNode* brush;
  size_t top;
  QPolygonF outline;
};

mdl::GroupNode* selectedTerrainGroup(const mdl::Map& map);
Result<TerrainRegion> createTerrainRegion(const mdl::GroupNode& group);
Result<std::vector<TerrainSurface>> terrainSurfaces(
  const mdl::GroupNode& group, const TerrainRegion& region);
double terrainArea(const QPolygonF& polygon);
} // namespace tb::ui
