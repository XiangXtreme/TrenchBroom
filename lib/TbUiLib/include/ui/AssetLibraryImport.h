/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom, licensed under GPL version 3 or later.
*/
#pragma once

#include "base/Result.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace tb::project
{
class AssetLibrary;
}

namespace tb::ui
{
/**
 * Default virtual category for an imported source file.
 *
 * Returns an empty path for file types a library cannot hold. GoldSrc containers and
 * images share `textures`; the catalog splits WAD members into individual textures.
 */
std::filesystem::path librarySubfolderForFile(const std::filesystem::path& file);

/**
 * Normalizes a file stem into a GoldSrc texture name.
 *
 * Only ASCII letters, digits, '_' and '-' survive; everything else becomes '_'. An empty
 * result becomes "texture". Masked textures get the '{' prefix and the whole name is
 * truncated to 15 characters, the miptex limit.
 */
std::string goldSrcTextureNameFromStem(const std::string& stem, bool masked);

/** Conversion recipe stored next to a converted library texture. */
std::string goldSrcTextureRecipe(
  const std::string& name, std::uint32_t width, std::uint32_t height, bool masked);

/**
 * Converts one uncompiled library image into GoldSrc texture content.
 *
 * The original image stays untouched; the converted bytes become the current library
 * content, so previews and later captures use the converted result. Returns false when
 * the asset is not an unconverted image (WAD members and native models are skipped).
 */
Result<bool> convertLibraryImage(
  project::AssetLibrary& library, const std::string& assetId);
} // namespace tb::ui
