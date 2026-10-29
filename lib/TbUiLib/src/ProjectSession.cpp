/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/ProjectSession.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>

#include "base/Uuid.h"
#include "img/DecodeImage.h"
#include "img/GoldSrcTexture.h"
#include "mdl/GameFileSystem.h"
#include "mdl/Map.h"
#include "project/GoldSrcWad.h"
#include "project/ProjectStore.h"
#include "ui/MapDocument.h"
#include "ui/ProjectResourceAudit.h"
#include "ui/QPathUtils.h"

#include <fmt/ranges.h>

#include <algorithm>
#include <functional>
#include <set>

namespace tb::ui
{
namespace
{
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
  return root + "/" + fileNameOf(asset.virtualPath);
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
  return m_store->removeAsset(id) | kdl::transform([&]() { changed(); });
}
Result<void> ProjectSession::exportCompileSnapshot(
  const std::string& mapAssetId, const std::filesystem::path& folder)
{
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

  // A snapshot exists to be compiled without asking questions later, so anything the
  // audit could not resolve is reported now instead of producing a broken BSP.
  auto problems = std::vector<std::string>{};
  auto externals = std::vector<std::string>{};
  auto directAssets = std::vector<std::string>{};
  auto directTextures = std::map<std::string, std::vector<std::string>>{};
  // The compiler recognizes these by name and never embeds them, so a missing entry says
  // nothing about the completeness of the project.
  const auto isToolTexture = [](const std::string& name) {
    static const auto names = std::set<std::string>{
      "aaatrigger",
      "bevel",
      "clip",
      "hint",
      "hintskip",
      "light",
      "null",
      "origin",
      "skip",
      "split",
      "trigger",
      "waterskip"};
    return names.contains(normalizedTextureName(name));
  };
  // Skybox faces are loaded from the game's gfx/env folder, never from a WAD, so a
  // snapshot can list them but cannot embed them.
  const auto isEngineExternal = [](const std::string& reference) {
    return normalizedTextureName(reference).starts_with("gfx/env/");
  };
  for (const auto& use : audit.value().uses)
  {
    if (use.mapId != mapAssetId)
      continue;
    const auto where = fmt::format("{} ({}:{})", use.reference, use.mapPath, use.line);
    switch (use.status)
    {
    case ProjectResourceStatus::Embedded:
      for (const auto& id : use.assetIds)
      {
        if (std::ranges::find(directAssets, id) == directAssets.end())
          directAssets.push_back(id);
        directTextures[id].push_back(use.reference);
      }
      break;
    case ProjectResourceStatus::Unsupported:
      // Sentence and decal lookups are not project resources; the engine resolves them.
      break;
    case ProjectResourceStatus::Missing:
      if (isToolTexture(use.reference) || isEngineExternal(use.reference))
        externals.push_back(use.reference);
      else
        problems.push_back(where + " is not embedded in the project");
      break;
    case ProjectResourceStatus::Ambiguous:
      problems.push_back(where + " matches more than one project asset");
      break;
    case ProjectResourceStatus::InvalidPath:
      problems.push_back(where + " is not a usable game path");
      break;
    case ProjectResourceStatus::ScanError:
      problems.push_back(where + " could not be scanned");
      break;
    }
  }
  if (!problems.empty())
  {
    constexpr auto MaxProblems = std::size_t{12};
    auto text = std::string{};
    const auto count = std::min(problems.size(), MaxProblems);
    for (auto index = std::size_t{0}; index < count; ++index)
      text += "\n  " + problems[index];
    if (problems.size() > count)
      text += fmt::format("\n  ... and {} more", problems.size() - count);
    return Error{
      "The compile snapshot would be incomplete or ambiguous:" + text
      + "\nCapture the missing resources into the project first."};
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

  auto miptexByName = std::map<std::string, std::vector<std::uint8_t>>{};
  auto resources = std::vector<std::pair<std::string, std::vector<std::uint8_t>>>{};
  auto metadata = std::vector<std::string>{
    "# TB compile snapshot",
    "project=" + m_store->info().id,
    "map=" + mapAssetId,
    "mapRevision=" + std::to_string(map.value()->revision)};
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

  if (const auto result = write(folder / "map.map", contents.value()); result.is_error())
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
           for (const auto& [document, entry] : m_documents)
           {
             document->map().gameFileSystem().setProjectDirectory(root);
             document->map().reloadEntityModels(modelPaths);
             document->map().materialCollectionsWillChangeNotifier();
             document->map().materialCollectionsDidChangeNotifier();
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

void ProjectSession::bindDocument(MapDocument& document)
{
  document.map().gameFileSystem().setProjectDirectory(pathFromQString(m_runtime->path()));
  auto modelPaths = std::vector<std::filesystem::path>{};
  for (const auto& asset : m_runtimeAssets)
    if (
      asset.type == project::AssetType::Model || asset.type == project::AssetType::Sprite)
      modelPaths.emplace_back(asset.publishPath);
  document.map().reloadEntityModels(modelPaths);
  document.map().materialCollectionsWillChangeNotifier();
  document.map().materialCollectionsDidChangeNotifier();
  auto weak = weak_from_this();
  document.map().setSaveHandler([weak]() -> Result<void> {
    if (const auto session = weak.lock())
      return session->save();
    return Error{"Project session has closed."};
  });
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
  document.map().setSaveHandler({});
  m_documents.erase(&document);
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

Result<void> ProjectSession::save()
{
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
  const auto result =
    stageMaps() | kdl::and_then([&]() { return writeProject(path, false); });
  const auto reset = unstageMaps();
  return reset.is_error() ? reset : result;
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
  if (hasOpenMaps())
    return Error{"Close project maps before restoring a version."};
  return m_store->restore(id) | kdl::transform([&](auto) { changed(); });
}

Result<std::unique_ptr<project::AssetLibrary>> ProjectSession::openLibrary(
  const std::string& libraryId) const
{
  const auto libraries = m_store->libraries();
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
    auto created = project::AssetLibrary::create(
      folder, name.empty() ? std::string{"Asset Library"} : name);
    if (created.is_error())
      return std::get<Error>(created.error());
    library = std::move(created).value();
    summary.created = true;
    auto scan = library->scan();
    if (scan.is_error())
      return std::get<Error>(scan.error());
    summary.invalid = scan.value().invalidEntries;
    if (!scan.value().warning.empty())
      summary.warnings.push_back(scan.value().warning);
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

Result<std::vector<ProjectSession::LibraryConnection>> ProjectSession::libraries() const
{
  const auto infos = m_store->libraries();
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
  const std::string& libraryId, const bool rescan) const
{
  // Unavailable libraries are a normal state, not an error: the project must keep working
  // and reporting its own snapshot.
  auto state = project::LibraryState{libraryId, {}, {}, false, {}, {}};
  const auto infos = m_store->libraries();
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
  if (rescan)
  {
    const auto scan = library.value()->scan();
    if (scan.is_error())
    {
      state.error = std::get<Error>(scan.error()).msg;
      return state;
    }
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

  if (project::libraryAssetNeedsConversion(asset))
  {
    auto decoded = img::decodeImage(native.value().data(), native.value().size());
    if (decoded.is_error())
      return std::get<Error>(decoded.error());
    const auto& image = decoded.value();
    // Keep the texture name of an existing snapshot so map faces keep resolving.
    const auto name = existing ? stemOf(existing->publishPath)
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

Result<project::Asset> ProjectSession::captureLibraryAsset(
  const std::string& libraryId, const std::string& libraryAssetId)
{
  auto indices = std::vector<std::size_t>{};
  auto plan = buildCapturePlan(libraryId, {libraryAssetId}, indices);
  if (plan.is_error())
    return std::get<Error>(plan.error());

  // The whole closure is committed once, so a conversion failure cannot leave a model in
  // the project without the resources it needs.
  auto captured = m_store->captureAssets(plan.value());
  if (captured.is_error())
    return std::get<Error>(captured.error());
  changed();
  emit libraryLinksChanged();
  return captured.value().at(indices.front());
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

  const auto& entry = *asset.value();
  const auto extension =
    entry.type == project::AssetType::Texture
      ? std::string{".wad"}
      : std::filesystem::path{entry.virtualPath}.extension().string();
  // The revision is part of the cache path, so a newer library revision can never be
  // previewed from a stale file.
  const auto path = pathFromQString(m_workspace.path()) / "library-preview" / libraryId
                    / libraryAssetId / std::to_string(entry.revision)
                    / (entry.displayName + extension);
  if (QFile::exists(pathAsQString(path)))
    return path;

  auto content = library.value()->readContent(libraryAssetId);
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
  if (libraryAsset.contentHash != projectAsset.library.libraryContentHash)
    return true;
  auto expected = std::map<std::string, std::string>{};
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
    expected[captured.value()->id] = dependency.kind;
  }
  const auto edges = store.dependencies(projectAsset.id);
  if (edges.is_error())
    return std::get<Error>(edges.error());
  for (const auto& edge : edges.value())
    if (!expected.contains(edge.dependencyId))
      return true;
  return expected.size() != edges.value().size();
}
} // namespace

Result<std::vector<project::LibraryUpdate>> ProjectSession::checkLibraryUpdates(
  const bool rescan)
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
    auto state = libraryState(asset.library.libraryId, rescan);
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
  for (auto& update : updates)
  {
    if (update.state != project::LibraryUpdateState::UpToDate)
      continue;
    const auto asset = m_store->findAsset(update.projectAssetId);
    if (asset.is_error() || !asset.value())
      continue;
    auto library = openLibrary(update.link.libraryId);
    if (library.is_error())
      continue;
    const auto current = library.value()->findAsset(update.link.libraryAssetId);
    if (current.is_error() || !current.value() || current.value()->missing)
      continue;
    const auto needsRefresh = snapshotNeedsRefresh(
      *m_store,
      update.link.libraryId,
      update.link.libraryAssetId,
      *asset.value(),
      *library.value(),
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
