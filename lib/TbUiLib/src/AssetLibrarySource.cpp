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
#include "ui/BuiltinAssetLibrary.h"
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

Result<std::string> importLibraryFile(
  project::AssetLibrary& library,
  const std::filesystem::path& source,
  const std::string& relativePath,
  const bool overwrite)
{
  auto files =
    std::vector<project::AssetLibrary::ImportFile>{{source, relativePath, std::nullopt}};
  if (librarySubfolderForFile(source) == "models")
  {
    auto stream = std::ifstream{source, std::ios::binary};
    auto header = std::array<char, 244>{};
    if (stream.read(header.data(), std::streamsize(header.size())))
    {
      const auto dependencies = project::readGoldSrcModelDependencies(
        std::vector<std::uint8_t>{header.begin(), header.end()});
      if (dependencies.is_success())
      {
        auto names = project::modelAnimationGroupNames(
          source.filename().string(), dependencies.value().sequenceGroups);
        const auto companion = project::modelCompanionName(source.filename().string());
        if (!companion.empty())
          names.push_back(companion);
        const auto existing = library.assets();
        if (existing.is_error())
          return std::get<Error>(existing.error());
        for (const auto& name : names)
        {
          const auto file = std::filesystem::path{name}.filename();
          const auto sourcePath = source.parent_path() / file;
          if (!std::filesystem::is_regular_file(sourcePath))
            continue;
          // Renaming an import also renames its companion's virtual filename.
          auto targetFile = file;
          const auto sourceStem = source.stem().string();
          if (file.string().starts_with(sourceStem))
            targetFile = std::filesystem::path{relativePath}.stem().string()
                         + file.string().substr(sourceStem.size());
          const auto target =
            (std::filesystem::path{relativePath}.parent_path() / targetFile)
              .generic_string();
          if (!overwrite && std::ranges::any_of(existing.value(), [&](const auto& asset) {
                return asset.virtualPath == target;
              }))
            return Error{"A model dependency already uses this import path."};
          files.push_back({sourcePath, target, std::nullopt});
        }
      }
    }
  }
  const auto imported = library.importFiles(files, overwrite);
  if (imported.is_error())
    return std::get<Error>(imported.error());
  emit AssetLibraryChanges::instance().changed(QString::fromStdString(library.info().id));
  return relativePath;
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
  const auto edges = source.allDependencies();
  if (edges.is_error())
    return previewKeys(assets.value(), source.libraryId(), [&](const auto& id) {
      return source.dependencies(id);
    });
  auto byAsset = std::unordered_map<std::string, std::vector<project::AssetDependency>>{};
  for (const auto& edge : edges.value())
    byAsset[edge.assetId].push_back(edge);
  return previewKeys(assets.value(), source.libraryId(), [&](const auto& id) {
    const auto found = byAsset.find(id);
    return Result<std::vector<project::AssetDependency>>{
      found == byAsset.end() ? std::vector<project::AssetDependency>{} : found->second};
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
    const auto allMetadata = source->allMetadata();
    for (auto& asset : assets)
    {
      if (asset.libraryId != source->libraryId() || asset.libraryAssetId.empty())
        continue;
      if (allMetadata.is_success())
      {
        const auto found = allMetadata.value().find(asset.libraryAssetId);
        asset.libraryMetadata = found == allMetadata.value().end()
                                  ? AssetLibrarySource::Metadata{}
                                  : found->second;
      }
      else if (const auto metadata = source->metadata(asset.libraryAssetId);
               metadata.is_success())
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

class CatalogAssetLibrarySource : public AssetLibrarySource
{
protected:
  std::unique_ptr<project::AssetLibrary> m_library;

public:
  explicit CatalogAssetLibrarySource(std::unique_ptr<project::AssetLibrary> library)
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

  Result<std::vector<project::AssetDependency>> allDependencies() const override
  {
    return m_library->allDependencies();
  }

  Result<std::vector<std::string>> folders() const override
  {
    return m_library->folders();
  }

  Result<std::map<std::string, Metadata>> allMetadata() override
  {
    const auto entries = m_library->allMetadata();
    if (entries.is_error())
      return std::get<Error>(entries.error());
    auto result = std::map<std::string, Metadata>{};
    for (const auto& [id, meta] : entries.value())
      result.emplace(id, Metadata{meta.favorite, meta.tags});
    return result;
  }

  Result<Metadata> metadata(const std::string& assetId) override
  {
    const auto meta = m_library->metadata(assetId);
    if (meta.is_error())
      return std::get<Error>(meta.error());
    return Metadata{meta.value().favorite, meta.value().tags};
  }
};

class LocalAssetLibrarySource : public CatalogAssetLibrarySource
{
public:
  using CatalogAssetLibrarySource::CatalogAssetLibrarySource;

  bool canImport() const override { return true; }

  bool canConvert() const override { return true; }

  bool canReveal() const override { return true; }

  Result<std::string> importFile(
    const std::filesystem::path& source, const std::string& relativePath) override
  {
    return importLibraryFile(*m_library, source, relativePath, false);
  }

  Result<std::string> replaceFile(
    const std::filesystem::path& source, const std::string& relativePath) override
  {
    return importLibraryFile(*m_library, source, relativePath, true);
  }

  Result<void> setGroups(const std::map<std::string, std::string>& groups) override
  {
    auto result = m_library->setGroups(groups);
    if (result.is_success())
      emit AssetLibraryChanges::instance().changed(QString::fromStdString(libraryId()));
    return result;
  }

  Result<bool> convertAsset(const std::string& assetId) override
  {
    auto result = convertLibraryImage(*m_library, assetId);
    if (result.is_success() && result.value())
      emit AssetLibraryChanges::instance().changed(QString::fromStdString(libraryId()));
    return result;
  }

  bool canManage() const override { return true; }

  Result<void> createFolder(const std::string& path) override
  {
    auto result = m_library->createFolder(path);
    if (result.is_success())
      emit AssetLibraryChanges::instance().changed(QString::fromStdString(libraryId()));
    return result;
  }
  Result<void> renameFolder(const std::string& path, const std::string& newPath) override
  {
    auto result = m_library->renameFolder(path, newPath);
    if (result.is_success())
      emit AssetLibraryChanges::instance().changed(QString::fromStdString(libraryId()));
    return result;
  }
  Result<void> removeFolder(const std::string& path) override
  {
    auto result = m_library->removeFolder(path);
    if (result.is_success())
      emit AssetLibraryChanges::instance().changed(QString::fromStdString(libraryId()));
    return result;
  }
  Result<void> moveAssetsToFolder(
    const std::vector<std::string>& ids, const std::string& path) override
  {
    auto result = m_library->moveAssetsToFolder(ids, path);
    if (result.is_success())
      emit AssetLibraryChanges::instance().changed(QString::fromStdString(libraryId()));
    return result;
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
    return removeAssets({assetId});
  }

  Result<void> removeAssets(const std::vector<std::string>& assetIds) override
  {
    auto result = m_library->removeAssets(assetIds);
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
  if (opened.value()->info().id == BuiltinAssetLibraryId)
    return std::make_unique<CatalogAssetLibrarySource>(std::move(opened).value());
  return std::make_unique<LocalAssetLibrarySource>(std::move(opened).value());
}

} // namespace tb::ui
