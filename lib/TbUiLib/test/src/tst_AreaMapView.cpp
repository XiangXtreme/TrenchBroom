/*
 Copyright (C) 2026 XiangXtreme
 SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <QApplication>
#include <QFocusEvent>
#include <QMouseEvent>
#include <QScopeGuard>
#include <QSettings>
#include <QSplitter>
#include <QtTest/QTest>

#include "base/PreferenceManager.h"
#include "base/Result.h"
#include "gl/GlManager.h"
#include "gl/ResourceManager.h"
#include "mdl/GameConfigFixture.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "prefs/Preferences.h"
#include "ui/AppControllerFixture.h"
#include "ui/AreaMapView.h"
#include "ui/CyclingMapView.h"
#include "ui/MapDocument.h"
#include "ui/MapViewBase.h"
#include "ui/MapWindow.h"
#include "ui/SwitchableMapViewContainer.h"

#include <algorithm>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
TEST_CASE("AreaMapView")
{
  const auto applicationName = QApplication::applicationName();
  QApplication::setApplicationName(
    QString{"TrenchBroom-AreaTest-%1"}.arg(QCoreApplication::applicationPid()));
  QSettings{}.clear();
  const auto restoreSettings = qScopeGuard([&]() {
    QSettings{}.clear();
    QApplication::setApplicationName(applicationName);
  });
  setPref(Preferences::DefaultPluginPaths, "");
  setPref(Preferences::PythonPluginDirectories, "");
  auto fixture = AppControllerFixture{};
  auto& app = fixture.appController();
  auto document = std::shared_ptr<MapDocument>{
    MapDocument::createDocument(
      app.environmentConfig(),
      mdl::QuakeGameInfo,
      mdl::MapFormat::Valve,
      vm::bbox3d{8192.0},
      app.taskManager(),
      app.glManager().resourceManager())
    | kdl::value()};
  auto window = MapWindow{app, document};
  window.resize(1400, 1000);
  window.show();
  auto* container = window.findChild<SwitchableMapViewContainer*>();
  REQUIRE(container);
  container->switchToMapView(MapViewLayout::OnePane);
  QApplication::processEvents();
  auto* layout = window.findChild<AreaMapView*>();
  REQUIRE(layout);
  auto* original = layout->areas().front();
  auto* native = original->firstMapViewBase();
  native->camera().moveTo(vm::vec3f{128, 256, 512});
  const auto position = native->camera().position();

  SECTION("nested splits and joins preserve surviving views and cameras")
  {
    REQUIRE(layout->splitArea(original, Qt::Horizontal));
    QApplication::processEvents();
    REQUIRE(layout->areas().size() == 2);
    auto* other =
      layout->areas()[0] == original ? layout->areas()[1] : layout->areas()[0];
    CHECK(other->firstMapViewBase()->camera().position() == position);
    CHECK(original->firstMapViewBase() == native);
    REQUIRE(layout->splitArea(other, Qt::Vertical));
    QApplication::processEvents();
    REQUIRE(layout->areas().size() == 3);
    CHECK_FALSE(layout->canJoinAreas(original, other));
    auto* bottom = original;
    for (auto* area : layout->areas())
      if (area != original && area != other)
        bottom = area;
    auto removed = QPointer<CyclingMapView>{bottom};
    REQUIRE(layout->joinAreas(bottom, other));
    QApplication::processEvents();
    CHECK(removed.isNull());
    REQUIRE(layout->joinAreas(other, original));
    QApplication::processEvents();
    CHECK(layout->areas().size() == 1);
    CHECK(original->firstMapViewBase() == native);
    CHECK(native->camera().position() == position);
    original->cycleChildMapView(native);
    original->firstMapViewBase()->camera().setZoom(2.0f);
    layout->refreshViews();
    CHECK_FALSE(document->map().modified());
  }

  SECTION("four panes join across branches and retain a perspective fallback")
  {
    container->switchToMapView(MapViewLayout::FourPanes);
    QApplication::processEvents();
    layout = window.findChild<AreaMapView*>();
    const auto perspectivePosition = layout->perspectiveCamera().position();
    auto sorted = layout->areas();
    std::ranges::sort(sorted, {}, [&](auto* area) {
      const auto p = area->mapTo(layout, QPoint{});
      return p.y() * layout->width() + p.x();
    });
    REQUIRE(sorted.size() == 4);
    REQUIRE(layout->canJoinAreas(sorted[0], sorted[1]));
    auto* survivor = sorted[1]->firstMapViewBase();
    REQUIRE(layout->joinAreas(sorted[0], sorted[1]));
    QApplication::processEvents();
    CHECK(layout->areas().size() == 3);
    CHECK(sorted[1]->firstMapViewBase() == survivor);
    CHECK(layout->perspectiveCamera().position() == perspectivePosition);
    REQUIRE(layout->joinAreas(sorted[2], sorted[3]));
    QApplication::processEvents();
    REQUIRE(layout->joinAreas(sorted[3], sorted[1]));
    QApplication::processEvents();
    CHECK(layout->areas().size() == 1);
    CHECK(sorted[1]->firstMapViewBase() == survivor);
    survivor->camera().setZoom(1.5f);
  }

  SECTION("corner drag previews, cancels and commits")
  {
    auto* grip = original->findChild<QWidget*>("MapViewAreaGrip");
    REQUIRE(grip);
    const auto drag = [&](QEvent::Type type, QPoint global) {
      auto event = QMouseEvent{
        type,
        QPointF{grip->mapFromGlobal(global)},
        QPointF{global},
        type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
        type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
        Qt::NoModifier};
      QApplication::sendEvent(grip, &event);
    };
    const auto start = grip->mapToGlobal(grip->rect().center());
    const auto middle = original->mapToGlobal(original->rect().center());
    drag(QEvent::MouseButtonPress, start);
    drag(QEvent::MouseMove, middle);
    auto* preview = layout->findChild<QWidget*>("MapViewAreaPreview");
    REQUIRE(preview);
    CHECK(preview->isVisible());
    QTest::keyClick(grip, Qt::Key_Escape);
    CHECK_FALSE(preview->isVisible());
    drag(QEvent::MouseButtonRelease, middle);
    QApplication::processEvents();
    CHECK(layout->areas().size() == 1);
    drag(QEvent::MouseButtonPress, start);
    drag(QEvent::MouseMove, middle);
    drag(QEvent::MouseButtonRelease, middle);
    QApplication::processEvents();
    REQUIRE(layout->areas().size() == 2);
    auto* other =
      layout->areas()[0] == original ? layout->areas()[1] : layout->areas()[0];
    grip = original->findChild<QWidget*>("MapViewAreaGrip");
    drag(QEvent::MouseButtonPress, grip->mapToGlobal(grip->rect().center()));
    const auto target = other->mapToGlobal(other->rect().center());
    drag(QEvent::MouseMove, target);
    CHECK(preview->isVisible());
    CHECK(preview->property("join").toBool());
    drag(QEvent::MouseButtonRelease, target);
    QApplication::processEvents();
    CHECK(layout->areas().size() == 1);
    CHECK(layout->areas().front() == other);
  }

  SECTION("layout restoration, maximization, preset reset and invalid settings")
  {
    REQUIRE(layout->splitArea(original, Qt::Vertical, 0.4));
    QApplication::processEvents();
    auto* splitter = qobject_cast<QSplitter*>(original->parentWidget());
    REQUIRE(splitter);
    const auto sizes = splitter->sizes();
    auto focus = QFocusEvent{QEvent::FocusIn};
    QApplication::sendEvent(native, &focus);
    layout->toggleMaximizeCurrentView();
    CHECK(layout->currentViewMaximized());
    CHECK_FALSE(layout->splitArea(original, Qt::Horizontal));
    layout->toggleMaximizeCurrentView();
    CHECK_FALSE(layout->currentViewMaximized());
    QApplication::processEvents();
    CHECK(splitter->sizes() == sizes);
    container->switchToMapView(MapViewLayout::OnePane, true);
    QApplication::processEvents();
    CHECK(window.findChild<AreaMapView*>()->areas().size() == 2);
    container->switchToMapView(MapViewLayout::OnePane);
    QApplication::processEvents();
    layout = window.findChild<AreaMapView*>();
    CHECK(layout->areas().size() == 1);
    CHECK_FALSE(layout->splitArea(layout->areas().front(), Qt::Horizontal, -1));
    CHECK_FALSE(layout->joinAreas(nullptr, layout->areas().front()));
    container->switchToMapView(MapViewLayout::TwoPanes);
    QSettings{}.setValue("Windows/MapViewAreas/0", R"({"version":1,"root":{"mask":0}})");
    container->switchToMapView(MapViewLayout::OnePane, true);
    CHECK(window.findChild<AreaMapView*>()->areas().size() == 1);
  }
}
} // namespace tb::ui
