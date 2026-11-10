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

#include "gl/OrthographicCamera.h"
#include "gl/PerspectiveCamera.h"
#include "render/EntityDirectionIndicator.h"

#include <algorithm>
#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace tb::render
{
namespace
{

vm::vec2f screenTip(
  const gl::Camera& camera,
  const EntityDirectionIndicator& indicator,
  const vm::vec3f& center)
{
  auto tip = vm::vec2f{};
  for (const auto& vertex : indicator.fill)
  {
    const auto delta = (camera.project(vertex) - camera.project(center)).xy();
    if (vm::squared_length(delta) > vm::squared_length(tip))
    {
      tip = delta;
    }
    CHECK(std::isfinite(delta.x()));
    CHECK(std::isfinite(delta.y()));
  }
  return tip;
}

float screenLength(
  const gl::Camera& camera,
  const EntityDirectionIndicator& indicator,
  const vm::vec3f& center)
{
  return vm::length(screenTip(camera, indicator, center));
}

} // namespace

TEST_CASE("EntityDirectionIndicator")
{
  auto camera = gl::OrthographicCamera{
    1, 8192, {0, 0, 800, 600}, {0, 0, 4096}, {0, 0, -1}, {0, 1, 0}};
  const auto center = vm::vec3f{0, 0, 0};

  SECTION("orthographic arrows remain readable at both zoom extremes")
  {
    for (const auto zoom : {0.02f, 0.25f, 1.0f, 4.0f, 100.0f})
    {
      camera.setZoom(zoom);
      const auto indicator = makeEntityDirectionIndicator(camera, center, {1, 0, 0});
      REQUIRE(indicator);
      CHECK(indicator->kind == EntityDirectionIndicatorKind::Arrow);
      const auto length = screenLength(camera, *indicator, center);
      CHECK(length >= 23.9f);
      CHECK(length <= 40.1f);
      CHECK(screenTip(camera, *indicator, center).x() > 0);
    }
  }

  SECTION("normal directions have distinct dot and cross markers in every 2D plane")
  {
    for (const auto normal :
         {vm::vec3f{0, 0, -1}, vm::vec3f{0, 1, 0}, vm::vec3f{1, 0, 0}})
    {
      camera.setDirection(
        normal, normal.z() != 0 ? vm::vec3f{0, 1, 0} : vm::vec3f{0, 0, 1});
      camera.moveTo(-normal * 4096.0f);
      const auto away = makeEntityDirectionIndicator(camera, center, normal);
      const auto towards = makeEntityDirectionIndicator(camera, center, -normal);
      REQUIRE(away);
      REQUIRE(towards);
      CHECK(away->kind == EntityDirectionIndicatorKind::AwayFromViewer);
      CHECK(towards->kind == EntityDirectionIndicatorKind::TowardsViewer);
      CHECK(away->fill != towards->fill);
      CHECK(screenLength(camera, *away, center) == Catch::Approx(6.5f).margin(0.01f));
      CHECK(screenLength(camera, *towards, center) == Catch::Approx(6.5f).margin(0.01f));
    }
  }

  SECTION("parallel 2D views do not depend on camera lateral position")
  {
    const auto first = makeEntityDirectionIndicator(camera, center, {0, 0, -1});
    camera.moveTo({128, 64, 4096});
    const auto moved = makeEntityDirectionIndicator(camera, center, {0, 0, -1});
    REQUIRE(first);
    REQUIRE(moved);
    CHECK(first->kind == moved->kind);
    CHECK(screenLength(camera, *moved, center) == Catch::Approx(6.5f).margin(0.01f));
  }

  SECTION("perspective arrows retain direction and size beyond the old distance limit")
  {
    auto perspective = gl::PerspectiveCamera{
      90, 1, 32768, {0, 0, 800, 600}, {0, -128, 0}, {0, 1, 0}, {0, 0, 1}};
    for (const auto distance : {8.0f, 128.0f, 1024.0f, 8192.0f})
    {
      perspective.moveTo({0, -distance, 0});
      const auto indicator = makeEntityDirectionIndicator(perspective, center, {1, 0, 0});
      REQUIRE(indicator);
      CHECK(indicator->kind == EntityDirectionIndicatorKind::Arrow);
      const auto length = screenLength(perspective, *indicator, center);
      CHECK(length >= 23.9f);
      CHECK(length <= 40.1f);
      CHECK(screenTip(perspective, *indicator, center).x() > 0);
    }
  }

  SECTION("perspective head-on markers follow the entity sightline off center")
  {
    auto perspective = gl::PerspectiveCamera{
      90, 1, 32768, {0, 0, 800, 600}, {0, -256, 0}, {0, 1, 0}, {0, 0, 1}};
    const auto offsetCenter = vm::vec3f{128, 0, 0};
    const auto direction = vm::normalize(offsetCenter - perspective.position());
    const auto away = makeEntityDirectionIndicator(perspective, offsetCenter, direction);
    const auto towards =
      makeEntityDirectionIndicator(perspective, offsetCenter, -direction);
    REQUIRE(away);
    REQUIRE(towards);
    CHECK(away->kind == EntityDirectionIndicatorKind::AwayFromViewer);
    CHECK(towards->kind == EntityDirectionIndicatorKind::TowardsViewer);
  }

  SECTION("pitched arrows retain the projected orientation")
  {
    const auto direction = vm::normalize(vm::vec3f{-1, 1, 1});
    const auto indicator = makeEntityDirectionIndicator(camera, center, direction);
    REQUIRE(indicator);
    CHECK(indicator->kind == EntityDirectionIndicatorKind::Arrow);
    const auto tip = screenTip(camera, *indicator, center);
    CHECK(tip.x() < 0);
    CHECK(tip.y() > 0);
    CHECK(tip.x() == Catch::Approx(-tip.y()).margin(0.01f));
  }

  SECTION("invisible anchors do not create arrows at the edge or behind the camera")
  {
    CHECK_FALSE(makeEntityDirectionIndicator(camera, {10000, 0, 0}, {1, 0, 0}));
    auto perspective = gl::PerspectiveCamera{
      90, 1, 32768, {0, 0, 800, 600}, {0, -128, 0}, {0, 1, 0}, {0, 0, 1}};
    CHECK_FALSE(makeEntityDirectionIndicator(perspective, {0, -129, 0}, {1, 0, 0}));
    CHECK_FALSE(makeEntityDirectionIndicator(perspective, {0, -127.5f, 0}, {1, 0, 0}));
    perspective.setViewport({0, 0, 0, 0});
    CHECK_FALSE(makeEntityDirectionIndicator(perspective, center, {1, 0, 0}));
  }
}

} // namespace tb::render
