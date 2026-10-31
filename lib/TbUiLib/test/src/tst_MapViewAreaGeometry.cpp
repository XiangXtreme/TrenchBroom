/*
 Copyright (C) 2026 XiangXtreme
 SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ui/MapViewAreaGeometry.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui::areaGeometry
{
TEST_CASE("MapViewAreaGeometry")
{
  const auto size = QSizeF{1000, 800};
  const auto three =
    Rects{{0, {0, 0, 0.5, 1}}, {1, {0.5, 0, 0.5, 0.5}}, {2, {0.5, 0.5, 0.5, 0.5}}};
  SECTION("aligned joins keep the source identity")
  {
    const auto plan = join(three, 2, 1, size);
    REQUIRE(plan.valid);
    CHECK(plan.rects.size() == 2);
    CHECK(plan.rects.at(2) == QRectF{0.5, 0, 0.5, 1});
    CHECK_FALSE(plan.rects.contains(1));
    CHECK(plan.rects.at(0) == three.at(0));
  }
  SECTION("target overhangs remain as copies of the target")
  {
    const auto plan = join(three, 1, 0, size);
    REQUIRE(plan.valid);
    CHECK(plan.rects.size() == 3);
    CHECK(plan.rects.at(1) == QRectF{0, 0, 1, 0.5});
    CHECK(plan.rects.at(2) == three.at(2));
    REQUIRE(plan.copies.size() == 1);
    const auto copy = plan.copies.begin()->first;
    CHECK(plan.copies.at(copy) == 0);
    CHECK(plan.rects.at(copy) == QRectF{0, 0.5, 0.5, 0.5});
    CHECK(plan.closed.empty());
  }
  SECTION("source overhangs close into the best other neighbor")
  {
    const auto plan = join(three, 0, 1, size);
    REQUIRE(plan.valid);
    CHECK(plan.rects.size() == 2);
    CHECK(plan.rects.at(0) == QRectF{0, 0, 1, 0.5});
    CHECK(plan.rects.at(2) == QRectF{0, 0.5, 1, 0.5});
    CHECK_FALSE(plan.closed.empty());
  }
  SECTION("small offsets align neighboring edges without gaps")
  {
    const auto rects = Rects{
      {0, {0, 0, 0.5, 0.5}},
      {1, {0, 0.5, 0.5, 0.5}},
      {2, {0.5, 0, 0.5, 0.51}},
      {3, {0.5, 0.51, 0.5, 0.49}}};
    const auto plan = join(rects, 0, 2, size);
    REQUIRE(plan.valid);
    CHECK(plan.rects.size() == 3);
    CHECK(plan.rects.at(0) == QRectF{0, 0, 1, 0.505});
    CHECK(plan.rects.at(1).top() == 0.505);
    CHECK(plan.rects.at(3).top() == 0.505);
  }
  SECTION("an intervening narrow region prevents edge alignment")
  {
    const auto rects = Rects{
      {0, {0, 0, 0.5, 0.5}},
      {1, {0, 0.5, 0.5, 0.5}},
      {2, {0.5, 0, 0.5, 0.51}},
      {3, {0.5, 0.51, 0.5, 0.02}},
      {4, {0.5, 0.53, 0.5, 0.47}}};
    CHECK_FALSE(join(rects, 0, 2, size).valid);
  }
  SECTION("diagonal areas cannot join but can dock")
  {
    const auto rects = Rects{
      {0, {0, 0, 0.5, 0.5}},
      {1, {0, 0.5, 0.5, 0.5}},
      {2, {0.5, 0, 0.5, 0.5}},
      {3, {0.5, 0.5, 0.5, 0.5}}};
    CHECK_FALSE(join(rects, 0, 3, size).valid);
    const auto plan = dock(rects, 0, 3, Dock::Center, 0.5, size);
    REQUIRE(plan.valid);
    CHECK(plan.rects.at(0) == rects.at(3));
    CHECK_FALSE(plan.rects.contains(3));
    CHECK(plan.rects.size() == 3);
    CHECK_FALSE(plan.closed.empty());
    const auto side = dock(rects, 0, 3, Dock::Right, 0.5, size);
    REQUIRE(side.valid);
    CHECK(side.rects.at(0) == QRectF{0.75, 0.5, 0.25, 0.5});
    CHECK(side.rects.at(3) == QRectF{0.5, 0.5, 0.25, 0.5});
  }
  SECTION("docking on a common edge expands the source and retains the target remainder")
  {
    const auto rects = Rects{{0, {0, 0, 0.5, 1}}, {1, {0.5, 0, 0.5, 1}}};
    const auto plan = dock(rects, 0, 1, Dock::Left, 0.5, size);
    REQUIRE(plan.valid);
    CHECK(plan.rects.size() == 2);
    CHECK(plan.rects.at(0) == QRectF{0, 0, 0.75, 1});
    CHECK(plan.rects.at(1) == QRectF{0.75, 0, 0.25, 1});
  }
  SECTION("common edge band joins, deep center replaces and sides dock")
  {
    auto fraction = 0.5;
    const auto a = QRectF{0, 0, 0.5, 1};
    const auto b = QRectF{0.5, 0, 0.5, 1};
    CHECK(dockingTarget(a, b, {0.51, 0.5}, size, false, false, fraction) == Dock::None);
    CHECK(dockingTarget(a, b, {0.75, 0.5}, size, false, false, fraction) == Dock::Center);
    CHECK(dockingTarget(a, b, {0.75, 0.1}, size, false, false, fraction) == Dock::Top);
    CHECK(fraction == 0.2);
    CHECK(dockingTarget(a, b, {0.75, 0.2}, size, false, true, fraction) == Dock::Top);
    CHECK(fraction == 0.5);
  }
  SECTION("non slicing mosaics are valid and invalid restored layouts are rejected")
  {
    const auto pinwheel = Rects{
      {0, {0, 0, 0.7, 0.3}},
      {1, {0.7, 0, 0.3, 0.7}},
      {2, {0.3, 0.7, 0.7, 0.3}},
      {3, {0, 0.3, 0.3, 0.7}},
      {4, {0.3, 0.3, 0.4, 0.4}}};
    CHECK(validTiling(pinwheel));
    CHECK(join(pinwheel, 4, 0, size).valid);
    CHECK_FALSE(validTiling({{0, {0, 0, 0.5, 1}}}));
    CHECK_FALSE(validTiling({{0, {0, 0, 1, 1}}, {1, {0, 0, 1, 1}}}));
    CHECK_FALSE(join(three, 0, 1, {}).valid);
    CHECK_FALSE(dock(three, 0, 1, Dock::Left, 0.5, {}).valid);
    CHECK_FALSE(dock(three, 0, 1, Dock::Left, 1, size).valid);
  }
}
} // namespace tb::ui::areaGeometry
