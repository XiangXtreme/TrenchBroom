/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include "mdl/BrushBuilder.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/ExportOptions.h"
#include "mdl/Map.h"
#include "mdl/MapFixture.h"
#include "mdl/Map_Nodes.h"
#include "mdl/WorldNode.h"
#include "prefs/Preferences.h"
#include "project/ProjectStore.h"
#include "render/LightmapData.h"
#include "ui/BuiltinGoldSrcAssets.h"
#include "ui/LightmapBaker.h"
#include "ui/QPathUtils.h"

#include "kd/result.h"

#include <algorithm>
#include <filesystem>
#include <optional>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace tb::ui
{
namespace
{
void addRoom(mdl::Map& map)
{
  const auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};
  for (const auto& bounds :
       {vm::bbox3d{{-144, -144, -16}, {144, 144, 0}},
        vm::bbox3d{{-144, -144, 128}, {144, 144, 144}},
        vm::bbox3d{{-144, -144, 0}, {-128, 144, 128}},
        vm::bbox3d{{128, -144, 0}, {144, 144, 128}},
        vm::bbox3d{{-128, -144, 0}, {128, -128, 128}},
        vm::bbox3d{{-128, 128, 0}, {128, 144, 128}}})
  {
    auto* node = new mdl::BrushNode{
      builder.createCuboid(bounds, mdl::BrushFace::NoMaterialName) | kdl::value()};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {node}}});
  }
  auto* light = new mdl::EntityNode{mdl::Entity{
    {{"classname", "light"}, {"origin", "0 0 80"}, {"_light", "255 210 150 300"}}}};
  auto* player = new mdl::EntityNode{
    mdl::Entity{{{"classname", "info_player_start"}, {"origin", "0 0 40"}}}};
  mdl::addNodes(map, {{&mdl::parentForNodes(map), {light, player}}});
}

struct BakeResult
{
  bool finished = false;
  std::optional<render::LightmapBake> bake;
  std::string error;

  LightmapBaker::Callback callback()
  {
    return [this](auto result, const auto& message) {
      bake = std::move(result);
      error = message;
      finished = true;
    };
  }
};

void requireBake(const mdl::Map& map, LightmapBaker& baker, BakeResult& result)
{
  REQUIRE(QTest::qWaitFor([&] { return result.finished; }, 30000));
  INFO(result.error);
  REQUIRE(result.bake);
  CHECK(result.error.empty());
  CHECK_FALSE(baker.running());
  CHECK(result.bake->width > 0);
  CHECK(result.bake->height > 0);
  CHECK(
    std::ranges::any_of(result.bake->pixels, [](const auto pixel) { return pixel > 1; }));
  auto assembly = render::LightmapAssembly{};
  render::buildLightmapAssembly(map, *result.bake, assembly);
  CHECK_FALSE(assembly.blocks.empty());
  CHECK_FALSE(assembly.pixels.empty());
}
} // namespace

