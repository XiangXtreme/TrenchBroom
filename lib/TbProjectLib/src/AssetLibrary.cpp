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

constexpr auto FolderSchema = "CREATE TABLE IF NOT EXISTS folders(path TEXT PRIMARY KEY)";

const char* folderRoot(const AssetType type)
{
  switch (type)
  {
  case AssetType::Map:
    return "maps";
  case AssetType::Texture:
    return "textures";
  case AssetType::Material:
    return "materials";
  case AssetType::Model:
    return "models";
  case AssetType::Sprite:
    return "sprites";
  case AssetType::Sound:
    return "sound";
  case AssetType::Skybox:
    return "skybox";
  case AssetType::Prefab:
    return "prefabs";
  }
  throw detail::StoreError{"Unknown asset type."};
}

bool containsFolder(const std::string& parent, const std::string& path)
{
  return path == parent || path.starts_with(parent + "/");
}

void validateFolder(const std::string& path)
{
  detail::validatePath(path);
  const auto slash = path.find('/');
  const auto root = path.substr(0, slash);
  if (
    slash == std::string::npos
    || std::ranges::none_of(
      std::array{
        "maps",
        "textures",
        "materials",
        "models",
        "sprites",
        "sound",
        "skybox",
        "prefabs"},
      [&](const auto* value) { return root == value; }))
    throw detail::StoreError{"Folders must be below an asset-type directory."};
}

void keepFolder(sqlite3* db, std::string path)
{
  while (path.find('/') != std::string::npos)
  {
    auto insert = detail::Statement{db, "INSERT OR IGNORE INTO folders(path) VALUES(?)"};
    insert.bind(1, path);
    insert.run();
    path.resize(path.rfind('/'));
  }
}

