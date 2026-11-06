/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/TerrainHeightField.h"

#include "mdl/BrushBuilder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <utility>

namespace tb::ui
{
namespace
{
constexpr auto MaxCellsPerAxis = 4096;
constexpr auto MaxVertices = size_t{4 * 1024 * 1024};
constexpr auto MinimumThickness = 1.0;
} // namespace

Result<TerrainHeightField> TerrainHeightField::create(
  const int columns,
  const int rows,
  const double spacing,
  const double bottom,
  const double height)
{
  if (
    columns <= 0 || rows <= 0 || columns > MaxCellsPerAxis || rows > MaxCellsPerAxis
    || size_t(columns + 1) * size_t(rows + 1) > MaxVertices || !std::isfinite(spacing)
    || spacing <= 0.0 || !std::isfinite(double(columns) * spacing)
    || !std::isfinite(double(rows) * spacing) || !std::isfinite(bottom)
    || !std::isfinite(height) || height < bottom + MinimumThickness)
    return Error{"Invalid terrain height field dimensions or initial height."};

  auto result = TerrainHeightField{};
  result.columns = columns;
  result.rows = rows;
  result.spacing = spacing;
  result.bottom = bottom;
  result.heights.assign(size_t(columns + 1) * size_t(rows + 1), height);
  return result;
}

bool TerrainHeightField::valid() const
{
  return columns > 0 && rows > 0 && columns <= MaxCellsPerAxis && rows <= MaxCellsPerAxis
         && size_t(columns + 1) * size_t(rows + 1) <= MaxVertices
         && heights.size() == size_t(columns + 1) * size_t(rows + 1)
         && std::isfinite(spacing) && spacing > 0.0
         && std::isfinite(double(columns) * spacing)
         && std::isfinite(double(rows) * spacing) && std::isfinite(bottom)
         && std::ranges::all_of(heights, [&](const double height) {
              return std::isfinite(height) && height >= bottom + MinimumThickness;
            });
}

size_t TerrainHeightField::index(const int column, const int row) const
{
  return size_t(row) * size_t(columns + 1) + size_t(column);
}

std::optional<double> TerrainHeightField::sample(const vm::vec2d& position) const
{
  return valid() ? sampleValidated(position) : std::nullopt;
}

std::vector<std::optional<double>> TerrainHeightField::sampleMany(
  const std::span<const vm::vec2d> positions) const
{
  auto result = std::vector<std::optional<double>>{};
  result.reserve(positions.size());
  const auto isValid = valid();
  for (const auto& position : positions)
    result.push_back(isValid ? sampleValidated(position) : std::nullopt);
  return result;
}

std::optional<double> TerrainHeightField::sampleValidated(const vm::vec2d& position) const
{
  if (
    !std::isfinite(position.x()) || !std::isfinite(position.y()) || position.x() < 0.0
    || position.y() < 0.0 || position.x() > double(columns) * spacing
    || position.y() > double(rows) * spacing)
    return std::nullopt;

  const auto x = position.x() / spacing;
  const auto y = position.y() / spacing;
  const auto column = std::min(int(std::floor(x)), columns - 1);
  const auto row = std::min(int(std::floor(y)), rows - 1);
  const auto s = x - double(column);
  const auto t = y - double(row);
  const auto a = heights[index(column, row)];
  const auto b = heights[index(column + 1, row)];
  const auto c = heights[index(column + 1, row + 1)];
  const auto d = heights[index(column, row + 1)];
  // Match the two triangles used by the terrain surface and material sampler.
  return s >= t ? a * (1.0 - s) + b * (s - t) + c * t
                : a * (1.0 - t) + c * s + d * (t - s);
}

TerrainSculptResult TerrainHeightField::sculpt(
  const TerrainSculptMode mode,
  const vm::vec2d& position,
  const double radius,
  const double strength,
  const double hardness,
  const double amount,
  const double targetHeight,
  const bool lockEdges)
{
  auto result = TerrainSculptResult{};
  if (
    !valid() || !std::isfinite(position.x()) || !std::isfinite(position.y())
    || !std::isfinite(radius) || radius <= 0.0 || !std::isfinite(strength)
    || !std::isfinite(hardness) || !std::isfinite(amount) || amount < 0.0
    || !std::isfinite(targetHeight))
    return result;

  const auto left =
    int(std::clamp(std::ceil((position.x() - radius) / spacing), 0.0, double(columns)));
  const auto right =
    int(std::clamp(std::floor((position.x() + radius) / spacing), 0.0, double(columns)));
  const auto top =
    int(std::clamp(std::ceil((position.y() - radius) / spacing), 0.0, double(rows)));
  const auto bottomRow =
    int(std::clamp(std::floor((position.y() + radius) / spacing), 0.0, double(rows)));
  // Keep the original neighborhood intact while calculating this stamp. Collect only
  // affected vertices so a small brush does not copy an entire large height field.
  auto changes = std::vector<std::pair<size_t, double>>{};
  const auto edge = radius * std::max(1e-6, 1.0 - std::clamp(hardness, 0.0, 1.0));
  for (auto row = top; row <= bottomRow; ++row)
  {
    for (auto column = left; column <= right; ++column)
    {
      if (lockEdges && (row == 0 || column == 0 || row == rows || column == columns))
        continue;
      const auto delta = position - vm::vec2d{column * spacing, row * spacing};
      const auto distance = std::hypot(delta.x(), delta.y());
      const auto t = std::clamp((radius - distance) / edge, 0.0, 1.0);
      const auto weight = std::clamp(strength, 0.0, 1.0) * t * t * (3.0 - 2.0 * t);
      if (weight <= 0.0)
        continue;

      const auto current = heights[index(column, row)];
      auto proposed = current;
      switch (mode)
      {
      case TerrainSculptMode::Raise:
        proposed += amount * weight;
        break;
      case TerrainSculptMode::Lower:
        proposed -= amount * weight;
        break;
      case TerrainSculptMode::Flatten:
        proposed = std::lerp(current, targetHeight, weight);
        break;
      case TerrainSculptMode::Smooth: {
        auto mean = 0.0;
        auto count = 0;
        for (auto dy = -1; dy <= 1; ++dy)
          for (auto dx = -1; dx <= 1; ++dx)
            if (
              (dx != 0 || dy != 0) && column + dx >= 0 && column + dx <= columns
              && row + dy >= 0 && row + dy <= rows)
            {
              ++count;
              mean = std::lerp(
                mean, heights[index(column + dx, row + dy)], 1.0 / double(count));
            }
        if (count > 0)
          proposed = std::lerp(current, mean, weight);
        break;
      }
      }
      if (!std::isfinite(proposed))
        return {};
      if (proposed < bottom + MinimumThickness)
      {
        proposed = bottom + MinimumThickness;
        result.reachedBottom = true;
      }
      if (std::abs(proposed - current) > 1e-9)
      {
        changes.emplace_back(index(column, row), proposed);
      }
    }
  }
  for (const auto& [vertex, height] : changes)
    heights[vertex] = height;
  result.changed = !changes.empty();
  return result;
}

Result<std::vector<mdl::Brush>> createTerrainBrushes(
  const TerrainHeightField& field,
  const vm::vec2d& origin,
  const vm::mat4x4d& transform,
  const mdl::MapFormat mapFormat,
  const vm::bbox3d& worldBounds,
  const std::string& materialName)
{
  constexpr auto MaxBrushes = size_t{32768};
  if (!field.valid() || size_t(field.columns) * size_t(field.rows) > MaxBrushes / 2)
    return Error{"Invalid or excessively dense terrain geometry."};
  auto indices = std::vector<size_t>(size_t(field.columns) * size_t(field.rows) * 2);
  std::iota(indices.begin(), indices.end(), size_t{0});
  return createTerrainBrushes(
    field, origin, transform, mapFormat, worldBounds, materialName, indices);
}

Result<std::vector<mdl::Brush>> createTerrainBrushes(
  const TerrainHeightField& field,
  const vm::vec2d& origin,
  const vm::mat4x4d& transform,
  const mdl::MapFormat mapFormat,
  const vm::bbox3d& worldBounds,
  const std::string& materialName,
  const std::span<const size_t> triangleIndices)
{
  // A map compiler needs two convex brushes per cell. Reject impractical requests
  // before allocating or building partial geometry.
  constexpr auto MaxBrushes = size_t{32768};
  if (
    !field.valid() || !std::isfinite(origin.x()) || !std::isfinite(origin.y())
    || size_t(field.columns) * size_t(field.rows) > MaxBrushes / 2)
    return Error{"Invalid or excessively dense terrain geometry."};

  const auto worldOrigin = transform * vm::vec3d{origin.x(), origin.y(), 0};
  const auto vertical = transform * vm::vec3d{origin.x(), origin.y(), 1} - worldOrigin;
  if (
    !std::isfinite(worldOrigin.x()) || !std::isfinite(worldOrigin.y())
    || !std::isfinite(worldOrigin.z()) || !std::isfinite(vertical.x())
    || !std::isfinite(vertical.y()) || !std::isfinite(vertical.z())
    || std::hypot(vertical.x(), vertical.y()) > 1e-6 || vertical.z() <= 0.0)
    return Error{"Terrain groups must keep their local Z axis vertical."};

  const auto builder = mdl::BrushBuilder{mapFormat, worldBounds};
  auto brushes = std::vector<mdl::Brush>{};
  brushes.reserve(triangleIndices.size());
  const auto triangleCount = size_t(field.columns) * size_t(field.rows) * 2;
  for (const auto index : triangleIndices)
  {
    if (index >= triangleCount)
      return Error{"Invalid terrain triangle index."};
    const auto cell = index / 2;
    const auto column = int(cell % size_t(field.columns));
    const auto row = int(cell / size_t(field.columns));
    const auto vertices = std::array{
      std::pair{column, row},
      std::pair{column + 1, row},
      std::pair{column + 1, row + 1},
      std::pair{column, row + 1}};
    const auto triangle = index % 2 == 0 ? std::array{0, 1, 2} : std::array{0, 2, 3};
    auto points = std::vector<vm::vec3d>{};
    points.reserve(6);
    for (const auto i : triangle)
    {
      const auto [x, y] = vertices[size_t(i)];
      const auto localX = origin.x() + double(x) * field.spacing;
      const auto localY = origin.y() - double(y) * field.spacing;
      points.push_back(
        transform * vm::vec3d{localX, localY, field.heights[field.index(x, y)]});
      points.push_back(transform * vm::vec3d{localX, localY, field.bottom});
    }
    auto brush = builder.createBrush(points, materialName);
    if (brush.is_error())
      return std::get<Error>(brush.error());
    brushes.push_back(std::move(brush).value());
  }
  return brushes;
}
} // namespace tb::ui
