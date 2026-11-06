/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <QJsonObject>
#include <QPolygonF>

#include "base/Result.h"
#include "ui/TerrainHeightField.h"

#include "vm/mat.h"
#include "vm/vec.h"

#include <array>
#include <optional>
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
  std::string materialAssetId;
  // A negative limit leaves the corresponding per-region budget unlimited.
  int maxAddedFaces = -1;
  int maxWadKiB = -1;
  // Zero disables the width limit; density is relative to the default patch.
  int minFaceWidth = 0;
  int patchDensityPercent = 100;
  // Present for sculptable regions; ordinary painted brush groups keep their existing
  // representation until converted.
  std::optional<TerrainHeightField> heightField;
  // Preserve optional fields added by newer editors when this region is edited.
  QJsonObject extensionFields;
  QJsonObject heightFieldExtensionFields;

  explicit TerrainRegion(vm::vec2d origin, std::string materialAssetId = {});
  vm::vec2d project(const vm::vec3d& local) const;
  vm::vec3d unproject(const QPointF& pixel, double z = 0) const;
  void paint(
    const vm::vec2d& point,
    double radius,
    size_t material,
    double strength = 1.0,
    double hardness = 0.35);
  const Weight& weight(int col, int row) const;
  std::string encode() const;
  /** Identifies the paint bake independently of editable terrain heights. */
  std::string paintCacheKey() const;
  static Result<TerrainRegion> decode(std::string_view text);

private:
  std::string encodeImpl(bool includeHeightField) const;
};

struct TerrainSurface
{
  const mdl::BrushNode* brush;
  size_t top;
  QPolygonF outline;
};

mdl::GroupNode* selectedTerrainGroup(const mdl::Map& map);
Result<TerrainRegion> createTerrainRegion(const mdl::GroupNode& group);
/** Sample aligned rectangular planar source brushes without changing their geometry. */
Result<TerrainHeightField> createTerrainHeightField(
  const mdl::GroupNode& group, const TerrainRegion& region, double spacing);
/** Map each height-field triangle to its verified child brush index. */
Result<std::vector<size_t>> terrainHeightFieldBrushIndices(
  const mdl::GroupNode& group, const TerrainRegion& region);
Result<std::vector<TerrainSurface>> terrainSurfaces(
  const mdl::GroupNode& group, const TerrainRegion& region);
double terrainArea(const QPolygonF& polygon);
} // namespace tb::ui
