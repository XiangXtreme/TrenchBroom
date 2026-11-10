/*
 Copyright (C) 2026 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "render/EntityDirectionIndicator.h"

#include "gl/Camera.h"

#include "vm/constants.h"

#include <algorithm>
#include <cmath>

namespace tb::render
{
namespace
{

using Polygon = std::vector<vm::vec2f>;

auto normal(const vm::vec2f& v)
{
  return vm::vec2f{v.y(), -v.x()};
}

// Outward miter offset of a counterclockwise silhouette, including the two
// concave corners between the arrow's shaft and head.
Polygon expanded(const Polygon& polygon, const float amount)
{
  auto result = Polygon{};
  result.reserve(polygon.size());
  for (size_t i = 0; i < polygon.size(); ++i)
  {
    const auto& p = polygon[i];
    const auto n1 =
      normal(vm::normalize(p - polygon[(i + polygon.size() - 1) % polygon.size()]));
    const auto n2 = normal(vm::normalize(polygon[(i + 1) % polygon.size()] - p));
    result.push_back(p + (n1 + n2) * (amount / (1.0f + vm::dot(n1, n2))));
  }
  return result;
}

Polygon circle(const float radius)
{
  auto result = Polygon{};
  constexpr size_t segments = 24;
  result.reserve(segments);
  for (size_t i = 0; i < segments; ++i)
  {
    const auto angle =
      vm::Cf::two_pi() * static_cast<float>(i) / static_cast<float>(segments);
    result.emplace_back(radius * std::cos(angle), radius * std::sin(angle));
  }
  return result;
}

} // namespace

std::optional<EntityDirectionIndicator> makeEntityDirectionIndicator(
  const gl::Camera& camera, const vm::vec3f& center, const vm::vec3f& direction)
{
  const auto& viewport = camera.viewport();
  if (viewport.width <= 0 || viewport.height <= 0)
  {
    return std::nullopt;
  }
  const auto depth = camera.perpendicularDistanceTo(center);
  if (camera.perspectiveProjection() && depth <= camera.nearPlane())
  {
    return std::nullopt;
  }
  const auto screen = camera.project(center);
  constexpr auto margin = 48.0f;
  if (
    screen.x() < static_cast<float>(viewport.x) - margin
    || screen.x() > static_cast<float>(viewport.x + viewport.width) + margin
    || screen.y() < static_cast<float>(viewport.y) - margin
    || screen.y() > static_cast<float>(viewport.y + viewport.height) + margin)
  {
    return std::nullopt;
  }

  // Camera viewport coordinates are logical pixels. Derive both axes from the
  // actual projection (including perspective FOV/aspect), without applying DPI twice.
  const auto right =
    camera.right() / vm::length((camera.project(center + camera.right()) - screen).xy());
  const auto up =
    camera.up() / vm::length((camera.project(center + camera.up()) - screen).xy());
  auto result = EntityDirectionIndicator{};
  const auto worldPoint = [&](const vm::vec2f& p) {
    return center + right * p.x() + up * p.y();
  };
  const auto triangle =
    [&](const vm::vec2f& a, const vm::vec2f& b, const vm::vec2f& c, auto& vertices) {
      vertices.push_back(worldPoint(a));
      vertices.push_back(worldPoint(b));
      vertices.push_back(worldPoint(c));
    };
  const auto polygonTriangles = [&](const Polygon& polygon, auto& vertices) {
    for (size_t i = 1; i + 1 < polygon.size(); ++i)
    {
      triangle(polygon[0], polygon[i], polygon[i + 1], vertices);
    }
  };
  const auto ring = [&](const Polygon& inner, const Polygon& outer, auto& polygons) {
    for (size_t i = 0; i < inner.size(); ++i)
    {
      const auto j = (i + 1) % inner.size();
      triangle(inner[i], outer[i], outer[j], polygons);
      triangle(inner[i], outer[j], inner[j], polygons);
    }
  };
  const auto outlined = [&](const Polygon& polygon) {
    polygonTriangles(polygon, result.fill);
    ring(polygon, expanded(polygon, 1.25f), result.outline);
  };

  const auto viewDirection = camera.perspectiveProjection()
                               ? vm::normalize(center - camera.position())
                               : camera.direction();
  const auto facing = vm::dot(direction, viewDirection);
  if (std::abs(facing) > 0.995f)
  {
    result.fill.reserve(216);
    result.outline.reserve(432);
    result.kind = facing < 0.0f ? EntityDirectionIndicatorKind::TowardsViewer
                                : EntityDirectionIndicatorKind::AwayFromViewer;
    const auto inner = circle(5.0f);
    const auto outer = circle(6.5f);
    ring(inner, outer, result.fill);
    ring(circle(3.75f), inner, result.outline);
    ring(outer, circle(7.75f), result.outline);
    if (facing < 0.0f)
    {
      outlined(circle(1.75f));
    }
    else
    {
      // A compact cross distinguishes an arrow pointing into the view.
      const auto cross = Polygon{
        {-2.5f, -1.25f},
        {-1.25f, -2.5f},
        {0, -1.25f},
        {1.25f, -2.5f},
        {2.5f, -1.25f},
        {1.25f, 0},
        {2.5f, 1.25f},
        {1.25f, 2.5f},
        {0, 1.25f},
        {-1.25f, 2.5f},
        {-2.5f, 1.25f},
        {-1.25f, 0}};
      // The cross is star-shaped around its center; submit convex triangles.
      for (size_t i = 0; i < cross.size(); ++i)
      {
        triangle({0, 0}, cross[i], cross[(i + 1) % cross.size()], result.fill);
      }
      ring(cross, expanded(cross, 0.6f), result.outline);
    }
    return result;
  }

  result.kind = EntityDirectionIndicatorKind::Arrow;
  result.fill.reserve(9);
  result.outline.reserve(42);
  // A short step cannot cross the perspective eye plane, even at close range.
  const auto step = camera.perspectiveProjection() ? std::min(1.0f, depth * 0.25f) : 1.0f;
  const auto projected = (camera.project(center + direction * step) - screen).xy() / step;
  const auto projectedLength = vm::length(projected);
  const auto axis = projected / projectedLength;
  const auto side = vm::vec2f{-axis.y(), axis.x()};
  const auto length = std::clamp(32.0f * projectedLength, 24.0f, 40.0f);
  const auto point = [&](const float x, const float y) { return axis * x + side * y; };
  const auto silhouette = Polygon{
    point(0, -1),
    point(length - 8, -1),
    point(length - 8, -5),
    point(length, 0),
    point(length - 8, 5),
    point(length - 8, 1),
    point(0, 1)};
  triangle(silhouette[0], silhouette[1], silhouette[5], result.fill);
  triangle(silhouette[0], silhouette[5], silhouette[6], result.fill);
  triangle(silhouette[2], silhouette[3], silhouette[4], result.fill);
  ring(silhouette, expanded(silhouette, 1.25f), result.outline);
  return result;
}

} // namespace tb::render
