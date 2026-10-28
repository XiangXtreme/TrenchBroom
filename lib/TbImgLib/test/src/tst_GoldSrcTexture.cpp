/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "img/GoldSrcTexture.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::img
{
namespace
{
size_t read32(const std::vector<std::uint8_t>& bytes, const size_t offset)
{
  return size_t(bytes.at(offset)) | (size_t(bytes.at(offset + 1)) << 8)
         | (size_t(bytes.at(offset + 2)) << 16) | (size_t(bytes.at(offset + 3)) << 24);
}
} // namespace
TEST_CASE("convertGoldSrcTexture")
{
  auto image = Image{32, 16, std::vector<unsigned char>(32 * 16 * 4, 255)};
  for (size_t y = 0; y < image.height; ++y)
    for (size_t x = 0; x < image.width; ++x)
    {
      const auto p = (y * image.width + x) * 4;
      image.pixels[p] = y < 8 ? 255 : 0;
      image.pixels[p + 1] = x < 16 ? 255 : 0;
      image.pixels[p + 2] = 0;
    }
  SECTION("WAD3 offsets palette mip levels and preview round trip")
  {
    auto result = convertGoldSrcTexture(image, "test");
    REQUIRE(result.is_success());
    const auto& texture = result.value();
    const auto& wad = texture.wad;
    CHECK(std::string(wad.begin(), wad.begin() + 4) == "WAD3");
    CHECK(read32(wad, 4) == 1);
    const auto directory = read32(wad, 8);
    CHECK(directory + 32 == wad.size());
    CHECK(wad[directory + 12] == 0x43);
    CHECK(read32(wad, directory) == 12);
    CHECK(read32(wad, 28) == 32);
    CHECK(read32(wad, 32) == 16);
    auto expected = size_t{40};
    for (size_t level = 0; level < 4; ++level)
    {
      CHECK(read32(wad, 36 + level * 4) == expected);
      expected += (32 >> level) * (16 >> level);
    }
    CHECK(wad.at(12 + expected) == 0);
    CHECK(wad.at(13 + expected) == 1);
    const auto palette = 14 + expected;
    for (size_t p = 0; p < 32 * 16; ++p)
    {
      const auto index = wad.at(52 + p);
      for (size_t c = 0; c < 3; ++c)
      {
        CHECK(
          texture.preview.pixels[p * 4 + c] == wad.at(palette + size_t(index) * 3 + c));
        CHECK(texture.preview.pixels[p * 4 + c] == image.pixels[p * 4 + c]);
      }
    }
  }
  SECTION("transparent index is reserved at every mip level")
  {
    image.alphaDomain = ImageAlphaDomain::Binary;
    for (size_t p = 0; p < 32 * 16; ++p)
      image.pixels[p * 4 + 3] = p % 32 < 16 ? 0 : 255;
    const auto result = convertGoldSrcTexture(image, "{fence");
    REQUIRE(result.is_success());
    const auto& texture = result.value();
    CHECK(texture.masked);
    CHECK(texture.preview.pixels[3] == 0);
    CHECK(texture.preview.pixels[16 * 4 + 3] == 255);
    for (size_t level = 0; level < 4; ++level)
    {
      const auto start = 12 + read32(texture.wad, 36 + level * 4);
      const auto width = size_t{32} >> level;
      CHECK(texture.wad.at(start) == 255);
      CHECK(texture.wad.at(start + width - 1) != 255);
    }
  }
  SECTION("resizes proportionally and rejects unsupported names and malformed images")
  {
    auto large = Image{1024, 256, std::vector<unsigned char>(1024 * 256 * 4, 255)};
    const auto result = convertGoldSrcTexture(large, "large");
    REQUIRE(result.is_success());
    CHECK(result.value().preview.width == 512);
    CHECK(result.value().preview.height == 128);
    CHECK(convertGoldSrcTexture(image, "!water").is_error());
    CHECK(convertGoldSrcTexture(image, "+0animated").is_error());
    CHECK(convertGoldSrcTexture(image, "name too long for goldsrc").is_error());
    CHECK(convertGoldSrcTexture({}, "empty").is_error());
    image.pixels.pop_back();
    CHECK(convertGoldSrcTexture(image, "broken").is_error());
  }
}
} // namespace tb::img
