/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom.
*/

#pragma once

#include "project/ProjectAsset.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tb::project
{

/** A self-contained asset catalog. Imported bytes and dependencies are owned by the
 * SQLite library; source files are read only during explicit imports. Projects capture
 * their own independent snapshots of library revisions.
 */
class AssetLibrary
{
public:
  // Catalog file inside the library folder.
  static const char* indexFileName();
  static std::filesystem::path indexPath(const std::filesystem::path& folder);
  static bool isLibraryFolder(const std::filesystem::path& folder);

  // If initialImport is supplied, import the folder before keeping the new catalog.
  // A failed initial import removes only the newly reserved catalog.
  static Result<std::unique_ptr<AssetLibrary>> create(
    const std::filesystem::path& folder,
    const std::string& name,
    LibraryScan* initialImport = nullptr);
  static Result<std::unique_ptr<AssetLibrary>> open(const std::filesystem::path& folder);

  ~AssetLibrary();
  AssetLibrary(AssetLibrary&&) noexcept;
  AssetLibrary& operator=(AssetLibrary&&) noexcept;
  AssetLibrary(const AssetLibrary&) = delete;
  AssetLibrary& operator=(const AssetLibrary&) = delete;

  const std::filesystem::path& folder() const;
  const LibraryInfo& info() const;

  struct ImportFile
  {
    std::filesystem::path source;
    std::string virtualPath;
    std::optional<std::vector<std::uint8_t>> bytes = std::nullopt;
  };
  // Explicit ingestion only. Replacing an existing path preserves its identity and group.
  Result<LibraryScan> importFiles(
    const std::vector<ImportFile>& files, bool overwrite = false);
  Result<LibraryScan> importDirectory(const std::filesystem::path& sourceFolder);
  Result<void> setGroups(const std::map<std::string, std::string>& groups);
  Result<void> setGroup(
    const std::vector<std::string>& assetIds, const std::string& group);

  // Catalog folders live below fixed asset-type roots. Moving and renaming only
  // changes organization; asset IDs, revisions, source paths and content stay intact.
  Result<std::vector<std::string>> folders() const;
  Result<void> createFolder(const std::string& path);
  Result<void> renameFolder(const std::string& path, const std::string& newPath);
  Result<void> removeFolder(const std::string& path);
  Result<void> moveAssetsToFolder(
    const std::vector<std::string>& assetIds, const std::string& path);

  Result<std::vector<LibraryAsset>> assets(
    std::optional<AssetType> type = std::nullopt, const std::string& group = {}) const;
  Result<std::vector<LibrarySource>> sources() const;
  Result<std::vector<std::string>> groups() const;
  Result<std::optional<LibraryAsset>> findAsset(const std::string& id) const;
  // The revision currently stored for this asset. Libraries keep no history, so this
  // contains at most one entry: a content change replaces the previous revision.
  Result<std::vector<LibraryAsset>> revisions(const std::string& id) const;

  Result<std::vector<std::uint8_t>> readContent(
    const std::string& id, std::optional<std::uint64_t> revision = std::nullopt) const;
  Result<std::vector<std::uint8_t>> readOriginal(
    const std::string& id, std::optional<std::uint64_t> revision = std::nullopt) const;

  // Publishes a converted GoldSrc result for an image asset.
  //
  // The source bytes and their hash stay untouched, so explicit reimports recognize
  // unchanged content; the revision advances so projects see a new library version, and
  // the native format becomes `goldsrc-wad` so captures copy the converted bytes
  // verbatim instead of converting the image again.
  Result<LibraryAsset> publishCompatibleContent(
    const std::string& assetId,
    std::vector<std::uint8_t> compatible,
    const std::string& recipe);

  Result<void> addDependency(
    const std::string& assetId, const std::string& dependencyId, const std::string& kind);
  Result<std::vector<AssetDependency>> dependencies(const std::string& assetId) const;
  Result<std::vector<AssetDependency>> allDependencies() const;
  Result<std::vector<AssetDependency>> dependents(const std::string& dependencyId) const;

  // Removes owned assets and their dependency edges. Projects keep their snapshots.
  Result<void> removeAsset(const std::string& assetId);
  // Removes all selected assets in one transaction. If any asset cannot be removed,
  // the catalog remains unchanged.
  Result<void> removeAssets(const std::vector<std::string>& assetIds);

  struct AssetMetadata
  {
    bool favorite = false;
    std::vector<std::string> tags;

    friend bool operator==(const AssetMetadata&, const AssetMetadata&) = default;
  };
  Result<AssetMetadata> metadata(const std::string& assetId) const;
  Result<std::map<std::string, AssetMetadata>> allMetadata() const;
  Result<void> setFavorite(const std::string& assetId, bool favorite);
  Result<void> setTags(const std::string& assetId, const std::vector<std::string>& tags);

  // Gives the asset a catalog name that the library keeps. The engine texture name is
  // separate: it lives in the converted miptex and only changes by converting again.
  Result<LibraryAsset> setDisplayName(
    const std::string& assetId, const std::string& name);

  struct TerrainMaterialSlot
  {
    std::string name;
    // Stable library asset ID, not a texture name (which can be ambiguous).
    std::string texture;
  };

  struct TerrainMaterialDefinition
  {
    std::string name;
    std::string folder; // Optional, e.g. "materials"
    std::array<TerrainMaterialSlot, 3> materialSlots;
  };

  Result<LibraryAsset> createTerrainMaterial(
    const TerrainMaterialDefinition& def, std::vector<std::uint8_t> compatible = {});

private:
  class Impl;
  std::unique_ptr<Impl> m_impl;

  explicit AssetLibrary(std::unique_ptr<Impl> impl);
};

/** Catalog names for library source kinds. Only used for display and diagnostics. */
const char* librarySourceKindName(AssetType type);
std::string libraryAssetFolder(const LibraryAsset& asset);

} // namespace tb::project
