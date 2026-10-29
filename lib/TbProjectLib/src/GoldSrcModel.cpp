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

// studiohdr_t up to the sequence group table. The header is read by offset because the
// layout differs between engine generations and only a few fields are relevant here.
constexpr std::size_t HeaderSize = 244;
constexpr std::size_t SequenceGroupsOffset = 172;
constexpr std::uint32_t StudioIdentifier = 0x54534449; // "IDST"
constexpr std::uint32_t StudioVersion = 10;
constexpr std::uint32_t MaxSequenceGroups = 1024;

std::uint32_t readU32(const std::vector<std::uint8_t>& bytes, const std::size_t offset)
{
  return std::uint32_t{bytes[offset]} | (std::uint32_t{bytes[offset + 1]} << 8)
         | (std::uint32_t{bytes[offset + 2]} << 16)
         | (std::uint32_t{bytes[offset + 3]} << 24);
}

std::string lowered(std::string value)
{
  std::ranges::transform(
    value, value.begin(), [](const unsigned char c) { return char(std::tolower(c)); });
  return value;
}

void splitPath(
  const std::string& path, std::string& folder, std::string& name, std::string& extension)
{
  auto text = path;
  std::ranges::replace(text, '\\', '/');
  const auto slash = text.find_last_of('/');
  folder = slash == std::string::npos ? std::string{} : text.substr(0, slash + 1);
  name = slash == std::string::npos ? text : text.substr(slash + 1);
  const auto dot = name.find_last_of('.');
  if (dot == std::string::npos)
  {
    extension = std::string{};
    return;
  }
  extension = name.substr(dot);
  name = name.substr(0, dot);
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

std::vector<std::string> modelAnimationGroupNames(
  const std::string& modelPath, const std::uint32_t sequenceGroups)
{
  auto folder = std::string{};
  auto name = std::string{};
  auto extension = std::string{};
  splitPath(modelPath, folder, name, extension);
  if (extension.empty())
    extension = ".mdl";
  auto result = std::vector<std::string>{};
  if (name.empty())
    return result;
  for (std::uint32_t group = 1; group < sequenceGroups; ++group)
    result.push_back(fmt::format("{}{}{:02d}{}", folder, name, group, extension));
  return result;
}

std::string modelCompanionName(const std::string& modelFileName)
{
  auto folder = std::string{};
  auto stem = std::string{};
  auto extension = std::string{};
  splitPath(modelFileName, folder, stem, extension);
  if (extension.empty())
    extension = ".mdl";
  if (stem.empty())
    return {};
  // The separate texture file belongs to the base model: `crate.mdl` may need
  // `crateT.mdl`, but `crateT.mdl` never needs `crate.mdl` back. Deriving both directions
  // would make every player model depend on itself through its companion.
  if (stem.size() > 1 && stem.back() == 'T')
    return {};
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

  const auto groups = readU32(bytes, SequenceGroupsOffset);
  if (groups > MaxSequenceGroups)
    return Error{"MDL declares an implausible sequence group count."};
  return ModelDependencies{std::max(groups, std::uint32_t{1})};
}

} // namespace tb::project
