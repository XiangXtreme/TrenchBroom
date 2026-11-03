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

#include "base/Result.h"
#include "ui/AssetBrowserModel.h"

#include "vm/vec.h"

#include <cstddef>
#include <cstdint>
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
class ProjectSession;

struct AutomationAssetSearchOptions
{
  std::string query;
  std::optional<BrowserCellType> type;
  size_t limit = 50u;
};

/**
 * One asset as automation sees it, regardless of where it lives.
 *
 * Project and library assets carry a stable id; game files are identified by their engine
 * path. Displays names and virtual paths are for humans, never for identity.
 */
struct AutomationAssetSummary
{
  std::string scope; // "game", "project" or "library"
  std::string id;
  std::string libraryId;
  std::string type;        // BrowserCellType name, lowercase
  std::string assetType;   // precise project type (material, skybox, ...); empty for game
  std::string path;        // engine path (game) or virtual path (project, library)
  std::string publishPath; // engine-facing path for project assets
  std::string absolutePath;
  std::string displayName;
  std::uint64_t revision = 0;
  bool missing = false;
  std::string group;
  std::string nativeFormat;
};

struct AutomationAssetQuery
{
  std::string query;
  std::optional<BrowserCellType> type;
  std::size_t limit = 50u;
};

/** GoldSrc assets visible to the map, using the asset browser's mod filtering. */
std::optional<std::vector<AutomationAssetSummary>> searchAutomationGameAssets(
  mdl::Map& map, const AutomationAssetQuery& query);

/** Assets captured in the owning project. */
Result<std::vector<AutomationAssetSummary>> searchAutomationProjectAssets(
  ProjectSession& project, const AutomationAssetQuery& query);

/** Assets published by every available library the project is attached to. */
Result<std::vector<AutomationAssetSummary>> searchAutomationLibraryAssets(
  ProjectSession& project, const AutomationAssetQuery& query);

/** Looks up a game asset by engine path or absolute path. */
std::optional<AutomationAssetSummary> findAutomationGameAsset(
  mdl::Map& map, const std::string& pathOrAbsolutePath);

/** Looks up a project asset by id or virtual path. */
Result<std::optional<AutomationAssetSummary>> findAutomationProjectAsset(
  ProjectSession& project, const std::string& idOrPath);

/** Looks up a library asset by `libraryId/assetId`, unique asset id, or virtual path. */
Result<std::optional<AutomationAssetSummary>> findAutomationLibraryAsset(
  ProjectSession& project, const std::string& idOrPath);

/** Imports a file into the project, using the same rules as the project window. */
Result<AutomationAssetSummary> importAutomationAssetFile(
  ProjectSession& project,
  const std::filesystem::path& path,
  const std::string& name = {},
  const std::string& virtualPath = {});

/** Captures one library revision into the project and returns the project asset. */
Result<AutomationAssetSummary> captureAutomationLibraryAsset(
  ProjectSession& project, const std::string& libraryId, const std::string& assetId);

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
