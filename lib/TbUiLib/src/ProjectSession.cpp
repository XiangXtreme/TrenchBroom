/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/ProjectSession.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QSaveFile>

#include "base/Uuid.h"
#include "mdl/GameFileSystem.h"
#include "mdl/Map.h"
#include "ui/MapDocument.h"
#include "ui/QPathUtils.h"

#include <algorithm>

namespace tb::ui
{
namespace
{
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
             return asset.type == project::AssetType::Map;
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
} // namespace tb::ui
