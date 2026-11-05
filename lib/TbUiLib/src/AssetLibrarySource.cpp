/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom, licensed under GPL version 3 or later.
*/
#include "ui/AssetLibrarySource.h"

#include <QObject>

#include "project/AssetLibrary.h"
#include "project/GoldSrcModel.h"
#include "project/ProjectStore.h"
#include "ui/AssetLibraryImport.h"
#include "ui/ProjectSession.h"
#include "ui/QPathUtils.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace tb::ui
{
namespace
{
template <typename Asset, typename ReadDependencies>
std::vector<Asset> browsableAssets(
  std::vector<Asset> assets, const ReadDependencies& readDependencies)
{
  auto hidden = std::unordered_set<std::string>{};
  for (const auto& asset : assets)
  {
    if (asset.type != project::AssetType::Skybox)
      continue;
    const auto dependencies = readDependencies(asset.id);
    if (dependencies.is_success())
      for (const auto& edge : dependencies.value())
        if (edge.kind == "skybox-face")
          hidden.insert(edge.dependencyId);
  }
  std::erase_if(assets, [&](const auto& asset) { return hidden.contains(asset.id); });
  return assets;
}

template <typename Asset, typename ReadDependencies>
Result<AssetPreviewKeys> previewKeys(
  const std::vector<Asset>& assets,
  const std::string& scope,
  const ReadDependencies& readDependencies)
{
  auto revisions = std::unordered_map<std::string, std::string>{};
  for (const auto& asset : assets)
  {
    auto revision = "/" + std::to_string(asset.revision) + "/" + asset.contentHash;
    if constexpr (requires { asset.missing; })
      revision += asset.missing ? "/missing" : "/available";
    revisions.emplace(asset.id, std::move(revision));
  }
  auto keys = AssetPreviewKeys{};
  for (const auto& asset : assets)
  {
    auto key = scope + "/" + asset.id + revisions.at(asset.id);
    if (
      asset.type == project::AssetType::Model || asset.type == project::AssetType::Skybox
      || asset.type == project::AssetType::Material)
    {
      const auto edges = readDependencies(asset.id);
      if (edges.is_error())
        return std::get<Error>(edges.error());
      for (const auto& edge : edges.value())
      {
        key += "/" + edge.kind + "/" + edge.dependencyId;
        if (const auto revision = revisions.find(edge.dependencyId);
            revision != revisions.end())
          key += revision->second;
      }
    }
    keys.emplace(asset.id, QString::fromStdString(key));
  }
  return keys;
}

/**
 * Copies the texture header and animation group files that live beside a GoldSrc model.
 *
 * Importing a lone .mdl used to leave the library with an untextured, bind-pose-only
 * preview. The engine expects these files to be siblings, so importing the model brings
 * its siblings along; existing files and non-MDL sources are ignored.
 */
void importModelSiblings(
  project::AssetLibrary& library,
  const std::filesystem::path& source,
  const std::string& relativePath,
  const bool overwrite)
{
  if (librarySubfolderForFile(source) != "models")
    return;
  const auto& libraryFolder = library.folder();
  auto stream = std::ifstream{source, std::ios::binary};
  if (!stream)
    return;
  // Dependency discovery needs only studiohdr_t, regardless of the source's size.
  auto header = std::array<char, 244>{};
  if (!stream.read(header.data(), std::streamsize(header.size())))
    return;
  const auto dependencies = project::readGoldSrcModelDependencies(
    std::vector<std::uint8_t>{header.begin(), header.end()});
  if (dependencies.is_error())
    return;

  auto names =
    project::modelAnimationGroupNames(relativePath, dependencies.value().sequenceGroups);
  if (const auto companion = project::modelCompanionName(
        std::filesystem::path{relativePath}.filename().string());
      !companion.empty())
    names.push_back(companion);

  const auto relative = std::filesystem::path{relativePath};
  const auto sourceFolder = source.parent_path();
  for (const auto& name : names)
  {
    const auto file = std::filesystem::path{name}.filename();
    if (file.empty())
      continue;
    const auto sourcePath = sourceFolder / file;
    auto error = std::error_code{};
    if (!std::filesystem::exists(sourcePath, error) || error)
      continue;
    const auto targetRelative = (relative.parent_path() / file).generic_string();
    if (!overwrite && std::filesystem::exists(libraryFolder / targetRelative, error))
      continue;
    if (importFileIntoLibrary(libraryFolder, sourcePath, targetRelative, overwrite)
          .is_success())
      (void)library.clearRemovedSource(targetRelative);
  }
}
} // namespace

Result<std::vector<project::Asset>> browsableProjectAssets(
  const project::ProjectStore& store)
{
  auto assets = store.assets();
  if (assets.is_error())
    return std::get<Error>(assets.error());
  return browsableAssets(
    std::move(assets).value(), [&](const auto& id) { return store.dependencies(id); });
}

Result<AssetPreviewKeys> assetPreviewKeys(const AssetLibrarySource& source)
{
  const auto assets = source.allAssets();
  if (assets.is_error())
    return std::get<Error>(assets.error());
  return previewKeys(assets.value(), source.libraryId(), [&](const auto& id) {
    return source.dependencies(id);
  });
}

Result<AssetPreviewKeys> assetPreviewKeys(const project::ProjectStore& store)
{
  const auto assets = store.assets();
  if (assets.is_error())
    return std::get<Error>(assets.error());
  return previewKeys(assets.value(), store.info().id, [&](const auto& id) {
    return store.dependencies(id);
  });
}

void loadBrowserAssetMetadata(
  std::vector<BrowserAsset>& assets, const ProjectSession& session)
{
  const auto connections = session.libraries();
  if (connections.is_error())
    return;
  for (const auto& connection : connections.value())
  {
    if (!connection.available)
      continue;
    auto source = openLocalAssetLibrary(connection.info.path);
    if (!source || source->libraryId() != connection.info.id)
      continue;
    for (auto& asset : assets)
    {
      if (asset.libraryId != source->libraryId() || asset.libraryAssetId.empty())
        continue;
      const auto metadata = source->metadata(asset.libraryAssetId);
      if (metadata.is_success())
        asset.libraryMetadata = metadata.value();
    }
  }
}
AssetLibraryChanges& AssetLibraryChanges::instance()
{
  static auto changes = AssetLibraryChanges{};
  return changes;
}

Result<std::optional<project::LibraryAsset>> AssetLibrarySource::findAsset(
  const std::string& assetId) const
{
  const auto entries = allAssets();
  if (entries.is_error())
    return std::get<Error>(entries.error());
  const auto found =
    std::ranges::find(entries.value(), assetId, &project::LibraryAsset::id);
  return found == entries.value().end() ? std::nullopt : std::optional{*found};
}

namespace
{

class LocalAssetLibrarySource : public AssetLibrarySource
{
  std::unique_ptr<project::AssetLibrary> m_library;

public:
  explicit LocalAssetLibrarySource(std::unique_ptr<project::AssetLibrary> library)
    : m_library{std::move(library)}
  {
  }

  QString name() const override { return QString::fromStdString(m_library->info().name); }

  std::string libraryId() const override { return m_library->info().id; }

  QString location() const override { return pathAsQString(m_library->folder()); }

  bool isAvailable() const override { return true; }

  Result<std::vector<project::LibraryAsset>> assets() const override
  {
    auto assets = m_library->assets();
    if (assets.is_error())
      return assets;

    return browsableAssets(std::move(assets).value(), [&](const auto& id) {
      return m_library->dependencies(id);
    });
  }

  Result<std::vector<project::LibraryAsset>> allAssets() const override
  {
    return m_library->assets();
  }

  Result<std::optional<project::LibraryAsset>> findAsset(
    const std::string& assetId) const override
  {
    return m_library->findAsset(assetId);
  }

  Result<std::vector<std::uint8_t>> readOriginal(
    const std::string& assetId) const override
  {
    return m_library->readOriginal(assetId);
  }

  Result<std::vector<std::uint8_t>> readContent(const std::string& assetId) const override
  {
    return m_library->readContent(assetId);
  }

  Result<std::vector<project::AssetDependency>> dependencies(
    const std::string& assetId) const override
  {
    return m_library->dependencies(assetId);
  }

  bool canImport() const override { return true; }

  bool canConvert() const override { return true; }

  bool canReveal() const override { return true; }

  Result<void> rescan() override
  {
    const auto scanned = m_library->scan();
    if (scanned.is_error())
      return std::get<Error>(scanned.error());
    emit AssetLibraryChanges::instance().changed(QString::fromStdString(libraryId()));
    return {};
  }

  Result<std::string> importFile(
    const std::filesystem::path& source, const std::string& relativePath) override
  {
    auto imported = importFileIntoLibrary(m_library->folder(), source, relativePath);
    if (imported.is_error())
      return imported;
    // Importing a source is an explicit restore: a previously removed asset may return.
    if (auto cleared = m_library->clearRemovedSource(relativePath); cleared.is_error())
      return std::get<Error>(cleared.error());
    importModelSiblings(*m_library, source, relativePath, false);
    return imported;
  }

  Result<std::string> replaceFile(
    const std::filesystem::path& source, const std::string& relativePath) override
  {
    auto imported =
      importFileIntoLibrary(m_library->folder(), source, relativePath, true);
    if (imported.is_error())
      return imported;
    if (auto cleared = m_library->clearRemovedSource(relativePath); cleared.is_error())
      return std::get<Error>(cleared.error());
    importModelSiblings(*m_library, source, relativePath, true);
    return imported;
  }

  Result<bool> convertAsset(const std::string& assetId) override
  {
    auto result = convertLibraryImage(*m_library, assetId);
    if (result.is_success() && result.value())
      emit AssetLibraryChanges::instance().changed(QString::fromStdString(libraryId()));
    return result;
  }

  bool canManage() const override { return true; }

  Result<Metadata> metadata(const std::string& assetId) override
  {
    const auto meta = m_library->metadata(assetId);
    if (meta.is_error())
      return std::get<Error>(meta.error());
    return Metadata{meta.value().favorite, meta.value().tags};
  }

  Result<void> setFavorite(const std::string& assetId, const bool favorite) override
  {
    auto result = m_library->setFavorite(assetId, favorite);
    if (result.is_success())
      emit AssetLibraryChanges::instance().changed(QString::fromStdString(libraryId()));
    return result;
  }

  Result<void> renameAsset(const std::string& assetId, const std::string& name) override
  {
    const auto renamed = m_library->setDisplayName(assetId, name);
    if (renamed.is_error())
      return std::get<Error>(renamed.error());
    emit AssetLibraryChanges::instance().changed(QString::fromStdString(libraryId()));
    return {};
  }

  Result<void> removeAsset(const std::string& assetId) override
  {
    auto result = m_library->removeAsset(assetId);
    if (result.is_success())
      emit AssetLibraryChanges::instance().changed(QString::fromStdString(libraryId()));
    return result;
  }

  Result<void> setTags(
    const std::string& assetId, const std::vector<std::string>& tags) override
  {
    auto result = m_library->setTags(assetId, tags);
    if (result.is_success())
      emit AssetLibraryChanges::instance().changed(QString::fromStdString(libraryId()));
    return result;
  }

  Result<project::LibraryAsset> createTerrainMaterial(
    const project::AssetLibrary::TerrainMaterialDefinition& def,
    std::vector<std::uint8_t> compatible) override
  {
    auto result = m_library->createTerrainMaterial(def, std::move(compatible));
    if (result.is_success())
      emit AssetLibraryChanges::instance().changed(QString::fromStdString(libraryId()));
    return result;
  }
};

} // namespace

std::unique_ptr<AssetLibrarySource> openLocalAssetLibrary(
  const std::filesystem::path& folder, QString* error)
{
  if (!project::AssetLibrary::isLibraryFolder(folder))
  {
    if (error)
      *error =
        QObject::tr("'%1' is not an asset library folder.").arg(pathAsQString(folder));
    return nullptr;
  }

  auto opened = project::AssetLibrary::open(folder);
  if (opened.is_error())
  {
    if (error)
      *error = QString::fromStdString(std::get<Error>(opened.error()).msg);
    return nullptr;
  }
  return std::make_unique<LocalAssetLibrarySource>(std::move(opened).value());
}

} // namespace tb::ui
