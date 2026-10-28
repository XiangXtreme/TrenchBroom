/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom.
*/

#include "project/GoldSrcWad.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace tb::project
{
namespace
{

constexpr std::size_t WadHeaderSize = 12;
constexpr std::size_t WadDirectoryEntrySize = 32;
constexpr std::size_t MiptexHeaderSize = 40;
constexpr std::size_t MiptexNameSize = 16;
constexpr std::size_t PaletteSize = 2 + 256 * 3;

std::uint32_t readU32(const std::vector<std::uint8_t>& bytes, const std::size_t offset)
{
  return std::uint32_t{bytes[offset]} | (std::uint32_t{bytes[offset + 1]} << 8)
         | (std::uint32_t{bytes[offset + 2]} << 16)
         | (std::uint32_t{bytes[offset + 3]} << 24);
}

void appendU32(std::vector<std::uint8_t>& bytes, const std::uint32_t value)
{
  for (std::size_t i = 0; i < 4; ++i)
    bytes.push_back(std::uint8_t((value >> (i * 8)) & 0xff));
}

std::string readName(const std::uint8_t* data, const std::size_t size)
{
  auto length = std::size_t{0};
  while (length < size && data[length] != 0)
    ++length;
  return std::string{reinterpret_cast<const char*>(data), length};
}

void appendName(std::vector<std::uint8_t>& bytes, std::string_view name)
{
  const auto count = std::min(name.size(), std::size_t{16});
  bytes.insert(bytes.end(), name.begin(), name.begin() + std::ptrdiff_t(count));
  bytes.insert(bytes.end(), MiptexNameSize - count, 0);
}

// Interprets a miptex lump far enough to trust its dimensions and mip offsets.
void inspectMiptex(WadTextureEntry& entry)
{
  const auto& lump = entry.miptex;
  if (lump.size() < MiptexHeaderSize)
  {
    entry.problem = "miptex header is truncated";
    return;
  }
  const auto storedName = readName(lump.data(), MiptexNameSize);
  if (entry.name.empty())
    entry.name = storedName;
  const auto width = readU32(lump, 16);
  const auto height = readU32(lump, 20);
  if (width == 0 || height == 0)
  {
    entry.problem = "miptex has a zero dimension";
    return;
  }
  auto expected = std::size_t{0};
  for (std::size_t level = 0; level < 4; ++level)
  {
    const auto offset = readU32(lump, 24 + level * 4);
    const auto levelWidth = std::size_t{width} >> level;
    const auto levelHeight = std::size_t{height} >> level;
    const auto size = levelWidth * levelHeight;
    if (
      levelWidth == 0 || levelHeight == 0 || offset < MiptexHeaderSize
      || std::size_t{offset} + size > lump.size())
    {
      entry.problem = "miptex mip level is out of range";
      return;
    }
    expected = std::max(expected, std::size_t{offset} + size);
  }
  if (expected + PaletteSize > lump.size())
  {
    entry.problem = "miptex palette is truncated";
    return;
  }
  const auto colors =
    std::size_t(std::uint32_t(lump[expected]) | (std::uint32_t{lump[expected + 1]} << 8));
  if (colors > 256 || expected + 2 + colors * 3 > lump.size())
  {
    entry.problem = "miptex palette is invalid";
    return;
  }
  entry.width = width;
  entry.height = height;
  entry.valid = true;
}

} // namespace

bool isGoldSrcWad(const std::vector<std::uint8_t>& bytes)
{
  if (bytes.size() < 4)
    return false;
  const auto magic =
    std::string_view{reinterpret_cast<const char*>(bytes.data()), std::size_t{4}};
  return magic == "WAD3" || magic == "WAD2";
}

std::string goldSrcMiptexName(const std::vector<std::uint8_t>& miptex)
{
  return miptex.size() < MiptexNameSize ? std::string{}
                                        : readName(miptex.data(), MiptexNameSize);
}

Result<WadArchive> readGoldSrcWad(const std::vector<std::uint8_t>& bytes)
{
  if (bytes.size() < WadHeaderSize)
    return Error{"WAD header is truncated."};
  if (!isGoldSrcWad(bytes))
    return Error{"Not a GoldSrc WAD archive."};
  const auto count = readU32(bytes, 4);
  const auto directory = std::size_t{readU32(bytes, 8)};
  if (
    directory < WadHeaderSize || directory > bytes.size()
    || count > (bytes.size() - directory) / WadDirectoryEntrySize)
    return Error{fmt::format("WAD directory is out of range ({} entries).", count)};

  auto archive =
    WadArchive{std::string{reinterpret_cast<const char*>(bytes.data()), 4}, {}};
  archive.textures.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index)
  {
    const auto base = directory + std::size_t{index} * WadDirectoryEntrySize;
    const auto filePosition = std::size_t{readU32(bytes, base)};
    const auto diskSize = std::size_t{readU32(bytes, base + 4)};
    const auto uncompressedSize = std::size_t{readU32(bytes, base + 8)};
    const auto compression = bytes[base + 13];
    auto entry = WadTextureEntry{};
    entry.name = readName(bytes.data() + base + 16, MiptexNameSize);
    // WAD3 texture lumps carry 'C'; other lump types (palettes, qpics) are not
    // textures and are skipped without failing the archive.
    if (bytes[base + 12] != 0x43)
      continue;
    if (compression != 0)
    {
      entry.problem = "compressed WAD lumps are not supported";
    }
    else if (
      diskSize == 0 || filePosition < WadHeaderSize || filePosition > bytes.size()
      || diskSize > bytes.size() - filePosition)
    {
      entry.problem = "WAD lump is out of range";
    }
    else if (uncompressedSize != 0 && uncompressedSize != diskSize)
    {
      entry.problem = "WAD lump size mismatch";
    }
    else
    {
      entry.miptex.assign(
        bytes.begin() + std::ptrdiff_t(filePosition),
        bytes.begin() + std::ptrdiff_t(filePosition + diskSize));
    }
    if (entry.problem.empty())
      inspectMiptex(entry);
    archive.textures.push_back(std::move(entry));
  }
  return archive;
}

