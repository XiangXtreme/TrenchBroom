/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom.
*/

#pragma once

#include "project/AssetLibrary.h"
#include "project/ProjectAsset.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace tb::project
{

/**
 * How a captured project asset relates to the library it came from.
 *
 * The project never reads a library implicitly. This value is produced by an explicit
 * check and drives the "a newer library version exists" prompt; the project keeps using
 * its snapshot until the user decides to update.
 */
enum class LibraryUpdateState
{
  UpToDate,
  // The library asset exists with different content than the captured revision.
  NewVersion,
  // The library asset is gone, or its source file is no longer readable.
  Missing,
  // The library folder could not be opened at all.
  LibraryUnavailable,
};

const char* libraryUpdateStateName(LibraryUpdateState state);

/** Current state of one library, gathered before evaluating project links. */
struct LibraryState
{
  std::string libraryId;
  std::string name;
  std::filesystem::path folder;
  bool available = false;
  std::string error;
  std::vector<LibraryAsset> assets;

  const LibraryAsset* find(const std::string& assetId) const;
};

struct LibraryUpdate
{
  std::string projectAssetId;
  std::string displayName;
  AssetType type = AssetType::Texture;
  std::uint64_t projectRevision = 0;
  std::string projectContentHash;
  LibraryLink link;
  LibraryUpdateState state = LibraryUpdateState::UpToDate;
  // The current library asset when it could be read.
  std::optional<LibraryAsset> available;
  // The project chose to ignore exactly this library revision.
  bool ignored = false;

  bool needsAttention() const
  {
    return !ignored
           && (state == LibraryUpdateState::NewVersion || state == LibraryUpdateState::Missing || state == LibraryUpdateState::LibraryUnavailable);
  }
};

/**
 * Evaluates every library link of a project against the current library state.
 *
 * Pure: `ignoredHashes` maps a project asset ID to the library content hashes the user
 * chose to ignore. Results are ordered by display name so the prompt and the impact
 * preview are stable.
 */
std::vector<LibraryUpdate> evaluateLibraryUpdates(
  const std::vector<Asset>& projectAssets,
  const std::vector<LibraryState>& libraries,
  const std::map<std::string, std::vector<std::string>>& ignoredHashes = {});

/**
 * Builds the GoldSrc compatible payload a project stores for a library asset.
 *
 * GoldSrc native entries are wrapped into a single-texture WAD3 so the project keeps
 * the same mounting contract as an imported texture. Formats that need an image
 * conversion are returned unchanged; the editor converts them because that needs the
 * image pipeline and the user's confirmed texture name.
 */
Result<std::vector<std::uint8_t>> compatibleLibraryContent(
  const LibraryAsset& asset, const std::vector<std::uint8_t>& nativeContent);

/** True when capturing this asset needs the editor's image conversion step. */
bool libraryAssetNeedsConversion(const LibraryAsset& asset);

/**
 * Conversion recipe recorded on the project snapshot.
 *
 * It keeps the library's own recipe and adds the capture provenance, so reimport,
 * comparison and diagnostics can tell a converted capture from a native copy without
 * consulting the library.
 */
std::string captureRecipe(
  const LibraryAsset& asset, const std::string& conversionRecipe = {});

} // namespace tb::project
