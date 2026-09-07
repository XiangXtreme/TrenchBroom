/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.
 */

#include "ui/automation/AutomationBrushes.h"

#include "mdl/BrushBuilder.h"
#include "mdl/BrushNode.h"
#include "mdl/Map.h"
#include "mdl/Map_World.h"
#include "mdl/WorldNode.h"

#include <cmath>

#include <algorithm>

namespace tb::ui::automation
{
namespace
{

constexpr auto GeometryEpsilon = 0.001;

double polygonSignedArea(const std::vector<vm::vec2d>& points)
{
  auto area = 0.0;
  for (size_t i = 0; i < points.size(); ++i)
  {
    const auto& current = points[i];
    const auto& next = points[(i + 1) % points.size()];
    area += current.x() * next.y() - next.x() * current.y();
  }
  return area * 0.5;
}

bool isStrictlyConvexPolygon(const std::vector<vm::vec2d>& points)
{
  if (points.size() < 3)
  {
    return false;
  }

  auto sign = 0;
  for (size_t i = 0; i < points.size(); ++i)
  {
    const auto& a = points[i];
    const auto& b = points[(i + 1) % points.size()];
    const auto& c = points[(i + 2) % points.size()];
    const auto ab = b - a;
    const auto bc = c - b;
    const auto cross = ab.x() * bc.y() - ab.y() * bc.x();
    if (std::abs(cross) <= GeometryEpsilon)
    {
      return false;
    }
    const auto currentSign = cross > 0.0 ? 1 : -1;
    if (sign == 0)
    {
      sign = currentSign;
    }
    else if (sign != currentSign)
    {
      return false;
    }
  }
  return true;
}

} // namespace

std::optional<std::vector<mdl::BrushNode*>> createBoxNodes(
  const mdl::Map& map, const std::vector<AutomationBoxSpec>& boxes, QString& error)
{
  if (boxes.empty())
  {
    error = "boxes must not be empty";
    return std::nullopt;
  }

  const auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};
  auto result = std::vector<mdl::BrushNode*>{};
  result.reserve(boxes.size());
  for (const auto& box : boxes)
  {
    const auto& min = box.bounds.min;
    const auto& max = box.bounds.max;
    if (
      !std::isfinite(min.x()) || !std::isfinite(min.y()) || !std::isfinite(min.z())
      || !std::isfinite(max.x()) || !std::isfinite(max.y()) || !std::isfinite(max.z())
      || min.x() >= max.x() || min.y() >= max.y() || min.z() >= max.z())
    {
      error = "box min must be smaller than max on all finite axes";
      for (auto* node : result)
      {
        delete node;
      }
      return std::nullopt;
    }

    auto brush = builder.createCuboid(box.bounds, box.material);
    if (brush.is_error())
    {
      error = "Could not create one or more box brushes";
      for (auto* node : result)
      {
        delete node;
      }
      return std::nullopt;
    }
    result.push_back(new mdl::BrushNode{std::move(brush).value()});
  }
  return result;
}

std::optional<mdl::Brush> createPrismBrush(
  const mdl::BrushBuilder& builder,
  std::vector<vm::vec2d> points,
  const double minZ,
  const double maxZ,
  const std::string& material,
  QString& error)
{
  if (!std::isfinite(minZ) || !std::isfinite(maxZ) || minZ >= maxZ)
  {
    error = "minZ must be smaller than maxZ";
    return std::nullopt;
  }
  if (!isStrictlyConvexPolygon(points))
  {
    error = "points2d must form a strictly convex polygon";
    return std::nullopt;
  }

  if (polygonSignedArea(points) < 0.0)
  {
    std::reverse(points.begin(), points.end());
  }

  auto vertices = std::vector<vm::vec3d>{};
  vertices.reserve(points.size() * 2u);
  for (const auto& point : points)
  {
    vertices.emplace_back(point.x(), point.y(), minZ);
  }
  for (const auto& point : points)
  {
    vertices.emplace_back(point.x(), point.y(), maxZ);
  }

  auto brush = builder.createBrush(vertices, material);
  if (brush.is_error())
  {
    error = "Could not create prism brush from points2d";
    return std::nullopt;
  }
  return std::move(brush).value();
}

} // namespace tb::ui::automation
