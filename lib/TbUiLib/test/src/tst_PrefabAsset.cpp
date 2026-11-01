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

#include <QColor>

#include "base/Error.h"
#include "base/PreferenceManager.h"
#include "fs/TestEnvironment.h"
#include "prefs/Preferences.h"
#include "ui/PrefabAsset.h"
#include "ui/SystemPaths.h"

#include <fstream>
#include <variant>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("PrefabAsset")
{
  SECTION("recognizes prefab asset paths")
  {
    CHECK(isPrefabAssetPath("prefabs/crate.tbprefab"));
    CHECK(isPrefabAssetPath("prefabs/crate.TBPREFAB"));
    CHECK_FALSE(isPrefabAssetPath("models/crate.mdl"));
  }

  SECTION("writes and reads prefab text")
  {
    auto env = fs::TestEnvironment{};
    const auto path = env.dir() / "prefabs/crate.tbprefab";
    const auto text = std::string{"// entity 0\n{\n\"classname\" \"info_null\"\n}\n"};

    REQUIRE(writePrefabAsset(path, text));
    const auto readResult = readPrefabAsset(path);

    REQUIRE(readResult);
    CHECK(readResult.value() == text);
  }

  SECTION("reads brush volume and explicit references from prefab text")
  {
    const auto text = std::string{
      "{\n"
      "\"classname\" \"worldspawn\"\n"
      "\"skyname\" \"desert\"\n"
      "{\n"
      "( 0 0 0 ) ( 64 0 0 ) ( 0 64 0 ) BRICK 0 0 0 1 1\n"
      "( 0 0 64 ) ( 64 0 64 ) ( 0 64 64 ) BRICK 0 0 0 1 1\n"
      "}\n"
      "}\n"
      "{\n"
      "\"classname\" \"ambient_generic\"\n"
      "\"message\" \"ambience/wind.wav\"\n"
      "}\n"};

    const auto contents = readPrefabContents(text);
    REQUIRE(contents.is_success());
    const auto& prefab = contents.value();
    CHECK(prefab.brushes == 1);
    CHECK(prefab.entities == 1);
    CHECK(prefab.faces == 2);
    CHECK(prefab.materials == std::vector<std::string>{"BRICK"});
    CHECK(prefab.sounds == std::vector<std::string>{"sound/ambience/wind.wav"});
    CHECK(prefab.skies == std::vector<std::string>{"desert"});
    CHECK(prefab.bounds.size() == vm::vec3d{64, 64, 64});
    CHECK(prefab.brushBounds.size() == 1);

    const auto thumbnail = renderPrefabThumbnail(prefab, 64);
    REQUIRE_FALSE(thumbnail.isNull());
    CHECK(thumbnail.size() == QSize{64, 64});
    CHECK(thumbnail.pixelColor(32, 32).alpha() > 0);
    CHECK(renderPrefabThumbnail(PrefabContents{}, 64).isNull());
  }

  SECTION("rejects prefab text without a worldspawn entity")
  {
    CHECK(readPrefabContents("{\n\"classname\" \"info_null\"\n}\n").is_error());
    CHECK(readPrefabContents("not a map").is_error());
  }

  SECTION("uses user data prefab directory by default")
  {
    CHECK(defaultPrefabDirectory() == SystemPaths::userDataDirectory() / "prefabs");
  }

  SECTION("uses configured prefab directory when set")
  {
    auto env = fs::TestEnvironment{};
    auto& prefs = PreferenceManager::instance();
    prefs.set(Preferences::PrefabDirectory, env.dir());

    CHECK(configuredPrefabDirectory() == env.dir());

    prefs.resetToDefault(Preferences::PrefabDirectory);
  }

  SECTION("builds prefab path from a name")
  {
    CHECK(
      prefabPathForName("prefabs", "crate")
      == std::filesystem::path{"prefabs/crate.tbprefab"});
    CHECK(
      prefabPathForName("prefabs", "crate.tbprefab")
      == std::filesystem::path{"prefabs/crate.tbprefab"});
  }

  SECTION("builds thumbnail path from a prefab path")
  {
    CHECK(
      prefabThumbnailPath("prefabs/crate.tbprefab")
      == std::filesystem::path{"prefabs/crate.png"});
  }

  SECTION("stores prefab material collection metadata as comments")
  {
    const auto prefabText =
      std::string{"// entity 0\n{\n\"classname\" \"info_null\"\n}\n"};

    const auto textWithMetadata = appendPrefabMaterialCollections(
      prefabText,
      {
        "textures/base.wad",
        "textures/detail.wad",
        "textures/base.wad",
      });

    CHECK(
      textWithMetadata
      == std::string{"// tb-prefab-material-collection: textures/base.wad\n"
                     "// tb-prefab-material-collection: textures/detail.wad\n"
                     "// entity 0\n"
                     "{\n"
                     "\"classname\" \"info_null\"\n"
                     "}\n"});
    CHECK(
      prefabMaterialCollections(textWithMetadata)
      == std::vector<std::filesystem::path>{
        "textures/base.wad",
        "textures/detail.wad",
      });
  }

  SECTION("stores prefab wad metadata as comments")
  {
    const auto prefabText =
      std::string{"// entity 0\n{\n\"classname\" \"info_null\"\n}\n"};

    const auto textWithMetadata =
      appendPrefabWadPaths(prefabText, {"halflife.wad", "liquids.wad", "halflife.wad"});

    CHECK(
      textWithMetadata
      == std::string{"// tb-prefab-wad: halflife.wad\n"
                     "// tb-prefab-wad: liquids.wad\n"
                     "// entity 0\n"
                     "{\n"
                     "\"classname\" \"info_null\"\n"
                     "}\n"});
    CHECK(
      prefabWadPaths(textWithMetadata)
      == std::vector<std::string>{"halflife.wad", "liquids.wad"});
  }

  SECTION("rejects duplicate prefab names")
  {
    auto env = fs::TestEnvironment{};
    const auto path = env.dir() / "crate.tbprefab";

    REQUIRE(writePrefabAsset(path, "prefab"));

    const auto result = checkPrefabNameAvailable(env.dir(), "crate");

    REQUIRE_FALSE(result);
    CHECK(std::get<Error>(result.error()).msg == "Prefab already exists: crate.tbprefab");
    CHECK(checkPrefabNameAvailable(env.dir(), "new_crate"));
  }

  SECTION("crops prefab thumbnails around non-background pixels")
  {
    auto image = QImage{100, 80, QImage::Format_RGBA8888};
    image.fill(QColor{32, 32, 32, 255});
    for (auto y = 30; y < 40; ++y)
    {
      for (auto x = 45; x < 55; ++x)
      {
        image.setPixelColor(x, y, QColor{200, 160, 80, 255});
      }
    }

    const auto cropped = cropPrefabThumbnailImage(image);

    CHECK(cropped.width() < image.width());
    CHECK(cropped.height() < image.height());
    CHECK(cropped.width() >= 10);
    CHECK(cropped.height() >= 10);
  }

  SECTION("renames prefab and thumbnail")
  {
    auto env = fs::TestEnvironment{};
    const auto oldPath = env.dir() / "crate.tbprefab";
    const auto newPath = env.dir() / "barrel.tbprefab";

    REQUIRE(writePrefabAsset(oldPath, "prefab"));
    std::ofstream{prefabThumbnailPath(oldPath)} << "png";

    REQUIRE(renamePrefabAsset(oldPath, "barrel"));

    CHECK_FALSE(env.fileExists("crate.tbprefab"));
    CHECK_FALSE(env.fileExists("crate.png"));
    CHECK(env.fileExists("barrel.tbprefab"));
    CHECK(env.fileExists("barrel.png"));
    CHECK(readPrefabAsset(newPath).value() == "prefab");
  }

  SECTION("renames prefab over orphan thumbnail")
  {
    auto env = fs::TestEnvironment{};
    const auto oldPath = env.dir() / "crate.tbprefab";
    const auto newPath = env.dir() / "barrel.tbprefab";

    REQUIRE(writePrefabAsset(oldPath, "prefab"));
    std::ofstream{prefabThumbnailPath(oldPath)} << "png";
    std::ofstream{prefabThumbnailPath(newPath)} << "orphan";

    REQUIRE(renamePrefabAsset(oldPath, "barrel"));

    CHECK_FALSE(env.fileExists("crate.tbprefab"));
    CHECK_FALSE(env.fileExists("crate.png"));
    CHECK(env.fileExists("barrel.tbprefab"));
    CHECK(env.fileExists("barrel.png"));
  }

  SECTION("deletes prefab and thumbnail")
  {
    auto env = fs::TestEnvironment{};
    const auto path = env.dir() / "crate.tbprefab";

    REQUIRE(writePrefabAsset(path, "prefab"));
    std::ofstream{prefabThumbnailPath(path)} << "png";

    REQUIRE(deletePrefabAsset(path));

    CHECK_FALSE(env.fileExists("crate.tbprefab"));
    CHECK_FALSE(env.fileExists("crate.png"));
  }
}

} // namespace tb::ui
