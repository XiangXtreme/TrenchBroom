/*
 Copyright (C) 2026 XiangXtreme

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

#include "render/LightmapData.h"

#include "vm/plane.h"
#include "vm/vec.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

namespace tb::render
{

namespace
{

/**
 * A bake with identity lightmap axes: one luxel spans 16 world units along x and y, so a
 * world position maps to (x / 16, y / 16).
 */
LightmapBake makeBake(const int width, const int height)
{
  auto bake = LightmapBake{};
  bake.width = width;
  bake.height = height;
  bake.pixels.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 3, 0);
  for (auto y = 0; y < height; ++y)
  {
    for (auto x = 0; x < width; ++x)
    {
      const auto index = (static_cast<size_t>(y) * static_cast<size_t>(width)
                          + static_cast<size_t>(x))
                         * 3;
      bake.pixels[index + 0] = static_cast<std::uint8_t>(x + 1);
      bake.pixels[index + 1] = static_cast<std::uint8_t>(y + 1);
      bake.pixels[index + 2] = 100;
    }
  }
  return bake;
}

/**
 * A record on the z = 0 plane covering the given luxel rectangle, with identity axes.
 */
LightmapFaceRecord makeRecord(
  const vm::vec2i mins, const vm::vec2i size, const vm::vec2i tile,
  std::vector<vm::vec2f> poly)
{
  auto record = LightmapFaceRecord{};
  record.normal = vm::vec3f{0.0f, 0.0f, 1.0f};
  record.dist = 0.0f;
  record.vecs[0] = vm::vec4f{1.0f, 0.0f, 0.0f, 0.0f};
  record.vecs[1] = vm::vec4f{0.0f, 1.0f, 0.0f, 0.0f};
  record.mins = mins;
  record.size = size;
  record.tile = tile;
  record.poly = std::move(poly);
  return record;
}

FaceLightmapInput makeFace(const float size)
{
  auto face = FaceLightmapInput{};
  face.plane = vm::plane3d{0.0, vm::vec3d{0.0, 0.0, 1.0}};
  face.outline = {
    vm::vec3f{0.0f, 0.0f, 0.0f},
    vm::vec3f{size, 0.0f, 0.0f},
    vm::vec3f{size, size, 0.0f},
    vm::vec3f{0.0f, size, 0.0f}};
  return face;
}

std::uint8_t pixelAt(
  const std::vector<std::uint8_t>& pixels, const vm::vec2i size, const int x, const int y,
  const int channel)
{
  return pixels[(static_cast<size_t>(y) * static_cast<size_t>(size.x())
                 + static_cast<size_t>(x))
                * 3 + static_cast<size_t>(channel)];
}

} // namespace

