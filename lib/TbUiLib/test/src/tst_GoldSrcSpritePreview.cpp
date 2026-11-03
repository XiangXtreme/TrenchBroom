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

#include "ui/CatchConfig.h"
#include "ui/GoldSrcSpritePreview.h"

#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{

enum class AlphaType
{
  Normal = 0,
  Additive = 1,
  IndexAlpha = 2,
  AlphaTest = 3,
};

void appendI32(std::vector<std::uint8_t>& data, const std::int32_t value)
{
  const auto unsignedValue = std::uint32_t(value);
  data.push_back(std::uint8_t(unsignedValue & 0xFFu));
  data.push_back(std::uint8_t((unsignedValue >> 8u) & 0xFFu));
  data.push_back(std::uint8_t((unsignedValue >> 16u) & 0xFFu));
  data.push_back(std::uint8_t((unsignedValue >> 24u) & 0xFFu));
}

void appendU16(std::vector<std::uint8_t>& data, const std::uint16_t value)
{
  data.push_back(std::uint8_t(value & 0xFFu));
  data.push_back(std::uint8_t((value >> 8u) & 0xFFu));
}

void appendF32(std::vector<std::uint8_t>& data, const float value)
{
  auto raw = std::uint32_t{};
  std::memcpy(&raw, &value, sizeof(raw));
  data.push_back(std::uint8_t(raw & 0xFFu));
  data.push_back(std::uint8_t((raw >> 8u) & 0xFFu));
  data.push_back(std::uint8_t((raw >> 16u) & 0xFFu));
  data.push_back(std::uint8_t((raw >> 24u) & 0xFFu));
}

void appendHeader(
  std::vector<std::uint8_t>& data,
  const AlphaType alphaType,
  const std::int32_t width,
  const std::int32_t height,
  const std::int32_t frameCount = 1)
{
  data.insert(std::end(data), {'I', 'D', 'S', 'P'});
  appendI32(data, 2);                       // version
  appendI32(data, 2);                       // VP_PARALLEL
  appendI32(data, std::int32_t(alphaType)); // texture format
  appendF32(data, 0.0f);                    // radius
  appendI32(data, width);
  appendI32(data, height);
  appendI32(data, frameCount);
  appendF32(data, 0.0f); // beam length
  appendI32(data, 0);    // synchronized
  appendU16(data, 256);

  for (auto i = 0; i < 256; ++i)
  {
    data.push_back(std::uint8_t(i));
    data.push_back(std::uint8_t(255 - i));
    data.push_back(std::uint8_t((i * 2) & 0xFF));
  }
}

void appendFrame(
  std::vector<std::uint8_t>& data,
  const std::int32_t width,
  const std::int32_t height,
  const std::vector<std::uint8_t>& pixels)
{
  appendI32(data, -width / 2);
  appendI32(data, -height / 2);
  appendI32(data, width);
  appendI32(data, height);
  data.insert(std::end(data), std::begin(pixels), std::end(pixels));
}

std::vector<std::uint8_t> makeSingleFrameSprite(
  const AlphaType alphaType,
  const std::int32_t width,
  const std::int32_t height,
  const std::vector<std::uint8_t>& pixels)
{
  auto data = std::vector<std::uint8_t>{};
  appendHeader(data, alphaType, width, height);
  appendI32(data, 0); // single frame
  appendFrame(data, width, height, pixels);
  return data;
}

} // namespace

TEST_CASE("GoldSrcSpritePreview")
{
  SECTION("rejects invalid magic")
  {
    const auto data = std::vector<std::uint8_t>{'N', 'O', 'P', 'E'};

    CHECK_FALSE(loadGoldSrcSpritePreview(data).has_value());
  }

  SECTION("decodes normal first frame")
  {
    const auto data = makeSingleFrameSprite(AlphaType::Normal, 2, 1, {1, 2});

    const auto preview = loadGoldSrcSpritePreview(data);

    REQUIRE(preview.has_value());
    CHECK(preview->width == 2u);
    CHECK(preview->height == 1u);
    CHECK(
      preview->rgba
      == std::vector<std::uint8_t>{
        1,
        254,
        2,
        255,
        2,
        253,
        4,
        255,
      });
  }

  SECTION("uses palette index as alpha for index alpha sprites")
  {
    const auto data = makeSingleFrameSprite(AlphaType::IndexAlpha, 2, 1, {8, 192});

    const auto preview = loadGoldSrcSpritePreview(data);

    REQUIRE(preview.has_value());
    CHECK(preview->rgba[3] == 8u);
    CHECK(preview->rgba[7] == 192u);
  }

  SECTION("makes palette index 255 transparent for alpha test sprites")
  {
    const auto data = makeSingleFrameSprite(AlphaType::AlphaTest, 2, 1, {1, 255});

    const auto preview = loadGoldSrcSpritePreview(data);

    REQUIRE(preview.has_value());
    CHECK(preview->rgba[3] == 255u);
    CHECK(preview->rgba[7] == 0u);
  }

  SECTION("uses brightness as alpha and keys index 255 for additive sprites")
  {
    const auto data = makeSingleFrameSprite(AlphaType::Additive, 2, 1, {1, 255});

    const auto preview = loadGoldSrcSpritePreview(data);

    REQUIRE(preview.has_value());
    // Index 1 is rgb(1, 254, 2), so the additive contribution is its brightness; index
    // 255 is the GoldSrc colour key and stays fully transparent.
    CHECK(preview->rgba[3] == 86u);
    CHECK(preview->rgba[7] == 0u);
  }

  SECTION("decodes first frame in a group")
  {
    auto data = std::vector<std::uint8_t>{};
    appendHeader(data, AlphaType::Normal, 2, 1);
    appendI32(data, 1); // group
    appendI32(data, 2); // group frame count
    appendF32(data, 0.1f);
    appendF32(data, 0.2f);
    appendFrame(data, 2, 1, {3, 4});
    appendFrame(data, 2, 1, {5, 6});

    const auto preview = loadGoldSrcSpritePreview(data);
    const auto animation = loadGoldSrcSpriteAnimation(data);
    REQUIRE(animation);
    CHECK(animation->frames.size() == 2);
    REQUIRE(animation->durations.size() == 2);
    CHECK(animation->durations[0] > 0.09f);
    CHECK(animation->durations[1] < 0.11f);
    data.pop_back();
    CHECK_FALSE(loadGoldSrcSpriteAnimation(data));
    CHECK(loadGoldSrcSpritePreview(data));

    REQUIRE(preview.has_value());
    CHECK(
      preview->rgba
      == std::vector<std::uint8_t>{
        3,
        252,
        6,
        255,
        4,
        251,
        8,
        255,
      });
  }
}

} // namespace tb::ui
