/*
 Copyright (C) 2026 Kristian Duske

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

#include "ui/ModelBrowserView.h"
#include "ui/QPathUtils.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

namespace tb::ui
{
using namespace Catch::Matchers;

namespace
{
std::vector<std::string> entryTitles(const std::vector<ModelBrowserEntry>& entries)
{
  auto result = std::vector<std::string>{};
  for (const auto& entry : entries)
  {
    result.push_back(entry.title);
  }
  return result;
}

std::vector<BrowserCellType> entryTypes(const std::vector<ModelBrowserEntry>& entries)
{
  auto result = std::vector<BrowserCellType>{};
  for (const auto& entry : entries)
  {
    result.push_back(entry.cellData.type);
  }
  return result;
}

std::vector<std::string> entryPaths(const std::vector<ModelBrowserEntry>& entries)
{
  auto result = std::vector<std::string>{};
  for (const auto& entry : entries)
  {
    result.push_back(entry.cellData.path.generic_string());
  }
  return result;
}
} // namespace

TEST_CASE("ModelBrowserView")
{
  const auto root = std::filesystem::path{"models"};
  const auto assets = std::vector<BrowserAsset>{
    {BrowserCellType::Model, "models/barrel.mdl"},
    {BrowserCellType::Model, "models/crates/wood.mdl"},
    {BrowserCellType::Model, "models/crates/metal.mdl"},
    {BrowserCellType::Model, "models/monsters/ogre.mdl"},
  };

  SECTION("shows top-level folders and models")
  {
    const auto entries = modelBrowserEntries(root, assets, {}, {});

    CHECK_THAT(
      entryTypes(entries),
      Equals(std::vector<BrowserCellType>{
        BrowserCellType::Folder,
        BrowserCellType::Folder,
        BrowserCellType::Model,
      }));
    CHECK_THAT(
      entryTitles(entries),
      Equals(std::vector<std::string>{"crates", "monsters", "barrel.mdl"}));
    CHECK_THAT(
      entryPaths(entries),
      Equals(std::vector<std::string>{"crates", "monsters", "models/barrel.mdl"}));
  }

  SECTION("navigates and searches Unicode folder paths")
  {
    const auto folder = pathFromQString(QString::fromUtf8("自然与地形/树木"));
    auto asset = BrowserAsset{};
    asset.type = BrowserCellType::Model;
    asset.path = folder / "tree.mdl";
    asset.displayName = "tree.mdl";
    asset.projectId = "library";
    const auto unicodeAssets = std::vector<BrowserAsset>{asset};
    const auto top = modelBrowserEntries({}, unicodeAssets, {}, {}, false);
    REQUIRE(top.size() == 1);
    CHECK(top.front().title == "自然与地形");
    const auto nested = modelBrowserEntries({}, unicodeAssets, folder, {}, false);
    REQUIRE(nested.size() == 1);
    CHECK(nested.front().title == "tree.mdl");
  }

  SECTION("shows children inside a folder without a parent card")
  {
    // The breadcrumb and the folder tree navigate upwards, so the grid holds only the
    // folder's own entries.
    const auto entries = modelBrowserEntries(root, assets, "crates", {});

    CHECK_THAT(
      entryTypes(entries),
      Equals(
        std::vector<BrowserCellType>{BrowserCellType::Model, BrowserCellType::Model}));
    CHECK_THAT(
      entryTitles(entries), Equals(std::vector<std::string>{"metal.mdl", "wood.mdl"}));
    CHECK_THAT(
      entryPaths(entries),
      Equals(
        std::vector<std::string>{"models/crates/metal.mdl", "models/crates/wood.mdl"}));
  }

  SECTION("search includes matching nested models and their containing folders")
  {
    const auto entries = modelBrowserEntries(root, assets, {}, "wood");

    CHECK_THAT(
      entryTypes(entries),
      Equals(
        std::vector<BrowserCellType>{BrowserCellType::Folder, BrowserCellType::Model}));
    CHECK_THAT(
      entryTitles(entries),
      Equals(std::vector<std::string>{"crates", "crates/wood.mdl"}));
    CHECK_THAT(
      entryPaths(entries),
      Equals(std::vector<std::string>{"crates", "models/crates/wood.mdl"}));
  }

  SECTION("search includes folders with matching names")
  {
    const auto entries = modelBrowserEntries(root, assets, {}, "monster");

    CHECK_THAT(
      entryTypes(entries), Equals(std::vector<BrowserCellType>{BrowserCellType::Folder}));
    CHECK_THAT(entryTitles(entries), Equals(std::vector<std::string>{"monsters"}));
    CHECK_THAT(entryPaths(entries), Equals(std::vector<std::string>{"monsters"}));
  }

  SECTION("shows unified asset types")
  {
    const auto unifiedRoot = std::filesystem::path{};
    const auto unifiedAssets = std::vector<BrowserAsset>{
      {BrowserCellType::Model, "models/barrel.mdl"},
      {BrowserCellType::Sprite, "sprites/glow01.spr"},
      {BrowserCellType::Sound, "sound/ambience/hum.wav"},
      {BrowserCellType::Prefab, "prefabs/crate.tbprefab"},
    };

    const auto entries = modelBrowserEntries(unifiedRoot, unifiedAssets, {}, {});

    CHECK_THAT(
      entryTypes(entries),
      Equals(std::vector<BrowserCellType>{
        BrowserCellType::Folder,
        BrowserCellType::Folder,
        BrowserCellType::Folder,
        BrowserCellType::Folder,
      }));
    CHECK_THAT(
      entryTitles(entries),
      Equals(std::vector<std::string>{"models", "prefabs", "sound", "sprites"}));
    CHECK_THAT(
      entryPaths(entries),
      Equals(std::vector<std::string>{"models", "prefabs", "sound", "sprites"}));
  }

  SECTION("shows empty unified root folders")
  {
    const auto entries = modelBrowserEntries({}, {}, {}, {});

    CHECK_THAT(
      entryTypes(entries),
      Equals(std::vector<BrowserCellType>{
        BrowserCellType::Folder,
        BrowserCellType::Folder,
        BrowserCellType::Folder,
        BrowserCellType::Folder,
      }));
    CHECK_THAT(
      entryTitles(entries),
      Equals(std::vector<std::string>{"models", "prefabs", "sound", "sprites"}));
    CHECK_THAT(
      entryPaths(entries),
      Equals(std::vector<std::string>{"models", "prefabs", "sound", "sprites"}));
  }

  SECTION("shows assets inside unified folders")
  {
    const auto unifiedRoot = std::filesystem::path{};
    const auto unifiedAssets = std::vector<BrowserAsset>{
      {BrowserCellType::Model, "models/barrel.mdl"},
      {BrowserCellType::Sprite, "sprites/glow01.spr"},
      {BrowserCellType::Sound, "sound/ambience/hum.wav"},
      {BrowserCellType::Prefab, "prefabs/crate.tbprefab"},
    };

    const auto entries = modelBrowserEntries(unifiedRoot, unifiedAssets, "sprites", {});

    CHECK_THAT(
      entryTypes(entries), Equals(std::vector<BrowserCellType>{BrowserCellType::Sprite}));
    CHECK_THAT(entryTitles(entries), Equals(std::vector<std::string>{"glow01.spr"}));
    CHECK_THAT(
      entryPaths(entries), Equals(std::vector<std::string>{"sprites/glow01.spr"}));
  }

  SECTION("shows prefab assets inside the unified prefab folder")
  {
    const auto unifiedRoot = std::filesystem::path{};
    const auto unifiedAssets = std::vector<BrowserAsset>{
      {BrowserCellType::Model, "models/barrel.mdl"},
      {BrowserCellType::Prefab, "prefabs/crate.tbprefab"},
    };

    const auto entries = modelBrowserEntries(unifiedRoot, unifiedAssets, "prefabs", {});

    CHECK_THAT(
      entryTypes(entries), Equals(std::vector<BrowserCellType>{BrowserCellType::Prefab}));
    CHECK_THAT(entryTitles(entries), Equals(std::vector<std::string>{"crate.tbprefab"}));
    CHECK_THAT(
      entryPaths(entries), Equals(std::vector<std::string>{"prefabs/crate.tbprefab"}));
  }

  SECTION("keeps unified listing titles free of the source")
  {
    auto projectAsset = BrowserAsset{BrowserCellType::Model, "models/box.mdl"};
    projectAsset.source = BrowserSource::Project;
    projectAsset.projectId = "project";
    projectAsset.displayName = "Box";
    auto libraryAsset = BrowserAsset{BrowserCellType::Model, "models/crate.mdl"};
    libraryAsset.source = BrowserSource::AssetLibrary;
    libraryAsset.projectId = "library";
    libraryAsset.libraryId = "library";
    libraryAsset.libraryAssetId = "lib-crate";
    libraryAsset.displayName = "Crate";

    // The browser draws a badge per source, so the title stays the resource name.
    const auto entries =
      modelBrowserEntries({}, {projectAsset, libraryAsset}, "models", {}, false);
    CHECK_THAT(entryTitles(entries), Equals(std::vector<std::string>{"Box", "Crate"}));

    // The source name is still searchable even though it is not part of the title.
    const auto found =
      modelBrowserEntries({}, {projectAsset, libraryAsset}, {}, "library", false);
    REQUIRE(found.size() == 2u);
    CHECK(found[1].title == "Crate");
  }

  SECTION("a whole-listing search ignores the folder being browsed")
  {
    // The default scope only searches below the current folder.
    CHECK(modelBrowserEntries(root, assets, "crates", "ogre").empty());

    // The whole-listing scope finds the match from any folder and keeps the path in the
    // title so the result is placeable.
    const auto global = modelBrowserEntries(root, assets, "crates", "ogre", true, true);
    CHECK_THAT(
      entryTitles(global),
      Equals(std::vector<std::string>{"monsters", "monsters/ogre.mdl"}));
    CHECK_THAT(
      entryPaths(global),
      Equals(std::vector<std::string>{"monsters", "models/monsters/ogre.mdl"}));
  }

  SECTION("sound preview button bounds stay inside item")
  {
    const auto itemBounds = LayoutBounds{10.0f, 20.0f, 93.0f, 93.0f};

    const auto buttonBounds = soundPreviewButtonBounds(itemBounds);

    CHECK(buttonBounds.left() >= itemBounds.left());
    CHECK(buttonBounds.top() >= itemBounds.top());
    CHECK(buttonBounds.right() <= itemBounds.right());
    CHECK(buttonBounds.bottom() <= itemBounds.bottom());
    CHECK(soundPreviewButtonHitTest(
      itemBounds, buttonBounds.left() + 1.0f, buttonBounds.top() + 1.0f));
    CHECK_FALSE(
      soundPreviewButtonHitTest(itemBounds, itemBounds.left(), itemBounds.top()));
  }
}

} // namespace tb::ui
