/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 */

#include "ui/automation/AutomationAssets.h"

#include "fs/PathMatcher.h"
#include "fs/TraversalMode.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityProperties.h"
#include "mdl/GameFileSystem.h"
#include "mdl/Map.h"
#include "mdl/Map_Assets.h"
#include "mdl/Map_World.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace tb::ui
{
namespace
{

std::string lowerCase(std::string value)
{
  std::ranges::transform(value, value.begin(), [](const unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

} // namespace

std::optional<std::vector<BrowserAsset>> collectAutomationAssets(mdl::Map& map)
{
  const auto enabledMods = mdl::enabledMods(map);
  if (enabledMods.empty())
  {
    return std::vector<BrowserAsset>{};
  }

  auto modRoots = std::vector<std::filesystem::path>{};
  modRoots.reserve(enabledMods.size());
  for (const auto& mod : enabledMods)
  {
    modRoots.push_back((map.gamePath() / std::filesystem::path{mod}).lexically_normal());
  }

  const auto& fs = map.gameFileSystem();
  return collectBrowserAssets(
    {},
    modRoots,
    [&](const auto& rootPath) {
      return fs.find(
        rootPath,
        fs::TraversalMode::Recursive,
        fs::makeExtensionPathMatcher(goldSrcAssetExtensions()));
    },
    [&](const auto& path) { return fs.makeAbsolute(path); });
}

std::optional<std::vector<BrowserAsset>> searchAutomationAssets(
  mdl::Map& map, const AutomationAssetSearchOptions& options)
{
  auto assets = collectAutomationAssets(map);
  if (!assets)
  {
    return std::nullopt;
  }
  if (options.limit == 0u)
  {
    return std::vector<BrowserAsset>{};
  }

  const auto query = lowerCase(options.query);
  auto results = std::vector<BrowserAsset>{};
  results.reserve(std::min(assets->size(), options.limit));
  for (const auto& asset : *assets)
  {
    if (options.type && asset.type != *options.type)
    {
      continue;
    }

    const auto path = lowerCase(asset.path.generic_string());
    if (
      !query.empty() && path.find(query) == std::string::npos
      && lowerCase(asset.displayName).find(query) == std::string::npos)
    {
      continue;
    }

    results.push_back(asset);
    if (results.size() >= options.limit)
    {
      break;
    }
  }

  return results;
}

std::unique_ptr<mdl::EntityNode> buildAutomationAssetEntity(
  const AutomationAssetPlacementSpec& spec, QString& error)
{
  if (spec.path.empty())
  {
    error = "asset path must not be empty";
    return nullptr;
  }
  if (assetTypeForExtension(spec.path) != spec.type)
  {
    error = "asset path does not match the requested asset type";
    return nullptr;
  }
  if (spec.classname.empty())
  {
    error = "asset classname must not be empty";
    return nullptr;
  }
  if (spec.property.empty())
  {
    error = "asset property must not be empty";
    return nullptr;
  }

  auto entity = mdl::Entity{{{mdl::EntityPropertyKeys::Classname, spec.classname}}};
  entity.addOrUpdateProperty(spec.property, spec.path.generic_string());
  entity.setOrigin(spec.origin);
  return std::make_unique<mdl::EntityNode>(std::move(entity));
}

} // namespace tb::ui
