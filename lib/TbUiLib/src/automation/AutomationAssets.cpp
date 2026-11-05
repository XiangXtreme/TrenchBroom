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
#include "gl/Material.h"
#include "gl/MaterialManager.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityProperties.h"
#include "mdl/GameFileSystem.h"
#include "mdl/Map.h"
#include "mdl/Map_Assets.h"
#include "mdl/Map_World.h"
#include "project/AssetLibrary.h"
#include "project/LibrarySync.h"
#include "project/ProjectAsset.h"
#include "ui/ProjectSession.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string_view>

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

std::string browserTypeName(const BrowserCellType type)
{
  switch (type)
  {
  case BrowserCellType::Folder:
    return "folder";
  case BrowserCellType::Model:
    return "model";
  case BrowserCellType::Sprite:
    return "sprite";
  case BrowserCellType::Sound:
    return "sound";
  case BrowserCellType::Prefab:
    return "prefab";
  case BrowserCellType::Map:
    return "map";
  case BrowserCellType::Texture:
    return "texture";
  case BrowserCellType::Material:
    return "material";
  }
  return "unknown";
}

/** The broad automation texture type includes material and skybox assets. */
BrowserCellType browserTypeForProjectType(const project::AssetType type)
{
  switch (type)
  {
  case project::AssetType::Map:
    return BrowserCellType::Map;
  case project::AssetType::Model:
    return BrowserCellType::Model;
  case project::AssetType::Sprite:
    return BrowserCellType::Sprite;
  case project::AssetType::Sound:
    return BrowserCellType::Sound;
  case project::AssetType::Prefab:
    return BrowserCellType::Prefab;
  case project::AssetType::Texture:
  case project::AssetType::Material:
  case project::AssetType::Skybox:
    return BrowserCellType::Texture;
  }
  return BrowserCellType::Texture;
}

std::string projectTypeName(const project::AssetType type)
{
  switch (type)
  {
  case project::AssetType::Map:
    return "map";
  case project::AssetType::Texture:
    return "texture";
  case project::AssetType::Material:
    return "material";
  case project::AssetType::Model:
    return "model";
  case project::AssetType::Sprite:
    return "sprite";
  case project::AssetType::Sound:
    return "sound";
  case project::AssetType::Skybox:
    return "skybox";
  case project::AssetType::Prefab:
    return "prefab";
  }
  return "unknown";
}

bool matchesAssetType(
  const AutomationAssetQuery& query, const AutomationAssetSummary& asset)
{
  return query.assetType.empty()
         || lowerCase(query.assetType) == lowerCase(asset.assetType);
}

bool matchesQuery(
  const std::string& query, std::initializer_list<std::string_view> fields)
{
  if (query.empty())
  {
    return true;
  }
  const auto needle = lowerCase(query);
  return std::ranges::any_of(fields, [&](const auto& field) {
    return lowerCase(std::string{field}).find(needle) != std::string::npos;
  });
}

AutomationAssetSummary summaryForGame(const BrowserAsset& asset)
{
  auto result = AutomationAssetSummary{};
  result.scope = "game";
  result.type = browserTypeName(asset.type);
  result.assetType = result.type;
  result.path = asset.path.generic_string();
  result.publishPath = result.path;
  result.absolutePath = asset.absolutePath.generic_string();
  result.displayName = asset.displayName;
  return result;
}

/**
 * Game textures are not files: a face references a material by name, so the name is the
 * stable identity and also the value to write when applying the asset.
 */
AutomationAssetSummary summaryForMaterial(const gl::Material& material)
{
  auto result = AutomationAssetSummary{};
  result.scope = "game";
  result.id = material.name();
  result.type = browserTypeName(BrowserCellType::Texture);
  result.assetType = "texture";
  result.path = material.name();
  result.publishPath = material.name();
  result.absolutePath = material.absolutePath().generic_string();
  result.displayName = material.name();
  result.revision = 1;
  result.group = material.collectionName();
  result.nativeFormat = "material";
  return result;
}

AutomationAssetSummary summaryForProject(const project::Asset& asset)
{
  auto result = AutomationAssetSummary{};
  result.scope = "project";
  result.id = asset.id;
  result.libraryId = asset.library.libraryId;
  result.type = browserTypeName(browserTypeForProjectType(asset.type));
  result.assetType = projectTypeName(asset.type);
  result.path = asset.virtualPath;
  result.publishPath = asset.publishPath;
  result.displayName = asset.displayName;
  result.revision = asset.revision;
  return result;
}

