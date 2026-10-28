/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.
 */

#pragma once

#include "base/Result.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tb::project
{

enum class AssetType
{
  Map,
  Texture,
  Material,
  Model,
  Sprite,
  Sound,
  Skybox,
  Prefab,
};

enum class ContentKind
{
  Original,
  Compatible,
};

struct AssetImport
{
  AssetType type = AssetType::Texture;
  std::string virtualPath;
  std::string displayName;
  std::string sourceName;
  std::vector<std::uint8_t> original;
  std::vector<std::uint8_t> compatible;
  std::string conversionRecipe;
};

struct Asset
{
  std::string id;
  AssetType type = AssetType::Texture;
  std::string virtualPath;
  std::string displayName;
  std::string sourceName;
  std::uint64_t revision = 0;
  std::string conversionRecipe;
  // Stable engine-facing path, independent from display names and virtual folders.
  std::string publishPath;

  friend bool operator==(const Asset&, const Asset&) = default;
};

struct ProjectInfo
{
  std::string id;
  std::string name;
  std::string gameId;
};

struct AssetDependency
{
  std::string assetId;
  std::string dependencyId;
  std::string kind;

  friend bool operator==(const AssetDependency&, const AssetDependency&) = default;
};

struct Checkpoint
{
  std::string id;
  std::string name;
  std::string description;
  std::string createdAt;
};

struct AssetChange
{
  std::string assetId;
  std::optional<Asset> before;
  std::optional<Asset> after;
};

struct ProjectChanges
{
  std::vector<AssetChange> assets;
  std::vector<AssetDependency> addedDependencies;
  std::vector<AssetDependency> removedDependencies;
};

const char* assetTypeName(AssetType type);
Result<AssetType> parseAssetType(const std::string& name);

} // namespace tb::project
