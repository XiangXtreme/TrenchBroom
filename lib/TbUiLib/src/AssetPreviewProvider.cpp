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

#include "ui/AssetPreviewProvider.h"

#include <QImage>

#include "fs/DiskIO.h"
#include "fs/File.h"
#include "fs/PathInfo.h"
#include "fs/Reader.h"
#include "mdl/GameFileSystem.h"
#include "ui/AssetInspection.h"
#include "ui/PrefabAsset.h"
#include "ui/QPathUtils.h"

#include <cstdint>
#include <memory>
#include <set>
#include <span>
#include <vector>

namespace tb::ui
{
AssetPreviewState assetImagePreview(const QImage& image)
{
  if (image.isNull())
    return {AssetPreviewStatus::Error, std::nullopt};
  const auto rgba = image.convertToFormat(QImage::Format_RGBA8888);
  return {
    AssetPreviewStatus::Ready,
    GoldSrcSpritePreview{
      size_t(rgba.width()),
      size_t(rgba.height()),
      std::vector<std::uint8_t>{
        rgba.constBits(), rgba.constBits() + rgba.sizeInBytes()}}};
}

AssetPreviewProvider::AssetPreviewProvider(const mdl::GameFileSystem& gameFileSystem)
  : m_gameFileSystem{gameFileSystem}
{
}

AssetPreviewState AssetPreviewProvider::preview(const BrowserAsset& asset) const
{
  switch (asset.type)
  {
  case BrowserCellType::Sprite:
    return spritePreview(asset);
  case BrowserCellType::Sound:
    return soundPreview(asset);
  case BrowserCellType::Prefab:
    return prefabPreview(asset);
  case BrowserCellType::Texture:
    return texturePreview(asset);
  case BrowserCellType::Folder:
  case BrowserCellType::Model:
  case BrowserCellType::Map:
    return {AssetPreviewStatus::Unsupported, std::nullopt};
  }

  return {AssetPreviewStatus::Unsupported, std::nullopt};
}

AssetPreviewState AssetPreviewProvider::texturePreview(const BrowserAsset& asset) const
{
  const auto& path = asset.absolutePath.empty() ? asset.path : asset.absolutePath;
  const auto file = fs::Disk::openFile(path);
  if (file.is_error())
    return {AssetPreviewStatus::Missing, std::nullopt};
  auto reader = file.value()->reader().buffer();
  const auto bytes = reader.stringView();
  const auto image =
    inspection::decodeImage(inspection::Bytes{bytes.begin(), bytes.end()});
  return image.is_success() ? assetImagePreview(image.value())
                            : AssetPreviewState{AssetPreviewStatus::Error, std::nullopt};
}

AssetPreviewState AssetPreviewProvider::spritePreview(const BrowserAsset& asset) const
{
  const auto& path = asset.absolutePath.empty() ? asset.path : asset.absolutePath;

  auto file = std::shared_ptr<fs::File>{};
  if (path.is_absolute())
  {
    auto fileResult = fs::Disk::openFile(path);
    if (fileResult.is_error())
    {
      return {AssetPreviewStatus::Missing, std::nullopt};
    }
    file = fileResult.value();
  }
  else
  {
    auto fileResult = m_gameFileSystem.openFile(path);
    if (fileResult.is_error())
    {
      return {AssetPreviewStatus::Missing, std::nullopt};
    }
    file = fileResult.value();
  }

  auto reader = file->reader().buffer();
  const auto bytes = reader.stringView();
  auto preview = loadGoldSrcSpritePreview(
    std::span{reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()});
  if (!preview)
  {
    return {AssetPreviewStatus::Error, std::nullopt};
  }

  return {AssetPreviewStatus::Ready, std::move(preview)};
}

AssetPreviewState AssetPreviewProvider::soundPreview(const BrowserAsset& asset) const
{
  auto soundPath = std::filesystem::path{};
  if (!asset.absolutePath.empty() && asset.absolutePath.is_absolute())
  {
    soundPath = asset.absolutePath;
  }
  else if (asset.path.is_absolute())
  {
    soundPath = asset.path;
  }
  else if (auto absPathResult = m_gameFileSystem.makeAbsolute(asset.path);
           !absPathResult.is_error())
  {
    soundPath = absPathResult.value();
  }

  if (soundPath.empty())
  {
    return {AssetPreviewStatus::Missing, std::nullopt};
  }

  auto fileResult = fs::Disk::openFile(soundPath);
  return fileResult.is_error()
           ? AssetPreviewState{AssetPreviewStatus::Missing, std::nullopt}
           : AssetPreviewState{AssetPreviewStatus::Ready, std::nullopt, soundPath};
}

AssetPreviewState AssetPreviewProvider::prefabPreview(const BrowserAsset& asset) const
{
  const auto& path = asset.absolutePath.empty() ? asset.path : asset.absolutePath;
  const auto thumbnailPath = prefabThumbnailPath(path);
  if (
    !thumbnailPath.is_absolute()
    || fs::Disk::pathInfo(thumbnailPath) == fs::PathInfo::Unknown)
  {
    return {AssetPreviewStatus::Missing, std::nullopt};
  }

  auto image = QImage{pathAsQString(thumbnailPath)};
  if (image.isNull())
  {
    return {AssetPreviewStatus::Error, std::nullopt};
  }

  return assetImagePreview(image);
}

AssetPreviewMap loadAssetPreviews(
  const AssetPreviewProvider& provider,
  const std::vector<BrowserAsset>& assets,
  const std::set<std::filesystem::path>& paths,
  const AssetPreviewPathResolver& resolver)
{
  auto pending = std::vector<const BrowserAsset*>{};
  auto unresolved = std::vector<BrowserAsset>{};
  for (const auto& asset : assets)
  {
    if (!paths.empty() && !paths.contains(asset.path))
    {
      continue;
    }

    if (
      asset.type == BrowserCellType::Folder || asset.type == BrowserCellType::Model
      || asset.type == BrowserCellType::Map)
      continue;

    pending.push_back(&asset);
    if (isLibraryAsset(asset) && asset.absolutePath.empty())
    {
      unresolved.push_back(asset);
    }
  }

  // Library resources are resolved in one request: a page of visible cells would
  // otherwise reopen the library catalog once per cell.
  auto files = std::map<std::filesystem::path, std::filesystem::path>{};
  auto fileError = std::string{};
  if (!unresolved.empty())
  {
    if (!resolver)
    {
      fileError = "No library preview resolver is available.";
    }
    else if (auto resolved = resolver(unresolved); resolved.is_error())
    {
      fileError = std::get<Error>(resolved.error()).msg;
    }
    else
    {
      files = std::move(resolved).value();
    }
  }

  auto previews = AssetPreviewMap{};
  for (const auto* asset : pending)
  {
    auto resolved = *asset;
    if (isLibraryAsset(*asset) && asset->absolutePath.empty())
    {
      const auto file = files.find(asset->path);
      if (file == files.end())
      {
        previews.emplace(
          asset->path,
          AssetPreviewState{
            AssetPreviewStatus::Error,
            std::nullopt,
            {},
            fileError.empty()
              ? "The library returned no absolute compatible preview file."
              : fileError});
        continue;
      }
      if (!file->second.is_absolute())
      {
        previews.emplace(
          asset->path,
          AssetPreviewState{
            AssetPreviewStatus::Error,
            std::nullopt,
            {},
            "The library returned no absolute compatible preview file."});
        continue;
      }
      resolved.absolutePath = file->second;
    }
    const auto preview = provider.preview(resolved);
    if (preview.status != AssetPreviewStatus::Unsupported)
    {
      previews.emplace(asset->path, preview);
    }
  }
  return previews;
}

AssetPreviewMap loadAssetPreviews(
  const AssetPreviewProvider& provider, const std::vector<BrowserAsset>& assets)
{
  return loadAssetPreviews(provider, assets, {});
}

} // namespace tb::ui
