/*
 Copyright (C) 2026 TrenchBroom Authors

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

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLocale>
#include <QMimeData>
#include <QMouseEvent>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QToolButton>
#include <QWheelEvent>
#include <QtTest/QTest>

#include "mdl/BrushNode.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/Grid.h"
#include "mdl/Map.h"
#include "mdl/Map_Entities.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/TestFactory.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"
#include "ui/SpinControl.h"
#include "ui/TransformCard.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("TransformCard")
{
  auto fixture = MapDocumentFixture{};
  auto& document = fixture.create();
  auto& map = document.map();

  auto card = TransformCard{document};
  card.resize(320, 400);
  card.show();
  QApplication::processEvents();

  const auto addLight =
    [&](const std::string& origin = "10 20 30", const std::string& scale = "1") {
      auto* entity = new mdl::EntityNode{
        mdl::Entity{{{"classname", "light"}, {"origin", origin}, {"scale", scale}}}};
      mdl::addNodes(map, {{&mdl::parentForNodes(map), {entity}}});
      mdl::selectNodes(map, {entity});
      card.updateFromSelection();
      return entity;
    };
  const auto mouse = [](
                       QWidget* widget,
                       QEvent::Type type,
                       QPoint global,
                       Qt::MouseButton button,
                       Qt::MouseButtons buttons,
                       Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    auto event = QMouseEvent{
      type,
      QPointF{widget->mapFromGlobal(global)},
      QPointF{global},
      button,
      buttons,
      modifiers};
    QApplication::sendEvent(widget, &event);
  };
  const auto channel = [&](const char* name) {
    auto* spin = card.findChild<SpinControl*>(name);
    REQUIRE(spin != nullptr);
    return spin;
  };
  const auto mouseFocus = [&](SpinControl* spin) {
    // Activate only Qt's focus bookkeeping; the test window stays off screen.
    QT_WARNING_PUSH
    QT_WARNING_DISABLE_DEPRECATED
    QApplication::setActiveWindow(&card);
    QT_WARNING_POP
    spin->setFocus(Qt::MouseFocusReason);
    REQUIRE(spin->hasFocus());
  };

  SECTION("mouse focus before the press still allows vertical channel selection")
  {
    auto* entity = addLight();
    auto* x = channel("TransformCard_PosX");
    auto* y = channel("TransformCard_PosY");
    auto* z = channel("TransformCard_PosZ");
    auto* edit = x->findChild<QLineEdit*>();
    mouseFocus(x);
    const auto start = edit->mapToGlobal(edit->rect().center());
    const auto end = z->mapToGlobal(z->rect().center());
    mouse(edit, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    CHECK(QWidget::mouseGrabber() == edit);
    mouse(edit, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
    CHECK(x->property("multiSelected").toBool());
    CHECK(y->property("multiSelected").toBool());
    CHECK(z->property("multiSelected").toBool());
    mouse(edit, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
    CHECK(QWidget::mouseGrabber() != edit);
    CHECK(edit->selectedText() == x->cleanText());
    QTest::keyClicks(edit, "99");
    QTest::keyClick(edit, Qt::Key_Return);
    CHECK(entity->entity().origin() == vm::vec3d{99, 99, 99});
  }

  SECTION("first click selects the current number after mouse focus")
  {
    auto* entity = addLight("123.5 20 30");
    auto* x = channel("TransformCard_PosX");
    auto* edit = x->findChild<QLineEdit*>();
    mouseFocus(x);
    const auto start = edit->mapToGlobal(edit->rect().center());
    mouse(edit, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(edit, QEvent::MouseButtonRelease, start, Qt::LeftButton, Qt::NoButton);
    CHECK(edit->selectedText() == "123.5");
    QTest::keyClicks(edit, "7");
    QTest::keyClick(edit, Qt::Key_Return);
    CHECK(entity->entity().origin().x() == 7);
  }

  SECTION("vertical selection can shrink back to two channels")
  {
    auto* entity = addLight();
    auto* x = channel("TransformCard_PosX");
    auto* y = channel("TransformCard_PosY");
    auto* z = channel("TransformCard_PosZ");
    auto* edit = x->findChild<QLineEdit*>();
    mouseFocus(x);
    const auto start = edit->mapToGlobal(edit->rect().center());
    const auto end = y->mapToGlobal(y->rect().center());
    mouse(edit, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(
      edit,
      QEvent::MouseMove,
      z->mapToGlobal(z->rect().center()),
      Qt::NoButton,
      Qt::LeftButton);
    REQUIRE(z->property("multiSelected").toBool());
    mouse(edit, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
    CHECK(x->property("multiSelected").toBool());
    CHECK(y->property("multiSelected").toBool());
    CHECK_FALSE(z->property("multiSelected").toBool());
    mouse(edit, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
    QTest::keyClicks(edit, "99");
    QTest::keyClick(edit, Qt::Key_Return);
    CHECK(entity->entity().origin() == vm::vec3d{99, 99, 30});
  }

  SECTION("vertical selection also works upward and skips locked axes")
  {
    auto* entity = addLight();
    auto* x = channel("TransformCard_PosX");
    auto* y = channel("TransformCard_PosY");
    auto* z = channel("TransformCard_PosZ");
    card.findChild<QToolButton*>("TransformCard_LockPosY")->setChecked(true);
    mouseFocus(z);
    const auto start = z->mapToGlobal(z->rect().center());
    const auto end = x->mapToGlobal(x->rect().center());
    mouse(z, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(z, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
    CHECK(x->property("multiSelected").toBool());
    CHECK_FALSE(y->property("multiSelected").toBool());
    CHECK(z->property("multiSelected").toBool());
    mouse(z, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
    QTest::keyClicks(z->findChild<QLineEdit*>(), "55");
    QTest::keyClick(z, Qt::Key_Return);
    CHECK(entity->entity().origin() == vm::vec3d{55, 20, 55});
  }

  SECTION("keyboard focus selects the numeric part of an angle")
  {
    addLight();
    auto* x = channel("TransformCard_RotX");
    auto* edit = x->findChild<QLineEdit*>();
    mouseFocus(x);
    auto focus = QFocusEvent{QEvent::FocusIn, Qt::TabFocusReason};
    QApplication::sendEvent(x, &focus);
    CHECK(edit->selectedText() == "0");
    QTest::keyClicks(edit, "12");
    QTest::keyClick(edit, Qt::Key_Return);
    CHECK(card.currentRotation() == vm::vec3d{12, 0, 0});
  }

  SECTION("pressing enter on a focused idle channel starts editing")
  {
    auto* entity = addLight();
    auto* x = channel("TransformCard_PosX");
    auto* edit = x->findChild<QLineEdit*>();
    mouseFocus(x);
    auto finished = QSignalSpy{&card, &TransformCard::editingFinished};
    QTest::keyClick(x, Qt::Key_Return);
    CHECK(edit->selectedText() == "10");
    CHECK(finished.isEmpty());
    QTest::keyClicks(edit, "15");
    QTest::keyClick(edit, Qt::Key_Return);
    CHECK(entity->entity().origin().x() == 15);
    CHECK(finished.count() == 1);
  }

  SECTION("text selection survives a refresh before confirmation")
  {
    auto* entity = addLight();
    auto* x = channel("TransformCard_PosX");
    auto* edit = x->findChild<QLineEdit*>();
    mouseFocus(x);
    const auto start = edit->mapToGlobal(edit->rect().center());
    mouse(edit, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(edit, QEvent::MouseButtonRelease, start, Qt::LeftButton, Qt::NoButton);
    QTest::keyClicks(edit, "42");
    card.updateFromSelection();
    CHECK(edit->text() == "42");
    CHECK(entity->entity().origin().x() == 10);
    QTest::keyClick(edit, Qt::Key_Escape);
    CHECK(x->value() == 10);
  }

  SECTION("horizontal turn freezes the selected channels and preserves offsets")
  {
    auto* entity = addLight();
    auto* x = channel("TransformCard_PosX");
    auto* y = channel("TransformCard_PosY");
    auto* z = channel("TransformCard_PosZ");
    const auto start = x->mapToGlobal(x->rect().center());
    const auto end = y->mapToGlobal(y->rect().center());
    mouseFocus(x);
    mouse(x, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(x, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
    mouse(x, QEvent::MouseMove, end + QPoint{10, 0}, Qt::NoButton, Qt::LeftButton);
    const auto dragEnd = QPoint{end.x() + 30, z->mapToGlobal(z->rect().center()).y()};
    mouse(x, QEvent::MouseMove, dragEnd, Qt::NoButton, Qt::LeftButton);
    CHECK(entity->entity().origin() == vm::vec3d{30, 40, 30});
    CHECK_FALSE(z->property("multiSelected").toBool());
    mouse(x, QEvent::MouseButtonRelease, dragEnd, Qt::LeftButton, Qt::NoButton);
    map.undoCommand();
    CHECK(entity->entity().origin() == vm::vec3d{10, 20, 30});
  }

  SECTION("vertical gesture selects only fields intersected by the segment")
  {
    addLight();
    auto* x = channel("TransformCard_PosX");
    auto* y = channel("TransformCard_PosY");
    auto* z = channel("TransformCard_PosZ");
    const auto start = x->mapToGlobal(QPoint{x->width() - 1, x->height() / 2});
    const auto end = QPoint{start.x() + 10, z->mapToGlobal(z->rect().center()).y()};
    mouse(x, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(x, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
    CHECK(x->property("multiSelected").toBool());
    CHECK_FALSE(y->property("multiSelected").toBool());
    CHECK_FALSE(z->property("multiSelected").toBool());
    QTest::keyClick(x, Qt::Key_Escape);
  }

  SECTION("scale multi-drag preserves proportions")
  {
    auto* entity = addLight("10 20 30", "2 3 4");
    auto* x = channel("TransformCard_ScaleX");
    auto* z = channel("TransformCard_ScaleZ");
    const auto start = x->mapToGlobal(x->rect().center());
    const auto end = z->mapToGlobal(z->rect().center());
    mouse(x, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(x, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
    mouse(x, QEvent::MouseMove, end + QPoint{10, 0}, Qt::NoButton, Qt::LeftButton);
    mouse(x, QEvent::MouseMove, end + QPoint{30, 0}, Qt::NoButton, Qt::LeftButton);
    CHECK(card.currentScale() == vm::vec3d{2.1, 3.15, 4.2});
    mouse(
      x, QEvent::MouseButtonRelease, end + QPoint{30, 0}, Qt::LeftButton, Qt::NoButton);
    CHECK(*entity->entity().property("scale") == "2.1 3.15 4.2");
    map.undoCommand();
    CHECK(card.currentScale() == vm::vec3d{2, 3, 4});
  }

  SECTION("explicit scale text selection takes precedence over the scale chain")
  {
    addLight("10 20 30", "2 3 4");
    card.setUniformScale(true);
    auto* x = channel("TransformCard_ScaleX");
    auto* y = channel("TransformCard_ScaleY");
    const auto start = x->mapToGlobal(x->rect().center());
    const auto end = y->mapToGlobal(y->rect().center());
    mouse(x, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(x, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
    mouse(x, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
    QTest::keyClicks(x->findChild<QLineEdit*>(), "10");
    QTest::keyClick(x, Qt::Key_Return);
    CHECK(card.currentScale() == vm::vec3d{10, 10, 4});
  }

  SECTION("snap modifiers update the primary value and retain channel offsets")
  {
    auto* entity = addLight();
    map.grid().setSize(3);
    REQUIRE(map.grid().actualSize() == 8);
    auto* x = channel("TransformCard_PosX");
    auto* y = channel("TransformCard_PosY");
    const auto start = x->mapToGlobal(x->rect().center());
    const auto end = y->mapToGlobal(y->rect().center());
    mouse(x, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(x, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
    mouse(x, QEvent::MouseMove, end + QPoint{10, 0}, Qt::NoButton, Qt::LeftButton);
    mouse(
      x,
      QEvent::MouseMove,
      end + QPoint{17, 0},
      Qt::NoButton,
      Qt::LeftButton,
      Qt::ControlModifier | Qt::ShiftModifier);
    CHECK(vm::is_equal(entity->entity().origin(), vm::vec3d{10.4, 20.4, 30}, 0.0001));
    // QKeyEvent toggles the modifier represented by its own key code.
    auto ctrl = QKeyEvent{QEvent::KeyPress, Qt::Key_Control, Qt::NoModifier};
    REQUIRE(ctrl.modifiers().testFlag(Qt::ControlModifier));
    QApplication::sendEvent(x, &ctrl);
    CHECK(entity->entity().origin() == vm::vec3d{8, 18, 30});
    auto release = QKeyEvent{QEvent::KeyRelease, Qt::Key_Control, Qt::ControlModifier};
    REQUIRE_FALSE(release.modifiers().testFlag(Qt::ControlModifier));
    QApplication::sendEvent(x, &release);
    CHECK(vm::is_equal(entity->entity().origin(), vm::vec3d{10.7, 20.7, 30}, 0.0001));
    QTest::keyClick(x, Qt::Key_Escape);
    CHECK(entity->entity().origin() == vm::vec3d{10, 20, 30});
  }

  SECTION("control click selects immediately and right click cancels text")
  {
    auto* entity = addLight();
    auto* x = channel("TransformCard_PosX");
    auto* edit = x->findChild<QLineEdit*>();
    mouseFocus(x);
    const auto start = edit->mapToGlobal(edit->rect().center());
    mouse(
      edit,
      QEvent::MouseButtonPress,
      start,
      Qt::LeftButton,
      Qt::LeftButton,
      Qt::ControlModifier);
    CHECK(edit->selectedText() == "10");
    mouse(
      edit,
      QEvent::MouseButtonRelease,
      start,
      Qt::LeftButton,
      Qt::NoButton,
      Qt::ControlModifier);
    CHECK(edit->selectedText() == "10");
    QTest::keyClicks(edit, "123");
    mouse(edit, QEvent::MouseButtonPress, start, Qt::RightButton, Qt::RightButton);
    CHECK(entity->entity().origin() == vm::vec3d{10, 20, 30});
    CHECK(x->value() == 10);
    CHECK_FALSE(x->hasFocus());
  }

  SECTION("editing again positions the caret and arrows select text without stepping")
  {
    auto* entity = addLight("123.5 20 30");
    auto* x = channel("TransformCard_PosX");
    auto* edit = x->findChild<QLineEdit*>();
    mouseFocus(x);
    const auto start = edit->mapToGlobal(edit->rect().center());
    mouse(edit, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(edit, QEvent::MouseButtonRelease, start, Qt::LeftButton, Qt::NoButton);
    REQUIRE(edit->hasSelectedText());
    mouse(edit, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(edit, QEvent::MouseButtonRelease, start, Qt::LeftButton, Qt::NoButton);
    CHECK_FALSE(edit->hasSelectedText());
    QTest::keyClick(edit, Qt::Key_Up);
    CHECK(edit->cursorPosition() == 0);
    QTest::keyClick(edit, Qt::Key_Down, Qt::ShiftModifier);
    CHECK(edit->selectedText() == "123.5");
    CHECK(entity->entity().origin() == vm::vec3d{123.5, 20, 30});
    QTest::keyClick(edit, Qt::Key_Escape);
  }

  SECTION("left and right collapse the numeric selection before moving by character")
  {
    addLight("123.5 20 30");
    auto* x = channel("TransformCard_PosX");
    auto* edit = x->findChild<QLineEdit*>();
    mouseFocus(x);
    const auto start = edit->mapToGlobal(edit->rect().center());
    mouse(edit, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(edit, QEvent::MouseButtonRelease, start, Qt::LeftButton, Qt::NoButton);
    REQUIRE(edit->selectedText() == "123.5");
    CHECK(edit->cursorPosition() == 5);
    QTest::keyClick(edit, Qt::Key_Right);
    CHECK_FALSE(edit->hasSelectedText());
    CHECK(edit->cursorPosition() == 5);
    QTest::keyClick(edit, Qt::Key_Left);
    CHECK(edit->cursorPosition() == 4);
    QTest::keyClick(edit, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(edit, Qt::Key_Left);
    CHECK_FALSE(edit->hasSelectedText());
    CHECK(edit->cursorPosition() == 0);
    QTest::keyClick(edit, Qt::Key_Right);
    CHECK(edit->cursorPosition() == 1);
    QTest::keyClick(edit, Qt::Key_Right, Qt::ShiftModifier);
    CHECK(edit->selectedText() == "2");
    QTest::keyClick(edit, Qt::Key_Escape);
  }

  SECTION("arithmetic respects precedence and commits only on confirmation")
  {
    auto* entity = addLight();
    auto* x = channel("TransformCard_PosX");
    auto* edit = x->findChild<QLineEdit*>();
    x->setLocale(QLocale::c());
    for (const auto& [expression, expected] : std::vector<std::pair<const char*, double>>{
           {"32 + 16 * 2", 64},
           {"(32 + 16) / 2", 24},
           {"-(-12 + 2) * 3", 30},
           {"1e2 / 4", 25},
           {"10 - 3", 7},
           {"2.5 * 4", 10}})
    {
      CAPTURE(expression);
      const auto start = edit->mapToGlobal(edit->rect().center());
      mouse(edit, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
      mouse(edit, QEvent::MouseButtonRelease, start, Qt::LeftButton, Qt::NoButton);
      QTest::keyClicks(edit, expression);
      CHECK(edit->text() == expression);
      CHECK(entity->entity().origin() == vm::vec3d{10, 20, 30});
      REQUIRE(x->hasAcceptableInput());
      QTest::keyClick(edit, Qt::Key_Return);
      CHECK(entity->entity().origin() == vm::vec3d{expected, 20, 30});
      if (expected != 10)
      {
        map.undoCommand();
        CHECK(entity->entity().origin() == vm::vec3d{10, 20, 30});
      }
    }
  }

  SECTION("arithmetic supports localized decimals and angle suffixes")
  {
    addLight();
    auto* x = channel("TransformCard_PosX");
    x->setLocale(QLocale{QLocale::German});
    auto* edit = x->findChild<QLineEdit*>();
    const auto start = edit->mapToGlobal(edit->rect().center());
    mouse(edit, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(edit, QEvent::MouseButtonRelease, start, Qt::LeftButton, Qt::NoButton);
    QTest::keyClicks(edit, "1,5 * 2");
    QTest::keyClick(edit, Qt::Key_Return);
    CHECK(card.currentLocation() == vm::vec3d{3, 20, 30});

    auto* rotation = channel("TransformCard_RotX");
    edit = rotation->findChild<QLineEdit*>();
    const auto rotationStart = edit->mapToGlobal(edit->rect().center());
    mouse(edit, QEvent::MouseButtonPress, rotationStart, Qt::LeftButton, Qt::LeftButton);
    mouse(edit, QEvent::MouseButtonRelease, rotationStart, Qt::LeftButton, Qt::NoButton);
    QTest::keyClicks(edit, "90 / 2");
    CHECK(edit->text().startsWith("90 / 2"));
    QTest::keyClick(edit, Qt::Key_Return);
    CHECK(card.currentRotation().x() == 45);
  }

  SECTION("multi-edit mirrors arithmetic and applies the result in one undo step")
  {
    auto* entity = addLight();
    auto* x = channel("TransformCard_PosX");
    auto* y = channel("TransformCard_PosY");
    auto* z = channel("TransformCard_PosZ");
    auto* edit = x->findChild<QLineEdit*>();
    const auto start = edit->mapToGlobal(edit->rect().center());
    const auto end = z->mapToGlobal(z->rect().center());
    mouse(edit, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(edit, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
    mouse(edit, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
    QTest::keyClicks(edit, "(32 + 16) / 2");
    CHECK(y->findChild<QLineEdit*>()->text() == edit->text());
    CHECK(z->findChild<QLineEdit*>()->text() == edit->text());
    CHECK(entity->entity().origin() == vm::vec3d{10, 20, 30});
    QTest::keyClick(edit, Qt::Key_Return);
    CHECK(entity->entity().origin() == vm::vec3d{24, 24, 24});
    map.undoCommand();
    CHECK(entity->entity().origin() == vm::vec3d{10, 20, 30});
  }

  SECTION("scale arithmetic keeps precision and can be cancelled or committed by tab")
  {
    addLight("10 20 30", "2 3 4");
    auto* x = channel("TransformCard_ScaleX");
    auto* edit = x->findChild<QLineEdit*>();
    const auto start = edit->mapToGlobal(edit->rect().center());
    mouse(edit, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(edit, QEvent::MouseButtonRelease, start, Qt::LeftButton, Qt::NoButton);
    QTest::keyClicks(edit, "(2 + 3) / 2");
    QTest::keyClick(edit, Qt::Key_Escape);
    CHECK(card.currentScale() == vm::vec3d{2, 3, 4});
    mouse(edit, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(edit, QEvent::MouseButtonRelease, start, Qt::LeftButton, Qt::NoButton);
    QTest::keyClicks(edit, "(2 + 3) / 2");
    QTest::keyClick(edit, Qt::Key_Tab);
    CHECK(card.currentScale() == vm::vec3d{2.5, 3, 4});
    CHECK(edit->text() == "2.500");
    map.undoCommand();
    CHECK(card.currentScale() == vm::vec3d{2, 3, 4});
  }

  SECTION("invalid arithmetic never changes the selected channels")
  {
    auto* entity = addLight();
    auto* x = channel("TransformCard_PosX");
    auto* y = channel("TransformCard_PosY");
    auto* z = channel("TransformCard_PosZ");
    auto* edit = x->findChild<QLineEdit*>();
    for (const auto* expression :
         {"2 +",
          "1/0",
          "0/0",
          "1e999",
          "1e999 + 1",
          "1e",
          "32//2",
          "2**3",
          "abc",
          "2(3)",
          "2^3"})
    {
      CAPTURE(expression);
      const auto start = edit->mapToGlobal(edit->rect().center());
      const auto end = z->mapToGlobal(z->rect().center());
      mouse(edit, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
      mouse(edit, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
      mouse(edit, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
      QTest::keyClicks(edit, expression);
      CHECK(edit->text() == expression);
      REQUIRE_FALSE(x->hasAcceptableInput());
      QTest::keyClick(edit, Qt::Key_Return);
      CHECK(entity->entity().origin() == vm::vec3d{10, 20, 30});
      CHECK(x->value() == 10);
      CHECK(y->value() == 20);
      CHECK(z->value() == 30);
    }
  }

  SECTION("ordinary wheel does not change a focused text edit")
  {
    auto* entity = addLight();
    auto* x = channel("TransformCard_PosX");
    auto* edit = x->findChild<QLineEdit*>();
    mouseFocus(x);
    const auto start = edit->mapToGlobal(edit->rect().center());
    mouse(edit, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(edit, QEvent::MouseButtonRelease, start, Qt::LeftButton, Qt::NoButton);
    auto wheel = QWheelEvent{
      QPointF{edit->rect().center()},
      QPointF{start},
      QPoint{},
      QPoint{0, 120},
      Qt::NoButton,
      Qt::NoModifier,
      Qt::NoScrollPhase,
      false};
    QApplication::sendEvent(edit, &wheel);
    CHECK_FALSE(wheel.isAccepted());
    CHECK(entity->entity().origin() == vm::vec3d{10, 20, 30});
    QTest::keyClick(edit, Qt::Key_Escape);
  }

  SECTION("scrubbing previews the scene and commits one undo step")
  {
    auto* entity = addLight();
    auto* x = channel("TransformCard_PosX");
    auto* edit = x->findChild<QLineEdit*>();
    const auto start = edit->mapToGlobal(edit->rect().center());
    mouse(edit, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(edit, QEvent::MouseMove, start + QPoint{20, 0}, Qt::NoButton, Qt::LeftButton);
    CHECK(entity->entity().origin().x() == 10.0);
    mouse(edit, QEvent::MouseMove, start + QPoint{40, 0}, Qt::NoButton, Qt::LeftButton);
    CHECK(entity->entity().origin().x() == 30.0);
    mouse(edit, QEvent::MouseMove, start + QPoint{60, 0}, Qt::NoButton, Qt::LeftButton);
    CHECK(entity->entity().origin().x() == 50.0);
    mouse(
      edit,
      QEvent::MouseButtonRelease,
      start + QPoint{60, 0},
      Qt::LeftButton,
      Qt::NoButton);
    map.undoCommand();
    CHECK(entity->entity().origin() == vm::vec3d{10, 20, 30});
    CHECK(x->value() == 10.0);
    map.redoCommand();
    CHECK(entity->entity().origin().x() == 50.0);
  }

  SECTION("escape and right click cancel a live scrub")
  {
    auto* entity = addLight();
    auto* x = channel("TransformCard_PosX");
    const auto start = x->mapToGlobal(x->rect().center());
    for (const auto rightClick : {false, true})
    {
      mouse(x, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
      mouse(x, QEvent::MouseMove, start + QPoint{20, 0}, Qt::NoButton, Qt::LeftButton);
      mouse(x, QEvent::MouseMove, start + QPoint{40, 0}, Qt::NoButton, Qt::LeftButton);
      CHECK(entity->entity().origin().x() == 30.0);
      if (rightClick)
      {
        mouse(x, QEvent::MouseButtonPress, start, Qt::RightButton, Qt::RightButton);
      }
      else
      {
        QTest::keyClick(x, Qt::Key_Escape);
      }
      CHECK(entity->entity().origin() == vm::vec3d{10, 20, 30});
      CHECK(x->value() == 10.0);
    }
  }

  SECTION("vertical text edit interprets the typed value before atomic commit")
  {
    auto* entity = addLight();
    auto* x = channel("TransformCard_PosX");
    auto* z = channel("TransformCard_PosZ");
    auto* edit = x->findChild<QLineEdit*>();
    auto finished = QSignalSpy{&card, &TransformCard::editingFinished};
    const auto start = edit->mapToGlobal(edit->rect().center());
    const auto end = z->mapToGlobal(z->rect().center());
    mouse(edit, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(edit, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
    mouse(edit, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
    edit->setText("123.5");
    CHECK(z->findChild<QLineEdit*>()->text() == edit->text());
    CHECK(entity->entity().origin() == vm::vec3d{10, 20, 30});
    QTest::keyClick(edit, Qt::Key_Return);
    CHECK(entity->entity().origin() == vm::vec3d{123.5, 123.5, 123.5});
    CHECK(finished.count() == 1);
    CHECK_FALSE(x->hasFocus());
    CHECK_FALSE(x->property("multiSelected").toBool());
    map.undoCommand();
    CHECK(entity->entity().origin() == vm::vec3d{10, 20, 30});
  }

  SECTION("multi-edit mirrors each text change and commits one undo step")
  {
    auto* entity = addLight();
    auto* x = channel("TransformCard_PosX");
    auto* y = channel("TransformCard_PosY");
    auto* z = channel("TransformCard_PosZ");
    auto* edit = x->findChild<QLineEdit*>();
    const auto start = edit->mapToGlobal(edit->rect().center());
    const auto end = z->mapToGlobal(z->rect().center());
    mouseFocus(x);
    mouse(edit, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(edit, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
    mouse(edit, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
    const auto checkMirrors = [&]() {
      CHECK(y->findChild<QLineEdit*>()->text() == edit->text());
      CHECK(z->findChild<QLineEdit*>()->text() == edit->text());
      CHECK(entity->entity().origin() == vm::vec3d{10, 20, 30});
    };
    checkMirrors();
    QTest::keyClicks(edit, "-");
    CHECK(edit->text() == "-");
    checkMirrors();
    QTest::keyClicks(edit, "12");
    checkMirrors();
    QTest::keyClick(edit, Qt::Key_Backspace);
    checkMirrors();
    QTest::keyClick(edit, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(edit, Qt::Key_Backspace);
    CHECK(edit->text().isEmpty());
    checkMirrors();
    auto* previousClipboard = new QMimeData{};
    if (const auto* original = QApplication::clipboard()->mimeData())
    {
      for (const auto& format : original->formats())
      {
        previousClipboard->setData(format, original->data(format));
      }
    }
    const auto restoreClipboard = qScopeGuard([previousClipboard]() {
      QApplication::clipboard()->setMimeData(previousClipboard);
    });
    QApplication::clipboard()->setText("123.5");
    QTest::keyClick(edit, Qt::Key_V, Qt::ControlModifier);
    CHECK(edit->text() == "123.5");
    checkMirrors();
    QTest::keyClick(edit, Qt::Key_Return);
    CHECK(entity->entity().origin() == vm::vec3d{123.5, 123.5, 123.5});
    map.undoCommand();
    CHECK(entity->entity().origin() == vm::vec3d{10, 20, 30});
    map.redoCommand();
    CHECK(entity->entity().origin() == vm::vec3d{123.5, 123.5, 123.5});
  }

  SECTION("cancelling mirrored text restores every original field")
  {
    auto* entity = addLight();
    auto* x = channel("TransformCard_PosX");
    auto* y = channel("TransformCard_PosY");
    auto* z = channel("TransformCard_PosZ");
    auto* edit = x->findChild<QLineEdit*>();
    const auto start = edit->mapToGlobal(edit->rect().center());
    const auto end = z->mapToGlobal(z->rect().center());
    for (const auto rightClick : {false, true})
    {
      mouseFocus(x);
      mouse(edit, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
      mouse(edit, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
      mouse(edit, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
      QTest::keyClicks(edit, "42");
      REQUIRE(y->findChild<QLineEdit*>()->text() == "42");
      REQUIRE(z->findChild<QLineEdit*>()->text() == "42");
      if (rightClick)
      {
        mouse(edit, QEvent::MouseButtonPress, start, Qt::RightButton, Qt::RightButton);
      }
      else
      {
        QTest::keyClick(edit, Qt::Key_Escape);
      }
      CHECK(edit->text() == "10");
      CHECK(y->findChild<QLineEdit*>()->text() == "20");
      CHECK(z->findChild<QLineEdit*>()->text() == "30");
      CHECK(entity->entity().origin() == vm::vec3d{10, 20, 30});
    }
  }

  SECTION("horizontal release exits the focused numeric field")
  {
    auto* entity = addLight();
    auto* x = channel("TransformCard_PosX");
    auto* edit = x->findChild<QLineEdit*>();
    auto finished = QSignalSpy{&card, &TransformCard::editingFinished};
    mouseFocus(x);
    const auto start = edit->mapToGlobal(edit->rect().center());
    mouse(edit, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(edit, QEvent::MouseMove, start + QPoint{20, 0}, Qt::NoButton, Qt::LeftButton);
    mouse(edit, QEvent::MouseMove, start + QPoint{40, 0}, Qt::NoButton, Qt::LeftButton);
    mouse(
      edit,
      QEvent::MouseButtonRelease,
      start + QPoint{40, 0},
      Qt::LeftButton,
      Qt::NoButton);
    CHECK(entity->entity().origin().x() == 30);
    CHECK_FALSE(x->hasFocus());
    CHECK_FALSE(edit->hasFocus());
    CHECK_FALSE(edit->hasSelectedText());
    CHECK(finished.count() == 1);
    CHECK(QWidget::mouseGrabber() != edit);
  }

  SECTION("enter commits once and exits text editing before the next drag")
  {
    auto* entity = addLight();
    auto* x = channel("TransformCard_PosX");
    auto* edit = x->findChild<QLineEdit*>();
    auto finished = QSignalSpy{&card, &TransformCard::editingFinished};
    const auto start = edit->mapToGlobal(edit->rect().center());
    mouse(edit, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(edit, QEvent::MouseButtonRelease, start, Qt::LeftButton, Qt::NoButton);
    edit->setText("42.5");
    QTest::keyClick(edit, Qt::Key_Enter);
    CHECK(entity->entity().origin() == vm::vec3d{42.5, 20, 30});
    CHECK(finished.count() == 1);
    CHECK_FALSE(x->hasFocus());
    CHECK(edit->cursor().shape() == Qt::SizeHorCursor);
    CHECK_FALSE(edit->hasSelectedText());
    map.undoCommand();
    CHECK(entity->entity().origin() == vm::vec3d{10, 20, 30});

    mouse(edit, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(edit, QEvent::MouseMove, start + QPoint{20, 0}, Qt::NoButton, Qt::LeftButton);
    mouse(edit, QEvent::MouseMove, start + QPoint{40, 0}, Qt::NoButton, Qt::LeftButton);
    CHECK(entity->entity().origin().x() == 30);
    mouse(
      edit,
      QEvent::MouseButtonRelease,
      start + QPoint{40, 0},
      Qt::LeftButton,
      Qt::NoButton);
  }

  SECTION("tab commits without requesting a return to the map")
  {
    auto* entity = addLight();
    auto* x = channel("TransformCard_PosX");
    auto* edit = x->findChild<QLineEdit*>();
    auto finished = QSignalSpy{&card, &TransformCard::editingFinished};
    const auto start = edit->mapToGlobal(edit->rect().center());
    mouse(edit, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(edit, QEvent::MouseButtonRelease, start, Qt::LeftButton, Qt::NoButton);
    edit->setText("25");
    QTest::keyClick(edit, Qt::Key_Tab);
    CHECK(entity->entity().origin().x() == 25);
    CHECK(finished.isEmpty());
  }

  SECTION("destroying a card with pending text does not apply an edit")
  {
    auto* entity = addLight();
    {
      auto pendingCard = TransformCard{document};
      pendingCard.resize(320, 400);
      pendingCard.show();
      QApplication::processEvents();
      auto* x = pendingCard.findChild<SpinControl*>("TransformCard_PosX");
      REQUIRE(x);
      x->setFocus();
      x->findChild<QLineEdit*>()->setText("99");
    }
    CHECK(entity->entity().origin() == vm::vec3d{10, 20, 30});
  }

  SECTION("uniform scale preserves proportions and locked axes")
  {
    auto* entity = addLight("10 20 30", "2 3 4");
    CHECK_FALSE(card.uniformScale());
    CHECK(channel("TransformCard_ScaleX")->text() == "2.000");
    card.setUniformScale(true);
    auto* lock = card.findChild<QToolButton*>("TransformCard_LockScaleZ");
    REQUIRE(lock);
    lock->setChecked(true);
    channel("TransformCard_ScaleY")->setValue(6);
    CHECK(card.currentScale() == vm::vec3d{4, 6, 4});
    CHECK(*entity->entity().property("scale") == "4 6 4");
    map.undoCommand();
    CHECK(card.currentScale() == vm::vec3d{2, 3, 4});
  }

  SECTION("yaw mode reads angles and disables unused channels")
  {
    auto* entity = addLight();
    mdl::setEntityProperty(map, "angles", "10 20 30");
    card.setRotationMode(TransformCard::RotationMode::YawOnly);
    CHECK(card.currentRotation() == vm::vec3d{0, 0, 20});
    CHECK_FALSE(channel("TransformCard_RotX")->isEnabled());
    CHECK_FALSE(channel("TransformCard_RotY")->isEnabled());
    CHECK(channel("TransformCard_RotZ")->isEnabled());
    channel("TransformCard_RotZ")->setValue(45);
    CHECK(*entity->entity().property("angle") == "45");
    CHECK(entity->entity().property("angles") == nullptr);
    card.updateFromSelection();
    CHECK(card.currentRotation() == vm::vec3d{0, 0, 45});
    map.undoCommand();
    CHECK(*entity->entity().property("angles") == "10 20 30");
    CHECK(entity->entity().property("angle") == nullptr);
  }

  SECTION("brush scaling previews accumulated drag and cancels without distortion")
  {
    auto* brush = mdl::createBrushNode(map);
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {brush}}});
    mdl::selectNodes(map, {brush});
    card.updateFromSelection();
    const auto bounds = brush->physicalBounds();
    auto* x = channel("TransformCard_ScaleX");
    const auto start = x->mapToGlobal(x->rect().center());
    mouse(x, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(x, QEvent::MouseMove, start + QPoint{20, 0}, Qt::NoButton, Qt::LeftButton);
    CHECK(brush->physicalBounds() == bounds);
    mouse(x, QEvent::MouseMove, start + QPoint{40, 0}, Qt::NoButton, Qt::LeftButton);
    CHECK(vm::is_equal(
      brush->physicalBounds().size(), bounds.size() * vm::vec3d{1.1, 1, 1}, 0.001));
    mouse(x, QEvent::MouseMove, start + QPoint{60, 0}, Qt::NoButton, Qt::LeftButton);
    CHECK(vm::is_equal(
      brush->physicalBounds().size(), bounds.size() * vm::vec3d{1.2, 1, 1}, 0.001));
    QTest::keyClick(x, Qt::Key_Escape);
    CHECK(brush->physicalBounds() == bounds);
  }

  SECTION("multi-axis text edit can be cancelled before changing the map")
  {
    auto* entity = addLight();
    auto* x = channel("TransformCard_PosX");
    auto* z = channel("TransformCard_PosZ");
    const auto start = x->mapToGlobal(x->rect().center());
    const auto end = z->mapToGlobal(z->rect().center());
    mouse(x, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(x, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
    mouse(x, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
    x->findChild<QLineEdit*>()->setText("99");
    QTest::keyClick(x, Qt::Key_Escape);
    CHECK(entity->entity().origin() == vm::vec3d{10, 20, 30});
    CHECK(x->value() == 10);
    CHECK_FALSE(z->property("multiSelected").toBool());
  }

  SECTION("vertical selection skips a locked channel")
  {
    auto* entity = addLight();
    auto* lock = card.findChild<QToolButton*>("TransformCard_LockPosY");
    REQUIRE(lock);
    lock->setChecked(true);
    auto* x = channel("TransformCard_PosX");
    auto* z = channel("TransformCard_PosZ");
    const auto start = x->mapToGlobal(x->rect().center());
    const auto end = z->mapToGlobal(z->rect().center());
    mouse(x, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(x, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
    mouse(x, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
    x->findChild<QLineEdit*>()->setText("99");
    QTest::keyClick(x, Qt::Key_Return);
    CHECK(entity->entity().origin() == vm::vec3d{99, 20, 99});
  }

  SECTION("precision dragging retains fractional mouse increments")
  {
    auto* entity = addLight();
    auto* x = channel("TransformCard_PosX");
    const auto start = x->mapToGlobal(x->rect().center());
    mouse(x, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(x, QEvent::MouseMove, start, Qt::NoButton, Qt::LeftButton);
    mouse(
      x,
      QEvent::MouseMove,
      start + QPoint{10, 0},
      Qt::NoButton,
      Qt::LeftButton,
      Qt::ShiftModifier);
    mouse(
      x,
      QEvent::MouseMove,
      start + QPoint{20, 0},
      Qt::NoButton,
      Qt::LeftButton,
      Qt::ShiftModifier);
    CHECK(entity->entity().origin().x() == 11);
    mouse(
      x, QEvent::MouseButtonRelease, start + QPoint{20, 0}, Qt::LeftButton, Qt::NoButton);
  }

  SECTION("card properties and initial state")
  {
    CHECK(card.objectName() == "TransformCard");
    CHECK(card.expanded());
  }

  SECTION("reads transform properties from point entity")
  {
    auto* light = new mdl::EntityNode{mdl::Entity{
      {{"classname", "light"},
       {"origin", "64 128 -32"},
       {"angles", "10 20 30"},
       {"scale", "2.0"}}}};

    mdl::addNodes(map, {{&mdl::parentForNodes(map), {light}}});
    mdl::selectNodes(map, {light});
    card.updateFromSelection();

    CHECK(light->entity().classname() == "light");
    CHECK(card.currentLocation() == vm::vec3d{64.0, 128.0, -32.0});

    // In default XyzEuler mode: Roll=30, Pitch=10, Yaw=20
    CHECK(card.currentRotation() == vm::vec3d{30.0, 10.0, 20.0});

    // In QuakeEuler mode: Pitch=10, Yaw=20, Roll=30
    card.setRotationMode(TransformCard::RotationMode::QuakeEuler);
    CHECK(card.currentRotation() == vm::vec3d{10.0, 20.0, 30.0});

    // Scale
    CHECK(card.currentScale() == vm::vec3d{2.0, 2.0, 2.0});
  }

  SECTION("toggles expanded state and emits signal")
  {
    bool observedExpanded = true;
    QObject::connect(
      &card, &CollapsibleTitledPanel::toggled, [&](bool exp) { observedExpanded = exp; });

    card.setExpanded(false);
    CHECK(!card.expanded());
    CHECK(!observedExpanded);

    card.setExpanded(true);
    CHECK(card.expanded());
    CHECK(observedExpanded);
  }

  SECTION("multi-drag across vector channels and multi-edit")
  {
    auto* light =
      new mdl::EntityNode{mdl::Entity{{{"classname", "light"}, {"origin", "10 20 30"}}}};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {light}}});
    mdl::selectNodes(map, {light});
    card.updateFromSelection();

    auto* posX = card.findChild<SpinControl*>("TransformCard_PosX");
    auto* posY = card.findChild<SpinControl*>("TransformCard_PosY");
    auto* posZ = card.findChild<SpinControl*>("TransformCard_PosZ");

    REQUIRE(posX != nullptr);
    REQUIRE(posY != nullptr);
    REQUIRE(posZ != nullptr);

    CHECK(posX->value() == 10.0);
    CHECK(posY->value() == 20.0);
    CHECK(posZ->value() == 30.0);

    // Simulate vertical drag from posX down through posY and posZ
    const auto startPos =
      posX->mapToGlobal(QPoint{posX->width() / 2, posX->height() / 2});
    const auto endPos = posZ->mapToGlobal(QPoint{posZ->width() / 2, posZ->height() / 2});

    auto* pressEvent = new QMouseEvent(
      QEvent::MouseButtonPress,
      QPointF{posX->width() / 2.0, posX->height() / 2.0},
      QPointF{startPos},
      Qt::LeftButton,
      Qt::LeftButton,
      Qt::NoModifier);
    QApplication::sendEvent(posX, pressEvent);
    delete pressEvent;

    auto* moveEvent = new QMouseEvent(
      QEvent::MouseMove,
      QPointF{endPos},
      QPointF{endPos},
      Qt::LeftButton,
      Qt::LeftButton,
      Qt::NoModifier);
    QApplication::sendEvent(posX, moveEvent);
    delete moveEvent;

    // Both posX, posY, and posZ should now be highlighted with multiSelected property
    CHECK(posX->property("multiSelected").toBool());
    CHECK(posY->property("multiSelected").toBool());
    CHECK(posZ->property("multiSelected").toBool());

    // Release mouse -> enters MultiEditing
    auto* releaseEvent = new QMouseEvent(
      QEvent::MouseButtonRelease,
      QPointF{endPos},
      QPointF{endPos},
      Qt::LeftButton,
      Qt::NoButton,
      Qt::NoModifier);
    QApplication::sendEvent(posX, releaseEvent);
    delete releaseEvent;

    // Type a value (e.g. 100) into posX and press Enter
    posX->setValue(100.0);
    auto* enterEvent = new QKeyEvent(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(posX, enterEvent);
    delete enterEvent;

    // Multi-edit commit should have propagated 100.0 to Y and Z as well!
    CHECK(posX->value() == 100.0);
    CHECK(posY->value() == 100.0);
    CHECK(posZ->value() == 100.0);

    // Highlights should be cleared after commit
    CHECK(!posX->property("multiSelected").toBool());
    CHECK(!posY->property("multiSelected").toBool());
    CHECK(!posZ->property("multiSelected").toBool());
  }

  SECTION("axis lock toggle disables spin control")
  {
    auto* posX = card.findChild<SpinControl*>("TransformCard_PosX");
    REQUIRE(posX != nullptr);
    CHECK(posX->isEnabled());

    const auto lockButtons = card.findChildren<QToolButton*>();
    QToolButton* lockX = nullptr;
    for (auto* btn : lockButtons)
    {
      if (btn->toolTip().contains("Lock X Location"))
      {
        lockX = btn;
        break;
      }
    }
    REQUIRE(lockX != nullptr);
    lockX->setChecked(true);
    CHECK(!posX->isEnabled());

    lockX->setChecked(false);
    CHECK(posX->isEnabled());
  }

  SECTION("horizontal scrubbing adjusts value by delta")
  {
    auto* light =
      new mdl::EntityNode{mdl::Entity{{{"classname", "light"}, {"origin", "50 50 50"}}}};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {light}}});
    mdl::selectNodes(map, {light});
    card.updateFromSelection();

    auto* posX = card.findChild<SpinControl*>("TransformCard_PosX");
    REQUIRE(posX != nullptr);
    CHECK(posX->value() == 50.0);

    const auto centerPos =
      posX->mapToGlobal(QPoint{posX->width() / 2, posX->height() / 2});
    auto* pressEvent = new QMouseEvent(
      QEvent::MouseButtonPress,
      QPointF{posX->width() / 2.0, posX->height() / 2.0},
      QPointF{centerPos},
      Qt::LeftButton,
      Qt::LeftButton,
      Qt::NoModifier);
    QApplication::sendEvent(posX, pressEvent);
    delete pressEvent;

    // Move horizontally to the right by 20 pixels
    const auto scrubPos = centerPos + QPoint{20, 0};
    auto* moveEvent = new QMouseEvent(
      QEvent::MouseMove,
      QPointF{scrubPos},
      QPointF{scrubPos},
      Qt::LeftButton,
      Qt::LeftButton,
      Qt::NoModifier);
    QApplication::sendEvent(posX, moveEvent);
    delete moveEvent;

    CHECK(posX->value() == 50.0);
    mouse(
      posX, QEvent::MouseMove, scrubPos + QPoint{20, 0}, Qt::NoButton, Qt::LeftButton);
    // Crossing the threshold establishes the baseline; subsequent motion edits.
    CHECK(posX->value() > 50.0);

    auto* releaseEvent = new QMouseEvent(
      QEvent::MouseButtonRelease,
      QPointF{scrubPos},
      QPointF{scrubPos},
      Qt::LeftButton,
      Qt::NoButton,
      Qt::NoModifier);
    QApplication::sendEvent(posX, releaseEvent);
    delete releaseEvent;
  }
}

} // namespace tb::ui
