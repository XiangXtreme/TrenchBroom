/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace tb::ui::detail
{
struct TerrainAtlasPatch
{
  int x = 0;
  int y = 0;
  std::array<std::vector<std::uint8_t>, 4> levels;
  std::vector<size_t> faceIndices;
};

struct TerrainAtlasPlacement
{
  size_t patchIndex = 0;
  int x = 0;
  int y = 0;
  bool referenced = true;
};

struct TerrainAtlasLayout
{
  int width = 0;
  int height = 0;
  std::vector<TerrainAtlasPlacement> placements;
};

// Selects the smallest layout without increasing the regular atlas count.
// Adjacent patches overlap only when their edges match at every MIP level.
std::vector<TerrainAtlasLayout> packTerrainAtlases(
  std::span<const TerrainAtlasPatch> patches, int patchSize, int coreSize);
} // namespace tb::ui::detail
