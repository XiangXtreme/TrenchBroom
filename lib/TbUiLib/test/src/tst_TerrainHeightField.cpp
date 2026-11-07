/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPolygonF>

#include "mdl/BrushFace.h"
#include "mdl/MapFormat.h"
#include "ui/TerrainHeightField.h"
#include "ui/TerrainRegion.h"

#include "vm/mat_ext.h"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{
TerrainHeightField field(const int columns = 2, const int rows = 2)
{
  auto result = TerrainHeightField::create(columns, rows, 32.0, -64.0, 0.0);
  REQUIRE(result.is_success());
  return std::move(result).value();
}
} // namespace

TEST_CASE("Terrain height field")
{
  SECTION("soft selection uses world-space spherical distance and locks edges")
  {
    auto terrain = field(4, 4);
    const auto center = terrain.index(2, 2);
    const auto neighbor = terrain.index(3, 2);
    auto weights = terrain.softSelectionWeights(
      {64, -64, 0}, {0, 0}, vm::mat4x4d::identity(), 64, 0, true);
    CHECK(weights[center] == 1.0);
    CHECK(weights[neighbor] == Catch::Approx(0.5));
    CHECK(weights[terrain.index(4, 2)] == 0.0);
    terrain.heights[neighbor] = 64;
    weights = terrain.softSelectionWeights(
      {64, -64, 0}, {0, 0}, vm::mat4x4d::identity(), 64, 0, true);
    CHECK(weights[neighbor] == 0.0);
    const auto transform = vm::translation_matrix(vm::vec3d{100, 200, 300})
                           * vm::scaling_matrix(vm::vec3d{2, 2, 2});
    weights =
      terrain.softSelectionWeights({228, 72, 300}, {0, 0}, transform, 64, 0, false);
    CHECK(weights[center] == 1.0);
    CHECK(weights[terrain.index(1, 2)] == 0.0);
    CHECK(
      terrain.softSelectionWeights({0, 0, 0}, {0, 0}, transform, 0, 0, false).empty());
  }

  SECTION("validates its dimensions and heights")
  {
    CHECK(TerrainHeightField::create(0, 2, 32, -64, 0).is_error());
    CHECK(TerrainHeightField::create(2, 2, 0, -64, 0).is_error());
    CHECK(TerrainHeightField::create(2, 2, 32, -64, -64).is_error());
    CHECK(TerrainHeightField::create(4096, 4096, 32, -64, 0).is_error());
    CHECK(
      TerrainHeightField::create(2, 2, 32, -64, std::numeric_limits<double>::quiet_NaN())
        .is_error());
    auto terrain = field();
    CHECK(terrain.valid());
    terrain.heights.pop_back();
    CHECK_FALSE(terrain.valid());
    CHECK_FALSE(terrain.sample({0, 0}));
  }

  SECTION("samples the same triangles that form the surface")
  {
    auto terrain = field(1, 1);
    terrain.heights = {0, 16, 32, 64};
    REQUIRE(terrain.sample({24, 8}));
    CHECK(*terrain.sample({24, 8}) == Catch::Approx(24));
    CHECK(*terrain.sample({8, 24}) == Catch::Approx(32));
    CHECK(*terrain.sample({32, 32}) == Catch::Approx(64));
    CHECK_FALSE(terrain.sample({-1, 0}));
    CHECK_FALSE(terrain.sample({0, 33}));
    const auto samples = terrain.sampleMany(
      std::array{vm::vec2d{24, 8}, vm::vec2d{8, 24}, vm::vec2d{-1, 0}});
    REQUIRE(samples.size() == 3);
    CHECK(samples[0] == terrain.sample({24, 8}));
    CHECK(samples[1] == terrain.sample({8, 24}));
    CHECK_FALSE(samples[2]);
  }

  SECTION("raises and lowers with falloff without moving locked edges")
  {
    auto terrain = field();
    auto result =
      terrain.sculpt(TerrainSculptMode::Raise, {32, 32}, 40, 1, 0, 16, 0, true);
    CHECK(result.changed);
    CHECK_FALSE(result.reachedBottom);
    CHECK(terrain.heights[terrain.index(1, 1)] == Catch::Approx(16));
    CHECK(terrain.heights[terrain.index(0, 1)] == Catch::Approx(0));
    result = terrain.sculpt(TerrainSculptMode::Lower, {32, 32}, 40, 1, 0, 1000, 0, true);
    CHECK(result.changed);
    CHECK(result.reachedBottom);
    CHECK(terrain.heights[terrain.index(1, 1)] == Catch::Approx(-63));
    CHECK(terrain.valid());
  }

  SECTION("smooths from the original neighborhood and flattens to a target")
  {
    auto terrain = field();
    terrain.heights[terrain.index(1, 1)] = 32;
    auto result =
      terrain.sculpt(TerrainSculptMode::Smooth, {32, 32}, 48, 1, 1, 1, 0, false);
    CHECK(result.changed);
    CHECK(terrain.heights[terrain.index(1, 1)] == Catch::Approx(0));
    CHECK(terrain.heights[terrain.index(0, 1)] == Catch::Approx(32.0 / 5.0));
    result = terrain.sculpt(TerrainSculptMode::Flatten, {32, 32}, 16, 1, 1, 1, 24, false);
    CHECK(result.changed);
    CHECK(terrain.heights[terrain.index(1, 1)] == Catch::Approx(24));
  }

  SECTION("ignores invalid brush inputs")
  {
    auto terrain = field();
    const auto original = terrain.heights;
    const auto result = terrain.sculpt(
      TerrainSculptMode::Raise,
      {32, 32},
      32,
      std::numeric_limits<double>::quiet_NaN(),
      0,
      16,
      0,
      false);
    CHECK_FALSE(result.changed);
    CHECK(terrain.heights == original);
  }

  SECTION("does not partially apply a stamp when heights overflow")
  {
    auto terrain = field();
    terrain.heights[terrain.index(1, 1)] = std::numeric_limits<double>::max();
    const auto original = terrain.heights;
    const auto result = terrain.sculpt(
      TerrainSculptMode::Raise,
      {32, 32},
      64,
      1,
      1,
      std::numeric_limits<double>::max(),
      0,
      false);
    CHECK_FALSE(result.changed);
    CHECK(terrain.heights == original);
    CHECK(terrain.valid());
  }
}

