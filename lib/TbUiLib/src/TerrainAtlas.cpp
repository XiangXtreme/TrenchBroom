/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/TerrainAtlas.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <utility>

namespace tb::ui::detail
{
namespace
{
constexpr auto AtlasSize = 256;
constexpr auto PatchCore = 48;

int nextPowerOfTwo(const int value)
{
  auto result = 1;
  while (result < value)
    result *= 2;
  return result;
}

size_t atlasBytes(const int width, const int height)
{
  auto bytes = size_t{40 + 2 + 768}; // miptex header and palette
  for (auto mip = 0; mip < 4; ++mip)
    bytes += size_t((width >> mip) * (height >> mip));
  return ((bytes + 3) & ~size_t{3}) + 32; // alignment and WAD directory entry
}

size_t atlasBytes(const std::vector<TerrainAtlasLayout>& layouts)
{
  auto bytes = size_t{0};
  for (const auto& layout : layouts)
    bytes += atlasBytes(layout.width, layout.height);
  return bytes;
}

bool matchingEdge(
  const TerrainAtlasPatch& first,
  const TerrainAtlasPatch& second,
  const int patchSize,
  const int coreSize,
  const bool vertical)
{
  for (auto mip = 0; mip < 4; ++mip)
  {
    const auto side = patchSize >> mip;
    const auto overlap = (patchSize - coreSize) >> mip;
    const auto& a = first.levels[size_t(mip)];
    const auto& b = second.levels[size_t(mip)];
    if (a.size() != size_t(side * side) || b.size() != size_t(side * side))
      return false;
    if (vertical)
    {
      if (!std::equal(a.end() - overlap * side, a.end(), b.begin()))
        return false;
    }
    else
    {
      for (auto row = 0; row < side; ++row)
      {
        const auto offset = row * side;
        if (!std::equal(
              a.begin() + offset + side - overlap,
              a.begin() + offset + side,
              b.begin() + offset))
          return false;
      }
    }
  }
  return true;
}

std::vector<TerrainAtlasLayout> regularPacking(
  const std::span<const TerrainAtlasPatch> patches, const int patchSize)
{
  auto result = std::vector<TerrainAtlasLayout>{};
  const auto side = AtlasSize / patchSize;
  const auto capacity = size_t(side * side);
  for (auto start = size_t{0}; start < patches.size(); start += capacity)
  {
    const auto count = std::min(capacity, patches.size() - start);
    const auto columns = nextPowerOfTwo(int(std::ceil(std::sqrt(double(count)))));
    const auto rows = nextPowerOfTwo(int(std::ceil(double(count) / columns)));
    auto layout = TerrainAtlasLayout{columns * patchSize, rows * patchSize, {}};
    for (auto slot = 0; slot < columns * rows; ++slot)
    {
      layout.placements.push_back(
        {start + size_t(slot) % count,
         (slot % columns) * patchSize,
         (slot / columns) * patchSize,
         size_t(slot) < count});
    }
    result.push_back(std::move(layout));
  }
  return result;
}

struct Chart
{
  std::vector<size_t> patches;
  int width = 0;
};

struct Shelf
{
  int width = 0;
  std::vector<Chart> charts;
};

std::optional<std::vector<TerrainAtlasLayout>> sharedPacking(
  const std::span<const TerrainAtlasPatch> patches,
  const int patchSize,
  const int coreSize,
  const bool vertical,
  const size_t atlasLimit)
{
  auto positions = std::map<std::pair<int, int>, size_t>{};
  for (auto i = size_t{0}; i < patches.size(); ++i)
  {
    const auto& patch = patches[i];
    positions.emplace(
      vertical ? std::pair{patch.x, patch.y} : std::pair{patch.y, patch.x}, i);
  }

  const auto maxPatches = (AtlasSize - (patchSize - coreSize)) / coreSize;
  auto consumed = std::vector<bool>(patches.size());
  auto charts = std::vector<Chart>{};
  for (const auto& [position, index] : positions)
  {
    if (consumed[index])
      continue;
    auto chart = Chart{};
    auto next = index;
    auto coordinate = position.second;
    while (true)
    {
      consumed[next] = true;
      chart.patches.push_back(next);
      if (int(chart.patches.size()) >= maxPatches)
        break;
      coordinate += PatchCore;
      const auto candidate = positions.find({position.first, coordinate});
      if (
        candidate == positions.end() || consumed[candidate->second]
        || !matchingEdge(
          patches[next], patches[candidate->second], patchSize, coreSize, vertical))
        break;
      next = candidate->second;
    }
    chart.width = patchSize + (int(chart.patches.size()) - 1) * coreSize;
    charts.push_back(std::move(chart));
  }
  std::stable_sort(charts.begin(), charts.end(), [](const auto& a, const auto& b) {
    return a.width > b.width;
  });

  // First-fit decreasing keeps independent charts separated by their full gutters.
  auto shelves = std::vector<Shelf>{};
  for (auto& chart : charts)
  {
    auto shelf = std::ranges::find_if(shelves, [&](const auto& candidate) {
      return candidate.width + chart.width <= AtlasSize;
    });
    if (shelf == shelves.end())
    {
      shelves.push_back({});
      shelf = std::prev(shelves.end());
    }
    shelf->width += chart.width;
    shelf->charts.push_back(std::move(chart));
  }
  std::stable_sort(shelves.begin(), shelves.end(), [](const auto& a, const auto& b) {
    return a.width > b.width;
  });

  // Partition shelves into power-of-two atlases, including each palette and header
  // in the cost. The regular layout's atlas count is a hard upper bound.
  struct Cost
  {
    size_t bytes = std::numeric_limits<size_t>::max();
    size_t atlases = std::numeric_limits<size_t>::max();
    size_t shelves = 0;
  };
  auto best =
    std::vector<std::vector<Cost>>(shelves.size() + 1, std::vector<Cost>(atlasLimit + 1));
  for (auto remaining = size_t{0}; remaining <= atlasLimit; ++remaining)
    best.back()[remaining] = {0, 0, 0};
  const auto maxShelves = size_t(AtlasSize / patchSize);
  for (auto i = shelves.size(); i-- > 0;)
  {
    for (auto remaining = size_t{1}; remaining <= atlasLimit; ++remaining)
    {
      for (auto count = size_t{1}; count <= maxShelves && i + count <= shelves.size();
           ++count)
      {
        const auto& next = best[i + count][remaining - 1];
        if (next.bytes == std::numeric_limits<size_t>::max())
          continue;
        const auto width = nextPowerOfTwo(shelves[i].width);
        const auto height = nextPowerOfTwo(int(count) * patchSize);
        const auto candidate =
          Cost{atlasBytes(width, height) + next.bytes, 1 + next.atlases, count};
        auto& current = best[i][remaining];
        if (
          candidate.bytes < current.bytes
          || (candidate.bytes == current.bytes && candidate.atlases < current.atlases))
          current = candidate;
      }
    }
  }
  if (best[0][atlasLimit].bytes == std::numeric_limits<size_t>::max())
    return std::nullopt;

  auto result = std::vector<TerrainAtlasLayout>{};
  auto remaining = atlasLimit;
  for (auto i = size_t{0}; i < shelves.size();)
  {
    const auto count = best[i][remaining].shelves;
    const auto width = nextPowerOfTwo(shelves[i].width);
    const auto height = nextPowerOfTwo(int(count) * patchSize);
    auto layout =
      TerrainAtlasLayout{vertical ? height : width, vertical ? width : height, {}};
    for (auto row = size_t{0}; row < count; ++row)
    {
      auto along = 0;
      for (const auto& chart : shelves[i + row].charts)
      {
        for (auto p = size_t{0}; p < chart.patches.size(); ++p)
        {
          const auto u = along + int(p) * coreSize;
          const auto v = int(row) * patchSize;
          layout.placements.push_back(
            {chart.patches[p], vertical ? v : u, vertical ? u : v, true});
        }
        along += chart.width;
      }
    }
    result.push_back(std::move(layout));
    i += count;
    --remaining;
  }
  return result;
}
} // namespace

std::vector<TerrainAtlasLayout> packTerrainAtlases(
  const std::span<const TerrainAtlasPatch> patches,
  const int patchSize,
  const int coreSize)
{
  auto best = regularPacking(patches, patchSize);
  const auto atlasLimit = best.size();
  for (const auto vertical : {false, true})
  {
    auto candidate = sharedPacking(patches, patchSize, coreSize, vertical, atlasLimit);
    if (!candidate)
      continue;
    const auto candidateBytes = atlasBytes(*candidate);
    const auto bestBytes = atlasBytes(best);
    if (
      candidateBytes < bestBytes
      || (candidateBytes == bestBytes && candidate->size() < best.size()))
      best = std::move(*candidate);
  }
  return best;
}
} // namespace tb::ui::detail
