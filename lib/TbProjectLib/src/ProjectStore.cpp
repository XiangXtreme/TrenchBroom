/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.
 */

#include "project/ProjectStore.h"

#include "base/Uuid.h"

#include <fcntl.h>
#include <fmt/format.h>
#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <limits>
#include <map>
#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#endif
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace tb::project
{
namespace
{

// Internal exceptions unwind statements and transactions before returning a public
// Result.
class StoreError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

// Reserve with OS-level exclusive creation, including empty existing files. The
// reservation outlives the SQLite connection so failed operations can clean up.
class NewFile
{
  std::filesystem::path m_path;
  bool m_keep = false;

public:
  explicit NewFile(const std::filesystem::path& path)
    : m_path{path}
  {
#ifdef _WIN32
    const auto fd = _wopen(
      path.c_str(), _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
    const auto fd = ::open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
#endif
    if (fd < 0)
      throw StoreError{fmt::format("Cannot create project: {}", std::strerror(errno))};
#ifdef _WIN32
    _close(fd);
#else
    ::close(fd);
#endif
  }
  ~NewFile()
  {
    if (!m_keep)
    {
      auto ec = std::error_code{};
      std::filesystem::remove(m_path, ec);
    }
  }
  void keep() { m_keep = true; }
};

template <typename F>
auto attempt(F&& f) -> Result<std::invoke_result_t<F>>
{
  try
  {
    if constexpr (std::is_void_v<std::invoke_result_t<F>>)
    {
      f();
      return {};
    }
    else
    {
      return f();
    }
  }
  catch (const StoreError& e)
  {
    return Error{e.what()};
  }
  catch (const std::filesystem::filesystem_error& e)
  {
    return Error{e.what()};
  }
}

void check(sqlite3* db, const int code)
{
  if (code != SQLITE_OK)
  {
    throw StoreError{fmt::format("Project storage: {}", sqlite3_errmsg(db))};
  }
}

void execute(sqlite3* db, const char* sql)
{
  check(db, sqlite3_exec(db, sql, nullptr, nullptr, nullptr));
}

std::string utf8Path(const std::filesystem::path& path)
{
  const auto s = path.u8string();
  return {reinterpret_cast<const char*>(s.data()), s.size()};
}

class Database
{
private:
  sqlite3* m_db = nullptr;

public:
  Database(const std::filesystem::path& path, const bool create)
  {
    const auto code = sqlite3_open_v2(
      utf8Path(path).c_str(),
      &m_db,
      SQLITE_OPEN_READWRITE | (create ? SQLITE_OPEN_CREATE : 0),
      nullptr);
    if (code != SQLITE_OK)
    {
      const auto message = std::string{sqlite3_errmsg(m_db)};
      sqlite3_close(m_db);
      throw StoreError{message};
    }
    sqlite3_extended_result_codes(m_db, 1);
    sqlite3_busy_timeout(m_db, 1000);
    sqlite3_db_config(m_db, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr);
    sqlite3_db_config(m_db, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, nullptr);
  }

  ~Database() { sqlite3_close(m_db); }
  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;
  sqlite3* get() const { return m_db; }
};

class Statement
{
private:
  sqlite3* m_db;
  sqlite3_stmt* m_stmt = nullptr;

public:
  Statement(sqlite3* db, const char* sql)
    : m_db{db}
  {
    check(db, sqlite3_prepare_v2(db, sql, -1, &m_stmt, nullptr));
  }
  ~Statement() { sqlite3_finalize(m_stmt); }
  Statement(const Statement&) = delete;
  Statement& operator=(const Statement&) = delete;

  void bind(const int index, const std::string_view value)
  {
    check(
      m_db,
      sqlite3_bind_text64(
        m_stmt, index, value.data(), value.size(), SQLITE_TRANSIENT, SQLITE_UTF8));
  }
  void bind(const int index, const std::uint64_t value)
  {
    if (value > static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max()))
    {
      throw StoreError{"Project revision is out of range."};
    }
    check(m_db, sqlite3_bind_int64(m_stmt, index, static_cast<sqlite3_int64>(value)));
  }
  void bind(const int index, const std::vector<std::uint8_t>& value)
  {
    if (value.empty())
    {
      check(m_db, sqlite3_bind_zeroblob(m_stmt, index, 0));
    }
    else
    {
      check(
        m_db,
        sqlite3_bind_blob64(m_stmt, index, value.data(), value.size(), SQLITE_TRANSIENT));
    }
  }
  bool next()
  {
    const auto code = sqlite3_step(m_stmt);
    if (code == SQLITE_ROW)
      return true;
    if (code == SQLITE_DONE)
      return false;
    check(m_db, code);
    return false;
  }
  void run()
  {
    if (next())
      throw StoreError{"Unexpected project storage query result."};
  }
  std::string text(const int column) const
  {
    const auto* data = sqlite3_column_text(m_stmt, column);
    const auto size = sqlite3_column_bytes(m_stmt, column);
    return data
             ? std::string{reinterpret_cast<const char*>(data), static_cast<size_t>(size)}
             : std::string{};
  }
  std::uint64_t integer(const int column) const
  {
    const auto value = sqlite3_column_int64(m_stmt, column);
    if (value < 0)
      throw StoreError{"Invalid negative project revision."};
    return static_cast<std::uint64_t>(value);
  }
  std::vector<std::uint8_t> bytes(const int column) const
  {
    const auto* data =
      static_cast<const std::uint8_t*>(sqlite3_column_blob(m_stmt, column));
    const auto size = sqlite3_column_bytes(m_stmt, column);
    return size ? std::vector<std::uint8_t>{data, data + size}
                : std::vector<std::uint8_t>{};
  }
};

class Transaction
{
private:
  sqlite3* m_db;
  bool m_committed = false;

public:
  explicit Transaction(sqlite3* db, const bool write = true)
    : m_db{db}
  {
    execute(db, write ? "BEGIN IMMEDIATE" : "BEGIN");
  }
  ~Transaction()
  {
    if (!m_committed)
      sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
  }
  Transaction(const Transaction&) = delete;
  Transaction& operator=(const Transaction&) = delete;
  void commit()
  {
    execute(m_db, "COMMIT");
    m_committed = true;
  }
};

void validateText(const std::string& value, const char* field)
{
  if (value.empty() || value.find('\0') != std::string::npos)
  {
    throw StoreError{fmt::format("Invalid empty or NUL-containing {}.", field)};
  }
}

void validatePath(const std::string& path, const bool folder = false)
{
  if (folder && path.empty())
    return;
  validateText(path, "virtual path");
  if (
    path.front() == '/' || path.back() == '/'
    || path.find_first_of("\\:<>\"|?*") != std::string::npos
    || std::ranges::any_of(path, [](const unsigned char c) { return c < 32; }))
  {
    throw StoreError{"Virtual paths must be relative, with forward slash separators."};
  }
  size_t start = 0;
  while (start < path.size())
  {
    const auto end = path.find('/', start);
    const auto part = path.substr(start, end == std::string::npos ? end : end - start);
    if (
      part.empty() || part == "." || part == ".." || part.back() == '.'
      || part.back() == ' ')
    {
      throw StoreError{"Virtual paths cannot contain empty, dot or parent components."};
    }
    auto stem = part.substr(0, part.find('.'));
    std::ranges::transform(
      stem, stem.begin(), [](const unsigned char c) { return char(std::tolower(c)); });
    if (
      stem == "con" || stem == "prn" || stem == "aux" || stem == "nul"
      || (stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '1' && stem[3] <= '9'))
      throw StoreError{"Virtual paths cannot contain reserved device names."};
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
}

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
    query.text(7)};
  if (asset.id.size() != 36 || std::ranges::any_of(asset.id, [](const unsigned char c) {
        return !std::isxdigit(c) && c != '-';
      }))
    throw StoreError{"Invalid project asset ID."};
  validatePath(asset.virtualPath);
  validatePath(asset.publishPath);
  validateText(asset.displayName, "display name");
  if (asset.revision == 0)
    throw StoreError{"Invalid project asset revision."};
  return asset;
}

constexpr auto AssetColumns =
  "r.asset_id,r.type,r.virtual_path,r.display_name,"
  "r.source_name,r.revision,r.recipe,"
  "(SELECT first.virtual_path FROM revisions first WHERE first.asset_id=r.asset_id ORDER "
  "BY first.revision LIMIT 1)";

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

// FNV is an index hint only. Full byte comparison below prevents hash collisions from
// aliasing distinct payloads; it is not used as an integrity or security checksum.
std::string contentIndex(const std::vector<std::uint8_t>& data)
{
  auto hash = std::uint64_t{14695981039346656037ULL};
  for (const auto byte : data)
    hash = (hash ^ byte) * 1099511628211ULL;
  return fmt::format("{:016x}", hash);
}

std::string storeContent(sqlite3* db, const std::vector<std::uint8_t>& data)
{
  const auto hash = contentIndex(data);
  auto find = Statement{db, "SELECT id FROM content WHERE hash=? AND data=?"};
  find.bind(1, hash);
  find.bind(2, data);
  if (find.next())
    return find.text(0);
  const auto id = generateUuid();
  auto insert = Statement{db, "INSERT INTO content(id,hash,data) VALUES(?,?,?)"};
  insert.bind(1, id);
  insert.bind(2, hash);
  insert.bind(3, data);
  insert.run();
  return id;
}

void writeRevision(
  sqlite3* db, Asset& asset, const std::string& original, const std::string& compatible)
{
  validateText(asset.displayName, "display name");
  validatePath(asset.virtualPath);
  const auto publishPath =
    asset.publishPath.empty() ? asset.virtualPath : asset.publishPath;
  validatePath(publishPath);
  auto conflicts = Statement{
    db,
    "SELECT h.virtual_path,(SELECT first.virtual_path FROM revisions first WHERE "
    "first.asset_id=h.asset_id ORDER BY first.revision LIMIT 1) "
    "FROM heads h WHERE h.asset_id<>?"};
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
    if (
      overlaps(asset.virtualPath, conflicts.text(0))
      || overlaps(publishPath, conflicts.text(1)))
      throw StoreError{"Project paths conflict with an existing asset or folder."};
  asset.publishPath = publishPath;
  // A restore never reuses a revision number, including for assets absent from heads.
  auto next =
    Statement{db, "SELECT COALESCE(MAX(revision),0)+1 FROM revisions WHERE asset_id=?"};
  next.bind(1, asset.id);
  next.next();
  asset.revision = next.integer(0);
  auto insert = Statement{
    db,
    "INSERT INTO "
    "revisions(asset_id,type,virtual_path,display_name,source_name,revision,recipe,"
    "original,compatible) VALUES(?,?,?,?,?,?,?,?,?)"};
  insert.bind(1, asset.id);
  insert.bind(2, assetTypeName(asset.type));
  insert.bind(3, asset.virtualPath);
  insert.bind(4, asset.displayName);
  insert.bind(5, asset.sourceName);
  insert.bind(6, asset.revision);
  insert.bind(7, asset.conversionRecipe);
  insert.bind(8, original);
  insert.bind(9, compatible);
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

void writeMetadataRevision(sqlite3* db, Asset& asset)
{
  auto query = Statement{
    db, "SELECT original,compatible FROM revisions WHERE asset_id=? AND revision=?"};
  query.bind(1, asset.id);
  query.bind(2, asset.revision);
  if (!query.next())
    throw StoreError{"Project asset revision not found."};
  const auto original = query.text(0);
  const auto compatible = query.text(1);
  writeRevision(db, asset, original, compatible);
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
  "PRAGMA application_id=0x5442504a; PRAGMA user_version=1;"
  "CREATE TABLE project(id TEXT PRIMARY KEY,name TEXT NOT NULL,game_id TEXT NOT NULL);"
  "CREATE TABLE content(id TEXT PRIMARY KEY,hash TEXT NOT NULL,data BLOB NOT NULL);"
  "CREATE INDEX content_hash ON content(hash);"
  "CREATE TABLE revisions("
  "asset_id TEXT NOT NULL,type TEXT NOT NULL,virtual_path TEXT NOT NULL,display_name "
  "TEXT NOT NULL,"
  "source_name TEXT NOT NULL,revision INTEGER NOT NULL CHECK(revision>0),recipe TEXT NOT "
  "NULL,"
  "original TEXT NOT NULL REFERENCES content(id),compatible TEXT NOT NULL REFERENCES "
  "content(id),"
  "PRIMARY KEY(asset_id,revision));"
  "CREATE TABLE heads(asset_id TEXT PRIMARY KEY,revision INTEGER NOT NULL,virtual_path "
  "TEXT NOT NULL UNIQUE,"
  "FOREIGN KEY(asset_id,revision) REFERENCES revisions(asset_id,revision));"
  "CREATE TABLE dependencies(asset_id TEXT NOT NULL REFERENCES heads(asset_id),"
  "dependency_id TEXT NOT NULL REFERENCES heads(asset_id),kind TEXT NOT NULL,"
  "PRIMARY KEY(asset_id,dependency_id,kind),CHECK(asset_id<>dependency_id));"
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
  "checkpoint_assets(checkpoint_id,asset_id));";

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
    {
      auto version =
        Statement{db, "SELECT * FROM pragma_application_id(),pragma_user_version()"};
      if (!version.next() || version.integer(0) != 0x5442504a || version.integer(1) != 1)
        throw StoreError{"Not a supported TB project (format version 1 required)."};
    }
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

Result<Asset> ProjectStore::importAsset(const AssetImport& import)
{
  return attempt([&] {
    if (parseAssetType(assetTypeName(import.type)).is_error())
      throw StoreError{"Invalid asset type."};
    auto* db = m_impl->database.get();
    auto transaction = Transaction{db};
    auto asset = Asset{
      generateUuid(),
      import.type,
      import.virtualPath,
      import.displayName,
      import.sourceName,
      0,
      import.conversionRecipe,
      {}};
    writeRevision(
      db, asset, storeContent(db, import.original), storeContent(db, import.compatible));
    transaction.commit();
    return asset;
  });
}

Result<Asset> ProjectStore::replaceAsset(const std::string& id, const AssetImport& import)
{
  return attempt([&] {
    auto* db = m_impl->database.get();
    auto transaction = Transaction{db};
    auto asset = requireAsset(db, id);
    if (asset.type != import.type)
      throw StoreError{"Reimport cannot change an asset's type."};
    asset.sourceName = import.sourceName;
    asset.conversionRecipe = import.conversionRecipe;
    const auto original = storeContent(db, import.original);
    const auto compatible = storeContent(db, import.compatible);
    auto existing = Statement{
      db,
      "SELECT revision FROM revisions WHERE asset_id=? AND source_name=? AND recipe=? "
      "AND original=? AND compatible=? AND display_name=? AND virtual_path=? ORDER BY "
      "revision DESC LIMIT 1"};
    existing.bind(1, id);
    existing.bind(2, asset.sourceName);
    existing.bind(3, asset.conversionRecipe);
    existing.bind(4, original);
    existing.bind(5, compatible);
    existing.bind(6, asset.displayName);
    existing.bind(7, asset.virtualPath);
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
    transaction.commit();
    return asset;
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

} // namespace tb::project
