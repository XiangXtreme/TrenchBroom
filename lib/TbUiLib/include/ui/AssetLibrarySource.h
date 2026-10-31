/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom, licensed under GPL version 3 or later.
*/
#pragma once

#include "base/Result.h"
#include "project/ProjectAsset.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <QString>

namespace tb::ui
{

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
  /** Folder path now; a URL once a remote source exists. */
  virtual QString location() const = 0;
  virtual bool isRemote() const {
    return false;
  }

  virtual bool isAvailable() const = 0;
  virtual QString error() const {
    return {};
  }

  virtual Result<std::vector<project::LibraryAsset>> assets() const = 0;
  virtual Result<std::vector<std::uint8_t>> readOriginal(
    const std::string& assetId) const = 0;
  virtual Result<std::vector<std::uint8_t>> readContent(
    const std::string& assetId) const = 0;
  virtual Result<std::vector<project::AssetDependency>> dependencies(
    const std::string& assetId) const = 0;

  virtual bool canImport() const {
    return false;
  }
  virtual bool canConvert() const {
    return false;
  }
  virtual bool canReveal() const {
    return false;
  }
  /** Whether this source supports catalog edits (favourite, rename, delete). */
  virtual bool canManage() const {
    return false;
  }

  struct Metadata
  {
    bool favorite = false;
    std::vector<std::string> tags;
  };
  virtual Result<Metadata> metadata(const std::string& /*assetId*/) {
    return Error{"This asset library is read only."};
  }
  virtual Result<void> setFavorite(const std::string& /*assetId*/, bool /*favorite*/) {
    return Error{"This asset library is read only."};
  }
  virtual Result<void> renameAsset(
    const std::string& /*assetId*/, const std::string& /*name*/) {
    return Error{"This asset library is read only."};
  }
  virtual Result<void> removeAsset(const std::string& /*assetId*/) {
    return Error{"This asset library is read only."};
  }
  virtual Result<void> setTags(
    const std::string& /*assetId*/, const std::vector<std::string>& /*tags*/) {
    return Error{"This asset library is read only."};
  }

  // Mutating operations. A read-only source reports the matching capability as false and
  // the browser hides the action instead of calling these.
  virtual Result<void> rescan() {
    return Error{"This asset library is read only."};
  }
  virtual Result<std::string> importFile(
    const std::filesystem::path& /*source*/, const std::string& /*relativePath*/) {
    return Error{"This asset library is read only."};
  }
  virtual Result<bool> convertAsset(const std::string& /*assetId*/) {
    return Error{"This asset library is read only."};
  }
};

/** Opens a library folder. Returns null when the folder is not a library. */
std::unique_ptr<AssetLibrarySource> openLocalAssetLibrary(
  const std::filesystem::path& folder, QString* error = nullptr);

} // namespace tb::ui