Result<std::vector<std::uint8_t>> buildGoldSrcWad(
  const std::vector<std::vector<std::uint8_t>>& miptexEntries)
{
  if (miptexEntries.size() > 0xffffffffu)
    return Error{"Too many textures for a WAD archive."};
  auto wad = std::vector<std::uint8_t>{'W', 'A', 'D', '3'};
  appendU32(wad, std::uint32_t(miptexEntries.size()));
  // The directory offset is only known once every lump has been written, so reserve the
  // field and patch it below.
  appendU32(wad, 0);
  auto sizes = std::vector<std::uint32_t>{};
  sizes.reserve(miptexEntries.size());
  for (const auto& miptex : miptexEntries)
  {
    if (miptex.size() > 0xffffffffu)
      return Error{"Texture is too large for a WAD archive."};
    sizes.push_back(std::uint32_t(miptex.size()));
    wad.insert(wad.end(), miptex.begin(), miptex.end());
  }
  const auto directory = std::uint32_t(wad.size());
  for (std::size_t byte = 0; byte < 4; ++byte)
    wad[8 + byte] = std::uint8_t((directory >> (byte * 8)) & 0xff);
  // Lumps start right after the fixed header.
  auto offset = std::uint32_t(WadHeaderSize);
  for (std::size_t index = 0; index < miptexEntries.size(); ++index)
  {
    appendU32(wad, offset);
    appendU32(wad, sizes[index]);
    appendU32(wad, sizes[index]);
    wad.insert(wad.end(), {0x43, 0, 0, 0});
    appendName(wad, goldSrcMiptexName(miptexEntries[index]));
    offset += sizes[index];
  }
  return wad;
}

} // namespace tb::project
