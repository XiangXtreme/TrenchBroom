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

#include "ui/AssetBrowserModel.h"

#include <QCoreApplication>
#include <QObject>

#include "base/Macros.h"
#include "ui/QPathUtils.h"

#include "kd/path_utils.h"

#include <algorithm>
#include <iterator>
#include <ranges>

namespace tb::ui
{
bool assetSearchMatches(
  const QString& name,
  const QString& path,
  const std::vector<std::string>& tags,
  const QString& search)
{
  const auto query = search.trimmed();
  if (
    query.isEmpty() || name.contains(query, Qt::CaseInsensitive)
    || path.contains(query, Qt::CaseInsensitive))
    return true;
  return std::ranges::any_of(tags, [&](const auto& tag) {
    return QString::fromStdString(tag).contains(query, Qt::CaseInsensitive);
  });
}

bool assetMetadataMatches(
  const std::optional<BrowserAssetMetadata>& metadata,
  const bool favoritesOnly,
  const QString& tag)
{
  if (!favoritesOnly && tag.isEmpty())
    return true;
  if (!metadata || (favoritesOnly && !metadata->favorite))
    return false;
  return tag.isEmpty() || std::ranges::any_of(metadata->tags, [&](const auto& value) {
           return QString::fromStdString(value).compare(tag, Qt::CaseInsensitive) == 0;
         });
}
namespace
{

std::optional<std::filesystem::file_time_type> lastWriteTime(
  const std::filesystem::path& path)
{
  if (path.empty())
  {
    return std::nullopt;
  }

  auto error = std::error_code{};
  const auto t = std::filesystem::last_write_time(path, error);
  return error ? std::nullopt : std::optional{t};
}

std::string displayNameForPath(const std::filesystem::path& path)
{
  const auto name = path.filename().generic_string();
  return name.empty() ? path.generic_string() : name;
}

bool sameAssetIdentity(const BrowserAsset& lhs, const BrowserAsset& rhs)
{
  if (!lhs.libraryAssetId.empty() || !rhs.libraryAssetId.empty())
    return lhs.libraryId == rhs.libraryId && lhs.libraryAssetId == rhs.libraryAssetId;
  if (!lhs.assetId.empty() || !rhs.assetId.empty())
    return lhs.projectId == rhs.projectId && lhs.assetId == rhs.assetId;
  return lhs.type == rhs.type && lhs.path == rhs.path;
}

} // namespace

BrowserAsset::BrowserAsset() = default;

BrowserAsset::BrowserAsset(BrowserCellType i_type, std::filesystem::path i_path)
  : BrowserAsset{
      i_type,
      std::move(i_path),
      std::filesystem::path{},
      std::optional<std::filesystem::file_time_type>{}}
{
}

BrowserAsset::BrowserAsset(
  const BrowserCellType i_type,
  std::filesystem::path i_path,
  std::filesystem::path i_absolutePath,
  std::optional<std::filesystem::file_time_type> i_lastModified)
  : type{i_type}
  , path{std::move(i_path)}
  , absolutePath{std::move(i_absolutePath)}
  , displayName{displayNameForPath(path)}
  , lastModified{
      i_lastModified ? i_lastModified
                     : lastWriteTime(absolutePath.empty() ? path : absolutePath)}
{
}

bool operator==(const BrowserAsset& lhs, const BrowserAsset& rhs)
{
  return lhs.type == rhs.type && lhs.path == rhs.path
         && lhs.absolutePath == rhs.absolutePath && lhs.displayName == rhs.displayName
         && lhs.lastModified == rhs.lastModified && lhs.projectId == rhs.projectId
         && lhs.assetId == rhs.assetId && lhs.revision == rhs.revision
         && lhs.catalogPath == rhs.catalogPath
         && lhs.textureCollection == rhs.textureCollection && lhs.source == rhs.source
         && lhs.previewIdentity == rhs.previewIdentity && lhs.skybox == rhs.skybox
         && lhs.libraryId == rhs.libraryId && lhs.libraryAssetId == rhs.libraryAssetId
         && lhs.libraryRevision == rhs.libraryRevision
         && lhs.libraryContentHash == rhs.libraryContentHash
         && lhs.projectAssetId == rhs.projectAssetId
         && lhs.projectRevision == rhs.projectRevision
         && lhs.projectPublishPath == rhs.projectPublishPath
         && lhs.projectUpdateAvailable == rhs.projectUpdateAvailable
         && lhs.missing == rhs.missing && lhs.libraryMetadata == rhs.libraryMetadata;
}

std::filesystem::path browserAssetDisplayPath(
  const BrowserAsset& asset, const bool groupWadTextures)
{
  const auto& path = asset.catalogPath.empty() ? asset.path : asset.catalogPath;
  if (asset.skybox)
    return std::filesystem::path{"skybox"} / path.filename();
  if (
    asset.type != BrowserCellType::Texture || asset.skybox
    || asset.textureCollection.empty())
    return path;

  auto folder = std::filesystem::path{"textures"};
  if (groupWadTextures)
  {
    // Containers already below textures keep that hierarchy without a second prefix.
    folder = *asset.textureCollection.begin() == folder
               ? asset.textureCollection
               : folder / asset.textureCollection;
  }
  return folder / path.filename();
}

const char* browserSourceName(const BrowserSource source)
{
  switch (source)
  {
  case BrowserSource::GameFiles:
    return "Game files";
  case BrowserSource::Project:
    return "Project";
  case BrowserSource::AssetLibrary:
    return "Asset library";
  }
  return "Unknown";
}

const char* browserCellTypeName(const BrowserCellType type)
{
  switch (type)
  {
  case BrowserCellType::Folder:
    return "Folder";
  case BrowserCellType::Model:
    return "Model";
  case BrowserCellType::Sprite:
    return "Sprite";
  case BrowserCellType::Sound:
    return "Sound";
  case BrowserCellType::Prefab:
    return "Prefab";
  case BrowserCellType::Map:
    return "Map";
  case BrowserCellType::Texture:
    return "Texture";
  case BrowserCellType::Material:
    return "Terrain Material";
  }
  return "Unknown";
}

QString browserCellTypeDisplayName(const BrowserCellType type)
{
  return QCoreApplication::translate("tb::ui::AssetTypes", browserCellTypeName(type));
}

bool browserFilterIncludesSource(
  const BrowserSourceFilter filter, const BrowserSource source)
{
  switch (filter)
  {
  case BrowserSourceFilter::All:
    return true;
  case BrowserSourceFilter::GameFiles:
    return source == BrowserSource::GameFiles;
  case BrowserSourceFilter::Project:
    return source == BrowserSource::Project;
  case BrowserSourceFilter::AssetLibrary:
    return source == BrowserSource::AssetLibrary;
  }
  return false;
}

bool isLibraryAsset(const BrowserAsset& asset)
{
  return asset.source == BrowserSource::AssetLibrary;
}

bool hasProjectSnapshot(const BrowserAsset& asset)
{
  return !asset.projectAssetId.empty() || !asset.assetId.empty();
}

QString browserAssetStatus(const BrowserAsset& asset)
{
  const auto kind = asset.skybox ? QObject::tr("Skybox (6 faces)") : QString{};
  const auto withKind = [&](const QString& status) {
    return kind.isEmpty() ? status : kind + QString{" — "} + status;
  };
  if (asset.missing)
    return withKind(QObject::tr("Missing in library"));
  if (asset.projectUpdateAvailable)
    return withKind(QObject::tr("Library update available"));
  if (isLibraryAsset(asset) && hasProjectSnapshot(asset))
    return withKind(
      QObject::tr("In project (rev %1)").arg(qulonglong(asset.projectRevision)));
  if (asset.source == BrowserSource::Project && asset.revision > 0)
    return withKind(QObject::tr("Project rev %1").arg(qulonglong(asset.revision)));
  return kind;
}

const std::vector<std::pair<BrowserCellType, std::filesystem::path>>& goldSrcAssetRoots()
{
  static const auto Roots =
    std::vector<std::pair<BrowserCellType, std::filesystem::path>>{
      {BrowserCellType::Model, std::filesystem::path{"models"}},
      {BrowserCellType::Sprite, std::filesystem::path{"sprites"}},
      {BrowserCellType::Sound, std::filesystem::path{"sound"}},
    };
  return Roots;
}

const std::vector<std::filesystem::path>& goldSrcAssetExtensions()
{
  static const auto Extensions =
    std::vector<std::filesystem::path>{".mdl", ".spr", ".wav"};
  return Extensions;
}

const std::vector<std::pair<BrowserCellType, std::filesystem::path>>& assetBrowserRoots()
{
  static const auto Roots =
    std::vector<std::pair<BrowserCellType, std::filesystem::path>>{
      {BrowserCellType::Model, std::filesystem::path{"models"}},
      {BrowserCellType::Sprite, std::filesystem::path{"sprites"}},
      {BrowserCellType::Sound, std::filesystem::path{"sound"}},
      {BrowserCellType::Prefab, std::filesystem::path{"prefabs"}},
    };
  return Roots;
}

const std::vector<std::filesystem::path>& assetBrowserExtensions()
{
  static const auto Extensions =
    std::vector<std::filesystem::path>{".mdl", ".spr", ".wav", ".tbprefab"};
  return Extensions;
}

BrowserCellType assetTypeForExtension(const std::filesystem::path& path)
{
  const auto extension = kdl::path_to_lower(path.extension());
  if (extension == ".mdl")
  {
    return BrowserCellType::Model;
  }
  if (extension == ".spr")
  {
    return BrowserCellType::Sprite;
  }
  if (extension == ".wav")
  {
    return BrowserCellType::Sound;
  }
  if (extension == ".tbprefab")
  {
    return BrowserCellType::Prefab;
  }
  return BrowserCellType::Folder;
}

bool isEntityModelAsset(const BrowserAsset& asset)
{
  return asset.type == BrowserCellType::Model || asset.type == BrowserCellType::Sprite;
}

bool sameBrowserAssetContent(const BrowserAsset& lhs, const BrowserAsset& rhs)
{
  return lhs.source == rhs.source && sameAssetIdentity(lhs, rhs) && lhs.type == rhs.type
         && lhs.path == rhs.path && lhs.absolutePath == rhs.absolutePath
         && lhs.revision == rhs.revision && lhs.libraryRevision == rhs.libraryRevision
         && lhs.projectUpdateAvailable == rhs.projectUpdateAvailable
         && lhs.previewIdentity == rhs.previewIdentity && lhs.missing == rhs.missing
         && lhs.lastModified == rhs.lastModified;
}

std::vector<std::filesystem::path> entityModelAssetPaths(
  const std::vector<BrowserAsset>& assets)
{
  auto result = std::vector<std::filesystem::path>{};
  for (const auto& asset : assets)
  {
    if (isEntityModelAsset(asset))
    {
      // Library entries refer to the engine path of their project snapshot; an
      // uncaptured library resource has none yet.
      result.push_back(
        asset.projectPublishPath.empty() ? asset.path : asset.projectPublishPath);
    }
  }
  return result;
}

std::vector<BrowserAsset> mergeBrowserAssets(
  std::vector<BrowserAsset> projectAssets,
  std::vector<BrowserAsset> libraryAssets,
  std::vector<BrowserAsset> gameAssets)
{
  const auto libraryIdentity = [](const BrowserAsset& asset) {
    return std::pair{asset.libraryId, asset.libraryAssetId};
  };
  const auto isSnapshotOf =
    [&](const BrowserAsset& projectAsset, const BrowserAsset& libraryAsset) {
      return !projectAsset.libraryAssetId.empty()
             && libraryIdentity(projectAsset) == libraryIdentity(libraryAsset);
    };

  // The project snapshot is the authoritative copy of a captured library resource, so the
  // library's current state is reported on the snapshot instead of listing the resource
  // twice.
  for (const auto& libraryAsset : libraryAssets)
  {
    if (libraryAsset.libraryAssetId.empty())
    {
      continue;
    }

    const auto snapshot = std::ranges::find_if(
      projectAssets,
      [&](const auto& projectAsset) { return isSnapshotOf(projectAsset, libraryAsset); });
    if (snapshot != projectAssets.end())
    {
      snapshot->projectUpdateAvailable =
        snapshot->projectUpdateAvailable || libraryAsset.projectUpdateAvailable;
      snapshot->missing = snapshot->missing || libraryAsset.missing;
    }
  }

  const auto enginePathKey = [](const BrowserAsset& asset) {
    return kdl::path_to_lower(asset.path.lexically_normal().generic_string());
  };

  auto result = std::vector<BrowserAsset>{};
  result.reserve(projectAssets.size() + libraryAssets.size() + gameAssets.size());
  const auto append = [&](const BrowserAsset& asset) {
    if (asset.path.empty())
    {
      return;
    }

    const auto key = enginePathKey(asset);
    const auto duplicate = std::ranges::any_of(result, [&](const auto& existing) {
      return existing.type == asset.type && enginePathKey(existing) == key;
    });
    if (!duplicate)
    {
      result.push_back(asset);
    }
  };

  for (const auto& asset : projectAssets)
  {
    append(asset);
  }
  for (const auto& asset : libraryAssets)
  {
    const auto captured = std::ranges::any_of(
      projectAssets,
      [&](const auto& projectAsset) { return isSnapshotOf(projectAsset, asset); });
    if (!captured)
    {
      append(asset);
    }
  }
  for (const auto& asset : gameAssets)
  {
    append(asset);
  }

  std::ranges::sort(result, [](const auto& lhs, const auto& rhs) {
    if (lhs.path != rhs.path)
    {
      return lhs.path.generic_string() < rhs.path.generic_string();
    }
    if (lhs.type != rhs.type)
    {
      return lhs.type < rhs.type;
    }
    return lhs.source < rhs.source;
  });
  return result;
}

std::vector<BrowserAsset> filterBrowserAssets(
  const std::vector<BrowserAsset>& assets, const std::optional<BrowserCellType> type)
{
  if (!type)
  {
    return assets;
  }

  auto result = std::vector<BrowserAsset>{};
  std::ranges::copy_if(assets, std::back_inserter(result), [&](const auto& asset) {
    return asset.type == *type;
  });
  return result;
}

std::optional<std::vector<BrowserAsset>> collectBrowserAssets(
  const std::filesystem::path& folderPath,
  const std::vector<std::filesystem::path>& modRoots,
  const std::function<
    Result<std::vector<std::filesystem::path>>(const std::filesystem::path&)>& findAssets,
  const std::function<Result<std::filesystem::path>(const std::filesystem::path&)>&
    makeAbsolute,
  const std::vector<std::pair<BrowserCellType, std::filesystem::path>>& rootFilters)
{
  auto roots = std::vector<std::pair<BrowserCellType, std::filesystem::path>>{};
  if (folderPath.empty())
  {
    roots = rootFilters;
  }
  else
  {
    roots.emplace_back(BrowserCellType::Folder, folderPath);
  }

  auto assets = std::vector<BrowserAsset>{};
  for (const auto& [rootType, rootPath] : roots)
  {
    unused(rootType);
    auto pathsResult = findAssets(rootPath);
    if (pathsResult.is_error())
    {
      continue;
    }

    for (const auto& path : pathsResult.value())
    {
      const auto assetType = assetTypeForExtension(path);
      if (assetType == BrowserCellType::Folder)
      {
        continue;
      }

      auto absPath = std::filesystem::path{};
      if (auto absPathResult = makeAbsolute(path); !absPathResult.is_error())
      {
        absPath = absPathResult.value().lexically_normal();
      }
      else if (path.is_absolute())
      {
        absPath = path.lexically_normal();
      }

      if (!modRoots.empty() && assetType != BrowserCellType::Prefab)
      {
        if (absPath.empty())
        {
          continue;
        }

        const auto inEnabledMod = std::ranges::any_of(modRoots, [&](const auto& modRoot) {
          return kdl::path_has_prefix(absPath, modRoot);
        });
        if (!inEnabledMod)
        {
          continue;
        }
      }

      assets.emplace_back(assetType, path, absPath);
    }
  }

  std::ranges::sort(assets, [](const auto& lhs, const auto& rhs) {
    return lhs.path.generic_string() < rhs.path.generic_string();
  });
  assets.erase(
    std::unique(std::begin(assets), std::end(assets), sameAssetIdentity),
    std::end(assets));

  return assets;
}

AssetWriteTimes assetLastWriteTimes(const std::vector<BrowserAsset>& assets)
{
  auto result = AssetWriteTimes{};
  result.reserve(assets.size());

  for (const auto& asset : assets)
  {
    if (asset.lastModified)
    {
      result.emplace(asset.path, *asset.lastModified);
    }
  }

  return result;
}

std::vector<std::filesystem::path> changedAssetPaths(
  const AssetWriteTimes& oldWriteTimes,
  const AssetWriteTimes& newWriteTimes,
  const std::vector<BrowserAsset>& oldAssets,
  const std::vector<BrowserAsset>& newAssets)
{
  auto changedPaths = std::vector<std::filesystem::path>{};
  const auto oldEntityModelPaths = entityModelAssetPaths(oldAssets);

  for (const auto& asset : newAssets)
  {
    if (!isEntityModelAsset(asset))
    {
      continue;
    }

    // Library entries are identified by their catalog identity, but the map only knows
    // the engine path of the project snapshot. An uncaptured library resource has no such
    // path and cannot be a map dependency yet.
    if (isLibraryAsset(asset))
    {
      const auto old = std::ranges::find_if(oldAssets, [&](const auto& previous) {
        return sameAssetIdentity(previous, asset);
      });
      const auto revisionChanged = old == oldAssets.end()
                                   || old->libraryRevision != asset.libraryRevision
                                   || old->missing != asset.missing;
      if (revisionChanged && !asset.projectPublishPath.empty())
        changedPaths.push_back(asset.projectPublishPath);
      continue;
    }

    if (!asset.assetId.empty())
    {
      const auto old = std::ranges::find_if(oldAssets, [&](const auto& previous) {
        return sameAssetIdentity(previous, asset);
      });
      if (
        old == oldAssets.end() || old->revision != asset.revision
        || old->path != asset.path)
        changedPaths.push_back(asset.path);
      continue;
    }

    if (const auto it = newWriteTimes.find(asset.path); it != std::end(newWriteTimes))
    {
      const auto oldIt = oldWriteTimes.find(asset.path);
      if (oldIt == std::end(oldWriteTimes) || oldIt->second != it->second)
      {
        changedPaths.push_back(asset.path);
      }
    }
  }

  for (const auto& oldPath : oldEntityModelPaths)
  {
    if (std::ranges::none_of(
          newAssets, [&](const auto& asset) { return asset.path == oldPath; }))
    {
      changedPaths.push_back(oldPath);
    }
  }

  std::ranges::sort(changedPaths);
  changedPaths.erase(
    std::unique(std::begin(changedPaths), std::end(changedPaths)),
    std::end(changedPaths));
  return changedPaths;
}

std::vector<ModelBrowserEntry> modelBrowserEntries(
  const std::filesystem::path& rootFolderPath,
  const std::vector<BrowserAsset>& assets,
  const std::filesystem::path& currentFolderPath,
  const QString& searchText,
  const bool includeDefaultRoots,
  const bool searchAllAssets)
{
  const auto trimmedSearchText = searchText.trimmed();
  const auto hasSearch = !trimmedSearchText.isEmpty();
  const auto matches = [&](const std::filesystem::path& path) {
    return !hasSearch
           || pathAsGenericQString(path).contains(trimmedSearchText, Qt::CaseInsensitive);
  };

  auto normalizedCurrentFolderPath = currentFolderPath.lexically_normal();
  if (normalizedCurrentFolderPath == std::filesystem::path{"."})
  {
    normalizedCurrentFolderPath.clear();
  }

  // A search that claims to cover the whole listing must not be limited to the folder the
  // user happens to be in.
  if (hasSearch && searchAllAssets)
  {
    normalizedCurrentFolderPath.clear();
  }

  const auto currentFolderAbs = normalizedCurrentFolderPath.empty()
                                  ? rootFolderPath
                                  : (rootFolderPath / normalizedCurrentFolderPath);

  auto folderChildren = std::vector<std::filesystem::path>{};
  auto assetChildren = std::vector<BrowserAsset>{};

  if (
    includeDefaultRoots && rootFolderPath.empty() && normalizedCurrentFolderPath.empty())
  {
    for (const auto& [type, rootPath] : assetBrowserRoots())
    {
      unused(type);
      if (!hasSearch || matches(rootPath))
      {
        folderChildren.push_back(rootPath);
      }
    }
  }

  for (const auto& asset : assets)
  {
    const auto& assetPath = asset.catalogPath.empty() ? asset.path : asset.catalogPath;
    if (assetPath.empty())
    {
      continue;
    }

    const auto folderPath = assetPath.parent_path();
    if (!currentFolderAbs.empty() && !kdl::path_has_prefix(folderPath, currentFolderAbs))
    {
      continue;
    }

    const auto relFromCurrent = currentFolderAbs.empty()
                                  ? folderPath
                                  : folderPath.lexically_relative(currentFolderAbs);

    const auto assetNameMatches =
      assetSearchMatches(
        QString::fromStdString(asset.displayName),
        pathAsGenericQString(assetPath.filename()),
        asset.libraryMetadata ? asset.libraryMetadata->tags : std::vector<std::string>{},
        trimmedSearchText)
      // The source is shown as a badge, so searching its name has to work whether or not
      // the current listing displays badges.
      || QObject::tr(browserSourceName(asset.source))
           .contains(trimmedSearchText, Qt::CaseInsensitive);

    if (relFromCurrent.empty() || relFromCurrent == std::filesystem::path{"."})
    {
      if (!hasSearch || assetNameMatches)
      {
        assetChildren.push_back(asset);
      }
      continue;
    }

    const auto first = *relFromCurrent.begin();
    const auto firstRelPath = (normalizedCurrentFolderPath / first).lexically_normal();

    if (hasSearch)
    {
      if (assetNameMatches)
      {
        assetChildren.push_back(asset);
        folderChildren.push_back(firstRelPath);
        continue;
      }

      if (matches(first))
      {
        folderChildren.push_back(firstRelPath);
      }
      continue;
    }

    folderChildren.push_back(firstRelPath);
  }

  std::ranges::sort(folderChildren, [](const auto& a, const auto& b) {
    return a.generic_string() < b.generic_string();
  });
  folderChildren.erase(
    std::unique(std::begin(folderChildren), std::end(folderChildren)),
    std::end(folderChildren));

  std::ranges::sort(assetChildren, [&](const auto& a, const auto& b) {
    if (hasSearch)
    {
      return a.path.generic_string() < b.path.generic_string();
    }
    if (a.type != b.type)
    {
      return a.type < b.type;
    }
    return a.path.filename().generic_string() < b.path.filename().generic_string();
  });

  // The breadcrumb and the folder tree navigate upwards; a ".." card would only repeat
  // that operation while consuming a grid slot next to the assets.
  auto entries = std::vector<ModelBrowserEntry>{};
  for (const auto& folderRelPath : folderChildren)
  {
    entries.push_back(
      {BrowserCellData{BrowserCellType::Folder, folderRelPath},
       folderRelPath.filename().generic_string()});
  }

  for (const auto& asset : assetChildren)
  {
    const auto& assetPath = asset.path;
    const auto titlePath =
      hasSearch
        ? (currentFolderAbs.empty() ? assetPath
                                    : assetPath.lexically_relative(currentFolderAbs))
        : assetPath.filename();
    // The title stays the resource name: the browser marks a non-game source with a
    // badge, so two sources with the same name are told apart without truncating either
    // title.
    auto title = asset.projectId.empty() ? titlePath.generic_string() : asset.displayName;
    entries.push_back({BrowserCellData{asset.type, assetPath}, std::move(title)});
  }

  return entries;
}

} // namespace tb::ui
