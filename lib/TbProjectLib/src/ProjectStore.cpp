/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.
 */

#include "project/ProjectStore.h"

#include "CatalogValidation.h"
#include "Sqlite.h"
#include "project/ContentHash.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <map>
#include <set>
#include <string_view>
#include <utility>

namespace tb::project
{
namespace
{

using detail::attempt;
using detail::check;
using detail::Database;
using detail::execute;
using detail::NewFile;
using detail::Statement;
using detail::storeContent;
using detail::StoreError;
using detail::Transaction;
using detail::utf8Path;
using detail::validateAssetId;
using detail::validatePath;
using detail::validateText;

Asset readAssetRow(const Statement& query)
{
  const auto type = parseAssetType(query.text(1));
  if (type.is_error())
    throw StoreError{std::get<Error>(type.error()).msg};
  auto asset = Asset{
    query.text(0),
    type.value(),
    query.text(2),
    query.text(3),
    query.text(4),
    query.integer(5),
    query.text(6),
    query.text(7),
    query.text(8),
    {}};
  if (!detail::validAssetId(asset.id))
    throw StoreError{"Invalid project asset ID."};
  validatePath(asset.virtualPath);
  validatePath(asset.publishPath);
  validateText(asset.displayName, "display name");
  if (asset.revision == 0)
    throw StoreError{"Invalid project asset revision."};
  // Library provenance is optional; when present it must be complete enough to locate
  // the library asset again and to compare content.
  const auto libraryAssetId = query.text(9);
  if (!libraryAssetId.empty())
  {
    detail::validateAssetId(libraryAssetId);
    const auto libraryId = query.text(10);
    validateText(libraryId, "library ID");
    const auto libraryHash = query.text(13);
    validateText(libraryHash, "library content hash");
    asset.library = LibraryLink{
      libraryId,
      libraryAssetId,
      query.integer(11),
      libraryHash,
      query.text(12),
      query.text(14)};
  }
  return asset;
}

constexpr auto AssetColumns =
  "r.asset_id,r.type,r.virtual_path,r.display_name,"
  "r.source_name,r.revision,r.recipe,"
  "r.publish_path,"
  "r.content_hash,r.library_asset_id,r.library_id,r.library_revision,r.source_container,"
  "r.library_hash,r.source_member";

std::vector<Asset> queryAssets(
  sqlite3* db, const std::optional<std::string>& checkpoint = std::nullopt)
{
  const auto sql = fmt::format(
    "SELECT {} FROM revisions r JOIN {} h ON r.asset_id=h.asset_id AND "
    "r.revision=h.revision {} ORDER BY r.virtual_path",
    AssetColumns,
    checkpoint ? "checkpoint_assets" : "heads",
    checkpoint ? "WHERE h.checkpoint_id=?" : "");
  auto query = Statement{db, sql.c_str()};
  if (checkpoint)
    query.bind(1, *checkpoint);
  auto result = std::vector<Asset>{};
  while (query.next())
    result.push_back(readAssetRow(query));
  return result;
}

Asset requireAsset(sqlite3* db, const std::string& id)
{
  const auto sql = fmt::format(
    "SELECT {} FROM revisions r JOIN heads h ON r.asset_id=h.asset_id AND "
    "r.revision=h.revision WHERE h.asset_id=?",
    AssetColumns);
  auto query = Statement{db, sql.c_str()};
  query.bind(1, id);
  if (!query.next())
    throw StoreError{"Project asset not found."};
  return readAssetRow(query);
}

std::vector<UnreferencedRevision> queryUnreferencedRevisions(sqlite3* db)
{
  // A revision is unreachable when neither the current heads nor any checkpoint captures
  // it; checkpoints are what keep an old version restorable.
  auto query = Statement{
    db,
    "SELECT r.asset_id,r.revision,r.virtual_path FROM revisions r "
    "LEFT JOIN heads h ON h.asset_id=r.asset_id AND h.revision=r.revision "
    "LEFT JOIN checkpoint_assets ca ON ca.asset_id=r.asset_id AND "
    "ca.revision=r.revision "
    "WHERE h.asset_id IS NULL AND ca.asset_id IS NULL "
    "ORDER BY r.asset_id,r.revision"};
  auto result = std::vector<UnreferencedRevision>{};
  while (query.next())
  {
    result.push_back({query.text(0), query.integer(1), query.text(2)});
  }
  return result;
}

struct ContentFootprint
{
  std::uint64_t rows = 0;
  std::uint64_t bytes = 0;
};

ContentFootprint orphanedContent(
  sqlite3* db, const std::vector<UnreferencedRevision>& removals)
{
  // Content is content addressed and shared between revisions, so a blob counts once and
  // only when no revision outside the cleanup plan still references it.
  auto removed = std::set<std::pair<std::string, std::uint64_t>>{};
  for (const auto& revision : removals)
  {
    removed.emplace(revision.assetId, revision.revision);
  }

  auto keptContent = std::set<std::string>{};
  {
    auto query =
      Statement{db, "SELECT asset_id,revision,original,compatible FROM revisions"};
    while (query.next())
    {
      const auto key =
        std::pair<std::string, std::uint64_t>{query.text(0), query.integer(1)};
      if (removed.contains(key))
      {
        continue;
      }
      keptContent.insert(query.text(2));
      keptContent.insert(query.text(3));
    }
  }

  auto footprint = ContentFootprint{};
  auto query = Statement{db, "SELECT id,length(data) FROM content"};
  while (query.next())
  {
    if (!keptContent.contains(query.text(0)))
    {
      ++footprint.rows;
      footprint.bytes += query.integer(1);
    }
  }
  return footprint;
}

std::vector<AssetDependency> queryDependencies(
  sqlite3* db, const std::optional<std::string>& checkpoint = std::nullopt)
{
  auto query = Statement{
    db,
    checkpoint ? "SELECT asset_id,dependency_id,kind FROM checkpoint_dependencies WHERE "
                 "checkpoint_id=? ORDER BY asset_id,dependency_id,kind"
               : "SELECT asset_id,dependency_id,kind FROM dependencies ORDER BY "
                 "asset_id,dependency_id,kind"};
  if (checkpoint)
    query.bind(1, *checkpoint);
  auto result = std::vector<AssetDependency>{};
  while (query.next())
    result.push_back({query.text(0), query.text(1), query.text(2)});
  return result;
}

void writeRevision(
  sqlite3* db,
  Asset& asset,
  const std::string& original,
  const std::string& compatible,
  const std::map<std::string, std::string>* finalPaths = nullptr);

Asset createAsset(sqlite3* db, const AssetImport& import)
{
  if (parseAssetType(assetTypeName(import.type)).is_error())
    throw StoreError{"Invalid asset type."};
  auto asset = Asset{
    generateUuid(),
    import.type,
    import.virtualPath,
    import.displayName,
    import.sourceName,
    0,
    import.conversionRecipe,
    import.publishPath,
    contentHash(import.compatible),
    import.library};
  writeRevision(
    db, asset, storeContent(db, import.original), storeContent(db, import.compatible));
  return asset;
}

void writeRevision(
  sqlite3* db,
  Asset& asset,
  const std::string& original,
  const std::string& compatible,
  const std::map<std::string, std::string>* finalPaths)
{
  validateText(asset.displayName, "display name");
  validatePath(asset.virtualPath);
  const auto publishPath =
    asset.publishPath.empty() ? asset.virtualPath : asset.publishPath;
  validatePath(publishPath);
  auto extension = std::filesystem::path{publishPath}.extension().string();
  std::ranges::transform(extension, extension.begin(), [](const unsigned char c) {
    return char(std::tolower(c));
  });
  if ((extension == ".mdl" || extension == ".spr") && publishPath.size() >= 64)
    throw StoreError{
      "The published model/sprite path must fit in 63 bytes: " + publishPath};
  auto conflicts = Statement{
    db,
    "SELECT h.virtual_path,r.publish_path,h.asset_id FROM heads h JOIN revisions r ON "
    "r.asset_id=h.asset_id AND r.revision=h.revision WHERE h.asset_id<>?"};
  conflicts.bind(1, asset.id);
  const auto normalized = [](std::string path) {
    std::ranges::transform(
      path, path.begin(), [](const unsigned char c) { return char(std::tolower(c)); });
    return path;
  };
  const auto overlaps = [&](const std::string& a, const std::string& b) {
    const auto x = normalized(a);
    const auto y = normalized(b);
    return x == y || x.starts_with(y + '/') || y.starts_with(x + '/');
  };
  while (conflicts.next())
  {
    auto otherPath = conflicts.text(1);
    if (finalPaths)
      if (const auto found = finalPaths->find(conflicts.text(2));
          found != finalPaths->end())
        otherPath = found->second;
    if (
      overlaps(asset.virtualPath, conflicts.text(0)) || overlaps(publishPath, otherPath))
      throw StoreError{"Project paths conflict with an existing asset or folder."};
  }
  asset.publishPath = publishPath;
  const auto& library = asset.library;
  if (!library.empty())
  {
    validateAssetId(library.libraryAssetId);
    validateText(library.libraryId, "library ID");
    validateText(library.libraryContentHash, "library content hash");
    if (library.libraryRevision == 0)
      throw StoreError{"A captured asset needs a library revision."};
    if (library.sourceContainer.empty())
      throw StoreError{"A captured asset needs a library source."};
  }
  // A restore never reuses a revision number, including for assets absent from heads.
  auto next =
    Statement{db, "SELECT COALESCE(MAX(revision),0)+1 FROM revisions WHERE asset_id=?"};
  next.bind(1, asset.id);
  next.next();
  asset.revision = next.integer(0);
  auto insert = Statement{
    db,
    "INSERT INTO "
    "revisions(asset_id,type,virtual_path,publish_path,display_name,source_name,revision,"
    "recipe,"
    "original,compatible,content_hash,library_id,library_asset_id,library_revision,"
    "library_hash,source_container,source_member) "
    "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"};
  insert.bind(1, asset.id);
  insert.bind(2, assetTypeName(asset.type));
  insert.bind(3, asset.virtualPath);
  insert.bind(4, asset.publishPath);
  insert.bind(5, asset.displayName);
  insert.bind(6, asset.sourceName);
  insert.bind(7, asset.revision);
  insert.bind(8, asset.conversionRecipe);
  insert.bind(9, original);
  insert.bind(10, compatible);
  insert.bind(11, asset.contentHash);
  insert.bind(12, library.libraryId);
  insert.bind(13, library.libraryAssetId);
  insert.bind(14, library.libraryRevision);
  insert.bind(15, library.libraryContentHash);
  insert.bind(16, library.sourceContainer);
  insert.bind(17, library.sourceMember);
  insert.run();
  auto head = Statement{
    db,
    "INSERT INTO heads(asset_id,revision,virtual_path) VALUES(?,?,?) ON "
    "CONFLICT(asset_id) DO UPDATE SET "
    "revision=excluded.revision,virtual_path=excluded.virtual_path"};
  head.bind(1, asset.id);
  head.bind(2, asset.revision);
  head.bind(3, asset.virtualPath);
  head.run();
}

void writeMetadataRevision(
  sqlite3* db,
  Asset& asset,
  const std::map<std::string, std::string>* finalPaths = nullptr)
{
  auto query = Statement{
    db, "SELECT original,compatible FROM revisions WHERE asset_id=? AND revision=?"};
  query.bind(1, asset.id);
  query.bind(2, asset.revision);
  if (!query.next())
    throw StoreError{"Project asset revision not found."};
  const auto original = query.text(0);
  const auto compatible = query.text(1);
  writeRevision(db, asset, original, compatible, finalPaths);
}

void requireCheckpoint(sqlite3* db, const std::string& id)
{
  auto query = Statement{db, "SELECT id FROM checkpoints WHERE id=?"};
  query.bind(1, id);
  if (!query.next())
    throw StoreError{"Project checkpoint not found."};
}

Checkpoint makeCheckpoint(
  sqlite3* db, const std::string& name, const std::string& description)
{
  validateText(name, "checkpoint name");
  auto time = Statement{db, "SELECT strftime('%Y-%m-%dT%H:%M:%fZ','now')"};
  time.next();
  auto result = Checkpoint{generateUuid(), name, description, time.text(0)};
  auto insert = Statement{
    db, "INSERT INTO checkpoints(id,name,description,created_at) VALUES(?,?,?,?)"};
  insert.bind(1, result.id);
  insert.bind(2, name);
  insert.bind(3, description);
  insert.bind(4, result.createdAt);
  insert.run();
  auto assets =
    Statement{db, "INSERT INTO checkpoint_assets SELECT ?,asset_id,revision FROM heads"};
  assets.bind(1, result.id);
  assets.run();
  auto deps = Statement{
    db,
    "INSERT INTO checkpoint_dependencies SELECT ?,asset_id,dependency_id,kind FROM "
    "dependencies"};
  deps.bind(1, result.id);
  deps.run();
  return result;
}

constexpr auto Schema =
  "PRAGMA application_id=0x5442504a; PRAGMA user_version=4;"
  "CREATE TABLE project(id TEXT PRIMARY KEY,name TEXT NOT NULL,game_id TEXT NOT NULL);"
  "CREATE TABLE content(id TEXT PRIMARY KEY,hash TEXT NOT NULL,data BLOB NOT NULL);"
  "CREATE INDEX content_hash ON content(hash);"
  "CREATE TABLE revisions("
  "asset_id TEXT NOT NULL,type TEXT NOT NULL,virtual_path TEXT NOT NULL,publish_path "
  "TEXT "
  "NOT NULL,display_name "
  "TEXT NOT NULL,"
  "source_name TEXT NOT NULL,revision INTEGER NOT NULL CHECK(revision>0),recipe TEXT NOT "
  "NULL,"
  "original TEXT NOT NULL REFERENCES content(id),compatible TEXT NOT NULL REFERENCES "
  "content(id),"
  // Snapshot provenance: content identity of this revision and the library revision it
  // was captured from. Empty library columns mean a directly imported asset.
  "content_hash TEXT NOT NULL DEFAULT '',"
  "library_id TEXT NOT NULL DEFAULT '',"
  "library_asset_id TEXT NOT NULL DEFAULT '',"
  "library_revision INTEGER NOT NULL DEFAULT 0,"
  "library_hash TEXT NOT NULL DEFAULT '',"
  "source_container TEXT NOT NULL DEFAULT '',"
  "source_member TEXT NOT NULL DEFAULT '',"
  "PRIMARY KEY(asset_id,revision));"
  "CREATE TABLE heads(asset_id TEXT PRIMARY KEY,revision INTEGER NOT NULL,virtual_path "
  "TEXT NOT NULL UNIQUE,"
  "FOREIGN KEY(asset_id,revision) REFERENCES revisions(asset_id,revision));"
  "CREATE TABLE dependencies(asset_id TEXT NOT NULL REFERENCES heads(asset_id),"
  "dependency_id TEXT NOT NULL REFERENCES heads(asset_id),kind TEXT NOT NULL,"
  "PRIMARY KEY(asset_id,dependency_id,kind),CHECK(asset_id<>dependency_id));"
  // Known asset libraries are a convenience for locating an origin; the library ID is
  // the identity and a path is only a local hint.
  "CREATE TABLE libraries(id TEXT PRIMARY KEY,name TEXT NOT NULL,last_path TEXT NOT NULL,"
  "attached_at TEXT NOT NULL);"
  // "Ignore this library revision" is project state, never library state.
  "CREATE TABLE library_ignores(asset_id TEXT NOT NULL,library_hash TEXT NOT NULL,"
  "created_at TEXT NOT NULL,PRIMARY KEY(asset_id,library_hash));"
  "CREATE TABLE checkpoints(id TEXT PRIMARY KEY,name TEXT NOT NULL,description TEXT NOT "
  "NULL,created_at TEXT NOT NULL);"
  "CREATE TABLE checkpoint_assets(checkpoint_id TEXT NOT NULL REFERENCES checkpoints(id),"
  "asset_id TEXT NOT NULL,revision INTEGER NOT NULL,PRIMARY KEY(checkpoint_id,asset_id),"
  "FOREIGN KEY(asset_id,revision) REFERENCES revisions(asset_id,revision));"
  "CREATE TABLE checkpoint_dependencies(checkpoint_id TEXT NOT NULL REFERENCES "
  "checkpoints(id),"
  "asset_id TEXT NOT NULL,dependency_id TEXT NOT NULL,kind TEXT NOT NULL,"
  "PRIMARY KEY(checkpoint_id,asset_id,dependency_id,kind),"
  "FOREIGN KEY(checkpoint_id,asset_id) REFERENCES "
  "checkpoint_assets(checkpoint_id,asset_id),"
  "FOREIGN KEY(checkpoint_id,dependency_id) REFERENCES "
  "checkpoint_assets(checkpoint_id,asset_id));"
  "CREATE TABLE terrain_compile("
  "asset_id TEXT PRIMARY KEY REFERENCES heads(asset_id) ON DELETE CASCADE,"
  "source_revision INTEGER NOT NULL,generator_version INTEGER NOT NULL,"
  "top_faces INTEGER NOT NULL,map BLOB NOT NULL,wad BLOB NOT NULL,"
  "error TEXT NOT NULL);"
  "CREATE TABLE terrain_compile_dependencies("
  "asset_id TEXT NOT NULL REFERENCES terrain_compile(asset_id) ON DELETE CASCADE,"
  "dependency_id TEXT NOT NULL,PRIMARY KEY(asset_id,dependency_id));"
  "CREATE INDEX terrain_compile_dependency_id ON "
  "terrain_compile_dependencies(dependency_id);"
  "CREATE TRIGGER terrain_head_insert AFTER INSERT ON heads BEGIN "
  "DELETE FROM terrain_compile WHERE asset_id IN (SELECT asset_id FROM "
  "terrain_compile_dependencies WHERE dependency_id=NEW.asset_id); END;"
  "CREATE TRIGGER terrain_head_update AFTER UPDATE ON heads "
  "WHEN OLD.revision<>NEW.revision BEGIN "
  "DELETE FROM terrain_compile WHERE asset_id IN (SELECT asset_id FROM "
  "terrain_compile_dependencies WHERE dependency_id=OLD.asset_id); END;"
  "CREATE TRIGGER terrain_head_delete AFTER DELETE ON heads BEGIN "
  "DELETE FROM terrain_compile WHERE asset_id IN (SELECT asset_id FROM "
  "terrain_compile_dependencies WHERE dependency_id=OLD.asset_id); END;"
  "CREATE TRIGGER terrain_dependency_insert AFTER INSERT ON "
  "dependencies BEGIN DELETE FROM terrain_compile WHERE asset_id IN "
  "(SELECT asset_id FROM terrain_compile_dependencies "
  "WHERE dependency_id=NEW.asset_id); END;"
  "CREATE TRIGGER terrain_dependency_delete AFTER DELETE ON "
  "dependencies BEGIN DELETE FROM terrain_compile WHERE asset_id IN "
  "(SELECT asset_id FROM terrain_compile_dependencies "
  "WHERE dependency_id=OLD.asset_id); END;"
  "CREATE TRIGGER terrain_dependency_update AFTER UPDATE ON "
  "dependencies BEGIN DELETE FROM terrain_compile WHERE asset_id IN "
  "(SELECT asset_id FROM terrain_compile_dependencies "
  "WHERE dependency_id=OLD.asset_id OR dependency_id=NEW.asset_id); END;";

// Format 1 stored no snapshot provenance and no content hashes. The migration adds the
// columns, then backfills the hash from the compatible payload so that every revision
// can be compared against a library one. Content blobs are hashed one row at a time.
void migrateFromVersion1(sqlite3* db)
{
  execute(
    db,
    "ALTER TABLE revisions ADD COLUMN content_hash TEXT NOT NULL DEFAULT '';"
    "ALTER TABLE revisions ADD COLUMN library_id TEXT NOT NULL DEFAULT '';"
    "ALTER TABLE revisions ADD COLUMN library_asset_id TEXT NOT NULL DEFAULT '';"
    "ALTER TABLE revisions ADD COLUMN library_revision INTEGER NOT NULL DEFAULT 0;"
    "ALTER TABLE revisions ADD COLUMN library_hash TEXT NOT NULL DEFAULT '';"
    "ALTER TABLE revisions ADD COLUMN source_container TEXT NOT NULL DEFAULT '';"
    "ALTER TABLE revisions ADD COLUMN source_member TEXT NOT NULL DEFAULT '';"
    "CREATE TABLE libraries(id TEXT PRIMARY KEY,name TEXT NOT NULL,last_path TEXT NOT "
    "NULL,attached_at TEXT NOT NULL);"
    "CREATE TABLE library_ignores(asset_id TEXT NOT NULL,library_hash TEXT NOT NULL,"
    "created_at TEXT NOT NULL,PRIMARY KEY(asset_id,library_hash));");
  auto pending = Statement{
    db,
    "SELECT r.asset_id,r.revision,c.data FROM revisions r JOIN content c ON "
    "c.id=r.compatible WHERE r.content_hash=''"};
  auto revisions = std::vector<std::pair<std::string, std::uint64_t>>{};
  auto hashes = std::vector<std::string>{};
  while (pending.next())
  {
    hashes.push_back(contentHash(pending.bytes(2)));
    revisions.emplace_back(pending.text(0), pending.integer(1));
  }
  auto update =
    Statement{db, "UPDATE revisions SET content_hash=? WHERE asset_id=? AND revision=?"};
  for (std::size_t index = 0; index < revisions.size(); ++index)
  {
    update.reset();
    update.bind(1, hashes[index]);
    update.bind(2, revisions[index].first);
    update.bind(3, revisions[index].second);
    update.run();
  }
  execute(db, "PRAGMA user_version=2;");
}

// Format 2 derived the engine-facing path from the first revision's virtual path, which
// made a published name impossible to change. Store it explicitly so a rename becomes a
// normal revision; existing projects keep their original engine-facing name.
void migrateFromVersion2(sqlite3* db)
{
  execute(
    db,
    "ALTER TABLE revisions ADD COLUMN publish_path TEXT NOT NULL DEFAULT '';"
    "UPDATE revisions SET publish_path=(SELECT first.virtual_path FROM revisions first "
    "WHERE first.asset_id=revisions.asset_id ORDER BY first.revision LIMIT 1);"
    "PRAGMA user_version=3;");
}

// Format 3 added publish_path. Format 4 established the permanent frozen baseline
// including terrain_compile. Format 3 projects automatically upgrade to 4.
void migrateFromVersion3(sqlite3* db)
{
  execute(
    db,
    "CREATE TABLE IF NOT EXISTS terrain_compile("
    "asset_id TEXT PRIMARY KEY REFERENCES heads(asset_id) ON DELETE CASCADE,"
    "source_revision INTEGER NOT NULL,generator_version INTEGER NOT NULL,"
    "top_faces INTEGER NOT NULL,map BLOB NOT NULL,wad BLOB NOT NULL,"
    "error TEXT NOT NULL);"
    "CREATE TABLE IF NOT EXISTS terrain_compile_dependencies("
    "asset_id TEXT NOT NULL REFERENCES terrain_compile(asset_id) ON DELETE CASCADE,"
    "dependency_id TEXT NOT NULL,PRIMARY KEY(asset_id,dependency_id));"
    "CREATE INDEX IF NOT EXISTS terrain_compile_dependency_id "
    "ON terrain_compile_dependencies(dependency_id);"
    "CREATE TRIGGER IF NOT EXISTS terrain_head_insert AFTER INSERT ON heads BEGIN "
    "DELETE FROM terrain_compile WHERE asset_id IN (SELECT asset_id FROM "
    "terrain_compile_dependencies WHERE dependency_id=NEW.asset_id); END;"
    "CREATE TRIGGER IF NOT EXISTS terrain_head_update AFTER UPDATE ON heads "
    "WHEN OLD.revision<>NEW.revision BEGIN "
    "DELETE FROM terrain_compile WHERE asset_id IN (SELECT asset_id FROM "
    "terrain_compile_dependencies WHERE dependency_id=OLD.asset_id); END;"
    "CREATE TRIGGER IF NOT EXISTS terrain_head_delete AFTER DELETE ON heads BEGIN "
    "DELETE FROM terrain_compile WHERE asset_id IN (SELECT asset_id FROM "
    "terrain_compile_dependencies WHERE dependency_id=OLD.asset_id); END;"
    "CREATE TRIGGER IF NOT EXISTS terrain_dependency_insert AFTER INSERT ON "
    "dependencies BEGIN DELETE FROM terrain_compile WHERE asset_id IN "
    "(SELECT asset_id FROM terrain_compile_dependencies "
    "WHERE dependency_id=NEW.asset_id); END;"
    "CREATE TRIGGER IF NOT EXISTS terrain_dependency_delete AFTER DELETE ON "
    "dependencies BEGIN DELETE FROM terrain_compile WHERE asset_id IN "
    "(SELECT asset_id FROM terrain_compile_dependencies "
    "WHERE dependency_id=OLD.asset_id); END;"
    "CREATE TRIGGER IF NOT EXISTS terrain_dependency_update AFTER UPDATE ON "
    "dependencies BEGIN DELETE FROM terrain_compile WHERE asset_id IN "
    "(SELECT asset_id FROM terrain_compile_dependencies "
    "WHERE dependency_id=OLD.asset_id OR dependency_id=NEW.asset_id); END;"
    "PRAGMA user_version=4;");
}

void configure(sqlite3* db)
{
  execute(
    db, "PRAGMA foreign_keys=ON; PRAGMA journal_mode=DELETE; PRAGMA synchronous=FULL;");
}

} // namespace

const char* assetTypeName(const AssetType type)
{
  switch (type)
  {
  case AssetType::Map:
    return "map";
  case AssetType::Texture:
    return "texture";
  case AssetType::Material:
    return "material";
  case AssetType::Model:
    return "model";
  case AssetType::Sprite:
    return "sprite";
  case AssetType::Sound:
    return "sound";
  case AssetType::Skybox:
    return "skybox";
  case AssetType::Prefab:
    return "prefab";
  }
  return "unknown";
}

Result<AssetType> parseAssetType(const std::string& name)
{
  for (const auto type :
       {AssetType::Map,
        AssetType::Texture,
        AssetType::Material,
        AssetType::Model,
        AssetType::Sprite,
        AssetType::Sound,
        AssetType::Skybox,
        AssetType::Prefab})
  {
    if (name == assetTypeName(type))
      return type;
  }
  return Error{fmt::format("Unknown project asset type '{}'.", name)};
}

class ProjectStore::Impl
{
public:
  std::filesystem::path path;
  Database database;
  ProjectInfo info;
  std::uint64_t schemaVersion = 4;

