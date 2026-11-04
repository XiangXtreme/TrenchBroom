/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom.
*/

#pragma once

#include "base/Result.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace tb::project
{

/**
 * Member level access to GoldSrc WAD3 archives.
 *
 * A WAD is only a container. Everything the asset library and the project snapshot do
 * with textures operates on one miptex entry at a time, so this reader preserves each
 * enormous lump exactly as stored and reports it as an independent named texture. The
 * writer rebuilds a WAD from such entries, which is how a captured texture becomes a
 * single-texture WAD that TrenchBroom and hltools can mount.
 */
template <typename MiptexBytes>
struct BasicWadTextureEntry
{
  // Trimmed name as recorded in the WAD directory. GoldSrc names are at most 15
  // characters and are compared case insensitively by the engine.
  std::string name;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  // Complete miptex lump, byte for byte as stored in the archive.
  MiptexBytes miptex;
  // False when the lump could not be interpreted as a miptex. The bytes are still
  // preserved so a damaged archive can be diagnosed without losing data.
  bool valid = false;
  std::string problem;
};

template <typename MiptexBytes>
struct BasicWadArchive
{
  std::string signature;
  std::vector<BasicWadTextureEntry<MiptexBytes>> textures;
};

using WadTextureEntry = BasicWadTextureEntry<std::vector<std::uint8_t>>;
using WadArchive = BasicWadArchive<std::vector<std::uint8_t>>;
using WadArchiveView = BasicWadArchive<std::span<const std::uint8_t>>;

// True when the payload starts with a WAD2/WAD3 signature.
bool isGoldSrcWad(const std::vector<std::uint8_t>& bytes);

/**
 * Parses a WAD archive into its texture entries.
 *
 * Fails when the container itself is malformed (bad signature, out of range directory
 * or lump offsets). Individual lumps that are not miptex data are reported as invalid
 * entries instead of failing the whole archive, so one damaged texture in a large WAD
 * does not block access to the rest.
 */
Result<WadArchive> readGoldSrcWad(const std::vector<std::uint8_t>& bytes);

// Parses the same metadata in directory order. Texture data borrows from bytes.
Result<WadArchiveView> readGoldSrcWadView(std::span<const std::uint8_t> bytes);

/**
 * Builds a WAD3 archive from complete miptex lumps.
 *
 * Entries keep their original bytes, so rewriting a captured texture is lossless and
 * the resulting archive is byte-identical for identical input order.
 */
Result<std::vector<std::uint8_t>> buildGoldSrcWad(
  const std::vector<std::vector<std::uint8_t>>& miptexEntries);

// Name of the texture stored in a single miptex lump, or an empty string.
std::string goldSrcMiptexName(const std::vector<std::uint8_t>& miptex);

} // namespace tb::project
