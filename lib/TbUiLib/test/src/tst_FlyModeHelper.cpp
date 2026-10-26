/*
 Copyright (C) 2026 Kristian Duske

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

#include "base/PreferenceManager.h"
#include "gl/PerspectiveCamera.h"
#include "prefs/Preferences.h"
#include "ui/CatchConfig.h"
#include "ui/FlyModeHelper.h"

#include <chrono>
#include <thread>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("FlyModeHelper")
{
  auto camera = gl::PerspectiveCamera{};
  camera.moveTo(vm::vec3f{0, 0, 0});
  camera.lookAt(vm::vec3f{0, 1, 0}, vm::vec3f{0, 0, 1});

  auto helper = FlyModeHelper{camera};

  SECTION("matchesFlyKey matches standard fly keys with and without modifiers")
  {
    auto forwardNoMod = QKeyEvent{QEvent::KeyPress, Qt::Key_W, Qt::NoModifier};
    auto backwardNoMod = QKeyEvent{QEvent::KeyPress, Qt::Key_S, Qt::NoModifier};
    auto leftNoMod = QKeyEvent{QEvent::KeyPress, Qt::Key_A, Qt::NoModifier};
    auto rightNoMod = QKeyEvent{QEvent::KeyPress, Qt::Key_D, Qt::NoModifier};
    auto upNoMod = QKeyEvent{QEvent::KeyPress, Qt::Key_Q, Qt::NoModifier};
    auto downNoMod = QKeyEvent{QEvent::KeyPress, Qt::Key_X, Qt::NoModifier};

    CHECK(helper.matchesFlyKey(forwardNoMod));
    CHECK(helper.matchesFlyKey(backwardNoMod));
    CHECK(helper.matchesFlyKey(leftNoMod));
    CHECK(helper.matchesFlyKey(rightNoMod));
    CHECK(helper.matchesFlyKey(upNoMod));
    CHECK(helper.matchesFlyKey(downNoMod));

    auto forwardShift = QKeyEvent{QEvent::KeyPress, Qt::Key_W, Qt::ShiftModifier};
    auto backwardShift = QKeyEvent{QEvent::KeyPress, Qt::Key_S, Qt::ShiftModifier};
    auto leftShift = QKeyEvent{QEvent::KeyPress, Qt::Key_A, Qt::ShiftModifier};
    auto rightShift = QKeyEvent{QEvent::KeyPress, Qt::Key_D, Qt::ShiftModifier};

    CHECK(helper.matchesFlyKey(forwardShift));
    CHECK(helper.matchesFlyKey(backwardShift));
    CHECK(helper.matchesFlyKey(leftShift));
    CHECK(helper.matchesFlyKey(rightShift));

    auto nonFlyKeyB = QKeyEvent{QEvent::KeyPress, Qt::Key_B, Qt::ShiftModifier};
    auto nonFlyKeyR = QKeyEvent{QEvent::KeyPress, Qt::Key_R, Qt::NoModifier};
    auto nonFlyKeySpace = QKeyEvent{QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier};

    CHECK_FALSE(helper.matchesFlyKey(nonFlyKeyB));
    CHECK_FALSE(helper.matchesFlyKey(nonFlyKeyR));
    CHECK_FALSE(helper.matchesFlyKey(nonFlyKeySpace));
  }

  SECTION("Shift+S fly backward movement")
  {
    CHECK_FALSE(helper.anyKeyDown());

    auto shiftPress = QKeyEvent{QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier};
    helper.keyDown(&shiftPress);
    CHECK_FALSE(helper.anyKeyDown());

    auto sWithShiftPress = QKeyEvent{QEvent::KeyPress, Qt::Key_S, Qt::ShiftModifier};
    helper.keyDown(&sWithShiftPress);
    CHECK(helper.anyKeyDown());

    const auto initialPos = camera.position();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    helper.pollAndUpdate();
    // Camera is looking toward +Y, so flying backward moves in -Y
    CHECK(camera.position().y() < initialPos.y());

    auto sRelease = QKeyEvent{QEvent::KeyRelease, Qt::Key_S, Qt::ShiftModifier};
    helper.keyUp(&sRelease);
    CHECK_FALSE(helper.anyKeyDown());

    auto shiftRelease = QKeyEvent{QEvent::KeyRelease, Qt::Key_Shift, Qt::NoModifier};
    helper.keyUp(&shiftRelease);
    CHECK_FALSE(helper.anyKeyDown());
  }

  SECTION("Simultaneous Shift+S keyDown sets fast backward movement")
  {
    helper.resetKeys();
    auto sWithShiftPress = QKeyEvent{QEvent::KeyPress, Qt::Key_S, Qt::ShiftModifier};
    helper.keyDown(&sWithShiftPress);
    CHECK(helper.anyKeyDown());

    const auto initialPos = camera.position();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    helper.pollAndUpdate();
    CHECK(camera.position().y() < initialPos.y());

    auto sRelease = QKeyEvent{QEvent::KeyRelease, Qt::Key_S, Qt::ShiftModifier};
    helper.keyUp(&sRelease);
    CHECK_FALSE(helper.anyKeyDown());
  }
}

} // namespace tb::ui
