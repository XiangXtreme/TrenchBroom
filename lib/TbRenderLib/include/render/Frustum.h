/*
 Copyright (C) 2026 TrenchBroom contributors

 This file is part of TrenchBroom, licensed under the GNU General Public License,
 version 3 or (at your option) any later version. See <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "gl/Camera.h"

#include "vm/bbox.h"
#include "vm/mat.h"
#include "vm/plane.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace tb::render
{

/** Conservative world-space sphere, including camera-facing sprite orientations. */
struct ModelSphere
{
  vm::vec3f center;
  float radius;
};

inline ModelSphere modelSphere(const vm::bbox3f& bounds, const vm::mat4x4f& transform)
{
  auto radius = 0.0f;
  for (size_t i = 0; i < 3; ++i)
  {
    if (!std::isfinite(bounds.min[i]) || !std::isfinite(bounds.max[i]))
    {
      return {vm::vec3f{transform[3]}, std::numeric_limits<float>::infinity()};
    }
    // Match the column lengths used by EntityModel.vertsh's getScaleMatrix().
    // The triangle bound also covers shears, reflections, offset sprite origins,
    // and upright billboard bases which may not be orthogonal for a rolled camera.
    radius += std::max(std::abs(bounds.min[i]), std::abs(bounds.max[i]))
              * vm::length(vm::vec3f{transform[i]});
  }
  return {vm::vec3f{transform[3]}, radius};
}

class Frustum
{
  std::array<vm::plane3f, 6> m_planes;

public:
  explicit Frustum(const gl::Camera& camera)
  {
    camera.frustumPlanes(m_planes[0], m_planes[1], m_planes[2], m_planes[3]);
    m_planes[4] = vm::plane3f{
      camera.position() + camera.direction() * camera.nearPlane(), -camera.direction()};
    m_planes[5] = vm::plane3f{
      camera.position() + camera.direction() * camera.farPlane(), camera.direction()};
  }

  bool intersects(const ModelSphere& sphere) const
  {
    if (
      !std::isfinite(sphere.radius) || !std::isfinite(sphere.center.x())
      || !std::isfinite(sphere.center.y()) || !std::isfinite(sphere.center.z()))
    {
      return true;
    }
    // Keep a small world-space margin to avoid edge popping due to float rounding.
    // Non-finite bounds deliberately fail open instead of hiding an entity.
    return std::ranges::none_of(m_planes, [&](const auto& plane) {
      return vm::dot(plane.normal, sphere.center) - plane.distance > sphere.radius + 1.0f;
    });
  }
};

} // namespace tb::render
