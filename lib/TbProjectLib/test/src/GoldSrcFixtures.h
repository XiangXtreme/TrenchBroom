/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom.
*/

#pragma once

// Minimal GoldSrc texture fixtures for the catalog tests. Real WADs are large binaries
// and cannot be committed for every case, so the tests build structurally valid
// archives instead.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace tb::test
{

inline void appendU32(std::vector<std::uint8_t>& bytes, const std::uint32_t value)
{
  for (std::size_t i = 0; i < 4; ++i)
    bytes.push_back(std::uint8_t((value >> (i * 8)) & 0xff));
}

inline void appendName(std::vector<std::uint8_t>& bytes, const std::string_view name)
{
  const auto count = name.size() < 16 ? name.size() : std::size_t{16};
  bytes.insert(bytes.end(), name.begin(), name.begin() + std::ptrdiff_t(count));
  bytes.insert(bytes.end(), 16 - count, 0);
}

// Builds one structurally valid miptex lump with four mip levels and a 256 color
// palette. `seed` changes every byte so different seeds produce different content.
inline std::vector<std::uint8_t> makeMiptex(
  const std::string_view name,
  const std::uint32_t width = 16,
  const std::uint32_t height = 16,
  const std::uint8_t seed = 0)
{
  auto miptex = std::vector<std::uint8_t>{};
  appendName(miptex, name);
  appendU32(miptex, width);
  appendU32(miptex, height);
  auto offset = std::uint32_t{40};
  for (std::uint32_t level = 0; level < 4; ++level)
  {
    appendU32(miptex, offset);
    offset += (width >> level) * (height >> level);
  }
  for (std::uint32_t level = 0; level < 4; ++level)
  {
    const auto pixels = (width >> level) * (height >> level);
    for (std::uint32_t i = 0; i < pixels; ++i)
      miptex.push_back(std::uint8_t((seed + i + level * 7) & 0xff));
  }
  miptex.push_back(0);
  miptex.push_back(1); // 256 palette entries, little endian
  for (std::uint32_t i = 0; i < 768; ++i)
    miptex.push_back(std::uint8_t((i * 5 + seed) & 0xff));
  while (miptex.size() % 4 != 0)
    miptex.push_back(0);
  return miptex;
}

inline void writeU32(
  std::vector<std::uint8_t>& bytes, const std::size_t offset, const std::uint32_t value)
{
  for (std::size_t i = 0; i < 4; ++i)
    bytes[offset + i] = std::uint8_t((value >> (i * 8)) & 0xff);
}

inline void writeName(
  std::vector<std::uint8_t>& bytes, const std::size_t offset, const std::string_view name)
{
  const auto count = name.size() < 64 ? name.size() : std::size_t{64};
  for (std::size_t i = 0; i < count; ++i)
    bytes[offset + i] = std::uint8_t(name[i]);
}

// Builds a structurally valid GoldSrc studio model header (version 10) that declares the
// given number of sequence groups, which is what dependency capture reads. Textures are
// embedded in the model rather than referenced as files, so the fixture declares none.
inline std::vector<std::uint8_t> makeModel(const std::uint32_t sequenceGroups = 1)
{
  constexpr std::size_t headerSize = 244;
  auto model = std::vector<std::uint8_t>(headerSize, 0);
  writeU32(model, 0, 0x54534449); // "IDST"
  writeU32(model, 4, 10);
  writeName(model, 8, "test.mdl");
  writeU32(model, 172, sequenceGroups);
  return model;
}
} // namespace tb::test
