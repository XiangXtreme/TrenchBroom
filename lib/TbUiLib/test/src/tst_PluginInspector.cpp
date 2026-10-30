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
#include <QLabel>
#include <QToolButton>
#include <QtTest/QTest>

#include "ui/PluginInspector.h"

#include <algorithm>
#include <memory>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{
std::vector<QWidget*> pluginPanels(PluginInspector& inspector)
{
  auto result = std::vector<QWidget*>{};
  for (auto* widget : inspector.findChildren<QWidget*>())
  {
    if (widget->objectName() == QStringLiteral("PluginInspector_PluginPanel"))
    {
      result.push_back(widget);
    }
  }
  return result;
}

bool hasEmptyPluginInfo(PluginInspector& inspector)
{
  const auto labels = inspector.findChildren<QLabel*>();
  return std::ranges::any_of(labels, [](const auto* label) {
    return label->text().contains(QStringLiteral("No plugin panels loaded"));
  });
}
} // namespace

TEST_CASE("PluginInspector")
{
  auto inspector = PluginInspector{};

  SECTION("adds and closes plugin panels")
  {
    CHECK(hasEmptyPluginInfo(inspector));
    CHECK(pluginPanels(inspector).empty());

    auto registration = inspector.addPluginPanel("Python Tools");
    auto* content = registration.content();
    REQUIRE(content != nullptr);
    QApplication::processEvents();

    CHECK_FALSE(hasEmptyPluginInfo(inspector));
    REQUIRE(pluginPanels(inspector).size() == 1u);

    auto* closeButton = inspector.findChild<QToolButton*>("PluginInspector_CloseButton");
    REQUIRE(closeButton != nullptr);
    QTest::mouseClick(closeButton, Qt::LeftButton);
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents();

    CHECK(pluginPanels(inspector).empty());
    CHECK(hasEmptyPluginInfo(inspector));
    CHECK(registration.content() == nullptr);
    registration.close();
  }

  SECTION("unregisters only the requested panel and restores the empty state")
  {
    auto first = inspector.addPluginPanel("First");
    auto second = inspector.addPluginPanel("Second");
    auto secondContent = QPointer<QWidget>{second.content()};
    // Cleanup must not depend on names or parent discovery.
    for (auto* panel : pluginPanels(inspector))
    {
      panel->setObjectName("Renamed");
    }

    first.close();
    CHECK(first.content() == nullptr);
    REQUIRE(secondContent != nullptr);
    CHECK_FALSE(hasEmptyPluginInfo(inspector));
    first.close();
    second.close();
    CHECK(secondContent == nullptr);
    CHECK(hasEmptyPluginInfo(inspector));
    CHECK(inspector.addPluginPanel("Reloaded").content() != nullptr);
    CHECK_FALSE(hasEmptyPluginInfo(inspector));
  }

  SECTION("registrations safely outlive their host")
  {
    auto host = std::make_unique<PluginInspector>();
    auto registration = host->addPluginPanel("Tool");
    host.reset();
    CHECK(registration.content() == nullptr);
    registration.close();
  }

  SECTION("session cleanup completes a pending user close")
  {
    auto registration = inspector.addPluginPanel("Tool");
    auto* closeButton = inspector.findChild<QToolButton*>("PluginInspector_CloseButton");
    REQUIRE(closeButton != nullptr);
    closeButton->click();
    registration.close();
    CHECK(registration.content() == nullptr);
    CHECK(hasEmptyPluginInfo(inspector));
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    CHECK(pluginPanels(inspector).empty());
  }
}

} // namespace tb::ui