TEST_CASE("Terrain height field produces matching closed brushes")
{
  auto terrain = field();
  terrain.heights = {0, 8, 16, 16, 48, 24, 8, 16, 32};
  const auto translation = vm::vec3d{100, 200, 32};
  const auto transform = vm::translation_matrix(translation);
  const auto bounds = vm::bbox3d{{-1024, -1024, -1024}, {1024, 1024, 1024}};
  auto result = createTerrainBrushes(
    terrain, {0, 0}, transform, mdl::MapFormat::Valve, bounds, "TERR_GRASS");
  REQUIRE(result.is_success());
  const auto& brushes = result.value();
  REQUIRE(brushes.size() == 8);

  auto outlines = std::vector<QPolygonF>{};
  auto totalArea = 0.0;
  for (const auto& brush : brushes)
  {
    REQUIRE(brush.faceCount() == 5);
    const auto top = std::ranges::find_if(
      brush.faces(), [](const auto& face) { return face.normal().z() > 1e-6; });
    REQUIRE(top != brush.faces().end());
    auto outline = QPolygonF{};
    auto center = vm::vec3d{};
    for (const auto& vertex : top->vertexPositions())
    {
      const auto local = vertex - translation;
      center = center + local;
      outline.push_back({local.x(), -local.y()});
    }
    REQUIRE(outline.size() == 3);
    center = center / 3.0;
    REQUIRE(terrain.sample({center.x(), -center.y()}));
    CHECK(center.z() == Catch::Approx(*terrain.sample({center.x(), -center.y()})));
    totalArea += terrainArea(outline);
    for (const auto& other : outlines)
      CHECK(terrainArea(outline.intersected(other)) < 1e-6);
    outlines.push_back(std::move(outline));
  }
  CHECK(totalArea == Catch::Approx(64 * 64));

  const auto tooSmall = vm::bbox3d{{-1, -1, -1}, {1, 1, 1}};
  CHECK(createTerrainBrushes(
          terrain, {0, 0}, transform, mdl::MapFormat::Valve, tooSmall, "TERR_GRASS")
          .is_error());
}

