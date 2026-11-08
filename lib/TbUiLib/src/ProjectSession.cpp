/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/ProjectSession.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>

#include "base/PreferenceManager.h"
#include "base/Uuid.h"
#include "img/DecodeImage.h"
#include "img/GoldSrcTexture.h"
#include "mdl/BrushFace.h"
#include "mdl/EntityProperties.h"
#include "mdl/GameFileSystem.h"
#include "mdl/Map.h"
#include "prefs/Preferences.h"
#include "project/ContentHash.h"
#include "project/GoldSrcWad.h"
#include "project/ProjectStore.h"
#include "ui/BuiltinAssetLibrary.h"
#include "ui/BuiltinGoldSrcAssets.h"
#include "ui/MapDocument.h"
#include "ui/ProjectReference.h"
#include "ui/ProjectResourceAudit.h"
#include "ui/QPathUtils.h"
#include "ui/TerrainExport.h"
#include "ui/python/PythonHandleRegistry.h"

#include <fmt/ranges.h>

#include <algorithm>
#include <cctype>
#include <functional>
#include <set>
#include <string_view>

namespace tb::ui
{
namespace
{
bool containsTerrainPaint(const std::vector<std::uint8_t>& source)
{
  constexpr auto marker = std::string_view{"\"_tb_terrain_paint\""};
  return std::search(source.begin(), source.end(), marker.begin(), marker.end())
         != source.end();
}

// GoldSrc texture references and miptex names are case insensitive. The audit indexes
// resources by this normalized form, so snapshot export must compare the same way.
std::string normalizedTextureName(std::string name)
{
  std::ranges::transform(
    name, name.begin(), [](const unsigned char c) { return char(std::tolower(c)); });
  return name;
}

Result<QByteArray> fileDigest(const std::filesystem::path& path)
{
  auto file = QFile{pathAsQString(path)};
  if (!file.open(QIODevice::ReadOnly))
    return Error{file.errorString().toStdString()};
  auto hash = QCryptographicHash{QCryptographicHash::Sha256};
  if (!hash.addData(&file))
    return Error{"Cannot verify project contents."};
  return hash.result();
}

Result<void> writeBytes(
  const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
{
  if (!QDir{}.mkpath(pathAsQString(path.parent_path())))
    return Error{"Cannot create project cache directory."};
  auto file = QSaveFile{pathAsQString(path)};
  if (
    !file.open(QIODevice::WriteOnly)
    || file.write(reinterpret_cast<const char*>(bytes.data()), qint64(bytes.size()))
         != qint64(bytes.size())
    || !file.commit())
    return Error{file.errorString().toStdString()};
  return {};
}

Result<std::vector<std::uint8_t>> readBytes(const std::filesystem::path& path)
{
  auto file = QFile{pathAsQString(path)};
  if (!file.open(QIODevice::ReadOnly))
    return Error{file.errorString().toStdString()};
  const auto bytes = file.readAll();
  if (file.error() != QFileDevice::NoError)
    return Error{file.errorString().toStdString()};
  return std::vector<std::uint8_t>{bytes.begin(), bytes.end()};
}

// Library paths are stored in the project as UTF-8 text, not as untyped narrow strings,
// so a project opened on another machine keeps a readable hint.
std::string storedPath(const std::filesystem::path& path)
{
  return pathAsQString(path).toUtf8().toStdString();
}

std::filesystem::path pathFromStoredPath(const std::string& path)
{
  return pathFromQString(QString::fromStdString(path));
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

const char* publishRoot(const project::AssetType type)
{
  switch (type)
  {
  case project::AssetType::Texture:
  case project::AssetType::Skybox:
    return "textures";
  case project::AssetType::Model:
    return "models";
  case project::AssetType::Sprite:
    return "sprites";
  case project::AssetType::Sound:
    return "sound";
  case project::AssetType::Prefab:
    return "prefabs";
  case project::AssetType::Map:
    return "maps";
  case project::AssetType::Material:
    break;
  }
  return "materials";
}

// GoldSrc names are short ASCII identifiers; anything else is replaced so a library file
// name cannot create an unloadable texture.
std::string goldSrcTextureName(const std::string& stem, const bool masked)
{
  auto name = std::string{};
  for (const auto c : stem)
    name += (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                || c == '_' || c == '-'
              ? c
              : '_';
  if (name.empty())
    name = "texture";
  if (masked && name.front() != '{')
    name.insert(name.begin(), '{');
  if (name.size() > 15)
    name.resize(15);
  return name;
}

// Project virtual path of a captured library asset. The library path is only a hint; the
// GameSrc roots decide where the engine will look for the resource.
std::string publishPathFor(
  const project::LibraryAsset& asset, const std::vector<std::uint8_t>& nativeContent)
{
  const auto root = std::string{publishRoot(asset.type)};
  if (asset.type == project::AssetType::Texture)
  {
    const auto name =
      asset.nativeFormat == "goldsrc-miptex"
        ? project::goldSrcMiptexName(nativeContent)
        : stemOf(
            asset.sourceContainer.empty() ? asset.virtualPath : asset.sourceContainer);
    const auto textureName =
      name.empty() ? goldSrcTextureName(stemOf(asset.virtualPath), false) : name;
    return "textures/" + textureName + ".wad";
  }
  if (asset.virtualPath == root || asset.virtualPath.starts_with(root + "/"))
    return asset.virtualPath;
  return root + "/" + asset.virtualPath;
}

// The game path a library resource would publish when captured, without reading or
// converting its content.
std::string libraryPublishPath(const project::LibraryAsset& asset)
{
  const auto root = std::string{publishRoot(asset.type)};
  if (asset.virtualPath == root || asset.virtualPath.starts_with(root + "/"))
    return asset.virtualPath;
  return root + "/" + asset.virtualPath;
}

std::string captureRecipeFor(
  const img::GoldSrcTexture& texture, const std::uint32_t alpha)
{
  return QJsonDocument{QJsonObject{
                         {"importer", "goldsrc-texture"},
                         {"version", 1},
                         {"name", QString::fromStdString(texture.name)},
                         {"width", int(texture.preview.width)},
                         {"height", int(texture.preview.height)},
                         {"masked", texture.masked},
                         {"alphaThreshold", int(alpha)},
                         {"formatValidated", true}}}
    .toJson(QJsonDocument::Compact)
    .toStdString();
}
} // namespace

ProjectSession::ProjectSession(std::filesystem::path path)
  : m_path{std::move(path)}
  , m_workspace{QDir::tempPath() + "/tb-project-XXXXXX"}
{
}
ProjectSession::~ProjectSession() = default;

Result<void> ProjectSession::initialize(
  const bool create, const std::string& name, const std::string& game)
{
  if (!m_workspace.isValid())
    return Error{"Cannot create project working directory."};
  m_lock = std::make_unique<QLockFile>(pathAsQString(m_path) + ".lock");
  m_lock->setStaleLockTime(0);
  if (!m_lock->tryLock())
    return Error{"Project is already open, or its directory is not writable."};
  auto source = create ? project::ProjectStore::create(m_path, name, game)
                       : project::ProjectStore::open(m_path);
  if (source.is_error())
    return std::get<Error>(source.error());
  auto digest = fileDigest(m_path);
  if (digest.is_error())
    return std::get<Error>(digest.error());
  m_savedDigest = digest.value();
  const auto working = pathFromQString(m_workspace.path()) / "working.tbproj";
  auto copy = source.value()->saveCopy(working);
  if (copy.is_error())
    return std::get<Error>(copy.error());
  auto opened = project::ProjectStore::open(working);
  if (opened.is_error())
    return std::get<Error>(opened.error());
  m_store = std::move(opened).value();
  return {};
}

Result<std::shared_ptr<ProjectSession>> ProjectSession::create(
  const std::filesystem::path& path, const std::string& name, const std::string& game)
{
  auto session = std::shared_ptr<ProjectSession>{new ProjectSession{path}};
  return session->initialize(true, name, game)
         | kdl::transform([&]() { return session; });
}
Result<std::shared_ptr<ProjectSession>> ProjectSession::open(
  const std::filesystem::path& path)
{
  auto session = std::shared_ptr<ProjectSession>{new ProjectSession{path}};
  return session->initialize(false, {}, {}) | kdl::transform([&]() { return session; });
}
const std::filesystem::path& ProjectSession::path() const
{
  return m_path;
}
const project::ProjectStore& ProjectSession::store() const
{
  return *m_store;
}
std::vector<std::shared_ptr<MapDocument>> ProjectSession::documents() const
{
  auto result = std::vector<std::shared_ptr<MapDocument>>{};
  for (const auto& [id, document] : m_retainedDocuments)
    result.push_back(document);
  return result;
}

bool ProjectSession::hasOpenMaps() const
{
  return !m_documents.empty();
}
bool ProjectSession::modified() const
{
  return m_modified || std::ranges::any_of(m_documents, [](const auto& entry) {
           return entry.first->map().modified();
         });
}
MapDocument* ProjectSession::documentFor(const std::string& id) const
{
  for (const auto& [document, entry] : m_documents)
    if (entry.assetId == id)
      return document;
  return nullptr;
}
std::optional<std::string> ProjectSession::assetFor(const MapDocument& document) const
{
  const auto it = m_documents.find(const_cast<MapDocument*>(&document));
  return it == m_documents.end() ? std::nullopt : std::optional{it->second.assetId};
}
void ProjectSession::changed()
{
  m_modified = true;
  const auto refreshed = refreshRuntime();
  if (refreshed.is_error())
    emit runtimeError(QString::fromStdString(std::get<Error>(refreshed.error()).msg));
  emit contentsChanged();
  emit stateChanged();
}
Result<project::Asset> ProjectSession::importAsset(const project::AssetImport& asset)
{
  return m_store->importAsset(asset) | kdl::transform([&](auto result) {
           changed();
           if (!result.library.empty())
             emit libraryLinksChanged();
           return result;
         });
}
Result<project::Asset> ProjectSession::replaceAsset(
  const std::string& id, const project::AssetImport& asset)
{
  if (documentFor(id))
    return Error{"Close this map before reimporting it."};
  return m_store->replaceAsset(id, asset) | kdl::transform([&](auto result) {
           changed();
           if (!result.library.empty())
             emit libraryLinksChanged();
           return result;
         });
}
Result<project::Asset> ProjectSession::renameAsset(
  const std::string& id, const std::string& name)
{
  return m_store->renameAsset(id, name) | kdl::transform([&](auto result) {
           changed();
           return result;
         });
}
Result<project::Asset> ProjectSession::moveAsset(
  const std::string& id, const std::string& path)
{
  return m_store->moveAsset(id, path) | kdl::transform([&](auto result) {
           changed();
           return result;
         });
}
Result<std::filesystem::path> ProjectSession::assetFile(const project::Asset& asset) const
{
  if (asset.type == project::AssetType::Model)
  {
    auto closure = std::map<std::string, project::Asset>{{asset.id, asset}};
    auto pending = std::vector<std::string>{asset.id};
    while (!pending.empty())
    {
      const auto id = pending.back();
      pending.pop_back();
      const auto edges = m_store->dependencies(id);
      if (edges.is_error())
        return std::get<Error>(edges.error());
      for (const auto& edge : edges.value())
      {
        if (closure.contains(edge.dependencyId))
          continue;
        const auto dependency = m_store->findAsset(edge.dependencyId);
        if (dependency.is_error())
          return std::get<Error>(dependency.error());
        if (!dependency.value())
          return Error{"Project model preview dependency is missing."};
        closure.emplace(edge.dependencyId, *dependency.value());
        pending.push_back(edge.dependencyId);
      }
    }
    auto signature = QByteArray{};
    for (const auto& [id, dependency] : closure)
      signature += QByteArray::fromStdString(
        id + ":" + std::to_string(dependency.revision) + ":" + dependency.publishPath
        + "\n");
    const auto digest =
      QCryptographicHash::hash(signature, QCryptographicHash::Sha256).toHex();
    const auto root = pathFromQString(m_workspace.path()) / "project-model-preview"
                      / asset.id / digest.toStdString();
    for (const auto& [id, dependency] : closure)
    {
      const auto relative =
        pathFromQString(QString::fromStdString(dependency.publishPath));
      if (relative.empty() || relative.has_root_path())
        return Error{"Invalid model preview path."};
      for (const auto& part : relative)
        if (part == "..")
          return Error{"Invalid model preview path."};
      const auto destination = root / relative;
      if (QFile::exists(pathAsQString(destination)))
        continue;
      const auto bytes =
        m_store->readAsset(id, project::ContentKind::Compatible, dependency.revision);
      if (bytes.is_error())
        return std::get<Error>(bytes.error());
      const auto written = writeBytes(destination, bytes.value());
      if (written.is_error())
        return std::get<Error>(written.error());
    }
    return root / pathFromQString(QString::fromStdString(asset.publishPath));
  }
  // Per-project session + stable ID + immutable revision isolate preview caches.
  const auto path =
    pathFromQString(m_workspace.path()) / "assets" / asset.id
    / std::to_string(asset.revision)
    / pathFromQString(QString::fromStdString(asset.virtualPath)).filename();
  if (QFile::exists(pathAsQString(path)))
    return path;
  return m_store->readAsset(asset.id, project::ContentKind::Compatible, asset.revision)
         | kdl::and_then([&](const auto& bytes) {
             return writeBytes(path, bytes) | kdl::transform([&]() { return path; });
           });
}

Result<void> ProjectSession::removeAsset(const std::string& id)
{
  if (documentFor(id))
    return Error{"Close this map before removing it."};
  const auto asset = m_store->findAsset(id);
  if (asset.is_error())
    return std::get<Error>(asset.error());
  // MAP files are not runtime reference targets. Keep the store's explicit dependency
  // guard, but allow broken maps to be removed even when other maps cannot be scanned.
  if (asset.value() && asset.value()->type == project::AssetType::Map)
    return m_store->removeAsset(id) | kdl::transform([&]() { changed(); });
  const auto references = projectAssetMapReferences(*this);
  if (references.is_error())
    return std::get<Error>(references.error());
  if (const auto uses = references.value().find(id); uses != references.value().end())
  {
    auto message = tr("Asset is referenced by project maps:").toStdString();
    for (const auto& mapPath : uses->second)
      message += "\n" + mapPath;
    return Error{message};
  }
  return m_store->removeAsset(id) | kdl::transform([&]() { changed(); });
}
Result<void> ProjectSession::exportCompileSnapshot(
  const std::string& mapAssetId,
  const std::filesystem::path& folder,
  const std::string& mapFileName)
{
  if (
    mapFileName.empty()
    || std::filesystem::path{mapFileName}.filename()
         != std::filesystem::path{mapFileName})
    return Error{"The snapshot map file name must be a plain file name."};
  const auto map = m_store->findAsset(mapAssetId);
  if (map.is_error())
    return std::get<Error>(map.error());
  if (!map.value() || map.value()->type != project::AssetType::Map)
    return Error{"Select a project map before exporting a compile snapshot."};
  const auto contents = workingMapContents(mapAssetId);
  if (contents.is_error())
    return std::get<Error>(contents.error());

  const auto audit = auditGoldSrcProjectResources(*this);
  if (audit.is_error())
    return std::get<Error>(audit.error());

  if (const auto valid = validateGoldSrcCompileResources(audit.value(), mapAssetId);
      valid.is_error())
    return std::get<Error>(valid.error());
  auto externals = std::vector<std::string>{};
  auto directAssets = std::vector<std::string>{};
  auto directTextures = std::map<std::string, std::vector<std::string>>{};
  auto needsEmptyTexture = false;
  for (const auto& use : audit.value().uses)
  {
    if (use.mapId != mapAssetId)
      continue;
    switch (use.status)
    {
    case ProjectResourceStatus::Embedded:
      for (const auto& id : use.assetIds)
      {
        // Terrain recipes and their slots are bake inputs. The generated map uses only
        // the baked textures; include an asset here only if the map also references it
        // as an ordinary resource.
        if (use.location.ends_with("." + mdl::EntityPropertyKeys::TbTerrainPaint))
          continue;
        if (std::ranges::find(directAssets, id) == directAssets.end())
          directAssets.push_back(id);
        directTextures[id].push_back(use.reference);
      }
      break;
    case ProjectResourceStatus::Unsupported:
      // Sentence and decal lookups are not project resources; the engine resolves them.
      break;
    case ProjectResourceStatus::Missing:
      if (
        normalizedTextureName(use.reference)
        == normalizedTextureName(mdl::BrushFace::NoMaterialName))
        needsEmptyTexture = true;
      else
        externals.push_back(use.reference);
      break;
    case ProjectResourceStatus::Ambiguous:
    case ProjectResourceStatus::InvalidPath:
    case ProjectResourceStatus::ScanError:
      break; // Rejected by the shared compile validation above.
    }
  }

  // The dependency closure is part of the fixed input: a model without its separate
  // texture file or animation groups compiles, but ships broken.
  auto assets = std::vector<project::Asset>{};
  auto visited = std::set<std::string>{};
  std::function<Result<void>(const std::string&)> collect =
    [&](const std::string& id) -> Result<void> {
    if (!visited.insert(id).second)
      return {};
    auto asset = m_store->findAsset(id);
    if (asset.is_error())
      return std::get<Error>(asset.error());
    if (!asset.value())
      return Error{"Project asset not found: " + id};
    auto dependencies = m_store->dependencies(id);
    if (dependencies.is_error())
      return std::get<Error>(dependencies.error());
    for (const auto& dependency : dependencies.value())
    {
      auto collected = collect(dependency.dependencyId);
      if (collected.is_error())
        return std::get<Error>(collected.error());
    }
    assets.push_back(*asset.value());
    return {};
  };
  for (const auto& id : directAssets)
  {
    auto collected = collect(id);
    if (collected.is_error())
      return std::get<Error>(collected.error());
  }

  const auto isDirect = [&](const std::string& id) {
    return std::ranges::find(directAssets, id) != directAssets.end();
  };

  // Sky groups address six source images, not their WAD-compatible brush textures.
  // Stock GoldSrc loads sky faces as 256x256 textures. Keep the original in the project,
  // but publish opaque, uncompressed 24-bit TGA at that size (bottom row first).
  // See https://github.com/advancedfx/advancedfx/wiki/GoldSrc:mirv_fx_skyhd
  constexpr auto skyFaceSize = 256;
  auto skyFaces = std::set<std::string>{};
  auto skyPaths = std::set<std::string>{};
  auto skyResources = std::map<std::string, std::vector<std::uint8_t>>{};
  auto skyMetadata = std::vector<std::string>{};
  for (const auto& group : assets)
  {
    if (group.type != project::AssetType::Skybox)
      continue;
    const auto name = gameName(*this, group);
    if (name.is_error())
      return std::get<Error>(name.error());
    const auto dependencies = m_store->dependencies(group.id);
    if (dependencies.is_error())
      return std::get<Error>(dependencies.error());
    auto suffixes = std::set<std::string>{};
    auto size = std::size_t{0};
    for (const auto& dependency : dependencies.value())
    {
      if (dependency.kind != "skybox-face")
        continue;
      const auto face = m_store->findAsset(dependency.dependencyId);
      if (face.is_error())
        return std::get<Error>(face.error());
      if (!face.value())
        return Error{"Skybox face is missing: " + dependency.dependencyId};
      const auto stem = normalizedTextureName(stemOf(face.value()->sourceName));
      const auto suffix = stem.size() >= 2 ? stem.substr(stem.size() - 2) : std::string{};
      static const auto expected =
        std::set<std::string>{"bk", "dn", "ft", "lf", "rt", "up"};
      if (!expected.contains(suffix) || !suffixes.insert(suffix).second)
        return Error{
          "Skybox '" + name.value() + "' has an invalid or duplicate face: " + stem};
      const auto original =
        m_store->readAsset(face.value()->id, project::ContentKind::Original);
      if (original.is_error())
        return std::get<Error>(original.error());
      const auto decoded =
        img::decodeImage(original.value().data(), original.value().size());
      if (decoded.is_error())
        return Error{
          "Cannot decode skybox face '" + stem
          + "': " + std::get<Error>(decoded.error()).msg};
      const auto& image = decoded.value();
      if (
        image.width == 0 || image.width != image.height || image.width > 65535
        || (size != 0 && size != image.width)
        || image.alphaDomain != img::ImageAlphaDomain::Opaque)
        return Error{
          "Skybox '" + name.value()
          + "' requires six equally sized opaque square images."};
      size = image.width;
      const auto published =
        QImage{
          image.pixels.data(),
          int(image.width),
          int(image.height),
          QImage::Format_RGBA8888}
          .scaled(
            skyFaceSize, skyFaceSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
          .convertToFormat(QImage::Format_RGBA8888);
      if (published.isNull())
        return Error{"Cannot resize skybox face '" + stem + "'."};
      auto tga = std::vector<std::uint8_t>(18, 0);
      tga[2] = 2;
      tga[12] = std::uint8_t(skyFaceSize & 255);
      tga[13] = std::uint8_t(skyFaceSize >> 8);
      tga[14] = tga[12];
      tga[15] = tga[13];
      tga[16] = 24;
      tga.reserve(18 + skyFaceSize * skyFaceSize * 3);
      for (auto y = skyFaceSize; y > 0; --y)
      {
        const auto* row = published.constScanLine(y - 1);
        for (auto x = 0; x < skyFaceSize; ++x)
        {
          const auto offset = x * 4;
          tga.insert(tga.end(), {row[offset + 2], row[offset + 1], row[offset]});
        }
      }
      const auto target = "gfx/env/" + name.value() + suffix + ".tga";
      const auto normalized = normalizeGamePath(target);
      if (!normalized || !skyPaths.insert(*normalized).second)
        return Error{"Skybox publish path is invalid or duplicated: " + target};
      skyMetadata.push_back(fmt::format(
        "skybox-face path={} id={} revision={} sourceHash={} hash={}",
        target,
        face.value()->id,
        face.value()->revision,
        project::contentHash(original.value()),
        project::contentHash(tga)));
      skyResources.emplace(target, std::move(tga));
      skyFaces.insert(face.value()->id);
    }
    if (suffixes.size() != 6)
      return Error{"Skybox '" + name.value() + "' does not contain all six faces."};
  }

  auto miptexByName = std::map<std::string, std::vector<std::uint8_t>>{};
  auto resources = std::vector<std::pair<std::string, std::vector<std::uint8_t>>>{};
  auto metadata = std::vector<std::string>{
    "# TB compile snapshot",
    "project=" + m_store->info().id,
    "map=" + mapAssetId,
    "mapFile=" + mapFileName,
    "mapRevision=" + std::to_string(map.value()->revision)};
  for (auto& [path, bytes] : skyResources)
    resources.emplace_back(path, std::move(bytes));
  metadata.insert(metadata.end(), skyMetadata.begin(), skyMetadata.end());
  for (const auto& reference : externals)
    metadata.push_back("external " + reference);
  for (const auto& entry : assets)
  {
    const auto compatible =
      m_store->readAsset(entry.id, project::ContentKind::Compatible);
    if (compatible.is_error())
      return std::get<Error>(compatible.error());
    metadata.push_back(fmt::format(
      "asset path={} type={} id={} revision={} hash={}{}",
      entry.publishPath,
      project::assetTypeName(entry.type),
      entry.id,
      entry.revision,
      entry.contentHash,
      isDirect(entry.id) ? " direct" : " dependency"));
    if (
      entry.type == project::AssetType::Skybox
      || (skyFaces.contains(entry.id) && directTextures[entry.id].empty()))
      continue;
    if (entry.type != project::AssetType::Texture)
    {
      resources.emplace_back(entry.publishPath, compatible.value());
      continue;
    }

    const auto wad = project::readGoldSrcWad(compatible.value());
    if (wad.is_error())
      return Error{"Project texture '" + entry.displayName + "' is not a GoldSrc WAD."};
    // An imported WAD may hold more textures than the map uses. Only the referenced names
    // are exported, so "the snapshot is exactly what the map needs" stays true.
    const auto& names = directTextures[entry.id];
    for (const auto& texture : wad.value().textures)
    {
      if (!texture.valid)
        continue;
      const auto wanted =
        names.empty() || std::ranges::any_of(names, [&](const auto& name) {
          return normalizedTextureName(name) == normalizedTextureName(texture.name);
        });
      if (wanted)
        miptexByName.emplace(texture.name, texture.miptex);
    }
    if (!names.empty())
    {
      for (const auto& name : names)
      {
        const auto found = std::ranges::any_of(miptexByName, [&](const auto& item) {
          return normalizedTextureName(item.first) == normalizedTextureName(name);
        });
        if (!found)
          return Error{
            "Project texture asset '" + entry.displayName + "' does not contain '" + name
            + "'."};
      }
    }
  }

  if (needsEmptyTexture)
  {
    const auto bytes = builtinEmptyTextureWad();
    if (bytes.is_error())
      return std::get<Error>(bytes.error());
    const auto wad = project::readGoldSrcWad(bytes.value());
    if (
      wad.is_error() || wad.value().textures.size() != 1
      || !wad.value().textures.front().valid)
      return Error{"Invalid built-in placeholder texture."};
    const auto& texture = wad.value().textures.front();
    miptexByName.emplace(texture.name, texture.miptex);
  }

  auto baked = [&]() -> Result<std::optional<TerrainSnapshot>> {
    const auto* document = documentFor(mapAssetId);
    if (!document || !document->map().modified())
    {
      const auto cached = m_store->terrainCompile(mapAssetId);
      if (cached.is_error())
        return std::get<Error>(cached.error());
      if (
        cached.value() && cached.value()->sourceRevision == map.value()->revision
        && cached.value()->generatorVersion == TerrainGeneratorVersion)
      {
        const auto& data = *cached.value();
        if (!data.error.empty())
          return Error{data.error};
        if (data.map.empty() || data.wad.empty() || containsTerrainPaint(data.map))
          return Error{"Invalid cached terrain compilation data."};
        const auto wad = project::readGoldSrcWad(data.wad);
        if (wad.is_error())
          return std::get<Error>(wad.error());
        auto terrain = TerrainSnapshot{};
        terrain.map = data.map;
        terrain.topFaces = static_cast<size_t>(data.topFaces);
        for (const auto& texture : wad.value().textures)
        {
          if (
            !texture.valid
            || !terrain.textures.emplace(texture.name, texture.miptex).second)
            return Error{"Invalid cached terrain texture."};
        }
        return std::optional<TerrainSnapshot>{std::move(terrain)};
      }
    }
    return bakeTerrainSnapshot(contents.value(), m_store.get());
  }();
  if (baked.is_error())
    return std::get<Error>(baked.error());
  auto terrain = std::move(baked).value();
  if (terrain)
  {
    metadata.push_back(fmt::format("terrain topFaces={}", terrain->topFaces));
    for (auto& [name, miptex] : terrain->textures)
    {
      if (std::ranges::any_of(miptexByName, [&](const auto& item) {
            return normalizedTextureName(item.first) == name;
          }))
        return Error{
          "Generated terrain texture conflicts with a project texture: " + name};
      miptexByName.emplace(name, std::move(miptex));
    }
  }

  auto entries = std::vector<std::vector<std::uint8_t>>{};
  entries.reserve(miptexByName.size());
  for (auto& [name, miptex] : miptexByName)
  {
    metadata.push_back("texture " + name);
    entries.push_back(std::move(miptex));
  }
  const auto wad = project::buildGoldSrcWad(entries);
  if (wad.is_error())
    return std::get<Error>(wad.error());

  auto error = std::error_code{};
  std::filesystem::create_directories(folder, error);
  if (error)
    return Error{"Cannot create compile snapshot folder: " + error.message()};
  const auto write = [](
                       const std::filesystem::path& path,
                       const std::vector<std::uint8_t>& bytes) -> Result<void> {
    auto file = QSaveFile{pathAsQString(path)};
    if (
      !file.open(QIODevice::WriteOnly)
      || file.write(reinterpret_cast<const char*>(bytes.data()), qint64(bytes.size()))
           != qint64(bytes.size())
      || !file.commit())
      return Error{file.errorString().toStdString()};
    return {};
  };
  const auto text = [](const std::string& value) {
    return std::vector<std::uint8_t>{value.begin(), value.end()};
  };

  if (const auto result =
        write(folder / mapFileName, terrain ? terrain->map : contents.value());
      result.is_error())
    return result;
  if (const auto result = write(folder / "textures.wad", wad.value()); result.is_error())
    return result;
  // hltools reads an explicit WAD list; the paths stay relative so the snapshot can be
  // compiled from any machine as long as the compiler runs inside the folder.
  if (const auto result = write(folder / "wads.cfg", text("textures.wad\n"));
      result.is_error())
    return result;
  if (const auto result = write(
        folder / "snapshot.txt", text(fmt::format("{}\n", fmt::join(metadata, "\n"))));
      result.is_error())
    return result;
  for (const auto& [path, bytes] : resources)
  {
    // Resources keep their publish path below `resources/` so the snapshot root stays the
    // compiler's input directory.
    const auto target =
      folder / "resources" / pathFromQString(QString::fromStdString(path));
    std::filesystem::create_directories(target.parent_path(), error);
    if (error)
      return Error{"Cannot create compile snapshot folder: " + error.message()};
    if (const auto result = write(target, bytes); result.is_error())
      return result;
  }
  return {};
}
Result<std::filesystem::path> ProjectSession::prepareMap(const std::string& id)
{
  return m_store->findAsset(id)
         | kdl::and_then([&](const auto& asset) -> Result<std::filesystem::path> {
             if (!asset || asset->type != project::AssetType::Map)
               return Error{"Map asset not found."};
             const auto path =
               pathFromQString(m_workspace.path()) / "maps" / id
               / pathFromQString(QString::fromStdString(asset->virtualPath)).filename();
             return m_store->readAsset(id, project::ContentKind::Compatible)
                    | kdl::and_then([&](const auto& bytes) {
                        return writeBytes(path, bytes)
                               | kdl::transform([&]() { return path; });
                      });
           });
}

Result<void> ProjectSession::refreshRuntime()
{
  return m_store->assets() | kdl::and_then([&](const auto& assets) -> Result<void> {
           auto runtimeAssets = assets;
           std::erase_if(runtimeAssets, [](const auto& asset) {
             // Maps are documents, not cache files. A skybox group is an organization
             // node whose faces are materialized as their own texture assets.
             return asset.type == project::AssetType::Map
                    || asset.type == project::AssetType::Skybox;
           });
           if (m_runtime && runtimeAssets == m_runtimeAssets)
             return {};
           auto runtime =
             std::make_unique<QTemporaryDir>(m_workspace.path() + "/runtime-XXXXXX");
           if (!runtime->isValid())
             return Error{"Cannot create project runtime directory."};
           const auto root = pathFromQString(runtime->path());
           auto modelPaths = std::vector<std::filesystem::path>{};
           for (const auto& asset : m_runtimeAssets)
             if (
               asset.type == project::AssetType::Model
               || asset.type == project::AssetType::Sprite)
               modelPaths.emplace_back(asset.publishPath);
           for (const auto& asset : runtimeAssets)
           {
             const auto path =
               root / pathFromQString(QString::fromStdString(asset.publishPath));
             const auto source = assetFile(asset);
             if (source.is_error())
               return std::get<Error>(source.error());
             if (!QDir{}.mkpath(pathAsQString(path.parent_path())))
               return Error{"Cannot create project runtime folder."};
             // Immutable cache files can share disk blocks with each runtime generation.
             // A copy is the portable fallback for filesystems without hard links.
             auto ec = std::error_code{};
             std::filesystem::create_hard_link(source.value(), path, ec);
             if (ec && !QFile::copy(pathAsQString(source.value()), pathAsQString(path)))
               return Error{"Cannot materialize project asset."};
             if (
               asset.type == project::AssetType::Model
               || asset.type == project::AssetType::Sprite)
               modelPaths.emplace_back(asset.publishPath);
           }
           ++m_runtimeGeneration;
           for (const auto& [document, entry] : m_documents)
           {
             document->map().gameFileSystem().setProjectDirectory(root);
             document->map().reloadEntityModels(modelPaths);
             document->map().materialCollectionsWillChangeNotifier();
             document->map().materialCollectionsDidChangeNotifier();
             markDocumentBound(*document, root);
           }
           m_runtime = std::move(runtime);
           m_runtimeAssets = std::move(runtimeAssets);
           return {};
         });
}

Result<std::vector<std::uint8_t>> ProjectSession::workingMapContents(
  const std::string& id) const
{
  const auto asset = m_store->findAsset(id);
  if (asset.is_error())
    return std::get<Error>(asset.error());
  if (!asset.value() || asset.value()->type != project::AssetType::Map)
    return Error{"Map asset not found."};
  const auto* document = documentFor(id);
  if (!document || !document->map().modified())
    return m_store->readAsset(id, project::ContentKind::Compatible);

  // Keep audit snapshots separate from the temporary revisions used by save/history.
  auto snapshot = QTemporaryDir{m_workspace.path() + "/map-read-XXXXXX"};
  if (!snapshot.isValid())
    return Error{"Cannot create map snapshot directory."};
  const auto path = pathFromQString(snapshot.path()) / "live.map";
  return document->map().saveTo(path) | kdl::and_then([&]() { return readBytes(path); });
}

void ProjectSession::markDocumentBound(
  MapDocument& document, const std::filesystem::path& projectDirectory)
{
  auto& map = document.map();
  m_boundDocuments[&document] = BoundDocumentState{
    &map, &map.gameFileSystem(), projectDirectory, m_runtimeGeneration};
}

void ProjectSession::bindDocument(MapDocument& document)
{
  auto weak = weak_from_this();
  document.map().setSaveHandler([weak]() -> Result<void> {
    if (const auto session = weak.lock())
      return session->save();
    return Error{"Project session has closed."};
  });

  // Opening a map fires documentWasLoaded right after registerDocument has bound it,
  // and a runtime refresh binds every open document itself. Repeating the entity model
  // and material reload for the same document state is pure work (including a full
  // game directory rescan), so skip it unless the map, its file system or the runtime
  // generation actually changed.
  auto& map = document.map();
  const auto projectDirectory = pathFromQString(m_runtime->path());
  auto* gameFileSystem = &map.gameFileSystem();
  const auto bound = m_boundDocuments.find(&document);
  if (
    bound != m_boundDocuments.end() && bound->second.map == &map
    && bound->second.gameFileSystem == gameFileSystem
    && bound->second.projectDirectory == projectDirectory
    && bound->second.runtimeGeneration == m_runtimeGeneration)
  {
    return;
  }

  gameFileSystem->setProjectDirectory(projectDirectory);
  auto modelPaths = std::vector<std::filesystem::path>{};
  for (const auto& asset : m_runtimeAssets)
    if (
      asset.type == project::AssetType::Model || asset.type == project::AssetType::Sprite)
      modelPaths.emplace_back(asset.publishPath);
  map.reloadEntityModels(modelPaths);
  map.materialCollectionsWillChangeNotifier();
  map.materialCollectionsDidChangeNotifier();
  markDocumentBound(document, projectDirectory);
}
Result<void> ProjectSession::registerDocument(
  MapDocument& document, const std::string& id)
{
  const auto asset = m_store->findAsset(id);
  if (asset.is_error())
    return std::get<Error>(asset.error());
  if (!asset.value() || asset.value()->type != project::AssetType::Map)
    return Error{"Map asset not found."};
  const auto runtime = refreshRuntime();
  if (runtime.is_error())
    return std::get<Error>(runtime.error());
  if (documentFor(id) || m_documents.contains(&document))
    return Error{"Map is already open."};
  auto entry = Document{id, {}};
  entry.connection += document.documentDidChangeNotifier.connect(
    [this](const auto&...) { emit mapContentsChanged(); });
  entry.connection += document.documentWasLoadedNotifier.connect(
    [this, &document]() { bindDocument(document); });
  entry.connection += document.modificationStateDidChangeNotifier.connect(
    [this]() { emit stateChanged(); });
  m_documents.emplace(&document, std::move(entry));
  bindDocument(document);
  emit stateChanged();
  return {};
}
Result<void> ProjectSession::retainDocument(
  std::shared_ptr<MapDocument> document, const std::string& id)
{
  if (!document)
    return Error{"Cannot retain an empty map document."};
  if (const auto* open = documentFor(id); open != nullptr && open != document.get())
    return Error{"Another document is already open for this map."};

  m_retainedDocuments[id] = std::move(document);
  return {};
}

std::shared_ptr<MapDocument> ProjectSession::retainedDocument(const std::string& id) const
{
  const auto it = m_retainedDocuments.find(id);
  return it == m_retainedDocuments.end() ? nullptr : it->second;
}

Result<void> ProjectSession::releaseDocument(const std::string& id)
{
  const auto it = m_retainedDocuments.find(id);
  if (it == m_retainedDocuments.end())
    return {};

  if (auto* document = documentFor(id))
    unregisterDocument(*document);
  m_retainedDocuments.erase(it);
  return {};
}
void ProjectSession::unregisterDocument(MapDocument& document)
{
  if (!m_documents.contains(&document))
    return;
  PythonHandleRegistry::instance().invalidateDocument(&document);
  document.map().setSaveHandler({});
  m_documents.erase(&document);
  m_boundDocuments.erase(&document);
  emit stateChanged();
}

Result<void> ProjectSession::stageMaps()
{
  const auto reset = unstageMaps();
  if (reset.is_error())
    return std::get<Error>(reset.error());
  for (const auto& [document, entry] : m_documents)
  {
    const auto& map = document->map();
    if (!map.modified())
      continue;
    const auto path = pathFromQString(m_workspace.path()) / (entry.assetId + ".map");
    auto saved = map.saveTo(path);
    if (saved.is_error())
      return std::get<Error>(saved.error());
    auto bytes = readBytes(path);
    if (bytes.is_error())
      return std::get<Error>(bytes.error());
    auto import = project::AssetImport{};
    import.type = project::AssetType::Map;
    auto previous = m_store->findAsset(entry.assetId);
    if (previous.is_error())
      return std::get<Error>(previous.error());
    if (!previous.value())
      return Error{"Project map asset is missing."};
    import.sourceName = previous.value()->sourceName;
    import.conversionRecipe = previous.value()->conversionRecipe;
    m_stagedMaps.push_back(*previous.value());
    import.original = bytes.value();
    import.compatible = std::move(bytes).value();
    const auto result = m_store->replaceAsset(entry.assetId, import);
    if (result.is_error())
      return std::get<Error>(result.error());
  }
  return {};
}

Result<void> ProjectSession::unstageMaps()
{
  return m_store->selectRevisions(m_stagedMaps)
         | kdl::transform([&]() { m_stagedMaps.clear(); });
}

namespace
{
Result<bool> terrainCompileNeedsRefresh(
  project::ProjectStore& store, const std::string& id)
{
  const auto asset = store.findAsset(id);
  if (asset.is_error())
    return std::get<Error>(asset.error());
  if (!asset.value() || asset.value()->type != project::AssetType::Map)
    return Error{"Project map asset is missing."};
  const auto cached = store.terrainCompile(id);
  if (cached.is_error())
    return std::get<Error>(cached.error());
  if (
    cached.value() && cached.value()->sourceRevision == asset.value()->revision
    && cached.value()->generatorVersion == TerrainGeneratorVersion)
    return false;
  const auto source = store.readAsset(id, project::ContentKind::Compatible);
  if (source.is_error())
    return std::get<Error>(source.error());
  return cached.value().has_value() || containsTerrainPaint(source.value());
}

Result<void> updateTerrainCompile(
  project::ProjectStore& store, const project::Asset& asset)
{
  const auto needed = terrainCompileNeedsRefresh(store, asset.id);
  if (needed.is_error())
    return std::get<Error>(needed.error());
  if (!needed.value())
    return {};
  const auto source = store.readAsset(asset.id, project::ContentKind::Compatible);
  if (source.is_error())
    return std::get<Error>(source.error());
  auto data = project::TerrainCompile{};
  auto baked = bakeTerrainSnapshot(source.value(), &store, &data.dependencies);
  data.sourceRevision = asset.revision;
  data.generatorVersion = TerrainGeneratorVersion;
  if (baked.is_error())
    data.error = std::get<Error>(baked.error()).msg;
  else if (auto terrain = std::move(baked).value())
  {
    data.map = std::move(terrain->map);
    data.topFaces = static_cast<std::uint64_t>(terrain->topFaces);
    auto entries = std::vector<std::vector<std::uint8_t>>{};
    for (auto& entry : terrain->textures)
      entries.push_back(std::move(entry.second));
    auto wad = project::buildGoldSrcWad(entries);
    if (wad.is_error())
      data.error = std::get<Error>(wad.error()).msg;
    else
      data.wad = std::move(wad).value();
  }
  else if (containsTerrainPaint(source.value()))
    data.error = "Terrain paint data could not be parsed.";
  else
    return store.clearTerrainCompile(asset.id);
  return store.setTerrainCompile(asset.id, data);
}
} // namespace

Result<bool> ProjectSession::terrainCompileNeedsSave(const std::string& id) const
{
  return terrainCompileNeedsRefresh(*m_store, id);
}

Result<void> ProjectSession::updateTerrainCompiles()
{
  const auto maps = m_store->assets(project::AssetType::Map);
  if (maps.is_error())
    return std::get<Error>(maps.error());
  for (const auto& asset : maps.value())
    if (const auto updated = updateTerrainCompile(*m_store, asset); updated.is_error())
      return std::get<Error>(updated.error());
  return {};
}

Result<void> ProjectSession::writeLightmapPreviewProject(
  const std::string& id, const std::filesystem::path& path) const
{
  const auto asset = m_store->findAsset(id);
  if (asset.is_error())
    return std::get<Error>(asset.error());
  if (!asset.value() || asset.value()->type != project::AssetType::Map)
    return Error{"Project map asset is missing."};

  if (const auto copied = m_store->saveCopy(path); copied.is_error())
    return std::get<Error>(copied.error());
  auto opened = project::ProjectStore::open(path);
  if (opened.is_error())
    return std::get<Error>(opened.error());
  auto& copy = *opened.value();
  auto snapshotAsset = *asset.value();
  if (const auto* document = documentFor(id); document && document->map().modified())
  {
    auto bytes = workingMapContents(id);
    if (bytes.is_error())
      return std::get<Error>(bytes.error());
    auto input = project::AssetImport{};
    input.type = project::AssetType::Map;
    input.sourceName = snapshotAsset.sourceName;
    input.conversionRecipe = snapshotAsset.conversionRecipe;
    input.original = input.compatible = std::move(bytes).value();
    auto replaced = copy.replaceAsset(id, input);
    if (replaced.is_error())
      return std::get<Error>(replaced.error());
    snapshotAsset = std::move(replaced).value();
  }
  return updateTerrainCompile(copy, snapshotAsset);
}

Result<void> ProjectSession::writeProject(
  const std::filesystem::path& path, const bool overwrite)
{
  if (overwrite)
  {
    auto current = fileDigest(path);
    if (current.is_error() || current.value() != m_savedDigest)
      return Error{"Project changed on disk. Save a copy to preserve both versions."};
  }
  else if (QFile::exists(pathAsQString(path)))
    return Error{"Copy destination already exists."};
  if (const auto updated = updateTerrainCompiles(); updated.is_error())
    return std::get<Error>(updated.error());
  const auto snapshot =
    pathFromQString(m_workspace.path()) / (generateUuid() + ".tbproj");
  auto backup = m_store->saveCopy(snapshot);
  if (backup.is_error())
    return std::get<Error>(backup.error());
  const auto digest = fileDigest(snapshot);
  if (digest.is_error())
  {
    QFile::remove(pathAsQString(snapshot));
    return std::get<Error>(digest.error());
  }
  if (!overwrite)
  {
    // QFile::copy fails if the destination exists, including a racing creator.
    const auto ok = QFile::copy(pathAsQString(snapshot), pathAsQString(path));
    QFile::remove(pathAsQString(snapshot));
    return ok ? Result<void>{} : Result<void>{Error{"Cannot create project copy."}};
  }
  auto input = QFile{pathAsQString(snapshot)};
  auto output = QSaveFile{pathAsQString(path)};
  output.setDirectWriteFallback(false);
  if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly))
  {
    input.close();
    QFile::remove(pathAsQString(snapshot));
    return Error{"Cannot open project for atomic saving."};
  }
  while (!input.atEnd())
  {
    const auto block = input.read(1024 * 1024);
    if (input.error() != QFileDevice::NoError || output.write(block) != block.size())
    {
      input.close();
      QFile::remove(pathAsQString(snapshot));
      return Error{"Project save failed while copying its snapshot."};
    }
  }
  input.close();
  QFile::remove(pathAsQString(snapshot));
  if (!output.commit())
    return Error{output.errorString().toStdString()};
  m_savedDigest = digest.value();
  return {};
}

Result<void> ProjectSession::setMapThumbnail(const std::string& id, const QImage& image)
{
  if (image.isNull())
    return Error{"Cannot capture map thumbnail."};
  auto bytes = QByteArray{};
  auto buffer = QBuffer{&bytes};
  if (
    !buffer.open(QIODevice::WriteOnly)
    || !image.scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation)
          .save(&buffer, "PNG"))
    return Error{"Cannot encode map thumbnail."};
  const auto* begin = reinterpret_cast<const std::uint8_t*>(bytes.constData());
  const auto png = std::vector<std::uint8_t>{begin, begin + bytes.size()};
  const auto previous = m_store->mapThumbnail(id);
  if (previous.is_success() && previous.value() == png)
    return {};
  auto result = m_store->setMapThumbnail(id, png);
  if (result.is_success())
  {
    m_modified = true;
    emit contentsChanged();
    emit stateChanged();
  }
  return result;
}

Result<void> ProjectSession::save()
{
  emit mapThumbnailsRequested();
  auto result = stageMaps() | kdl::and_then([&]() { return writeProject(m_path, true); })
                | kdl::transform([&]() {
                    m_stagedMaps.clear();
                    m_modified = false;
                    for (const auto& [document, entry] : m_documents)
                      document->map().markSaved();
                    emit contentsChanged();
                    emit stateChanged();
                  });
  if (result.is_error())
  {
    const auto reset = unstageMaps();
    if (reset.is_error())
      return std::get<Error>(reset.error());
  }
  return result;
}
Result<void> ProjectSession::saveCopy(const std::filesystem::path& path)
{
  emit mapThumbnailsRequested();
  const auto result =
    stageMaps() | kdl::and_then([&]() { return writeProject(path, false); });
  const auto reset = unstageMaps();
  return reset.is_error() ? reset : result;
}
Result<project::RevisionCleanup> ProjectSession::unreferencedRevisions() const
{
  return m_store->unreferencedRevisions();
}
Result<project::RevisionCleanup> ProjectSession::pruneUnreferencedRevisions()
{
  auto result = m_store->pruneUnreferencedRevisions();
  if (result.is_success() && !result.value().empty())
  {
    // The project file only shrinks when the working copy is saved, so the cleanup joins
    // the dirty state instead of silently rewriting the file behind the user.
    changed();
  }
  return result;
}
Result<project::Checkpoint> ProjectSession::checkpoint(const std::string& name)
{
  auto result = stageMaps() | kdl::and_then([&]() { return m_store->checkpoint(name); });
  const auto reset = unstageMaps();
  if (reset.is_error())
    return std::get<Error>(reset.error());
  if (result.is_success())
    changed();
  return result;
}
Result<project::ProjectChanges> ProjectSession::compare(const std::string& id)
{
  auto result = stageMaps() | kdl::and_then([&]() { return m_store->compare(id); });
  const auto reset = unstageMaps();
  if (reset.is_error())
    return std::get<Error>(reset.error());
  return result;
}

Result<void> ProjectSession::restore(const std::string& id)
{
  const auto changes = m_store->compare(id);
  if (changes.is_error())
    return std::get<Error>(changes.error());
  for (const auto& [document, entry] : m_documents)
  {
    const auto& assets = changes.value().assets;
    const auto it = std::ranges::find_if(
      assets, [&](const auto& change) { return change.assetId == entry.assetId; });
    if (it != assets.end() && !it->before)
      return Error{
        "This version does not contain " + it->after->virtualPath
        + "; close that map before restoring."};
  }

  // Prepare everything in a disposable generation. No live store, document, or cache
  // is changed until every fallible storage, write, and parsing step has succeeded.
  auto generation = QTemporaryDir{m_workspace.path() + "/restore-XXXXXX"};
  if (!generation.isValid())
    return Error{"Cannot create restore preparation directory."};
  const auto root = pathFromQString(generation.path());
  if (const auto copied = m_store->saveCopy(root / "project.tbproj"); copied.is_error())
    return std::get<Error>(copied.error());
  auto opened = project::ProjectStore::open(root / "project.tbproj");
  if (opened.is_error())
    return std::get<Error>(opened.error());
  auto candidate = std::move(opened).value();
  for (const auto& [document, entry] : m_documents)
  {
    if (!document->map().modified())
      continue;
    const auto previous = candidate->findAsset(entry.assetId);
    if (previous.is_error())
      return std::get<Error>(previous.error());
    if (!previous.value())
      return Error{"Project map asset is missing."};
    auto bytes = workingMapContents(entry.assetId);
    if (bytes.is_error())
      return std::get<Error>(bytes.error());
    auto input = project::AssetImport{};
    input.type = project::AssetType::Map;
    input.sourceName = previous.value()->sourceName;
    input.conversionRecipe = previous.value()->conversionRecipe;
    input.original = input.compatible = std::move(bytes).value();
    if (const auto replaced = candidate->replaceAsset(entry.assetId, input);
        replaced.is_error())
      return std::get<Error>(replaced.error());
  }
  if (const auto restored = candidate->restore(id); restored.is_error())
    return std::get<Error>(restored.error());

  auto assets = candidate->assets();
  if (assets.is_error())
    return std::get<Error>(assets.error());
  auto runtimeAssets = std::move(assets).value();
  std::erase_if(runtimeAssets, [](const auto& asset) {
    return asset.type == project::AssetType::Map
           || asset.type == project::AssetType::Skybox;
  });
  auto runtime = std::make_unique<QTemporaryDir>(m_workspace.path() + "/runtime-XXXXXX");
  if (!runtime->isValid())
    return Error{"Cannot create project runtime directory."};
  for (const auto& asset : runtimeAssets)
  {
    const auto bytes = candidate->readAsset(asset.id, project::ContentKind::Compatible);
    if (bytes.is_error())
      return std::get<Error>(bytes.error());
    const auto target = pathFromQString(runtime->path())
                        / pathFromQString(QString::fromStdString(asset.publishPath));
    if (const auto written = writeBytes(target, bytes.value()); written.is_error())
      return written;
  }
  auto replacements = std::vector<std::pair<MapDocument*, std::unique_ptr<mdl::Map>>>{};
  for (const auto& [document, entry] : m_documents)
  {
    const auto asset = candidate->findAsset(entry.assetId);
    if (asset.is_error())
      return std::get<Error>(asset.error());
    const auto path =
      root / "maps" / entry.assetId
      / pathFromQString(QString::fromStdString(asset.value()->virtualPath)).filename();
    const auto bytes =
      candidate->readAsset(entry.assetId, project::ContentKind::Compatible);
    if (bytes.is_error())
      return std::get<Error>(bytes.error());
    if (const auto written = writeBytes(path, bytes.value()); written.is_error())
      return written;
    auto replacement = document->prepareReload(path);
    if (replacement.is_error())
      return std::get<Error>(replacement.error());
    replacement.value()->gameFileSystem().setProjectDirectory(
      pathFromQString(runtime->path()));
    replacements.emplace_back(document, std::move(replacement).value());
  }

  generation.setAutoRemove(false); // Owned by the containing session workspace.
  m_store = std::move(candidate);
  auto previousRuntime =
    std::move(m_runtime); // Keep old resources alive during view refresh.
  ++m_runtimeGeneration;
  m_boundDocuments.clear(); // Every open document gets a new Map below.
  m_runtime = std::move(runtime);
  m_runtimeAssets = std::move(runtimeAssets);
  m_modified = true;
  for (auto& [document, replacement] : replacements)
    document->commitReload(std::move(replacement));
  emit contentsChanged();
  emit stateChanged();
  return {};
}

Result<std::vector<project::LibraryInfo>> ProjectSession::libraryInfos() const
{
  auto storedInfos = m_store->libraries();
  if (storedInfos.is_error())
    return std::get<Error>(storedInfos.error());
  auto infos = std::move(storedInfos).value();
  const auto folder =
    PreferenceManager::instance().get(Preferences::AssetLibraryDirectory);
  if (!folder.empty() && project::AssetLibrary::isLibraryFolder(folder))
  {
    auto library = project::AssetLibrary::open(folder);
    if (library.is_error())
      return std::get<Error>(library.error());
    infos = std::vector<project::LibraryInfo>{library.value()->info()};
  }
  // Browsing the built-in catalog does not attach it or modify the project. A capture
  // stores its own bytes and provenance through the same path as a user library.
  const auto builtin = builtinAssetLibraryInfo();
  if (builtin.is_success())
  {
    std::erase_if(
      infos, [](const auto& info) { return info.id == BuiltinAssetLibraryId; });
    infos.push_back(builtin.value());
  }
  return infos;
}

Result<std::unique_ptr<project::AssetLibrary>> ProjectSession::openLibrary(
  const std::string& libraryId) const
{
  const auto libraries = libraryInfos();
  if (libraries.is_error())
    return std::get<Error>(libraries.error());
  const auto& infos = libraries.value();
  const auto it =
    std::ranges::find_if(infos, [&](const auto& info) { return info.id == libraryId; });
  if (it == infos.end())
    return Error{"Asset library is not attached to this project."};
  return project::AssetLibrary::open(pathFromStoredPath(it->path));
}

Result<ProjectSession::LibraryAttachSummary> ProjectSession::attachLibrary(
  const std::filesystem::path& folder, const bool createIfMissing)
{
  auto error = std::error_code{};
  if (!std::filesystem::is_directory(folder, error))
    return Error{"Asset library folder does not exist: " + folder.generic_string()};

  auto summary = LibraryAttachSummary{};
  auto library = std::unique_ptr<project::AssetLibrary>{};
  if (project::AssetLibrary::isLibraryFolder(folder))
  {
    auto opened = project::AssetLibrary::open(folder);
    if (opened.is_error())
      return std::get<Error>(opened.error());
    library = std::move(opened).value();
  }
  else
  {
    // Turning an arbitrary folder into a library writes a catalog file into it, so it
    // only happens when the caller asked for it after confirming the selection.
    if (!createIfMissing)
      return Error{
        "'" + folder.filename().string()
        + "' is not an asset library folder yet. Create the library first."};
    auto name = folder.filename().string();
    auto scan = project::LibraryScan{};
    auto created = project::AssetLibrary::create(
      folder, name.empty() ? std::string{"Asset Library"} : name, &scan);
    if (created.is_error())
      return std::get<Error>(created.error());
    library = std::move(created).value();
    summary.created = true;
    summary.invalid = scan.invalidEntries;
    if (!scan.warning.empty())
      summary.warnings.push_back(scan.warning);
  }

  const auto assets = library->assets();
  if (assets.is_error())
    return std::get<Error>(assets.error());
  summary.assets = assets.value().size();
  const auto groups = library->groups();
  if (groups.is_error())
    return std::get<Error>(groups.error());
  summary.groups = groups.value().size();

  const auto& info = library->info();
  summary.libraryId = info.id;
  auto attached = m_store->attachLibrary(info.id, info.name, storedPath(folder));
  if (attached.is_error())
    return std::get<Error>(attached.error());
  changed();
  emit libraryLinksChanged();
  return summary;
}

Result<void> ProjectSession::relocateLibrary(
  const std::string& libraryId, const std::filesystem::path& folder)
{
  auto error = std::error_code{};
  if (!std::filesystem::is_directory(folder, error))
    return Error{"Asset library folder does not exist: " + folder.generic_string()};

  auto opened = project::AssetLibrary::open(folder);
  if (opened.is_error())
    return std::get<Error>(opened.error());

  // The identity comes from the catalog, so relinking cannot accrete a second library
  // under the same project link.
  const auto& info = opened.value()->info();
  if (info.id != libraryId)
    return Error{
      "The selected folder belongs to a different asset library (" + info.name + ")."};

  auto attached = m_store->attachLibrary(libraryId, info.name, storedPath(folder));
  if (attached.is_error())
    return std::get<Error>(attached.error());
  changed();
  emit libraryLinksChanged();
  return {};
}

Result<std::vector<ProjectSession::LibraryConnection>> ProjectSession::libraries() const
{
  const auto infos = libraryInfos();
  if (infos.is_error())
    return std::get<Error>(infos.error());
  auto result = std::vector<LibraryConnection>{};
  result.reserve(infos.value().size());
  for (const auto& info : infos.value())
  {
    auto connection = LibraryConnection{info, false, {}};
    auto library = project::AssetLibrary::open(pathFromStoredPath(info.path));
    if (library.is_error())
      connection.error = std::get<Error>(library.error()).msg;
    else
      connection.available = true;
    result.push_back(std::move(connection));
  }
  return result;
}

Result<project::LibraryState> ProjectSession::libraryState(
  const std::string& libraryId) const
{
  // Unavailable libraries are a normal state, not an error: the project must keep working
  // and reporting its own snapshot.
  auto state = project::LibraryState{libraryId, {}, {}, false, {}, {}};
  const auto infos = libraryInfos();
  if (infos.is_error())
    return std::get<Error>(infos.error());
  const auto& entries = infos.value();
  const auto it =
    std::ranges::find_if(entries, [&](const auto& info) { return info.id == libraryId; });
  if (it == entries.end())
  {
    state.error = "Asset library is not attached to this project.";
    return state;
  }
  state.name = it->name;
  state.folder = pathFromStoredPath(it->path);
  auto library = project::AssetLibrary::open(state.folder);
  if (library.is_error())
  {
    state.error = std::get<Error>(library.error()).msg;
    return state;
  }
  auto assets = library.value()->assets();
  if (assets.is_error())
  {
    state.error = std::get<Error>(assets.error()).msg;
    return state;
  }
  state.assets = std::move(assets).value();
  state.available = true;
  return state;
}

Result<project::AssetImport> ProjectSession::captureImport(
  const project::AssetLibrary& library,
  const project::LibraryAsset& asset,
  const std::optional<project::Asset>& existing) const
{
  auto native = library.readContent(asset.id);
  if (native.is_error())
    return std::get<Error>(native.error());
  auto original = library.readOriginal(asset.id);
  if (original.is_error())
    return std::get<Error>(original.error());

  auto import = project::AssetImport{};
  import.type = asset.type;
  import.displayName = existing ? existing->displayName : asset.displayName;
  import.library = project::LibraryLink{
    library.info().id,
    asset.id,
    asset.revision,
    asset.contentHash,
    asset.sourceContainer,
    asset.sourceMember};
  import.sourceName =
    fileNameOf(asset.sourceContainer.empty() ? asset.virtualPath : asset.sourceContainer);
  import.original = std::move(original).value();

  // A library conversion may change its miptex name. The project already has maps
  // referring to its captured name (possibly renamed by the user), so updates must
  // preserve that name for raw images and preconverted WADs alike.
  auto capturedTextureName = std::string{};
  if (existing && asset.type == project::AssetType::Texture)
  {
    const auto contents =
      m_store->readAsset(existing->id, project::ContentKind::Compatible);
    if (contents.is_error())
      return std::get<Error>(contents.error());
    const auto wad = project::readGoldSrcWad(contents.value());
    if (wad.is_error())
      return std::get<Error>(wad.error());
    if (wad.value().textures.size() != 1 || !wad.value().textures.front().valid)
      return Error{
        tr("Updating a captured texture requires a readable single-texture WAD.")
          .toStdString()};
    capturedTextureName = wad.value().textures.front().name;
    if (capturedTextureName.empty() || capturedTextureName.size() > 15)
      return Error{tr("The captured texture name is invalid.").toStdString()};
  }

  if (project::libraryAssetNeedsConversion(asset))
  {
    auto decoded = img::decodeImage(native.value().data(), native.value().size());
    if (decoded.is_error())
      return std::get<Error>(decoded.error());
    const auto& image = decoded.value();
    // Keep the texture name of an existing snapshot so map faces keep resolving.
    const auto name = existing ? capturedTextureName
                               : goldSrcTextureName(
                                   stemOf(asset.virtualPath),
                                   image.alphaDomain != img::ImageAlphaDomain::Opaque);
    auto converted = img::convertGoldSrcTexture(image, name);
    if (converted.is_error())
      return std::get<Error>(converted.error());
    const auto& texture = converted.value();
    import.compatible = texture.wad;
    import.virtualPath =
      existing ? existing->virtualPath : "textures/" + texture.name + ".wad";
    import.conversionRecipe =
      project::captureRecipe(asset, captureRecipeFor(texture, 128));
    return import;
  }

  auto compatible = project::compatibleLibraryContent(asset, native.value());
  if (compatible.is_error())
    return std::get<Error>(compatible.error());
  if (!capturedTextureName.empty())
  {
    const auto wad = project::readGoldSrcWad(compatible.value());
    if (wad.is_error())
      return std::get<Error>(wad.error());
    if (wad.value().textures.size() != 1 || !wad.value().textures.front().valid)
      return Error{
        tr("Updating a captured texture requires a readable single-texture WAD.")
          .toStdString()};
    auto miptex = wad.value().textures.front().miptex;
    std::fill(miptex.begin(), miptex.begin() + 16, std::uint8_t{0});
    std::copy(capturedTextureName.begin(), capturedTextureName.end(), miptex.begin());
    compatible = project::buildGoldSrcWad({miptex});
    if (compatible.is_error())
      return std::get<Error>(compatible.error());
  }
  import.compatible = std::move(compatible).value();
  import.virtualPath =
    existing ? existing->virtualPath : publishPathFor(asset, native.value());
  import.conversionRecipe = project::captureRecipe(asset);
  return import;
}

Result<std::vector<project::AssetCapture>> ProjectSession::buildCapturePlan(
  const std::string& libraryId,
  const std::vector<std::string>& libraryAssetIds,
  std::vector<std::size_t>& planIndices)
{
  auto library = openLibrary(libraryId);
  if (library.is_error())
    return std::get<Error>(library.error());
  auto& source = *library.value();

  struct Resolved
  {
    bool inPlan = false;
    std::size_t index = 0;
    std::string assetId;
  };
  auto plan = std::vector<project::AssetCapture>{};
  auto resolved = std::map<std::string, Resolved>{};
  auto resolve = std::function<Result<Resolved>(const std::string&, bool)>{};

  // Adds a plan entry for one library resource. The slot is reserved before its
  // dependencies are walked so a chain always terminates, and every edge is recorded so
  // the store can rewrite the whole graph in one transaction.
  const auto addEntry = [&](
                          const std::string& id,
                          const std::optional<project::Asset>& existing,
                          const project::LibraryAsset& asset) -> Result<Resolved> {
    auto import = captureImport(source, asset, existing);
    if (import.is_error())
      return std::get<Error>(import.error());
    const auto index = plan.size();
    plan.push_back({});
    auto entry = Resolved{true, index, existing ? existing->id : std::string{}};
    resolved[id] = entry;
    auto capture = project::AssetCapture{};
    if (existing)
      capture.assetId = existing->id;
    capture.import = std::move(import).value();
    auto dependencies = source.dependencies(id);
    if (dependencies.is_error())
      return std::get<Error>(dependencies.error());
    for (const auto& dependency : dependencies.value())
    {
      auto target = resolve(dependency.dependencyId, false);
      if (target.is_error())
        return Error{
          "Cannot capture " + capture.import.displayName + ": "
          + std::get<Error>(target.error()).msg};
      capture.dependencies.push_back(project::AssetCaptureLink{
        target.value().index, target.value().assetId, dependency.kind});
    }
    plan[index] = std::move(capture);
    return entry;
  };

  resolve = [&](const std::string& id, const bool refresh) -> Result<Resolved> {
    if (const auto it = resolved.find(id); it != resolved.end())
    {
      if (it->second.inPlan || !refresh)
        return it->second;
      // Seen first as a dependency and now requested explicitly: promote it in place so
      // its edges are rebuilt without creating a second asset.
      auto asset = source.findAsset(id);
      if (asset.is_error())
        return std::get<Error>(asset.error());
      if (!asset.value() || asset.value()->missing)
        return Error{"Asset library resource is missing or inaccessible."};
      auto existing = m_store->findAsset(it->second.assetId);
      if (existing.is_error())
        return std::get<Error>(existing.error());
      return addEntry(id, existing.value(), *asset.value());
    }

    auto existing = m_store->findAssetByLibraryAsset(libraryId, id);
    if (existing.is_error())
      return std::get<Error>(existing.error());
    // A dependency that is already snapshotted is linked by its ID: a closure must never
    // rewrite a snapshot the user did not select.
    if (existing.value() && !refresh)
    {
      auto linked = Resolved{false, 0, existing.value()->id};
      resolved[id] = linked;
      return linked;
    }

    auto asset = source.findAsset(id);
    if (asset.is_error())
      return std::get<Error>(asset.error());
    if (!asset.value() || asset.value()->missing)
      return Error{"Asset library resource is missing or inaccessible."};
    return addEntry(id, existing.value(), *asset.value());
  };

  planIndices.clear();
  planIndices.reserve(libraryAssetIds.size());
  for (const auto& id : libraryAssetIds)
  {
    auto entry = resolve(id, true);
    if (entry.is_error())
      return std::get<Error>(entry.error());
    planIndices.push_back(entry.value().index);
  }
  return plan;
}

namespace
{
std::string normalizedProjectPath(std::string path)
{
  std::ranges::transform(
    path, path.begin(), [](const unsigned char c) { return char(std::tolower(c)); });
  return path;
}

// The store rejects overlapping project paths; the session uses the same rule so a
// conflict is reported before anything is written.
bool projectPathsOverlap(const std::string& left, const std::string& right)
{
  const auto a = normalizedProjectPath(left);
  const auto b = normalizedProjectPath(right);
  return a == b || a.starts_with(b + '/') || b.starts_with(a + '/');
}

// Replaces every quoted MAP value equal to `from`, case insensitively. Only quoted
// values are considered, so brush face texture tokens and comments are never touched.
std::size_t replaceQuotedValue(
  std::string& text, const std::string& from, const std::string& to)
{
  if (from.empty())
    return 0;
  auto replaced = std::size_t{0};
  auto index = std::size_t{0};
  while (index < text.size())
  {
    const auto open = text.find('"', index);
    if (open == std::string::npos)
      break;
    const auto close = text.find('"', open + 1);
    if (close == std::string::npos)
      break;
    const auto value = text.substr(open + 1, close - open - 1);
    if (normalizedProjectPath(value) == normalizedProjectPath(from))
    {
      text.replace(open + 1, close - open - 1, to);
      index = open + 1 + to.size() + 1;
      ++replaced;
    }
    else
      index = close + 1;
  }
  return replaced;
}

// Replaces the material token of brush faces and Valve patches, which MAP stores
// unquoted. A face carries exactly three plane points before its material; a patch puts
// the material on its own line followed by numeric attributes.
std::size_t replaceFaceMaterial(
  std::string& text, const std::string& from, const std::string& to)
{
  const auto key = normalizedProjectPath(from);
  const auto material = [&](const std::string& token) {
    return normalizedProjectPath(token) == key;
  };
  const auto numericAttributes = [](const std::string& attributes) {
    return !attributes.empty()
           && std::all_of(attributes.begin(), attributes.end(), [](const char c) {
                return std::isspace(static_cast<unsigned char>(c)) || std::isdigit(c)
                       || c == '.' || c == '-' || c == '+';
              });
  };

  auto replaced = std::size_t{0};
  auto lineStart = std::size_t{0};
  while (lineStart < text.size())
  {
    auto lineEnd = text.find('\n', lineStart);
    if (lineEnd == std::string::npos)
      lineEnd = text.size();
    auto line = text.substr(lineStart, lineEnd - lineStart);
    const auto nextLine = lineEnd + 1;

    auto tokenStart = std::string::npos;
    auto tokenLength = std::size_t{0};
    auto closings = std::size_t{0};
    for (auto index = std::size_t{0}; index < line.size(); ++index)
      if (line[index] == ')' && ++closings == 3)
      {
        const auto start = line.find_first_not_of(" \t", index + 1);
        if (start != std::string::npos)
        {
          const auto end = line.find_first_of(" \t", start);
          const auto length = (end == std::string::npos ? line.size() : end) - start;
          if (material(line.substr(start, length)))
          {
            tokenStart = start;
            tokenLength = length;
          }
        }
        break;
      }

    if (tokenStart == std::string::npos)
    {
      const auto start = line.find_first_not_of(" \t");
      if (
        start != std::string::npos && line[start] != '"' && line[start] != '('
        && line[start] != '{' && line[start] != '}')
      {
        const auto end = line.find_first_of(" \t", start);
        if (end != std::string::npos)
        {
          const auto token = line.substr(start, end - start);
          if (material(token) && numericAttributes(line.substr(end)))
          {
            tokenStart = start;
            tokenLength = end - start;
          }
        }
      }
    }

    if (tokenStart != std::string::npos)
    {
      line.replace(tokenStart, tokenLength, to);
      text.replace(lineStart, lineEnd - lineStart, line);
      ++replaced;
    }
    lineStart = nextLine;
  }
  return replaced;
}
} // namespace

Result<std::vector<CaptureConflict>> ProjectSession::captureConflicts(
  const std::vector<project::AssetCapture>& plan) const
{
  const auto assets = m_store->assets();
  if (assets.is_error())
    return std::get<Error>(assets.error());

  auto conflicts = std::vector<CaptureConflict>{};
  for (auto index = std::size_t{0}; index < plan.size(); ++index)
  {
    const auto& entry = plan[index];
    // Refreshing an existing asset cannot conflict with itself.
    if (entry.assetId)
      continue;
    const auto& path = entry.import.virtualPath;
    for (const auto& asset : assets.value())
      if (
        projectPathsOverlap(path, asset.virtualPath)
        || projectPathsOverlap(path, asset.publishPath))
      {
        conflicts.push_back(
          CaptureConflict{index, entry.import.displayName, path, asset});
        break;
      }
  }
  return conflicts;
}

void ProjectSession::resolveCaptureConflict(
  std::vector<project::AssetCapture>& plan,
  const CaptureConflict& conflict,
  const CaptureConflictChoice choice)
{
  if (
    choice != CaptureConflictChoice::Replace || !conflict.existing
    || conflict.planIndex >= plan.size())
    return;

  auto& entry = plan[conflict.planIndex];
  entry.assetId = conflict.existing->id;
  // Keep the project folder and engine-facing name: maps and the compiler already
  // reference them. Only the snapshot revision is replaced.
  entry.import.virtualPath = conflict.existing->virtualPath;
  entry.import.displayName = conflict.existing->displayName;
}

Result<std::optional<project::Asset>> ProjectSession::captureLibraryAssetResolving(
  const std::string& libraryId,
  const std::string& libraryAssetId,
  const CaptureConflictResolver& resolveConflict)
{
  auto indices = std::vector<std::size_t>{};
  auto plan = buildCapturePlan(libraryId, {libraryAssetId}, indices);
  if (plan.is_error())
    return std::get<Error>(plan.error());
  auto captures = std::move(plan).value();

  auto conflicts = captureConflicts(captures);
  if (conflicts.is_error())
    return std::get<Error>(conflicts.error());
  for (const auto& conflict : conflicts.value())
  {
    if (resolveConflict(conflict) == CaptureConflictChoice::Cancel)
      return std::optional<project::Asset>{};
    resolveCaptureConflict(captures, conflict, CaptureConflictChoice::Replace);
  }

  // The whole closure is committed once, so a conversion failure cannot leave a model in
  // the project without the resources it needs.
  auto captured = applyAssetCapturePlan(captures);
  if (captured.is_error())
    return std::get<Error>(captured.error());
  return std::optional<project::Asset>{captured.value().at(indices.front())};
}

Result<ProjectSession::MapReferenceCapture> ProjectSession::captureMapReferences(
  const std::string& mapAssetId, const CaptureConflictResolver& resolveConflict)
{
  const auto audit = auditGoldSrcProjectResources(*this);
  if (audit.is_error())
    return std::get<Error>(audit.error());

  const auto lowered = [](std::string value) {
    std::ranges::transform(
      value, value.begin(), [](const unsigned char c) { return char(std::tolower(c)); });
    return value;
  };

  // Keyed by engine-facing name so matching ignores case but reports the original
  // spelling from the map.
  auto pending = std::map<std::string, std::string>{};
  for (const auto& use : audit.value().uses)
    if (
      use.mapId == mapAssetId && use.status == ProjectResourceStatus::Missing
      && !use.reference.empty())
      pending.emplace(lowered(use.reference), use.reference);

  auto result = MapReferenceCapture{};
  if (pending.empty())
    return result;

  const auto connections = libraries();
  if (connections.is_error())
    return std::get<Error>(connections.error());

  struct Candidate
  {
    std::string libraryId;
    project::LibraryAsset asset;
  };
  auto candidates = std::map<std::string, std::vector<Candidate>>{};
  for (const auto& connection : connections.value())
  {
    if (!connection.available)
      continue;
    const auto library = openLibrary(connection.info.id);
    if (library.is_error())
      continue;
    const auto assets = library.value()->assets();
    if (assets.is_error())
      continue;
    for (const auto& asset : assets.value())
    {
      if (asset.missing)
        continue;
      const auto texture = asset.type == project::AssetType::Texture;
      if (
        !texture && asset.type != project::AssetType::Model
        && asset.type != project::AssetType::Sprite
        && asset.type != project::AssetType::Sound)
        continue;
      const auto addCandidate = [&](const std::string& key) {
        if (pending.contains(key))
          candidates[key].push_back({connection.info.id, asset});
      };
      if (texture)
        addCandidate(lowered(asset.displayName));
      else
      {
        const auto path = lowered(libraryPublishPath(asset));
        const auto root = std::string{publishRoot(asset.type)} + "/";
        // Index each directory suffix once. Matching cost depends on path depth,
        // rather than multiplying library size by the number of map references.
        for (auto slash = path.find('/'); slash != std::string::npos;
             slash = path.find('/', slash + 1))
          addCandidate(root + path.substr(slash + 1));
      }
    }
  }
  for (const auto& [key, reference] : pending)
  {
    const auto found = candidates.find(key);
    // Ambiguity is left for explicit capture and publish-path editing. Library order
    // must never decide which version of a resource a map receives.
    if (found == candidates.end() || found->second.size() != 1)
    {
      result.unresolved.push_back(reference);
      continue;
    }
    const auto& candidate = found->second.front();
    auto indices = std::vector<std::size_t>{};
    auto built = buildCapturePlan(candidate.libraryId, {candidate.asset.id}, indices);
    if (built.is_error())
      return std::get<Error>(built.error());
    auto plan = std::move(built).value();
    auto& requested = plan.at(indices.front());
    if (candidate.asset.type != project::AssetType::Texture)
    {
      const auto oldFolder = libraryPublishPath(candidate.asset);
      const auto slash = oldFolder.find_last_of('/');
      const auto prefix = oldFolder.substr(0, slash + 1);
      const auto targetPrefix = reference.substr(0, reference.find_last_of('/') + 1);
      // Model companions and animation groups must remain siblings after mapping a
      // categorized library folder to the path explicitly requested by the map.
      for (auto& entry : plan)
        if (!entry.assetId && entry.import.virtualPath.starts_with(prefix))
          entry.import.virtualPath =
            targetPrefix + entry.import.virtualPath.substr(prefix.size());
      if (requested.assetId && lowered(requested.import.virtualPath) != key)
      {
        result.unresolved.push_back(reference);
        continue;
      }
      requested.import.virtualPath = reference;
      auto misplacedDependency = false;
      const auto folder = [&](const std::string& path) {
        return lowered(path.substr(0, path.find_last_of('/') + 1));
      };
      for (const auto& entry : plan)
        for (const auto& dependency : entry.dependencies)
        {
          if (
            dependency.kind != "model-companion" && dependency.kind != "model-animation")
            continue;
          auto path = std::string{};
          if (dependency.assetId.empty())
            path = plan.at(dependency.target).import.virtualPath;
          else
          {
            const auto existing = m_store->findAsset(dependency.assetId);
            if (existing.is_error())
              return std::get<Error>(existing.error());
            if (existing.value())
              path = existing.value()->publishPath;
          }
          if (path.empty() || folder(path) != folder(entry.import.virtualPath))
            misplacedDependency = true;
        }
      // Existing snapshots must not be moved implicitly to satisfy a new map path.
      if (misplacedDependency)
      {
        result.unresolved.push_back(reference);
        continue;
      }
    }
    const auto conflicts = captureConflicts(plan);
    if (conflicts.is_error())
      return std::get<Error>(conflicts.error());
    auto cancelled = false;
    for (const auto& conflict : conflicts.value())
    {
      if (resolveConflict(conflict) == CaptureConflictChoice::Cancel)
      {
        cancelled = true;
        break;
      }
      resolveCaptureConflict(plan, conflict, CaptureConflictChoice::Replace);
    }
    if (cancelled)
    {
      result.unresolved.push_back(reference);
      continue;
    }
    const auto captured = applyAssetCapturePlan(plan);
    if (captured.is_error())
      return std::get<Error>(captured.error());
    result.captured.push_back(captured.value().at(indices.front()));
  }
  return result;
}

Result<ProjectSession::PublishRename> ProjectSession::renamePublishPath(
  const std::string& assetId, const std::string& newPath)
{
  const auto asset = m_store->findAsset(assetId);
  if (asset.is_error())
    return std::get<Error>(asset.error());
  if (!asset.value())
    return Error{"Project asset not found."};

  const auto& current = *asset.value();
  const auto isTexture = current.type == project::AssetType::Texture;
  if (
    !isTexture && current.type != project::AssetType::Model
    && current.type != project::AssetType::Sprite
    && current.type != project::AssetType::Sound)
    return Error{
      "Published paths can be renamed for textures, models, sprites and sounds."};

  auto normalized = newPath;
  std::ranges::replace(normalized, '\\', '/');

  // A texture is published as textures/<name>.wad, and the name inside the WAD is what
  // maps reference, so renaming has to rewrite the miptex too.
  auto oldTextureName = std::string{};
  auto newTextureName = std::string{};
  auto oldTextureWad = std::vector<std::uint8_t>{};
  auto newTextureWad = std::vector<std::uint8_t>{};
  auto oldOriginals = std::vector<std::uint8_t>{};
  if (isTexture)
  {
    if (!normalized.starts_with("textures/") || !normalized.ends_with(".wad"))
      return Error{"A texture publishes as 'textures/<name>.wad'."};
    auto content = m_store->readAsset(assetId, project::ContentKind::Compatible);
    if (content.is_error())
      return std::get<Error>(content.error());
    oldTextureWad = content.value();
    const auto archive = project::readGoldSrcWad(oldTextureWad);
    if (archive.is_error())
      return std::get<Error>(archive.error());
    if (archive.value().textures.size() != 1)
      return Error{
        "Renaming a texture requires a single-texture WAD. Split '" + current.virtualPath
        + "' into one texture per asset first."};
    const auto& entry = archive.value().textures.front();
    if (!entry.valid || entry.name.empty())
      return Error{"The texture WAD does not contain a readable miptex name."};
    oldTextureName = entry.name;
    if (entry.miptex.size() < 40)
      return Error{"The texture lump is too small to rename."};

    newTextureName =
      goldSrcTextureName(stemOf(normalized), oldTextureName.front() == '{');
    auto miptex = entry.miptex;
    // The miptex lump starts with its 16-byte name; width follows at offset 16.
    std::fill(miptex.begin(), miptex.begin() + 16, std::uint8_t{0});
    std::copy(newTextureName.begin(), newTextureName.end(), miptex.begin());
    auto wad = project::buildGoldSrcWad({miptex});
    if (wad.is_error())
      return std::get<Error>(wad.error());
    newTextureWad = wad.value();

    const auto original = m_store->readAsset(assetId, project::ContentKind::Original);
    if (original.is_error())
      return std::get<Error>(original.error());
    oldOriginals = original.value();
  }
  else
  {
    const auto root = std::string{publishRoot(current.type)};
    if (!(normalized == root || normalized.starts_with(root + "/")))
      return Error{"The new path must stay under '" + root + "/'."};
  }

  if (normalized == current.publishPath)
  {
    auto unchanged = PublishRename{};
    unchanged.asset = current;
    return unchanged;
  }

  const auto assets = m_store->assets();
  if (assets.is_error())
    return std::get<Error>(assets.error());

  // Validate everything before writing: a rejected path must not leave half a rename.
  for (const auto& other : assets.value())
    if (
      other.id != assetId
      && (projectPathsOverlap(normalized, other.virtualPath)
          || projectPathsOverlap(normalized, other.publishPath)))
      return Error{"The new path conflicts with an existing asset: " + other.virtualPath};
  for (const auto& other : assets.value())
    if (other.type == project::AssetType::Map && documentFor(other.id))
      return Error{
        "Close the map '" + other.displayName + "' before renaming a published path."};

  // Sounds are referenced relative to sound/, everything else by the name maps store.
  const auto mapForm = [&](const std::string& path) {
    return current.type == project::AssetType::Sound && path.starts_with("sound/")
             ? path.substr(std::string{"sound/"}.size())
             : path;
  };
  const auto from = isTexture ? oldTextureName : mapForm(current.publishPath);
  const auto to = isTexture ? newTextureName : mapForm(normalized);
  if (from.empty())
    return Error{"The asset has no published path yet."};

  const auto saved = checkpoint(
    "Before publish rename "
    + QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss").toStdString());
  if (saved.is_error())
    return std::get<Error>(saved.error());

  const auto writeMapText = [&](const project::Asset& map, const std::string& text) {
    auto import = project::AssetImport{};
    import.type = project::AssetType::Map;
    import.sourceName = map.sourceName;
    import.conversionRecipe = map.conversionRecipe;
    import.library = map.library;
    import.original.assign(text.begin(), text.end());
    import.compatible = import.original;
    return m_store->updateAssets({project::AssetUpdate{map.id, std::move(import)}});
  };

  auto rewritten = std::vector<std::pair<std::string, std::string>>{};
  auto result = PublishRename{};
  result.checkpointId = saved.value().id;
  for (const auto& map : assets.value())
  {
    if (map.type != project::AssetType::Map)
      continue;
    auto content = m_store->readAsset(map.id, project::ContentKind::Compatible);
    if (content.is_error())
      return std::get<Error>(content.error());
    const auto originalText = std::string{content.value().begin(), content.value().end()};
    auto text = originalText;
    auto replaced = replaceQuotedValue(text, from, to);
    if (isTexture)
      replaced += replaceFaceMaterial(text, from, to);
    if (replaced == 0)
      continue;
    if (const auto written = writeMapText(map, text); written.is_error())
      return std::get<Error>(written.error());
    rewritten.emplace_back(map.id, originalText);
    ++result.mapsChanged;
    result.referencesChanged += replaced;
  }

  const auto restoreMaps = [&]() -> Result<void> {
    for (const auto& [mapId, text] : rewritten)
    {
      const auto map = m_store->findAsset(mapId);
      if (map.is_error())
        return std::get<Error>(map.error());
      if (!map.value())
        return Error{"Cannot restore a rewritten map: " + mapId};
      if (const auto restored = writeMapText(*map.value(), text); restored.is_error())
        return std::get<Error>(restored.error());
    }
    return {};
  };
  const auto writeAssetContent = [&](
                                   const std::vector<std::uint8_t>& original,
                                   const std::vector<std::uint8_t>& compatible) {
    auto import = project::AssetImport{};
    import.type = current.type;
    import.sourceName = current.sourceName;
    import.conversionRecipe = current.conversionRecipe;
    import.library = current.library;
    import.original = original;
    import.compatible = compatible;
    return m_store->updateAssets({project::AssetUpdate{assetId, std::move(import)}});
  };

  if (isTexture)
    if (const auto written = writeAssetContent(oldOriginals, newTextureWad);
        written.is_error())
    {
      if (const auto restored = restoreMaps(); restored.is_error())
        return Error{
          std::get<Error>(written.error()).msg
          + "; restoring rewritten maps also failed: "
          + std::get<Error>(restored.error()).msg};
      return std::get<Error>(written.error());
    }

  const auto published = m_store->setPublishPath(assetId, normalized);
  if (published.is_error())
  {
    // A rejected path must leave every map and the texture content as they were.
    if (const auto restored = restoreMaps(); restored.is_error())
      return Error{
        std::get<Error>(published.error()).msg
        + "; restoring rewritten maps also failed: "
        + std::get<Error>(restored.error()).msg};
    if (isTexture)
      if (const auto restored = writeAssetContent(oldOriginals, oldTextureWad);
          restored.is_error())
        return Error{
          std::get<Error>(published.error()).msg + "; restoring the texture also failed: "
          + std::get<Error>(restored.error()).msg};
    return std::get<Error>(published.error());
  }

  result.asset = published.value();
  changed();
  emit contentsChanged();
  emit stateChanged();
  return result;
}

Result<project::Asset> ProjectSession::captureLibraryAsset(
  const std::string& libraryId, const std::string& libraryAssetId)
{
  auto indices = std::vector<std::size_t>{};
  auto plan = buildCapturePlan(libraryId, {libraryAssetId}, indices);
  if (plan.is_error())
    return std::get<Error>(plan.error());

  if (const auto conflicts = captureConflicts(plan.value());
      conflicts.is_success() && !conflicts.value().empty())
  {
    return Error{
      "The project already publishes the name '" + conflicts.value().front().path
      + "'. Use the capture conflict prompt to replace it, or rename the library "
        "resource."};
  }

  // The whole closure is committed once, so a conversion failure cannot leave a model in
  // the project without the resources it needs.
  auto captured = applyAssetCapturePlan(plan.value());
  if (captured.is_error())
    return std::get<Error>(captured.error());
  return captured.value().at(indices.front());
}
Result<std::vector<project::Asset>> ProjectSession::applyAssetCapturePlan(
  const std::vector<project::AssetCapture>& plan)
{
  auto captured = m_store->captureAssets(plan);
  if (captured.is_error())
    return std::get<Error>(captured.error());
  changed();
  emit libraryLinksChanged();
  return captured;
}
Result<std::optional<project::Asset>> ProjectSession::projectAssetForLibraryAsset(
  const std::string& libraryId, const std::string& libraryAssetId) const
{
  return m_store->findAssetByLibraryAsset(libraryId, libraryAssetId);
}

Result<std::filesystem::path> ProjectSession::libraryAssetFile(
  const std::string& libraryId, const std::string& libraryAssetId)
{
  auto library = openLibrary(libraryId);
  if (library.is_error())
    return std::get<Error>(library.error());
  auto asset = library.value()->findAsset(libraryAssetId);
  if (asset.is_error())
    return std::get<Error>(asset.error());
  if (!asset.value())
    return Error{"Asset library resource not found."};
  return materializeLibraryAsset(*library.value(), *asset.value());
}

Result<std::map<std::string, std::filesystem::path>> ProjectSession::libraryAssetFiles(
  const std::string& libraryId, const std::vector<std::string>& libraryAssetIds)
{
  auto library = openLibrary(libraryId);
  if (library.is_error())
    return std::get<Error>(library.error());

  // A browser can resolve a visible page in one batch without reopening the catalog for
  // every resource.
  auto files = std::map<std::string, std::filesystem::path>{};
  for (const auto& libraryAssetId : libraryAssetIds)
  {
    const auto asset = library.value()->findAsset(libraryAssetId);
    if (asset.is_error() || !asset.value())
      continue;
    const auto file = materializeLibraryAsset(*library.value(), *asset.value());
    if (file.is_success())
      files.emplace(libraryAssetId, file.value());
  }
  return files;
}

Result<std::filesystem::path> ProjectSession::materializeLibraryAsset(
  const project::AssetLibrary& library, const project::LibraryAsset& entry) const
{
  const auto& libraryId = library.info().id;
  if (entry.type == project::AssetType::Model)
  {
    auto closure = std::map<std::string, project::LibraryAsset>{};
    auto pending = std::vector<std::string>{entry.id};
    while (!pending.empty())
    {
      const auto id = pending.back();
      pending.pop_back();
      if (closure.contains(id))
        continue;
      const auto current = library.findAsset(id);
      if (current.is_error())
        return std::get<Error>(current.error());
      if (!current.value())
        return Error{"Model preview dependency is missing."};
      closure.emplace(id, *current.value());
      const auto edges = library.dependencies(id);
      if (edges.is_error())
        return std::get<Error>(edges.error());
      for (const auto& edge : edges.value())
        pending.push_back(edge.dependencyId);
    }
    auto signature = QByteArray{};
    for (const auto& [id, dependency] : closure)
      signature += QByteArray::fromStdString(
        id + ":" + std::to_string(dependency.revision) + ":" + dependency.virtualPath
        + "\n");
    const auto digest =
      QCryptographicHash::hash(signature, QCryptographicHash::Sha256).toHex();
    const auto root = pathFromQString(m_workspace.path()) / "library-model-preview"
                      / libraryId / entry.id / digest.toStdString();
    for (const auto& [id, dependency] : closure)
    {
      const auto relative =
        pathFromQString(QString::fromStdString(dependency.virtualPath));
      if (relative.empty() || relative.has_root_path())
        return Error{"Invalid model preview path."};
      for (const auto& part : relative)
        if (part == "..")
          return Error{"Invalid model preview path."};
      const auto destination = root / relative;
      if (QFile::exists(pathAsQString(destination)))
        continue;
      const auto bytes = library.readContent(id, dependency.revision);
      if (bytes.is_error())
        return std::get<Error>(bytes.error());
      const auto written = writeBytes(destination, bytes.value());
      if (written.is_error())
        return std::get<Error>(written.error());
    }
    return root / pathFromQString(QString::fromStdString(entry.virtualPath));
  }
  const auto extension =
    entry.type == project::AssetType::Texture
      ? std::string{".wad"}
      : std::filesystem::path{entry.virtualPath}.extension().string();
  // The revision is part of the cache path, so a newer library revision can never be
  // previewed from a stale file.
  const auto path = pathFromQString(m_workspace.path()) / "library-preview" / libraryId
                    / entry.id / std::to_string(entry.revision)
                    / (entry.displayName + extension);
  if (QFile::exists(pathAsQString(path)))
    return path;

  auto content = library.readContent(entry.id);
  if (content.is_error())
    return std::get<Error>(content.error());
  auto compatible = project::compatibleLibraryContent(entry, content.value());
  if (compatible.is_error())
    return std::get<Error>(compatible.error());
  return writeBytes(path, compatible.value()) | kdl::transform([&]() { return path; });
}

namespace
{
// A snapshot is stale when the library content moved on, when a dependency it declares is
// not captured yet, or when an edge it no longer declares survived an earlier build.
Result<bool> snapshotNeedsRefresh(
  project::ProjectStore& store,
  const std::string& libraryId,
  const std::string& libraryAssetId,
  const project::Asset& projectAsset,
  project::AssetLibrary& library,
  const project::LibraryAsset& libraryAsset)
{
  // A conversion publishes a new library revision without changing the source hash, so
  // the revision is part of the refresh decision.
  if (
    libraryAsset.contentHash != projectAsset.library.libraryContentHash
    || libraryAsset.revision != projectAsset.library.libraryRevision)
    return true;
  auto expected = std::set<std::pair<std::string, std::string>>{};
  const auto dependencies = library.dependencies(libraryAssetId);
  if (dependencies.is_error())
    return std::get<Error>(dependencies.error());
  for (const auto& dependency : dependencies.value())
  {
    const auto captured =
      store.findAssetByLibraryAsset(libraryId, dependency.dependencyId);
    if (captured.is_error())
      return std::get<Error>(captured.error());
    if (!captured.value())
      return true;
    expected.emplace(captured.value()->id, dependency.kind);
  }
  const auto edges = store.dependencies(projectAsset.id);
  if (edges.is_error())
    return std::get<Error>(edges.error());
  for (const auto& edge : edges.value())
    if (!expected.contains({edge.dependencyId, edge.kind}))
      return true;
  return expected.size() != edges.value().size();
}
} // namespace

Result<std::vector<project::LibraryUpdate>> ProjectSession::checkLibraryUpdates()
{
  const auto linked = m_store->libraryLinkedAssets();
  if (linked.is_error())
    return std::get<Error>(linked.error());
  const auto& assets = linked.value();

  auto states = std::vector<project::LibraryState>{};
  auto visited = std::vector<std::string>{};
  for (const auto& asset : assets)
  {
    if (std::ranges::find(visited, asset.library.libraryId) != visited.end())
      continue;
    visited.push_back(asset.library.libraryId);
    auto state = libraryState(asset.library.libraryId);
    if (state.is_error())
      return std::get<Error>(state.error());
    states.push_back(std::move(state).value());
  }

  auto ignores = std::map<std::string, std::vector<std::string>>{};
  for (const auto& asset : assets)
  {
    auto ignored = m_store->ignoredLibraryUpdates(asset.id);
    if (ignored.is_error())
      return std::get<Error>(ignored.error());
    if (!ignored.value().empty())
      ignores.emplace(asset.id, std::move(ignored).value());
  }
  auto updates = project::evaluateLibraryUpdates(assets, states, ignores);
  // Content can be current while the dependency graph is not: the library may have added
  // or removed a companion the snapshot must follow. That is still an update the user has
  // to see, so it is promoted here instead of hiding behind "up to date".
  // Every snapshot of one library is reviewed against the same catalog, so the pass opens
  // each library once instead of once per snapshot.
  auto opened = std::map<std::string, std::unique_ptr<project::AssetLibrary>>{};
  for (auto& update : updates)
  {
    if (update.state != project::LibraryUpdateState::UpToDate)
      continue;
    const auto asset = m_store->findAsset(update.projectAssetId);
    if (asset.is_error() || !asset.value())
      continue;
    auto hit = opened.find(update.link.libraryId);
    if (hit == opened.end())
    {
      auto library = openLibrary(update.link.libraryId);
      if (library.is_error())
        continue;
      hit = opened.emplace(update.link.libraryId, std::move(library).value()).first;
    }
    const auto current = hit->second->findAsset(update.link.libraryAssetId);
    if (current.is_error() || !current.value() || current.value()->missing)
      continue;
    const auto needsRefresh = snapshotNeedsRefresh(
      *m_store,
      update.link.libraryId,
      update.link.libraryAssetId,
      *asset.value(),
      *hit->second,
      *current.value());
    if (needsRefresh.is_success() && needsRefresh.value())
      update.state = project::LibraryUpdateState::DependenciesChanged;
  }
  return updates;
}

Result<std::vector<project::Asset>> ProjectSession::updateLibraryAssets(
  const std::vector<std::string>& projectAssetIds)
{
  // Requests are grouped per library so each plan can be built against one catalog, then
  // merged into a single transaction: one stale resource must not leave the others
  // updated.
  auto requests = std::map<std::string, std::vector<std::string>>{};
  for (const auto& id : projectAssetIds)
  {
    auto asset = m_store->findAsset(id);
    if (asset.is_error())
      return std::get<Error>(asset.error());
    if (!asset.value())
      return Error{"Project asset not found."};
    if (asset.value()->library.empty())
      return Error{"Asset was not captured from an asset library."};

    const auto& link = asset.value()->library;
    auto library = openLibrary(link.libraryId);
    if (library.is_error())
      return std::get<Error>(library.error());
    auto current = library.value()->findAsset(link.libraryAssetId);
    if (current.is_error())
      return std::get<Error>(current.error());
    if (!current.value() || current.value()->missing)
      return Error{
        "Asset library resource '" + asset.value()->displayName
        + "' is missing or inaccessible; the project keeps its current snapshot."};
    auto needsRefresh = snapshotNeedsRefresh(
      *m_store,
      link.libraryId,
      link.libraryAssetId,
      *asset.value(),
      *library.value(),
      *current.value());
    if (needsRefresh.is_error())
      return std::get<Error>(needsRefresh.error());
    if (!needsRefresh.value())
      continue;
    requests[link.libraryId].push_back(link.libraryAssetId);
  }

  auto plan = std::vector<project::AssetCapture>{};
  auto roots = std::vector<std::size_t>{};
  for (const auto& [libraryId, libraryAssetIds] : requests)
  {
    auto indices = std::vector<std::size_t>{};
    auto libraryPlan = buildCapturePlan(libraryId, libraryAssetIds, indices);
    if (libraryPlan.is_error())
      return std::get<Error>(libraryPlan.error());
    const auto base = plan.size();
    auto entries = libraryPlan.value();
    for (auto& capture : entries)
    {
      for (auto& link : capture.dependencies)
      {
        // Links inside one library plan are index based; links to an already snapshotted
        // dependency carry an explicit asset ID and stay untouched.
        if (link.assetId.empty())
          link.target += base;
      }
      plan.push_back(std::move(capture));
    }
    for (const auto index : indices)
      roots.push_back(base + index);
  }
  if (plan.empty())
    return std::vector<project::Asset>{};

  auto captured = m_store->captureAssets(plan);
  if (captured.is_error())
    return std::get<Error>(captured.error());
  changed();
  emit libraryLinksChanged();
  auto result = std::vector<project::Asset>{};
  result.reserve(roots.size());
  for (const auto root : roots)
    result.push_back(captured.value().at(root));
  return result;
}
Result<void> ProjectSession::ignoreLibraryUpdate(
  const std::string& projectAssetId, const std::string& libraryHash)
{
  return m_store->ignoreLibraryUpdate(projectAssetId, libraryHash)
         | kdl::transform([&]() { emit libraryLinksChanged(); });
}

void ProjectSession::deferLibraryUpdates()
{
  if (m_libraryUpdatesDeferred)
    return;
  m_libraryUpdatesDeferred = true;
  emit stateChanged();
}

bool ProjectSession::libraryUpdatesDeferred() const
{
  return m_libraryUpdatesDeferred;
}

Result<project::Asset> ProjectSession::restoreAssetRevision(
  const std::string& projectAssetId, const std::uint64_t revision)
{
  auto asset = m_store->findAsset(projectAssetId);
  if (asset.is_error())
    return std::get<Error>(asset.error());
  if (!asset.value())
    return Error{"Project asset not found."};
  auto target = *asset.value();
  target.revision = revision;
  auto selected = m_store->selectRevisions({target});
  if (selected.is_error())
    return std::get<Error>(selected.error());
  changed();
  emit libraryLinksChanged();
  auto restored = m_store->findAsset(projectAssetId);
  if (restored.is_error())
    return std::get<Error>(restored.error());
  if (!restored.value())
    return Error{"Project asset not found."};
  return *restored.value();
}
} // namespace tb::ui
