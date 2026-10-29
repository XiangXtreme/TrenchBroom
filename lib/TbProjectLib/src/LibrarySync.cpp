/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom.
*/

#include "project/LibrarySync.h"

#include "project/GoldSrcWad.h"

#include <fmt/format.h>

#include <algorithm>

namespace tb::project
{

const char* libraryUpdateStateName(const LibraryUpdateState state)
{
  switch (state)
  {
  case LibraryUpdateState::UpToDate:
    return "up-to-date";
  case LibraryUpdateState::NewVersion:
    return "new-version";
  case LibraryUpdateState::Missing:
    return "missing";
  case LibraryUpdateState::LibraryUnavailable:
    return "library-unavailable";
  case LibraryUpdateState::DependenciesChanged:
    return "dependencies-changed";
  }
  return "unknown";
}

const LibraryAsset* LibraryState::find(const std::string& assetId) const
{
  const auto it =
    std::ranges::find_if(assets, [&](const auto& asset) { return asset.id == assetId; });
  return it == assets.end() ? nullptr : &*it;
}

std::vector<LibraryUpdate> evaluateLibraryUpdates(
  const std::vector<Asset>& projectAssets,
  const std::vector<LibraryState>& libraries,
  const std::map<std::string, std::vector<std::string>>& ignoredHashes)
{
  auto result = std::vector<LibraryUpdate>{};
  for (const auto& asset : projectAssets)
  {
    if (asset.library.empty())
      continue;
    const auto* library = [&]() -> const LibraryState* {
      const auto it = std::ranges::find_if(libraries, [&](const auto& candidate) {
        return candidate.libraryId == asset.library.libraryId;
      });
      return it == libraries.end() ? nullptr : &*it;
    }();
    auto update = LibraryUpdate{
      asset.id,
      asset.displayName,
      asset.type,
      asset.revision,
      asset.contentHash,
      asset.library,
      LibraryUpdateState::UpToDate,
      std::nullopt,
      false};
    if (!library || !library->available)
    {
      update.state = LibraryUpdateState::LibraryUnavailable;
    }
    else if (const auto* current = library->find(asset.library.libraryAssetId))
    {
      update.available = *current;
      if (current->missing)
        update.state = LibraryUpdateState::Missing;
      else
        update.state = current->contentHash == asset.library.libraryContentHash
                         ? LibraryUpdateState::UpToDate
                         : LibraryUpdateState::NewVersion;
    }
    else
    {
      update.state = LibraryUpdateState::Missing;
    }
    const auto ignored = ignoredHashes.find(asset.id);
    if (ignored != ignoredHashes.end() && update.available)
    {
      update.ignored = std::ranges::find(ignored->second, update.available->contentHash)
                       != ignored->second.end();
    }
    result.push_back(std::move(update));
  }
  std::ranges::sort(result, [](const auto& a, const auto& b) {
    return a.displayName == b.displayName ? a.projectAssetId < b.projectAssetId
                                          : a.displayName < b.displayName;
  });
  return result;
}

bool libraryAssetNeedsConversion(const LibraryAsset& asset)
{
  return asset.nativeFormat == "image";
}

Result<std::vector<std::uint8_t>> compatibleLibraryContent(
  const LibraryAsset& asset, const std::vector<std::uint8_t>& nativeContent)
{
  if (asset.nativeFormat == "goldsrc-miptex")
  {
    return buildGoldSrcWad({nativeContent});
  }
  return nativeContent;
}

std::string captureRecipe(const LibraryAsset& asset, const std::string& conversionRecipe)
{
  const auto base = conversionRecipe.empty() ? asset.recipe : conversionRecipe;
  // Appending a comma separated field keeps the existing recipe readable and avoids
  // parsing JSON just to annotate it; the recipe is an opaque, forward compatible blob.
  const auto wrapped = base.starts_with('{') && base.ends_with('}')
                         ? base.substr(0, base.size() - 1)
                         : std::string{"{"};
  return fmt::format(
    R"({},"capturedFrom":"asset-library","libraryAssetId":"{}","libraryRevision":{},)"
    R"("libraryContentHash":"{}","sourceContainer":"{}","sourceMember":"{}"}})",
    wrapped,
    asset.id,
    asset.revision,
    asset.contentHash,
    asset.sourceContainer,
    asset.sourceMember);
}

} // namespace tb::project
