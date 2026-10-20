/*
 Copyright (C) 2026 TrenchBroom contributors

 This file is part of TrenchBroom, licensed under the GNU General Public License,
 version 3 or (at your option) any later version. See <http://www.gnu.org/licenses/>.
 */

#include "gl/OrthographicCamera.h"
#include "gl/PerspectiveCamera.h"
#include "render/Frustum.h"

#include "vm/mat_ext.h"

#include <limits>

#include <catch2/catch_test_macros.hpp>

namespace tb::render
{
TEST_CASE("Frustum")
{
  auto camera = gl::PerspectiveCamera{
    90, 1, 1000, {0, 0, 1000, 1000}, {0, 0, 0}, {1, 0, 0}, {0, 0, 1}};
  const auto frustum = Frustum{camera};

  SECTION("retains spheres crossing any clip plane")
  {
    CHECK(frustum.intersects({{10, 0, 0}, 1}));
    CHECK(frustum.intersects({{0, 0, 0}, 5}));
    CHECK(frustum.intersects({{1001, 0, 0}, 5}));
    for (const auto offset : {vm::vec3f{0, 12, 0}, {0, -12, 0}, {0, 0, 12}, {0, 0, -12}})
    {
      CHECK(frustum.intersects({vm::vec3f{10, 0, 0} + offset, 5}));
    }
  }

  SECTION("rejects spheres fully outside each clip plane")
  {
    for (const auto center :
         {vm::vec3f{-20, 0, 0},
          {1020, 0, 0},
          {10, 30, 0},
          {10, -30, 0},
          {10, 0, 30},
          {10, 0, -30}})
    {
      CHECK_FALSE(frustum.intersects({center, 5}));
    }
  }

  SECTION("follows camera movement rotation and zoom")
  {
    camera.moveTo({100, 200, 300});
    camera.setDirection({0, 1, 0}, {0, 0, 1});
    CHECK(Frustum{camera}.intersects({{100, 210, 300}, 1}));
    CHECK_FALSE(Frustum{camera}.intersects({{110, 200, 300}, 1}));
    CHECK(Frustum{camera}.intersects({{170, 300, 300}, 1}));
    camera.setZoom(0.5f);
    CHECK_FALSE(Frustum{camera}.intersects({{170, 300, 300}, 1}));
    camera.setZoom(2);
    CHECK(Frustum{camera}.intersects({{250, 300, 300}, 1}));
  }

  SECTION("retains points inside the actual projection across aspect FOV and zoom")
  {
    for (const auto viewport : {gl::Camera::Viewport{0, 0, 1600, 600}, {0, 0, 600, 1600}})
    {
      camera.setViewport(viewport);
      for (const auto fov : {60.0f, 90.0f, 100.0f})
      {
        camera.setFov(fov);
        for (const auto zoom : {0.5f, 1.0f, 2.0f})
        {
          camera.setZoom(zoom);
          const auto projection = camera.projectionMatrix() * camera.viewMatrix();
          for (const auto depth : {2.0f, 100.0f, 900.0f})
          {
            for (const auto y : {-1200.0f, -100.0f, 0.0f, 100.0f, 1200.0f})
            {
              for (const auto z : {-1200.0f, -100.0f, 0.0f, 100.0f, 1200.0f})
              {
                const auto point = vm::vec3f{depth, y, z};
                const auto clip = projection * vm::vec4f{point, 1.0f};
                if (std::abs(clip.x()) <= clip.w() && std::abs(clip.y()) <= clip.w())
                {
                  CHECK(Frustum{camera}.intersects({point, 0}));
                }
              }
            }
          }
        }
      }
    }
  }

  SECTION("supports orthographic views")
  {
    const auto ortho =
      gl::OrthographicCamera{1, 1000, {0, 0, 100, 100}, {0, 0, 0}, {1, 0, 0}, {0, 0, 1}};
    CHECK(Frustum{ortho}.intersects({{10, 0, 0}, 5}));
    CHECK(Frustum{ortho}.intersects({{10, 54, 0}, 5}));
    CHECK_FALSE(Frustum{ortho}.intersects({{10, 70, 0}, 5}));
  }

  SECTION("non-finite bounds are retained")
  {
    CHECK(frustum.intersects({{10, 0, 0}, std::numeric_limits<float>::quiet_NaN()}));
    CHECK(frustum.intersects({{10, 0, 0}, std::numeric_limits<float>::infinity()}));
    CHECK(frustum.intersects({{std::numeric_limits<float>::infinity(), 0, 0}, 1}));
  }
}

TEST_CASE("modelSphere")
{
  const auto bounds = vm::bbox3f{{-2, -8, -1}, {5, 3, 4}};
  auto transform = vm::translation_matrix(vm::vec3f{10, 20, 30});
  transform[0] = vm::vec4f{-2, 0, 0, 0};
  transform[1] = vm::vec4f{1, 3, 0, 0}; // nonuniform scale plus shear
  transform[2] = vm::vec4f{0, 0, 4, 0};
  const auto sphere = modelSphere(bounds, transform);
  CHECK(sphere.center == vm::vec3f{10, 20, 30});
  for (const auto x : {bounds.min.x(), bounds.max.x()})
  {
    for (const auto y : {bounds.min.y(), bounds.max.y()})
    {
      for (const auto z : {bounds.min.z(), bounds.max.z()})
      {
        const auto point = vm::vec3f{x, y, z};
        CHECK(vm::length(transform * point - sphere.center) <= sphere.radius);
        // A camera-facing sprite replaces rotation/shear with column scales.
        const auto scaled = vm::vec3f{
          x * vm::length(vm::vec3f{transform[0]}),
          y * vm::length(vm::vec3f{transform[1]}),
          z * vm::length(vm::vec3f{transform[2]})};
        CHECK(vm::length(scaled) <= sphere.radius);
      }
    }
  }
  auto invalidBounds = bounds;
  invalidBounds.max[1] = std::numeric_limits<float>::quiet_NaN();
  CHECK(std::isinf(modelSphere(invalidBounds, transform).radius));
}
} // namespace tb::render
