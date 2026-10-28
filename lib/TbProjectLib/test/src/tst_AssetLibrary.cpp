/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom.
*/

#include "GoldSrcFixtures.h"
#include "base/Uuid.h"
#include "project/AssetLibrary.h"
#include "project/ContentHash.h"
#include "project/GoldSrcWad.h"

#include <sqlite3.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace tb::project
{
namespace
{

struct Workspace
{
  std::filesystem::path directory =
    std::filesystem::temp_directory_path() / ("tb-library-" + generateUuid());
  std::filesystem::path folder = directory / "library";

  Workspace() { std::filesystem::create_directories(folder); }
  ~Workspace()
  {
    auto error = std::error_code{};
    std::filesystem::remove_all(directory, error);
  }

  std::filesystem::path write(
    const std::string& relativePath, const std::vector<std::uint8_t>& bytes) const
  {
    const auto path = folder / std::filesystem::path{relativePath};
    std::filesystem::create_directories(path.parent_path());
    auto file = std::ofstream{path, std::ios::binary};
    file.write(
      reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    file.close();
    return path;
  }
  void remove(const std::string& relativePath) const
  {
    std::filesystem::remove(folder / std::filesystem::path{relativePath});
  }
  void rename(const std::string& from, const std::string& to) const
  {
    std::filesystem::rename(
      folder / std::filesystem::path{from}, folder / std::filesystem::path{to});
  }
};

template <typename T>
T require(Result<T> result)
{
  if (result.is_error())
    FAIL(std::get<Error>(result.error()).msg);
  return std::move(result).value();
}

void require(Result<void> result)
{
  if (result.is_error())
    FAIL(std::get<Error>(result.error()).msg);
}

const LibraryAsset& findByMember(
  const std::vector<LibraryAsset>& assets, const std::string& member)
{
  for (const auto& asset : assets)
    if (asset.sourceMember == member)
      return asset;
  FAIL("missing library asset " + member);
  return assets.front();
}

struct RawDatabase
{
  sqlite3* db = nullptr;
  explicit RawDatabase(const std::filesystem::path& path)
  {
    const auto utf8 = path.u8string();
    REQUIRE(sqlite3_open(reinterpret_cast<const char*>(utf8.c_str()), &db) == SQLITE_OK);
  }
  ~RawDatabase() { sqlite3_close(db); }
  int count(const char* sql)
  {
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    const auto result = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return result;
  }
};

} // namespace

TEST_CASE("AssetLibrary")
{
  auto workspace = Workspace{};
  const auto wall = test::makeMiptex("WALL01", 32, 16, 1);
  const auto floor = test::makeMiptex("FLOOR01", 16, 16, 2);

  SECTION("indexes WAD members as independent grouped assets")
  {
    workspace.write("wads/custom.wad", require(buildGoldSrcWad({wall, floor})));
    auto library = require(AssetLibrary::create(workspace.folder, "工业区素材"));
    CHECK(AssetLibrary::isLibraryFolder(workspace.folder));
    const auto libraryId = library->info().id;
    REQUIRE_FALSE(libraryId.empty());

    const auto scan = require(library->scan());
    CHECK(scan.added == 2);
    CHECK(scan.updated == 0);
    CHECK(scan.missing == 0);
    CHECK(scan.sources == 1);

    const auto assets = require(library->assets());
    REQUIRE(assets.size() == 2);
    const auto& wallAsset = findByMember(assets, "WALL01");
    CHECK(wallAsset.id.size() == 36);
    CHECK(wallAsset.type == AssetType::Texture);
    CHECK(wallAsset.displayName == "WALL01");
    CHECK(wallAsset.group == "wads/custom.wad");
    CHECK(wallAsset.virtualPath == "wads/custom.wad/WALL01");
    CHECK(wallAsset.sourceContainer == "wads/custom.wad");
    CHECK(wallAsset.nativeFormat == "goldsrc-miptex");
    CHECK(wallAsset.revision == 1);
    CHECK(wallAsset.size == wall.size());
    CHECK(wallAsset.contentHash == contentHash(wall));
    CHECK_FALSE(wallAsset.missing);
    CHECK(require(library->readContent(wallAsset.id)) == wall);
    CHECK(require(library->readOriginal(wallAsset.id)) == wall);
    CHECK(require(library->groups()) == std::vector<std::string>{"wads/custom.wad"});

    // A rescan of unchanged content must not invent revisions or new IDs.
    const auto again = require(library->scan());
    CHECK(again.added == 0);
    CHECK(again.updated == 0);
    CHECK(require(library->assets()).size() == 2);
    CHECK(require(library->findAsset(wallAsset.id))->id == wallAsset.id);
  }

  SECTION("advances only the revision of the changed WAD member")
  {
    workspace.write("custom.wad", require(buildGoldSrcWad({wall, floor})));
    auto library = require(AssetLibrary::create(workspace.folder, "Library"));
    require(library->scan());
    const auto before = require(library->assets());
    const auto wallId = findByMember(before, "WALL01").id;
    const auto floorId = findByMember(before, "FLOOR01").id;

    const auto changedWall = test::makeMiptex("WALL01", 32, 16, 200);
    workspace.write("custom.wad", require(buildGoldSrcWad({changedWall, floor})));
    const auto scan = require(library->scan());
    CHECK(scan.updated == 1);
    CHECK(scan.added == 0);

    const auto wallAfter = require(library->findAsset(wallId));
    const auto floorAfter = require(library->findAsset(floorId));
    REQUIRE(wallAfter);
    REQUIRE(floorAfter);
    CHECK(wallAfter->revision == 2);
    CHECK(wallAfter->contentHash == contentHash(changedWall));
    // The other texture keeps its identity and revision: one changed texel must not
    // rewrite every texture of the WAD.
    CHECK(floorAfter->revision == 1);
    CHECK(floorAfter->contentHash == contentHash(floor));

    // Both revisions stay readable for comparison and rollback.
    CHECK(require(library->readContent(wallId, 1)) == wall);
    CHECK(require(library->readContent(wallId)) == changedWall);
    const auto history = require(library->revisions(wallId));
    REQUIRE(history.size() == 2);
    CHECK(history[0].revision == 1);
    CHECK(history[0].contentHash == contentHash(wall));
    CHECK(history[1].revision == 2);
    CHECK(history[1].contentHash == contentHash(changedWall));
  }

  SECTION("keeps missing assets readable after a source disappears")
  {
    workspace.write("custom.wad", require(buildGoldSrcWad({wall})));
    auto library = require(AssetLibrary::create(workspace.folder, "Library"));
    require(library->scan());
    const auto indexed = require(library->assets());
    REQUIRE(indexed.size() == 1);
    const auto id = indexed.front().id;

    workspace.remove("custom.wad");
    const auto scan = require(library->scan());
    CHECK(scan.missing == 1);
    const auto asset = require(library->findAsset(id));
    REQUIRE(asset);
    CHECK(asset->missing);
    CHECK(asset->revision == 1);
    // The catalog still serves the snapshot; the source file is not required to read it.
    CHECK(require(library->readContent(id)) == wall);

    workspace.write("custom.wad", require(buildGoldSrcWad({wall})));
    const auto restored = require(library->scan());
    CHECK(restored.restored == 1);
    CHECK_FALSE(require(library->findAsset(id))->missing);
    CHECK(require(library->findAsset(id))->revision == 1);
  }

  SECTION("preserves identity when a source is moved inside the library")
  {
    workspace.write("custom.wad", require(buildGoldSrcWad({wall})));
    auto library = require(AssetLibrary::create(workspace.folder, "Library"));
    require(library->scan());
    const auto indexed = require(library->assets());
    REQUIRE(indexed.size() == 1);
    const auto before = indexed.front();

    std::filesystem::create_directories(workspace.folder / "archive");
    workspace.rename("custom.wad", "archive/custom.wad");
    const auto scan = require(library->scan());
    CHECK(scan.relocated == 1);
    CHECK(scan.added == 0);
    CHECK(scan.missing == 0);

    const auto after = require(library->findAsset(before.id));
    REQUIRE(after);
    CHECK(after->id == before.id);
    CHECK(after->revision == before.revision);
    CHECK(after->virtualPath == "archive/custom.wad/WALL01");
    CHECK(require(library->assets()).size() == 1);
  }

  SECTION("distinguishes same named assets with different content")
  {
    workspace.write("a/tex.wad", require(buildGoldSrcWad({wall})));
    workspace.write(
      "b/tex.wad", require(buildGoldSrcWad({test::makeMiptex("WALL01", 32, 16, 99)})));
    auto library = require(AssetLibrary::create(workspace.folder, "Library"));
    require(library->scan());

    const auto assets = require(library->assets());
    REQUIRE(assets.size() == 2);
    CHECK(assets[0].displayName == assets[1].displayName);
    CHECK(assets[0].id != assets[1].id);
    CHECK(assets[0].contentHash != assets[1].contentHash);
    CHECK(assets[0].virtualPath != assets[1].virtualPath);
  }

  SECTION("recognizes images models sprites and sounds")
  {
    workspace.write("textures/wall.png", {'P', 'N', 'G', 1, 2, 3});
    workspace.write("models/box.mdl", {'M', 'D', 'L', 1});
    workspace.write("sprites/flare.spr", {'S', 'P', 'R', 1});
    workspace.write("sound/step.wav", {'R', 'I', 'F', 'F'});
    auto library = require(AssetLibrary::create(workspace.folder, "Library"));
    const auto scan = require(library->scan());
    CHECK(scan.added == 4);
    const auto assets = require(library->assets());
    REQUIRE(assets.size() == 4);

    const auto image = std::ranges::find_if(
      assets, [](const auto& asset) { return asset.virtualPath == "textures/wall.png"; });
    REQUIRE(image != assets.end());
    CHECK(image->type == AssetType::Texture);
    CHECK(image->nativeFormat == "image");
    CHECK(image->group == "textures");
    CHECK(image->sourceMember.empty());
    CHECK(image->recipe.find("requiresConversion") != std::string::npos);

    const auto model = std::ranges::find_if(
      assets, [](const auto& asset) { return asset.type == AssetType::Model; });
    REQUIRE(model != assets.end());
    CHECK(model->nativeFormat == "mdl");
    CHECK(model->virtualPath == "models/box.mdl");
    CHECK(require(library->assets(AssetType::Model)).size() == 1);
    CHECK(require(library->assets(std::nullopt, "sprites")).size() == 1);
  }

  SECTION("rejects duplicate members and invalid lumps without losing the archive")
  {
    const auto duplicate = test::makeMiptex("WALL01", 16, 16, 5);
    workspace.write("dup.wad", require(buildGoldSrcWad({wall, duplicate})));
    auto library = require(AssetLibrary::create(workspace.folder, "Library"));
    const auto scan = require(library->scan());
    CHECK(scan.added == 1);
    CHECK(scan.invalidEntries == 1);
    CHECK_FALSE(scan.warning.empty());
    CHECK(require(library->assets()).size() == 1);
  }

  SECTION("deduplicates identical payloads while keeping identities separate")
  {
    const auto bytes = std::vector<std::uint8_t>{'P', 'N', 'G', 9, 9, 9};
    workspace.write("one/wall.png", bytes);
    workspace.write("two/wall.png", bytes);
    auto library = require(AssetLibrary::create(workspace.folder, "Library"));
    require(library->scan());
    const auto assets = require(library->assets());
    REQUIRE(assets.size() == 2);
    CHECK(assets[0].id != assets[1].id);
    CHECK(assets[0].contentHash == assets[1].contentHash);
    auto db = RawDatabase{AssetLibrary::indexPath(workspace.folder)};
    CHECK(db.count("SELECT count(*) FROM content") == 1);
    CHECK(db.count("SELECT count(*) FROM assets") == 2);
  }

  SECTION("opens existing libraries and rejects other folders")
  {
    workspace.write("custom.wad", require(buildGoldSrcWad({wall})));
    auto library = require(AssetLibrary::create(workspace.folder, "Studio"));
    require(library->scan());
    const auto id = library->info().id;
    library.reset();

    CHECK(AssetLibrary::create(workspace.folder, "Again").is_error());
    CHECK(AssetLibrary::open(workspace.directory).is_error());
    auto reopened = require(AssetLibrary::open(workspace.folder));
    CHECK(reopened->info().id == id);
    CHECK(reopened->info().name == "Studio");
    CHECK(require(reopened->assets()).size() == 1);
    CHECK(require(reopened->sources()).size() == 1);
    CHECK(require(reopened->sources()).front().assetCount == 1);
  }

  SECTION("records dependencies between library assets")
  {
    workspace.write("custom.wad", require(buildGoldSrcWad({wall, floor})));
    auto library = require(AssetLibrary::create(workspace.folder, "Library"));
    require(library->scan());
    const auto assets = require(library->assets());
    const auto wallId = findByMember(assets, "WALL01").id;
    const auto floorId = findByMember(assets, "FLOOR01").id;
    require(library->addDependency(wallId, floorId, "companion"));
    require(library->addDependency(wallId, floorId, "companion"));
    CHECK(require(library->dependencies(wallId)).size() == 1);
    CHECK(require(library->dependents(floorId)).size() == 1);
    CHECK(library->addDependency(wallId, "missing", "companion").is_error());
  }
}

} // namespace tb::project
