/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include "base/Result.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace tb::mdl
{
class Map;
}

namespace tb::ui
{
/** The editor's placeholder image as a compiler-compatible GoldSrc WAD. */
Result<std::vector<std::uint8_t>> builtinEmptyTextureWad();
bool mapUsesEmptyTexture(const mdl::Map& map);

/** Provide the built-in texture in an isolated project copy when needed. */
Result<std::filesystem::path> prepareLightmapProject(
  const std::filesystem::path& projectPath, const std::filesystem::path& copyPath);

/** Preserve a map's WAD configuration while adding the built-in texture last. */
Result<std::filesystem::path> prepareLightmapWads(
  const mdl::Map& map, const std::filesystem::path& scratchDirectory);
} // namespace tb::ui
