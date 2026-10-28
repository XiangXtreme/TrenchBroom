/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom.
*/

#pragma once

#include "project/ProjectAsset.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tb::project
{

/**
 * An asset library is a folder of GoldSrc sources with its own catalog.
 *
 * The library is deliberately not a project: it owns no map, no editor state and no
 * references. It publishes stable asset identities, immutable content revisions and
 * content hashes so that a project can copy exactly the revisions it uses and stop
 * depending on the library afterwards.
 *
 * The catalog lives in `<folder>/.tbassets` (SQLite). Sources stay where the user put
 * them; the catalog only indexes them. Rescanning is the only way library content
 * changes, and a rescan never touches any project.
 *
 * Granularity rules:
 *  - a WAD is a *container*: each miptex entry becomes an independent asset grouped
 *    under the WAD's relative path (for example `custom.wad/WALL01`),
 *  - an image, model, sprite or sound file is one asset,
 *  - non-native formats keep their original bytes and conversion recipe; the
 *    compatible GoldSrc result is produced when the project captures the asset.
 */
class AssetLibrary
{
public:
  // Catalog file inside the library folder.
  static const char* indexFileName();
  static std::filesystem::path indexPath(const std::filesystem::path& folder);
  static bool isLibraryFolder(const std::filesystem::path& folder);

  static Result<std::unique_ptr<AssetLibrary>> create(
    const std::filesystem::path& folder, const std::string& name);
  static Result<std::unique_ptr<AssetLibrary>> open(const std::filesystem::path& folder);

  ~AssetLibrary();
  AssetLibrary(AssetLibrary&&) noexcept;
  AssetLibrary& operator=(AssetLibrary&&) noexcept;
  AssetLibrary(const AssetLibrary&) = delete;
  AssetLibrary& operator=(const AssetLibrary&) = delete;

  const std::filesystem::path& folder() const;
  const LibraryInfo& info() const;

  // Rescans the folder and advances revision numbers for changed content. Assets that
  // disappeared are flagged missing instead of deleted, so a project can report the
  // loss and keep using its own snapshot.
  Result<LibraryScan> scan();

  Result<std::vector<LibraryAsset>> assets(
    std::optional<AssetType> type = std::nullopt, const std::string& group = {}) const;
  Result<std::vector<LibrarySource>> sources() const;
  Result<std::vector<std::string>> groups() const;
  Result<std::optional<LibraryAsset>> findAsset(const std::string& id) const;
  // Every recorded revision, oldest first. Used for "view differences".
  Result<std::vector<LibraryAsset>> revisions(const std::string& id) const;

  Result<std::vector<std::uint8_t>> readContent(
    const std::string& id, std::optional<std::uint64_t> revision = std::nullopt) const;
  Result<std::vector<std::uint8_t>> readOriginal(
    const std::string& id, std::optional<std::uint64_t> revision = std::nullopt) const;

  Result<void> addDependency(
    const std::string& assetId, const std::string& dependencyId, const std::string& kind);
  Result<std::vector<AssetDependency>> dependencies(const std::string& assetId) const;
  Result<std::vector<AssetDependency>> dependents(const std::string& dependencyId) const;

private:
  class Impl;
  std::unique_ptr<Impl> m_impl;

  explicit AssetLibrary(std::unique_ptr<Impl> impl);
};

/** Catalog names for library source kinds. Only used for display and diagnostics. */
const char* librarySourceKindName(AssetType type);

} // namespace tb::project
