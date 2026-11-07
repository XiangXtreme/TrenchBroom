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

#include <QApplication>
#include <QFile>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtTest/QTest>

#include "ui/ClickableTitleBar.h"
#include "ui/CollapsibleTitledPanel.h"
#include "ui/QPathUtils.h"
#include "ui/SystemPaths.h"
#include "ui/Theme.h"
#include "ui/ThemeRegistry.h"

#include <algorithm>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("CollapsibleTitledPanel")
{
  auto panel = CollapsibleTitledPanel{"Properties"};
  panel.resize(360, 240);
  panel.show();
  QApplication::processEvents();

  auto* titleBar = panel.findChild<ClickableTitleBar*>();
  auto* stateIcon = panel.findChild<QToolButton*>("ClickableTitleBar_StateIcon");
  auto* stateText = panel.findChild<QLabel*>("ClickableTitleBar_StateText");

  REQUIRE(titleBar != nullptr);
  REQUIRE(stateIcon != nullptr);
  REQUIRE(stateText != nullptr);

  SECTION("uses an icon-only expanded state")
  {
    CHECK(panel.expanded());
    CHECK(panel.property("expanded").toBool());
    CHECK(titleBar->property("expanded").toBool());
    CHECK(!panel.getPanel()->isHidden());
    CHECK(!stateIcon->isHidden());
    CHECK(stateIcon->arrowType() == Qt::DownArrow);
    CHECK(stateIcon->toolTip() == "Collapse");
    CHECK(stateText->isHidden());
  }

  SECTION("toggles from the full title row")
  {
    QTest::mouseClick(titleBar, Qt::LeftButton);
    QApplication::processEvents();

    CHECK_FALSE(panel.expanded());
    CHECK_FALSE(panel.property("expanded").toBool());
    CHECK_FALSE(titleBar->property("expanded").toBool());
    CHECK(panel.getPanel()->isHidden());
    CHECK(stateIcon->toolTip() == "Expand");
    CHECK(stateIcon->arrowType() == Qt::RightArrow);
  }

  SECTION("supports keyboard toggling")
  {
    CHECK(titleBar->focusPolicy() == Qt::StrongFocus);

    QTest::keyClick(titleBar, Qt::Key_Space);
    CHECK_FALSE(panel.expanded());

    QTest::keyClick(titleBar, Qt::Key_Return);
    CHECK(panel.expanded());
  }

  SECTION("restores the collapsed state")
  {
    panel.collapse();
    const auto state = panel.saveState();

    auto restoredPanel = CollapsibleTitledPanel{"Restored"};
    REQUIRE(restoredPanel.restoreState(state));
    CHECK_FALSE(restoredPanel.expanded());
    CHECK(restoredPanel.getPanel()->isHidden());
  }

  SECTION("expanded content can still grow with its container")
  {
    const auto initialHeight = panel.getPanel()->height();
    panel.resize(360, 480);
    REQUIRE(
      QTest::qWaitFor([&] { return panel.getPanel()->height() > initialHeight; }, 1000));
    const auto titleHeight = titleBar->height();
    panel.collapse();
    REQUIRE(QTest::qWaitFor([&] { return panel.height() == titleHeight; }, 1000));
    panel.expand();
    panel.resize(360, 480);
    REQUIRE(
      QTest::qWaitFor([&] { return panel.getPanel()->height() > initialHeight; }, 1000));
    CHECK(titleBar->height() == titleHeight);
  }

  SECTION("keeps themed scroll sections compact through repeated toggles and resizing")
  {
    const auto oldStyleSheet = qApp->styleSheet();
    const auto restoreStyle = qScopeGuard([&] { qApp->setStyleSheet(oldStyleSheet); });
    auto file =
      QFile{pathAsQString(SystemPaths::findResourceFile("stylesheets/base.qss"))};
    REQUIRE(file.open(QIODevice::ReadOnly));
    const auto sourceStyleSheet = QString::fromUtf8(file.readAll());

    for (const auto* theme : {"builtin.light", "builtin.dark", "builtin.blender"})
    {
      CAPTURE(theme);
      auto styleSheet = sourceStyleSheet;
      const auto& tokens = ThemeRegistry::instance().resolveTheme(theme).tokens;
      REQUIRE(expandThemeStyleSheet(styleSheet, tokens));
      qApp->setStyleSheet(styleSheet);

      auto scroll = QScrollArea{};
      scroll.setWidgetResizable(true);
      auto* content = new QWidget{};
      auto* sectionsLayout = new QVBoxLayout{content};
      auto sections = std::vector<CollapsibleTitledPanel*>{};
      for (auto i = 0; i < 3; ++i)
      {
        auto* section = new CollapsibleTitledPanel{"Terrain properties"};
        auto* form = new QFormLayout{section->getPanel()};
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        for (auto row = 0; row < 5; ++row)
          form->addRow("Minimum terrain face width", new QLineEdit{"16"});
        sectionsLayout->addWidget(section);
        sections.push_back(section);
      }
      sectionsLayout->addStretch();
      scroll.setWidget(content);
      scroll.show();
      auto* enabledTitle =
        sections.front()->findChild<QLabel*>("ClickableTitleBar_Title");
      REQUIRE(enabledTitle);
      enabledTitle->ensurePolished();
      CHECK(
        enabledTitle->palette().color(QPalette::Active, QPalette::WindowText)
        == tokens.text);
      sections.front()->setEnabled(false);
      QApplication::processEvents();
      CHECK(
        enabledTitle->palette().color(QPalette::Disabled, QPalette::WindowText)
        == tokens.disabledText);
      sections.front()->setEnabled(true);

      for (auto cycle = 0; cycle < 12; ++cycle)
      {
        CAPTURE(cycle);
        scroll.resize(cycle % 2 == 0 ? 320 : 480, cycle % 3 == 0 ? 360 : 800);
        for (auto* section : sections)
        {
          auto* bar = section->findChild<ClickableTitleBar*>();
          REQUIRE(bar);
          QTest::mouseClick(bar, Qt::LeftButton);
          REQUIRE_FALSE(section->expanded());
        }
        const auto compact = QTest::qWaitFor(
          [&] {
            return std::ranges::all_of(sections, [](auto* section) {
              const auto* bar = section->template findChild<ClickableTitleBar*>();
              return section->height() == bar->height()
                     && bar->height()
                          == bar->sizeHint().expandedTo(bar->minimumSize()).height();
            });
          },
          1000);
        for (auto* section : sections)
        {
          const auto* bar = section->findChild<ClickableTitleBar*>();
          CHECK(section->height() == bar->height());
          CHECK(bar->height() == bar->sizeHint().expandedTo(bar->minimumSize()).height());
        }
        REQUIRE(compact);
        CHECK(scroll.horizontalScrollBar()->maximum() == 0);
        for (auto* section : sections)
        {
          auto* bar = section->findChild<ClickableTitleBar*>();
          const auto titleHeight = bar->height();
          auto* icon = section->findChild<QToolButton*>("ClickableTitleBar_StateIcon");
          QTest::mouseClick(icon, Qt::LeftButton);
          REQUIRE(section->expanded());
          REQUIRE(QTest::qWaitFor(
            [&] {
              return section->height() > titleHeight && bar->height() == titleHeight;
            },
            1000));
        }
      }
    }
  }
}

} // namespace tb::ui
