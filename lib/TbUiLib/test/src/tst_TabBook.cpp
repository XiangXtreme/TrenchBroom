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
#include <QHBoxLayout>
#include <QPushButton>
#include <QtTest/QTest>

#include "ui/TabBar.h"
#include "ui/TabBook.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{

class ActionPage : public TabBookPage
{
public:
  QWidget* createTabBarPage(QWidget* parent) override
  {
    auto* actions = new QWidget{parent};
    auto* layout = new QHBoxLayout{actions};
    layout->addWidget(new QPushButton{"Run", actions});
    return actions;
  }
};

TabBarButton* tabButton(TabBook& tabBook, const QString& accessibleName)
{
  for (auto* button : tabBook.findChildren<TabBarButton*>())
  {
    if (button->accessibleName() == accessibleName)
    {
      return button;
    }
  }
  return nullptr;
}

void addTestPages(TabBook& tabBook)
{
  tabBook.addPage(new TabBookPage{}, "First");
  tabBook.addPage(new TabBookPage{}, "Second");
}

} // namespace

TEST_CASE("TabBook")
{
  auto tabBook = TabBook{};
  addTestPages(tabBook);
  tabBook.resize(400, 240);
  tabBook.show();
  QApplication::processEvents();

  auto* firstButton = tabButton(tabBook, "First");
  auto* secondButton = tabButton(tabBook, "Second");
  REQUIRE(firstButton != nullptr);
  REQUIRE(secondButton != nullptr);

  SECTION("switches pages with mouse and keyboard")
  {
    CHECK(tabBook.currentPageIndex() == 0);
    CHECK(firstButton->property("active").toBool());

    tabBook.activateWindow();
    secondButton->setFocus(Qt::OtherFocusReason);
    QApplication::processEvents();
    QTest::keyClick(secondButton, Qt::Key_Space);

    CHECK(tabBook.currentPageIndex() == 1);
    CHECK(secondButton->property("active").toBool());
    CHECK_FALSE(firstButton->property("active").toBool());

    QTest::mouseClick(firstButton, Qt::LeftButton);
    CHECK(tabBook.currentPageIndex() == 0);
  }

  SECTION("restores the current page")
  {
    tabBook.switchToPage(1);
    const auto state = tabBook.saveState();

    auto restoredTabBook = TabBook{};
    addTestPages(restoredTabBook);
    REQUIRE(restoredTabBook.restoreState(state));
    CHECK(restoredTabBook.currentPageIndex() == 1);
  }

  SECTION("external navigation removes empty action rows but retains page commands")
  {
    tabBook.addPage(new ActionPage{}, "Actions");
    tabBook.tabBar()->setNavigationVisible(false);
    QApplication::processEvents();
    CHECK_FALSE(tabBook.tabBar()->isVisible());
    tabBook.switchToPage(2);
    QApplication::processEvents();
    CHECK(tabBook.tabBar()->isVisible());
    CHECK(tabBook.tabBar()->findChild<QPushButton*>()->isVisible());
    tabBook.switchToPage(0);
    QApplication::processEvents();
    CHECK_FALSE(tabBook.tabBar()->isVisible());
    tabBook.tabBar()->setNavigationVisible(true);
    CHECK(tabBook.tabBar()->isVisible());
  }
}

TEST_CASE("TabBook lazy page replacement")
{
  auto tabBook = TabBook{};
  tabBook.addPage(new TabBookPage{}, "First");
  tabBook.addPlaceholderPage("Second");
  tabBook.addPlaceholderPage("Third");
  tabBook.resize(400, 240);
  tabBook.show();
  QApplication::processEvents();

  CHECK(tabBook.currentPageIndex() == 0);
  CHECK(tabBook.findChildren<TabBarButton*>().size() == 3);

  auto* second = new TabBookPage{};
  second->setObjectName("SecondPage");
  tabBook.replacePage(1, second);
  CHECK(tabBook.currentPageIndex() == 0);
  CHECK(tabBook.findChild<QWidget*>("SecondPage") == second);

  tabBook.switchToPage(2);
  QApplication::processEvents();
  CHECK(tabBook.currentPageIndex() == 2);

  // Replacing the page that is currently displayed must keep it displayed.
  auto* third = new TabBookPage{};
  third->setObjectName("ThirdPage");
  tabBook.replacePage(2, third);
  CHECK(tabBook.currentPageIndex() == 2);
  CHECK(tabBook.findChild<QWidget*>("ThirdPage") == third);
  CHECK(tabBook.findChildren<TabBarButton*>().size() == 3);
}

} // namespace tb::ui
