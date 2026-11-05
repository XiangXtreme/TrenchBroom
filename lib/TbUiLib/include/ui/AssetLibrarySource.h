/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom, licensed under GPL version 3 or later.
*/
#pragma once

#include <QObject>
#include <QString>

#include "base/Result.h"
#include "project/AssetLibrary.h"
#include "project/ProjectAsset.h"
#include "ui/AssetBrowserModel.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tb::project
{
class ProjectStore;
}

namespace tb::ui
{
class ProjectSession;
class AssetLibrarySource;
/** Both browser hosts hide the faces owned by a logical skybox group. */
Result<std::vector<project::Asset>> browsableProjectAssets(
  const project::ProjectStore& store);
using AssetPreviewKeys = std::map<std::string, QString>;
/** Content identities include hidden dependency revisions, but exclude annotations. */
Result<AssetPreviewKeys> assetPreviewKeys(const AssetLibrarySource& source);
Result<AssetPreviewKeys> assetPreviewKeys(const project::ProjectStore& store);
void loadBrowserAssetMetadata(
  std::vector<BrowserAsset>& assets, const ProjectSession& session);
/** Content/metadata invalidation only; it never changes a project's captured bytes. */
class AssetLibraryChanges : public QObject
{
  Q_OBJECT
public:
  static AssetLibraryChanges& instance();
signals:
  void changed(const QString& libraryId);
};

/**
 * Read-only view of one asset library.
 *
 * The browser talks to this interface only, so a future HTTP backed library can reuse
 * the whole view without touching the panels: a remote source answers assets(),
 * previews and dependencies the same way and simply reports the mutating capabilities
 * as unavailable, which hides those actions instead of failing them.
 */
class AssetLibrarySource
{
public:
  virtual ~AssetLibrarySource() = default;

  virtual QString name() const = 0;
  /** Stable catalog identity; empty for a source without one. */
  virtual std::string libraryId() const { return {}; }
  /** Folder path now; a URL once a remote source exists. */
  virtual QString location() const = 0;
  virtual bool isRemote() const { return false; }

  virtual bool isAvailable() const = 0;
  virtual QString error() const { return {}; }

  /** Browsable assets; logical groups such as skyboxes may hide their member faces. */
  virtual Result<std::vector<project::LibraryAsset>> assets() const = 0;
  /** Every indexed asset, including members hidden from the browser. */
  virtual Result<std::vector<project::LibraryAsset>> allAssets() const
  {
    return assets();
  }
  /** Looks up any indexed asset, including a hidden group member. */
  virtual Result<std::optional<project::LibraryAsset>> findAsset(
    const std::string& assetId) const;
  virtual Result<std::vector<std::uint8_t>> readOriginal(
    const std::string& assetId) const = 0;
  virtual Result<std::vector<std::uint8_t>> readContent(
    const std::string& assetId) const = 0;
  virtual Result<std::vector<project::AssetDependency>> dependencies(
    const std::string& assetId) const = 0;

  virtual bool canImport() const { return false; }
  virtual bool canConvert() const { return false; }
  virtual bool canReveal() const { return false; }
  /** Whether this source supports catalog edits (favourite, rename, delete). */
  virtual bool canManage() const { return false; }

  using Metadata = BrowserAssetMetadata;
  virtual Result<Metadata> metadata(const std::string& /*assetId*/)
  {
    return Error{"This asset library is read only."};
  }
  virtual Result<void> setFavorite(const std::string& /*assetId*/, bool /*favorite*/)
  {
    return Error{"This asset library is read only."};
  }
  virtual Result<void> renameAsset(
    const std::string& /*assetId*/, const std::string& /*name*/)
  {
    return Error{"This asset library is read only."};
  }
  virtual Result<void> removeAsset(const std::string& /*assetId*/)
  {
    return Error{"This asset library is read only."};
  }
  virtual Result<void> setTags(
    const std::string& /*assetId*/, const std::vector<std::string>& /*tags*/)
  {
    return Error{"This asset library is read only."};
  }

  // Mutating operations. A read-only source reports the matching capability as false and
  // the browser hides the action instead of calling these.
  virtual Result<void> rescan() { return Error{"This asset library is read only."}; }
  virtual Result<std::string> importFile(
    const std::filesystem::path& /*source*/, const std::string& /*relativePath*/)
  {
    return Error{"This asset library is read only."};
  }
  virtual Result<bool> convertAsset(const std::string& /*assetId*/)
  {
    return Error{"This asset library is read only."};
  }
  virtual Result<std::string> replaceFile(
    const std::filesystem::path&, const std::string&)
  {
    return Error{"This asset library does not support replacing source files."};
  }
  virtual Result<project::LibraryAsset> createTerrainMaterial(
    const project::AssetLibrary::TerrainMaterialDefinition& /*def*/,
    std::vector<std::uint8_t> /*compatible*/ = {})
  {
    return Error{"This asset library is read only."};
  }
};

/** Opens a library folder. Returns null when the folder is not a library. */
std::unique_ptr<AssetLibrarySource> openLocalAssetLibrary(
  const std::filesystem::path& folder, QString* error = nullptr);

} // namespace tb::ui
