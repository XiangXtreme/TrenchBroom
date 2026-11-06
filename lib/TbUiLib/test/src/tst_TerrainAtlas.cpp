/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/TerrainAtlas.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui::detail
{
namespace
{
TerrainAtlasPatch makePatch(
  const int x, const int y, const int patchSize, const int coreSize)
{
  auto patch = TerrainAtlasPatch{};
  patch.x = x * 48;
  patch.y = y * 48;
  const auto gutter = (patchSize - coreSize) / 2;
  for (auto mip = 0; mip < 4; ++mip)
  {
    const auto side = patchSize >> mip;
    const auto originX = (2048 + x * coreSize - gutter) >> mip;
    const auto originY = (2048 + y * coreSize - gutter) >> mip;
    auto& pixels = patch.levels[size_t(mip)];
    pixels.resize(size_t(side * side));
    for (auto row = 0; row < side; ++row)
      for (auto col = 0; col < side; ++col)
      {
        const auto u = originX + col;
        const auto v = originY + row;
        pixels[size_t(row * side + col)] =
          std::uint8_t((u * 31 + v * 17 + u * v + mip * 37) & 255);
      }
  }
  return patch;
}

void checkReconstructed(
  const std::vector<TerrainAtlasPatch>& patches,
  const std::vector<TerrainAtlasLayout>& layouts,
  const int patchSize)
{
  auto references = std::vector<size_t>(patches.size());
  for (const auto& layout : layouts)
  {
    REQUIRE(layout.width <= 256);
    REQUIRE(layout.height <= 256);
    auto atlases = std::array<std::vector<std::uint8_t>, 4>{};
    auto written = std::array<std::vector<bool>, 4>{};
    for (auto mip = 0; mip < 4; ++mip)
    {
      const auto pixels = size_t((layout.width >> mip) * (layout.height >> mip));
      atlases[size_t(mip)].resize(pixels);
      written[size_t(mip)].resize(pixels);
    }
    for (const auto& placement : layout.placements)
    {
      REQUIRE(placement.patchIndex < patches.size());
      REQUIRE(placement.x % 8 == 0);
      REQUIRE(placement.y % 8 == 0);
      REQUIRE(placement.x + patchSize <= layout.width);
      REQUIRE(placement.y + patchSize <= layout.height);
      if (placement.referenced)
        ++references[placement.patchIndex];
      for (auto mip = 0; mip < 4; ++mip)
      {
        const auto side = patchSize >> mip;
        const auto width = layout.width >> mip;
        const auto x = placement.x >> mip;
        const auto y = placement.y >> mip;
        for (auto row = 0; row < side; ++row)
          for (auto col = 0; col < side; ++col)
          {
            const auto dst = size_t((y + row) * width + x + col);
            const auto src = size_t(row * side + col);
            const auto value = patches[placement.patchIndex].levels[size_t(mip)][src];
            if (written[size_t(mip)][dst])
              REQUIRE(atlases[size_t(mip)][dst] == value);
            atlases[size_t(mip)][dst] = value;
            written[size_t(mip)][dst] = true;
          }
      }
    }
    for (const auto& placement : layout.placements)
    {
      if (!placement.referenced)
        continue;
      for (auto mip = 0; mip < 4; ++mip)
      {
        const auto side = patchSize >> mip;
        const auto width = layout.width >> mip;
        const auto x = placement.x >> mip;
        const auto y = placement.y >> mip;
        for (auto row = 0; row < side; ++row)
          for (auto col = 0; col < side; ++col)
            REQUIRE(
              atlases[size_t(mip)][size_t((y + row) * width + x + col)]
              == patches[placement.patchIndex]
                   .levels[size_t(mip)][size_t(row * side + col)]);
      }
    }
  }
  CHECK(std::ranges::all_of(references, [](const auto count) { return count == 1; }));
}
} // namespace

TEST_CASE("Terrain atlas shares only matching MIP borders")
{
  for (const auto [patchSize, coreSize] :
       {std::pair{32, 24}, std::pair{64, 48}, std::pair{128, 96}})
  {
    DYNAMIC_SECTION("patch size " << patchSize)
    {
      for (const auto vertical : {false, true})
      {
        auto patches = std::vector<TerrainAtlasPatch>{};
        for (auto i = 0; i < 5; ++i)
          patches.push_back(
            makePatch(vertical ? 0 : i, vertical ? i : 0, patchSize, coreSize));
        const auto layouts = packTerrainAtlases(patches, patchSize, coreSize);
        checkReconstructed(patches, layouts, patchSize);
        if (patchSize == 64)
        {
          REQUIRE(layouts.size() == 1);
          CHECK(layouts[0].width == (vertical ? 64 : 256));
          CHECK(layouts[0].height == (vertical ? 256 : 64));
        }

        // An incompatibility in the smallest MIP must prevent that overlap.
        patches[1].levels[3][0] ^= 0x7f;
        checkReconstructed(
          patches, packTerrainAtlases(patches, patchSize, coreSize), patchSize);
      }
    }
  }
}

TEST_CASE("Terrain atlas does not trade more textures for fewer bytes")
{
  const auto rows = std::array{
    ".......######.##....",
    "......##############",
    "##############....##",
    "##.......###########",
    "#############.......",
    "....##.............."};
  auto patches = std::vector<TerrainAtlasPatch>{};
  for (auto y = size_t{0}; y < rows.size(); ++y)
    for (auto x = 0; rows[y][x] != '\0'; ++x)
      if (rows[y][x] == '#')
        patches.push_back(makePatch(x, int(y), 32, 24));
  REQUIRE(patches.size() == 66);
  const auto layouts = packTerrainAtlases(patches, 32, 24);
  CHECK(layouts.size() <= 2);
  checkReconstructed(patches, layouts, 32);
}
} // namespace tb::ui::detail
