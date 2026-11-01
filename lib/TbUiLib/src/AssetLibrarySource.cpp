/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom, licensed under GPL version 3 or later.
*/
#include "ui/AssetLibrarySource.h"

#include <QObject>

#include "project/AssetLibrary.h"
#include "ui/AssetLibraryImport.h"
#include "ui/ProjectSession.h"
#include "ui/QPathUtils.h"

#include <utility>

namespace tb::ui
{
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
    return m_library->assets();
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
    return importFileIntoLibrary(m_library->folder(), source, relativePath);
  }

  Result<std::string> replaceFile(
    const std::filesystem::path& source, const std::string& relativePath) override
  {
    return importFileIntoLibrary(m_library->folder(), source, relativePath, true);
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
