/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 */

#pragma once

#include <QString>

#include "ui/AssetBrowserModel.h"

#include "vm/vec.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tb::mdl
{
class Map;
class EntityNode;
} // namespace tb::mdl

namespace tb::ui
{

struct AutomationAssetSearchOptions
{
  std::string query;
  std::optional<BrowserCellType> type;
  size_t limit = 50u;
};

/** A protocol-neutral point entity placement for a GoldSrc asset. */
struct AutomationAssetPlacementSpec
{
  std::filesystem::path path;
  BrowserCellType type = BrowserCellType::Model;
  std::string classname;
  std::string property;
  vm::vec3d origin = vm::vec3d{0.0, 0.0, 0.0};
};

/**
 * Collects the GoldSrc assets visible to a map using the same enabled-mod
 * filtering as the asset browser. A null result indicates a filesystem failure.
 */
std::optional<std::vector<BrowserAsset>> collectAutomationAssets(mdl::Map& map);

std::optional<std::vector<BrowserAsset>> searchAutomationAssets(
  mdl::Map& map, const AutomationAssetSearchOptions& options);

/**
 * Validates the asset kind and creates an unattached point entity node. The
 * caller retains ownership until it adds the node through a native command.
 */
std::unique_ptr<mdl::EntityNode> buildAutomationAssetEntity(
  const AutomationAssetPlacementSpec& spec, QString& error);

} // namespace tb::ui