AutomationAssetSummary summaryForLibrary(
  const std::string& libraryId, const project::LibraryAsset& asset)
{
  auto result = AutomationAssetSummary{};
  result.scope = "library";
  result.id = asset.id;
  result.libraryId = libraryId;
  result.type = browserTypeName(browserTypeForProjectType(asset.type));
  result.assetType = projectTypeName(asset.type);
  result.path = asset.virtualPath;
  result.displayName = asset.displayName;
  result.revision = asset.revision;
  result.missing = asset.missing;
  result.group = asset.group;
  result.nativeFormat = asset.nativeFormat;
  return result;
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

std::optional<std::vector<AutomationAssetSummary>> searchAutomationGameAssets(
  mdl::Map& map, const AutomationAssetQuery& query)
{
  const auto assets = searchAutomationAssets(
    map, AutomationAssetSearchOptions{query.query, query.type, query.limit});
  if (!assets)
  {
    return std::nullopt;
  }

  auto result = std::vector<AutomationAssetSummary>{};
  result.reserve(assets->size());
  for (const auto& asset : *assets)
  {
    auto summary = summaryForGame(asset);
    if (!matchesAssetType(query, summary))
    {
      continue;
    }
    result.push_back(std::move(summary));
  }

  // Game textures live in the material manager, not in the model/sprite/sound scan.
  if (!query.type || *query.type == BrowserCellType::Texture)
  {
    for (const auto* material : map.materialManager().materials())
    {
      auto summary = summaryForMaterial(*material);
      if (
        !matchesAssetType(query, summary)
        || !matchesQuery(
          query.query,
          {material->name(),
           material->collectionName(),
           material->relativePath().generic_string()}))
      {
        continue;
      }
      result.push_back(std::move(summary));
      if (result.size() >= query.limit)
      {
        break;
      }
    }
  }
  return result;
}

std::optional<AutomationAssetSummary> findAutomationGameAsset(
  mdl::Map& map, const std::string& pathOrAbsolutePath)
{
  const auto assets = collectAutomationAssets(map);
  if (!assets)
  {
    return std::nullopt;
  }

  const auto key = lowerCase(pathOrAbsolutePath);
  for (const auto& asset : *assets)
  {
    if (
      lowerCase(asset.path.generic_string()) == key
      || lowerCase(asset.absolutePath.generic_string()) == key)
    {
      return summaryForGame(asset);
    }
  }

  // A texture is identified by its material name; faces store exactly this value.
  for (const auto* material : map.materialManager().materials())
  {
    if (lowerCase(material->name()) == key)
    {
      return summaryForMaterial(*material);
    }
  }
  return std::nullopt;
}

Result<std::vector<AutomationAssetSummary>> searchAutomationProjectAssets(
  ProjectSession& project, const AutomationAssetQuery& query)
{
  auto result = std::vector<AutomationAssetSummary>{};
  if (query.limit == 0u)
  {
    return result;
  }

  const auto assets = project.store().assets();
  if (assets.is_error())
  {
    return std::get<Error>(assets.error());
  }

  for (const auto& asset : assets.value())
  {
    if (query.type && browserTypeForProjectType(asset.type) != *query.type)
    {
      continue;
    }
    if (!matchesQuery(
          query.query,
          {asset.virtualPath, asset.publishPath, asset.displayName, asset.sourceName}))
    {
      continue;
    }

    auto summary = summaryForProject(asset);
    if (!matchesAssetType(query, summary))
    {
      continue;
    }
    result.push_back(std::move(summary));
    if (result.size() >= query.limit)
    {
      break;
    }
  }
  return result;
}

Result<std::vector<AutomationAssetSummary>> searchAutomationLibraryAssets(
  ProjectSession& project, const AutomationAssetQuery& query)
{
  auto result = std::vector<AutomationAssetSummary>{};
  if (query.limit == 0u)
  {
    return result;
  }

  const auto connections = project.libraries();
  if (connections.is_error())
  {
    return std::get<Error>(connections.error());
  }

  for (const auto& connection : connections.value())
  {
    if (!connection.available)
    {
      continue;
    }
    const auto state = project.libraryState(connection.info.id, false);
    if (state.is_error())
    {
      // An unreadable library must not hide the assets of the readable ones.
      continue;
    }

    for (const auto& asset : state.value().assets)
    {
      if (query.type && browserTypeForProjectType(asset.type) != *query.type)
      {
        continue;
      }
      if (!matchesQuery(
            query.query,
            {asset.virtualPath, asset.displayName, asset.sourceMember, asset.group}))
      {
        continue;
      }

      auto summary = summaryForLibrary(connection.info.id, asset);
      if (!matchesAssetType(query, summary))
      {
        continue;
      }
      result.push_back(std::move(summary));
      if (result.size() >= query.limit)
      {
        return result;
      }
    }
  }
  return result;
}

Result<std::optional<AutomationAssetSummary>> findAutomationProjectAsset(
  ProjectSession& project, const std::string& idOrPath)
{
  const auto byId = project.store().findAsset(idOrPath);
  if (byId.is_error())
  {
    return std::get<Error>(byId.error());
  }
  if (byId.value())
  {
    return std::optional{summaryForProject(*byId.value())};
  }

  const auto byPath = project.store().findAssetByPath(idOrPath);
  if (byPath.is_error())
  {
    return std::get<Error>(byPath.error());
  }
  if (byPath.value())
  {
    return std::optional{summaryForProject(*byPath.value())};
  }
  return std::optional<AutomationAssetSummary>{};
}

Result<std::optional<AutomationAssetSummary>> findAutomationLibraryAsset(
  ProjectSession& project, const std::string& idOrPath)
{
  const auto connections = project.libraries();
  if (connections.is_error())
  {
    return std::get<Error>(connections.error());
  }

  auto matches = std::vector<AutomationAssetSummary>{};
  for (const auto& connection : connections.value())
  {
    if (!connection.available)
    {
      continue;
    }
    const auto state = project.libraryState(connection.info.id, false);
    if (state.is_error())
    {
      continue;
    }

    const auto qualifiedPrefix = connection.info.id + "/";
    for (const auto& asset : state.value().assets)
    {
      if (
        idOrPath == asset.id || idOrPath == asset.virtualPath
        || idOrPath == qualifiedPrefix + asset.id)
      {
        matches.push_back(summaryForLibrary(connection.info.id, asset));
      }
    }
  }

  if (matches.size() > 1u)
  {
    return Error{
      "The library asset id is ambiguous; pass library_id or use libraryId/assetId."};
  }
  if (matches.empty())
  {
    return std::optional<AutomationAssetSummary>{};
  }
  return std::optional{matches.front()};
}

Result<AutomationAssetSummary> importAutomationAssetFile(
  ProjectSession& project,
  const std::filesystem::path& path,
  const std::string& name,
  const std::string& virtualPath)
{
  auto file = std::ifstream{path, std::ios::binary | std::ios::ate};
  if (!file)
  {
    return Error{"Could not read " + path.generic_string()};
  }
  const auto size = file.tellg();
  if (size <= 0)
  {
    return Error{"The asset file is empty: " + path.generic_string()};
  }
  auto bytes = std::vector<std::uint8_t>(static_cast<std::size_t>(size));
  file.seekg(0);
  file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
  if (!file)
  {
    return Error{"Could not read " + path.generic_string()};
  }

  const auto extension = lowerCase(path.extension().string());
  auto import = project::AssetImport{};
  auto folder = std::string{};
  if (extension == ".map")
  {
    import.type = project::AssetType::Map;
    folder = "maps/";
  }
  else if (extension == ".mdl")
  {
    import.type = project::AssetType::Model;
    folder = "models/";
  }
  else if (extension == ".spr")
  {
    import.type = project::AssetType::Sprite;
    folder = "sprites/";
  }
  else if (extension == ".wav")
  {
    import.type = project::AssetType::Sound;
    folder = "sound/";
  }
  else if (extension == ".tbprefab")
  {
    import.type = project::AssetType::Prefab;
    folder = "prefabs/";
  }
  else if (
    extension == ".png" || extension == ".bmp" || extension == ".tga"
    || extension == ".jpg" || extension == ".wad")
  {
    import.type = project::AssetType::Texture;
    folder = "textures/";
  }
  else
  {
    return Error{"Unsupported project asset type: " + extension};
  }

  import.sourceName = path.filename().string();
  import.virtualPath = virtualPath.empty() ? folder + import.sourceName : virtualPath;
  import.displayName = name.empty() ? path.stem().string() : name;
  import.original = bytes;
  import.compatible = bytes;
  import.conversionRecipe =
    R"({"importer":"native-copy","version":1,"engineValidated":false})";

  const auto imported = project.importAsset(import);
  if (imported.is_error())
  {
    return std::get<Error>(imported.error());
  }
  return summaryForProject(imported.value());
}

Result<AutomationAssetSummary> captureAutomationLibraryAsset(
  ProjectSession& project, const std::string& libraryId, const std::string& assetId)
{
  if (assetId.empty())
  {
    return Error{"The library asset id must not be empty."};
  }

  auto resolvedLibrary = libraryId;
  auto resolvedAsset = assetId;
  if (resolvedLibrary.empty())
  {
    const auto found = findAutomationLibraryAsset(project, assetId);
    if (found.is_error())
    {
      return std::get<Error>(found.error());
    }
    if (!found.value())
    {
      return Error{"No library asset matches '" + assetId + "'."};
    }
    resolvedLibrary = found.value()->libraryId;
    resolvedAsset = found.value()->id;
  }

  const auto captured = project.captureLibraryAsset(resolvedLibrary, resolvedAsset);
  if (captured.is_error())
  {
    return std::get<Error>(captured.error());
  }
  return summaryForProject(captured.value());
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
