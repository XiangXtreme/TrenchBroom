/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QFile>
#include <QTemporaryDir>

#include "gl/MaterialManager.h"
#include "img/GoldSrcTexture.h"
#include "mdl/GameFileSystem.h"
#include "mdl/Map.h"
#include "mdl/Map_Entities.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"
#include "ui/ProjectSession.h"
#include "ui/QPathUtils.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{
template <typename T>
T requireProject(Result<T> result)
{
  if (result.is_error())
    FAIL(std::get<Error>(result.error()).msg);
  return std::move(result).value();
}
void requireProject(Result<void> result)
{
  if (result.is_error())
    FAIL(std::get<Error>(result.error()).msg);
}
std::vector<std::uint8_t> readProjectFile(const std::filesystem::path& path)
{
  auto file = QFile{pathAsQString(path)};
  REQUIRE(file.open(QIODevice::ReadOnly));
  const auto bytes = file.readAll();
  REQUIRE(file.error() == QFileDevice::NoError);
  return {bytes.begin(), bytes.end()};
}
project::AssetImport testAsset()
{
  auto asset = project::AssetImport{};
  asset.type = project::AssetType::Sound;
  asset.virtualPath = "sound/test.wav";
  asset.displayName = "Test sound";
  asset.original = {1, 2, 3};
  asset.compatible = asset.original;
  return asset;
}
} // namespace

