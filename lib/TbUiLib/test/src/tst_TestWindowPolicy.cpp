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

#include <QApplication>
#include <QDialog>
#include <QImage>
#include <QTimer>
#include <QWidget>
#include <QWindow>

#include "TestWindowPolicy.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{

void checkBackgroundWindow(QWidget& widget)
{
  CHECK(widget.isVisible());
  CHECK(widget.testAttribute(Qt::WA_DontShowOnScreen));
  REQUIRE(widget.windowHandle() != nullptr);
  CHECK_FALSE(widget.windowHandle()->isVisible());
  CHECK_FALSE(widget.windowHandle()->isExposed());
}

} // namespace

TEST_CASE("TestWindowPolicy.configuration")
{
  const auto showWindows = qEnvironmentVariableIntValue("TB_TEST_SHOW_WINDOWS") == 1;
  auto window = QWidget{};
  window.ensurePolished();
  CHECK(window.testAttribute(Qt::WA_DontShowOnScreen) == !showWindows);
  CHECK(QApplication::testAttribute(Qt::AA_DontUseNativeDialogs) == !showWindows);
}

TEST_CASE("TestWindowPolicy")
{
  auto policy = TestWindowPolicy{*qApp};

  SECTION("shown widgets retain rendering without native visibility")
  {
    auto window = QWidget{};
    window.resize(120, 80);
    window.setAutoFillBackground(true);
    auto palette = window.palette();
    palette.setColor(QPalette::Window, QColor{17, 34, 51});
    window.setPalette(palette);
    window.show();
    QApplication::processEvents();
    checkBackgroundWindow(window);

    auto image = QImage{window.size(), QImage::Format_RGB32};
    image.fill(Qt::black);
    window.render(&image);
    CHECK(image.pixelColor(60, 40) == QColor{17, 34, 51});

    auto popup = QWidget{&window, Qt::Popup};
    popup.show();
    QApplication::processEvents();
    checkBackgroundWindow(popup);
  }

  SECTION("a polished child promoted to a window stays off the desktop")
  {
    auto parent = QWidget{};
    auto child = QWidget{&parent};
    child.ensurePolished();
    child.setParent(nullptr);
    child.show();
    QApplication::processEvents();
    checkBackgroundWindow(child);
  }

  SECTION("modal dialogs retain their event loop and active modal state")
  {
    auto dialog = QDialog{};
    QTimer::singleShot(0, &dialog, [&] {
      checkBackgroundWindow(dialog);
      CHECK(QApplication::activeModalWidget() == &dialog);
      dialog.accept();
    });
    CHECK(dialog.exec() == QDialog::Accepted);
    CHECK_FALSE(dialog.isVisible());
  }
}

} // namespace tb::ui
