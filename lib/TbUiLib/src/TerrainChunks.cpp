/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/TerrainChunks.h"

#include "mdl/Group.h"
#include "mdl/GroupNode.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace tb::ui
{
namespace
{
constexpr auto ChunkGroupMarker = "terrain-chunks";
constexpr auto Tolerance = 1e-6;
} // namespace

bool isTerrainChunkGroup(const mdl::GroupNode& group)
{
  return group.group().terrainPaint() == ChunkGroupMarker;
}

const char* terrainChunkGroupMarker()
{
  return ChunkGroupMarker;
}

Result<TerrainChunks> readTerrainChunks(const mdl::GroupNode& group)
{
  const auto invalid =
    Error{"The terrain chunks are missing, overlapping, or inconsistent."};
  if (!isTerrainChunkGroup(group) || group.children().empty())
    return invalid;

  auto chunks = std::vector<TerrainChunk>{};
  chunks.reserve(group.children().size());
  auto left = std::numeric_limits<double>::max();
  auto top = std::numeric_limits<double>::lowest();
  auto right = std::numeric_limits<double>::lowest();
  auto bottomY = std::numeric_limits<double>::max();
  for (auto i = size_t{0}; i < group.children().size(); ++i)
  {
    const auto* child = dynamic_cast<const mdl::GroupNode*>(group.children()[i]);
    if (!child || child->group().transformation() != group.group().transformation())
      return invalid;
    auto decoded = TerrainRegion::decode(child->group().terrainPaint());
    if (decoded.is_error() || !decoded.value().heightField)
      return invalid;
    auto region = std::move(decoded).value();
    const auto& field = *region.heightField;
    if (!chunks.empty())
    {
      const auto& first = chunks.front().region;
      const auto& reference = *first.heightField;
      if (
        field.spacing != reference.spacing || field.bottom != reference.bottom
        || region.materialAssetId != first.materialAssetId
        || region.maxAddedFaces != first.maxAddedFaces
        || region.maxWadKiB != first.maxWadKiB
        || region.minFaceWidth != first.minFaceWidth
        || region.patchDensityPercent != first.patchDensityPercent)
        return invalid;
    }
    left = std::min(left, region.origin.x());
    top = std::max(top, region.origin.y());
    right = std::max(right, region.origin.x() + field.columns * field.spacing);
    bottomY = std::min(bottomY, region.origin.y() - field.rows * field.spacing);
    chunks.push_back({i, 0, 0, std::move(region)});
  }

  const auto spacing = chunks.front().region.heightField->spacing;
  const auto columns = (right - left) / spacing;
  const auto rows = (top - bottomY) / spacing;
  if (
    !std::isfinite(columns) || !std::isfinite(rows) || columns < 1 || rows < 1
    || columns > 4096 || rows > 4096
    || std::abs(columns - std::round(columns)) > Tolerance
    || std::abs(rows - std::round(rows)) > Tolerance)
    return invalid;
  auto combined = std::make_unique<TerrainRegion>(chunks.front().region);
  combined->origin = {left, top};
  auto created = TerrainHeightField::create(
    int(std::round(columns)),
    int(std::round(rows)),
    spacing,
    combined->heightField->bottom,
    combined->heightField->bottom + 1.0);
  if (created.is_error())
    return invalid;
  auto field = std::move(created).value();
  auto assigned = std::vector<bool>(field.heights.size(), false);
  auto cells = std::vector<bool>(size_t(field.columns) * size_t(field.rows), false);
  for (auto& chunk : chunks)
  {
    const auto column = (chunk.region.origin.x() - left) / spacing;
    const auto row = (top - chunk.region.origin.y()) / spacing;
    if (
      std::abs(column - std::round(column)) > Tolerance
      || std::abs(row - std::round(row)) > Tolerance)
      return invalid;
    chunk.column = int(std::round(column));
    chunk.row = int(std::round(row));
    const auto paintStep = TerrainRegion::Step * TerrainRegion::UnitsPerTexel;
    const auto paintColumn = (chunk.region.origin.x() - left) / paintStep;
    const auto paintRow = (top - chunk.region.origin.y()) / paintStep;
    if (
      std::abs(paintColumn - std::round(paintColumn)) > Tolerance
      || std::abs(paintRow - std::round(paintRow)) > Tolerance)
      return invalid;
    const auto& part = *chunk.region.heightField;
    if (
      chunk.column < 0 || chunk.row < 0 || chunk.column + part.columns > field.columns
      || chunk.row + part.rows > field.rows)
      return invalid;
    for (auto y = 0; y <= part.rows; ++y)
      for (auto x = 0; x <= part.columns; ++x)
      {
        const auto index = field.index(chunk.column + x, chunk.row + y);
        const auto value = part.heights[part.index(x, y)];
        if (assigned[index] && std::abs(field.heights[index] - value) > Tolerance)
          return invalid;
        field.heights[index] = value;
        assigned[index] = true;
        if (x < part.columns && y < part.rows)
        {
          const auto cell =
            size_t(chunk.row + y) * size_t(field.columns) + size_t(chunk.column + x);
          if (cells[cell])
            return invalid;
          cells[cell] = true;
        }
      }
  }
  if (!std::ranges::all_of(cells, [](const bool occupied) { return occupied; }))
    return invalid;

  // A shared boundary must use the same paint samples on both sides. Include the
  // samples just outside a partial chunk so bilinear interpolation stays continuous.
  const auto paintStep = TerrainRegion::Step * TerrainRegion::UnitsPerTexel;
  for (auto i = size_t{0}; i < chunks.size(); ++i)
    for (auto j = i + 1; j < chunks.size(); ++j)
    {
      const auto& a = chunks[i].region;
      const auto& b = chunks[j].region;
      const auto& fa = *a.heightField;
      const auto& fb = *b.heightField;
      const auto aRight = a.origin.x() + fa.columns * spacing;
      const auto bRight = b.origin.x() + fb.columns * spacing;
      const auto aBottom = a.origin.y() - fa.rows * spacing;
      const auto bBottom = b.origin.y() - fb.rows * spacing;
      const auto matches = [&](const double x, const double y) {
        const auto ax = int(std::round((x - a.origin.x()) / paintStep));
        const auto ay = int(std::round((a.origin.y() - y) / paintStep));
        const auto bx = int(std::round((x - b.origin.x()) / paintStep));
        const auto by = int(std::round((b.origin.y() - y) / paintStep));
        if (
          ax < 0 || ax > TerrainRegion::Columns || bx < 0 || bx > TerrainRegion::Columns
          || ay < 0 || ay > TerrainRegion::Rows || by < 0 || by > TerrainRegion::Rows)
          return false;
        const auto& aw = a.weight(ax, ay);
        const auto& bw = b.weight(bx, by);
        for (auto slot = size_t{0}; slot < aw.size(); ++slot)
          if (std::abs(aw[slot] - bw[slot]) > Tolerance)
            return false;
        return true;
      };
      if (
        std::abs(aRight - b.origin.x()) <= Tolerance
        || std::abs(bRight - a.origin.x()) <= Tolerance)
      {
        const auto x = std::abs(aRight - b.origin.x()) <= Tolerance ? aRight : bRight;
        const auto boundary = (x - left) / paintStep;
        if (std::abs(boundary - std::round(boundary)) > Tolerance)
          return invalid;
        const auto first =
          int(std::floor((top - std::min(a.origin.y(), b.origin.y())) / paintStep));
        const auto last = int(std::ceil((top - std::max(aBottom, bBottom)) / paintStep));
        for (auto row = first; row <= last; ++row)
          if (!matches(x, top - double(row) * paintStep))
            return invalid;
      }
      if (
        std::abs(aBottom - b.origin.y()) <= Tolerance
        || std::abs(bBottom - a.origin.y()) <= Tolerance)
      {
        const auto y = std::abs(aBottom - b.origin.y()) <= Tolerance ? aBottom : bBottom;
        const auto boundary = (top - y) / paintStep;
        if (std::abs(boundary - std::round(boundary)) > Tolerance)
          return invalid;
        const auto first =
          int(std::floor((std::max(a.origin.x(), b.origin.x()) - left) / paintStep));
        const auto last = int(std::ceil((std::min(aRight, bRight) - left) / paintStep));
        for (auto column = first; column <= last; ++column)
          if (!matches(left + double(column) * paintStep, y))
            return invalid;
      }
    }
  combined->heightField = std::move(field);
  return TerrainChunks{std::move(combined), std::move(chunks)};
}

Result<TerrainRegion> readTerrainGroup(const mdl::GroupNode& group)
{
  if (!isTerrainChunkGroup(group))
    return TerrainRegion::decode(group.group().terrainPaint());
  auto chunks = readTerrainChunks(group);
  if (chunks.is_error())
    return std::get<Error>(chunks.error());
  auto result = std::move(chunks).value();
  return std::move(*result.combined);
}

Result<std::vector<TerrainSurface>> terrainGroupSurfaces(
  const mdl::GroupNode& group, const TerrainRegion& region)
{
  if (!isTerrainChunkGroup(group))
    return terrainSurfaces(group, region);
  const auto chunks = readTerrainChunks(group);
  if (chunks.is_error())
    return std::get<Error>(chunks.error());
  auto surfaces = std::vector<TerrainSurface>{};
  for (const auto& chunk : chunks.value().chunks)
  {
    const auto* child =
      static_cast<const mdl::GroupNode*>(group.children()[chunk.childIndex]);
    auto part = terrainSurfaces(*child, chunk.region);
    if (part.is_error())
      return std::get<Error>(part.error());
    const auto offset = QPointF{
      (chunk.region.origin.x() - region.origin.x()) / TerrainRegion::UnitsPerTexel,
      (region.origin.y() - chunk.region.origin.y()) / TerrainRegion::UnitsPerTexel};
    auto values = std::move(part).value();
    for (auto& surface : values)
    {
      surface.outline.translate(offset);
      surfaces.push_back(std::move(surface));
    }
  }
  return surfaces;
}

std::string terrainGroupRevision(const mdl::GroupNode& group)
{
  auto result = group.group().terrainPaint();
  if (isTerrainChunkGroup(group))
  {
    for (const auto* child : group.children())
    {
      if (const auto* chunk = dynamic_cast<const mdl::GroupNode*>(child))
      {
        const auto& encoded = chunk->group().terrainPaint();
        result += ':' + std::to_string(encoded.size()) + ':' + encoded;
      }
      else
        result += ":invalid";
    }
  }
  return result;
}
} // namespace tb::ui
