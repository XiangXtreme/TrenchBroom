/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.
 */

#include "base/Uuid.h"
#include "project/ContentHash.h"
#include "project/ProjectStore.h"

#include <sqlite3.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>

namespace tb::project
{
namespace
{

struct Workspace
{
  std::filesystem::path directory =
    std::filesystem::temp_directory_path() / generateUuid();
  std::filesystem::path path = directory / std::filesystem::path{u8"测试工程.tbproj"};

  Workspace() { std::filesystem::create_directories(directory); }
  ~Workspace()
  {
    auto error = std::error_code{};
    std::filesystem::remove_all(directory, error);
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

AssetImport texture(const std::string& path = "工业区/brick")
{
  return {
    AssetType::Texture,
    path,
    "砖墙",
    "brick.png",
    {0, 1, 255},
    {4, 5, 0, 128},
    R"({"converter":"test","version":1})"};
}

AssetImport map(const std::string& path)
{
  return {AssetType::Map, path, path, "", {}, {'{', '\n', '}'}, ""};
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
  void exec(const char* sql)
  {
    REQUIRE(sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK);
  }
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

TEST_CASE("ProjectStore")
{
  auto workspace = Workspace{};

  SECTION("creates a portable Unicode project and persists embedded content")
  {
    auto store =
      require(ProjectStore::create(workspace.path, "工业区", "Counter-Strike"));
    const auto projectId = store->info().id;
    REQUIRE_FALSE(projectId.empty());
    CHECK(store->info().name == "工业区");
    CHECK(store->info().gameId == "Counter-Strike");
    CHECK(store->path() == workspace.path);
    const auto input = texture();
    const auto asset = require(store->importAsset(input));
    CHECK(asset.revision == 1);
    CHECK(require(store->readAsset(asset.id, ContentKind::Original)) == input.original);
    CHECK(
      require(store->readAsset(asset.id, ContentKind::Compatible)) == input.compatible);
    store.reset();

    const auto copy = workspace.directory / "shared.tbproj";
    std::filesystem::copy_file(workspace.path, copy);
    std::filesystem::remove(workspace.path);
    store = require(ProjectStore::open(copy));
    CHECK(store->info().id == projectId);
    CHECK(require(store->findAsset(asset.id)) == asset);
    CHECK(require(store->readAsset(asset.id, ContentKind::Original)) == input.original);
    CHECK(
      require(store->readAsset(asset.id, ContentKind::Compatible)) == input.compatible);
  }

  SECTION("two map documents share stable references across reimport rename and move")
  {
    auto store = require(ProjectStore::create(workspace.path, "Test", "Counter-Strike"));
    auto asset = require(store->importAsset(texture()));
    const auto a = require(store->importAsset(map("maps/de_a")));
    const auto b = require(store->importAsset(map("maps/de_b")));
    require(store->addDependency(a.id, asset.id, "face-material"));
    require(store->addDependency(b.id, asset.id, "face-material"));
    require(store->addDependency(a.id, asset.id, "face-material"));
    CHECK(require(store->dependents(asset.id)).size() == 2);
    CHECK(require(store->assets(AssetType::Map)).size() == 2);

    auto replacement = texture("incoming/renamed.png");
    replacement.displayName = "外部名称";
    replacement.compatible = {9, 8, 7};
    const auto replaced = require(store->replaceAsset(asset.id, replacement));
    CHECK(replaced.id == asset.id);
    CHECK(replaced.virtualPath == asset.virtualPath);
    CHECK(replaced.displayName == asset.displayName);
    CHECK(replaced.revision == 2);
    const auto renamed = require(store->renameAsset(asset.id, "新砖墙"));
    const auto moved = require(store->moveAsset(asset.id, "共享/新砖墙"));
    CHECK(renamed.id == asset.id);
    CHECK(moved.revision == 4);
    CHECK_FALSE(require(store->findAssetByPath(asset.virtualPath)));
    CHECK(require(store->findAssetByPath(moved.virtualPath)) == moved);
    store.reset();
    store = require(ProjectStore::open(workspace.path));
    for (const auto& document : require(store->assets(AssetType::Map)))
    {
      const auto deps = require(store->dependencies(document.id));
      REQUIRE(deps.size() == 1);
      CHECK(deps.front().dependencyId == asset.id);
      CHECK(
        require(store->readAsset(deps.front().dependencyId, ContentKind::Compatible))
        == replacement.compatible);
    }
    CHECK(
      require(store->readAsset(asset.id, ContentKind::Compatible, 1))
      == texture().compatible);
  }

  SECTION("checkpoints preserve content metadata maps and dependencies with safe restore")
  {
    auto store =
      require(ProjectStore::create(workspace.path, "History", "Counter-Strike"));
    const auto asset = require(store->importAsset(texture()));
    const auto document = require(store->importAsset(map("maps/a")));
    require(store->addDependency(document.id, asset.id, "face-material"));
    const auto baseline = require(store->checkpoint("初版", "可发布版本"));
    auto replacement = texture();
    replacement.original = {111};
    replacement.compatible = {222};
    replacement.conversionRecipe = "new recipe";
    require(store->replaceAsset(asset.id, replacement));
    require(store->renameAsset(asset.id, "改名"));
    require(store->moveAsset(asset.id, "other/wall"));
    const auto extra = require(store->importAsset(texture("unused/second")));
    require(store->addDependency(document.id, extra.id, "face-material"));
    const auto delta = require(store->compare(baseline.id));
    REQUIRE(delta.assets.size() == 2);
    CHECK(delta.addedDependencies.size() == 1);
    CHECK(delta.removedDependencies.empty());

    const auto safety = require(store->restore(baseline.id));
    CHECK(require(store->compare(baseline.id)).assets.empty());
    CHECK(require(store->compare(baseline.id)).addedDependencies.empty());
    CHECK(require(store->findAsset(asset.id)) == asset);
    CHECK_FALSE(require(store->findAsset(extra.id)));
    CHECK(
      require(store->readAsset(asset.id, ContentKind::Original)) == texture().original);
    CHECK(
      require(store->readAsset(asset.id, ContentKind::Compatible))
      == texture().compatible);
    REQUIRE(require(store->checkpoints()).size() == 2);
    CHECK(require(store->checkpoints()).front().description == "可发布版本");
    store.reset();
    store = require(ProjectStore::open(workspace.path));
    require(store->restore(safety.id));
    CHECK(require(store->findAsset(extra.id)).has_value());
    CHECK(
      require(store->readAsset(asset.id, ContentKind::Compatible))
      == replacement.compatible);
    CHECK(require(store->dependencies(document.id)).size() == 2);
    require(store->restore(baseline.id));
    const auto next = require(store->replaceAsset(asset.id, replacement));
    CHECK(
      next.revision == 2); // Identical content and metadata reuse an immutable revision.
    CHECK(
      require(store->readAsset(asset.id, ContentKind::Compatible, 2))
      == replacement.compatible);
    CHECK(store->restore("missing").is_error());
    CHECK(require(store->findAsset(asset.id))->revision == 2);
  }

  SECTION("failed mutations roll back payloads revisions and references")
  {
    auto store =
      require(ProjectStore::create(workspace.path, "Rollback", "Counter-Strike"));
    const auto asset = require(store->importAsset(texture()));
    auto duplicate = texture();
    duplicate.original = {14, 15};
    duplicate.compatible = {16};
    CHECK(store->importAsset(duplicate).is_error());
    CHECK(require(store->assets()).size() == 1);
    CHECK(require(store->findAsset(asset.id)) == asset);
    auto db = RawDatabase{workspace.path};
    CHECK(db.count("SELECT count(*) FROM content") == 2);
    CHECK(db.count("SELECT count(*) FROM revisions") == 1);
    CHECK(store->replaceAsset("missing", texture()).is_error());
    auto wrongType = texture();
    wrongType.type = AssetType::Model;
    CHECK(store->replaceAsset(asset.id, wrongType).is_error());
    CHECK(store->renameAsset(asset.id, "").is_error());
    CHECK(store->moveAsset(asset.id, "../escape").is_error());
    CHECK(store->addDependency(asset.id, "missing", "test").is_error());
    CHECK(store->addDependency(asset.id, asset.id, "test").is_error());
    CHECK(require(store->dependencies(asset.id)).empty());
    CHECK(db.count("SELECT count(*) FROM revisions") == 1);
    CHECK(store->restore("missing").is_error());
    CHECK(require(store->checkpoints()).empty());
    // Storage must remain usable after every rejected operation.
    CHECK(require(store->renameAsset(asset.id, "valid")).revision == 2);
  }

  SECTION("rejects cycles and reports changes to dependency edges")
  {
    auto store = require(ProjectStore::create(workspace.path, "Graph", "Counter-Strike"));
    const auto a = require(store->importAsset(texture("a")));
    const auto b = require(store->importAsset(texture("b")));
    const auto c = require(store->importAsset(texture("c")));
    const auto baseline = require(store->checkpoint("empty graph"));
    require(store->addDependency(a.id, b.id, "uses"));
    require(store->addDependency(b.id, c.id, "uses"));
    CHECK(store->addDependency(c.id, a.id, "uses").is_error());
    CHECK(require(store->dependencies(c.id)).empty());
    const auto delta = require(store->compare(baseline.id));
    CHECK(delta.assets.empty());
    CHECK(delta.addedDependencies.size() == 2);
    const auto safety = require(store->restore(baseline.id));
    CHECK(require(store->compare(safety.id)).removedDependencies.size() == 2);
  }

  SECTION("deduplicates equal content and preserves empty binary payloads")
  {
    auto store = require(ProjectStore::create(workspace.path, "Blobs", "Counter-Strike"));
    auto input = texture();
    input.compatible = input.original;
    const auto a = require(store->importAsset(input));
    input.virtualPath = "another";
    const auto b = require(store->importAsset(input));
    CHECK(a.id != b.id);
    require(store->renameAsset(a.id, "rename"));
    require(store->checkpoint("snapshot"));
    CHECK(RawDatabase{workspace.path}.count("SELECT count(*) FROM content") == 1);
    input.original.clear();
    input.compatible.clear();
    require(store->replaceAsset(b.id, input));
    CHECK(require(store->readAsset(b.id, ContentKind::Original)).empty());
    CHECK(require(store->readAsset(b.id, ContentKind::Compatible)).empty());
    CHECK(RawDatabase{workspace.path}.count("SELECT count(*) FROM content") == 2);
  }

  SECTION("validates virtual paths and handles literal wildcard folder names")
  {
    auto store = require(ProjectStore::create(workspace.path, "Paths", "Counter-Strike"));
    for (const auto* path :
         {"",
          "/abs",
          "D:/abs",
          "a\\b",
          "a//b",
          "a/./b",
          "a/../b",
          "a/",
          "a/CON.wav",
          "a/trailing.",
          "a/trailing ",
          "a/*"})
    {
      CHECK(store->importAsset(texture(path)).is_error());
    }
    require(store->importAsset(texture("a_%/wall")));
    require(store->importAsset(texture("abc/wall")));
    CHECK(require(store->assets(AssetType::Texture, "a_%")).size() == 1);
    CHECK(store->assets(std::nullopt, "../").is_error());
    CHECK_FALSE(require(store->findAsset("missing")));
    CHECK(store->readAsset("missing", ContentKind::Compatible).is_error());
  }

  SECTION("consistent share copies contain content and history without changing source")
  {
    auto store = require(ProjectStore::create(workspace.path, "Copy", "Counter-Strike"));
    const auto asset = require(store->importAsset(texture()));
    const auto baseline = require(store->checkpoint("initial"));
    const auto copy = workspace.directory / "copy.tbproj";
    require(store->saveCopy(copy));
    CHECK(store->saveCopy(copy).is_error());
    CHECK(store->saveCopy(workspace.path).is_error());
    auto copied = require(ProjectStore::open(copy));
    CHECK(copied->info().id == store->info().id);
    CHECK(
      require(copied->readAsset(asset.id, ContentKind::Original)) == texture().original);
    CHECK(require(copied->compare(baseline.id)).assets.empty());
    require(copied->renameAsset(asset.id, "copy only"));
    CHECK(require(store->findAsset(asset.id)) == asset);
  }

  SECTION("catalog moves preserve game paths and reject ambiguous materialization")
  {
    auto store = require(ProjectStore::create(workspace.path, "Paths", "Counter-Strike"));
    const auto asset = require(store->importAsset(texture("models/box.mdl")));
    const auto moved = require(store->moveAsset(asset.id, "Library/Props/box"));
    CHECK(moved.publishPath == "models/box.mdl");
    CHECK(store->importAsset(texture("models/BOX.mdl")).is_error());
    CHECK(store->importAsset(texture("models/box.mdl/child")).is_error());
    CHECK(store->importAsset(texture("Library/Props/box/child")).is_error());
    require(store->selectRevisions({asset}));
    CHECK(require(store->findAsset(asset.id)) == asset);
    auto invalid = asset;
    invalid.revision = 1234;
    CHECK(store->selectRevisions({moved, invalid}).is_error());
    CHECK(require(store->findAsset(asset.id)) == asset);
    require(store->selectRevisions({moved}));
    CHECK(require(store->findAsset(asset.id)) == moved);
  }

  SECTION("removed content remains recoverable and referenced assets cannot be removed")
  {
    auto store =
      require(ProjectStore::create(workspace.path, "Delete", "Counter-Strike"));
    const auto asset = require(store->importAsset(texture()));
    const auto document = require(store->importAsset(map("maps/a")));
    require(store->addDependency(document.id, asset.id, "uses"));
    const auto baseline = require(store->checkpoint("Before deletion"));
    CHECK(store->removeAsset(asset.id).is_error());
    require(store->removeAsset(document.id));
    require(store->removeAsset(asset.id));
    CHECK(require(store->assets()).empty());
    CHECK(require(store->compare(baseline.id)).assets.size() == 2);
    require(store->restore(baseline.id));
    CHECK(require(store->findAsset(asset.id)) == asset);
    CHECK(require(store->dependents(asset.id)).size() == 1);
  }

  SECTION("untrusted asset paths cannot escape the project cache")
  {
    auto store =
      require(ProjectStore::create(workspace.path, "Invalid", "Counter-Strike"));
    require(store->importAsset(texture()));
    store.reset();
    {
      auto db = RawDatabase{workspace.path};
      db.exec(
        "UPDATE revisions SET asset_id='../outside'; UPDATE heads SET "
        "asset_id='../outside'");
    }
    CHECK(ProjectStore::open(workspace.path).is_error());
  }

  SECTION("does not create missing projects or overwrite existing files")
  {
    CHECK(ProjectStore::open(workspace.path).is_error());
    CHECK_FALSE(std::filesystem::exists(workspace.path));
    {
      auto file = std::ofstream{workspace.path, std::ios::binary};
      file << "not a project";
    }
    CHECK(ProjectStore::create(workspace.path, "Test", "Counter-Strike").is_error());
    CHECK(ProjectStore::open(workspace.path).is_error());
    auto file = std::ifstream{workspace.path};
    auto text = std::string{};
    std::getline(file, text);
    CHECK(text == "not a project");
  }

  SECTION("rejects unrelated and future databases without migrating them")
  {
    {
      auto db = RawDatabase{workspace.path};
      db.exec(
        "CREATE TABLE unrelated(value TEXT); INSERT INTO unrelated VALUES('keep');");
    }
    CHECK(ProjectStore::open(workspace.path).is_error());
    CHECK(RawDatabase{workspace.path}.count("SELECT count(*) FROM unrelated") == 1);
    std::filesystem::remove(workspace.path);
    require(ProjectStore::create(workspace.path, "Test", "Counter-Strike")).reset();
    {
      auto db = RawDatabase{workspace.path};
      db.exec("PRAGMA user_version=999");
    }
    CHECK(ProjectStore::open(workspace.path).is_error());
    CHECK(RawDatabase{workspace.path}.count("PRAGMA user_version") == 999);
  }

  SECTION("busy writers fail without leaving partial state")
  {
    auto store = require(ProjectStore::create(workspace.path, "Lock", "Counter-Strike"));
    auto lock = RawDatabase{workspace.path};
    lock.exec("BEGIN IMMEDIATE");
    CHECK(store->importAsset(texture()).is_error());
    lock.exec("ROLLBACK");
    CHECK(require(store->assets()).empty());
    CHECK(require(store->importAsset(texture())).revision == 1);
  }

  SECTION("captures library resources as project snapshots with provenance")
  {
    auto store =
      require(ProjectStore::create(workspace.path, "Snapshot", "Counter-Strike"));
    const auto libraryId = generateUuid();
    const auto libraryAssetId = generateUuid();
    auto link = LibraryLink{
      libraryId, libraryAssetId, 7, std::string(64, 'a'), "wads/custom.wad", "WALL01"};
    auto input = texture("textures/WALL01");
    input.library = link;
    input.sourceName = "custom.wad";
    const auto asset = require(store->importAsset(input));
    CHECK(asset.revision == 1);
    CHECK(asset.contentHash == contentHash(input.compatible));
    CHECK(asset.library == link);
    CHECK(asset.publishPath == "textures/WALL01");

    const auto linked = require(store->libraryLinkedAssets());
    REQUIRE(linked.size() == 1);
    CHECK(linked.front().id == asset.id);
    CHECK(require(store->findAssetByLibraryAsset(libraryId, libraryAssetId)) == asset);
    CHECK_FALSE(require(store->findAssetByLibraryAsset(libraryId, generateUuid())));

    require(store->attachLibrary(libraryId, "工业区素材", "D:/assets/studio"));
    const auto libraries = require(store->libraries());
    REQUIRE(libraries.size() == 1);
    CHECK(libraries.front().id == libraryId);
    CHECK(libraries.front().name == "工业区素材");
    CHECK(libraries.front().path == "D:/assets/studio");
    require(store->attachLibrary(libraryId, "改名素材", "E:/moved"));
    REQUIRE(require(store->libraries()).size() == 1);
    CHECK(require(store->libraries()).front().path == "E:/moved");

    require(store->ignoreLibraryUpdate(asset.id, std::string(64, 'b')));
    require(store->ignoreLibraryUpdate(asset.id, std::string(64, 'b')));
    CHECK(
      require(store->ignoredLibraryUpdates(asset.id))
      == std::vector<std::string>{std::string(64, 'b')});
    CHECK(store->ignoreLibraryUpdate(asset.id, "").is_error());
    CHECK(store->ignoreLibraryUpdate("missing", std::string(64, 'b')).is_error());
  }

  SECTION("library updates create new revisions and keep the previous snapshot")
  {
    auto store =
      require(ProjectStore::create(workspace.path, "Update", "Counter-Strike"));
    const auto libraryId = generateUuid();
    const auto libraryAssetId = generateUuid();
    const auto first = LibraryLink{
      libraryId, libraryAssetId, 1, std::string(64, 'a'), "custom.wad", "WALL01"};
    auto input = texture("textures/WALL01");
    input.library = first;
    const auto captured = require(store->importAsset(input));
    const auto a = require(store->importAsset(map("maps/de_a")));
    const auto b = require(store->importAsset(map("maps/de_b")));
    require(store->addDependency(a.id, captured.id, "face-material"));
    require(store->addDependency(b.id, captured.id, "face-material"));

    auto updated = texture("textures/WALL01");
    updated.library = {
      libraryId, libraryAssetId, 2, std::string(64, 'b'), "custom.wad", "WALL01"};
    updated.compatible = {42, 43, 44};
    const auto next = require(store->replaceAsset(captured.id, updated));
    CHECK(next.id == captured.id);
    CHECK(next.revision == 2);
    CHECK(next.library.libraryRevision == 2);
    CHECK(next.contentHash == contentHash(updated.compatible));
    // Every map keeps referencing the same stable project asset ID.
    CHECK(require(store->dependents(captured.id)).size() == 2);
    CHECK(
      require(store->readAsset(captured.id, ContentKind::Compatible))
      == updated.compatible);
    CHECK(
      require(store->readAsset(captured.id, ContentKind::Compatible, 1))
      == input.compatible);

    // Replaying the same library revision reuses the immutable revision.
    auto replay = texture("textures/WALL01");
    replay.library = updated.library;
    replay.compatible = updated.compatible;
    CHECK(require(store->replaceAsset(captured.id, replay)).revision == 2);

    // A failed update leaves the current revision untouched.
    auto broken = texture("textures/WALL01");
    broken.library = {
      libraryId, libraryAssetId, 3, std::string(64, 'c'), "custom.wad", "WALL01"};
    CHECK(store->replaceAsset("missing", broken).is_error());
    CHECK(require(store->findAsset(captured.id))->revision == 2);
    CHECK(
      require(store->readAsset(captured.id, ContentKind::Compatible))
      == updated.compatible);

    // Rolling back to the previous library revision is an explicit revision selection.
    require(store->selectRevisions({captured}));
    CHECK(require(store->findAsset(captured.id))->revision == 1);
    CHECK(
      require(store->readAsset(captured.id, ContentKind::Compatible))
      == input.compatible);
  }

  SECTION("rejects incomplete library provenance")
  {
    auto store =
      require(ProjectStore::create(workspace.path, "Provenance", "Counter-Strike"));
    auto missingLibrary = texture();
    missingLibrary.library =
      LibraryLink{"", generateUuid(), 1, std::string(64, 'a'), "a.wad", "X"};
    CHECK(store->importAsset(missingLibrary).is_error());
    auto missingRevision = texture();
    missingRevision.library =
      LibraryLink{generateUuid(), generateUuid(), 0, std::string(64, 'a'), "a.wad", "X"};
    CHECK(store->importAsset(missingRevision).is_error());
    auto missingContainer = texture();
    missingContainer.library =
      LibraryLink{generateUuid(), generateUuid(), 1, std::string(64, 'a'), "", "X"};
    CHECK(store->importAsset(missingContainer).is_error());
    CHECK(require(store->assets()).empty());
  }

  SECTION("migrates format 1 projects and backfills content hashes")
  {
    {
      auto db = RawDatabase{workspace.path};
      db.exec(
        "PRAGMA application_id=0x5442504a; PRAGMA user_version=1;"
        "CREATE TABLE project(id TEXT PRIMARY KEY,name TEXT NOT NULL,game_id TEXT NOT "
        "NULL);"
        "CREATE TABLE content(id TEXT PRIMARY KEY,hash TEXT NOT NULL,data BLOB NOT NULL);"
        "CREATE INDEX content_hash ON content(hash);"
        "CREATE TABLE revisions(asset_id TEXT NOT NULL,type TEXT NOT NULL,virtual_path "
        "TEXT NOT NULL,display_name TEXT NOT NULL,source_name TEXT NOT NULL,revision "
        "INTEGER NOT NULL CHECK(revision>0),recipe TEXT NOT NULL,original TEXT NOT NULL "
        "REFERENCES content(id),compatible TEXT NOT NULL REFERENCES content(id),PRIMARY "
        "KEY(asset_id,revision));"
        "CREATE TABLE heads(asset_id TEXT PRIMARY KEY,revision INTEGER NOT NULL,"
        "virtual_path TEXT NOT NULL UNIQUE,FOREIGN KEY(asset_id,revision) REFERENCES "
        "revisions(asset_id,revision));"
        "CREATE TABLE dependencies(asset_id TEXT NOT NULL REFERENCES heads(asset_id),"
        "dependency_id TEXT NOT NULL REFERENCES heads(asset_id),kind TEXT NOT NULL,"
        "PRIMARY KEY(asset_id,dependency_id,kind),CHECK(asset_id<>dependency_id));"
        "CREATE TABLE checkpoints(id TEXT PRIMARY KEY,name TEXT NOT NULL,description "
        "TEXT "
        "NOT NULL,created_at TEXT NOT NULL);"
        "CREATE TABLE checkpoint_assets(checkpoint_id TEXT NOT NULL REFERENCES "
        "checkpoints(id),asset_id TEXT NOT NULL,revision INTEGER NOT NULL,PRIMARY "
        "KEY(checkpoint_id,asset_id),FOREIGN KEY(asset_id,revision) REFERENCES "
        "revisions(asset_id,revision));"
        "CREATE TABLE checkpoint_dependencies(checkpoint_id TEXT NOT NULL REFERENCES "
        "checkpoints(id),asset_id TEXT NOT NULL,dependency_id TEXT NOT NULL,kind TEXT "
        "NOT "
        "NULL,PRIMARY KEY(checkpoint_id,asset_id,dependency_id,kind),FOREIGN KEY("
        "checkpoint_id,asset_id) REFERENCES checkpoint_assets(checkpoint_id,asset_id),"
        "FOREIGN KEY(checkpoint_id,dependency_id) REFERENCES "
        "checkpoint_assets(checkpoint_id,asset_id));");
      db.exec(
        "INSERT INTO project VALUES('p','Legacy','Counter-Strike');"
        "INSERT INTO content VALUES('c1','0','legacybytes');"
        "INSERT INTO revisions VALUES('11111111-1111-1111-1111-111111111111','texture',"
        "'textures/wall','wall','wall.png',1,'{}','c1','c1');"
        "INSERT INTO heads VALUES('11111111-1111-1111-1111-111111111111',1,"
        "'textures/wall');");
    }
    auto store = require(ProjectStore::open(workspace.path));
    CHECK(store->info().name == "Legacy");
    const auto asset = require(store->findAsset("11111111-1111-1111-1111-111111111111"));
    REQUIRE(asset);
    CHECK(asset->contentHash == contentHash(std::string_view{"legacybytes"}));
    CHECK(asset->library.empty());
    CHECK(require(store->libraryLinkedAssets()).empty());
    CHECK(RawDatabase{workspace.path}.count("PRAGMA user_version") == 2);
    // The migrated project must stay writable.
    const auto renamed = require(store->renameAsset(asset->id, "renamed"));
    CHECK(renamed.revision == 2);
    CHECK(renamed.contentHash == asset->contentHash);
  }
}

} // namespace tb::project