  Impl(std::filesystem::path projectPath, const bool create)
    : path{std::move(projectPath)}
    , database{path, create}
  {
  }
};

ProjectStore::ProjectStore(std::unique_ptr<Impl> impl)
  : m_impl{std::move(impl)}
{
}
ProjectStore::~ProjectStore() = default;
ProjectStore::ProjectStore(ProjectStore&&) noexcept = default;
ProjectStore& ProjectStore::operator=(ProjectStore&&) noexcept = default;

Result<std::unique_ptr<ProjectStore>> ProjectStore::create(
  const std::filesystem::path& path,
  const std::string& projectName,
  const std::string& gameId)
{
  return attempt([&] {
    validateText(projectName, "project name");
    validateText(gameId, "game ID");
    auto reservation = NewFile{path};
    auto impl = std::make_unique<Impl>(path, false);
    auto* db = impl->database.get();
    configure(db);
    auto transaction = Transaction{db};
    // Check after taking the write lock too: a concurrent creator must not be
    // overwritten.
    auto tables = Statement{db, "SELECT name FROM sqlite_schema LIMIT 1"};
    if (tables.next())
      throw StoreError{"Project destination is not empty."};
    execute(db, Schema);
    impl->info = {generateUuid(), projectName, gameId};
    auto insert = Statement{db, "INSERT INTO project(id,name,game_id) VALUES(?,?,?)"};
    insert.bind(1, impl->info.id);
    insert.bind(2, projectName);
    insert.bind(3, gameId);
    insert.run();
    transaction.commit();
    reservation.keep();
    return std::unique_ptr<ProjectStore>{new ProjectStore{std::move(impl)}};
  });
}

Result<std::unique_ptr<ProjectStore>> ProjectStore::open(
  const std::filesystem::path& path)
{
  return attempt([&] {
    if (path.empty())
      throw StoreError{"Project path is empty."};
    auto impl = std::make_unique<Impl>(path, false);
    auto* db = impl->database.get();
    auto version = std::uint64_t{0};
    {
      auto query =
        Statement{db, "SELECT * FROM pragma_application_id(),pragma_user_version()"};
      if (!query.next() || query.integer(0) != 0x5442504a)
        throw StoreError{"Not a supported TB project."};
      version = query.integer(1);
    }
    if (version == 1)
    {
      // Adding provenance columns is a forward-only migration; an older build must not
      // silently read a project it cannot interpret.
      auto transaction = Transaction{db};
      migrateFromVersion1(db);
      transaction.commit();
      version = 2;
    }
    if (version == 2)
    {
      auto transaction = Transaction{db};
      migrateFromVersion2(db);
      transaction.commit();
      version = 3;
    }
    if (version == 3)
    {
      auto transaction = Transaction{db};
      migrateFromVersion3(db);
      transaction.commit();
      version = 4;
    }
    if (version != 4)
      throw StoreError{"Not a supported TB project (format version 4 required)."};
    impl->schemaVersion = version;
    auto info = Statement{db, "SELECT id,name,game_id FROM project"};
    if (!info.next())
      throw StoreError{"Project metadata is missing."};
    impl->info = {info.text(0), info.text(1), info.text(2)};
    validateText(impl->info.id, "project ID");
    validateText(impl->info.name, "project name");
    validateText(impl->info.gameId, "game ID");
    if (info.next())
      throw StoreError{"Duplicate project metadata."};
    // Fail closed on missing tables/columns without loading content blobs.
    queryAssets(db);
    queryDependencies(db);
    {
      auto probe =
        Statement{db, "SELECT asset_id,library_hash FROM library_ignores LIMIT 1"};
      while (probe.next())
        ;
      auto librariesProbe = Statement{db, "SELECT id FROM libraries LIMIT 1"};
      while (librariesProbe.next())
        ;
      if (version == 4)
      {
        auto terrainProbe = Statement{
          db,
          "SELECT asset_id,source_revision,generator_version,top_faces,map,wad,error "
          "FROM terrain_compile LIMIT 1"};
        while (terrainProbe.next())
          ;
      }
    }
    configure(db);
    return std::unique_ptr<ProjectStore>{new ProjectStore{std::move(impl)}};
  });
}

const std::filesystem::path& ProjectStore::path() const
{
  return m_impl->path;
}
const ProjectInfo& ProjectStore::info() const
{
  return m_impl->info;
}
Result<void> ProjectStore::validatePublishSettings(const PublishSettings& settings)
{
  return attempt([&] {
    if (!settings.assetNamespace.empty())
      validatePath(settings.assetNamespace);
    if (
      settings.assetNamespace.size() > 32
      || std::ranges::any_of(settings.assetNamespace, [](const unsigned char c) {
           return !(
             (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
             || c == '_' || c == '-');
         }))
      throw StoreError{
        "The resource namespace must contain at most 32 ASCII letters, digits, "
        "underscores or hyphens."};
    if (!settings.gameDirectory.empty())
    {
      validateText(settings.gameDirectory, "game directory");
      const auto& text = settings.gameDirectory;
      const auto path = std::filesystem::path{
        std::u8string{reinterpret_cast<const char8_t*>(text.data()), text.size()}};
      if (!path.is_absolute() || path == path.root_path())
        throw StoreError{"Select an absolute game/mod directory."};
    }
  });
}

Result<PublishSettings> ProjectStore::publishSettings() const
{
  return attempt([&] {
    auto* db = m_impl->database.get();
    auto exists = Statement{
      db, "SELECT 1 FROM sqlite_schema WHERE type='table' AND name='publish_settings'"};
    if (!exists.next())
      return PublishSettings{};
    auto query = Statement{
      db, "SELECT asset_namespace,game_directory FROM publish_settings WHERE id=1"};
    const auto settings =
      query.next() ? PublishSettings{query.text(0), query.text(1)} : PublishSettings{};
    if (const auto valid = validatePublishSettings(settings); valid.is_error())
      throw StoreError{std::get<Error>(valid.error()).msg};
    return settings;
  });
}

Result<void> ProjectStore::setPublishSettings(const PublishSettings& settings)
{
  if (const auto valid = validatePublishSettings(settings); valid.is_error())
    return std::get<Error>(valid.error());
  return attempt([&] {
    auto* db = m_impl->database.get();
    auto transaction = Transaction{db};
    // Optional editor metadata keeps the format-4 compiler contract unchanged.
    execute(
      db,
      "CREATE TABLE IF NOT EXISTS publish_settings(id INTEGER PRIMARY KEY "
      "CHECK(id=1),asset_namespace TEXT NOT NULL,game_directory TEXT NOT NULL)");
    auto write = Statement{
      db,
      "INSERT INTO publish_settings VALUES(1,?,?) ON CONFLICT(id) DO UPDATE SET "
      "asset_namespace=excluded.asset_namespace,game_directory=excluded.game_directory"};
    write.bind(1, settings.assetNamespace);
    write.bind(2, settings.gameDirectory);
    write.run();
    transaction.commit();
  });
}

Result<Asset> ProjectStore::importAsset(const AssetImport& import)
{
  return attempt([&] {
    auto* db = m_impl->database.get();
    auto transaction = Transaction{db};
    auto asset = createAsset(db, import);
    transaction.commit();
    return asset;
  });
}

namespace
{

// Shared by replaceAsset and updateAssets so a batch has exactly the semantics of a
// single replacement.
Asset applyImport(sqlite3* db, const std::string& id, const AssetImport& import)
{
  auto asset = requireAsset(db, id);
  if (asset.type != import.type)
    throw StoreError{"Reimport cannot change an asset's type."};
  asset.sourceName = import.sourceName;
  asset.conversionRecipe = import.conversionRecipe;
  asset.contentHash = contentHash(import.compatible);
  asset.library = import.library;
  const auto original = storeContent(db, import.original);
  const auto compatible = storeContent(db, import.compatible);
  // Reusing an immutable revision is only valid when the snapshot provenance is the same
  // too; a library update that happens to carry identical bytes still has to be recorded
  // so "which library revision is this project on" stays truthful.
  auto existing = Statement{
    db,
    "SELECT revision FROM revisions WHERE asset_id=? AND source_name=? AND recipe=? "
    "AND original=? AND compatible=? AND display_name=? AND virtual_path=? AND "
    "content_hash=? AND library_id=? AND library_asset_id=? AND library_revision=? "
    "AND library_hash=? AND source_container=? AND source_member=? AND publish_path=? "
    "ORDER BY revision DESC LIMIT 1"};
  existing.bind(1, id);
  existing.bind(2, asset.sourceName);
  existing.bind(3, asset.conversionRecipe);
  existing.bind(4, original);
  existing.bind(5, compatible);
  existing.bind(6, asset.displayName);
  existing.bind(7, asset.virtualPath);
  existing.bind(8, asset.contentHash);
  existing.bind(9, asset.library.libraryId);
  existing.bind(10, asset.library.libraryAssetId);
  existing.bind(11, asset.library.libraryRevision);
  existing.bind(12, asset.library.libraryContentHash);
  existing.bind(13, asset.library.sourceContainer);
  existing.bind(14, asset.library.sourceMember);
  existing.bind(15, asset.publishPath);
  if (existing.next())
  {
    asset.revision = existing.integer(0);
    auto head = Statement{db, "UPDATE heads SET revision=? WHERE asset_id=?"};
    head.bind(1, asset.revision);
    head.bind(2, id);
    head.run();
  }
  else
    writeRevision(db, asset, original, compatible);
  return asset;
}

} // namespace

Result<Asset> ProjectStore::replaceAsset(const std::string& id, const AssetImport& import)
{
  return attempt([&] {
    auto* db = m_impl->database.get();
    auto transaction = Transaction{db};
    auto asset = applyImport(db, id, import);
    transaction.commit();
    return asset;
  });
}

Result<std::vector<Asset>> ProjectStore::updateAssets(
  const std::vector<AssetUpdate>& updates)
{
  return attempt([&] {
    auto* db = m_impl->database.get();
    auto transaction = Transaction{db};
    auto result = std::vector<Asset>{};
    result.reserve(updates.size());
    for (const auto& update : updates)
      result.push_back(applyImport(db, update.assetId, update.import));
    transaction.commit();
    return result;
  });
}

Result<std::vector<Asset>> ProjectStore::captureAssets(
  const std::vector<AssetCapture>& captures)
{
  return attempt([&] {
    auto* db = m_impl->database.get();
    auto transaction = Transaction{db};
    auto result = std::vector<Asset>{};
    result.reserve(captures.size());
    for (const auto& capture : captures)
      result.push_back(
        capture.assetId ? applyImport(db, *capture.assetId, capture.import)
                        : createAsset(db, capture.import));

    // Dependency edges are rewritten for every captured asset, in the same transaction,
    // so content and graph can never disagree about what the asset needs.
    for (auto index = std::size_t{0}; index < captures.size(); ++index)
    {
      const auto& assetId = result[index].id;
      auto clear = Statement{db, "DELETE FROM dependencies WHERE asset_id=?"};
      clear.bind(1, assetId);
      clear.run();
      for (const auto& link : captures[index].dependencies)
      {
        const auto dependencyId = link.assetId.empty() ? [&]() {
          if (link.target >= result.size())
            throw StoreError{"Capture dependency does not point at a captured asset."};
          return result[link.target].id;
        }()
                                                       : link.assetId;
        if (dependencyId == assetId)
          throw StoreError{"Project asset dependency would create a cycle."};
        validateText(link.kind, "dependency kind");
        // The plan is a DAG by construction, but the graph invariant is enforced for
        // every edge so a malformed plan fails before it is committed.
        auto cycle = Statement{
          db,
          "WITH RECURSIVE reachable(id) AS (SELECT ? UNION SELECT dependency_id FROM "
          "dependencies JOIN reachable ON asset_id=reachable.id) SELECT id FROM "
          "reachable "
          "WHERE id=?"};
        cycle.bind(1, dependencyId);
        cycle.bind(2, assetId);
        if (cycle.next())
          throw StoreError{"Project asset dependency would create a cycle."};
        auto insert = Statement{
          db,
          "INSERT INTO dependencies(asset_id,dependency_id,kind) VALUES(?,?,?) ON "
          "CONFLICT "
          "DO NOTHING"};
        insert.bind(1, assetId);
        insert.bind(2, dependencyId);
        insert.bind(3, link.kind);
        insert.run();
      }
    }
    transaction.commit();
    return result;
  });
}

Result<Asset> ProjectStore::renameAsset(
  const std::string& id, const std::string& displayName)
{
  return attempt([&] {
    auto* db = m_impl->database.get();
    auto transaction = Transaction{db};
    auto asset = requireAsset(db, id);
    asset.displayName = displayName;
    writeMetadataRevision(db, asset);
    transaction.commit();
    return asset;
  });
}

Result<Asset> ProjectStore::moveAsset(
  const std::string& id, const std::string& virtualPath)
{
  return attempt([&] {
    auto* db = m_impl->database.get();
    auto transaction = Transaction{db};
    auto asset = requireAsset(db, id);
    asset.virtualPath = virtualPath;
    writeMetadataRevision(db, asset);
    transaction.commit();
    return asset;
  });
}

Result<Asset> ProjectStore::setPublishPath(
  const std::string& id, const std::string& publishPath)
{
  return attempt([&] {
    auto* db = m_impl->database.get();
    auto transaction = Transaction{db};
    auto asset = requireAsset(db, id);
    asset.publishPath = publishPath;
    writeMetadataRevision(db, asset);
    transaction.commit();
    return asset;
  });
}

Result<void> ProjectStore::setPublishPaths(
  const std::map<std::string, std::string>& paths)
{
  return attempt([&] {
    auto* db = m_impl->database.get();
    auto transaction = Transaction{db};
    for (const auto& [id, path] : paths)
    {
      auto asset = requireAsset(db, id);
      asset.publishPath = path;
      writeMetadataRevision(db, asset, &paths);
    }
    transaction.commit();
  });
}

Result<void> ProjectStore::removeAsset(const std::string& id)
{
  return attempt([&] {
    auto* db = m_impl->database.get();
    auto transaction = Transaction{db};
    requireAsset(db, id);
    auto users =
      Statement{db, "SELECT 1 FROM dependencies WHERE dependency_id=? LIMIT 1"};
    users.bind(1, id);
    if (users.next())
      throw StoreError{"Asset is referenced by other project content."};
    auto dependencies = Statement{db, "DELETE FROM dependencies WHERE asset_id=?"};
    dependencies.bind(1, id);
    dependencies.run();
    auto head = Statement{db, "DELETE FROM heads WHERE asset_id=?"};
    head.bind(1, id);
    head.run();
    transaction.commit();
  });
}

Result<void> ProjectStore::selectRevisions(const std::vector<Asset>& assets)
{
  return attempt([&] {
    auto* db = m_impl->database.get();
    auto transaction = Transaction{db};
    for (const auto& asset : assets)
    {
      requireAsset(db, asset.id);
      auto query = Statement{
        db, "SELECT virtual_path FROM revisions WHERE asset_id=? AND revision=?"};
      query.bind(1, asset.id);
      query.bind(2, asset.revision);
      if (!query.next())
        throw StoreError{"Project asset revision not found."};
      auto head =
        Statement{db, "UPDATE heads SET revision=?,virtual_path=? WHERE asset_id=?"};
      head.bind(1, asset.revision);
      head.bind(2, query.text(0));
      head.bind(3, asset.id);
      head.run();
    }
    transaction.commit();
  });
}

Result<std::vector<Asset>> ProjectStore::assets(
  const std::optional<AssetType> type, const std::string& virtualFolder) const
{
  return attempt([&] {
    validatePath(virtualFolder, true);
    auto result = queryAssets(m_impl->database.get());
    std::erase_if(result, [&](const auto& asset) {
      return (type && asset.type != *type)
             || (!virtualFolder.empty() && !asset.virtualPath.starts_with(virtualFolder + '/'));
    });
    return result;
  });
}

Result<std::optional<Asset>> ProjectStore::findAsset(const std::string& id) const
{
  return attempt([&]() -> std::optional<Asset> {
    const auto sql = fmt::format(
      "SELECT {} FROM revisions r JOIN heads h ON r.asset_id=h.asset_id AND "
      "r.revision=h.revision WHERE h.asset_id=?",
      AssetColumns);
    auto query = Statement{m_impl->database.get(), sql.c_str()};
    query.bind(1, id);
    return query.next() ? std::optional{readAssetRow(query)} : std::nullopt;
  });
}

Result<std::optional<Asset>> ProjectStore::findAssetByPath(
  const std::string& virtualPath) const
{
  return attempt([&]() -> std::optional<Asset> {
    const auto sql = fmt::format(
      "SELECT {} FROM revisions r JOIN heads h ON r.asset_id=h.asset_id AND "
      "r.revision=h.revision WHERE h.virtual_path=?",
      AssetColumns);
    auto query = Statement{m_impl->database.get(), sql.c_str()};
    query.bind(1, virtualPath);
    return query.next() ? std::optional{readAssetRow(query)} : std::nullopt;
  });
}

Result<std::vector<std::uint8_t>> ProjectStore::readAsset(
  const std::string& id,
  const ContentKind kind,
  const std::optional<std::uint64_t> revision) const
{
  return attempt([&] {
    const auto sql = fmt::format(
      "SELECT c.data FROM revisions r JOIN content c ON c.id=r.{} {} WHERE r.asset_id=? "
      "{}",
      kind == ContentKind::Original ? "original" : "compatible",
      revision ? "" : "JOIN heads h ON r.asset_id=h.asset_id AND r.revision=h.revision",
      revision ? "AND r.revision=?" : "");
    auto query = Statement{m_impl->database.get(), sql.c_str()};
    query.bind(1, id);
    if (revision)
      query.bind(2, *revision);
    if (!query.next())
      throw StoreError{"Project asset content not found."};
    return query.bytes(0);
  });
}

Result<void> ProjectStore::setMapThumbnail(
  const std::string& id, const std::vector<std::uint8_t>& png)
{
  return attempt([&] {
    const auto asset = findAsset(id);
    if (asset.is_error() || !asset.value() || asset.value()->type != AssetType::Map)
      throw StoreError{"Map asset not found."};
    if (png.empty() || png.size() > 4 * 1024 * 1024)
      throw StoreError{"Invalid map thumbnail size."};
    auto* db = m_impl->database.get();
    auto transaction = Transaction{db};
    // Optional derived metadata: older format-4 readers can safely ignore this table.
    execute(
      db,
      "CREATE TABLE IF NOT EXISTS map_thumbnails("
      "asset_id TEXT PRIMARY KEY REFERENCES heads(asset_id) ON DELETE CASCADE,"
      "hash TEXT NOT NULL,png BLOB NOT NULL)");
    auto write = Statement{
      db,
      "INSERT INTO map_thumbnails(asset_id,hash,png) VALUES(?,?,?) "
      "ON CONFLICT(asset_id) DO UPDATE SET hash=excluded.hash,png=excluded.png"};
    write.bind(1, id);
    write.bind(2, contentHash(png));
    write.bind(3, png);
    write.run();
    transaction.commit();
  });
}

Result<std::vector<std::uint8_t>> ProjectStore::mapThumbnail(const std::string& id) const
{
  return attempt([&] {
    auto* db = m_impl->database.get();
    auto table = Statement{
      db, "SELECT 1 FROM sqlite_master WHERE name='map_thumbnails' AND type='table'"};
    if (!table.next())
      return std::vector<std::uint8_t>{};
    auto query = Statement{db, "SELECT png FROM map_thumbnails WHERE asset_id=?"};
    query.bind(1, id);
    return query.next() ? query.bytes(0) : std::vector<std::uint8_t>{};
  });
}

Result<std::map<std::string, std::string>> ProjectStore::mapThumbnailHashes() const
{
  return attempt([&] {
    auto result = std::map<std::string, std::string>{};
    auto* db = m_impl->database.get();
    auto table = Statement{
      db, "SELECT 1 FROM sqlite_master WHERE name='map_thumbnails' AND type='table'"};
    if (!table.next())
      return result;
    auto query = Statement{db, "SELECT asset_id,hash FROM map_thumbnails"};
    while (query.next())
      result.emplace(query.text(0), query.text(1));
    return result;
  });
}

Result<std::optional<TerrainCompile>> ProjectStore::terrainCompile(
  const std::string& id) const
{
  return attempt([&]() -> std::optional<TerrainCompile> {
    if (m_impl->schemaVersion < 4)
      return std::nullopt;
    auto query = Statement{
      m_impl->database.get(),
      "SELECT source_revision,generator_version,top_faces,map,wad,error "
      "FROM terrain_compile WHERE asset_id=?"};
    query.bind(1, id);
    if (!query.next())
      return std::nullopt;
    auto result = TerrainCompile{
      query.integer(0),
      query.integer(1),
      query.integer(2),
      query.bytes(3),
      query.bytes(4),
      query.text(5),
      {}};
    auto table = Statement{
      m_impl->database.get(),
      "SELECT 1 FROM sqlite_master WHERE type='table' "
      "AND name='terrain_compile_dependencies'"};
    if (table.next())
    {
      auto dependencies = Statement{
        m_impl->database.get(),
        "SELECT dependency_id FROM terrain_compile_dependencies "
        "WHERE asset_id=? ORDER BY dependency_id"};
      dependencies.bind(1, id);
      while (dependencies.next())
        result.dependencies.push_back(dependencies.text(0));
    }
    return result;
  });
}

Result<void> ProjectStore::setTerrainCompile(
  const std::string& id, const TerrainCompile& data)
{
  return attempt([&] {
    auto* db = m_impl->database.get();
    auto transaction = Transaction{db};
    const auto asset = requireAsset(db, id);
    if (
      asset.type != AssetType::Map || asset.revision != data.sourceRevision
      || data.generatorVersion == 0
      || (data.error.empty() && (data.map.empty() || data.wad.empty()))
      || data.error.find('\0') != std::string::npos)
      throw StoreError{"Invalid or outdated terrain compile data."};
    // These auxiliary records and triggers are deliberately compatible with version 4
    // readers. External compilers see a missing cache, never a stale terrain result.
    execute(
      db,
      "CREATE TABLE IF NOT EXISTS terrain_compile_dependencies("
      "asset_id TEXT NOT NULL REFERENCES terrain_compile(asset_id) ON DELETE CASCADE,"
      "dependency_id TEXT NOT NULL,PRIMARY KEY(asset_id,dependency_id));"
      "CREATE INDEX IF NOT EXISTS terrain_compile_dependency_id "
      "ON terrain_compile_dependencies(dependency_id);"
      "CREATE TRIGGER IF NOT EXISTS terrain_head_insert AFTER INSERT ON heads BEGIN "
      "DELETE FROM terrain_compile WHERE asset_id IN (SELECT asset_id FROM "
      "terrain_compile_dependencies WHERE dependency_id=NEW.asset_id); END;"
      "CREATE TRIGGER IF NOT EXISTS terrain_head_update AFTER UPDATE ON heads "
      "WHEN OLD.revision<>NEW.revision BEGIN "
      "DELETE FROM terrain_compile WHERE asset_id IN (SELECT asset_id FROM "
      "terrain_compile_dependencies WHERE dependency_id=OLD.asset_id); END;"
      "CREATE TRIGGER IF NOT EXISTS terrain_head_delete AFTER DELETE ON heads BEGIN "
      "DELETE FROM terrain_compile WHERE asset_id IN (SELECT asset_id FROM "
      "terrain_compile_dependencies WHERE dependency_id=OLD.asset_id); END;"
      "CREATE TRIGGER IF NOT EXISTS terrain_dependency_insert AFTER INSERT ON "
      "dependencies BEGIN DELETE FROM terrain_compile WHERE asset_id IN "
      "(SELECT asset_id FROM terrain_compile_dependencies "
      "WHERE dependency_id=NEW.asset_id); END;"
      "CREATE TRIGGER IF NOT EXISTS terrain_dependency_delete AFTER DELETE ON "
      "dependencies BEGIN DELETE FROM terrain_compile WHERE asset_id IN "
      "(SELECT asset_id FROM terrain_compile_dependencies "
      "WHERE dependency_id=OLD.asset_id); END;"
      "CREATE TRIGGER IF NOT EXISTS terrain_dependency_update AFTER UPDATE ON "
      "dependencies BEGIN DELETE FROM terrain_compile WHERE asset_id IN "
      "(SELECT asset_id FROM terrain_compile_dependencies "
      "WHERE dependency_id=OLD.asset_id OR dependency_id=NEW.asset_id); END;");
    auto write = Statement{
      db,
      "INSERT INTO terrain_compile"
      "(asset_id,source_revision,generator_version,top_faces,map,wad,error) "
      "VALUES(?,?,?,?,?,?,?) ON CONFLICT(asset_id) DO UPDATE SET "
      "source_revision=excluded.source_revision,"
      "generator_version=excluded.generator_version,top_faces=excluded.top_faces,"
      "map=excluded.map,wad=excluded.wad,error=excluded.error"};
    write.bind(1, id);
    write.bind(2, data.sourceRevision);
    write.bind(3, data.generatorVersion);
    write.bind(4, data.topFaces);
    write.bind(5, data.map);
    write.bind(6, data.wad);
    write.bind(7, data.error);
    write.run();
    auto remove =
      Statement{db, "DELETE FROM terrain_compile_dependencies WHERE asset_id=?"};
    remove.bind(1, id);
    remove.run();
    auto insert = Statement{
      db,
      "INSERT OR IGNORE INTO terrain_compile_dependencies(asset_id,dependency_id) "
      "VALUES(?,?)"};
    for (const auto& dependency : data.dependencies)
    {
      validateText(dependency, "terrain dependency ID");
      insert.bind(1, id);
      insert.bind(2, dependency);
      insert.run();
      insert.reset();
    }
    transaction.commit();
    m_impl->schemaVersion = 4;
  });
}

Result<void> ProjectStore::clearTerrainCompile(const std::string& id)
{
  return attempt([&] {
    if (m_impl->schemaVersion < 4)
      return;
    auto remove =
      Statement{m_impl->database.get(), "DELETE FROM terrain_compile WHERE asset_id=?"};
    remove.bind(1, id);
    remove.run();
  });
}

Result<std::vector<Asset>> ProjectStore::libraryLinkedAssets() const
{
  return attempt([&] {
    // Only library linked assets are wanted here, so the query filters in SQL instead of
    // materializing every asset and dropping most of them afterwards. The update check
    // runs on every library scan, where reading the whole asset table dominated it.
    const auto sql = fmt::format(
      "SELECT {} FROM revisions r JOIN heads h ON r.asset_id=h.asset_id AND "
      "r.revision=h.revision WHERE r.library_id IS NOT NULL AND r.library_id <> '' "
      "ORDER BY r.virtual_path",
      AssetColumns);
    auto query = Statement{m_impl->database.get(), sql.c_str()};
    auto result = std::vector<Asset>{};
    while (query.next())
      result.push_back(readAssetRow(query));
    return result;
  });
}

Result<std::optional<Asset>> ProjectStore::findAssetByLibraryAsset(
  const std::string& libraryId, const std::string& libraryAssetId) const
{
  return attempt([&]() -> std::optional<Asset> {
    const auto sql = fmt::format(
      "SELECT {} FROM revisions r JOIN heads h ON r.asset_id=h.asset_id AND "
      "r.revision=h.revision WHERE r.library_id=? AND r.library_asset_id=?",
      AssetColumns);
    auto query = Statement{m_impl->database.get(), sql.c_str()};
    query.bind(1, libraryId);
    query.bind(2, libraryAssetId);
    return query.next() ? std::optional{readAssetRow(query)} : std::nullopt;
  });
}

Result<void> ProjectStore::attachLibrary(
  const std::string& id, const std::string& name, const std::string& lastPath)
{
  return attempt([&] {
    detail::validateAssetId(id);
    validateText(name, "library name");
    validateText(lastPath, "library path");
    auto* db = m_impl->database.get();
    auto transaction = Transaction{db};
    auto time = Statement{db, "SELECT strftime('%Y-%m-%dT%H:%M:%fZ','now')"};
    time.next();
    auto insert = Statement{
      db,
      "INSERT INTO libraries(id,name,last_path,attached_at) VALUES(?,?,?,?) ON "
      "CONFLICT(id) "
      "DO UPDATE SET name=excluded.name,last_path=excluded.last_path"};
    insert.bind(1, id);
    insert.bind(2, name);
    insert.bind(3, lastPath);
    insert.bind(4, time.text(0));
    insert.run();
    transaction.commit();
  });
}

Result<std::vector<LibraryInfo>> ProjectStore::libraries() const
{
  return attempt([&] {
    auto query = Statement{
      m_impl->database.get(),
      "SELECT id,name,last_path,attached_at FROM libraries ORDER BY name,id"};
    auto result = std::vector<LibraryInfo>{};
    while (query.next())
      result.push_back({query.text(0), query.text(1), query.text(2), query.text(3), {}});
    return result;
  });
}

Result<void> ProjectStore::ignoreLibraryUpdate(
  const std::string& assetId, const std::string& libraryHash)
{
  return attempt([&] {
    validateText(libraryHash, "library content hash");
    auto* db = m_impl->database.get();
    auto transaction = Transaction{db};
    requireAsset(db, assetId);
    auto time = Statement{db, "SELECT strftime('%Y-%m-%dT%H:%M:%fZ','now')"};
    time.next();
    auto insert = Statement{
      db,
      "INSERT INTO library_ignores(asset_id,library_hash,created_at) VALUES(?,?,?) ON "
      "CONFLICT DO NOTHING"};
    insert.bind(1, assetId);
    insert.bind(2, libraryHash);
    insert.bind(3, time.text(0));
    insert.run();
    transaction.commit();
  });
}

Result<std::vector<std::string>> ProjectStore::ignoredLibraryUpdates(
  const std::string& assetId) const
{
  return attempt([&] {
    auto query = Statement{
      m_impl->database.get(),
      "SELECT library_hash FROM library_ignores WHERE asset_id=? ORDER BY created_at"};
    query.bind(1, assetId);
    auto result = std::vector<std::string>{};
    while (query.next())
      result.push_back(query.text(0));
    return result;
  });
}

Result<void> ProjectStore::addDependency(
  const std::string& assetId, const std::string& dependencyId, const std::string& kind)
{
  return attempt([&] {
    validateText(kind, "dependency kind");
    auto* db = m_impl->database.get();
    auto transaction = Transaction{db};
    // Reject containment cycles before changing the current graph.
    auto cycle = Statement{
      db,
      "WITH RECURSIVE reachable(id) AS (SELECT ? UNION SELECT dependency_id FROM "
      "dependencies JOIN reachable ON asset_id=reachable.id) SELECT id FROM reachable "
      "WHERE id=?"};
    cycle.bind(1, dependencyId);
    cycle.bind(2, assetId);
    if (cycle.next())
      throw StoreError{"Project asset dependency would create a cycle."};
    auto insert = Statement{
      db,
      "INSERT INTO dependencies(asset_id,dependency_id,kind) VALUES(?,?,?) ON CONFLICT "
      "DO NOTHING"};
    insert.bind(1, assetId);
    insert.bind(2, dependencyId);
    insert.bind(3, kind);
    insert.run();
    transaction.commit();
  });
}

Result<std::vector<AssetDependency>> ProjectStore::dependencies(
  const std::string& assetId) const
{
  return attempt([&] {
    auto result = queryDependencies(m_impl->database.get());
    std::erase_if(result, [&](const auto& dep) { return dep.assetId != assetId; });
    return result;
  });
}

Result<std::vector<AssetDependency>> ProjectStore::dependents(
  const std::string& dependencyId) const
{
  return attempt([&] {
    auto result = queryDependencies(m_impl->database.get());
    std::erase_if(
      result, [&](const auto& dep) { return dep.dependencyId != dependencyId; });
    return result;
  });
}

Result<Checkpoint> ProjectStore::checkpoint(
  const std::string& name, const std::string& description)
{
  return attempt([&] {
    auto* db = m_impl->database.get();
    auto transaction = Transaction{db};
    auto result = makeCheckpoint(db, name, description);
    transaction.commit();
    return result;
  });
}

Result<std::vector<Checkpoint>> ProjectStore::checkpoints() const
{
  return attempt([&] {
    auto query = Statement{
      m_impl->database.get(),
      "SELECT id,name,description,created_at FROM checkpoints ORDER BY rowid"};
    auto result = std::vector<Checkpoint>{};
    while (query.next())
      result.push_back({query.text(0), query.text(1), query.text(2), query.text(3)});
    return result;
  });
}

Result<ProjectChanges> ProjectStore::compare(const std::string& checkpointId) const
{
  return attempt([&] {
    auto* db = m_impl->database.get();
    auto transaction = Transaction{db, false};
    requireCheckpoint(db, checkpointId);
    auto changes = std::map<std::string, AssetChange>{};
    for (const auto& asset : queryAssets(db, checkpointId))
      changes[asset.id] = {asset.id, asset, std::nullopt};
    for (const auto& asset : queryAssets(db))
    {
      auto& change = changes[asset.id];
      change.assetId = asset.id;
      change.after = asset;
    }
    auto result = ProjectChanges{};
    for (auto& [id, change] : changes)
    {
      if (change.before != change.after)
        result.assets.push_back(std::move(change));
    }
    const auto before = queryDependencies(db, checkpointId);
    const auto after = queryDependencies(db);
    for (const auto& dep : before)
    {
      if (std::find(after.begin(), after.end(), dep) == after.end())
        result.removedDependencies.push_back(dep);
    }
    for (const auto& dep : after)
    {
      if (std::find(before.begin(), before.end(), dep) == before.end())
        result.addedDependencies.push_back(dep);
    }
    transaction.commit();
    return result;
  });
}

Result<Checkpoint> ProjectStore::restore(const std::string& checkpointId)
{
  return attempt([&] {
    auto* db = m_impl->database.get();
    auto transaction = Transaction{db};
    requireCheckpoint(db, checkpointId);
    auto safety = makeCheckpoint(
      db, "Before restore", "State preserved before restoring " + checkpointId);
    execute(db, "DELETE FROM dependencies; DELETE FROM heads;");
    auto assets = Statement{
      db,
      "INSERT INTO heads SELECT r.asset_id,r.revision,r.virtual_path FROM revisions r "
      "JOIN checkpoint_assets h ON r.asset_id=h.asset_id AND r.revision=h.revision WHERE "
      "h.checkpoint_id=?"};
    assets.bind(1, checkpointId);
    assets.run();
    auto deps = Statement{
      db,
      "INSERT INTO dependencies SELECT asset_id,dependency_id,kind FROM "
      "checkpoint_dependencies WHERE checkpoint_id=?"};
    deps.bind(1, checkpointId);
    deps.run();
    transaction.commit();
    return safety;
  });
}

Result<void> ProjectStore::saveCopy(const std::filesystem::path& destination) const
{
  return attempt([&] {
    auto reservation = NewFile{destination};
    auto target = Database{destination, false};
    // Backup holds the destination write lock. SQLite handles source snapshot
    // consistency.
    auto* backup =
      sqlite3_backup_init(target.get(), "main", m_impl->database.get(), "main");
    if (!backup)
      throw StoreError{sqlite3_errmsg(target.get())};
    const auto step = sqlite3_backup_step(backup, -1);
    const auto finish = sqlite3_backup_finish(backup);
    if (step != SQLITE_DONE)
      check(target.get(), step);
    check(target.get(), finish);
    reservation.keep();
  });
}

Result<RevisionCleanup> ProjectStore::unreferencedRevisions() const
{
  return attempt([&] {
    auto* db = m_impl->database.get();
    auto cleanup = RevisionCleanup{};
    cleanup.revisions = queryUnreferencedRevisions(db);
    const auto orphaned = orphanedContent(db, cleanup.revisions);
    cleanup.contentRows = orphaned.rows;
    cleanup.contentBytes = orphaned.bytes;
    return cleanup;
  });
}

Result<RevisionCleanup> ProjectStore::pruneUnreferencedRevisions()
{
  return attempt([&] {
    auto* db = m_impl->database.get();
    auto transaction = Transaction{db};
    auto cleanup = RevisionCleanup{};
    cleanup.revisions = queryUnreferencedRevisions(db);

    auto drop = Statement{db, "DELETE FROM revisions WHERE asset_id=? AND revision=?"};
    for (const auto& revision : cleanup.revisions)
    {
      drop.bind(1, revision.assetId);
      drop.bind(2, revision.revision);
      drop.run();
      drop.reset();
    }

    // Content is shared between revisions, so it is removed only after every revision
    // that could still reference it is gone.
    auto orphaned = Statement{
      db,
      "SELECT count(*),coalesce(sum(length(data)),0) FROM content WHERE id NOT IN "
      "(SELECT original FROM revisions UNION SELECT compatible FROM revisions)"};
    if (orphaned.next())
    {
      cleanup.contentRows = orphaned.integer(0);
      cleanup.contentBytes = orphaned.integer(1);
    }
    auto dropContent = Statement{
      db,
      "DELETE FROM content WHERE id NOT IN (SELECT original FROM revisions UNION "
      "SELECT compatible FROM revisions)"};
    dropContent.run();

    transaction.commit();
    return cleanup;
  });
}

} // namespace tb::project
