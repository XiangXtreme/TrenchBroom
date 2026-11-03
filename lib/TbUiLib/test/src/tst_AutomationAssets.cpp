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

#include "mdl/Map.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"
#include "ui/automation/AutomationAssets.h"

#include <algorithm>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("AutomationAssets")
{
  auto fixture = MapDocumentFixture{};
  auto& map = fixture.create(mdl::Quake2FixtureConfig).map();

  SECTION("the game catalog lists materials as textures")
  {
    auto query = AutomationAssetQuery{};
    query.type = BrowserCellType::Texture;
    const auto textures = searchAutomationGameAssets(map, query);
    REQUIRE(textures);
    REQUIRE_FALSE(textures->empty());

    const auto found = std::ranges::find_if(
      *textures, [](const auto& asset) { return asset.path == "lavatest"; });
    REQUIRE(found != textures->end());
    CHECK(found->scope == "game");
    CHECK(found->id == "lavatest");
    CHECK(found->type == "texture");
    CHECK(found->assetType == "texture");
    CHECK(found->publishPath == "lavatest");
    CHECK(found->nativeFormat == "material");
    CHECK_FALSE(found->group.empty());
  }

  SECTION("texture search matches names and respects the limit")
  {
    auto query = AutomationAssetQuery{};
    query.type = BrowserCellType::Texture;
    query.query = "water";
    const auto matches = searchAutomationGameAssets(map, query);
    REQUIRE(matches);
    CHECK(std::ranges::any_of(
      *matches, [](const auto& asset) { return asset.path == "watertest"; }));

    query.limit = 1u;
    const auto limited = searchAutomationGameAssets(map, query);
    REQUIRE(limited);
    CHECK(limited->size() == 1u);
  }

  SECTION("materials resolve by name without opening a file")
  {
    const auto found = findAutomationGameAsset(map, "LAVATEST");
    REQUIRE(found);
    CHECK(found->id == "lavatest");
    CHECK(found->type == "texture");
    CHECK_FALSE(findAutomationGameAsset(map, "definitely-not-a-texture"));
  }
}

} // namespace tb::ui
