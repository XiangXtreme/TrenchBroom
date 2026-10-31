/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom, licensed under GPL version 3 or later.
*/
#include "ui/AssetLibrarySource.h"

#include <QObject>

#include "project/AssetLibrary.h"
#include "ui/AssetLibraryImport.h"
#include "ui/QPathUtils.h"

#include <utility>

namespace tb::ui
{
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

  QString name() const override
  {
    return QString::fromStdString(m_library->info().name);
  }

  QString location() const override
  {
    return pathAsQString(m_library->folder());
  }

  bool isAvailable() const override
  {
    return true;
  }

  Result<std::vector<project::LibraryAsset>> assets() const override
  {
    return m_library->assets();
  }

  Result<std::vector<std::uint8_t>> readOriginal(
    const std::string& assetId) const override
  {
    return m_library->readOriginal(assetId);
  }

  Result<std::vector<std::uint8_t>> readContent(
    const std::string& assetId) const override
  {
    return m_library->readContent(assetId);
  }

  Result<std::vector<project::AssetDependency>> dependencies(
    const std::string& assetId) const override
  {
    return m_library->dependencies(assetId);
  }

  bool canImport() const override
  {
    return true;
  }

  bool canConvert() const override
  {
    return true;
  }

  bool canReveal() const override
  {
    return true;
  }

  Result<void> rescan() override
  {
    const auto scanned = m_library->scan();
    if (scanned.is_error())
      return std::get<Error>(scanned.error());
    return {};
  }

  Result<std::string> importFile(
    const std::filesystem::path& source, const std::string& relativePath) override
  {
    return importFileIntoLibrary(m_library->folder(), source, relativePath);
  }

  Result<bool> convertAsset(const std::string& assetId) override
  {
    return convertLibraryImage(*m_library, assetId);
  }

  bool canManage() const override
  {
    return true;
  }

  Result<Metadata> metadata(const std::string& assetId) override
  {
    const auto meta = m_library->metadata(assetId);
    if (meta.is_error())
      return std::get<Error>(meta.error());
    return Metadata{meta.value().favorite, meta.value().tags};
  }

  Result<void> setFavorite(const std::string& assetId, const bool favorite) override
  {
    return m_library->setFavorite(assetId, favorite);
  }

  Result<void> renameAsset(const std::string& assetId, const std::string& name) override
  {
    const auto renamed = m_library->setDisplayName(assetId, name);
    if (renamed.is_error())
      return std::get<Error>(renamed.error());
    return {};
  }

  Result<void> removeAsset(const std::string& assetId) override
  {
    return m_library->removeAsset(assetId);
  }
};

} // namespace

std::unique_ptr<AssetLibrarySource> openLocalAssetLibrary(
  const std::filesystem::path& folder, QString* error)
{
  if (!project::AssetLibrary::isLibraryFolder(folder))
  {
    if (error)
      *error = QObject::tr("'%1' is not an asset library folder.")
                 .arg(pathAsQString(folder));
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
