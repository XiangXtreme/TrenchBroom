/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <QImage>

#include "base/Result.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tb::project
{
class ProjectStore;
}

namespace tb::ui
{
using TerrainMipLevels = std::array<std::vector<std::uint8_t>, 4>;

/** Normalized source textures, a common palette, and optional precomputed mip levels. */
struct TerrainMaterial
{
  static constexpr auto Period = 128;
  std::array<QImage, 3> images;
  std::array<std::uint8_t, 768> palette;
  std::array<TerrainMipLevels, 3> baseLevels{};
  std::array<TerrainMipLevels, 3> pairLevels{};
  std::array<std::uint8_t, 32> levelSourceHash{};
};

/** Resolve exact captured texture dependencies, without consulting the asset library. */
Result<std::array<QImage, 3>> loadTerrainMaterialImages(
  const project::ProjectStore& store, const std::string& materialAssetId);

/** Validate and normalize all three textures to the terrain period. */
Result<std::array<QImage, 3>> normalizeTerrainMaterialImages(
  std::array<QImage, 3> images);

/** Blend all three source images into a compact preview. */
QImage renderTerrainMaterialPreview(std::array<QImage, 3> images);

/** Normalize the source images and construct one palette covering their blends. */
Result<TerrainMaterial> prepareTerrainMaterial(std::array<QImage, 3> images);

/** Persist the normalized textures, palette, and precomputed mip levels. */
std::vector<std::uint8_t> encodeTerrainMaterialPackage(const TerrainMaterial& material);

/** Use a package only when it matches the currently captured texture pixels. */
std::optional<TerrainMaterial> decodeTerrainMaterialPackage(
  const std::vector<std::uint8_t>& bytes, std::array<QImage, 3> normalizedImages);

/** Resolve a captured material, falling back to rebuilding a stale or absent package. */
Result<TerrainMaterial> loadTerrainMaterial(
  const project::ProjectStore& store, const std::string& materialAssetId);
} // namespace tb::ui
