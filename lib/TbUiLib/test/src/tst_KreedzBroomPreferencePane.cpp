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

#include <QCheckBox>
#include <QComboBox>
#include <QKeyEvent>
#include <QtTest/QTest>

#include "base/PreferenceManager.h"
#include "prefs/Preferences.h"
#include "ui/AppControllerFixture.h"
#include "ui/CatchConfig.h"
#include "ui/KreedzBroomPreferencePane.h"

#include <algorithm>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("KreedzBroomPreferencePane")
{
  auto fixture = AppControllerFixture{};
  setPref(Preferences::PlayModeShowJumpSpan, false);
  setPref(Preferences::PlayModeKeyHook, Qt::Key_C);
  setPref(Preferences::PlayModeKeyNoclip, Qt::Key_Z);
  auto pane = KreedzBroomPreferencePane{};
  auto& base = static_cast<PreferencePane&>(pane);
  base.updateControls();
  const auto buttons = pane.findChildren<QPushButton*>();
  const auto keyCount = [&](const int code) {
    return std::ranges::count_if(buttons, [code](const auto* button) {
      const auto* key = dynamic_cast<const PlayModeKeyButton*>(button);
      return key && key->key() == code;
    });
  };
  CHECK(keyCount(Qt::Key_C) == 1);
  CHECK(keyCount(Qt::Key_Z) == 1);
  auto* span = static_cast<QCheckBox*>(nullptr);
  for (auto* box : pane.findChildren<QCheckBox*>())
  {
    if (box->text() == "Show jump guides")
      span = box;
  }
  REQUIRE(span);
  CHECK_FALSE(span->isChecked());
  span->click();
  CHECK(pref(Preferences::PlayModeShowJumpSpan));
  base.resetToDefaults();
  CHECK_FALSE(pref(Preferences::PlayModeShowJumpSpan));
  CHECK_FALSE(span->isChecked());

  auto* results = static_cast<QCheckBox*>(nullptr);
  auto* failures = static_cast<QCheckBox*>(nullptr);
  for (auto* box : pane.findChildren<QCheckBox*>())
  {
    if (box->text() == "Show jump results")
      results = box;
    if (box->text() == "Show failed jumps")
      failures = box;
  }
  auto* strafes = static_cast<QCheckBox*>(nullptr);
  for (auto* box : pane.findChildren<QCheckBox*>())
    if (box->text() == "Show strafe details")
      strafes = box;
  REQUIRE(strafes);
  CHECK(strafes->isChecked());
  strafes->click();
  CHECK_FALSE(pref(Preferences::PlayModeShowStrafes));
  REQUIRE(results);
  REQUIRE(failures);
  CHECK(results->isChecked());
  CHECK(failures->isChecked());

  SECTION("legacytrim selects hltools and switching compiler restores legacy")
  {
    QComboBox* compiler = nullptr;
    QComboBox* cliptype = nullptr;
    for (auto* field : pane.findChildren<QComboBox*>())
    {
      if (field->findText("hltools") >= 0)
        compiler = field;
      if (field->findText("Legacytrim (hltools)") >= 0)
        cliptype = field;
    }
    REQUIRE(compiler);
    REQUIRE(cliptype);
    cliptype->setCurrentIndex(
      cliptype->findData(Preferences::PlayModeClipTypeLegacytrim));
    CHECK(pref(Preferences::PlayModeCompiler) == Preferences::PlayModeCompilerHltools);
    CHECK(pref(Preferences::PlayModeClipType) == Preferences::PlayModeClipTypeLegacytrim);
    compiler->setCurrentIndex(compiler->findData(Preferences::PlayModeCompilerZhlt));
    CHECK(pref(Preferences::PlayModeClipType) == Preferences::PlayModeClipTypeLegacy);
  }
  results->click();
  CHECK_FALSE(pref(Preferences::PlayModeShowJumpStats));
  CHECK(pref(Preferences::PlayModeShowFailedJumps));
  failures->click();
  CHECK_FALSE(pref(Preferences::PlayModeShowFailedJumps));
  base.resetToDefaults();
  CHECK(results->isChecked());
  CHECK(failures->isChecked());
}

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
