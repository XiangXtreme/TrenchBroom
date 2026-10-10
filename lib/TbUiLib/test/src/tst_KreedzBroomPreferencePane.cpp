/*
 Copyright (C) 2026 KreedzBroom contributors

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

#include <QKeyEvent>
#include <QtTest/QTest>

#include "ui/CatchConfig.h"
#include "ui/KreedzBroomPreferencePane.h"

#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("playModeKeyName")
{
  CHECK(playModeKeyName(Qt::Key_Control) == "Ctrl");
  CHECK(playModeKeyName(Qt::Key_Shift) == "Shift");
  CHECK(playModeKeyName(Qt::Key_Space) == "Space");
  CHECK(playModeKeyName(Qt::Key_W) == "W");
  CHECK(playModeKeyName(0) == "(none)");
}

TEST_CASE("PlayModeKeyButton")
{
  auto recorded = std::vector<int>{};
  auto button = PlayModeKeyButton{[&](const int key) { recorded.push_back(key); }};
  button.setKey(Qt::Key_Space);
  REQUIRE(button.text() == "Space");

  SECTION("records a bare modifier after being clicked")
  {
    button.click();
    CHECK(button.text() == "Press a key...");

    QTest::keyPress(&button, Qt::Key_Control);
    CHECK(recorded == std::vector<int>{Qt::Key_Control});
    CHECK(button.key() == Qt::Key_Control);
    CHECK(button.text() == "Ctrl");
  }

  SECTION("Escape cancels recording")
  {
    button.click();
    QTest::keyPress(&button, Qt::Key_Escape);
    CHECK(recorded.empty());
    CHECK(button.key() == Qt::Key_Space);
    CHECK(button.text() == "Space");
  }

  SECTION("keys are only recorded while recording")
  {
    QTest::keyPress(&button, Qt::Key_W);
    CHECK(recorded.empty());
    CHECK(button.key() == Qt::Key_Space);
  }

  SECTION("auto repeat does not end recording")
  {
    button.click();
    auto repeat = QKeyEvent{QEvent::KeyPress, Qt::Key_W, Qt::NoModifier, "w", true};
    QCoreApplication::sendEvent(&button, &repeat);
    CHECK(recorded.empty());
    CHECK(button.text() == "Press a key...");
  }
}

} // namespace tb::ui
