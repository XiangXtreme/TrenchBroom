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

bool isEntityModelAsset(const BrowserAsset& asset);

bool sameBrowserAssetContent(const BrowserAsset& lhs, const BrowserAsset& rhs);

std::vector<std::filesystem::path> entityModelAssetPaths(
  const std::vector<BrowserAsset>& assets);

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
  const QString& searchText);

} // namespace tb::ui