TEST_CASE("buildFaceLightmap")
{
  SECTION("samples the rectangle the record covers")
  {
    auto bake = makeBake(8, 8);
    bake.faces.push_back(makeRecord(
      {0, 0}, {3, 3}, {2, 1},
      // a quad covering luxels [0, 3) on both axes
      {{0.0f, 0.0f}, {3.0f, 0.0f}, {3.0f, 3.0f}, {0.0f, 3.0f}}));

    auto pixels = std::vector<std::uint8_t>{};
    const auto block = buildFaceLightmap(makeFace(32.0f), bake, pixels);

    REQUIRE(block.has_value());
    CHECK(block->mins == vm::vec2i{0, 0});
    CHECK(block->size == vm::vec2i{3, 3});
    REQUIRE(pixels.size() == 3u * 3u * 3u);

    // the block copies the bake verbatim, shifted by the record's tile origin
    CHECK(pixelAt(pixels, block->size, 0, 0, 0) == 3);
    CHECK(pixelAt(pixels, block->size, 0, 0, 1) == 2);
    CHECK(pixelAt(pixels, block->size, 2, 2, 0) == 5);
    CHECK(pixelAt(pixels, block->size, 2, 2, 1) == 4);
  }

  SECTION("combines records that share the plane")
  {
    auto bake = makeBake(8, 8);
    // left half in one record, right half in another, each in its own tile
    bake.faces.push_back(makeRecord(
      {0, 0}, {2, 3}, {0, 0},
      {{0.0f, 0.0f}, {2.0f, 0.0f}, {2.0f, 3.0f}, {0.0f, 3.0f}}));
    bake.faces.push_back(makeRecord(
      {2, 0}, {2, 3}, {4, 0},
      {{2.0f, 0.0f}, {4.0f, 0.0f}, {4.0f, 3.0f}, {2.0f, 3.0f}}));

    auto pixels = std::vector<std::uint8_t>{};
    const auto block = buildFaceLightmap(makeFace(48.0f), bake, pixels);

    REQUIRE(block.has_value());
    CHECK(block->size == vm::vec2i{4, 4});
    // luxel 0 comes from the first tile (column 0), luxel 3 from the second (column 1)
    CHECK(pixelAt(pixels, block->size, 0, 0, 0) == 1);
    CHECK(pixelAt(pixels, block->size, 3, 0, 0) == 6);
  }

  SECTION("bleeds luxels that no record covers")
  {
    auto bake = makeBake(8, 8);
    // the record only covers the left half of the face's rectangle
    bake.faces.push_back(makeRecord(
      {0, 0}, {3, 3}, {0, 0},
      {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 3.0f}, {0.0f, 3.0f}}));

    auto pixels = std::vector<std::uint8_t>{};
    const auto block = buildFaceLightmap(makeFace(32.0f), bake, pixels);

    REQUIRE(block.has_value());
    CHECK(block->size == vm::vec2i{3, 3});
    // every luxel has data, because the uncovered ones took their neighbour's color
    for (auto y = 0; y < block->size.y(); ++y)
    {
      for (auto x = 0; x < block->size.x(); ++x)
      {
        CHECK(pixelAt(pixels, block->size, x, y, 2) != 0);
      }
    }
  }

  SECTION("chooses an overlapping record when the center was clipped away")
  {
    auto bake = makeBake(8, 8);
    auto unrelated = makeRecord(
      {4, 0}, {2, 3}, {0, 0}, {{4.0f, 0.0f}, {5.0f, 0.0f}, {5.0f, 3.0f}, {4.0f, 3.0f}});
    unrelated.vecs[0][3] = 16.0f;
    bake.faces.push_back(std::move(unrelated));
    bake.faces.push_back(makeRecord(
      {0, 0},
      {2, 3},
      {4, 1},
      {{0.0f, 0.0f}, {0.75f, 0.0f}, {0.75f, 2.0f}, {0.0f, 2.0f}}));

    auto pixels = std::vector<std::uint8_t>{};
    const auto block = buildFaceLightmap(makeFace(32.0f), bake, pixels);

    REQUIRE(block.has_value());
    CHECK(block->vecs[0][3] == 0.0f);
    CHECK(pixelAt(pixels, block->size, 0, 0, 0) == 5);
  }

  SECTION("keeps baked samples for a half-luxel strip split across BSP faces")
  {
    auto bake = makeBake(8, 8);
    bake.faces.push_back(makeRecord(
      {0, 0},
      {2, 2},
      {0, 0},
      {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.5f}, {0.0f, 0.5f}}));
    bake.faces.push_back(makeRecord(
      {1, 0},
      {2, 2},
      {4, 0},
      {{1.0f, 0.0f}, {2.0f, 0.0f}, {2.0f, 0.5f}, {1.0f, 0.5f}}));

    auto face = makeFace(32.0f);
    face.outline = {
      {0.0f, 0.0f, 0.0f}, {32.0f, 0.0f, 0.0f}, {32.0f, 8.0f, 0.0f}, {0.0f, 8.0f, 0.0f}};

    auto pixels = std::vector<std::uint8_t>{};
    const auto block = buildFaceLightmap(face, bake, pixels);

    REQUIRE(block.has_value());
    CHECK(pixelAt(pixels, block->size, 0, 0, 0) == 1);
    CHECK(pixelAt(pixels, block->size, 1, 0, 0) == 5);
  }

  SECTION("fills a sub-luxel strip alongside a record with center samples")
  {
    auto bake = makeBake(8, 8);
    bake.faces.push_back(makeRecord(
      {0, 0},
      {7, 2},
      {0, 0},
      {{0.0f, 0.0f}, {6.0f, 0.0f}, {6.0f, 0.25f}, {0.0f, 0.25f}}));
    bake.faces.push_back(makeRecord(
      {0, 0},
      {2, 7},
      {0, 3},
      {{0.0f, 0.25f}, {1.0f, 0.25f}, {1.0f, 6.0f}, {0.0f, 6.0f}}));

    auto pixels = std::vector<std::uint8_t>{};
    const auto block = buildFaceLightmap(makeFace(96.0f), bake, pixels);

    REQUIRE(block.has_value());
    CHECK(pixelAt(pixels, block->size, 4, 0, 0) == 5);
    CHECK(pixelAt(pixels, block->size, 0, 0, 1) == 4);
  }

  SECTION("preserves the samples used to filter a diagonal edge")
  {
    auto bake = makeBake(8, 8);
    bake.faces.push_back(
      makeRecord({0, 0}, {3, 3}, {2, 1}, {{0.0f, 0.0f}, {2.0f, 0.0f}, {0.0f, 2.0f}}));
    auto face = makeFace(32.0f);
    face.outline.erase(face.outline.begin() + 2);

    auto pixels = std::vector<std::uint8_t>{};
    const auto block = buildFaceLightmap(face, bake, pixels);

    REQUIRE(block.has_value());
    // At (0.5, 1.5), the filter uses (1, 2) even though it lies outside the triangle.
    CHECK(pixelAt(pixels, block->size, 1, 2, 0) == 4);
    CHECK(pixelAt(pixels, block->size, 1, 2, 1) == 4);
  }

  SECTION("returns nothing when no record shares the plane")
  {
    auto bake = makeBake(8, 8);
    auto record = makeRecord(
      {0, 0}, {3, 3}, {0, 0},
      {{0.0f, 0.0f}, {3.0f, 0.0f}, {3.0f, 3.0f}, {0.0f, 3.0f}});
    record.dist = -64.0f;
    record.normal = vm::vec3f{0.0f, 0.0f, -1.0f};
    bake.faces.push_back(std::move(record));

    auto pixels = std::vector<std::uint8_t>{};
    CHECK_FALSE(buildFaceLightmap(makeFace(32.0f), bake, pixels).has_value());
  }
}

