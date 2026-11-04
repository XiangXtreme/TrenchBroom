/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include "base/Result.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace tb::ui
{
struct TerrainSnapshot
{
  std::vector<std::uint8_t> map;
  std::map<std::string, std::vector<std::uint8_t>> textures;
  size_t topFaces = 0;
};

/** Bakes a disposable compile copy of a serialized project map. */
Result<std::optional<TerrainSnapshot>> bakeTerrainSnapshot(
  const std::vector<std::uint8_t>& source);
} // namespace tb::ui
