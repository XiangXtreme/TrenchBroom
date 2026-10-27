/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>

#include "kd/result.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushNode.h"
#include "mdl/MapFormat.h"
#include "mdl/ModelUtils.h"
#include "ui/SmartFaceSelectionPanel.h"

#include "vm/bbox.h"
#include "vm/vec.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("SmartFaceSelectionPanel")
{
  auto changeCount = 0;
  auto confirmCount = 0;
  auto cancelCount = 0;
  auto panel = SmartFaceSelectionPanel{
    {}, [&] { ++changeCount; }, [&] { ++confirmCount; }, [&] { ++cancelCount; }};

  CHECK(panel.options().mode == mdl::SmartFaceSelectionMode::FaceStrip);
  CHECK(panel.options().angleTolerance == 15.0);
  CHECK(panel.options().gapTolerance == 0.0);
  CHECK_FALSE(panel.options().followSeedDirection);
  CHECK_FALSE(panel.options().stopAtBranches);
  CHECK_FALSE(panel.options().sameMaterial);
  CHECK(panel.operation() == SmartFaceSelectionOperation::Replace);
  CHECK(panel.expanded());
  CHECK(changeCount == 0);
  CHECK(confirmCount == 0);
  CHECK(cancelCount == 0);

  SECTION("Controls update options and operation")
  {
    auto* mode = panel.findChild<QComboBox*>(QStringLiteral("smartFaceSelectionMode"));
    auto* operation =
      panel.findChild<QComboBox*>(QStringLiteral("smartFaceSelectionOperation"));
    auto* angle =
      panel.findChild<QDoubleSpinBox*>(QStringLiteral("smartFaceSelectionAngle"));
    auto* gap = panel.findChild<QDoubleSpinBox*>(QStringLiteral("smartFaceSelectionGap"));
    auto* followSeedDirection = panel.findChild<QCheckBox*>(
      QStringLiteral("smartFaceSelectionFollowSeedDirection"));
    auto* stopAtBranches =
      panel.findChild<QCheckBox*>(QStringLiteral("smartFaceSelectionStopAtBranches"));
    auto* sameMaterial =
      panel.findChild<QCheckBox*>(QStringLiteral("smartFaceSelectionSameMaterial"));
    REQUIRE(mode != nullptr);
    REQUIRE(operation != nullptr);
    REQUIRE(angle != nullptr);
    REQUIRE(gap != nullptr);
    REQUIRE(followSeedDirection != nullptr);
    REQUIRE(stopAtBranches != nullptr);
    REQUIRE(sameMaterial != nullptr);
    CHECK_FALSE(followSeedDirection->isEnabled());

    mode->setCurrentIndex(1);
    operation->setCurrentIndex(2);
    angle->setValue(22.5);
    gap->setValue(1.25);
    stopAtBranches->setChecked(true);
    sameMaterial->setChecked(true);

    CHECK(panel.options().mode == mdl::SmartFaceSelectionMode::Parallel);
    CHECK(panel.options().angleTolerance == 22.5);
    CHECK(panel.options().gapTolerance == 1.25);
    CHECK_FALSE(panel.options().followSeedDirection);
    CHECK(panel.options().stopAtBranches);
    CHECK(panel.options().sameMaterial);
    CHECK(panel.operation() == SmartFaceSelectionOperation::Remove);
    CHECK(changeCount == 6);
  }

  SECTION("Panel can be collapsed and expanded")
  {
    panel.setExpanded(false);
    CHECK_FALSE(panel.expanded());
    panel.setExpanded(true);
    CHECK(panel.expanded());
    CHECK(changeCount == 2);
  }

  SECTION("Following seed direction requires two seed faces and face strip mode")
  {
    constexpr auto worldBounds = vm::bbox3d{8192.0};
    const auto builder = mdl::BrushBuilder{mdl::MapFormat::Standard, worldBounds};
    auto firstNode = mdl::BrushNode{
      builder.createCuboid({{0, 0, 0}, {64, 64, 64}}, "material") | kdl::value()};
    auto secondNode = mdl::BrushNode{
      builder.createCuboid({{64, 0, 0}, {128, 64, 64}}, "material") | kdl::value()};
    const auto topFace = [](auto& node) {
      return mdl::BrushFaceHandle{node, *node.brush().findFace(vm::vec3d{0, 0, 1})};
    };

    auto directedPanel = SmartFaceSelectionPanel{
      {topFace(firstNode), topFace(secondNode)},
      [&] { ++changeCount; },
      [&] { ++confirmCount; },
      [&] { ++cancelCount; }};
    auto* followSeedDirection = directedPanel.findChild<QCheckBox*>(
      QStringLiteral("smartFaceSelectionFollowSeedDirection"));
    auto* mode =
      directedPanel.findChild<QComboBox*>(QStringLiteral("smartFaceSelectionMode"));
    REQUIRE(followSeedDirection != nullptr);
    REQUIRE(mode != nullptr);

    CHECK(followSeedDirection->isEnabled());
    followSeedDirection->setChecked(true);
    CHECK(directedPanel.options().followSeedDirection);

    mode->setCurrentIndex(1);
    CHECK_FALSE(followSeedDirection->isEnabled());
    CHECK_FALSE(directedPanel.options().followSeedDirection);

    mode->setCurrentIndex(0);
    CHECK(followSeedDirection->isEnabled());
    CHECK(directedPanel.options().followSeedDirection);
    CHECK(changeCount == 3);
  }

  SECTION("Apply and cancel buttons invoke their callbacks")
  {
    auto* apply =
      panel.findChild<QPushButton*>(QStringLiteral("smartFaceSelectionApply"));
    auto* cancel =
      panel.findChild<QPushButton*>(QStringLiteral("smartFaceSelectionCancel"));
    REQUIRE(apply != nullptr);
    REQUIRE(cancel != nullptr);

    apply->click();
    cancel->click();

    CHECK(confirmCount == 1);
    CHECK(cancelCount == 1);
  }
}

} // namespace tb::ui
