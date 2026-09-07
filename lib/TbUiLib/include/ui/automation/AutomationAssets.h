/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 */

#pragma once

#include "ui/AssetBrowserModel.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace tb::mdl
{
class Map;
}

namespace tb::ui
{

struct AutomationAssetSearchOptions
{
  std::string query;
  std::optional<BrowserCellType> type;
  size_t limit = 50u;
};

/**
 * Collects the GoldSrc assets visible to a map using the same enabled-mod
 * filtering as the asset browser. A null result indicates a filesystem failure.
 */
std::optional<std::vector<BrowserAsset>> collectAutomationAssets(mdl::Map& map);

std::optional<std::vector<BrowserAsset>> searchAutomationAssets(
  mdl::Map& map, const AutomationAssetSearchOptions& options);

} // namespace tb::ui
