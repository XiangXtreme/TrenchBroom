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
#include "fs/TraversalMode.h"
#include "fs/WadFileSystem.h"
#include "gl/Texture.h"
#include "mdl/GameFileSystem.h"
#include "mdl/LoadMipTexture.h"
#include "ui/PrefabAsset.h"
#include "ui/QPathUtils.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <set>
#include <span>
#include <vector>

namespace tb::ui
{

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
  auto wad = fs::WadFileSystem{file.value()};
  if (wad.reload().is_error())
    return {AssetPreviewStatus::Error, std::nullopt};
  const auto entries = wad.find({}, fs::TraversalMode::Flat);
  if (entries.is_error())
    return {AssetPreviewStatus::Error, std::nullopt};
  // A WAD collection is represented by its first decodable GoldSrc texture.
  // Individual textures remain available through the material selector.
  for (const auto& entry : entries.value())
  {
    if (!mdl::isHlMipTexture(entry))
      continue;
    const auto lump = wad.openFile(entry);
    if (lump.is_error())
      continue;
    auto reader = lump.value()->reader().buffer();
    const auto name = entry.stem().string();
    auto decoded = mdl::loadHlMipTexture(reader, name.starts_with('{'));
    if (decoded.is_error())
      continue;
    const auto& texture = decoded.value();
    const auto& buffers = texture.buffersIfLoaded();
    if (buffers.empty())
      continue;
    const auto& pixels = buffers.front();
    return {
      AssetPreviewStatus::Ready,
      GoldSrcSpritePreview{
        texture.width(),
        texture.height(),
        std::vector<std::uint8_t>{pixels.data(), pixels.data() + pixels.size()}}};
  }
  return {AssetPreviewStatus::Error, std::nullopt};
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

  image = image.convertToFormat(QImage::Format_RGBA8888);
  auto rgba = std::vector<std::uint8_t>{};
  rgba.resize(size_t(image.sizeInBytes()));
  std::memcpy(rgba.data(), image.constBits(), rgba.size());

  return {
    AssetPreviewStatus::Ready,
    GoldSrcSpritePreview{size_t(image.width()), size_t(image.height()), std::move(rgba)}};
}

AssetPreviewMap loadAssetPreviews(
  const AssetPreviewProvider& provider,
  const std::vector<BrowserAsset>& assets,
  const std::set<std::filesystem::path>& paths)
{
  auto previews = AssetPreviewMap{};
  for (const auto& asset : assets)
  {
    if (!paths.empty() && !paths.contains(asset.path))
    {
      continue;
    }

    const auto preview = provider.preview(asset);
    if (preview.status != AssetPreviewStatus::Unsupported)
    {
      previews.emplace(asset.path, preview);
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