TEST_CASE("Sculptable terrain region preserves heights and paint")
{
  auto region = TerrainRegion{{128, -64}, "material-id"};
  region.maxAddedFaces = 100;
  region.maxWadKiB = 256;
  region.minFaceWidth = 8;
  region.patchDensityPercent = 200;
  region.paint({32, 32}, 24, 1);
  region.heightField = field();
  region.heightField->heights[region.heightField->index(1, 1)] = 20.5;

  const auto encoded = region.encode();
  CHECK(encoded.starts_with("5 "));
  auto decoded = TerrainRegion::decode(encoded);
  REQUIRE(decoded.is_success());
  REQUIRE(decoded.value().heightField);
  CHECK(decoded.value().origin == region.origin);
  CHECK(decoded.value().materialAssetId == region.materialAssetId);
  CHECK(decoded.value().maxAddedFaces == 100);
  CHECK(decoded.value().maxWadKiB == 256);
  CHECK(decoded.value().minFaceWidth == 8);
  CHECK(decoded.value().patchDensityPercent == 200);
  CHECK(decoded.value().heightField->heights == region.heightField->heights);
  CHECK(decoded.value().encode() == encoded);

  const auto paintKey = region.paintCacheKey();
  region.heightField->heights[region.heightField->index(1, 1)] += 8;
  CHECK(region.paintCacheKey() == paintKey);
  region.paint({2, 2}, 8, 1);
  CHECK(region.paintCacheKey() != paintKey);

  // Optional, unknown fields can be added without another format version.
  const auto bytes = QByteArray::fromBase64(
    QByteArray::fromStdString(encoded.substr(2)), QByteArray::Base64UrlEncoding);
  auto json = QJsonDocument::fromJson(bytes).object();
  json.insert(QStringLiteral("futureOptionalSetting"), true);
  auto height = json.value(QStringLiteral("heightField")).toObject();
  height.insert(QStringLiteral("futureHeightSetting"), 42);
  json.insert(QStringLiteral("heightField"), height);
  const auto extended =
    "5 "
    + QJsonDocument{json}
        .toJson(QJsonDocument::Compact)
        .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)
        .toStdString();
  const auto extendedRegion = TerrainRegion::decode(extended);
  REQUIRE(extendedRegion.is_success());
  const auto reencoded = extendedRegion.value().encode();
  const auto savedJson =
    QJsonDocument::fromJson(
      QByteArray::fromBase64(
        QByteArray::fromStdString(reencoded.substr(2)), QByteArray::Base64UrlEncoding))
      .object();
  CHECK(savedJson.value(QStringLiteral("futureOptionalSetting")).toBool());
  CHECK(
    savedJson.value(QStringLiteral("heightField"))
      .toObject()
      .value(QStringLiteral("futureHeightSetting"))
      .toInt()
    == 42);

  height.insert(QStringLiteral("heights"), QJsonArray{0});
  json.insert(QStringLiteral("heightField"), height);
  const auto malformed =
    "5 "
    + QJsonDocument{json}
        .toJson(QJsonDocument::Compact)
        .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)
        .toStdString();
  CHECK(TerrainRegion::decode(malformed).is_error());
  CHECK(TerrainRegion::decode("5 !").is_error());
}
} // namespace tb::ui
