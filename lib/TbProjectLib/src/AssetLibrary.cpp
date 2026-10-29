/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom.
*/

#include "project/AssetLibrary.h"

#include "CatalogValidation.h"
#include "Sqlite.h"
#include "project/ContentHash.h"
#include "project/GoldSrcModel.h"
#include "project/GoldSrcWad.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace tb::project
{
namespace
{

constexpr auto IndexFileName = ".tbassets";

std::string pathText(const std::filesystem::path& path)
{
  const auto text = path.generic_u8string();
  return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::string lowered(std::string value)
{
  std::ranges::transform(
    value, value.begin(), [](const unsigned char c) { return char(std::tolower(c)); });
  return value;
}

std::string jsonQuote(const std::string_view text)
{
  auto result = std::string{"\""};
  for (const auto c : text)
  {
    switch (c)
    {
    case '"':
      result += "\\\"";
      break;
    case '\\':
      result += "\\\\";
      break;
    case '\n':
      result += "\\n";
      break;
    case '\r':
      result += "\\r";
      break;
    case '\t':
      result += "\\t";
      break;
    default:
      if (static_cast<unsigned char>(c) < 32)
        result += fmt::format("\\u{:04x}", int(static_cast<unsigned char>(c)));
      else
        result.push_back(c);
    }
  }
  result.push_back('"');
  return result;
}

Result<std::vector<std::uint8_t>> readFile(const std::filesystem::path& path)
{
  auto file = std::ifstream{path, std::ios::binary};
  if (!file)
    return Error{"Cannot open library source file."};
  file.seekg(0, std::ios::end);
  const auto size = file.tellg();
  if (size < 0)
    return Error{"Cannot measure library source file."};
  file.seekg(0, std::ios::beg);
  auto bytes = std::vector<std::uint8_t>(std::size_t(size));
  if (!bytes.empty())
    file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
  if (!file && !bytes.empty())
    return Error{"Cannot read library source file."};
  return bytes;
}

enum class SourceFormat
{
  Wad,
  Image,
  Model,
  Sprite,
  Sound,
  Unknown,
};

SourceFormat sourceFormatFor(const std::filesystem::path& extension)
{
  const auto ext = lowered(extension.string());
  if (ext == ".wad")
    return SourceFormat::Wad;
  if (ext == ".png" || ext == ".bmp" || ext == ".tga" || ext == ".jpg" || ext == ".jpeg")
    return SourceFormat::Image;
  if (ext == ".mdl")
    return SourceFormat::Model;
  if (ext == ".spr")
    return SourceFormat::Sprite;
  if (ext == ".wav")
    return SourceFormat::Sound;
  return SourceFormat::Unknown;
}

AssetType assetTypeFor(const SourceFormat format)
{
  switch (format)
  {
  case SourceFormat::Image:
    return AssetType::Texture;
  case SourceFormat::Model:
    return AssetType::Model;
  case SourceFormat::Sprite:
    return AssetType::Sprite;
  case SourceFormat::Sound:
    return AssetType::Sound;
  case SourceFormat::Wad:
  case SourceFormat::Unknown:
    break;
  }
  return AssetType::Texture;
}

const char* nativeFormatFor(const SourceFormat format)
{
  switch (format)
  {
  case SourceFormat::Image:
    return "image";
  case SourceFormat::Model:
    return "mdl";
  case SourceFormat::Sprite:
    return "spr";
  case SourceFormat::Sound:
    return "wav";
  case SourceFormat::Wad:
    return "wad3";
  case SourceFormat::Unknown:
    break;
  }
  return "unknown";
}

// Every non-native source keeps its original bytes and describes how a GoldSrc
// compatible result is produced. Conversion itself runs in the editor, because it
// needs the image pipeline and a user confirmation of the texture name.
std::string recipeFor(const SourceFormat format, const std::string& container)
{
  switch (format)
  {
  case SourceFormat::Image:
    return fmt::format(
      R"({{"importer":"image","version":1,"nativeFormat":"image",)"
      R"("compatibleFormat":"goldsrc-miptex","requiresConversion":true}})");
  case SourceFormat::Model:
    return R"({"importer":"native-copy","version":1,"nativeFormat":"mdl"})";
  case SourceFormat::Sprite:
    return R"({"importer":"native-copy","version":1,"nativeFormat":"spr"})";
  case SourceFormat::Sound:
    return R"({"importer":"native-copy","version":1,"nativeFormat":"wav"})";
  case SourceFormat::Wad:
    return fmt::format(
      R"({{"importer":"wad-member","version":1,"nativeFormat":"wad3-miptex",)"
      R"("container":{},"requiresConversion":false}})",
      jsonQuote(container));
  case SourceFormat::Unknown:
    break;
  }
  return R"({"importer":"unknown","version":1})";
}

std::string libraryKey(
  const std::string& libraryId, const std::string& container, const std::string& member)
{
  return fmt::format("tbasset|{}|{}|{}", libraryId, container, member);
}

struct Discovered
{
  std::string id;
  AssetType type = AssetType::Texture;
  std::string displayName;
  std::string virtualPath;
  std::string group;
  std::string sourceRelPath;
  std::string sourceMember;
  std::string identity;
  std::string nativeFormat;
  std::string recipe;
  std::vector<std::uint8_t> content;
  std::string contentHash;
  std::uint64_t size = 0;
  // Dependency hints as `<kind>:<normalized key>`; resolved once every asset is known.
  std::vector<std::string> hints;
};

std::string joinedHints(const std::vector<std::string>& hints)
{
  auto result = std::string{};
  for (const auto& hint : hints)
  {
    if (!result.empty())
      result.push_back('\n');
    result += hint;
  }
  return result;
}

struct DiscoveredSource
{
  std::string relativePath;
  AssetType type = AssetType::Texture;
  std::string contentHash;
  std::uint64_t size = 0;
  std::uint64_t assetCount = 0;
  bool missing = false;
};

struct Discovery
{
  std::vector<Discovered> assets;
  std::vector<DiscoveredSource> sources;
  std::vector<std::string> warnings;
  std::size_t invalid = 0;
};

std::string parentFolder(const std::string& relativePath)
{
  const auto slash = relativePath.rfind('/');
  return slash == std::string::npos ? std::string{} : relativePath.substr(0, slash);
}

std::string fileNameOf(const std::string& path)
{
  const auto slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string stemOf(const std::string& path)
{
  const auto name = fileNameOf(path);
  const auto dot = name.find_last_of('.');
  return dot == std::string::npos ? name : name.substr(0, dot);
}

void discoverFile(
  const std::filesystem::path& folder,
  const std::filesystem::path& file,
  const std::string& libraryId,
  Discovery& discovery,
  std::unordered_set<std::string>& seenIds)
{
  auto error = std::error_code{};
  const auto relative = std::filesystem::relative(file, folder, error);
  if (error || relative.empty())
    return;
  const auto relativePath = pathText(relative);
  const auto format = sourceFormatFor(file.extension());
  if (format == SourceFormat::Unknown)
    return;

  const auto bytesResult = readFile(file);
  if (bytesResult.is_error())
  {
    discovery.warnings.push_back(relativePath + ": cannot read source file");
    return;
  }
  const auto& bytes = bytesResult.value();
  auto& source = discovery.sources.emplace_back(DiscoveredSource{
    relativePath,
    assetTypeFor(format),
    contentHash(bytes),
    std::uint64_t(bytes.size()),
    0});

  if (format == SourceFormat::Wad)
  {
    const auto wad = readGoldSrcWad(bytes);
    if (wad.is_error())
    {
      discovery.warnings.push_back(
        relativePath + ": " + std::get<Error>(wad.error()).msg);
      return;
    }
    for (const auto& entry : wad.value().textures)
    {
      if (!entry.valid)
      {
        ++discovery.invalid;
        continue;
      }
      auto asset = Discovered{};
      asset.id = stableId(libraryKey(libraryId, relativePath, entry.name));
      if (!seenIds.insert(asset.id).second)
      {
        // A WAD can list the same name twice. The engine sees the first entry, and a
        // single asset identity must not be split into two catalog rows.
        discovery.warnings.push_back(
          relativePath + ": duplicate texture entry " + entry.name);
        ++discovery.invalid;
        continue;
      }
      asset.type = AssetType::Texture;
      asset.displayName = entry.name;
      asset.virtualPath = relativePath + "/" + entry.name;
      asset.group = relativePath;
      asset.sourceRelPath = relativePath;
      asset.sourceMember = entry.name;
      asset.identity = lowered(entry.name);
      asset.nativeFormat = "goldsrc-miptex";
      asset.recipe = recipeFor(format, relativePath);
      asset.content = entry.miptex;
      asset.size = std::uint64_t(entry.miptex.size());
      asset.contentHash = contentHash(asset.content);
      source.assetCount += 1;
      discovery.assets.push_back(std::move(asset));
    }
    return;
  }

  auto asset = Discovered{};
  asset.id = stableId(libraryKey(libraryId, relativePath, {}));
  if (!seenIds.insert(asset.id).second)
    return;
  asset.type = assetTypeFor(format);
  asset.displayName = file.stem().string();
  if (asset.displayName.empty())
    asset.displayName = file.filename().string();
  asset.virtualPath = relativePath;
  asset.group = parentFolder(relativePath);
  asset.sourceRelPath = relativePath;
  asset.identity = lowered(file.filename().string());
  asset.nativeFormat = nativeFormatFor(format);
  asset.recipe = recipeFor(format, relativePath);
  asset.content = bytes;
  asset.size = std::uint64_t(bytes.size());
  asset.contentHash = contentHash(asset.content);
  if (format == SourceFormat::Model)
  {
    // A GoldSrc model embeds its textures; what lives beside it are the separate texture
    // file and the external animation groups. Names are derived the way the engine
    // derives them and only become edges when the file exists in the library.
    if (const auto model = readGoldSrcModelDependencies(bytes); model.is_success())
      for (const auto& group :
           modelAnimationGroupNames(relativePath, model.value().sequenceGroups))
        asset.hints.push_back("model-animation:" + modelDependencyKey(group));
    if (const auto companion = modelCompanionName(file.filename().string());
        !companion.empty())
      asset.hints.push_back("model-companion:" + modelDependencyKey(companion));
  }
  source.assetCount = 1;
  discovery.assets.push_back(std::move(asset));
}

/**
 * Groups the six faces of a GoldSrc skybox.
 *
 * A skybox is a logical asset: the faces stay independently editable textures, and the
 * group only records which six belong together so a capture can pull the whole set.
 */
void addSkyboxGroups(const std::string& libraryId, Discovery& discovery)
{
  static constexpr std::array<std::string_view, 6> Faces{
    "bk", "rt", "lf", "ft", "up", "dn"};
  struct FaceSet
  {
    std::string folder;
    std::string stem;
    std::map<std::string, const Discovered*> faces;
  };

  auto groups = std::map<std::string, FaceSet>{};
  for (const auto& asset : discovery.assets)
  {
    if (asset.nativeFormat != "image")
      continue;
    const auto folder = parentFolder(asset.virtualPath);
    const auto stem = stemOf(fileNameOf(asset.virtualPath));
    const auto lowerStem = lowered(stem);
    for (const auto suffix : Faces)
    {
      if (lowerStem.size() <= suffix.size() || !lowerStem.ends_with(suffix))
        continue;
      auto originalStem = stem.substr(0, stem.size() - suffix.size());
      const auto key =
        (folder.empty() ? std::string{} : folder + "/") + lowered(originalStem);
      auto& group = groups[key];
      group.folder = folder;
      if (group.stem.empty())
        group.stem = originalStem;
      group.faces.emplace(std::string{suffix}, &asset);
      break;
    }
  }

  for (const auto& [key, group] : groups)
  {
    if (group.faces.size() != Faces.size())
      continue;
    const auto virtualPath =
      (group.folder.empty() ? std::string{} : group.folder + "/") + group.stem;
    auto asset = Discovered{};
    asset.id = stableId(libraryKey(libraryId, virtualPath, {}));
    asset.type = AssetType::Skybox;
    asset.displayName = group.stem;
    asset.virtualPath = virtualPath;
    asset.group = group.folder;
    asset.sourceRelPath = group.folder;
    asset.identity = lowered(group.stem);
    asset.nativeFormat = "skybox";
    asset.recipe =
      R"({"importer":"skybox-group","version":1,"faces":["bk","rt","lf","ft","up","dn"],)"
      R"("engineValidated":false})";
    // The manifest is the group's content: the faces keep their own identity, hashes and
    // revisions, so changing one face never rewrites the group as a texture change.
    auto manifest = fmt::format("skybox {}\n", virtualPath);
    for (const auto suffix : Faces)
    {
      const auto& face = group.faces.at(std::string{suffix});
      manifest += fmt::format("{} {}\n", suffix, face->virtualPath);
      asset.hints.push_back("skybox-face:" + modelDependencyKey(face->virtualPath));
    }
    asset.content.assign(manifest.begin(), manifest.end());
    asset.size = std::uint64_t(asset.content.size());
    asset.contentHash = contentHash(asset.content);
    discovery.assets.push_back(std::move(asset));
  }
}

Discovery discover(const std::filesystem::path& folder, const std::string& libraryId)
{
  auto discovery = Discovery{};
  auto seenIds = std::unordered_set<std::string>{};
  auto iterator = std::filesystem::recursive_directory_iterator{
    folder, std::filesystem::directory_options::skip_permission_denied};
  const auto end = std::filesystem::recursive_directory_iterator{};
  for (auto error = std::error_code{}; iterator != end; iterator.increment(error))
  {
    if (error)
    {
      discovery.warnings.push_back("cannot enumerate part of the library folder");
      error.clear();
      continue;
    }
    const auto& entry = *iterator;
    auto status = entry.symlink_status(error);
    if (error)
    {
      error.clear();
      continue;
    }
    if (std::filesystem::is_directory(status))
    {
      // The catalog itself and hidden folders are not library content.
      const auto name = entry.path().filename().string();
      if (name.starts_with('.'))
        iterator.disable_recursion_pending();
      continue;
    }
    if (entry.path().filename() == IndexFileName)
      continue;
    if (!std::filesystem::is_regular_file(status))
      continue;
    discoverFile(folder, entry.path(), libraryId, discovery, seenIds);
  }
  addSkyboxGroups(libraryId, discovery);
  std::ranges::sort(discovery.assets, [](const auto& a, const auto& b) {
    return a.virtualPath < b.virtualPath;
  });
  std::ranges::sort(discovery.sources, [](const auto& a, const auto& b) {
    return a.relativePath < b.relativePath;
  });
  return discovery;
}

constexpr auto AssetColumns =
  "a.id,a.type,a.display_name,a.virtual_path,a.group_path,a.source_rel_path,"
  "a.source_member,a.native_format,a.recipe,a.revision,a.content_hash,a.size,a.missing";

LibraryAsset readAssetRow(const detail::Statement& query)
{
  const auto type = parseAssetType(query.text(1));
  if (type.is_error())
    throw detail::StoreError{std::get<Error>(type.error()).msg};
  auto asset = LibraryAsset{
    query.text(0),
    type.value(),
    query.text(2),
    query.text(3),
    query.text(4),
    query.text(5),
    query.text(6),
    query.text(7),
    query.text(8),
    query.integer(9),
    query.text(10),
    query.integer(11),
    query.flag(12) != 0};
  detail::validateAssetId(asset.id);
  detail::validatePath(asset.virtualPath);
  detail::validatePath(asset.sourceContainer);
  detail::validateText(asset.displayName, "display name");
  if (asset.revision == 0)
    throw detail::StoreError{"Invalid library asset revision."};
  return asset;
}

constexpr auto Schema =
  "PRAGMA application_id=0x54424153; PRAGMA user_version=1;"
  "CREATE TABLE library(id TEXT PRIMARY KEY,name TEXT NOT NULL,"
  "created_at TEXT NOT NULL,updated_at TEXT NOT NULL);"
  "CREATE TABLE content(id TEXT PRIMARY KEY,hash TEXT NOT NULL,data BLOB NOT NULL);"
  "CREATE INDEX content_hash ON content(hash);"
  "CREATE TABLE sources(relative_path TEXT PRIMARY KEY,type TEXT NOT NULL,"
  "content_hash TEXT NOT NULL,size INTEGER NOT NULL,asset_count INTEGER NOT NULL,"
  "missing INTEGER NOT NULL DEFAULT 0);"
  "CREATE TABLE assets(id TEXT PRIMARY KEY,type TEXT NOT NULL,display_name TEXT NOT NULL,"
  "virtual_path TEXT NOT NULL,group_path TEXT NOT NULL,source_rel_path TEXT NOT NULL,"
  "source_member TEXT NOT NULL,native_format TEXT NOT NULL,identity TEXT NOT NULL,"
  "recipe TEXT NOT NULL,revision INTEGER NOT NULL CHECK(revision>0),"
  "content_hash TEXT NOT NULL,content TEXT NOT NULL REFERENCES content(id),"
  "original TEXT NOT NULL REFERENCES content(id),size INTEGER NOT NULL,"
  "dependency_hints TEXT NOT NULL DEFAULT '',missing INTEGER NOT NULL DEFAULT 0);"
  "CREATE UNIQUE INDEX assets_virtual_path ON assets(virtual_path);"
  "CREATE INDEX assets_group ON assets(group_path);"
  "CREATE INDEX assets_identity ON assets(identity,content_hash);"
  "CREATE TABLE asset_revisions(asset_id TEXT NOT NULL REFERENCES assets(id),"
  "revision INTEGER NOT NULL CHECK(revision>0),display_name TEXT NOT NULL,"
  "virtual_path TEXT NOT NULL,recipe TEXT NOT NULL,content_hash TEXT NOT NULL,"
  "content TEXT NOT NULL REFERENCES content(id),"
  "original TEXT NOT NULL REFERENCES content(id),size INTEGER NOT NULL,"
  "recorded_at TEXT NOT NULL,PRIMARY KEY(asset_id,revision));"
  "CREATE TABLE dependencies(asset_id TEXT NOT NULL REFERENCES assets(id),"
  "dependency_id TEXT NOT NULL REFERENCES assets(id),kind TEXT NOT NULL,"
  "PRIMARY KEY(asset_id,dependency_id,kind),CHECK(asset_id<>dependency_id));";

void configure(detail::Database& database)
{
  detail::execute(
    database.get(),
    "PRAGMA foreign_keys=ON; PRAGMA journal_mode=DELETE; "
    "PRAGMA synchronous=FULL;");
}

std::string nowText(sqlite3* db)
{
  auto time = detail::Statement{db, "SELECT strftime('%Y-%m-%dT%H:%M:%fZ','now')"};
  time.next();
  return time.text(0);
}

} // namespace

const char* AssetLibrary::indexFileName()
{
  return IndexFileName;
}

std::filesystem::path AssetLibrary::indexPath(const std::filesystem::path& folder)
{
  return folder / std::filesystem::path{IndexFileName};
}

bool AssetLibrary::isLibraryFolder(const std::filesystem::path& folder)
{
  auto error = std::error_code{};
  return std::filesystem::is_regular_file(indexPath(folder), error);
}

const char* librarySourceKindName(const AssetType type)
{
  return assetTypeName(type);
}

class AssetLibrary::Impl
{
public:
  std::filesystem::path folder;
  detail::Database database;
  LibraryInfo info;

  Impl(std::filesystem::path libraryFolder, const bool create)
    : folder{std::move(libraryFolder)}
    , database{indexPath(folder), create}
  {
  }
};

AssetLibrary::AssetLibrary(std::unique_ptr<Impl> impl)
  : m_impl{std::move(impl)}
{
}
AssetLibrary::~AssetLibrary() = default;
AssetLibrary::AssetLibrary(AssetLibrary&&) noexcept = default;
AssetLibrary& AssetLibrary::operator=(AssetLibrary&&) noexcept = default;

Result<std::unique_ptr<AssetLibrary>> AssetLibrary::create(
  const std::filesystem::path& folder, const std::string& name)
{
  return detail::attempt([&] {
    detail::validateText(name, "library name");
    auto error = std::error_code{};
    if (!std::filesystem::is_directory(folder, error))
      throw detail::StoreError{"Asset library folder does not exist."};
    const auto index = indexPath(folder);
    if (std::filesystem::exists(index, error))
      throw detail::StoreError{"This folder already contains an asset library."};
    auto reservation = detail::NewFile{index};
    auto impl = std::make_unique<Impl>(folder, false);
    auto* db = impl->database.get();
    detail::execute(
      db, "PRAGMA foreign_keys=ON; PRAGMA journal_mode=DELETE; PRAGMA synchronous=FULL;");
    auto transaction = detail::Transaction{db};
    detail::execute(db, Schema);
    const auto now = nowText(db);
    impl->info = {generateUuid(), name, pathText(folder), now, now};
    auto insert = detail::Statement{
      db, "INSERT INTO library(id,name,created_at,updated_at) VALUES(?,?,?,?)"};
    insert.bind(1, impl->info.id);
    insert.bind(2, impl->info.name);
    insert.bind(3, impl->info.createdAt);
    insert.bind(4, impl->info.updatedAt);
    insert.run();
    transaction.commit();
    reservation.keep();
    return std::unique_ptr<AssetLibrary>{new AssetLibrary{std::move(impl)}};
  });
}

Result<std::unique_ptr<AssetLibrary>> AssetLibrary::open(
  const std::filesystem::path& folder)
{
  return detail::attempt([&] {
    if (folder.empty())
      throw detail::StoreError{"Asset library path is empty."};
    if (!isLibraryFolder(folder))
      throw detail::StoreError{"Folder is not an asset library."};
    auto impl = std::make_unique<Impl>(folder, false);
    auto* db = impl->database.get();
    {
      auto version = detail::Statement{
        db, "SELECT * FROM pragma_application_id(),pragma_user_version()"};
      if (!version.next() || version.integer(0) != 0x54424153 || version.integer(1) != 1)
        throw detail::StoreError{"Not a supported TB asset library (format version 1)."};
    }
    auto info =
      detail::Statement{db, "SELECT id,name,created_at,updated_at FROM library"};
    if (!info.next())
      throw detail::StoreError{"Asset library metadata is missing."};
    impl->info = {
      info.text(0), info.text(1), pathText(folder), info.text(2), info.text(3)};
    detail::validateText(impl->info.id, "library ID");
    detail::validateText(impl->info.name, "library name");
    if (info.next())
      throw detail::StoreError{"Duplicate asset library metadata."};
    {
      // Catalogs written before dependency capture existed keep working: the hints are
      // additive and simply resolve to nothing until the next scan.
      auto columns = detail::Statement{
        db,
        "SELECT count(*) FROM pragma_table_info('assets') WHERE name='dependency_hints'"};
      if (columns.next() && columns.integer(0) == 0)
        detail::execute(
          db, "ALTER TABLE assets ADD COLUMN dependency_hints TEXT NOT NULL DEFAULT ''");
    }
    // Fail closed on missing tables or columns.
    auto probe = detail::Statement{db, "SELECT id FROM assets LIMIT 1"};
    while (probe.next())
      ;
    auto probeSources =
      detail::Statement{db, "SELECT relative_path FROM sources LIMIT 1"};
    while (probeSources.next())
      ;
    configure(impl->database);
    return std::unique_ptr<AssetLibrary>{new AssetLibrary{std::move(impl)}};
  });
}

const std::filesystem::path& AssetLibrary::folder() const
{
  return m_impl->folder;
}

const LibraryInfo& AssetLibrary::info() const
{
  return m_impl->info;
}

namespace
{

struct IndexedAsset
{
  std::string id;
  std::string virtualPath;
  std::string sourceRelPath;
  std::string sourceMember;
  std::string identity;
  std::string contentHash;
  std::uint64_t revision = 0;
  bool missing = false;
  bool claimed = false;
};

std::unordered_map<std::string, IndexedAsset> loadIndexedAssets(sqlite3* db)
{
  auto query = detail::Statement{
    db,
    "SELECT id,virtual_path,source_rel_path,source_member,identity,content_hash,"
    "revision,missing FROM assets"};
  auto result = std::unordered_map<std::string, IndexedAsset>{};
  while (query.next())
  {
    auto asset = IndexedAsset{
      query.text(0),
      query.text(1),
      query.text(2),
      query.text(3),
      query.text(4),
      query.text(5),
      query.integer(6),
      query.flag(7) != 0};
    result.emplace(asset.id, std::move(asset));
  }
  return result;
}

void writeRevisionRow(
  sqlite3* db,
  const std::string& assetId,
  const LibraryAsset& asset,
  const std::string& contentRow,
  const std::string& originalRow)
{
  auto insert = detail::Statement{
    db,
    "INSERT INTO asset_revisions(asset_id,revision,display_name,virtual_path,recipe,"
    "content_hash,content,original,size,recorded_at) VALUES(?,?,?,?,?,?,?,?,?,?)"};
  insert.bind(1, assetId);
  insert.bind(2, asset.revision);
  insert.bind(3, asset.displayName);
  insert.bind(4, asset.virtualPath);
  insert.bind(5, asset.recipe);
  insert.bind(6, asset.contentHash);
  insert.bind(7, contentRow);
  insert.bind(8, originalRow);
  insert.bind(9, asset.size);
  insert.bind(10, nowText(db));
  insert.run();
}

/**
 * Resolves the dependency hints recorded during discovery into real catalog edges.
 *
 * Hints are matched by normalized file/member stem, which is the only identity a model
 * skin name and a library file share. Edge kinds produced here are rebuilt from scratch
 * on every scan; edges added by hand with any other kind survive.
 */
void resolveDependencies(sqlite3* db)
{
  auto keys = std::unordered_map<std::string, std::string>{};
  {
    auto query =
      detail::Statement{db, "SELECT id,identity FROM assets ORDER BY virtual_path"};
    while (query.next())
      keys.emplace(modelDependencyKey(query.text(1)), query.text(0));
  }

  detail::execute(
    db,
    "DELETE FROM dependencies WHERE kind IN "
    "('model-skin','model-companion','model-animation','skybox-face')");

  auto hints = detail::Statement{
    db, "SELECT id,dependency_hints FROM assets WHERE dependency_hints<>''"};
  auto insert = detail::Statement{
    db,
    "INSERT INTO dependencies(asset_id,dependency_id,kind) VALUES(?,?,?) ON CONFLICT DO "
    "NOTHING"};
  while (hints.next())
  {
    const auto assetId = hints.text(0);
    const auto text = hints.text(1);
    auto start = std::size_t{0};
    while (start < text.size())
    {
      const auto end = text.find('\n', start);
      const auto line = text.substr(start, end == std::string::npos ? end : end - start);
      start = end == std::string::npos ? text.size() : end + 1;
      const auto colon = line.find(':');
      if (colon == std::string::npos)
        continue;
      const auto target = keys.find(line.substr(colon + 1));
      if (target == keys.end() || target->second == assetId)
        continue;
      insert.reset();
      insert.bind(1, assetId);
      insert.bind(2, target->second);
      insert.bind(3, line.substr(0, colon));
      insert.run();
    }
  }
}

} // namespace

Result<LibraryScan> AssetLibrary::scan()
{
  return detail::attempt([&] {
    auto* db = m_impl->database.get();
    const auto discovery = discover(m_impl->folder, m_impl->info.id);
    auto result = LibraryScan{};
    result.invalidEntries = discovery.invalid;
    if (!discovery.warnings.empty())
    {
      result.warning = discovery.warnings.front();
      if (discovery.warnings.size() > 1)
        result.warning += fmt::format(" (and {} more)", discovery.warnings.size() - 1);
    }

    auto transaction = detail::Transaction{db};
    auto indexed = loadIndexedAssets(db);
    // A row may only be relocated when its own location is gone. Without this, a WAD
    // whose bytes happen to match another WAD could steal the row of an asset that is
    // still present at its recorded path.
    auto discoveredIds = std::unordered_set<std::string>{};
    discoveredIds.reserve(discovery.assets.size());
    for (const auto& discovered : discovery.assets)
      discoveredIds.insert(discovered.id);

    auto claimed = std::unordered_set<std::string>{};
    for (const auto& discovered : discovery.assets)
    {
      // Reading content back for an unchanged asset would compare the whole payload
      // against stored blobs, so the row is only materialized when it is needed.
      auto contentRow = std::optional<std::string>{};
      const auto contentRowFor = [&]() -> const std::string& {
        if (!contentRow)
          contentRow = detail::storeContent(db, discovered.content);
        return *contentRow;
      };
      const auto existing = indexed.find(discovered.id);
      if (existing != indexed.end())
      {
        auto& asset = existing->second;
        claimed.insert(asset.id);
        if (asset.contentHash != discovered.contentHash)
        {
          auto row = LibraryAsset{
            asset.id,
            discovered.type,
            discovered.displayName,
            discovered.virtualPath,
            discovered.group,
            discovered.sourceRelPath,
            discovered.sourceMember,
            discovered.nativeFormat,
            discovered.recipe,
            asset.revision + 1,
            discovered.contentHash,
            discovered.size,
            false};
          const auto& stored = contentRowFor();
          auto update = detail::Statement{
            db,
            "UPDATE assets SET type=?,display_name=?,virtual_path=?,group_path=?,"
            "source_rel_path=?,source_member=?,native_format=?,identity=?,recipe=?,"
            "revision=?,content_hash=?,content=?,original=?,size=?,dependency_hints=?,"
            "missing=0 WHERE id=?"};
          update.bind(1, assetTypeName(row.type));
          update.bind(2, row.displayName);
          update.bind(3, row.virtualPath);
          update.bind(4, row.group);
          update.bind(5, row.sourceContainer);
          update.bind(6, row.sourceMember);
          update.bind(7, row.nativeFormat);
          update.bind(8, discovered.identity);
          update.bind(9, row.recipe);
          update.bind(10, row.revision);
          update.bind(11, row.contentHash);
          update.bind(12, stored);
          update.bind(13, stored);
          update.bind(14, row.size);
          update.bind(15, joinedHints(discovered.hints));
          update.bind(16, row.id);
          update.run();
          writeRevisionRow(db, row.id, row, stored, stored);
          ++result.updated;
        }
        else if (asset.missing)
        {
          auto update = detail::Statement{db, "UPDATE assets SET missing=0 WHERE id=?"};
          update.bind(1, asset.id);
          update.run();
          ++result.restored;
        }
        continue;
      }

      // The natural key of an indexed asset can disappear because the user renamed or
      // moved the source. Identical bytes under the same member or file name keep the
      // identity, so the project link survives a library-side reorganization without
      // pretending the content changed.
      auto relocated = std::optional<IndexedAsset*>{};
      for (auto& [id, asset] : indexed)
      {
        if (asset.claimed || claimed.contains(id) || discoveredIds.contains(id))
          continue;
        if (
          asset.contentHash == discovered.contentHash
          && asset.identity == discovered.identity)
        {
          relocated = &asset;
          break;
        }
      }
      auto row = LibraryAsset{
        relocated ? (*relocated)->id : discovered.id,
        discovered.type,
        discovered.displayName,
        discovered.virtualPath,
        discovered.group,
        discovered.sourceRelPath,
        discovered.sourceMember,
        discovered.nativeFormat,
        discovered.recipe,
        relocated ? (*relocated)->revision : 1,
        discovered.contentHash,
        discovered.size,
        false};
      if (relocated)
      {
        auto update = detail::Statement{
          db,
          "UPDATE assets SET "
          "display_name=?,virtual_path=?,group_path=?,source_rel_path=?,"
          "source_member=?,native_format=?,recipe=?,dependency_hints=?,missing=0 WHERE "
          "id=?"};
        update.bind(1, row.displayName);
        update.bind(2, row.virtualPath);
        update.bind(3, row.group);
        update.bind(4, row.sourceContainer);
        update.bind(5, row.sourceMember);
        update.bind(6, row.nativeFormat);
        update.bind(7, row.recipe);
        update.bind(8, joinedHints(discovered.hints));
        update.bind(9, row.id);
        update.run();
        auto revision = detail::Statement{
          db,
          "UPDATE asset_revisions SET display_name=?,virtual_path=? "
          "WHERE asset_id=? AND revision=?"};
        revision.bind(1, row.displayName);
        revision.bind(2, row.virtualPath);
        revision.bind(3, row.id);
        revision.bind(4, row.revision);
        revision.run();
        (*relocated)->claimed = true;
        claimed.insert(row.id);
        ++result.relocated;
        continue;
      }

      const auto& stored = contentRowFor();
      auto insert = detail::Statement{
        db,
        "INSERT INTO assets(id,type,display_name,virtual_path,group_path,source_rel_path,"
        "source_member,native_format,identity,recipe,revision,content_hash,content,"
        "original,size,dependency_hints,missing) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,0)"};
      insert.bind(1, row.id);
      insert.bind(2, assetTypeName(row.type));
      insert.bind(3, row.displayName);
      insert.bind(4, row.virtualPath);
      insert.bind(5, row.group);
      insert.bind(6, row.sourceContainer);
      insert.bind(7, row.sourceMember);
      insert.bind(8, row.nativeFormat);
      insert.bind(9, discovered.identity);
      insert.bind(10, row.recipe);
      insert.bind(11, row.revision);
      insert.bind(12, row.contentHash);
      insert.bind(13, stored);
      insert.bind(14, stored);
      insert.bind(15, row.size);
      insert.bind(16, joinedHints(discovered.hints));
      insert.run();
      writeRevisionRow(db, row.id, row, stored, stored);
      claimed.insert(row.id);
      ++result.added;
    }

    for (auto& [id, asset] : indexed)
    {
      if (claimed.contains(id) || asset.claimed)
        continue;
      if (!asset.missing)
      {
        auto update = detail::Statement{db, "UPDATE assets SET missing=1 WHERE id=?"};
        update.bind(1, id);
        update.run();
      }
      ++result.missing;
    }

    auto existingSources = std::unordered_set<std::string>{};
    {
      auto query = detail::Statement{db, "SELECT relative_path FROM sources"};
      while (query.next())
        existingSources.insert(query.text(0));
    }
    auto discoveredSources = std::unordered_set<std::string>{};
    for (const auto& source : discovery.sources)
    {
      discoveredSources.insert(source.relativePath);
      auto upsert = detail::Statement{
        db,
        "INSERT INTO sources(relative_path,type,content_hash,size,asset_count,missing) "
        "VALUES(?,?,?,?,?,0) ON CONFLICT(relative_path) DO UPDATE SET "
        "type=excluded.type,content_hash=excluded.content_hash,size=excluded.size,"
        "asset_count=excluded.asset_count,missing=0"};
      upsert.bind(1, source.relativePath);
      upsert.bind(2, assetTypeName(source.type));
      upsert.bind(3, source.contentHash);
      upsert.bind(4, source.size);
      upsert.bind(5, source.assetCount);
      upsert.run();
      ++result.sources;
    }
    for (const auto& relativePath : existingSources)
    {
      if (discoveredSources.contains(relativePath))
        continue;
      auto update =
        detail::Statement{db, "UPDATE sources SET missing=1 WHERE relative_path=?"};
      update.bind(1, relativePath);
      update.run();
    }

    resolveDependencies(db);

    auto update = detail::Statement{db, "UPDATE library SET updated_at=?"};
    update.bind(1, nowText(db));
    update.run();
    transaction.commit();
    return result;
  });
}

Result<std::vector<LibraryAsset>> AssetLibrary::assets(
  const std::optional<AssetType> type, const std::string& group) const
{
  return detail::attempt([&] {
    detail::validatePath(group, true);
    const auto sql = fmt::format(
      "SELECT {} FROM assets a {} ORDER BY a.virtual_path",
      AssetColumns,
      group.empty() ? "" : "WHERE a.group_path=?");
    auto query = detail::Statement{m_impl->database.get(), sql.c_str()};
    if (!group.empty())
      query.bind(1, group);
    auto result = std::vector<LibraryAsset>{};
    while (query.next())
    {
      auto asset = readAssetRow(query);
      if (!type || asset.type == *type)
        result.push_back(std::move(asset));
    }
    return result;
  });
}

Result<std::vector<LibrarySource>> AssetLibrary::sources() const
{
  return detail::attempt([&] {
    auto query = detail::Statement{
      m_impl->database.get(),
      "SELECT relative_path,type,content_hash,size,asset_count,missing FROM sources "
      "ORDER BY relative_path"};
    auto result = std::vector<LibrarySource>{};
    while (query.next())
    {
      const auto type = parseAssetType(query.text(1));
      if (type.is_error())
        throw detail::StoreError{std::get<Error>(type.error()).msg};
      result.push_back(
        {query.text(0),
         type.value(),
         query.text(2),
         query.integer(3),
         query.integer(4),
         query.flag(5) != 0});
    }
    return result;
  });
}

Result<std::vector<std::string>> AssetLibrary::groups() const
{
  return detail::attempt([&] {
    auto query = detail::Statement{
      m_impl->database.get(),
      "SELECT DISTINCT group_path FROM assets ORDER BY group_path"};
    auto result = std::vector<std::string>{};
    while (query.next())
      result.push_back(query.text(0));
    return result;
  });
}

Result<std::optional<LibraryAsset>> AssetLibrary::findAsset(const std::string& id) const
{
  return detail::attempt([&]() -> std::optional<LibraryAsset> {
    const auto sql = fmt::format("SELECT {} FROM assets a WHERE a.id=?", AssetColumns);
    auto query = detail::Statement{m_impl->database.get(), sql.c_str()};
    query.bind(1, id);
    return query.next() ? std::optional{readAssetRow(query)} : std::nullopt;
  });
}

Result<std::vector<LibraryAsset>> AssetLibrary::revisions(const std::string& id) const
{
  return detail::attempt([&] {
    auto current = detail::Statement{
      m_impl->database.get(),
      "SELECT type,group_path,source_rel_path,source_member,native_format,revision FROM "
      "assets WHERE id=?"};
    current.bind(1, id);
    if (!current.next())
      throw detail::StoreError{"Library asset not found."};
    const auto type = parseAssetType(current.text(0));
    if (type.is_error())
      throw detail::StoreError{std::get<Error>(type.error()).msg};
    const auto head = current.integer(5);
    const auto group = current.text(1);
    const auto container = current.text(2);
    const auto member = current.text(3);
    const auto nativeFormat = current.text(4);

    auto query = detail::Statement{
      m_impl->database.get(),
      "SELECT revision,display_name,virtual_path,recipe,content_hash,size FROM "
      "asset_revisions WHERE asset_id=? ORDER BY revision"};
    query.bind(1, id);
    auto result = std::vector<LibraryAsset>{};
    while (query.next())
    {
      const auto revision = query.integer(0);
      result.push_back(
        {id,
         type.value(),
         query.text(1),
         query.text(2),
         group,
         container,
         member,
         nativeFormat,
         query.text(3),
         revision,
         query.text(4),
         query.integer(5),
         revision != head});
    }
    return result;
  });
}

namespace
{

// Reads a content column for the current revision from `assets`, or for a historical
// revision from `asset_revisions`. Both tables carry the same content references, so
// only the join target differs.
Result<std::vector<std::uint8_t>> readContentColumn(
  sqlite3* db,
  const std::string& id,
  const std::optional<std::uint64_t> revision,
  const char* column)
{
  return detail::attempt([&] {
    const auto sql = revision
                       ? fmt::format(
                           "SELECT c.data FROM asset_revisions t JOIN content c ON "
                           "c.id=t.{} WHERE t.asset_id=? AND t.revision=?",
                           column)
                       : fmt::format(
                           "SELECT c.data FROM assets t JOIN content c ON c.id=t.{} "
                           "WHERE t.id=?",
                           column);
    auto query = detail::Statement{db, sql.c_str()};
    query.bind(1, id);
    if (revision)
      query.bind(2, *revision);
    if (!query.next())
      throw detail::StoreError{"Library asset content not found."};
    return query.bytes(0);
  });
}

} // namespace

Result<std::vector<std::uint8_t>> AssetLibrary::readContent(
  const std::string& id, const std::optional<std::uint64_t> revision) const
{
  return readContentColumn(m_impl->database.get(), id, revision, "content");
}

Result<std::vector<std::uint8_t>> AssetLibrary::readOriginal(
  const std::string& id, const std::optional<std::uint64_t> revision) const
{
  return readContentColumn(m_impl->database.get(), id, revision, "original");
}

Result<void> AssetLibrary::addDependency(
  const std::string& assetId, const std::string& dependencyId, const std::string& kind)
{
  return detail::attempt([&] {
    detail::validateText(kind, "dependency kind");
    auto* db = m_impl->database.get();
    auto transaction = detail::Transaction{db};
    auto exists = detail::Statement{db, "SELECT 1 FROM assets WHERE id=?"};
    exists.bind(1, assetId);
    if (!exists.next())
      throw detail::StoreError{"Library asset not found."};
    auto target = detail::Statement{db, "SELECT 1 FROM assets WHERE id=?"};
    target.bind(1, dependencyId);
    if (!target.next())
      throw detail::StoreError{"Library dependency not found."};
    auto insert = detail::Statement{
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

Result<std::vector<AssetDependency>> AssetLibrary::dependencies(
  const std::string& assetId) const
{
  return detail::attempt([&] {
    auto query = detail::Statement{
      m_impl->database.get(),
      "SELECT asset_id,dependency_id,kind FROM dependencies WHERE asset_id=? ORDER BY "
      "dependency_id,kind"};
    query.bind(1, assetId);
    auto result = std::vector<AssetDependency>{};
    while (query.next())
      result.push_back({query.text(0), query.text(1), query.text(2)});
    return result;
  });
}

Result<std::vector<AssetDependency>> AssetLibrary::dependents(
  const std::string& dependencyId) const
{
  return detail::attempt([&] {
    auto query = detail::Statement{
      m_impl->database.get(),
      "SELECT asset_id,dependency_id,kind FROM dependencies WHERE dependency_id=? ORDER "
      "BY asset_id,kind"};
    query.bind(1, dependencyId);
    auto result = std::vector<AssetDependency>{};
    while (query.next())
      result.push_back({query.text(0), query.text(1), query.text(2)});
    return result;
  });
}

} // namespace tb::project