TEST_CASE("ProjectSession")
{
  auto temporary = QTemporaryDir{};
  REQUIRE(temporary.isValid());
  const auto path = pathFromQString(temporary.path()) / "test.tbproj";
  auto session = requireProject(ProjectSession::create(path, "Test", "Quake"));

  SECTION("isolates unsaved content locks the source and saves portable copies")
  {
    CHECK_FALSE(session->modified());
    CHECK(ProjectSession::open(path).is_error());
    const auto input = testAsset();
    const auto asset = requireProject(session->importAsset(input));
    CHECK(session->modified());
    CHECK(requireProject(project::ProjectStore::open(path))->assets().value().empty());
    const auto copy = path.parent_path() / "copy.tbproj";
    requireProject(session->saveCopy(copy));
    CHECK(session->modified());
    CHECK(session->saveCopy(copy).is_error());
    auto copied = requireProject(project::ProjectStore::open(copy));
    CHECK(
      requireProject(copied->readAsset(asset.id, project::ContentKind::Compatible))
      == input.compatible);
    copied.reset();
    requireProject(session->save());
    CHECK_FALSE(session->modified());
    session.reset();
    session = requireProject(ProjectSession::open(path));
    CHECK(requireProject(session->store().findAsset(asset.id)) == asset);
  }

  SECTION("refuses to overwrite an externally changed source")
  {
    const auto asset = requireProject(session->importAsset(testAsset()));
    {
      auto external = requireProject(project::ProjectStore::open(path));
      requireProject(external->checkpoint("External edit"));
    }
    const auto before = readProjectFile(path);
    CHECK(session->save().is_error());
    CHECK(readProjectFile(path) == before);
    CHECK(session->modified());
    CHECK(requireProject(session->store().findAsset(asset.id)) == asset);
  }

  SECTION("map comparison snapshots and copies never leak staged map changes")
  {
    auto fixture = MapDocumentFixture{};
    auto& document = fixture.create();
    auto& map = document.map();
    const auto source = path.parent_path() / "initial.map";
    requireProject(map.saveTo(source));
    auto input = project::AssetImport{};
    input.type = project::AssetType::Map;
    input.virtualPath = "maps/a.map";
    input.displayName = "A";
    input.original = readProjectFile(source);
    input.compatible = input.original;
    const auto asset = requireProject(session->importAsset(input));
    requireProject(session->registerDocument(document, asset.id));
    requireProject(session->save());
    CHECK_FALSE(map.modified());
    const auto baseline = requireProject(session->checkpoint("Before edit"));
    REQUIRE(mdl::setEntityProperty(map, "message", "Unsaved text"));
    REQUIRE(map.modified());
    CHECK(requireProject(session->compare(baseline.id)).assets.size() == 1);
    CHECK(requireProject(session->store().findAsset(asset.id)) == asset);
    const auto edited = requireProject(session->checkpoint("With edit"));
    CHECK(requireProject(session->store().findAsset(asset.id)) == asset);
    CHECK(requireProject(session->compare(edited.id)).assets.empty());
    const auto copy = path.parent_path() / "dirty-copy.tbproj";
    requireProject(session->saveCopy(copy));
    CHECK(requireProject(session->store().findAsset(asset.id)) == asset);
    CHECK(map.modified());
    CHECK(
      requireProject(project::ProjectStore::open(copy))
        ->readAsset(asset.id, project::ContentKind::Compatible)
        .value()
      != input.compatible);
    requireProject(map.save());
    CHECK_FALSE(map.modified());
    CHECK_FALSE(session->modified());
    CHECK(
      requireProject(
        session->store().readAsset(asset.id, project::ContentKind::Compatible))
      != input.compatible);
    session->unregisterDocument(document);
    CHECK_FALSE(session->hasOpenMaps());
    requireProject(session->restore(baseline.id));
    CHECK(
      requireProject(
        session->store().readAsset(asset.id, project::ContentKind::Compatible))
      == input.compatible);
  }

  SECTION("runtime generations retain publish paths and remove restored content")
  {
    const auto asset = requireProject(session->importAsset(testAsset()));
    const auto baseline = requireProject(session->checkpoint("Before extra"));
    auto fixture = MapDocumentFixture{};
    auto& document = fixture.create();
    auto input = testAsset();
    input.type = project::AssetType::Map;
    input.virtualPath = "maps/test.map";
    const auto mapAsset = requireProject(session->importAsset(input));
    requireProject(session->registerDocument(document, mapAsset.id));
    const auto root = document.map().gameFileSystem().projectDirectory();
    CHECK(readProjectFile(root / "sound/test.wav") == testAsset().compatible);
    input = testAsset();
    input.compatible = {9, 8};
    requireProject(session->replaceAsset(asset.id, input));
    const auto updatedRoot = document.map().gameFileSystem().projectDirectory();
    CHECK(readProjectFile(updatedRoot / "sound/test.wav") == input.compatible);
    CHECK_FALSE(std::filesystem::exists(root));
    const auto moved =
      requireProject(session->moveAsset(asset.id, "Favorites/renamed.wav"));
    CHECK(moved.publishPath == asset.publishPath);
    CHECK(std::filesystem::exists(
      document.map().gameFileSystem().projectDirectory() / "sound/test.wav"));
    session->unregisterDocument(document);
    requireProject(session->restore(baseline.id));
    CHECK(requireProject(session->store().findAsset(asset.id)) == asset);
  }

  SECTION(
    "embedded WADs load and replace materials in multiple maps without source files")
  {
    auto config = mdl::QuakeFixtureConfig;
    config.gameInfo.gameConfig.materialConfig.extensions = {".C"};
    auto firstFixture = MapDocumentFixture{};
    auto secondFixture = MapDocumentFixture{};
    auto& first = firstFixture.create(config);
    auto& second = secondFixture.create(config);
    auto mapInput = testAsset();
    mapInput.type = project::AssetType::Map;
    mapInput.virtualPath = "maps/first.map";
    const auto firstAsset = requireProject(session->importAsset(mapInput));
    mapInput.virtualPath = "maps/second.map";
    const auto secondAsset = requireProject(session->importAsset(mapInput));
    requireProject(session->registerDocument(first, firstAsset.id));
    requireProject(session->registerDocument(second, secondAsset.id));
    auto input = project::AssetImport{};
    input.type = project::AssetType::Texture;
    input.virtualPath = "textures/portable.wad";
    input.displayName = "Portable";
    input.original = {1, 2, 3};
    input.compatible =
      requireProject(
        img::convertGoldSrcTexture(
          {16, 16, std::vector<unsigned char>(16 * 16 * 4, 255)}, "portable"))
        .wad;
    const auto texture = requireProject(session->importAsset(input));
    CHECK(first.map().materialManager().material("portable") != nullptr);
    CHECK(second.map().materialManager().material("portable") != nullptr);
    const auto oldRoot = first.map().gameFileSystem().projectDirectory();
    input.compatible =
      requireProject(
        img::convertGoldSrcTexture(
          {32, 32, std::vector<unsigned char>(32 * 32 * 4, 255)}, "portable"))
        .wad;
    requireProject(session->replaceAsset(texture.id, input));
    CHECK_FALSE(std::filesystem::exists(oldRoot));
    CHECK(first.map().materialManager().material("portable") != nullptr);
    CHECK(second.map().materialManager().material("portable") != nullptr);
    requireProject(session->moveAsset(texture.id, "Favorites/moved.wad"));
    CHECK(first.map().materialManager().material("portable") != nullptr);
    requireProject(session->save());
    session->unregisterDocument(first);
    session->unregisterDocument(second);
    session.reset();
    session = requireProject(ProjectSession::open(path));
    requireProject(session->registerDocument(first, firstAsset.id));
    CHECK(first.map().materialManager().material("portable") != nullptr);
    session->unregisterDocument(first);
  }
}
} // namespace tb::ui
