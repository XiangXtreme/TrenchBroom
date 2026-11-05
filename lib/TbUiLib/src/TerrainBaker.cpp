/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/TerrainBaker.h"

#include "TerrainMaterialRaster.h"
#include "ui/TerrainMaterial.h"
#include "ui/TerrainRegion.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace tb::ui
{
namespace
{
constexpr int Columns = 10;
constexpr int Rows = 5;
constexpr double Core = 96.0;
constexpr int Period = 128;
constexpr double Step = 24.0;
constexpr double Width = Columns * Core;               // 960.0
constexpr double Height = Rows * Core;                 // 480.0
constexpr int Across = static_cast<int>(Width / Step); // 40
constexpr int Down = static_cast<int>(Height / Step);  // 20
constexpr int Stride = Across + 1;                     // 41
constexpr double PatchCore = 2.0 * Step;               // 48.0
constexpr double PatchGutter = 8.0; // In the terrain's base texel coordinates.
constexpr double LowV = 24.0;
constexpr double HighV = 104.0;
const vm::vec3d WorldU{1.0, 0.0, 0.0};
const vm::vec3d WorldV{0.0, 1.0, 0.0};
const vm::vec3d SharedU{0.875, 0.5, 0.0};
constexpr int WeightSteps = 7;
constexpr int Shades = 7;
constexpr auto MaterialPairs =
  std::array{std::array{0, 1}, std::array{0, 2}, std::array{1, 2}};

inline double clamp(const double value, const double minVal, const double maxVal)
{
  return std::max(minVal, std::min(maxVal, value));
}

inline double smooth(const double t)
{
  return t * t * (3.0 - 2.0 * t);
}

inline int wrap(const int value, const int size)
{
  return ((value % size) + size) % size;
}

inline double mix(const double a, const double b, const double t)
{
  return a + (b - a) * t;
}

inline double noise(const int x, const int y, const int seed)
{
  const auto ux = static_cast<std::uint32_t>(x + 91);
  const auto uy = static_cast<std::uint32_t>(y + seed);
  auto bits = (ux * 374761393u) ^ (uy * 668265263u);
  bits = (bits ^ (bits >> 13)) * 1274126177u;
  return static_cast<double>(bits ^ (bits >> 16)) / 4294967295.0;
}

inline double valueNoise(const double x, const double y, const int size, const int seed)
{
  const auto ix = static_cast<int>(std::floor(x / static_cast<double>(size)));
  const auto iy = static_cast<int>(std::floor(y / static_cast<double>(size)));
  const auto tx = smooth(x / static_cast<double>(size) - static_cast<double>(ix));
  const auto ty = smooth(y / static_cast<double>(size) - static_cast<double>(iy));
  auto at = [size, seed](const int u, const int v) {
    return noise(wrap(u, Period / size), wrap(v, Period / size), seed);
  };
  return mix(
    mix(at(ix, iy), at(ix + 1, iy), tx), mix(at(ix, iy + 1), at(ix + 1, iy + 1), tx), ty);
}

struct PaletteData
{
  std::array<std::uint8_t, 768> palette{};
  std::array<int, WeightSteps + 1> ramp{};
};

PaletteData createPalette()
{
  PaletteData result;
  constexpr std::array<std::array<double, 3>, 3> colors{{
    {102.0, 115.0, 62.0},
    {158.0, 116.0, 78.0},
    {129.0, 128.0, 130.0},
  }};
  auto nextColor = 0;
  for (auto dirt = 0; dirt <= WeightSteps; ++dirt)
  {
    result.ramp[static_cast<size_t>(dirt)] = nextColor;
    for (auto rock = 0; rock <= WeightSteps - dirt; ++rock)
    {
      const auto grass = WeightSteps - dirt - rock;
      for (auto shade = 0; shade < Shades; ++shade)
      {
        for (auto channel = 0; channel < 3; ++channel)
        {
          const auto base =
            (colors[0][static_cast<size_t>(channel)] * static_cast<double>(grass)
             + colors[1][static_cast<size_t>(channel)] * static_cast<double>(dirt)
             + colors[2][static_cast<size_t>(channel)] * static_cast<double>(rock))
            / static_cast<double>(WeightSteps);
          result.palette[static_cast<size_t>(nextColor * 3 + channel)] =
            static_cast<std::uint8_t>(std::clamp(
              static_cast<int>(
                std::lround(base + (static_cast<double>(shade) - 3.0) * 9.0)),
              0,
              255));
        }
        ++nextColor;
      }
    }
  }
  return result;
}

const PaletteData& getPalette()
{
  static const auto instance = createPalette();
  return instance;
}

std::array<std::vector<float>, 3> createDetails()
{
  std::array<std::vector<float>, 3> details;
  for (size_t mat = 0; mat < 3; ++mat)
  {
    details[mat].resize(Period * Period);
    for (auto i = 0; i < Period * Period; ++i)
    {
      const auto x = i % Period;
      const auto y = i / Period;
      const auto grain = noise(x, y, 97 + static_cast<int>(mat) * 17) - 0.5;
      auto val = 0.0;
      if (mat == 0)
        val = clamp(3.0 + 3.0 * (valueNoise(x, y, 2, 62) - 0.5) + 3.0 * grain, 0.0, 6.0);
      else if (mat == 1)
        val = clamp(3.0 + 4.0 * (valueNoise(x, y, 4, 71) - 0.5) + 2.0 * grain, 0.0, 6.0);
      else
        val = clamp(3.0 + 5.0 * (valueNoise(x, y, 8, 83) - 0.5) + 3.0 * grain, 0.0, 6.0);
      details[mat][static_cast<size_t>(i)] = static_cast<float>(val);
    }
  }
  return details;
}

const std::array<std::vector<float>, 3>& getDetails()
{
  static const auto instance = createDetails();
  return instance;
}

std::vector<std::uint8_t> createMiptexLump(
  const std::string_view name,
  const int width,
  const int height,
  const std::array<std::vector<std::uint8_t>, 4>& levels,
  const std::array<std::uint8_t, 768>& palette)
{
  const auto level0Size = static_cast<std::uint32_t>(width * height);
  const auto level1Size = static_cast<std::uint32_t>((width / 2) * (height / 2));
  const auto level2Size = static_cast<std::uint32_t>((width / 4) * (height / 4));
  const auto level3Size = static_cast<std::uint32_t>((width / 8) * (height / 8));

  const auto offset0 = std::uint32_t{40};
  const auto offset1 = offset0 + level0Size;
  const auto offset2 = offset1 + level1Size;
  const auto offset3 = offset2 + level2Size;
  const auto paletteOffset = offset3 + level3Size;

  const auto totalSize = static_cast<std::size_t>(paletteOffset + 2 + 768);
  const auto alignedSize = (totalSize + 3) & ~std::size_t{3};
  auto bytes = std::vector<std::uint8_t>(alignedSize, 0);

  const auto nameLen = std::min(name.size(), std::size_t{15});
  std::memcpy(bytes.data(), name.data(), nameLen);

  auto writeU32 = [&](const std::size_t at, const std::uint32_t val) {
    bytes[at] = static_cast<std::uint8_t>(val & 0xff);
    bytes[at + 1] = static_cast<std::uint8_t>((val >> 8) & 0xff);
    bytes[at + 2] = static_cast<std::uint8_t>((val >> 16) & 0xff);
    bytes[at + 3] = static_cast<std::uint8_t>((val >> 24) & 0xff);
  };
  writeU32(16, static_cast<std::uint32_t>(width));
  writeU32(20, static_cast<std::uint32_t>(height));
  writeU32(24, offset0);
  writeU32(28, offset1);
  writeU32(32, offset2);
  writeU32(36, offset3);

  std::memcpy(bytes.data() + offset0, levels[0].data(), level0Size);
  std::memcpy(bytes.data() + offset1, levels[1].data(), level1Size);
  std::memcpy(bytes.data() + offset2, levels[2].data(), level2Size);
  std::memcpy(bytes.data() + offset3, levels[3].data(), level3Size);

  bytes[paletteOffset] = 0x00;
  bytes[paletteOffset + 1] = 0x01; // 256 in little-endian uint16
  std::memcpy(bytes.data() + paletteOffset + 2, palette.data(), 768);

  return bytes;
}

template <typename SampleFn>
std::array<std::vector<std::uint8_t>, 4> rasterizeLevels(
  SampleFn&& sample,
  const double originX,
  const double originY,
  const int size,
  const double texelStep,
  const PaletteData& pal,
  const std::array<std::vector<float>, 3>& detail,
  const TerrainMaterial* material,
  std::unordered_map<std::uint32_t, std::uint8_t>& nearestColors)
{
  auto side = size;
  auto samples = std::vector<float>(static_cast<size_t>(side * side * 3));
  for (auto y = 0; y < side; ++y)
  {
    for (auto x = 0; x < side; ++x)
    {
      const auto offset = static_cast<size_t>((y * side + x) * 3);
      const auto worldX = originX + static_cast<double>(x) * texelStep;
      const auto worldY = originY + static_cast<double>(y) * texelStep;
      const auto weights = sample(worldX + 0.5 * texelStep, worldY + 0.5 * texelStep);
      const auto tx = wrap(static_cast<int>(std::floor(worldX)), Period);
      const auto ty = wrap(static_cast<int>(std::floor(worldY)), Period);
      if (material)
      {
        for (auto channel = size_t{0}; channel < 3; ++channel)
        {
          auto value = 0.0;
          for (auto slot = size_t{0}; slot < 3; ++slot)
            value += weights[slot]
                     * double(material->images[slot].constScanLine(
                       ty)[size_t(tx) * 4 + channel]);
          samples[offset + channel] = static_cast<float>(value);
        }
      }
      else
      {
        samples[offset] = static_cast<float>(weights[1]);
        samples[offset + 1] = static_cast<float>(weights[2]);
        const auto texel = static_cast<size_t>(ty * Period + tx);
        samples[offset + 2] = static_cast<float>(
          weights[0] * static_cast<double>(detail[0][texel])
          + weights[1] * static_cast<double>(detail[1][texel])
          + weights[2] * static_cast<double>(detail[2][texel]));
      }
    }
  }

  std::array<std::vector<std::uint8_t>, 4> levels;
  for (auto mip = 0; mip < 4; ++mip)
  {
    levels[static_cast<size_t>(mip)].resize(static_cast<size_t>(side * side));
    for (auto i = 0; i < side * side; ++i)
    {
      const auto offset = static_cast<size_t>(i * 3);
      if (material)
      {
        auto rgb = std::array<int, 3>{};
        auto key = std::uint32_t{0};
        for (auto channel = size_t{0}; channel < 3; ++channel)
        {
          rgb[channel] = std::clamp(int(std::lround(samples[offset + channel])), 0, 255);
          // Quantized lookup bounds palette searches to 32^3 colors regardless of
          // terrain size, while keeping the result independent of traversal order.
          key = (key << 5) | std::uint32_t(rgb[channel] >> 3);
          rgb[channel] = (rgb[channel] & ~7) + 4;
        }
        auto it = nearestColors.find(key);
        if (it == nearestColors.end())
        {
          auto distance = std::numeric_limits<int>::max();
          auto best = std::uint8_t{0};
          for (auto index = 0; index < 256; ++index)
          {
            auto error = 0;
            for (auto channel = size_t{0}; channel < 3; ++channel)
            {
              const auto delta =
                rgb[channel] - int(pal.palette[size_t(index) * 3 + channel]);
              error += delta * delta;
            }
            if (error < distance)
            {
              distance = error;
              best = std::uint8_t(index);
            }
          }
          it = nearestColors.emplace(key, best).first;
        }
        levels[size_t(mip)][size_t(i)] = it->second;
        continue;
      }
      const auto dirt = samples[offset] * static_cast<float>(WeightSteps);
      const auto rock = samples[offset + 1] * static_cast<float>(WeightSteps);
      const auto dRound = std::clamp(static_cast<int>(std::lround(dirt)), 0, WeightSteps);
      const auto drRound = static_cast<int>(std::lround(dirt + rock));
      const auto r = std::clamp(
        std::min(WeightSteps - dRound, drRound - dRound), 0, WeightSteps - dRound);
      const auto shadeIndex =
        std::clamp(static_cast<int>(std::lround(samples[offset + 2])), 0, Shades - 1);
      const auto colorIndex =
        pal.ramp[static_cast<size_t>(dRound)] + r * Shades + shadeIndex;
      levels[static_cast<size_t>(mip)][static_cast<size_t>(i)] =
        static_cast<std::uint8_t>(std::clamp(colorIndex, 0, 255));
    }
    if (mip == 3)
      break;

    const auto nextSide = side / 2;
    auto nextSamples = std::vector<float>(static_cast<size_t>(nextSide * nextSide * 3));
    for (auto y = 0; y < side; y += 2)
    {
      for (auto x = 0; x < side; x += 2)
      {
        const auto src = static_cast<size_t>((y * side + x) * 3);
        const auto dst = static_cast<size_t>(((y / 2) * nextSide + (x / 2)) * 3);
        for (size_t k = 0; k < 3; ++k)
        {
          nextSamples[dst + k] = (samples[src + k] + samples[src + k + 3]
                                  + samples[src + k + static_cast<size_t>(side * 3)]
                                  + samples[src + k + static_cast<size_t>(side * 3 + 3)])
                                 / 4.0f;
        }
      }
    }
    samples = std::move(nextSamples);
    side = nextSide;
  }
  return levels;
}

project::WadTextureEntry makeTexture(
  const std::string_view name,
  const int size,
  const TerrainMipLevels& levels,
  const PaletteData& pal)
{
  project::WadTextureEntry entry;
  entry.name = std::string{name};
  entry.width = static_cast<std::uint32_t>(size);
  entry.height = static_cast<std::uint32_t>(size);
  entry.miptex = createMiptexLump(name, size, size, levels, pal.palette);
  entry.valid = true;
  return entry;
}

bool hasMaterialLevels(const TerrainMipLevels& levels)
{
  for (auto mip = size_t{0}; mip < levels.size(); ++mip)
  {
    const auto side = size_t(Period) >> mip;
    if (levels[mip].size() != side * side)
      return false;
  }
  return true;
}

bool isUniform(const QImage& image)
{
  const auto* first = image.constScanLine(0);
  for (auto y = 0; y < image.height(); ++y)
  {
    const auto* row = image.constScanLine(y);
    for (auto x = 0; x < image.width(); ++x)
      if (std::memcmp(first, row + x * 4, 4) != 0)
        return false;
  }
  return true;
}

template <typename SampleFn>
project::WadTextureEntry generateTexture(
  const std::string_view name,
  SampleFn&& sample,
  const double originX,
  const double originY,
  const int size,
  const PaletteData& pal,
  const std::array<std::vector<float>, 3>& detail,
  const TerrainMaterial* material,
  std::unordered_map<std::uint32_t, std::uint8_t>& nearestColors)
{
  auto levels = rasterizeLevels(
    std::forward<SampleFn>(sample),
    originX,
    originY,
    size,
    1.0,
    pal,
    detail,
    material,
    nearestColors);
  return makeTexture(name, size, levels, pal);
}

inline bool usableTransitionUv(const double vx, const double vy)
{
  const auto ux = SharedU.x();
  const auto uy = SharedU.y();
  const auto trace = ux * ux + uy * uy + vx * vx + vy * vy;
  const auto determinant = ux * vy - uy * vx;
  const auto root =
    std::sqrt(std::max(0.0, trace * trace - 4.0 * determinant * determinant));
  const auto largestSquared = (trace + root) / 2.0;
  if (largestSquared <= 1e-12)
    return false;
  const auto smallestSquared = (determinant * determinant) / largestSquared;
  return smallestSquared >= 0.65 * 0.65 && largestSquared <= 1.65 * 1.65
         && largestSquared <= 4.0 * smallestSquared;
}

inline int powerOfTwo(const int n)
{
  if (n <= 1)
    return 1;
  auto val = 1;
  while (val < n)
    val <<= 1;
  return val;
}

struct MixInfo
{
  std::string kind;
  int material = -1;
  int key = 0;
  vm::vec3d v;
};

struct PatchTile
{
  int x = 0;
  int y = 0;
  std::array<std::vector<std::uint8_t>, 4> levels;
  std::vector<size_t> faceIndices;
};

QPolygonF makeRect(const double x1, const double y1, const double x2, const double y2)
{
  QPolygonF poly;
  poly.reserve(4);
  poly.append(QPointF{x1, y1});
  poly.append(QPointF{x2, y1});
  poly.append(QPointF{x2, y2});
  poly.append(QPointF{x1, y2});
  return poly;
}
} // namespace

void detail::precomputeTerrainMaterialLevels(TerrainMaterial& material)
{
  auto pal = PaletteData{};
  pal.palette = material.palette;
  const auto& detail = getDetails();
  auto nearestColors = std::unordered_map<std::uint32_t, std::uint8_t>{};
  for (auto slot = size_t{0}; slot < material.baseLevels.size(); ++slot)
    material.baseLevels[slot] = rasterizeLevels(
      [slot](double, double) {
        auto weights = std::array<double, 3>{};
        weights[slot] = 1.0;
        return weights;
      },
      0.0,
      0.0,
      Period,
      1.0,
      pal,
      detail,
      &material,
      nearestColors);
  for (auto pair = size_t{0}; pair < MaterialPairs.size(); ++pair)
  {
    const auto [a, b] = MaterialPairs[pair];
    material.pairLevels[pair] = rasterizeLevels(
      [a, b](double, const double y) {
        const auto weight = clamp((y - LowV) / (HighV - LowV), 0.0, 1.0);
        auto weights = std::array<double, 3>{};
        weights[size_t(a)] = 1.0 - weight;
        weights[size_t(b)] = weight;
        return weights;
      },
      0.0,
      0.0,
      Period,
      1.0,
      pal,
      detail,
      &material,
      nearestColors);
  }
}

Result<TerrainBake> runTerrainBaker(
  const TerrainRegion& region,
  const TerrainMaterial* material,
  const TerrainBakeMode mode)
{
  if (
    region.patchDensityPercent != 50 && region.patchDensityPercent != 100
    && region.patchDensityPercent != 200)
    return Error{"Invalid terrain patch density."};
  const auto density = double(region.patchDensityPercent) / 100.0;
  const auto patchSize = 64 * region.patchDensityPercent / 100;
  const auto atlasSide = 256 / patchSize;
  const auto atlasCapacity = atlasSide * atlasSide;
  auto pal = getPalette();
  if (material)
  {
    for (const auto& image : material->images)
      if (
        image.size() != QSize{Period, Period}
        || image.format() != QImage::Format_RGBA8888)
        return Error{"Invalid normalized terrain material texture."};
    pal.palette = material->palette;
  }
  const auto& detail = getDetails();
  auto nearestColors = std::unordered_map<std::uint32_t, std::uint8_t>{};

  // Normalize vertices with 0.05 threshold
  std::vector<std::array<double, 3>> vertices;
  vertices.reserve(region.weights.size());
  for (const auto& w : region.weights)
  {
    std::array<double, 3> active{};
    auto sum = 0.0;
    for (size_t k = 0; k < 3; ++k)
    {
      if (w[k] >= 0.05)
      {
        active[k] = w[k];
        sum += w[k];
      }
    }
    if (sum > 0.0)
    {
      for (size_t k = 0; k < 3; ++k)
        active[k] /= sum;
    }
    else
    {
      active = w;
    }
    vertices.push_back(active);
  }

  // Quads
  struct Quad
  {
    std::array<double, 3> a;
    std::array<double, 3> b;
    std::array<double, 3> c;
    std::array<double, 3> d;
  };
  std::vector<Quad> quads;
  quads.reserve(static_cast<size_t>(Across * Down));
  for (auto row = 0; row < Down; ++row)
  {
    for (auto col = 0; col < Across; ++col)
    {
      const auto idx = static_cast<size_t>(row * Stride + col);
      quads.push_back({
        vertices[idx],
        vertices[idx + 1],
        vertices[idx + static_cast<size_t>(Stride) + 1],
        vertices[idx + static_cast<size_t>(Stride)],
      });
    }
  }

  auto locate = [](const double x, const double y) {
    const auto col = std::min(static_cast<int>(std::floor(x / Step)), Across - 1);
    const auto row = std::min(static_cast<int>(std::floor(y / Step)), Down - 1);
    return std::tuple{
      static_cast<size_t>(row * Across + col),
      x / Step - static_cast<double>(col),
      y / Step - static_cast<double>(row)};
  };

  auto sample = [&](const double x, const double y) -> std::array<double, 3> {
    const auto [cell, s, t] = locate(clamp(x, 0.0, Width), clamp(y, 0.0, Height));
    const auto& [a, b, c, d] = quads[cell];
    std::array<double, 3> res{};
    for (size_t k = 0; k < 3; ++k)
    {
      if (s >= t)
        res[k] = a[k] * (1.0 - s) + b[k] * (s - t) + c[k] * t;
      else
        res[k] = a[k] * (1.0 - t) + c[k] * s + d[k] * (t - s);
    }
    return res;
  };

  auto activeMaterials = [](const std::span<const std::array<double, 3>> corners) {
    std::vector<int> mats;
    for (auto k = 0; k < 3; ++k)
    {
      if (std::ranges::any_of(
            corners, [k](const auto& v) { return v[static_cast<size_t>(k)] > 0.0; }))
        mats.push_back(k);
    }
    return mats;
  };

  const auto matchesPairNeighborhood = [&](
                                         const int col,
                                         const int row,
                                         const int a,
                                         const int b,
                                         const double vx,
                                         const double vy,
                                         const double offset) {
    // MIP filtering and the minimum UV scale can reach beyond an adjacent cell.
    // Check two cells in every direction, including bilinear filter footprints.
    constexpr auto padding = 2;
    if (
      col < padding || row < padding || col >= Across - padding || row >= Down - padding)
      return false;
    constexpr auto triangles = std::array{std::array{0, 1, 2}, std::array{0, 2, 3}};
    constexpr auto tolerance = 1e-8;
    for (auto y = row - padding; y <= row + padding; ++y)
      for (auto x = col - padding; x <= col + padding; ++x)
      {
        const auto corners = std::array{
          std::array{x, y},
          std::array{x + 1, y},
          std::array{x + 1, y + 1},
          std::array{x, y + 1}};
        for (const auto& triangle : triangles)
        {
          auto low = std::numeric_limits<double>::infinity();
          auto high = -std::numeric_limits<double>::infinity();
          for (const auto corner : triangle)
          {
            const auto [gx, gy] = corners[size_t(corner)];
            const auto raw =
              (vx * double(gx) * Step + vy * double(gy) * Step + offset - LowV)
              / (HighV - LowV);
            low = std::min(low, raw);
            high = std::max(high, raw);
            const auto weight = clamp(raw, 0.0, 1.0);
            const auto& actual = vertices[size_t(gy * Stride + gx)];
            for (auto slot = 0; slot < 3; ++slot)
            {
              const auto expected = slot == a ? 1.0 - weight : slot == b ? weight : 0.0;
              if (std::abs(actual[size_t(slot)] - expected) > tolerance)
                return false;
            }
          }
          // A clamp crossing inside a triangle is not affine there, even if its
          // three corner weights happen to agree.
          if (
            (low < -tolerance && high > tolerance)
            || (low < 1.0 - tolerance && high > 1.0 + tolerance))
            return false;
        }
      }
    return true;
  };

  auto uniform = std::array<bool, 3>{};
  const auto usePrecomputed = material && detail::terrainMaterialLevelsMatch(*material);
  if (material)
    for (auto slot = size_t{0}; slot < uniform.size(); ++slot)
      uniform[slot] = isUniform(material->images[slot]);

  // Base textures
  std::vector<project::WadTextureEntry> bases;
  constexpr std::array<std::string_view, 3> names{"grass", "dirt", "rock"};
  bases.reserve(3);
  for (size_t idx = 0; idx < 3; ++idx)
  {
    if (usePrecomputed && hasMaterialLevels(material->baseLevels[idx]))
    {
      bases.push_back(makeTexture(
        fmt::format("tm_{}", names[idx]), Period, material->baseLevels[idx], pal));
      continue;
    }
    bases.push_back(generateTexture(
      fmt::format("tm_{}", names[idx]),
      [idx](double, double) {
        std::array<double, 3> w{};
        w[idx] = 1.0;
        return w;
      },
      0.0,
      0.0,
      Period,
      pal,
      detail,
      material,
      nearestColors));
  }

  // Built-in pairs are generated once. Custom pairs are generated or loaded only
  // when a face actually needs one.
  std::map<int, project::WadTextureEntry> pairs;
  auto pairLookup = std::array<size_t, 9>{};
  const auto makePair = [&](const size_t pair) {
    const auto [a, b] = MaterialPairs[pair];
    const auto name = fmt::format("tm_{}_{}", names[size_t(a)], names[size_t(b)]);
    if (usePrecomputed && hasMaterialLevels(material->pairLevels[pair]))
      return makeTexture(name, Period, material->pairLevels[pair], pal);
    return generateTexture(
      name,
      [a, b](double, const double y) {
        const auto weight = clamp((y - LowV) / (HighV - LowV), 0.0, 1.0);
        auto weights = std::array<double, 3>{};
        weights[size_t(a)] = 1.0 - weight;
        weights[size_t(b)] = weight;
        return weights;
      },
      0.0,
      0.0,
      Period,
      pal,
      detail,
      material,
      nearestColors);
  };
  for (auto pair = size_t{0}; pair < MaterialPairs.size(); ++pair)
  {
    const auto [a, b] = MaterialPairs[pair];
    if (
      material
      && (mode != TerrainBakeMode::Shared || region.patchDensityPercent != 100 || !uniform[size_t(a)] || !uniform[size_t(b)]))
      continue;
    const auto key = a * 3 + b;
    pairLookup[size_t(key)] = pair + 1;
    if (!material)
      pairs.emplace(key, makePair(pair));
  }

  auto mixedTiles = 0;
  for (auto row = 0; row < Rows; ++row)
  {
    for (auto col = 0; col < Columns; ++col)
    {
      constexpr auto cellsPerTile = static_cast<int>(Core / Step); // 4
      std::vector<std::array<double, 3>> corners;
      corners.reserve((cellsPerTile + 1) * (cellsPerTile + 1));
      for (auto dy = 0; dy <= cellsPerTile; ++dy)
      {
        for (auto dx = 0; dx <= cellsPerTile; ++dx)
        {
          corners.push_back(vertices[static_cast<size_t>(
            (row * cellsPerTile + dy) * Stride + col * cellsPerTile + dx)]);
        }
      }
      if (activeMaterials(corners).size() > 1)
        ++mixedTiles;
    }
  }

  auto textures = bases;
  std::map<int, size_t> pairIndices;
  std::map<int, size_t> sharedTiles;
  std::map<std::vector<std::uint8_t>, size_t> sharedContents;
  std::vector<PatchTile> patchTiles;
  std::map<std::pair<int, int>, size_t> patchKeyToIndex;
  std::vector<BakeFace> faces;
  std::vector<bool> cellCovered(static_cast<size_t>(Across * Down), false);

  std::vector<int> pure(static_cast<size_t>(Across * Down), -1);
  for (size_t cell = 0; cell < quads.size(); ++cell)
  {
    const std::array<std::array<double, 3>, 4> quadCorners{
      quads[cell].a, quads[cell].b, quads[cell].c, quads[cell].d};
    const auto active = activeMaterials(quadCorners);
    if (active.size() == 1)
      pure[cell] = active[0];
  }

  std::vector<std::array<MixInfo, 2>> mixes(static_cast<size_t>(Across * Down));
  for (size_t cell = 0; cell < quads.size(); ++cell)
  {
    if (pure[cell] >= 0)
      continue;
    const std::array<std::array<double, 3>, 4> quadCorners{
      quads[cell].a, quads[cell].b, quads[cell].c, quads[cell].d};
    for (auto half = 0; half < 2; ++half)
    {
      const std::array<std::array<double, 3>, 3> triangleCorners{
        quadCorners[0],
        quadCorners[static_cast<size_t>(half ? 2 : 1)],
        quadCorners[static_cast<size_t>(half ? 3 : 2)]};
      const auto active = activeMaterials(triangleCorners);
      auto& mix = mixes[cell][static_cast<size_t>(half)];
      if (active.size() == 1)
      {
        mix = {"base", active[0], 0, {}};
      }
      else if (active.size() == 3)
      {
        mix = {"junction", -1, 0, {}};
      }
      else
      {
        const auto a = active[0];
        const auto b = active[1];
        const auto v0 =
          LowV + triangleCorners[0][static_cast<size_t>(b)] * (HighV - LowV);
        const auto v1 =
          LowV + triangleCorners[1][static_cast<size_t>(b)] * (HighV - LowV);
        const auto v2 =
          LowV + triangleCorners[2][static_cast<size_t>(b)] * (HighV - LowV);
        const auto vx = (half ? v1 - v2 : v1 - v0) / Step;
        const auto vy = (half ? v2 - v0 : v2 - v1) / Step;
        const auto cellCol = static_cast<int>(cell) % Across;
        const auto cellRow = static_cast<int>(cell) / Across;
        const auto x = cellCol * int(Step);
        const auto y = cellRow * int(Step);
        const auto tileX = x / Period;
        const auto tileY = y / Period;
        // Eight base texels keep all four MIP levels inside the sampled tile.
        // UVs aligned to the terrain grid preserve the source images and paint.
        const auto insideSharedTile =
          x >= tileX * Period + int(PatchGutter) && y >= tileY * Period + int(PatchGutter)
          && x + int(Step) <= (tileX + 1) * Period - int(PatchGutter)
          && y + int(Step) <= (tileY + 1) * Period - int(PatchGutter);
        const auto offset = v0 - vx * x - vy * y;
        if (
          pairLookup[size_t(a * 3 + b)] != 0 && usableTransitionUv(vx, vy)
          && (!material || matchesPairNeighborhood(cellCol, cellRow, a, b, vx, vy, offset)))
        {
          // A rotated transition preserves arbitrary procedural textures only for the
          // built-in material. Custom sources must be uniform in both slots.
          mix = {"pair", -1, a * 3 + b, vm::vec3d{vx, vy, offset}};
        }
        else if (
          material && mode == TerrainBakeMode::Shared && region.patchDensityPercent == 100
          && insideSharedTile)
        {
          constexpr auto tilesAcross = (int(Width) + Period - 1) / Period;
          mix = {"shared", -1, tileY * tilesAcross + tileX, WorldV};
        }
        else
        {
          mix = {"fit", -1, 0, {}};
        }
      }
    }
  }

  auto addFace = [&](
                   QPolygonF points,
                   const vm::vec3d& u,
                   const vm::vec3d& v,
                   const size_t mat,
                   const TerrainFaceKind kind) -> size_t {
    faces.push_back({std::move(points), u, v, mat, kind});
    return faces.size() - 1;
  };

  auto patchFace = [&](QPolygonF points, const int px, const int py) -> size_t {
    const auto key = std::pair{px, py};
    auto it = patchKeyToIndex.find(key);
    if (it == patchKeyToIndex.end())
    {
      const auto idx = patchTiles.size();
      patchKeyToIndex.emplace(key, idx);
      auto levels = rasterizeLevels(
        sample,
        static_cast<double>(px) - PatchGutter,
        static_cast<double>(py) - PatchGutter,
        patchSize,
        1.0 / density,
        pal,
        detail,
        material,
        nearestColors);
      patchTiles.push_back({px, py, std::move(levels), {}});
      it = patchKeyToIndex.find(key);
    }
    const auto faceIdx = addFace(
      std::move(points),
      vm::vec3d{density, 0.0, (PatchGutter - static_cast<double>(px)) * density},
      vm::vec3d{0.0, density, (PatchGutter - static_cast<double>(py)) * density},
      size_t(-1),
      TerrainFaceKind::Local);
    patchTiles[it->second].faceIndices.push_back(faceIdx);
    return faceIdx;
  };

  // Merge busy blocks
  struct Candidate
  {
    int row = 0;
    int col = 0;
    std::array<size_t, 4> cells{};
    int count = 0;
    std::string kind;
  };
  std::vector<Candidate> candidates;
  for (auto row = 0; row < Down; row += 2)
  {
    for (auto col = 0; col < Across; col += 2)
    {
      const auto cell = static_cast<size_t>(row * Across + col);
      const std::array<size_t, 4> cells{
        cell,
        cell + 1,
        cell + static_cast<size_t>(Across),
        cell + static_cast<size_t>(Across) + 1};
      std::vector<size_t> mixed;
      for (const auto c : cells)
      {
        if (pure[c] < 0)
          mixed.push_back(c);
      }
      if (mixed.size() < 2)
        continue;
      auto has = [&](const std::string_view k) {
        return std::ranges::any_of(mixed, [&](const size_t c) {
          return mixes[c][0].kind == k || mixes[c][1].kind == k;
        });
      };
      if (has("shared") || (material && has("pair")))
        continue;
      const auto kind = has("junction") ? "junction" : has("fit") ? "fit" : "patch";
      if (kind != std::string_view{"patch"} || mixed.size() >= 3)
      {
        candidates.push_back(
          {row, col, cells, static_cast<int>(mixed.size()), std::string{kind}});
      }
    }
  }

  const auto extraPatches = std::min(16, mixedTiles / 2);
  auto addedPatches = 0;
  std::stable_sort(
    candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
      if (a.count != b.count)
        return a.count > b.count;
      if (a.row != b.row)
        return a.row < b.row;
      return a.col < b.col;
    });

  for (const auto& cand : candidates)
  {
    if (cand.kind == "patch" && addedPatches >= extraPatches)
      continue;
    if (cand.kind == "patch")
      ++addedPatches;
    const auto px = static_cast<int>(cand.col * Step);
    const auto py = static_cast<int>(cand.row * Step);
    patchFace(
      makeRect(
        static_cast<double>(px),
        static_cast<double>(py),
        static_cast<double>(px) + PatchCore,
        static_cast<double>(py) + PatchCore),
      px,
      py);
    for (const auto c : cand.cells)
      cellCovered[c] = true;
  }

  // Iterate all cells
  for (auto row = 0; row < Down; ++row)
  {
    for (auto col = 0; col < Across; ++col)
    {
      const auto cell = static_cast<size_t>(row * Across + col);
      const auto x = static_cast<double>(col * Step);
      const auto y = static_cast<double>(row * Step);
      if (cellCovered[cell])
        continue;

      if (pure[cell] >= 0)
      {
        auto w = 1;
        auto h = 1;
        while (col + w < Across && pure[cell + static_cast<size_t>(w)] == pure[cell]
               && !cellCovered[cell + static_cast<size_t>(w)])
        {
          ++w;
        }
        while (row + h < Down)
        {
          const auto start = cell + static_cast<size_t>(h * Across);
          auto ok = true;
          for (auto i = 0; i < w; ++i)
          {
            if (
              pure[start + static_cast<size_t>(i)] != pure[cell]
              || cellCovered[start + static_cast<size_t>(i)])
            {
              ok = false;
              break;
            }
          }
          if (!ok)
            break;
          ++h;
        }
        const auto right = x + static_cast<double>(w) * Step;
        const auto bottom = y + static_cast<double>(h) * Step;
        addFace(
          makeRect(x, y, right, bottom),
          WorldU,
          WorldV,
          static_cast<size_t>(pure[cell]),
          TerrainFaceKind::Base);
        for (auto dy = 0; dy < h; ++dy)
        {
          for (auto dx = 0; dx < w; ++dx)
          {
            cellCovered[cell + static_cast<size_t>(dy * Across + dx)] = true;
          }
        }
        continue;
      }

      const auto points = makeRect(x, y, x + Step, y + Step);
      const auto& first = mixes[cell][0];
      const auto& second = mixes[cell][1];

      auto createSurface = [&](QPolygonF poly, const MixInfo& mix) {
        if (mix.kind == "base")
        {
          addFace(
            std::move(poly),
            WorldU,
            WorldV,
            static_cast<size_t>(mix.material),
            TerrainFaceKind::Base);
          return;
        }
        if (mix.kind == "shared")
        {
          auto it = sharedTiles.find(mix.key);
          if (it == sharedTiles.end())
          {
            constexpr auto tilesAcross = (int(Width) + Period - 1) / Period;
            const auto tileX = (mix.key % tilesAcross) * Period;
            const auto tileY = (mix.key / tilesAcross) * Period;
            auto texture = generateTexture(
              fmt::format("tm_s{}", mix.key),
              sample,
              double(tileX),
              double(tileY),
              Period,
              pal,
              detail,
              material,
              nearestColors);
            // The first 16 bytes contain the texture name, not its pixels.
            auto content = std::vector<std::uint8_t>{
              texture.miptex.begin() + 16, texture.miptex.end()};
            const auto existing = sharedContents.find(content);
            auto textureIndex = size_t{0};
            if (existing != sharedContents.end())
              textureIndex = existing->second;
            else
            {
              textureIndex = textures.size();
              textures.push_back(std::move(texture));
              sharedContents.emplace(std::move(content), textureIndex);
            }
            it = sharedTiles.emplace(mix.key, textureIndex).first;
          }
          addFace(std::move(poly), WorldU, WorldV, it->second, TerrainFaceKind::Shared);
          return;
        }
        if (mix.kind != "pair")
        {
          const auto px = static_cast<int>(std::floor(x / PatchCore) * PatchCore);
          const auto py = static_cast<int>(std::floor(y / PatchCore) * PatchCore);
          patchFace(std::move(poly), px, py);
          return;
        }
        auto it = pairIndices.find(mix.key);
        if (it == pairIndices.end())
        {
          if (!pairs.contains(mix.key))
            pairs.emplace(mix.key, makePair(pairLookup[size_t(mix.key)] - 1));
          const auto matIdx = textures.size();
          pairIndices.emplace(mix.key, matIdx);
          textures.push_back(pairs.at(mix.key));
          it = pairIndices.find(mix.key);
        }
        addFace(std::move(poly), SharedU, mix.v, it->second, TerrainFaceKind::Shared);
      };

      const auto samePair = first.kind == "pair" && second.kind == "pair"
                            && first.key == second.key
                            && vm::is_equal(first.v, second.v, 1e-8);
      const auto sameShared =
        first.kind == "shared" && second.kind == "shared" && first.key == second.key;
      const auto samePatch =
        first.kind == second.kind && (first.kind == "junction" || first.kind == "fit");

      if (sameShared || (material && samePair))
      {
        const auto reusable = [&](const size_t candidate) {
          const auto& halves = mixes[candidate];
          if (cellCovered[candidate])
            return false;
          if (sameShared)
            return halves[0].kind == "shared" && halves[1].kind == "shared"
                   && halves[0].key == first.key && halves[1].key == first.key;
          return halves[0].kind == "pair" && halves[1].kind == "pair"
                 && halves[0].key == first.key && halves[1].key == first.key
                 && vm::is_equal(halves[0].v, first.v, 1e-8)
                 && vm::is_equal(halves[1].v, first.v, 1e-8);
        };
        auto width = 1;
        auto height = 1;
        while (col + width < Across && reusable(cell + size_t(width)))
          ++width;
        while (row + height < Down)
        {
          const auto start = cell + size_t(height * Across);
          auto allReusable = true;
          for (auto dx = 0; dx < width; ++dx)
            allReusable &= reusable(start + size_t(dx));
          if (!allReusable)
            break;
          ++height;
        }
        createSurface(
          makeRect(x, y, x + double(width) * Step, y + double(height) * Step), first);
        for (auto dy = 0; dy < height; ++dy)
          for (auto dx = 0; dx < width; ++dx)
            cellCovered[cell + size_t(dy * Across + dx)] = true;
        continue;
      }
      if (samePair || samePatch)
      {
        createSurface(points, first);
      }
      else
      {
        QPolygonF tri0;
        tri0.reserve(3);
        tri0.append(points[0]);
        tri0.append(points[1]);
        tri0.append(points[2]);

        QPolygonF tri1;
        tri1.reserve(3);
        tri1.append(points[0]);
        tri1.append(points[2]);
        tri1.append(points[3]);

        createSurface(std::move(tri0), first);
        createSurface(std::move(tri1), second);
      }
      cellCovered[cell] = true;
    }
  }

  // Pack patches into texture atlases
  for (size_t start = 0; start < patchTiles.size();
       start += static_cast<size_t>(atlasCapacity))
  {
    const auto count =
      std::min(static_cast<size_t>(atlasCapacity), patchTiles.size() - start);
    const auto tilesSlice = std::span{&patchTiles[start], count};
    const auto cols =
      powerOfTwo(static_cast<int>(std::ceil(std::sqrt(tilesSlice.size()))));
    const auto rows = powerOfTwo(static_cast<int>(
      std::ceil(static_cast<double>(tilesSlice.size()) / static_cast<double>(cols))));
    const auto atlasWidth = cols * patchSize;
    const auto atlasHeight = rows * patchSize;

    std::array<std::vector<std::uint8_t>, 4> levels;
    for (size_t mip = 0; mip < 4; ++mip)
    {
      levels[mip].resize(
        static_cast<size_t>((atlasWidth >> mip) * (atlasHeight >> mip)), 0);
    }

    for (auto slot = 0; slot < cols * rows; ++slot)
    {
      const auto& tile = tilesSlice[static_cast<size_t>(slot) % tilesSlice.size()];
      const auto sx = (slot % cols) * patchSize;
      const auto sy = (slot / cols) * patchSize;

      for (size_t mip = 0; mip < 4; ++mip)
      {
        const auto side = patchSize >> mip;
        const auto width = atlasWidth >> mip;
        const auto offset = (sy >> mip) * width + (sx >> mip);
        for (auto r = 0; r < side; ++r)
        {
          std::memcpy(
            levels[mip].data() + offset + r * width,
            tile.levels[mip].data() + r * side,
            static_cast<size_t>(side));
        }
      }

      if (static_cast<size_t>(slot) < tilesSlice.size())
      {
        for (const auto faceIdx : tilesSlice[static_cast<size_t>(slot)].faceIndices)
        {
          faces[faceIdx].material = textures.size();
          faces[faceIdx].u[2] += static_cast<double>(sx);
          faces[faceIdx].v[2] += static_cast<double>(sy);
        }
      }
    }

    project::WadTextureEntry atlasEntry;
    atlasEntry.name = fmt::format("tm_a{}", start / static_cast<size_t>(atlasCapacity));
    atlasEntry.width = static_cast<std::uint32_t>(atlasWidth);
    atlasEntry.height = static_cast<std::uint32_t>(atlasHeight);
    atlasEntry.miptex =
      createMiptexLump(atlasEntry.name, atlasWidth, atlasHeight, levels, pal.palette);
    atlasEntry.valid = true;
    textures.push_back(std::move(atlasEntry));
  }

  return TerrainBake{std::move(faces), std::move(textures)};
}
} // namespace tb::ui
