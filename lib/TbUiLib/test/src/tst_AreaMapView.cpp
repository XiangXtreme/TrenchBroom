/*
 Copyright (C) 2026 XiangXtreme
 SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <QApplication>
#include <QFocusEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QScopeGuard>
#include <QSettings>
#include <QToolButton>
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
#include "ui/MapView3D.h"
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

  SECTION("3D lightmap buttons follow the shared preview setting")
  {
    const auto savedSetting = pref(Preferences::ShowLightmap);
    const auto restoreSetting =
      qScopeGuard([&] { setPref(Preferences::ShowLightmap, savedSetting); });
    setPref(Preferences::ShowLightmap, false);
    auto* selector = original->findChild<QToolButton*>("MapViewTypeButton");
    auto* button = original->findChild<QToolButton*>("MapViewLightmapButton");
    REQUIRE(selector);
    REQUIRE(button);
    CHECK_FALSE(button->isHidden());
    CHECK_FALSE(button->isChecked());
    CHECK(selector->x() == selector->y());
    CHECK(button->x() > selector->geometry().right());
    CHECK(button->y() == selector->y());
    CHECK(button->height() == selector->height());

    REQUIRE(layout->splitArea(original, Qt::Horizontal));
    auto* second = layout->areas().back();
    REQUIRE(second != original);
    auto* otherButton = second->findChild<QToolButton*>("MapViewLightmapButton");
    REQUIRE(otherButton);
    second->setViewType(CyclingMapView::View_XZ);
    CHECK(otherButton->isHidden());
    button->click();
    CHECK(pref(Preferences::ShowLightmap));
    second->setViewType(CyclingMapView::View_3D);
    CHECK_FALSE(otherButton->isHidden());
    CHECK(otherButton->isChecked());
    setPref(Preferences::ShowLightmap, false);
    CHECK_FALSE(button->isChecked());
    CHECK_FALSE(otherButton->isChecked());
  }

  SECTION("3D lightmap quality and bake status appear beside the toggle")
  {
    const auto savedQuality = pref(Preferences::LightmapQuality);
    const auto restoreQuality =
      qScopeGuard([&] { setPref(Preferences::LightmapQuality, savedQuality); });
    setPref(Preferences::LightmapQuality, Preferences::LightmapQualityNormal);
    auto* toggle = original->findChild<QToolButton*>("MapViewLightmapButton");
    auto* quality = original->findChild<QToolButton*>("MapViewLightmapQualityButton");
    REQUIRE(toggle);
    REQUIRE(quality);
    REQUIRE(quality->menu());
    REQUIRE(quality->menu()->actions().size() == 3);
    CHECK_FALSE(quality->isHidden());
    CHECK(quality->x() > toggle->geometry().right());
    CHECK(quality->y() == toggle->y());
    CHECK(quality->text() == "Normal");
    CHECK(quality->menu()->actions()[1]->isChecked());
    quality->menu()->actions()[2]->trigger();
    CHECK(pref(Preferences::LightmapQuality) == Preferences::LightmapQualityUltra);
    CHECK(quality->text() == "Ultra");
    emit native->lightmapBakeProgressChanged(true, 65);
    CHECK(quality->text() == "Ultra · ~1:05");
    emit native->lightmapBakeProgressChanged(false, -1);
    CHECK(quality->text() == "Ultra");
    original->setViewType(CyclingMapView::View_XZ);
    CHECK(quality->isHidden());
  }

  SECTION("each area can select every view without losing cameras or changing the map")
  {
    auto* button = original->findChild<QToolButton*>("MapViewTypeButton");
    REQUIRE(button);
    REQUIRE(button->menu());
    REQUIRE(button->menu()->actions().size() == 4);
    for (auto* action : button->menu()->actions())
    {
      action->trigger();
      CHECK(original->viewType() == action->data().toInt());
      CHECK(action->isChecked());
      CHECK(button->text() == action->text());
    }
    original->setViewType(CyclingMapView::View_3D);
    CHECK(original->firstMapViewBase() == native);
    CHECK(native->camera().position() == position);
    original->setViewType(CyclingMapView::View_XZ);
    CHECK(layout->perspectiveCamera().position() == position);
    REQUIRE(layout->splitArea(original, Qt::Horizontal));
    for (auto* area : layout->areas())
    {
      CHECK(area->viewType() == CyclingMapView::View_XZ);
      area->setViewType(CyclingMapView::View_3D);
      CHECK(area->firstMapViewBase()->camera().position() == position);
    }
    container->switchToMapView(MapViewLayout::TwoPanes);
    QApplication::processEvents();
    layout = window.findChild<AreaMapView*>();
    REQUIRE(layout->areas().size() == 2);
    for (auto* area : layout->areas())
    {
      area->setViewType(CyclingMapView::View_YZ);
      CHECK(area->viewType() == CyclingMapView::View_YZ);
      area->setViewType(CyclingMapView::View_3D);
      CHECK(area->viewType() == CyclingMapView::View_3D);
    }
    for (auto* area : layout->areas())
    {
      auto* view = area->firstMapViewBase();
      view->camera().moveTo(vm::vec3f{float(area->x()), 32, 64});
      auto focus = QFocusEvent{QEvent::FocusIn};
      QApplication::sendEvent(view, &focus);
      Q_EMIT qApp->focusChanged(nullptr, view);
      CHECK(layout->perspectiveCamera().position() == view->camera().position());
      CHECK(&window.activate3DMapView() == view);
    }
    container->switchToMapView(MapViewLayout::TwoPanes, true);
    QApplication::processEvents();
    for (auto* area : window.findChild<AreaMapView*>()->areas())
      CHECK(area->viewType() == CyclingMapView::View_3D);
    CHECK_FALSE(document->map().modified());
  }

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
    CHECK(layout->canJoinAreas(original, other));
    auto* bottom = original;
    for (auto* area : layout->areas())
      if (area != original && area != other)
        bottom = area;
    auto removed = QPointer<CyclingMapView>{other};
    REQUIRE(layout->joinAreas(bottom, other));
    QApplication::processEvents();
    CHECK(removed.isNull());
    REQUIRE(layout->joinAreas(original, bottom));
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
    auto* survivor = sorted[0]->firstMapViewBase();
    REQUIRE(layout->joinAreas(sorted[0], sorted[1]));
    QApplication::processEvents();
    CHECK(layout->areas().size() == 3);
    CHECK(sorted[0]->firstMapViewBase() == survivor);
    CHECK(layout->perspectiveCamera().position() == perspectivePosition);
    REQUIRE(layout->joinAreas(sorted[2], sorted[3]));
    QApplication::processEvents();
    REQUIRE(layout->joinAreas(sorted[0], sorted[2]));
    QApplication::processEvents();
    CHECK(layout->areas().size() == 1);
    CHECK(sorted[0]->firstMapViewBase() == survivor);
    survivor->camera().setZoom(1.5f);
  }

  SECTION("partial joins preserve the source and copy target remainder cameras")
  {
    REQUIRE(layout->splitArea(original, Qt::Horizontal));
    auto* other =
      layout->areas()[0] == original ? layout->areas()[1] : layout->areas()[0];
    REQUIRE(layout->splitArea(other, Qt::Vertical));
    QApplication::processEvents();
    const auto source = QPointer<CyclingMapView>{other};
    const auto removed = QPointer<CyclingMapView>{original};
    auto* sourceNative = other->firstMapViewBase();
    const auto sourcePosition = sourceNative->camera().position();
    native->setFocus();
    REQUIRE(layout->joinAreas(other, original));
    QApplication::processEvents();
    REQUIRE(source);
    CHECK(removed.isNull());
    CHECK(source->firstMapViewBase() == sourceNative);
    CHECK(sourceNative->camera().position() == sourcePosition);
    CHECK(layout->areaBounds(source) == QRectF{0, 0, 1, 0.5});
    const auto feedback =
      QPointer<QWidget>{layout->findChild<QWidget*>("MapViewAreaFeedback")};
    REQUIRE(feedback);
    REQUIRE(QTest::qWaitFor([&]() { return feedback.isNull(); }, 2000));
    REQUIRE(layout->areas().size() == 3);
    for (auto* area : layout->areas())
      if (layout->areaBounds(area) == QRectF{0, 0.5, 0.5, 0.5})
        CHECK(area->firstMapViewBase()->camera().position() == position);
    layout->refreshViews();
    CHECK_FALSE(document->map().modified());
  }

  SECTION("nonadjacent docking retains native source and closes its old location")
  {
    REQUIRE(layout->splitArea(original, Qt::Horizontal));
    auto* right =
      layout->areas()[0] == original ? layout->areas()[1] : layout->areas()[0];
    REQUIRE(layout->splitArea(original, Qt::Vertical));
    REQUIRE(layout->splitArea(right, Qt::Vertical));
    QApplication::processEvents();
    auto* target = original;
    for (auto* area : layout->areas())
      if (layout->areaBounds(area) == QRectF{0.5, 0.5, 0.5, 0.5})
        target = area;
    REQUIRE(target != original);
    const auto removed = QPointer<CyclingMapView>{target};
    target->firstMapViewBase()->setFocus();
    REQUIRE(layout->dockArea(original, target, areaGeometry::Dock::Center));
    QApplication::processEvents();
    CHECK(removed.isNull());
    CHECK(layout->areas().size() == 3);
    CHECK(layout->areaBounds(original) == QRectF{0.5, 0.5, 0.5, 0.5});
    CHECK(original->firstMapViewBase() == native);
    CHECK(native->camera().position() == position);
  }

  SECTION("shared edges resize connected regions and Escape restores the mosaic")
  {
    REQUIRE(layout->splitArea(original, Qt::Horizontal));
    auto* other =
      layout->areas()[0] == original ? layout->areas()[1] : layout->areas()[0];
    REQUIRE(layout->splitArea(other, Qt::Vertical, 0.7));
    QApplication::processEvents();
    const auto findVertical = [&]() -> QWidget* {
      for (auto* candidate : layout->findChildren<QWidget*>("MapViewAreaEdge"))
        if (candidate->property("axis").toInt() == 0)
          return candidate;
      return nullptr;
    };
    auto* edge = findVertical();
    REQUIRE(edge);
    const auto bounds = layout->areaBounds(original);
    const auto start = edge->mapToGlobal(edge->rect().center());
    const auto sendMouse = [&](const QEvent::Type type, const QPoint global) {
      auto event = QMouseEvent{
        type,
        QPointF{edge->mapFromGlobal(global)},
        QPointF{global},
        type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
        type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
        Qt::NoModifier};
      QApplication::sendEvent(edge, &event);
    };
    sendMouse(QEvent::MouseButtonPress, start);
    sendMouse(QEvent::MouseMove, start + QPoint{80, 0});
    CHECK(layout->areaBounds(original).width() > bounds.width());
    for (auto* candidate : layout->findChildren<QWidget*>("MapViewAreaEdge"))
      if (candidate->property("axis").toInt() == 1)
        CHECK(candidate->x() > edge->x());
    QTest::keyClick(edge, Qt::Key_Escape);
    CHECK(layout->areaBounds(original) == bounds);
    QApplication::processEvents();
    edge = findVertical();
    REQUIRE(edge);
    sendMouse(QEvent::MouseButtonPress, start);
    sendMouse(QEvent::MouseMove, start + QPoint{40, 0});
    sendMouse(QEvent::MouseButtonRelease, start + QPoint{40, 0});
    QApplication::processEvents();
    CHECK(layout->areaBounds(original).width() > bounds.width());
    CHECK(original->firstMapViewBase() == native);
    CHECK(native->camera().position() == position);
  }

  SECTION("four-pane intersection moves boundaries in both directions simultaneously")
  {
    container->switchToMapView(MapViewLayout::FourPanes);
    QApplication::processEvents();
    layout = window.findChild<AreaMapView*>();
    REQUIRE(layout);
    REQUIRE(layout->areas().size() == 4);

    auto* junction = layout->findChild<QWidget*>("MapViewAreaIntersection");
    REQUIRE(junction);
    CHECK(junction->cursor().shape() == Qt::SizeAllCursor);

    auto sorted = layout->areas();
    std::ranges::sort(sorted, {}, [&](auto* area) {
      const auto p = area->mapTo(layout, QPoint{});
      return p.y() * layout->width() + p.x();
    });
    REQUIRE(sorted.size() == 4);
    const auto topLeftBounds = layout->areaBounds(sorted[0]);

    const auto start = junction->mapToGlobal(junction->rect().center());
    const auto sendMouse = [&](const QEvent::Type type, const QPoint global) {
      auto event = QMouseEvent{
        type,
        QPointF{junction->mapFromGlobal(global)},
        QPointF{global},
        type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
        type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
        Qt::NoModifier};
      QApplication::sendEvent(junction, &event);
    };

    sendMouse(QEvent::MouseButtonPress, start);
    sendMouse(QEvent::MouseMove, start + QPoint{60, 40});
    CHECK(layout->areaBounds(sorted[0]).width() > topLeftBounds.width());
    CHECK(layout->areaBounds(sorted[0]).height() > topLeftBounds.height());
    QTest::keyClick(junction, Qt::Key_Escape);
    CHECK(layout->areaBounds(sorted[0]) == topLeftBounds);
    QApplication::processEvents();

    junction = layout->findChild<QWidget*>("MapViewAreaIntersection");
    REQUIRE(junction);
    sendMouse(QEvent::MouseButtonPress, start);
    sendMouse(QEvent::MouseMove, start + QPoint{40, 30});
    sendMouse(QEvent::MouseButtonRelease, start + QPoint{40, 30});
    QApplication::processEvents();
    CHECK(layout->areaBounds(sorted[0]).width() > topLeftBounds.width());
    CHECK(layout->areaBounds(sorted[0]).height() > topLeftBounds.height());
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
    // A release alone must not synthesize a preview and create an unseen split.
    drag(QEvent::MouseButtonPress, start);
    drag(QEvent::MouseButtonRelease, middle);
    QApplication::processEvents();
    REQUIRE(layout->areas().size() == 1);
    drag(QEvent::MouseButtonPress, start);
    drag(QEvent::MouseMove, start - QPoint{QApplication::startDragDistance() + 1, 0});
    auto* preview = layout->findChild<QWidget*>("MapViewAreaPreview");
    REQUIRE(preview);
    REQUIRE(preview->isVisible());
    CHECK(preview->property("split").toBool());
    CHECK(preview->property("splitHorizontal").toBool());
    CHECK(preview->geometry().size() == original->size());
    CHECK(layout->areas().size() == 1);
    CHECK(original->findChildren<MapViewBase*>().size() == 4);
    drag(QEvent::MouseMove, middle);
    CHECK(preview->property("splitRatio").toDouble() == 0.5);
    CHECK(preview->property("label").toString().contains("50% / 50%"));
    QTest::keyClick(grip, Qt::Key_Escape);
    CHECK_FALSE(preview->isVisible());
    drag(QEvent::MouseButtonRelease, middle);
    QApplication::processEvents();
    CHECK(layout->areas().size() == 1);
    drag(QEvent::MouseButtonPress, start);
    drag(QEvent::MouseMove, start + QPoint{0, original->height() / 2});
    REQUIRE(preview->isVisible());
    CHECK_FALSE(preview->property("splitHorizontal").toBool());
    CHECK(layout->areas().size() == 1);
    QTest::keyClick(grip, Qt::Key_Escape);
    drag(QEvent::MouseButtonPress, start);
    drag(QEvent::MouseMove, middle);
    drag(QEvent::MouseButtonRelease, middle);
    QApplication::processEvents();
    REQUIRE(layout->areas().size() == 2);
    auto* other =
      layout->areas()[0] == original ? layout->areas()[1] : layout->areas()[0];
    grip = original->findChild<QWidget*>("MapViewAreaGrip");
    drag(QEvent::MouseButtonPress, grip->mapToGlobal(grip->rect().center()));
    const auto target = other->mapToGlobal(QPoint{8, other->height() / 2});
    drag(QEvent::MouseMove, target);
    CHECK(preview->isVisible());
    CHECK(preview->property("join").toBool());
    CHECK_FALSE(preview->property("split").toBool());
    drag(QEvent::MouseButtonRelease, target);
    QApplication::processEvents();
    CHECK(layout->areas().size() == 1);
    CHECK(layout->areas().front() == original);
  }

  SECTION("four corner grips support splitting and joining from any corner")
  {
    const auto grips = original->findChildren<QWidget*>("MapViewAreaGrip");
    REQUIRE(grips.size() == 4);

    auto* topLeftGrip = static_cast<QWidget*>(nullptr);
    auto* topRightGrip = static_cast<QWidget*>(nullptr);
    auto* bottomLeftGrip = static_cast<QWidget*>(nullptr);
    auto* bottomRightGrip = static_cast<QWidget*>(nullptr);

    for (auto* g : grips)
    {
      if (g->x() == 0 && g->y() == 0)
        topLeftGrip = g;
      else if (g->x() > 0 && g->y() == 0)
        topRightGrip = g;
      else if (g->x() == 0 && g->y() > 0)
        bottomLeftGrip = g;
      else if (g->x() > 0 && g->y() > 0)
        bottomRightGrip = g;
    }

    REQUIRE(topLeftGrip);
    REQUIRE(topRightGrip);
    REQUIRE(bottomLeftGrip);
    REQUIRE(bottomRightGrip);

    CHECK(topLeftGrip->pos() == QPoint(0, 0));
    CHECK(topRightGrip->pos() == QPoint(original->width() - topRightGrip->width(), 0));
    CHECK(
      bottomLeftGrip->pos() == QPoint(0, original->height() - bottomLeftGrip->height()));
    CHECK(
      bottomRightGrip->pos()
      == QPoint(
        original->width() - bottomRightGrip->width(),
        original->height() - bottomRightGrip->height()));

    auto* preview = layout->findChild<QWidget*>("MapViewAreaPreview");
    REQUIRE(preview);

    const auto dragFrom = [&](QWidget* grip, const QPoint targetGlobal) {
      const auto start = grip->mapToGlobal(grip->rect().center());
      auto press = QMouseEvent{
        QEvent::MouseButtonPress,
        QPointF{grip->rect().center()},
        QPointF{start},
        Qt::LeftButton,
        Qt::LeftButton,
        Qt::NoModifier};
      QApplication::sendEvent(grip, &press);
      auto move = QMouseEvent{
        QEvent::MouseMove,
        QPointF{grip->mapFromGlobal(targetGlobal)},
        QPointF{targetGlobal},
        Qt::NoButton,
        Qt::LeftButton,
        Qt::NoModifier};
      QApplication::sendEvent(grip, &move);
      auto release = QMouseEvent{
        QEvent::MouseButtonRelease,
        QPointF{grip->mapFromGlobal(targetGlobal)},
        QPointF{targetGlobal},
        Qt::LeftButton,
        Qt::NoButton,
        Qt::NoModifier};
      QApplication::sendEvent(grip, &release);
      QApplication::processEvents();
    };

    // Helper to find grip by corner location
    const auto findGrip = [&](const bool right, const bool bottom) -> QWidget* {
      for (auto* g : original->findChildren<QWidget*>("MapViewAreaGrip"))
      {
        const auto isRight = g->x() > 0;
        const auto isBottom = g->y() > 0;
        if (isRight == right && isBottom == bottom)
          return g;
      }
      return nullptr;
    };

    // 1. Split horizontally from Top-Left: new view is on the left
    dragFrom(topLeftGrip, topLeftGrip->mapToGlobal(QPoint(original->width() / 2, 0)));
    REQUIRE(layout->areas().size() == 2);
    auto* other =
      layout->areas()[0] == original ? layout->areas()[1] : layout->areas()[0];
    CHECK(layout->areaBounds(other).x() < layout->areaBounds(original).x());
    REQUIRE(layout->joinAreas(original, other));
    QApplication::processEvents();
    REQUIRE(layout->areas().size() == 1);

    // 2. Split vertically from Top-Left: new view is on the top
    topLeftGrip = findGrip(false, false);
    REQUIRE(topLeftGrip);
    dragFrom(topLeftGrip, topLeftGrip->mapToGlobal(QPoint(0, original->height() / 2)));
    REQUIRE(layout->areas().size() == 2);
    other = layout->areas()[0] == original ? layout->areas()[1] : layout->areas()[0];
    CHECK(layout->areaBounds(other).y() < layout->areaBounds(original).y());
    REQUIRE(layout->joinAreas(original, other));
    QApplication::processEvents();
    REQUIRE(layout->areas().size() == 1);

    // 3. Split vertically from Bottom-Right: new view is on the bottom
    bottomRightGrip = findGrip(true, true);
    REQUIRE(bottomRightGrip);
    dragFrom(
      bottomRightGrip, bottomRightGrip->mapToGlobal(QPoint(0, -original->height() / 2)));
    REQUIRE(layout->areas().size() == 2);
    other = layout->areas()[0] == original ? layout->areas()[1] : layout->areas()[0];
    CHECK(layout->areaBounds(other).y() > layout->areaBounds(original).y());
    REQUIRE(layout->joinAreas(original, other));
    QApplication::processEvents();
    REQUIRE(layout->areas().size() == 1);

    // 4. Split horizontally from Bottom-Left: new view is on the left
    bottomLeftGrip = findGrip(false, true);
    REQUIRE(bottomLeftGrip);
    dragFrom(
      bottomLeftGrip, bottomLeftGrip->mapToGlobal(QPoint(original->width() / 2, 0)));
    REQUIRE(layout->areas().size() == 2);
    other = layout->areas()[0] == original ? layout->areas()[1] : layout->areas()[0];
    CHECK(layout->areaBounds(other).x() < layout->areaBounds(original).x());
    REQUIRE(layout->joinAreas(original, other));
    QApplication::processEvents();
    REQUIRE(layout->areas().size() == 1);
    CHECK(layout->areas().front() == original);
  }

  SECTION("saved splitter trees migrate and overlapping rectangles are rejected")
  {
    container->switchToMapView(MapViewLayout::TwoPanes);
    QSettings{}.setValue(
      "Windows/MapViewAreas/0",
      R"({"version":1,"root":{"axis":1,"ratio":0.4,"first":{"mask":15,"active":0},"second":{"mask":14,"active":1}}})");
    container->switchToMapView(MapViewLayout::OnePane, true);
    QApplication::processEvents();
    layout = window.findChild<AreaMapView*>();
    REQUIRE(layout->areas().size() == 2);
    for (auto* area : layout->areas())
    {
      CHECK(
        (layout->areaBounds(area) == QRectF{0, 0, 1, 0.4}
         || layout->areaBounds(area) == QRectF{0, 0.4, 1, 0.6}));
      CHECK(
        area->viewType()
        == (layout->areaBounds(area).y() == 0 ? CyclingMapView::View_3D : CyclingMapView::View_XZ));
    }
    container->switchToMapView(MapViewLayout::TwoPanes);
    QSettings{}.setValue(
      "Windows/MapViewAreas/0",
      R"({"version":2,"areas":[{"mask":15,"active":0,"x":0,"y":0,"w":1,"h":1},{"mask":15,"active":0,"x":0,"y":0,"w":1,"h":1}]})");
    container->switchToMapView(MapViewLayout::OnePane, true);
    CHECK(window.findChild<AreaMapView*>()->areas().size() == 1);
  }

  SECTION("layout restoration, maximization, preset reset and invalid settings")
  {
    REQUIRE(layout->splitArea(original, Qt::Vertical, 0.4));
    QApplication::processEvents();
    const auto bounds = layout->areaBounds(original);
    auto focus = QFocusEvent{QEvent::FocusIn};
    QApplication::sendEvent(native, &focus);
    layout->toggleMaximizeCurrentView();
    CHECK(layout->currentViewMaximized());
    CHECK_FALSE(layout->splitArea(original, Qt::Horizontal));
    layout->toggleMaximizeCurrentView();
    CHECK_FALSE(layout->currentViewMaximized());
    QApplication::processEvents();
    CHECK(layout->areaBounds(original) == bounds);
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

  SECTION("four panes support unbinding and unifying center points")
  {
    container->switchToMapView(MapViewLayout::FourPanes);
    QApplication::processEvents();
    layout = window.findChild<AreaMapView*>();
    REQUIRE(layout != nullptr);
    REQUIRE(layout->areas().size() == 4);

    CHECK(layout->fourPaneCenterMode() == AreaMapView::FourPaneCenterMode::Unified);
    CHECK(layout->canUnbindCenters());
    CHECK_FALSE(layout->canUnifyCenters());

    auto intersections = window.findChildren<QWidget*>("MapViewAreaIntersection");
    REQUIRE(intersections.size() == 1);

    // Unbind horizontal dividers (left / right move independently)
    layout->unbindCenters(true);
    QApplication::processEvents();
    CHECK(layout->fourPaneCenterMode() == AreaMapView::FourPaneCenterMode::UnboundLR);
    CHECK(layout->canUnifyCenters());

    intersections = window.findChildren<QWidget*>("MapViewAreaIntersection");
    REQUIRE(intersections.size() == 2);
    CHECK(intersections[0]->y() != intersections[1]->y());

    // Unify centers back
    layout->unifyCenters();
    QApplication::processEvents();
    CHECK(layout->fourPaneCenterMode() == AreaMapView::FourPaneCenterMode::Unified);
    CHECK_FALSE(layout->canUnifyCenters());

    intersections = window.findChildren<QWidget*>("MapViewAreaIntersection");
    REQUIRE(intersections.size() == 1);

    // Unbind vertical dividers (top / bottom move independently)
    layout->unbindCenters(false);
    QApplication::processEvents();
    CHECK(layout->fourPaneCenterMode() == AreaMapView::FourPaneCenterMode::UnboundTB);
    CHECK(layout->canUnifyCenters());

    intersections = window.findChildren<QWidget*>("MapViewAreaIntersection");
    REQUIRE(intersections.size() == 2);
    CHECK(intersections[0]->x() != intersections[1]->x());

    // Unify centers back
    layout->unifyCenters();
    QApplication::processEvents();
    CHECK(layout->fourPaneCenterMode() == AreaMapView::FourPaneCenterMode::Unified);
    CHECK_FALSE(layout->canUnifyCenters());

    intersections = window.findChildren<QWidget*>("MapViewAreaIntersection");
    REQUIRE(intersections.size() == 1);
  }
}
} // namespace tb::ui
