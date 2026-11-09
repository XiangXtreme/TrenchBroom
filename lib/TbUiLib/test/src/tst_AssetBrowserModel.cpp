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
#include "ui/QPathUtils.h"

#include <algorithm>

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
  SECTION(
    "configured partition names are displayed and searchable without changing paths")
  {
    const auto names = BrowserPartitionNames{{"models", "Props"}};
    CHECK(browserFolderDisplayName("models", names) == "Props");
    CHECK(browserFolderDisplayName("models/trees", names) == "trees");
    const auto entries = modelBrowserEntries({}, {}, {}, "Props", true, false, names);
    REQUIRE(entries.size() == 1);
    CHECK(entries.front().cellData.path == "models");
    CHECK(entries.front().title == "Props");
  }
  SECTION("shared library filters include tags without confusing content revisions")
  {
    auto metadata = BrowserAssetMetadata{true, {"Industrial", "Ambient loops"}};
    CHECK(assetSearchMatches("Valve", "sound/valve.wav", metadata.tags, "ambient"));
    CHECK(assetSearchMatches("Valve", "sound/valve.wav", metadata.tags, "SOUND/"));
    CHECK_FALSE(assetSearchMatches("Valve", "sound/valve.wav", metadata.tags, "metal"));
    CHECK(assetMetadataMatches(metadata, true, "industrial"));
    CHECK_FALSE(assetMetadataMatches(metadata, true, "ind"));
    CHECK_FALSE(assetMetadataMatches(std::nullopt, true, {}));
    CHECK_FALSE(assetMetadataMatches(std::nullopt, false, "Industrial"));
    CHECK(assetMetadataMatches(std::nullopt, false, {}));
    auto before = BrowserAsset{BrowserCellType::Sound, "sound/valve.wav"};
    auto after = before;
    after.libraryMetadata = metadata;
    CHECK_FALSE(before == after);
    CHECK(sameBrowserAssetContent(before, after));
    const auto entries = modelBrowserEntries({}, {after}, {}, "ambient", false);
    REQUIRE(entries.size() == 2);
    CHECK(entries.back().cellData.path == after.path);
  }
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
    REQUIRE(entries.size() == 1);
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
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].cellData.type == BrowserCellType::Map);
    CHECK(entries[0].cellData.path == map.path);
    CHECK(entries[1].cellData.type == BrowserCellType::Texture);
    CHECK(entries[1].title == "Red brick");
    const auto found = modelBrowserEntries({}, {map, texture}, {}, "Factory");
    REQUIRE(found.size() == 2);
    CHECK(found.back().cellData.path == map.path);
  }
  SECTION("WAD members flatten into textures with optional container groups")
  {
    auto wall = BrowserAsset{BrowserCellType::Texture, "custom.wad/WALL01"};
    wall.textureCollection = "custom.wad";
    CHECK(
      browserAssetDisplayPath(wall, false) == std::filesystem::path{"textures/WALL01"});
    CHECK(
      browserAssetDisplayPath(wall, true)
      == std::filesystem::path{"textures/custom.wad/WALL01"});
    auto other = wall;
    other.path = "nested/other.wad/WALL01";
    other.textureCollection = "nested/other.wad";
    CHECK(
      browserAssetDisplayPath(other, true)
      == std::filesystem::path{"textures/nested/other.wad/WALL01"});
    wall.catalogPath = browserAssetDisplayPath(wall, false);
    other.catalogPath = browserAssetDisplayPath(other, false);
    const auto flat = modelBrowserEntries({}, {wall, other}, "textures", {}, false);
    REQUIRE(flat.size() == 2);
    CHECK(flat[0].cellData.path != flat[1].cellData.path);
    CHECK(wall.path == std::filesystem::path{"custom.wad/WALL01"});
    // A captured member groups by its source WAD, not its generated publish WAD.
    wall.path = "textures/WALL01.wad";
    CHECK(
      browserAssetDisplayPath(wall, true)
      == std::filesystem::path{"textures/custom.wad/WALL01"});
    wall.textureCollection = "textures/custom.wad";
    CHECK(
      browserAssetDisplayPath(wall, true)
      == std::filesystem::path{"textures/custom.wad/WALL01"});
    const auto loose = BrowserAsset{BrowserCellType::Texture, "textures/brick.png"};
    CHECK(browserAssetDisplayPath(loose, true) == loose.path);
  }
  SECTION("project and library assets use type folders without changing their paths")
  {
    auto model = BrowserAsset{BrowserCellType::Model, "Nature/tree.mdl"};
    model.source = BrowserSource::AssetLibrary;
    CHECK(browserAssetDisplayPath(model, false) == "models/Nature/tree.mdl");
    CHECK(model.path == std::filesystem::path{"Nature/tree.mdl"});

    model.catalogPath = "Models/Nature/tree.mdl";
    CHECK(browserAssetDisplayPath(model, false) == "models/Nature/tree.mdl");
    model.catalogPath = pathFromQString(QString::fromUtf8("模型/自然/tree.mdl"));
    CHECK(
      browserAssetDisplayPath(model, false)
      == pathFromQString(QString::fromUtf8("models/自然/tree.mdl")));

    model.catalogPath.clear();
    model.source = BrowserSource::GameFiles;
    CHECK(browserAssetDisplayPath(model, false) == model.path);
    model.source = BrowserSource::Project;
    model.type = BrowserCellType::Sound;
    CHECK(browserAssetDisplayPath(model, false) == "sound/Nature/tree.mdl");
    model.type = BrowserCellType::Texture;
    CHECK(browserAssetDisplayPath(model, false) == "textures/Nature/tree.mdl");
    model.type = BrowserCellType::Material;
    CHECK(browserAssetDisplayPath(model, false) == "materials/Nature/tree.mdl");
    model.type = BrowserCellType::Sprite;
    CHECK(browserAssetDisplayPath(model, false) == "sprites/Nature/tree.mdl");
    model.type = BrowserCellType::Prefab;
    CHECK(browserAssetDisplayPath(model, false) == "prefabs/Nature/tree.mdl");
    model.type = BrowserCellType::Map;
    CHECK(browserAssetDisplayPath(model, false) == "maps/Nature/tree.mdl");
  }
  SECTION("skybox groups have one presentation root independent of engine paths")
  {
    auto sky = BrowserAsset{BrowserCellType::Texture, "gfx/env/desert"};
    sky.skybox = true;
    CHECK(browserAssetDisplayPath(sky, false) == std::filesystem::path{"skybox/desert"});
    CHECK(browserAssetDisplayPath(sky, true) == std::filesystem::path{"skybox/desert"});
    sky.path = "gfx/env/desert.v2";
    CHECK(
      browserAssetDisplayPath(sky, false) == std::filesystem::path{"skybox/desert.v2"});
    auto changed = sky;
    changed.previewIdentity = "changed-face";
    CHECK_FALSE(sameBrowserAssetContent(sky, changed));
    CHECK(sky.path == std::filesystem::path{"gfx/env/desert.v2"});
  }
  SECTION("library organization overrides WAD and skybox source paths")
  {
    auto asset = BrowserAsset{BrowserCellType::Texture, "original.wad/WALL01"};
    asset.source = BrowserSource::AssetLibrary;
    asset.textureCollection = "original.wad";
    asset.catalogPath = "original.wad/WALL01";
    CHECK(browserAssetDisplayPath(asset, false) == "textures/WALL01");
    CHECK(browserAssetDisplayPath(asset, true) == "textures/original.wad/WALL01");
    asset.catalogPath = "textures/Architecture/WALL01";
    CHECK(browserAssetDisplayPath(asset, false) == "textures/Architecture/WALL01");
    CHECK(browserAssetDisplayPath(asset, true) == "textures/Architecture/WALL01");
    asset.skybox = true;
    asset.path = "gfx/env/desert";
    asset.catalogPath = asset.path;
    CHECK(browserAssetDisplayPath(asset, true) == "skybox/desert");
    asset.catalogPath = "skybox/Day/desert";
    CHECK(browserAssetDisplayPath(asset, true) == "skybox/Day/desert");
    CHECK(asset.path == "gfx/env/desert");
  }
  SECTION("library entries report snapshot state")
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
    REQUIRE(inside.size() == 2);
    CHECK(inside[0].title == "FLOOR01");
    CHECK(inside[1].title == "WALL01");

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

  SECTION("filters sources for the layered listings")
  {
    CHECK(
      browserFilterIncludesSource(BrowserSourceFilter::Project, BrowserSource::Project));
    CHECK_FALSE(browserFilterIncludesSource(
      BrowserSourceFilter::Project, BrowserSource::AssetLibrary));
    CHECK_FALSE(browserFilterIncludesSource(
      BrowserSourceFilter::AssetLibrary, BrowserSource::GameFiles));
    CHECK_FALSE(browserFilterIncludesSource(
      BrowserSourceFilter::GameFiles, BrowserSource::Project));
  }

  SECTION("filters the unified listing by asset type")
  {
    const auto assets = std::vector<BrowserAsset>{
      {BrowserCellType::Map, "maps/de_a.map"},
      {BrowserCellType::Texture, "textures/wall.wad"},
      {BrowserCellType::Model, "models/box.mdl"},
      {BrowserCellType::Model, "models/crate.mdl"},
    };

    CHECK(filterBrowserAssets(assets, std::nullopt).size() == assets.size());
    const auto models = filterBrowserAssets(assets, BrowserCellType::Model);
    REQUIRE(models.size() == 2u);
    CHECK(models[0].path == std::filesystem::path{"models/box.mdl"});
    CHECK(models[1].path == std::filesystem::path{"models/crate.mdl"});
    CHECK(filterBrowserAssets(assets, BrowserCellType::Sound).empty());
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
