/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QTemporaryDir>

#include "base/PreferenceManager.h"
#include "prefs/Preferences.h"
#include "project/AssetLibrary.h"
#include "ui/AssetInspection.h"
#include "ui/AssetLibrarySource.h"
#include "ui/AssetLibraryWindow.h"
#include "ui/BuiltinAssetLibrary.h"
#include "ui/ProjectSession.h"
#include "ui/QPathUtils.h"
#include "ui/TerrainBaker.h"
#include "ui/TerrainMaterial.h"

#include <algorithm>

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
const project::LibraryAsset& libraryAssetNamed(
  const std::vector<project::LibraryAsset>& assets, const std::string& name)
{
  const auto found =
    std::ranges::find(assets, name, &project::LibraryAsset::sourceMember);
  REQUIRE(found != assets.end());
  return *found;
}
} // namespace

TEST_CASE("BuiltinAssetLibrary")
{
  auto temporary = QTemporaryDir{};
  REQUIRE(temporary.isValid());
  const auto root = pathFromQString(temporary.path());
  const auto info = requireProject(ensureBuiltinAssetLibrary(root / "builtin"));
  CHECK(info.id == BuiltinAssetLibraryId);
  auto source = openLocalAssetLibrary(root / "builtin");
  REQUIRE(source);
  const auto assets = requireProject(source->assets());
  REQUIRE(assets.size() == 4);

  SECTION("portable identities and exact procedural base textures")
  {
    const auto copy = requireProject(ensureBuiltinAssetLibrary(root / "other"));
    CHECK(copy.id == info.id);
    auto other = openLocalAssetLibrary(root / "other");
    REQUIRE(other);
    CHECK(requireProject(ensureBuiltinAssetLibrary(root / "builtin")).id == info.id);
    const auto copiedAssets = requireProject(other->assets());
    for (const auto& asset : assets)
    {
      CHECK(std::ranges::any_of(copiedAssets, [&](const auto& item) {
        return item.id == asset.id && item.contentHash == asset.contentHash;
      }));
    }
    for (const auto& texture : builtinTerrainTextures())
    {
      const auto& asset = libraryAssetNamed(assets, texture.name);
      const auto bytes = requireProject(source->readContent(asset.id));
      CHECK(bytes == texture.miptex);
      const auto image =
        requireProject(inspection::decodeImage(bytes, asset.nativeFormat));
      CHECK(image.size() == QSize{128, 128});
    }
  }

  SECTION("read-only source rejects edits and preserves its contents")
  {
    CHECK_FALSE(source->canImport());
    CHECK_FALSE(source->canConvert());
    CHECK_FALSE(source->canManage());
    CHECK(source->removeAsset(assets.front().id).is_error());
    CHECK(source->setFavorite(assets.front().id, true).is_error());
    CHECK(source->renameAsset(assets.front().id, "Changed").is_error());
    CHECK(source->createFolder("textures/changed").is_error());
    CHECK(source->replaceFile("missing.wad", "textures/changed.wad").is_error());
    CHECK(requireProject(source->assets()).size() == 4);
  }

  SECTION("material captures its three textures and survives a project reopen")
  {
    const auto builtin = requireProject(builtinAssetLibraryInfo());
    auto library = requireProject(project::AssetLibrary::open(builtin.path));
    const auto materials = requireProject(library->assets(project::AssetType::Material));
    REQUIRE(materials.size() == 1);
    auto session =
      requireProject(ProjectSession::create(root / "test.tbproj", "Test", "Half-Life"));
    CHECK_FALSE(session->modified());
    const auto connections = requireProject(session->libraries());
    CHECK(std::ranges::any_of(connections, [](const auto& connection) {
      return connection.info.id == BuiltinAssetLibraryId && connection.available;
    }));
    CHECK_FALSE(session->modified());
    CHECK(requireProject(session->store().assets()).empty());
    const auto captured =
      requireProject(session->captureLibraryAsset(builtin.id, materials.front().id));
    CHECK(
      requireProject(session->store().assets(project::AssetType::Texture)).size() == 3);
    CHECK(requireProject(session->store().dependencies(captured.id)).size() == 3);
    const auto images =
      requireProject(loadTerrainMaterialImages(session->store(), captured.id));
    for (const auto& image : images)
      CHECK(image.size() == QSize{128, 128});
    const auto prepared =
      requireProject(loadTerrainMaterial(session->store(), captured.id));
    CHECK_FALSE(prepared.images[0].isNull());
    // Applying one base texture to brushes reuses the dependency already captured.
    const auto textures = requireProject(library->assets(project::AssetType::Texture));
    const auto brushTexture =
      requireProject(session->captureLibraryAsset(builtin.id, textures.front().id));
    CHECK(requireProject(session->store().assets()).size() == 4);
    CHECK(brushTexture.type == project::AssetType::Texture);
    const auto bytes = requireProject(
      session->store().readAsset(brushTexture.id, project::ContentKind::Compatible));
    requireProject(session->save());
    session.reset();
    session = requireProject(ProjectSession::open(root / "test.tbproj"));
    CHECK(
      requireProject(
        session->store().readAsset(brushTexture.id, project::ContentKind::Compatible))
      == bytes);
    CHECK(loadTerrainMaterial(session->store(), captured.id).is_success());
  }

  SECTION("library window can open the built-in source without changing user preference")
  {
    const auto before =
      PreferenceManager::instance().get(Preferences::AssetLibraryDirectory);
    auto window = AssetLibraryWindow{pathAsQString(root / "builtin"), false};
    CHECK(window.libraryId() == BuiltinAssetLibraryId);
    CHECK(
      PreferenceManager::instance().get(Preferences::AssetLibraryDirectory) == before);
  }
}

} // namespace tb::ui
