/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom.
*/

#include "project/GoldSrcModel.h"

#include <fmt/format.h>

#include <algorithm>
#include <cctype>

namespace tb::project
{
namespace
{

// studiohdr_t up to the transition table; samelen fields are read by offset because the
// header has changed layout between engine generations.
constexpr std::size_t HeaderSize = 244;
constexpr std::size_t TextureEntrySize = 80;
constexpr std::size_t TextureNameSize = 64;
constexpr std::uint32_t MaxTextures = 4096;
constexpr std::uint32_t StudioIdentifier = 0x54534449; // "IDST"
constexpr std::uint32_t StudioVersion = 10;

std::uint32_t readU32(const std::vector<std::uint8_t>& bytes, const std::size_t offset)
{
  return std::uint32_t{bytes[offset]} | (std::uint32_t{bytes[offset + 1]} << 8)
         | (std::uint32_t{bytes[offset + 2]} << 16)
         | (std::uint32_t{bytes[offset + 3]} << 24);
}

std::string readCString(const std::uint8_t* data, const std::size_t size)
{
  auto length = std::size_t{0};
  while (length < size && data[length] != 0)
    ++length;
  return std::string{reinterpret_cast<const char*>(data), length};
}

std::string lowered(std::string value)
{
  std::ranges::transform(
    value, value.begin(), [](const unsigned char c) { return char(std::tolower(c)); });
  return value;
}

} // namespace

std::string modelDependencyKey(const std::string& name)
{
  // Dependency names appear as texture names, relative paths or plain file names
  // depending on the model. Comparing stems is the only normalization that matches all
  // three without guessing an actual directory.
  auto text = name;
  std::ranges::replace(text, '\\', '/');
  const auto slash = text.find_last_of('/');
  if (slash != std::string::npos)
    text = text.substr(slash + 1);
  const auto dot = text.find_last_of('.');
  if (dot != std::string::npos && dot > 0)
    text = text.substr(0, dot);
  return lowered(text);
}

std::string modelCompanionName(const std::string& modelFileName)
{
  auto name = modelFileName;
  std::ranges::replace(name, '\\', '/');
  const auto slash = name.find_last_of('/');
  auto folder = std::string{};
  if (slash != std::string::npos)
  {
    folder = name.substr(0, slash + 1);
    name = name.substr(slash + 1);
  }
  const auto dot = name.find_last_of('.');
  auto stem = dot == std::string::npos ? name : name.substr(0, dot);
  auto extension = dot == std::string::npos ? std::string{".mdl"} : name.substr(dot);
  if (stem.empty())
    return {};
  if (stem.size() > 1 && stem.back() == 'T')
    stem.pop_back();
  else
    stem.push_back('T');
  return folder + stem + extension;
}

Result<ModelDependencies> readGoldSrcModelDependencies(
  const std::vector<std::uint8_t>& bytes)
{
  if (bytes.size() < HeaderSize)
    return Error{"MDL header is truncated."};
  if (readU32(bytes, 0) != StudioIdentifier)
    return Error{"Not a GoldSrc studio model."};
  const auto version = readU32(bytes, 4);
  if (version != StudioVersion)
    return Error{fmt::format("Unsupported MDL version {}.", version)};

  const auto textureCount = readU32(bytes, 180);
  const auto textureOffset = readU32(bytes, 184);
  auto result = ModelDependencies{};
  if (textureCount == 0)
    return result;
  if (textureCount > MaxTextures)
    return Error{"MDL declares an implausible texture count."};
  if (
    textureOffset > bytes.size()
    || std::size_t{textureCount} * TextureEntrySize > bytes.size() - textureOffset)
    return Error{"MDL texture table is out of range."};

  for (std::uint32_t index = 0; index < textureCount; ++index)
  {
    const auto base = std::size_t{textureOffset} + std::size_t{index} * TextureEntrySize;
    auto name = readCString(bytes.data() + base, TextureNameSize);
    if (!name.empty())
      result.skins.push_back(std::move(name));
  }
  return result;
}

} // namespace tb::project
