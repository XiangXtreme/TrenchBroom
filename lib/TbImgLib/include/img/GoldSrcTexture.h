/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include "base/Result.h"
#include "img/Image.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tb::img
{
struct GoldSrcTexture
{
  std::string name;
  Image preview;
  std::vector<std::uint8_t> wad;
  bool masked = false;
};

// Creates one WAD3 miptex with a shared palette and four mip levels. Ordinary
// textures and {masked textures are supported. Animated/water groups need their
// own importer and are rejected here. Dimensions are capped at 512, rounded to 16.
Result<GoldSrcTexture> convertGoldSrcTexture(const Image& image, const std::string& name);
} // namespace tb::img