TEST_CASE("buildLightmapAssembly")
{
  SECTION("packs one rectangle per face")
  {
    auto bake = makeBake(8, 8);
    bake.faces.push_back(makeRecord(
      {0, 0}, {3, 3}, {0, 0},
      {{0.0f, 0.0f}, {3.0f, 0.0f}, {3.0f, 3.0f}, {0.0f, 3.0f}}));

    auto faces = std::vector<FaceLightmapInput>{makeFace(32.0f), makeFace(16.0f)};
    auto assembly = LightmapAssembly{};
    buildLightmapAssembly(faces, bake, assembly);

    CHECK(assembly.width >= 3);
    CHECK(assembly.height >= 3);
    CHECK(assembly.blocks.size() == 2);
    CHECK(
      assembly.pixels.size()
      == static_cast<size_t>(assembly.width) * static_cast<size_t>(assembly.height) * 3);

    // the rectangles must not overlap, so their tiles differ
    CHECK(assembly.blocks[0].second.tile != assembly.blocks[1].second.tile);
  }

  SECTION("stays empty without baked data")
  {
    auto bake = LightmapBake{};
    auto faces = std::vector<FaceLightmapInput>{makeFace(32.0f)};
    auto assembly = LightmapAssembly{};
    buildLightmapAssembly(faces, bake, assembly);

    CHECK(assembly.width == 0);
    CHECK(assembly.height == 0);
    CHECK(assembly.blocks.empty());
  }
}

} // namespace tb::render
