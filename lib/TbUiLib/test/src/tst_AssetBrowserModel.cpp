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

#include "ui/AssetBrowserModel.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

namespace tb::ui
{
using namespace Catch::Matchers;

namespace
{

auto makeTime(const int value)
{
  return std::filesystem::file_time_type{
    std::filesystem::file_time_type::duration{value}};
}

std::vector<std::string> genericStrings(const std::vector<std::filesystem::path>& paths)
{
  auto result = std::vector<std::string>{};
  for (const auto& path : paths)
  {
    result.push_back(path.generic_string());
  }
  return result;
}

std::vector<std::string> rootStrings(
  const std::vector<std::pair<BrowserCellType, std::filesystem::path>>& roots)
{
  auto result = std::vector<std::string>{};
  for (const auto& [type, path] : roots)
  {
    (void)type;
    result.push_back(path.generic_string());
  }
  return result;
}

} // namespace

TEST_CASE("AssetBrowserModel")
{
  SECTION("project revisions invalidate previews independently of file timestamps")
  {
    auto before = BrowserAsset{BrowserCellType::Model, "models/box.mdl"};
    before.projectId = "project";
    before.assetId = "box";
    before.revision = 1;
    auto after = before;
    after.revision = 2;
    CHECK_FALSE(sameBrowserAssetContent(before, after));
    CHECK(
      changedAssetPaths({}, {}, {before}, {after})
      == std::vector<std::filesystem::path>{"models/box.mdl"});
    after = before;
    after.catalogPath = "Favorites/box.mdl";
    CHECK(sameBrowserAssetContent(before, after));
    CHECK_FALSE(before == after);
    CHECK(changedAssetPaths({}, {}, {before}, {after}).empty());
    const auto entries = modelBrowserEntries({}, {after}, "Favorites", {});
    REQUIRE(entries.size() == 2);
    CHECK(entries.back().cellData.path == before.path);
    after = before;
    after.projectId = "another-project";
    CHECK_FALSE(sameBrowserAssetContent(before, after));
    after = before;
    after.absolutePath = "another-cache/box.mdl";
    CHECK_FALSE(sameBrowserAssetContent(before, after));
  }
  SECTION("project maps and texture collections share virtual folders and search")
  {
    auto map = BrowserAsset{BrowserCellType::Map, "maps/test.map"};
    map.projectId = "project";
    map.catalogPath = "Industrial/Layout.map";
    map.displayName = "Factory";
    auto texture = BrowserAsset{BrowserCellType::Texture, "textures/brick.wad"};
    texture.projectId = "project";
    texture.catalogPath = "Industrial/Brick.wad";
    texture.displayName = "Red brick";
    const auto entries = modelBrowserEntries({}, {map, texture}, "Industrial", {});
    REQUIRE(entries.size() == 3);
    CHECK(entries[1].cellData.type == BrowserCellType::Map);
    CHECK(entries[1].cellData.path == map.path);
    CHECK(entries[2].cellData.type == BrowserCellType::Texture);
    CHECK(entries[2].title == "Red brick");
    const auto found = modelBrowserEntries({}, {map, texture}, {}, "Factory");
    REQUIRE(found.size() == 2);
    CHECK(found.back().cellData.path == map.path);
  }
  SECTION("library entries group by container and report snapshot state")
  {
    auto wall = BrowserAsset{BrowserCellType::Texture, "custom.wad/WALL01"};
    wall.source = BrowserSource::AssetLibrary;
    wall.projectId = "library";
    wall.libraryId = "library";
    wall.libraryAssetId = "asset-wall";
    wall.libraryRevision = 2;
    wall.displayName = "WALL01";
    wall.catalogPath = wall.path;
    CHECK(isLibraryAsset(wall));
    CHECK_FALSE(hasProjectSnapshot(wall));
    CHECK(browserAssetStatus(wall).isEmpty());

    auto floor = wall;
    floor.path = "custom.wad/FLOOR01";
    floor.catalogPath = floor.path;
    floor.libraryAssetId = "asset-floor";
    floor.displayName = "FLOOR01";

    // The WAD container appears as a group folder even though the library is a flat
    // catalog of member assets.
    const auto grouped = modelBrowserEntries({}, {wall, floor}, {}, {}, false);
    REQUIRE(grouped.size() == 1);
    CHECK(grouped.front().cellData.type == BrowserCellType::Folder);
    CHECK(grouped.front().cellData.path == std::filesystem::path{"custom.wad"});

    const auto inside = modelBrowserEntries({}, {wall, floor}, "custom.wad", {}, false);
    REQUIRE(inside.size() == 3); // ".." plus both members
    CHECK(inside[1].title == "FLOOR01");
    CHECK(inside[2].title == "WALL01");

    // Marking the entry as captured reports the project revision and the update badge.
    wall.projectAssetId = "project-asset";
    wall.projectRevision = 3;
    wall.projectPublishPath = "textures/WALL01.wad";
    CHECK(hasProjectSnapshot(wall));
    CHECK(browserAssetStatus(wall).contains("3"));
    wall.projectUpdateAvailable = true;
    CHECK(browserAssetStatus(wall).contains("update", Qt::CaseInsensitive));
    wall.missing = true;
    CHECK(browserAssetStatus(wall).contains("Missing", Qt::CaseInsensitive));

    // A newer library revision invalidates previews and entity models through the project
    // snapshot path, not the library path.
    auto previous = BrowserAsset{BrowserCellType::Model, "models/box.mdl"};
    previous.source = BrowserSource::AssetLibrary;
    previous.libraryId = "library";
    previous.libraryAssetId = "asset-box";
    previous.libraryRevision = 1;
    previous.projectPublishPath = "models/box.mdl";
    auto current = previous;
    auto otherSource = previous;
    otherSource.source = BrowserSource::Project;
    CHECK_FALSE(sameBrowserAssetContent(previous, otherSource));
    current.libraryRevision = 2;
    CHECK_FALSE(sameBrowserAssetContent(previous, current));
    CHECK(
      changedAssetPaths({}, {}, {previous}, {current})
      == std::vector<std::filesystem::path>{"models/box.mdl"});
    // An uncaptured library resource has no engine path and must not be reloaded as if it
    // were a map dependency.
    auto uncaptured = previous;
    uncaptured.projectPublishPath.clear();
    CHECK(changedAssetPaths({}, {}, {}, {uncaptured}).empty());
  }

  SECTION("unifies project library and game sources")
  {
    // The project snapshot of a captured library resource is authoritative, and the
    // library's update state is reported on it instead of listing the resource twice.
    auto snapshot = BrowserAsset{BrowserCellType::Model, "models/box.mdl"};
    snapshot.source = BrowserSource::Project;
    snapshot.projectId = "project";
    snapshot.assetId = "asset-box";
    snapshot.revision = 4;
    snapshot.libraryId = "library";
    snapshot.libraryAssetId = "lib-box";
    snapshot.libraryRevision = 3;
    snapshot.projectAssetId = "asset-box";
    snapshot.projectRevision = 4;
    snapshot.projectPublishPath = "models/box.mdl";

    auto library = BrowserAsset{BrowserCellType::Model, "models/box.mdl"};
    library.source = BrowserSource::AssetLibrary;
    library.projectId = "library";
    library.libraryId = "library";
    library.libraryAssetId = "lib-box";
    library.libraryRevision = 5;
    library.projectAssetId = "asset-box";
    library.projectRevision = 4;
    library.projectPublishPath = "models/box.mdl";
    library.projectUpdateAvailable = true;

    auto game = BrowserAsset{BrowserCellType::Model, "models/box.mdl"};
    auto unclaimedGame = BrowserAsset{BrowserCellType::Model, "models/tree.mdl"};

    const auto merged = mergeBrowserAssets({snapshot}, {library}, {game, unclaimedGame});
    REQUIRE(merged.size() == 2u);
    CHECK(merged[0].path == std::filesystem::path{"models/box.mdl"});
    CHECK(merged[0].source == BrowserSource::Project);
    CHECK(merged[0].projectUpdateAvailable);
    CHECK(merged[1].path == std::filesystem::path{"models/tree.mdl"});
    CHECK(merged[1].source == BrowserSource::GameFiles);
  }

  SECTION("keeps an uncaptured library copy instead of the game file at the same path")
  {
    auto library = BrowserAsset{BrowserCellType::Texture, "textures/brick.wad"};
    library.source = BrowserSource::AssetLibrary;
    library.libraryId = "library";
    library.libraryAssetId = "lib-brick";
    library.libraryRevision = 1;
    auto game = BrowserAsset{BrowserCellType::Texture, "textures/brick.wad"};

    const auto merged = mergeBrowserAssets({}, {library}, {game});
    REQUIRE(merged.size() == 1u);
    CHECK(merged[0].source == BrowserSource::AssetLibrary);
    CHECK(merged[0].libraryAssetId == "lib-brick");
  }

  SECTION("filters sources for the layered listings")
  {
    CHECK(
      browserFilterIncludesSource(BrowserSourceFilter::All, BrowserSource::GameFiles));
    CHECK(
      browserFilterIncludesSource(BrowserSourceFilter::All, BrowserSource::AssetLibrary));
    CHECK(
      browserFilterIncludesSource(BrowserSourceFilter::Project, BrowserSource::Project));
    CHECK_FALSE(browserFilterIncludesSource(
      BrowserSourceFilter::Project, BrowserSource::AssetLibrary));
    CHECK_FALSE(browserFilterIncludesSource(
      BrowserSourceFilter::AssetLibrary, BrowserSource::GameFiles));
    CHECK_FALSE(browserFilterIncludesSource(
      BrowserSourceFilter::GameFiles, BrowserSource::Project));
  }

  SECTION("classifies supported asset extensions")
  {
    CHECK(assetTypeForExtension("models/foo.mdl") == BrowserCellType::Model);
    CHECK(assetTypeForExtension("sprites/foo.SPR") == BrowserCellType::Sprite);
    CHECK(assetTypeForExtension("sound/foo.wav") == BrowserCellType::Sound);
    CHECK(assetTypeForExtension("prefabs/crate.TBPREFAB") == BrowserCellType::Prefab);
    CHECK(assetTypeForExtension("maps/foo.bsp") == BrowserCellType::Folder);
  }

  SECTION("uses prefab assets only in the UI browser asset set")
  {
    CHECK_THAT(
      rootStrings(goldSrcAssetRoots()),
      Equals(std::vector<std::string>{"models", "sprites", "sound"}));
    CHECK_THAT(
      genericStrings(goldSrcAssetExtensions()),
      Equals(std::vector<std::string>{".mdl", ".spr", ".wav"}));

    CHECK_THAT(
      rootStrings(assetBrowserRoots()),
      Equals(std::vector<std::string>{"models", "sprites", "sound", "prefabs"}));
    CHECK_THAT(
      genericStrings(assetBrowserExtensions()),
      Equals(std::vector<std::string>{".mdl", ".spr", ".wav", ".tbprefab"}));
  }

  SECTION("collects root model sprite and sound assets")
  {
    const auto assets = collectBrowserAssets(
      {},
      {},
      [](const auto& rootPath) {
        if (rootPath == std::filesystem::path{"models"})
        {
          return Result<std::vector<std::filesystem::path>>{
            std::vector<std::filesystem::path>{"models/tree.mdl", "models/readme.txt"}};
        }
        if (rootPath == std::filesystem::path{"sprites"})
        {
          return Result<std::vector<std::filesystem::path>>{
            std::vector<std::filesystem::path>{"sprites/glow.spr"}};
        }
        if (rootPath == std::filesystem::path{"sound"})
        {
          return Result<std::vector<std::filesystem::path>>{
            std::vector<std::filesystem::path>{"sound/ambience/hum.wav"}};
        }
        return Result<std::vector<std::filesystem::path>>{
          std::vector<std::filesystem::path>{}};
      },
      [](const auto& path) { return Result<std::filesystem::path>{path}; });

    REQUIRE(assets.has_value());
    CHECK_THAT(
      genericStrings(entityModelAssetPaths(*assets)),
      Equals(std::vector<std::string>{"models/tree.mdl", "sprites/glow.spr"}));
    CHECK((*assets)[0].displayName == "tree.mdl");
    CHECK((*assets)[1].displayName == "hum.wav");
    CHECK((*assets)[2].displayName == "glow.spr");
  }

  SECTION("collects prefab assets for the UI browser roots")
  {
    const auto assets = collectBrowserAssets(
      {},
      {},
      [](const auto& rootPath) {
        if (rootPath == std::filesystem::path{"prefabs"})
        {
          return Result<std::vector<std::filesystem::path>>{
            std::vector<std::filesystem::path>{"prefabs/crate.tbprefab"}};
        }
        return Result<std::vector<std::filesystem::path>>{
          std::vector<std::filesystem::path>{}};
      },
      [](const auto& path) { return Result<std::filesystem::path>{path}; },
      assetBrowserRoots());

    REQUIRE(assets.has_value());
    REQUIRE(assets->size() == 1u);
    CHECK((*assets)[0].type == BrowserCellType::Prefab);
    CHECK((*assets)[0].path == std::filesystem::path{"prefabs/crate.tbprefab"});
  }

  SECTION("filters assets outside enabled mod roots")
  {
    const auto assets = collectBrowserAssets(
      "models",
      {std::filesystem::path{"game/mod1"}},
      [](const auto&) {
        return Result<std::vector<std::filesystem::path>>{
          std::vector<std::filesystem::path>{"models/a.mdl", "models/b.mdl"}};
      },
      [](const auto& path) {
        if (path == std::filesystem::path{"models/a.mdl"})
        {
          return Result<std::filesystem::path>{"game/mod1/models/a.mdl"};
        }
        return Result<std::filesystem::path>{"game/mod2/models/b.mdl"};
      });

    REQUIRE(assets.has_value());
    REQUIRE(assets->size() == 1u);
    CHECK((*assets)[0].path == std::filesystem::path{"models/a.mdl"});
    CHECK((*assets)[0].absolutePath == std::filesystem::path{"game/mod1/models/a.mdl"});
  }

  SECTION("keeps prefab assets outside enabled mod roots")
  {
    const auto assets = collectBrowserAssets(
      {},
      {std::filesystem::path{"game/mod1"}},
      [](const auto& rootPath) {
        if (rootPath == std::filesystem::path{"prefabs"})
        {
          return Result<std::vector<std::filesystem::path>>{
            std::vector<std::filesystem::path>{"prefabs/crate.tbprefab"}};
        }
        return Result<std::vector<std::filesystem::path>>{
          std::vector<std::filesystem::path>{}};
      },
      [](const auto& path) {
        return Result<std::filesystem::path>{"user/prefabs" / path.filename()};
      },
      assetBrowserRoots());

    REQUIRE(assets.has_value());
    REQUIRE(assets->size() == 1u);
    CHECK((*assets)[0].type == BrowserCellType::Prefab);
    CHECK((*assets)[0].path == std::filesystem::path{"prefabs/crate.tbprefab"});
  }

  SECTION("detects added modified and removed model assets")
  {
    const auto oldAssets = std::vector<BrowserAsset>{
      {BrowserCellType::Model, "models/old.mdl", "models/old.mdl", makeTime(1)},
      {BrowserCellType::Sprite, "sprites/same.spr", "sprites/same.spr", makeTime(1)},
      {BrowserCellType::Sound, "sound/changed.wav", "sound/changed.wav", makeTime(1)},
      {BrowserCellType::Model, "models/removed.mdl", "models/removed.mdl", makeTime(1)},
    };
    const auto newAssets = std::vector<BrowserAsset>{
      {BrowserCellType::Model, "models/old.mdl", "models/old.mdl", makeTime(2)},
      {BrowserCellType::Sprite, "sprites/same.spr", "sprites/same.spr", makeTime(1)},
      {BrowserCellType::Sound, "sound/changed.wav", "sound/changed.wav", makeTime(2)},
      {BrowserCellType::Model, "models/added.mdl", "models/added.mdl", makeTime(1)},
    };

    const auto changed = changedAssetPaths(
      assetLastWriteTimes(oldAssets),
      assetLastWriteTimes(newAssets),
      oldAssets,
      newAssets);

    CHECK_THAT(
      genericStrings(changed),
      Equals(std::vector<std::string>{
        "models/added.mdl", "models/old.mdl", "models/removed.mdl"}));
  }
}

} // namespace tb::ui
