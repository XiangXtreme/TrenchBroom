/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <QString>

#include "base/Result.h"

#include "kd/path_hash.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace tb::ui
{

enum class BrowserCellType
{
  Folder,
  Model,
  Sprite,
  Sound,
  Prefab,
  Map,
  Texture,
};

/**
 * Where a browser entry comes from.
 *
 * The browser shows one layer at a time but always keeps the marker, so a project
 * resource never looks like a library resource and vice versa.
 */
enum class BrowserSource
{
  GameFiles,
  Project,
  AssetLibrary,
};

/**
 * Which sources the browser lists.
 *
 * The filter is a view setting, not a property of a resource: every listing entry still
 * carries its real BrowserSource, so the unified listing can mark where each resource
 * comes from without pretending it has one origin.
 */
enum class BrowserSourceFilter
{
  All,
  GameFiles,
  Project,
  AssetLibrary,
};

struct BrowserCellData
{
  BrowserCellType type;
  std::filesystem::path path;
};

struct BrowserAsset
{
  BrowserCellType type = BrowserCellType::Folder;
  std::filesystem::path path;
  std::filesystem::path absolutePath;
  std::string displayName;
  std::optional<std::filesystem::file_time_type> lastModified;
  std::string projectId;
  std::string assetId;
  std::uint64_t revision = 0;
  std::filesystem::path catalogPath;
  // The entry is a skybox group: its content is a manifest of six face textures, not an
  // image, so it must not be previewed or dropped as a single texture.
  bool skybox = false;

  BrowserSource source = BrowserSource::GameFiles;
  // Library identity. Empty unless this entry was enumerated from an asset library.
  std::string libraryId;
  std::string libraryAssetId;
  std::uint64_t libraryRevision = 0;
  std::string libraryContentHash;
  // Project snapshot already captured from that library resource, if any.
  std::string projectAssetId;
  std::uint64_t projectRevision = 0;
  // Engine path of that snapshot. Empty until the resource is captured, which is what
  // keeps a library entry from being used as a map dependency before it is embedded.
  std::filesystem::path projectPublishPath;
  // The library holds a newer revision than the project snapshot.
  bool projectUpdateAvailable = false;
  // The library asset or its source file is gone.
  bool missing = false;

  BrowserAsset();
  BrowserAsset(BrowserCellType i_type, std::filesystem::path i_path);
  BrowserAsset(
    BrowserCellType i_type,
    std::filesystem::path i_path,
    std::filesystem::path i_absolutePath,
    std::optional<std::filesystem::file_time_type> i_lastModified = std::nullopt);

  friend bool operator==(const BrowserAsset& lhs, const BrowserAsset& rhs);
};

struct ModelBrowserEntry
{
  BrowserCellData cellData;
  std::string title;
};

using AssetWriteTimes = std::
  unordered_map<std::filesystem::path, std::filesystem::file_time_type, kdl::path_hash>;

const std::vector<std::pair<BrowserCellType, std::filesystem::path>>& goldSrcAssetRoots();

const std::vector<std::filesystem::path>& goldSrcAssetExtensions();

const std::vector<std::pair<BrowserCellType, std::filesystem::path>>& assetBrowserRoots();

const std::vector<std::filesystem::path>& assetBrowserExtensions();

BrowserCellType assetTypeForExtension(const std::filesystem::path& path);

const char* browserSourceName(BrowserSource source);

bool browserFilterIncludesSource(BrowserSourceFilter filter, BrowserSource source);

bool isEntityModelAsset(const BrowserAsset& asset);

bool sameBrowserAssetContent(const BrowserAsset& lhs, const BrowserAsset& rhs);

/** Short, stable status marker shown next to a catalog entry. */
QString browserAssetStatus(const BrowserAsset& asset);

/** True when the entry was enumerated from an asset library catalog. */
bool isLibraryAsset(const BrowserAsset& asset);

/** True when the entry is the project snapshot of a captured library resource. */
bool hasProjectSnapshot(const BrowserAsset& asset);

std::vector<std::filesystem::path> entityModelAssetPaths(
  const std::vector<BrowserAsset>& assets);

/**
 * Combines the project, library and game-file listings into the unified listing.
 *
 * The three sources overlap, so the order of preference is:
 *  - a project snapshot replaces the library entry it was captured from, keeping the
 *    library's "missing" and "update available" state visible on the snapshot;
 *  - the remaining entries are deduplicated by type and engine path, with a library entry
 *    replacing a game-file entry that has the same path.
 *
 * The result is ordered by engine path so the listing is stable across scans.
 */
std::vector<BrowserAsset> mergeBrowserAssets(
  std::vector<BrowserAsset> projectAssets,
  std::vector<BrowserAsset> libraryAssets,
  std::vector<BrowserAsset> gameAssets);

std::optional<std::vector<BrowserAsset>> collectBrowserAssets(
  const std::filesystem::path& folderPath,
  const std::vector<std::filesystem::path>& modRoots,
  const std::function<
    Result<std::vector<std::filesystem::path>>(const std::filesystem::path&)>& findAssets,
  const std::function<Result<std::filesystem::path>(const std::filesystem::path&)>&
    makeAbsolute,
  const std::vector<std::pair<BrowserCellType, std::filesystem::path>>& rootFilters =
    goldSrcAssetRoots());

AssetWriteTimes assetLastWriteTimes(const std::vector<BrowserAsset>& assets);

std::vector<std::filesystem::path> changedAssetPaths(
  const AssetWriteTimes& oldWriteTimes,
  const AssetWriteTimes& newWriteTimes,
  const std::vector<BrowserAsset>& oldAssets,
  const std::vector<BrowserAsset>& newAssets);

std::vector<ModelBrowserEntry> modelBrowserEntries(
  const std::filesystem::path& rootFolderPath,
  const std::vector<BrowserAsset>& assets,
  const std::filesystem::path& currentFolderPath,
  const QString& searchText,
  // Game files are browsed through fixed roots; project and library listings only show
  // the folders their own entries imply.
  bool includeDefaultRoots = true,
  // The unified listing marks each entry's source, because the same engine path can be
  // listed from more than one place.
  bool showSourceMarkers = false);

} // namespace tb::ui