std::string pathText(const std::filesystem::path& path)
{
  const auto text = path.generic_u8string();
  return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::filesystem::path pathFromText(const std::string_view text)
{
  return std::filesystem::path{
    std::u8string{reinterpret_cast<const char8_t*>(text.data()), text.size()}};
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
  Prefab,
  Material,
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
  if (ext == ".tbprefab")
    return SourceFormat::Prefab;
  if (ext == ".tbmat")
    return SourceFormat::Material;
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
  case SourceFormat::Prefab:
    return AssetType::Prefab;
  case SourceFormat::Material:
    return AssetType::Material;
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
  case SourceFormat::Prefab:
    return "prefab";
  case SourceFormat::Material:
    return "goldsrc-terrain-material";
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
  case SourceFormat::Prefab:
    return R"({"importer":"native-copy","version":1,"nativeFormat":"prefab"})";
  case SourceFormat::Material:
    return R"({"importer":"terrain-material","version":1,"nativeFormat":"goldsrc-terrain-material"})";
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
  std::string contentRow;
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

// Discovery retains metadata only. Persist each payload while its source is in memory;
// the surrounding scan transaction rolls back both blobs and catalog on failure.
std::string storeDiscoveredContent(
  sqlite3* db,
  const std::vector<std::uint8_t>& bytes,
  const std::string& hash,
  const std::string& assetId)
{
  auto existing =
    detail::Statement{db, "SELECT original FROM assets WHERE id=? AND content_hash=?"};
  existing.bind(1, assetId);
  existing.bind(2, hash);
  if (existing.next())
    return existing.text(0);
  return detail::storeContent(db, bytes);
}

void discoverFile(
  sqlite3* db,
  const std::string& relativePath,
  const std::filesystem::path& file,
  const std::string& libraryId,
  Discovery& discovery,
  std::unordered_set<std::string>& seenIds,
  const std::optional<std::vector<std::uint8_t>>& supplied = std::nullopt)
{
  const auto format = sourceFormatFor(pathFromText(relativePath).extension());
  if (format == SourceFormat::Unknown)
    return;

  const auto bytesResult =
    supplied ? Result<std::vector<std::uint8_t>>{*supplied} : readFile(file);
  if (bytesResult.is_error())
  {
    throw detail::StoreError{relativePath + ": cannot read import file"};
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
      throw detail::StoreError{relativePath + ": " + std::get<Error>(wad.error()).msg};
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
      asset.size = std::uint64_t(entry.miptex.size());
      asset.contentHash = contentHash(entry.miptex);
      asset.contentRow =
        storeDiscoveredContent(db, entry.miptex, asset.contentHash, asset.id);
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
  asset.displayName = pathText(file.stem());
  if (asset.displayName.empty())
    asset.displayName = pathText(file.filename());
  asset.virtualPath = relativePath;
  asset.group = parentFolder(relativePath);
  asset.sourceRelPath = relativePath;
  asset.identity = lowered(pathText(file.filename()));
  asset.nativeFormat = nativeFormatFor(format);
  asset.recipe = recipeFor(format, relativePath);
  asset.size = std::uint64_t(bytes.size());
  asset.contentHash = source.contentHash;
  if (format == SourceFormat::Material)
  {
    const auto contentText =
      std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    // Extract "texture" fields from slots
    auto pos = size_t{0};
    auto slotIdx = 0;
    while (slotIdx < 3
           && (pos = contentText.find("\"texture\"", pos)) != std::string_view::npos)
    {
      pos += 9;
      const auto colon = contentText.find(':', pos);
      if (colon == std::string_view::npos)
        break;
      const auto quoteStart = contentText.find('"', colon + 1);
      if (quoteStart == std::string_view::npos)
        break;
      const auto quoteEnd = contentText.find('"', quoteStart + 1);
      if (quoteEnd == std::string_view::npos)
        break;
      const auto texName = contentText.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
      if (!texName.empty())
      {
        const auto texture = std::string{texName};
        // New material recipes store the exact library asset ID. Older recipes used
        // texture names; retain their existing resolution behavior when rescanning.
        asset.hints.push_back(fmt::format(
          "terrain-slot-{}:{}",
          slotIdx,
          detail::validAssetId(texture) ? "@" + texture : modelDependencyKey(texture)));
      }
      pos = quoteEnd + 1;
      ++slotIdx;
    }
  }
  if (format == SourceFormat::Model)
  {
    // A GoldSrc model embeds its textures; what lives beside it are the separate texture
    // file and the external animation groups. Names are derived the way the engine
    // derives them and only become edges when the file exists in the library.
    if (const auto model = readGoldSrcModelDependencies(bytes); model.is_success())
      for (const auto& group :
           modelAnimationGroupNames(relativePath, model.value().sequenceGroups))
        asset.hints.push_back("model-animation:/" + lowered(group));
    if (const auto companion = modelCompanionName(relativePath); !companion.empty())
      asset.hints.push_back("model-companion:/" + lowered(companion));
  }
  asset.contentRow = storeDiscoveredContent(db, bytes, asset.contentHash, asset.id);
  source.assetCount = 1;
  discovery.assets.push_back(std::move(asset));
}

/**
 * Groups the six faces of a GoldSrc skybox.
 *
 * A skybox is a logical asset: the faces stay independently editable textures, and the
 * group only records which six belong together so a capture can pull the whole set.
 */
void addSkyboxGroups(sqlite3* db, const std::string& libraryId, Discovery& discovery)
{
  static constexpr std::array<std::string_view, 6> Faces{
    "bk", "rt", "lf", "ft", "up", "dn"};
  struct FaceSet
  {
    std::string folder;
    std::string stem;
    std::map<std::string, std::string> faces;
  };

  auto groups = std::map<std::string, FaceSet>{};
  auto faces = discovery.assets;
  auto importedPaths = std::unordered_set<std::string>{};
  for (const auto& asset : discovery.assets)
    importedPaths.insert(asset.virtualPath);
  auto knownPaths = importedPaths;
  auto stored = detail::Statement{
    db,
    "SELECT virtual_path FROM assets WHERE native_format='image' OR "
    "native_format='goldsrc-wad'"};
  while (stored.next())
  {
    auto asset = Discovered{};
    asset.virtualPath = stored.text(0);
    asset.nativeFormat = "image";
    if (knownPaths.insert(asset.virtualPath).second)
      faces.push_back(std::move(asset));
  }
  for (const auto& asset : faces)
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
      group.faces.emplace(std::string{suffix}, asset.virtualPath);
      break;
    }
  }

  for (const auto& [key, group] : groups)
  {
    if (group.faces.size() != Faces.size())
      continue;
    const auto touched = std::ranges::any_of(
      group.faces, [&](const auto& face) { return importedPaths.contains(face.second); });
    if (!touched)
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
      manifest += fmt::format("{} {}\n", suffix, face);
      asset.hints.push_back("skybox-face:/" + lowered(face));
    }
    const auto bytes = std::vector<std::uint8_t>{manifest.begin(), manifest.end()};
    asset.size = std::uint64_t(bytes.size());
    asset.contentHash = contentHash(bytes);
    asset.contentRow = storeDiscoveredContent(db, bytes, asset.contentHash, asset.id);
    discovery.assets.push_back(std::move(asset));
  }
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
  // A skybox is a logical group whose source container is its parent folder, which may
  // be the library root. File-backed assets still require a non-empty source path.
  detail::validatePath(asset.sourceContainer, asset.type == AssetType::Skybox);
  detail::validateText(asset.displayName, "display name");
  if (asset.revision == 0)
    throw detail::StoreError{"Invalid library asset revision."};
  return asset;
}

constexpr auto Schema =
  "PRAGMA application_id=0x54424153; PRAGMA user_version=4;"
  "CREATE TABLE library(id TEXT PRIMARY KEY,name TEXT NOT NULL,"
  "created_at TEXT NOT NULL,updated_at TEXT NOT NULL);"
  "CREATE TABLE content(id TEXT PRIMARY KEY,hash TEXT NOT NULL,data BLOB NOT NULL);"
  "CREATE INDEX content_hash ON content(hash);"
  "CREATE TABLE sources(relative_path TEXT PRIMARY KEY,type TEXT NOT NULL,"
  "content_hash TEXT NOT NULL,size INTEGER NOT NULL,asset_count INTEGER NOT NULL,"
  "missing INTEGER NOT NULL DEFAULT 0);"
  "CREATE TABLE assets(id TEXT PRIMARY KEY,type TEXT NOT NULL,display_name TEXT NOT NULL,"
  "custom_display_name TEXT NOT NULL DEFAULT '',"
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
  "PRIMARY KEY(asset_id,dependency_id,kind),CHECK(asset_id<>dependency_id));"
  "CREATE TABLE asset_meta(asset_id TEXT PRIMARY KEY REFERENCES assets(id) ON DELETE "
  "CASCADE,favorite INTEGER NOT NULL DEFAULT 0,tags TEXT NOT NULL DEFAULT '');";


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