TEST_CASE("LightmapBaker")
{
  REQUIRE(std::filesystem::is_regular_file(LightmapBaker::workerPath()));
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.create();
  addRoom(map);
  auto baker = LightmapBaker{};
  auto result = BakeResult{};

  SECTION("bundled compiler bakes the live map and assembles brush lightmaps")
  {
    const auto quality = GENERATE(
      Preferences::LightmapQualityFast,
      Preferences::LightmapQualityNormal,
      Preferences::LightmapQualityUltra);
    auto progress = QSignalSpy{&baker, &LightmapBaker::remainingSecondsChanged};
    baker.start(map, 2, quality, result.callback());
    requireBake(map, baker, result);
    CHECK_FALSE(progress.empty());
  }

  SECTION("relative WAD paths retain the editor working directory")
  {
    auto directory = QTemporaryDir{};
    REQUIRE(directory.isValid());
    const auto previous = QDir::currentPath();
    const auto restore = qScopeGuard([&] { QDir::setCurrent(previous); });
    REQUIRE(QDir::setCurrent(directory.path()));
    const auto wad = builtinEmptyTextureWad();
    REQUIRE(wad.is_success());
    auto file = QFile{"relative.wad"};
    REQUIRE(file.open(QIODevice::WriteOnly));
    REQUIRE(
      file.write(
        reinterpret_cast<const char*>(wad.value().data()), qint64(wad.value().size()))
      == qint64(wad.value().size()));
    file.close();
    auto entity = map.worldNode().entity();
    entity.addOrUpdateProperty("wad", "relative.wad");
    map.worldNode().setEntity(std::move(entity));
    baker.start(map, 2, Preferences::LightmapQualityFast, result.callback());
    requireBake(map, baker, result);
  }

  SECTION("project preparation errors do not fall back to map compilation")
  {
    baker.start(
      map,
      2,
      Preferences::LightmapQualityFast,
      result.callback(),
      [](const auto&) -> Result<std::optional<ProjectCompileInfo>> {
        return Error{"Project preview preparation failed."};
      });
    REQUIRE(result.finished);
    CHECK(result.error == "Project preview preparation failed.");
    CHECK_FALSE(result.bake);
    CHECK_FALSE(baker.running());
  }

  SECTION("project resources are read from the actual project schema")
  {
    auto directory = QTemporaryDir{};
    REQUIRE(directory.isValid());
    const auto root = pathFromQString(directory.path());
    const auto exported = map.exportAs(
      mdl::MapExportOptions{root / "room.map", false, std::nullopt, std::nullopt});
    REQUIRE(exported.is_success());
    auto file = QFile{pathAsQString(root / "room.map")};
    REQUIRE(file.open(QIODevice::ReadOnly));
    const auto bytes = file.readAll();
    const auto created =
      project::ProjectStore::create(root / "room.tbproj", "Room", "Half-Life");
    REQUIRE(created.is_success());
    auto input = project::AssetImport{};
    input.type = project::AssetType::Map;
    input.virtualPath = "maps/room.map";
    input.displayName = "Room";
    input.original.assign(bytes.begin(), bytes.end());
    input.compatible = input.original;
    REQUIRE(created.value()->importAsset(input).is_success());
    baker.start(
      map,
      2,
      Preferences::LightmapQualityFast,
      result.callback(),
      [&](const auto&) -> Result<std::optional<ProjectCompileInfo>> {
        return std::optional{ProjectCompileInfo{root / "room.tbproj", "maps/room.map"}};
      });
    requireBake(map, baker, result);
  }

  SECTION("cancel while starting cleans scratch and allows immediate replacement")
  {
    auto cancelled = BakeResult{};
    auto scratch = std::filesystem::path{};
    baker.start(
      map,
      2,
      Preferences::LightmapQualityUltra,
      cancelled.callback(),
      [&](const auto& directory) -> Result<std::optional<ProjectCompileInfo>> {
        scratch = directory;
        return std::optional<ProjectCompileInfo>{};
      });
    baker.cancel();
    CHECK_FALSE(baker.running());
    baker.start(map, 2, Preferences::LightmapQualityFast, result.callback());
    requireBake(map, baker, result);
    REQUIRE(QTest::qWaitFor([&] { return !std::filesystem::exists(scratch); }, 5000));
    CHECK_FALSE(cancelled.finished);
  }

  SECTION("cancelling an active compiler suppresses completion and removes scratch")
  {
    auto scratch = std::filesystem::path{};
    baker.start(
      map,
      2,
      Preferences::LightmapQualityUltra,
      result.callback(),
      [&](const auto& directory) -> Result<std::optional<ProjectCompileInfo>> {
        scratch = directory;
        return std::optional<ProjectCompileInfo>{};
      });
    REQUIRE(QTest::qWaitFor([&] { return !baker.output().empty(); }, 5000));
    REQUIRE(baker.running());
    baker.cancel();
    REQUIRE(QTest::qWaitFor([&] { return !std::filesystem::exists(scratch); }, 5000));
    CHECK_FALSE(result.finished);
    CHECK_FALSE(baker.running());
  }

  SECTION("compiler failure is reported and the next bake succeeds")
  {
    auto emptyFixture = mdl::MapFixture{};
    baker.start(
      emptyFixture.create(), 2, Preferences::LightmapQualityFast, result.callback());
    REQUIRE(QTest::qWaitFor([&] { return result.finished; }, 30000));
    CHECK_FALSE(result.bake);
    CHECK_FALSE(result.error.empty());
    CHECK_FALSE(baker.running());
    result = {};
    baker.start(map, 2, Preferences::LightmapQualityFast, result.callback());
    requireBake(map, baker, result);
  }

  SECTION("destroying a baker suppresses its callback and cleans scratch")
  {
    auto scratch = std::filesystem::path{};
    {
      auto temporary = LightmapBaker{};
      temporary.start(
        map,
        2,
        Preferences::LightmapQualityUltra,
        result.callback(),
        [&](const auto& directory) -> Result<std::optional<ProjectCompileInfo>> {
          scratch = directory;
          return std::optional<ProjectCompileInfo>{};
        });
    }
    REQUIRE(QTest::qWaitFor([&] { return !std::filesystem::exists(scratch); }, 5000));
    CHECK_FALSE(result.finished);
  }
}
} // namespace tb::ui
