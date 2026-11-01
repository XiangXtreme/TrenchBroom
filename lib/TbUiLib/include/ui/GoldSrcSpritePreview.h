/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace tb::ui
{

struct GoldSrcSpritePreview
{
  std::size_t width = 0u;
  std::size_t height = 0u;
  std::vector<std::uint8_t> rgba;

  friend bool operator==(const GoldSrcSpritePreview&, const GoldSrcSpritePreview&) =
    default;
};

struct GoldSrcSpriteAnimation
{
  std::vector<GoldSrcSpritePreview> frames;
  // Zero duration means a single frame with user-controlled rate; groups retain timing.
  std::vector<float> durations;
  int alphaType = 0;
};

std::optional<GoldSrcSpriteAnimation> loadGoldSrcSpriteAnimation(
  std::span<const std::uint8_t> data);

std::optional<GoldSrcSpritePreview> loadGoldSrcSpritePreview(
  std::span<const std::uint8_t> data);

} // namespace tb::ui