std::string libraryAssetFolder(const LibraryAsset& asset)
{
  const auto root = std::string{folderRoot(asset.type)};
  const auto slash = asset.group.find('/');
  const auto first = asset.group.substr(0, slash);
  static const auto aliases = std::map<std::string, std::string>{
    {"maps", "地图"},
    {"textures", "纹理"},
    {"materials", "材质"},
    {"models", "模型"},
    {"sprites", "精灵"},
    {"sound", "声音"},
    {"skybox", "天空盒"},
    {"prefabs", "预制件"}};
  if (lowered(first) == root || first == aliases.at(root))
    return slash == std::string::npos ? root : root + asset.group.substr(slash);
  return asset.group.empty() ? root : root + "/" + asset.group;
}

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
  const std::filesystem::path& folder,
  const std::string& name,
  LibraryScan* initialImport,
  const std::string& catalogId)
{
  return detail::attempt([&] {
    detail::validateText(name, "library name");
    if (!catalogId.empty())
      detail::validateAssetId(catalogId);
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
    {
      auto transaction = detail::Transaction{db};
      detail::execute(db, Schema);
      detail::execute(db, FolderSchema);
      const auto now = nowText(db);
      impl->info = {
        catalogId.empty() ? generateUuid() : catalogId, name, pathText(folder), now, now};
      auto insert = detail::Statement{
        db, "INSERT INTO library(id,name,created_at,updated_at) VALUES(?,?,?,?)"};
      insert.bind(1, impl->info.id);
      insert.bind(2, impl->info.name);
      insert.bind(3, impl->info.createdAt);
      insert.bind(4, impl->info.updatedAt);
      insert.run();
      transaction.commit();
    }
    auto library = std::unique_ptr<AssetLibrary>{new AssetLibrary{std::move(impl)}};
    if (initialImport)
    {
      auto imported = library->importDirectory(folder);
      if (imported.is_error())
      {
        const auto message = std::get<Error>(imported.error()).msg;
        // Close SQLite before the file reservation removes the failed catalog.
        library.reset();
        throw detail::StoreError{message};
      }
      *initialImport = std::move(imported).value();
    }
    reservation.keep();
    return library;
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
    auto adoptStoredContent = false;
    {
      auto version = detail::Statement{
        db, "SELECT * FROM pragma_application_id(),pragma_user_version()"};
      if (
        !version.next() || version.integer(0) != 0x54424153
        || (version.integer(1) != 1 && version.integer(1) != 2 && version.integer(1) != 3 && version.integer(1) != 4))
        throw detail::StoreError{"Not a supported TB asset library (format version 4)."};
      adoptStoredContent = version.integer(1) < 4;
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

      // Version 2 adds per asset metadata and a user supplied name that survives rescans.
      auto customName = detail::Statement{
        db,
        "SELECT count(*) FROM pragma_table_info('assets') WHERE "
        "name='custom_display_name'"};
      if (customName.next() && customName.integer(0) == 0)
        detail::execute(
          db,
          "ALTER TABLE assets ADD COLUMN custom_display_name TEXT NOT NULL DEFAULT ''");
      detail::execute(
        db,
        "CREATE TABLE IF NOT EXISTS asset_meta(asset_id TEXT PRIMARY KEY REFERENCES "
        "assets(id) ON DELETE CASCADE,favorite INTEGER NOT NULL DEFAULT 0,"
        "tags TEXT NOT NULL DEFAULT '')");
    }
    if (adoptStoredContent)
    {
      auto transaction = detail::Transaction{db};
      detail::execute(
        db,
        "DROP TABLE IF EXISTS removed_assets; UPDATE assets SET missing=0; UPDATE "
        "sources SET missing=0; PRAGMA user_version=4");
      transaction.commit();
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
    // Additive metadata, like asset_meta: old catalogs acquire empty-folder storage.
    detail::execute(db, FolderSchema);
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
  std::string contentHash;
  std::uint64_t revision = 0;
  bool missing = false;
};

std::unordered_map<std::string, IndexedAsset> loadIndexedAssets(sqlite3* db)
{
  auto query =
    detail::Statement{db, "SELECT id,content_hash,revision,missing FROM assets"};
  auto result = std::unordered_map<std::string, IndexedAsset>{};
  while (query.next())
  {
    auto asset =
      IndexedAsset{query.text(0), query.text(1), query.integer(2), query.flag(3) != 0};
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

  // Libraries keep no history: the newest content replaces every earlier revision.
  auto prune =
    detail::Statement{db, "DELETE FROM asset_revisions WHERE asset_id=? AND revision<>?"};
  prune.bind(1, assetId);
  prune.bind(2, asset.revision);
  prune.run();
}

/**
 * Resolves the dependency hints recorded during discovery into real catalog edges.
 *
 * Model and skybox hints match full library paths; legacy texture-name hints only
 * resolve when unique. Edge kinds produced here are rebuilt from scratch
 * on every scan; edges added by hand with any other kind survive.
 */
void resolveDependencies(sqlite3* db)
{
  auto keys = std::unordered_map<std::string, std::string>{};
  auto textureIds = std::unordered_set<std::string>{};
  auto paths = std::unordered_map<std::string, std::string>{};
  {
    auto query = detail::Statement{
      db, "SELECT id,identity,type,virtual_path FROM assets ORDER BY virtual_path"};
    while (query.next())
    {
      paths.emplace("/" + lowered(query.text(3)), query.text(0));
      if (query.text(2) == "texture")
      {
        textureIds.insert(query.text(0));
        const auto [it, inserted] =
          keys.emplace(modelDependencyKey(query.text(1)), query.text(0));
        if (!inserted)
          it->second.clear(); // Ambiguous legacy names must not select the first row.
      }
    }
  }

  detail::execute(
    db,
    "DELETE FROM dependencies WHERE kind IN "
    "('model-skin','model-companion','model-animation','skybox-face',"
    "'material-slot-0','material-slot-1','material-slot-2',"
    "'terrain-slot-0','terrain-slot-1','terrain-slot-2')");

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
      const auto key = line.substr(colon + 1);
      auto targetId = std::string{};
      if (key.starts_with('@'))
      {
        targetId = key.substr(1);
        if (!textureIds.contains(targetId))
          continue;
      }
      else if (key.starts_with('/'))
      {
        if (const auto target = paths.find(key); target != paths.end())
          targetId = target->second;
      }
      else if (const auto target = keys.find(key); target != keys.end())
      {
        targetId = target->second;
      }
      if (targetId.empty() || targetId == assetId)
        continue;
      insert.reset();
      insert.bind(1, assetId);
      insert.bind(2, targetId);
      insert.bind(3, line.substr(0, colon));
      insert.run();
    }
  }
}

} // namespace

Result<LibraryScan> AssetLibrary::importDirectory(
  const std::filesystem::path& sourceFolder)
{
  return detail::attempt([&]() -> LibraryScan {
    auto files = std::vector<ImportFile>{};
    for (const auto& entry : std::filesystem::recursive_directory_iterator{sourceFolder})
    {
      if (
        entry.is_regular_file()
        && sourceFormatFor(entry.path().extension()) != SourceFormat::Unknown)
        files.push_back(
          {entry.path(),
           pathText(std::filesystem::relative(entry.path(), sourceFolder)),
           std::nullopt});
    }
    auto result = importFiles(files, true);
    if (result.is_error())
      throw detail::StoreError{std::get<Error>(result.error()).msg};
    return std::move(result).value();
  });
}

Result<LibraryScan> AssetLibrary::importFiles(
  const std::vector<ImportFile>& files, const bool overwrite)
{
  return detail::attempt([&] {
    auto* db = m_impl->database.get();
    auto transaction = detail::Transaction{db};
    auto discovery = Discovery{};
    auto seen = std::unordered_set<std::string>{};
    for (const auto& file : files)
    {
      detail::validatePath(file.virtualPath);
      if (
        sourceFormatFor(pathFromText(file.virtualPath).extension())
        == SourceFormat::Unknown)
        throw detail::StoreError{"Unsupported asset format."};
      auto conflict = detail::Statement{
        db, "SELECT 1 FROM assets WHERE source_rel_path=? OR virtual_path=?"};
      conflict.bind(1, file.virtualPath);
      conflict.bind(2, file.virtualPath);
      if (conflict.next() && !overwrite)
        throw detail::StoreError{"An asset already uses this import path."};
      discoverFile(
        db, file.virtualPath, file.source, m_impl->info.id, discovery, seen, file.bytes);
    }
    addSkyboxGroups(db, m_impl->info.id, discovery);
    auto result = LibraryScan{};
    result.invalidEntries = discovery.invalid;
    if (!discovery.warnings.empty())
    {
      result.warning = discovery.warnings.front();
      if (discovery.warnings.size() > 1)
        result.warning += fmt::format(" (and {} more)", discovery.warnings.size() - 1);
    }

    auto indexed = loadIndexedAssets(db);
    for (const auto& discovered : discovery.assets)
    {
      const auto existing = indexed.find(discovered.id);
      if (existing != indexed.end())
      {
        auto& asset = existing->second;
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
          const auto& stored = discovered.contentRow;
          auto update = detail::Statement{
            db,
            "UPDATE assets SET type=?,display_name=CASE WHEN custom_display_name<>'' "
            "THEN "
            "custom_display_name ELSE ? END,virtual_path=?,group_path=group_path,"
            "source_rel_path=?,source_member=?,native_format=?,identity=?,recipe=?,"
            "revision=?,content_hash=?,content=?,original=?,size=?,dependency_hints=?,"
            "missing=0 WHERE id=?"};
          update.bind(1, assetTypeName(row.type));
          update.bind(2, row.displayName);
          update.bind(3, row.virtualPath);
          update.bind(4, row.sourceContainer);
          update.bind(5, row.sourceMember);
          update.bind(6, row.nativeFormat);
          update.bind(7, discovered.identity);
          update.bind(8, row.recipe);
          update.bind(9, row.revision);
          update.bind(10, row.contentHash);
          update.bind(11, stored);
          update.bind(12, stored);
          update.bind(13, row.size);
          update.bind(14, joinedHints(discovered.hints));
          update.bind(15, row.id);
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
        // Discovery rules may change without source bytes changing (for example,
        // replacing basename dependencies with full paths). Refresh derived hints.
        auto hints =
          detail::Statement{db, "UPDATE assets SET dependency_hints=? WHERE id=?"};
        hints.bind(1, joinedHints(discovered.hints));
        hints.bind(2, asset.id);
        hints.run();
        continue;
      }

      auto row = LibraryAsset{
        discovered.id,
        discovered.type,
        discovered.displayName,
        discovered.virtualPath,
        discovered.group,
        discovered.sourceRelPath,
        discovered.sourceMember,
        discovered.nativeFormat,
        discovered.recipe,
        1,
        discovered.contentHash,
        discovered.size,
        false};
      const auto& stored = discovered.contentRow;
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
      ++result.added;
    }

    for (const auto& source : discovery.sources)
    {
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
    resolveDependencies(db);

    auto update = detail::Statement{db, "UPDATE library SET updated_at=?"};
    update.bind(1, nowText(db));
    update.run();
    transaction.commit();
    return result;
  });
}

Result<std::vector<std::string>> AssetLibrary::folders() const
{
  return detail::attempt([&] {
    auto paths = std::set<std::string>{
      "maps", "textures", "materials", "models", "sprites", "sound", "skybox", "prefabs"};
    auto add = [&](std::string path) {
      while (!path.empty())
      {
        paths.insert(path);
        const auto slash = path.rfind('/');
        if (slash == std::string::npos)
          break;
        path.resize(slash);
      }
    };
    auto query = detail::Statement{m_impl->database.get(), "SELECT path FROM folders"};
    while (query.next())
      add(query.text(0));
    auto groups = detail::Statement{
      m_impl->database.get(), "SELECT DISTINCT type,group_path FROM assets"};
    while (groups.next())
    {
      auto asset = LibraryAsset{};
      const auto type = parseAssetType(groups.text(0));
      if (type.is_error())
        throw detail::StoreError{"Unknown asset type."};
      asset.type = type.value();
      asset.group = groups.text(1);
      add(libraryAssetFolder(asset));
    }
    return std::vector<std::string>{paths.begin(), paths.end()};
  });
}

Result<void> AssetLibrary::createFolder(const std::string& path)
{
  return detail::attempt([&] {
    validateFolder(path);
    auto* db = m_impl->database.get();
    auto transaction = detail::Transaction{db};
    const auto existing = folders();
    if (existing.is_error())
      throw detail::StoreError{std::get<Error>(existing.error()).msg};
    if (std::ranges::any_of(existing.value(), [&](const auto& folder) {
          return lowered(folder) == lowered(path);
        }))
      throw detail::StoreError{"A folder with that name already exists."};
    keepFolder(db, path);
    transaction.commit();
  });
}

Result<void> AssetLibrary::renameFolder(
  const std::string& path, const std::string& newPath)
{
  return detail::attempt([&] {
    validateFolder(path);
    validateFolder(newPath);
    if (path.substr(0, path.find('/')) != newPath.substr(0, newPath.find('/')))
      throw detail::StoreError{"Folders must stay below the same asset-type directory."};
    if (path == newPath)
      return;
    if (
      containsFolder(lowered(path), lowered(newPath))
      && lowered(path) != lowered(newPath))
      throw detail::StoreError{"Cannot move a folder into itself."};
    auto* db = m_impl->database.get();
    auto transaction = detail::Transaction{db};
    const auto existing = folders();
    const auto stored = assets();
    if (existing.is_error() || stored.is_error())
      throw detail::StoreError{"Cannot read asset folders."};
    if (std::ranges::find(existing.value(), path) == existing.value().end())
      throw detail::StoreError{"Folder not found."};
    if (std::ranges::any_of(existing.value(), [&](const auto& folder) {
          return folder != path && lowered(folder) == lowered(newPath);
        }))
      throw detail::StoreError{"A folder with that name already exists."};
    auto changes = std::map<std::string, std::string>{};
    for (const auto& asset : stored.value())
      if (const auto folder = libraryAssetFolder(asset); containsFolder(path, folder))
        changes.emplace(asset.id, newPath + folder.substr(path.size()));
    auto update = detail::Statement{db, "UPDATE assets SET group_path=? WHERE id=?"};
    for (const auto& [id, folder] : changes)
    {
      update.reset();
      update.bind(1, folder);
      update.bind(2, id);
      update.run();
    }
    for (const auto& folder : existing.value())
      if (containsFolder(path, folder))
      {
        auto remove = detail::Statement{db, "DELETE FROM folders WHERE path=?"};
        remove.bind(1, folder);
        remove.run();
      }
    for (const auto& folder : existing.value())
      if (containsFolder(path, folder))
        keepFolder(db, newPath + folder.substr(path.size()));
    keepFolder(db, path.substr(0, path.rfind('/')));
    transaction.commit();
  });
}

Result<void> AssetLibrary::removeFolder(const std::string& path)
{
  return detail::attempt([&] {
    validateFolder(path);
    auto* db = m_impl->database.get();
    auto transaction = detail::Transaction{db};
    const auto existing = folders();
    const auto stored = assets();
    if (existing.is_error() || stored.is_error())
      throw detail::StoreError{"Cannot read asset folders."};
    if (std::ranges::find(existing.value(), path) == existing.value().end())
      throw detail::StoreError{"Folder not found."};
    if (std::ranges::any_of(stored.value(), [&](const auto& asset) {
          return containsFolder(path, libraryAssetFolder(asset));
        }))
      throw detail::StoreError{
        "This folder contains assets. Move them before deleting the folder."};
    for (const auto& folder : existing.value())
      if (containsFolder(path, folder))
      {
        auto remove = detail::Statement{db, "DELETE FROM folders WHERE path=?"};
        remove.bind(1, folder);
        remove.run();
      }
    keepFolder(db, path.substr(0, path.rfind('/')));
    transaction.commit();
  });
}

Result<void> AssetLibrary::moveAssetsToFolder(
  const std::vector<std::string>& assetIds, const std::string& path)
{
  return detail::attempt([&] {
    detail::validatePath(path);
    auto* db = m_impl->database.get();
    auto transaction = detail::Transaction{db};
    const auto existing = folders();
    if (existing.is_error())
      throw detail::StoreError{std::get<Error>(existing.error()).msg};
    if (std::ranges::find(existing.value(), path) == existing.value().end())
      throw detail::StoreError{"Destination folder not found."};
    auto update = detail::Statement{db, "UPDATE assets SET group_path=? WHERE id=?"};
    for (const auto& id : assetIds)
    {
      const auto asset = findAsset(id);
      if (asset.is_error() || !asset.value())
        throw detail::StoreError{"Asset not found."};
      if (!containsFolder(folderRoot(asset.value()->type), path))
        throw detail::StoreError{
          "Assets must stay below their own asset-type directory."};
      keepFolder(db, libraryAssetFolder(*asset.value()));
      update.reset();
      update.bind(1, path);
      update.bind(2, id);
      update.run();
    }
    keepFolder(db, path);
    transaction.commit();
  });
}

Result<void> AssetLibrary::setGroup(
  const std::vector<std::string>& assetIds, const std::string& group)
{
  auto groups = std::map<std::string, std::string>{};
  for (const auto& id : assetIds)
    groups.emplace(id, group);
  return setGroups(groups);
}

Result<void> AssetLibrary::setGroups(const std::map<std::string, std::string>& groups)
{
  return detail::attempt([&] {
    auto* db = m_impl->database.get();
    auto transaction = detail::Transaction{db};
    for (const auto& [id, group] : groups)
    {
      detail::validatePath(group, true);
      const auto asset = findAsset(id);
      if (asset.is_error() || !asset.value())
        throw detail::StoreError{"Asset not found."};
      auto update = detail::Statement{db, "UPDATE assets SET group_path=? WHERE id=?"};
      update.bind(1, group);
      update.bind(2, id);
      update.run();
    }
    transaction.commit();
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

Result<LibraryAsset> AssetLibrary::publishCompatibleContent(
  const std::string& assetId,
  std::vector<std::uint8_t> compatible,
  const std::string& recipe)
{
  return detail::attempt([&] {
    auto* db = m_impl->database.get();
    auto transaction = detail::Transaction{db};
    auto current = detail::Statement{
      db,
      "SELECT type,native_format,display_name,virtual_path,content_hash,revision,"
      "original,group_path,source_rel_path,source_member FROM assets WHERE id=?"};
    current.bind(1, assetId);
    if (!current.next())
      throw detail::StoreError{"Library asset not found."};

    const auto type = parseAssetType(current.text(0));
    if (type.is_error())
      throw detail::StoreError{std::get<Error>(type.error()).msg};
    if (type.value() != AssetType::Texture)
      throw detail::StoreError{"Only texture images can be published as GoldSrc."};

    const auto nativeFormat = current.text(1);
    // Re-publishing a converted result is allowed so a failed conversion can be fixed
    // without deleting the asset; a native WAD or model must keep its own source.
    if (nativeFormat != "image" && nativeFormat != "goldsrc-wad")
      throw detail::StoreError{"This asset already has a binary GoldSrc source."};

    auto asset = LibraryAsset{
      assetId,
      type.value(),
      current.text(2),
      current.text(3),
      current.text(7),
      current.text(8),
      current.text(9),
      "goldsrc-wad",
      recipe,
      static_cast<std::uint64_t>(current.integer(5) + 1),
      current.text(4),
      static_cast<std::uint64_t>(compatible.size()),
      false};

    const auto originalRow = current.text(6);
    const auto contentRow = detail::storeContent(db, compatible);

    auto update = detail::Statement{
      db,
      "UPDATE assets SET native_format=?,recipe=?,revision=?,content=?,size=?,missing=0 "
      "WHERE id=?"};
    update.bind(1, asset.nativeFormat);
    update.bind(2, asset.recipe);
    update.bind(3, asset.revision);
    update.bind(4, contentRow);
    update.bind(5, asset.size);
    update.bind(6, assetId);
    update.run();

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

    // No history: publishing replaces the previous revision of this asset.
    auto prune = detail::Statement{
      db, "DELETE FROM asset_revisions WHERE asset_id=? AND revision<>?"};
    prune.bind(1, assetId);
    prune.bind(2, asset.revision);
    prune.run();

    transaction.commit();
    return asset;
  });
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

Result<void> AssetLibrary::removeAsset(const std::string& assetId)
{
  return removeAssets({assetId});
}

Result<void> AssetLibrary::removeAssets(const std::vector<std::string>& assetIds)
{
  return detail::attempt([&] {
    if (assetIds.empty())
      return;
    auto* db = m_impl->database.get();
    auto transaction = detail::Transaction{db};
    auto exists =
      detail::Statement{db, "SELECT virtual_path,source_rel_path FROM assets WHERE id=?"};
    auto deleteDependencies = detail::Statement{
      db, "DELETE FROM dependencies WHERE asset_id=? OR dependency_id=?"};
    auto deleteRevisions =
      detail::Statement{db, "DELETE FROM asset_revisions WHERE asset_id=?"};
    auto deleteAsset = detail::Statement{db, "DELETE FROM assets WHERE id=?"};
    auto seen = std::unordered_set<std::string>{};
    for (const auto& assetId : assetIds)
    {
      if (!seen.insert(assetId).second)
        continue;
      detail::validateAssetId(assetId);
      exists.bind(1, assetId);
      if (!exists.next())
        throw detail::StoreError{"Library asset not found."};

      exists.reset();
      deleteDependencies.bind(1, assetId);
      deleteDependencies.bind(2, assetId);
      deleteDependencies.run();
      deleteDependencies.reset();
      deleteRevisions.bind(1, assetId);
      deleteRevisions.run();
      deleteRevisions.reset();
      deleteAsset.bind(1, assetId);
      deleteAsset.run();
      deleteAsset.reset();
    }
    transaction.commit();
  });
}


namespace
{
std::string joinTags(const std::vector<std::string>& tags)
{
  auto text = std::string{};
  for (const auto& tag : tags)
  {
    if (!text.empty())
      text.push_back('\n');
    text += tag;
  }
  return text;
}

std::vector<std::string> splitTags(const std::string& text)
{
  auto tags = std::vector<std::string>{};
  auto start = std::size_t{0};
  while (start <= text.size())
  {
    const auto end = text.find('\n', start);
    const auto tag =
      text.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (!tag.empty())
      tags.push_back(tag);
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
  return tags;
}
} // namespace

Result<AssetLibrary::AssetMetadata> AssetLibrary::metadata(
  const std::string& assetId) const
{
  return detail::attempt([&] {
    detail::validateAssetId(assetId);
    auto query = detail::Statement{
      m_impl->database.get(), "SELECT favorite,tags FROM asset_meta WHERE asset_id=?"};
    query.bind(1, assetId);
    if (!query.next())
      return AssetMetadata{};
    return AssetMetadata{query.integer(0) != 0, splitTags(query.text(1))};
  });
}

Result<std::map<std::string, AssetLibrary::AssetMetadata>> AssetLibrary::allMetadata()
  const
{
  return detail::attempt([&] {
    auto query = detail::Statement{
      m_impl->database.get(), "SELECT asset_id,favorite,tags FROM asset_meta"};
    auto result = std::map<std::string, AssetMetadata>{};
    while (query.next())
      result.emplace(
        query.text(0), AssetMetadata{query.integer(1) != 0, splitTags(query.text(2))});
    return result;
  });
}

Result<void> AssetLibrary::setFavorite(const std::string& assetId, const bool favorite)
{
  return detail::attempt([&] {
    detail::validateAssetId(assetId);
    auto* db = m_impl->database.get();
    auto exists = detail::Statement{db, "SELECT 1 FROM assets WHERE id=?"};
    exists.bind(1, assetId);
    if (!exists.next())
      throw detail::StoreError{"Library asset not found."};
    auto statement = detail::Statement{
      db,
      "INSERT INTO asset_meta(asset_id,favorite) VALUES(?,?) ON CONFLICT(asset_id) DO "
      "UPDATE "
      "SET favorite=excluded.favorite"};
    statement.bind(1, assetId);
    statement.bind(2, std::uint64_t{favorite ? 1u : 0u});
    statement.run();
  });
}

Result<void> AssetLibrary::setTags(
  const std::string& assetId, const std::vector<std::string>& tags)
{
  return detail::attempt([&] {
    detail::validateAssetId(assetId);
    for (const auto& tag : tags)
      detail::validateText(tag, "tag");
    auto* db = m_impl->database.get();
    auto exists = detail::Statement{db, "SELECT 1 FROM assets WHERE id=?"};
    exists.bind(1, assetId);
    if (!exists.next())
      throw detail::StoreError{"Library asset not found."};
    auto statement = detail::Statement{
      db,
      "INSERT INTO asset_meta(asset_id,tags) VALUES(?,?) ON CONFLICT(asset_id) DO UPDATE "
      "SET tags=excluded.tags"};
    statement.bind(1, assetId);
    statement.bind(2, joinTags(tags));
    statement.run();
  });
}

Result<LibraryAsset> AssetLibrary::setDisplayName(
  const std::string& assetId, const std::string& name)
{
  return detail::attempt([&] {
    detail::validateAssetId(assetId);
    detail::validateText(name, "display name");
    auto* db = m_impl->database.get();
    auto transaction = detail::Transaction{db};
    auto check = detail::Statement{db, "SELECT 1 FROM assets WHERE id=?"};
    check.bind(1, assetId);
    if (!check.next())
      throw detail::StoreError{"Library asset not found."};
    auto update = detail::Statement{
      db, "UPDATE assets SET display_name=?,custom_display_name=? WHERE id=?"};
    update.bind(1, name);
    update.bind(2, name);
    update.bind(3, assetId);
    update.run();
    transaction.commit();

    const auto found = findAsset(assetId);
    if (found.is_error())
      throw detail::StoreError{std::get<Error>(found.error()).msg};
    if (!found.value())
      throw detail::StoreError{"Library asset not found."};
    return found.value().value();
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

Result<std::vector<AssetDependency>> AssetLibrary::allDependencies() const
{
  return detail::attempt([&] {
    auto query = detail::Statement{
      m_impl->database.get(),
      "SELECT asset_id,dependency_id,kind FROM dependencies ORDER BY "
      "asset_id,dependency_id,kind"};
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


Result<LibraryAsset> AssetLibrary::createTerrainMaterial(
  const TerrainMaterialDefinition& def, std::vector<std::uint8_t> compatible)
{
  return detail::attempt([&]() -> LibraryAsset {
    detail::validatePath(def.name);
    if (def.name.find('/') != std::string::npos)
      throw detail::StoreError{"Material name must be a single file name."};
    for (size_t i = 0; i < 3; ++i)
    {
      const auto& id = def.materialSlots[i].texture;
      detail::validateAssetId(id);
      const auto texture = findAsset(id);
      if (texture.is_error())
        throw detail::StoreError{std::get<Error>(texture.error()).msg};
      if (
        !texture.value() || texture.value()->missing
        || texture.value()->type != AssetType::Texture)
        throw detail::StoreError{
          fmt::format("Terrain material slot {} requires an available texture.", i)};
    }

    const auto subfolder = def.folder.empty() ? std::string{"materials"} : def.folder;
    detail::validatePath(subfolder, true);
    const auto fileName = def.name + ".tbmat";
    auto json = fmt::format(
      "{{\n"
      "  \"format\": \"goldsrc-terrain-material\",\n"
      "  \"version\": 1,\n"
      "  \"name\": {},\n"
      "  \"slots\": [\n"
      "    {{\"name\": {}, \"texture\": {}}},\n"
      "    {{\"name\": {}, \"texture\": {}}},\n"
      "    {{\"name\": {}, \"texture\": {}}}\n"
      "  ]\n"
      "}}\n",
      jsonQuote(def.name),
      jsonQuote(def.materialSlots[0].name.empty() ? "Grass" : def.materialSlots[0].name),
      jsonQuote(def.materialSlots[0].texture),
      jsonQuote(def.materialSlots[1].name.empty() ? "Dirt" : def.materialSlots[1].name),
      jsonQuote(def.materialSlots[1].texture),
      jsonQuote(def.materialSlots[2].name.empty() ? "Rock" : def.materialSlots[2].name),
      jsonQuote(def.materialSlots[2].texture));

    const auto scanResult = importFiles(
      {{pathFromText(fileName),
        subfolder + "/" + fileName,
        std::vector<std::uint8_t>{json.begin(), json.end()}}});
    if (scanResult.is_error())
      throw detail::StoreError{std::get<Error>(scanResult.error()).msg};

    const auto virtualPath = subfolder + "/" + fileName;
    auto* db = m_impl->database.get();
    auto asset = [&]() {
      const auto querySql =
        fmt::format("SELECT {} FROM assets a WHERE a.virtual_path=?", AssetColumns);
      auto query = detail::Statement{db, querySql.c_str()};
      query.bind(1, virtualPath);
      if (!query.next())
        throw detail::StoreError{"Failed to locate created material in catalog."};
      return readAssetRow(query);
    }();
    if (!compatible.empty())
    {
      auto transaction = detail::Transaction{db};
      const auto content = detail::storeContent(db, compatible);
      asset.size = static_cast<std::uint64_t>(compatible.size());
      auto update =
        detail::Statement{db, "UPDATE assets SET content=?,size=? WHERE id=?"};
      update.bind(1, content);
      update.bind(2, asset.size);
      update.bind(3, asset.id);
      update.run();
      auto revision = detail::Statement{
        db,
        "UPDATE asset_revisions SET content=?,size=? WHERE asset_id=? AND revision=?"};
      revision.bind(1, content);
      revision.bind(2, asset.size);
      revision.bind(3, asset.id);
      revision.bind(4, asset.revision);
      revision.run();
      transaction.commit();
    }
    return asset;
  });
}
} // namespace tb::project
