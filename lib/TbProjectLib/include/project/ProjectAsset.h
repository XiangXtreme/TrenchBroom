/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom.
*/

#pragma once

#include "base/Result.h"

#include <cstddef>
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

/**
 * Provenance of a project asset that was captured from an asset library.
 *
 * An empty `libraryAssetId` means the asset was imported directly and has no library
 * origin. The recorded revision and content hash describe the library state that was
 * copied into the project; they are the basis for "a new library version exists" and
 * never change on their own.
 */
struct LibraryLink
{
  std::string libraryId;
  std::string libraryAssetId;
  std::uint64_t libraryRevision = 0;
  std::string libraryContentHash;
  // Container and member inside the library, for display and diagnostics only.
  std::string sourceContainer;
  std::string sourceMember;

  bool empty() const { return libraryAssetId.empty(); }

  friend bool operator==(const LibraryLink&, const LibraryLink&) = default;
};

struct LibraryInfo
{
  std::string id;
  std::string name;
  std::string path;
  std::string createdAt;
  std::string updatedAt;
};

/** A library asset as published by the library catalog, independent of any project. */
struct LibraryAsset
{
  std::string id;
  AssetType type = AssetType::Texture;
  std::string displayName;
  // Group-relative identity used by the browser, for example `custom.wad/WALL01`.
  std::string virtualPath;
  // WAD container path or parent folder, for example `wads/custom.wad`.
  std::string group;
  std::string sourceContainer;
  // WAD member name; empty for standalone files.
  std::string sourceMember;
  // How the bytes are stored: `goldsrc-miptex`, `image`, `mdl`, `spr`, `wav`, `skybox`.
  std::string nativeFormat;
  // Conversion parameters or importer description, JSON encoded.
  std::string recipe;
  std::uint64_t revision = 0;
  std::string contentHash;
  std::uint64_t size = 0;
  // Source file is gone or inaccessible; the catalog still holds the last revision.
  bool missing = false;

  friend bool operator==(const LibraryAsset&, const LibraryAsset&) = default;
};

/** One source file the library indexed, for rescan reporting and diagnostics. */
struct LibrarySource
{
  std::string relativePath;
  AssetType type = AssetType::Texture;
  std::string contentHash;
  std::uint64_t size = 0;
  std::uint64_t assetCount = 0;
  bool missing = false;

  friend bool operator==(const LibrarySource&, const LibrarySource&) = default;
};

/** Result summary of a library rescan. */
struct LibraryScan
{
  std::size_t added = 0;
  std::size_t updated = 0;
  std::size_t relocated = 0;
  std::size_t restored = 0;
  std::size_t missing = 0;
  std::size_t sources = 0;
  std::size_t invalidEntries = 0;
  // Empty when every source was readable.
  std::string warning;
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
  // Set when the import is a capture or update from an asset library.
  LibraryLink library;
};

/** One project asset revision replacement, applied as part of a batch. */
struct AssetUpdate
{
  std::string assetId;
  AssetImport import;
};

/** One edge of a capture plan, pointing at another entry of the same plan or at an
 * already captured dependency. */
struct AssetCaptureLink
{
  // Plan position, used when `assetId` is empty.
  std::size_t target = 0;
  // Existing project asset outside the plan; never refreshed by this capture.
  std::string assetId;
  std::string kind;
};

/**
 * One project asset to create or refresh, together with the resources it depends on.
 *
 * A capture is applied as a single transaction with its whole dependency closure, so a
 * model can never end up in the project without the companions it needs, and a failed
 * conversion leaves no partial content behind. Applying a plan also replaces the
 * existing dependency edges of every captured asset, so a newer library revision that
 * dropped a companion cannot leave a stale edge behind.
 */
struct AssetCapture
{
  // Refreshes this asset's revision when set, otherwise a new asset is created.
  std::optional<std::string> assetId;
  AssetImport import;
  std::vector<AssetCaptureLink> dependencies;
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
  // SHA-256 of the compatible content of this revision.
  std::string contentHash;
  LibraryLink library;

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
