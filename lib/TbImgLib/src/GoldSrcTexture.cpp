/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "img/GoldSrcTexture.h"

#include "FreeImage.h"
#include "InitFreeImage.h"

#include "kd/resource.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace tb::img
{
namespace
{
void append32(std::vector<std::uint8_t>& bytes, const size_t value)
{
  for (size_t i = 0; i < 4; ++i)
    bytes.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 255));
}
void appendName(std::vector<std::uint8_t>& bytes, const std::string& name)
{
  bytes.insert(bytes.end(), name.begin(), name.end());
  bytes.insert(bytes.end(), 16 - name.size(), 0);
}
} // namespace

Result<GoldSrcTexture> convertGoldSrcTexture(const Image& image, const std::string& name)
{
  if (
    name.empty() || name.size() > 15
    || !std::ranges::all_of(
      name,
      [](const unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
               || c == '_' || c == '-' || c == '{';
      })
    || name.find('{', 1) != std::string::npos)
    return Error{
      "Texture names must use 1–15 ASCII letters, digits, _ or -; an initial { enables "
      "transparency."};
  if (
    image.width == 0 || image.height == 0 || image.width > 16384 || image.height > 16384
    || image.width * image.height > 64 * 1024 * 1024
    || image.pixels.size() != image.width * image.height * 4)
    return Error{"Invalid or excessively large texture image."};

  const auto masked = name.front() == '{';
  const auto ratio = std::min(1.0, 512.0 / double(std::max(image.width, image.height)));
  const auto dimension = [&](const size_t value) {
    return static_cast<size_t>(
      std::clamp(std::lround(double(value) * ratio / 16.0) * 16L, 16L, 512L));
  };
  const auto width = dimension(image.width);
  const auto height = dimension(image.height);
  auto scaled = Image{
    width,
    height,
    std::vector<unsigned char>(width * height * 4),
    masked ? ImageAlphaDomain::Binary : ImageAlphaDomain::Opaque};
  // Area sampling preserves coverage when reducing large source images. Alpha is
  // premultiplied during filtering so invisible RGB cannot bleed into mask edges.
  for (size_t y = 0; y < height; ++y)
    for (size_t x = 0; x < width; ++x)
    {
      const auto x0 = x * image.width / width;
      const auto x1 = std::max(x0 + 1, (x + 1) * image.width / width);
      const auto y0 = y * image.height / height;
      const auto y1 = std::max(y0 + 1, (y + 1) * image.height / height);
      auto sum = std::array<size_t, 4>{};
      for (auto sy = y0; sy < y1; ++sy)
        for (auto sx = x0; sx < x1; ++sx)
        {
          const auto offset = (sy * image.width + sx) * 4;
          const auto alpha = masked ? image.pixels[offset + 3] : 255;
          for (size_t c = 0; c < 3; ++c)
            sum[c] += size_t(image.pixels[offset + c]) * size_t(alpha);
          sum[3] += size_t(alpha);
        }
      const auto out = (y * width + x) * 4;
      for (size_t c = 0; c < 3; ++c)
        scaled.pixels[out + c] = sum[3] ? static_cast<unsigned char>(sum[c] / sum[3]) : 0;
      scaled.pixels[out + 3] = masked && sum[3] / ((x1 - x0) * (y1 - y0)) < 128 ? 0 : 255;
    }

  InitFreeImage::initialize();
  auto rgb =
    kdl::resource{FreeImage_Allocate(int(width), int(height), 24), FreeImage_Unload};
  if (!rgb)
    return Error{"Cannot allocate texture conversion bitmap."};
  for (size_t y = 0; y < height; ++y)
  {
    auto* row = FreeImage_GetScanLine(*rgb, int(height - y - 1));
    for (size_t x = 0; x < width; ++x)
    {
      const auto offset = (y * width + x) * 4;
      row[x * 3 + FI_RGBA_RED] = scaled.pixels[offset];
      row[x * 3 + FI_RGBA_GREEN] = scaled.pixels[offset + 1];
      row[x * 3 + FI_RGBA_BLUE] = scaled.pixels[offset + 2];
    }
  }
  const auto colors = masked ? 255 : 256;
  auto indexed =
    kdl::resource{FreeImage_ColorQuantizeEx(*rgb, FIQ_WUQUANT, colors), FreeImage_Unload};
  if (!indexed)
    return Error{"Cannot quantize texture palette."};
  const auto* palette = FreeImage_GetPalette(*indexed);
  if (!palette)
    return Error{"Texture conversion produced no palette."};
  auto paletteBytes = std::array<std::uint8_t, 768>{};
  for (size_t i = 0; i < size_t(colors); ++i)
  {
    paletteBytes[i * 3] = palette[i].rgbRed;
    paletteBytes[i * 3 + 1] = palette[i].rgbGreen;
    paletteBytes[i * 3 + 2] = palette[i].rgbBlue;
  }
  if (masked)
    paletteBytes[767] = 255;
  auto lookup = std::unordered_map<size_t, std::uint8_t>{};
  const auto nearest = [&](const std::array<size_t, 3>& color) {
    const auto key = (color[0] << 16) | (color[1] << 8) | color[2];
    if (const auto it = lookup.find(key); it != lookup.end())
      return it->second;
    auto distance = std::numeric_limits<int>::max();
    auto best = std::uint8_t{0};
    for (int i = 0; i < colors; ++i)
    {
      const auto dr = int(color[0]) - int(palette[i].rgbRed);
      const auto dg = int(color[1]) - int(palette[i].rgbGreen);
      const auto db = int(color[2]) - int(palette[i].rgbBlue);
      const auto d = dr * dr + dg * dg + db * db;
      if (d < distance)
      {
        distance = d;
        best = static_cast<std::uint8_t>(i);
      }
    }
    lookup.emplace(key, best);
    return best;
  };

  auto miptex = std::vector<std::uint8_t>{};
  appendName(miptex, name);
  append32(miptex, width);
  append32(miptex, height);
  auto offset = size_t{40};
  for (size_t level = 0; level < 4; ++level)
  {
    append32(miptex, offset);
    offset += (width >> level) * (height >> level);
  }
  auto preview = scaled;
  for (size_t level = 0; level < 4; ++level)
  {
    const auto block = size_t{1} << level;
    for (size_t y = 0; y < (height >> level); ++y)
      for (size_t x = 0; x < (width >> level); ++x)
      {
        auto sum = std::array<size_t, 3>{};
        auto opaque = size_t{0};
        for (size_t dy = 0; dy < block; ++dy)
          for (size_t dx = 0; dx < block; ++dx)
          {
            const auto p = ((y * block + dy) * width + x * block + dx) * 4;
            if (scaled.pixels[p + 3] == 0)
              continue;
            ++opaque;
            for (size_t c = 0; c < 3; ++c)
              sum[c] += scaled.pixels[p + c];
          }
        for (auto& c : sum)
          c = opaque ? c / opaque : 0;
        const auto index =
          masked && opaque * 2 < block * block ? std::uint8_t{255} : nearest(sum);
        miptex.push_back(index);
        if (level == 0)
        {
          const auto p = (y * width + x) * 4;
          for (size_t c = 0; c < 3; ++c)
            preview.pixels[p + c] = paletteBytes[size_t(index) * 3 + c];
          preview.pixels[p + 3] = masked && index == 255 ? 0 : 255;
        }
      }
  }
  miptex.push_back(0);
  miptex.push_back(1); // 256 palette entries, little endian
  miptex.insert(miptex.end(), paletteBytes.begin(), paletteBytes.end());
  while (miptex.size() % 4)
    miptex.push_back(0);
  auto wad = std::vector<std::uint8_t>{'W', 'A', 'D', '3'};
  append32(wad, 1);
  append32(wad, 12 + miptex.size());
  wad.insert(wad.end(), miptex.begin(), miptex.end());
  append32(wad, 12);
  append32(wad, miptex.size());
  append32(wad, miptex.size());
  wad.insert(wad.end(), {0x43, 0, 0, 0});
  appendName(wad, name);
  return GoldSrcTexture{name, std::move(preview), std::move(wad), masked};
}
} // namespace tb::img
