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

#include "ui/AssetBrowserModel.h"
#include "ui/GoldSrcSpritePreview.h"

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace tb::mdl
{
class GameFileSystem;
}

namespace tb::ui
{

enum class AssetPreviewStatus
{
  Ready,
  Missing,
  Error,
  Unsupported,
};

struct AssetPreviewState
{
  AssetPreviewStatus status = AssetPreviewStatus::Unsupported;
  std::optional<GoldSrcSpritePreview> sprite = std::nullopt;
  std::filesystem::path soundPath{};
  std::string error{};

  friend bool operator==(const AssetPreviewState&, const AssetPreviewState&) = default;
};

class AssetPreviewProvider
{
private:
  const mdl::GameFileSystem& m_gameFileSystem;

public:
  explicit AssetPreviewProvider(const mdl::GameFileSystem& gameFileSystem);

  AssetPreviewState preview(const BrowserAsset& asset) const;

private:
  AssetPreviewState spritePreview(const BrowserAsset& asset) const;
  AssetPreviewState soundPreview(const BrowserAsset& asset) const;
  AssetPreviewState prefabPreview(const BrowserAsset& asset) const;
  AssetPreviewState texturePreview(const BrowserAsset& asset) const;
};

using AssetPreviewPathResolver =
  std::function<Result<std::filesystem::path>(const BrowserAsset&)>;

using AssetPreviewMap = std::map<std::filesystem::path, AssetPreviewState>;

AssetPreviewMap loadAssetPreviews(
  const AssetPreviewProvider& provider, const std::vector<BrowserAsset>& assets);
AssetPreviewMap loadAssetPreviews(
  const AssetPreviewProvider& provider,
  const std::vector<BrowserAsset>& assets,
  const std::set<std::filesystem::path>& paths,
  const AssetPreviewPathResolver& resolver = {});

} // namespace tb::ui
