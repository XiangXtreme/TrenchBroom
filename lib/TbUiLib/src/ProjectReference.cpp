/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/ProjectReference.h"

#include "project/GoldSrcWad.h"
#include "ui/ProjectSession.h"

#include "kd/string_utils.h"

#include <algorithm>
#include <cctype>
#include <cstdint>

namespace tb::ui
{
namespace
{
std::string fileNameOf(const std::string& path)
{
  const auto slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string stemOf(const std::string& path)
{
  const auto name = fileNameOf(path);
  const auto dot = name.find_last_of('.');
  return dot == std::string::npos ? name : name.substr(0, dot);
}

std::string extensionOf(const std::string& path)
{
  const auto name = fileNameOf(path);
  const auto dot = name.find_last_of('.');
  return dot == std::string::npos ? std::string{} : name.substr(dot);
}

std::string lowered(std::string text)
{
  std::ranges::transform(
    text, text.begin(), [](const unsigned char c) { return char(std::tolower(c)); });
  return text;
}

UnreadableWad unreadable(
  const project::Asset& asset, const std::string& message)
{
  return UnreadableWad{asset.id, asset.virtualPath, message};
}
} // namespace

std::optional<std::string> normalizeGamePath(std::string path)
{
  std::replace(path.begin(), path.end(), '\\', '/');
  if (path.empty() || path.front() == '/' || path.find(':') != std::string::npos)
    return std::nullopt;
  if (
    std::ranges::any_of(path, [](const unsigned char c) { return c < 32 || c == 127; }))
    return std::nullopt;
  auto start = size_t{0};
  while (start <= path.size())
  {
    const auto end = path.find('/', start);
    const auto part = path.substr(start, end - start);
    if (part.empty() || part == "." || part == "..")
      return std::nullopt;
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
  return lowered(path);
}

Result<ProjectReferenceIndex> ProjectReferenceIndex::build(const ProjectSession& session)
{
  auto index = ProjectReferenceIndex{};
  const auto assets = session.store().assets();
  if (assets.is_error())
    return std::get<Error>(assets.error());

  for (const auto& asset : assets.value())
  {
    if (
      asset.type == project::AssetType::Map || asset.type == project::AssetType::Prefab)
      continue;
    if (const auto path = normalizeGamePath(asset.publishPath))
      index.m_paths[*path].push_back(asset);
    if (extensionOf(asset.publishPath) != ".wad")
      continue;

    const auto bytes = session.store().readAsset(asset.id, project::ContentKind::Compatible);
    if (bytes.is_error())
    {
      index.m_unreadableWads.push_back(
        unreadable(asset, std::get<Error>(bytes.error()).msg));
      continue;
    }
    const auto archive = project::readGoldSrcWad(bytes.value());
    if (archive.is_error())
    {
      index.m_unreadableWads.push_back(
        unreadable(asset, std::get<Error>(archive.error()).msg));
      continue;
    }
    for (const auto& texture : archive.value().textures)
    {
      if (!texture.valid || texture.name.empty())
        continue;
      index.m_textures[lowered(texture.name)].push_back(asset);
    }
  }
  return index;
}

ReferenceMatch ProjectReferenceIndex::resolveTexture(const std::string& name) const
{
  const auto found = m_textures.find(lowered(name));
  if (found == m_textures.end())
    return {};
  return found->second.size() == 1 ? ReferenceMatch{ReferenceStatus::Resolved, found->second}
                                   : ReferenceMatch{ReferenceStatus::Ambiguous, found->second};
}

ReferenceMatch ProjectReferenceIndex::resolvePath(const std::string& path) const
{
  const auto normalized = normalizeGamePath(path);
  if (!normalized)
    return {};
  const auto found = m_paths.find(*normalized);
  if (found == m_paths.end())
    return {};
  return found->second.size() == 1 ? ReferenceMatch{ReferenceStatus::Resolved, found->second}
                                   : ReferenceMatch{ReferenceStatus::Ambiguous, found->second};
}

const std::vector<UnreadableWad>& ProjectReferenceIndex::unreadableWads() const
{
  return m_unreadableWads;
}

Result<std::string> gameName(const ProjectSession& session, const project::Asset& asset)
{
  if (asset.type != project::AssetType::Texture)
  {
    if (asset.publishPath.empty())
      return Error{"The project resource has no published path."};
    return asset.publishPath;
  }

  const auto bytes = session.store().readAsset(asset.id, project::ContentKind::Compatible);
  if (bytes.is_error())
    return Error{"Could not read the texture archive: " + std::get<Error>(bytes.error()).msg};
  const auto archive = project::readGoldSrcWad(bytes.value());
  if (archive.is_error())
    return Error{"Could not read the texture archive: " + std::get<Error>(archive.error()).msg};

  auto names = std::vector<std::string>{};
  for (const auto& texture : archive.value().textures)
    if (texture.valid && !texture.name.empty())
      names.push_back(texture.name);
  if (names.empty())
    return Error{"The texture archive contains no usable miptex."};
  if (names.size() > 1)
    return Error{
      "The texture archive embeds " + std::to_string(names.size())
      + " textures, so it has no single name to put on a face."};
  return names.front();
}

Result<std::string> capturedGameName(const project::AssetImport& import)
{
  if (import.type != project::AssetType::Texture)
  {
    if (import.virtualPath.empty())
      return Error{"The captured resource has no published path."};
    return import.virtualPath;
  }
  const auto name = stemOf(import.virtualPath);
  if (name.empty())
    return Error{"The captured texture has no name."};
  return name;
}
} // namespace tb::ui
