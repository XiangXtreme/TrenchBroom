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

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCursor>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QScreen>
#include <QSplitter>
#include <QStatusBar>
#include <QStyleOptionViewItem>
#include <QTextEdit>
#include <QToolButton>
#include <QWidget>
#include <QtTest/QTest>

#include "TestEnvironment.h"
#include "base/PreferenceManager.h"
#include "base/Result.h"
#include "fs/TestEnvironment.h"
#include "gl/Camera.h"
#include "gl/GlManager.h"
#include "gl/Resource.h"
#include "gl/ResourceManager.h"
#include "gl/TestGl.h"
#include "gl/TestUtils.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityProperties.h"
#include "mdl/GameConfigFixture.h"
#include "mdl/Grid.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/PickResult.h"
#include "mdl/WorldNode.h"
#include "prefs/Preferences.h"
#include "ui/ActionExecutionContext.h"
#include "ui/AppControllerFixture.h"
#include "ui/CatchConfig.h"
#include "ui/CommandPaletteDialog.h"
#include "ui/InfoPanel.h"
#include "ui/InputEvent.h"
#include "ui/Inspector.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"
#include "ui/MapView2D.h"
#include "ui/MapView3D.h"
#include "ui/MapViewBase.h"
#include "ui/MapViewLayout.h"
#include "ui/MapViewToolBox.h"
#include "ui/MapWindow.h"
#include "ui/MoveObjectsTool.h"
#include "ui/PieMenu.h"
#include "ui/PlayModeFullScreen.h"
#include "ui/PythonConsole.h"
#include "ui/QPathUtils.h"
#include "ui/SpinControl.h"
#include "ui/SwitchableMapViewContainer.h"
#include "ui/TransformCard.h"
#include "ui/python/PythonScripting.h"

#include <utility>

#include <catch2/catch_test_macros.hpp>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace tb::ui
{
namespace
{

struct CurrentPathGuard
{
  explicit CurrentPathGuard(std::filesystem::path newCurrentPath)
    : m_oldCurrentPath{std::filesystem::current_path()}
  {
    std::filesystem::current_path(std::move(newCurrentPath));
  }

  ~CurrentPathGuard() { std::filesystem::current_path(m_oldCurrentPath); }

private:
  std::filesystem::path m_oldCurrentPath;
};

std::vector<QWidget*> pluginPanels(MapWindow& window)
{
  auto result = std::vector<QWidget*>{};
  for (auto* widget : window.findChildren<QWidget*>())
  {
    if (widget->objectName() == QStringLiteral("PluginInspector_PluginPanel"))
    {
      result.push_back(widget);
    }
  }
  return result;
}

void sendMouseMove(QWidget& widget, const QPoint& pos)
{
  auto event = QMouseEvent{
    QEvent::MouseMove,
    QPointF{pos},
    QPointF{widget.mapTo(widget.window(), pos)},
    QPointF{widget.mapToGlobal(pos)},
    Qt::NoButton,
    Qt::NoButton,
    Qt::NoModifier};
  QApplication::sendEvent(&widget, &event);
}

void sendMouseRelease(QWidget& widget, const QPoint& pos)
{
  auto event = QMouseEvent{
    QEvent::MouseButtonRelease,
    QPointF{pos},
    QPointF{widget.mapTo(widget.window(), pos)},
    QPointF{widget.mapToGlobal(pos)},
    Qt::LeftButton,
    Qt::NoButton,
    Qt::NoModifier};
  QApplication::sendEvent(&widget, &event);
}

void sendMousePress(QWidget& widget, const QPoint& pos)
{
  auto event = QMouseEvent{
    QEvent::MouseButtonPress,
    QPointF{pos},
    QPointF{widget.mapTo(widget.window(), pos)},
    QPointF{widget.mapToGlobal(pos)},
    Qt::LeftButton,
    Qt::LeftButton,
    Qt::NoModifier};
  QApplication::sendEvent(&widget, &event);
}

PieMenu* findVisiblePieMenu()
{
  for (auto* widget : QApplication::topLevelWidgets())
  {
    if (auto* menu = qobject_cast<PieMenu*>(widget); menu && menu->isVisible())
    {
      return menu;
    }
  }
  return nullptr;
}

void activateModeAction(MapWindow& window, MapViewBase& view, const bool selectMode)
{
  auto* action = window.findAction(
    selectMode ? "Menu/Edit/Tools/Select Mode" : "Menu/Edit/Tools/Create Brush Mode");
  REQUIRE(action);
  const auto key = selectMode ? Qt::Key_S : Qt::Key_B;
  CHECK(action->shortcuts().contains(QKeySequence{Qt::SHIFT | key}));
  if (qEnvironmentVariableIntValue("TB_TEST_SHOW_WINDOWS") == 1)
  {
    QTest::keyClick(&view, key, Qt::ShiftModifier);
  }
  else
  {
    // Native menu shortcut dispatch needs an active window on macOS. Background
    // tests exercise the same QAction callback without native focus ownership.
    action->trigger();
  }
  QApplication::processEvents();
  view.grabFramebuffer();
}

} // namespace

// Simple resource type for testing resource processing
struct TestResource
{
  void upload(gl::Gl&) const {}
  void drop(gl::Gl&) const {}
};

TEST_CASE("MapWindow")
{
  setPref(Preferences::DefaultPluginPaths, "");
  setPref(Preferences::PythonPluginDirectories, "");

  UNSCOPED_INFO("creating AppControllerFixture");
  auto appControllerFixture = AppControllerFixture{};
  auto& appController = appControllerFixture.appController();

  UNSCOPED_INFO("creating MapDocument");
  auto document = MapDocument::createDocument(
                    appController.environmentConfig(),
                    mdl::QuakeGameInfo,
                    mdl::MapFormat::Valve,
                    vm::bbox3d{8192.0},
                    appController.taskManager(),
                    appController.glManager().resourceManager())
                  | kdl::value();

  UNSCOPED_INFO("creating MapWindow");
  auto window = MapWindow{appController, std::shared_ptr<MapDocument>{std::move(document)}};
  UNSCOPED_INFO("MapWindow created");

  SECTION("transform confirmation returns focus to the last map view")
  {
    auto* container = window.findChild<SwitchableMapViewContainer*>();
    REQUIRE(container);
    container->switchToMapView(MapViewLayout::FourPanes);
    window.resize(1200, 900);
    window.show();
    window.switchToInspectorPage(InspectorPage::Outliner);
    auto& map = window.document().map();
    auto* entity =
      new mdl::EntityNode{mdl::Entity{{{"classname", "light"}, {"origin", "10 20 30"}}}};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {entity}}});
    mdl::selectNodes(map, {entity});
    QApplication::processEvents();
    auto* view = window.findChild<MapView2D*>();
    REQUIRE(view);
    view->setFocus();
    // Record the view explicitly as background windows do not get native activation.
    window.focusChange(nullptr, view);
    auto* card = window.findChild<TransformCard*>();
    REQUIRE(card);
    auto* x = card->findChild<SpinControl*>("TransformCard_PosX");
    REQUIRE(x);
    auto expectedX = 42.0;
    SECTION("enter")
    {
      x->setFocus(Qt::TabFocusReason);
      auto inputFocus = QFocusEvent{QEvent::FocusIn, Qt::TabFocusReason};
      QApplication::sendEvent(x, &inputFocus);
      x->findChild<QLineEdit*>()->setText("42");
      QTest::keyClick(x, Qt::Key_Return);
    }
    SECTION("horizontal drag release")
    {
      expectedX = 30.0;
      x->setFocus(Qt::MouseFocusReason);
      auto* edit = x->findChild<QLineEdit*>();
      const auto start = edit->mapToGlobal(edit->rect().center());
      const auto mouse = [&](QEvent::Type type, QPoint offset) {
        const auto release = type == QEvent::MouseButtonRelease;
        auto event = QMouseEvent{
          type,
          QPointF{edit->rect().center() + offset},
          QPointF{start + offset},
          type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
          release ? Qt::NoButton : Qt::LeftButton,
          Qt::NoModifier};
        QApplication::sendEvent(edit, &event);
      };
      mouse(QEvent::MouseButtonPress, {});
      mouse(QEvent::MouseMove, QPoint{20, 0});
      mouse(QEvent::MouseMove, QPoint{40, 0});
      mouse(QEvent::MouseButtonRelease, QPoint{40, 0});
    }
    CHECK(entity->entity().origin() == vm::vec3d{expectedX, 20, 30});
    CHECK(window.focusWidget() == view);
    CHECK(window.currentMapViewBase() == view);
    CHECK_FALSE(x->hasFocus());
    map.undoCommand();
    CHECK(entity->entity().origin() == vm::vec3d{10, 20, 30});
  }

  SECTION("2D base modes route drags and toggle with Shift S")
  {
    setPref(Preferences::Default2DSelectMode, true);
    auto* container = window.findChild<SwitchableMapViewContainer*>();
    REQUIRE(container);
    container->switchToMapView(MapViewLayout::FourPanes);
    window.resize(1200, 900);
    window.show();
    QApplication::processEvents();
    auto* view = window.findChild<MapView2D*>();
    REQUIRE(view);
    const auto showWindows = qEnvironmentVariableIntValue("TB_TEST_SHOW_WINDOWS") == 1;
    const auto originalCursorPos = QCursor::pos();
    const auto restoreCursor = qScopeGuard([&]() {
      if (showWindows)
      {
        QCursor::setPos(originalCursorPos);
      }
    });
    window.activateWindow();
    if (showWindows)
    {
      window.raise();
      REQUIRE(QTest::qWaitForWindowActive(&window));
      QCursor::setPos(view->mapToGlobal(view->rect().center()));
    }
    view->setFocus();
    QApplication::processEvents();
    QTest::mouseClick(view, Qt::LeftButton);
    REQUIRE(container->active());
    auto& map = window.document().map();
    auto& toolBox = window.toolBox();

    auto* selectAction = window.findAction("Menu/Edit/Tools/Select Mode");
    auto* brushAction = window.findAction("Menu/Edit/Tools/Create Brush Mode");
    REQUIRE(selectAction);
    REQUIRE(brushAction);
    toolBox.setSelectMode(false);
    activateModeAction(window, *view, true);
    CHECK(toolBox.selectModeActive());
    CHECK(selectAction->isChecked());
    CHECK_FALSE(brushAction->isChecked());
    activateModeAction(window, *view, true);
    CHECK(toolBox.brushCreationModeActive());
    CHECK_FALSE(selectAction->isChecked());
    CHECK(brushAction->isChecked());
    activateModeAction(window, *view, true);
    CHECK(toolBox.selectModeActive());
    CHECK(selectAction->isChecked());
    CHECK_FALSE(brushAction->isChecked());

    toolBox.togglePathTool();
    REQUIRE(toolBox.pathToolActive());
    activateModeAction(window, *view, true);
    CHECK_FALSE(toolBox.pathToolActive());
    CHECK(toolBox.selectModeActive());
    QApplication::processEvents();
    CHECK(selectAction->isChecked());

    auto* brush = new mdl::BrushNode{
      mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()}.createCube(
        32.0, "test")
      | kdl::value()};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {brush}}});
    const auto center = view->camera().project(vm::vec3f{0, 0, 0});
    const auto startX = center.x() - 60.0f;
    const auto startY = center.y() - 60.0f;
    const auto endX = center.x() + 60.0f;
    const auto endY = center.y() + 60.0f;
    const auto drag = [&](const QPoint& start, const QPoint& end) {
      CAPTURE(start.x(), start.y(), end.x(), end.y());
      if (!showWindows)
      {
        // Background tests drive the view's input processor synchronously; native
        // event delivery and paint scheduling are exercised with visible windows.
        const auto send = [&](
                            const MouseEvent::Type type,
                            const MouseEvent::Button button,
                            const QPoint& pos) {
          view->processEvent(MouseEvent{type, button, float(pos.x()), float(pos.y())});
        };
        send(MouseEvent::Type::Motion, MouseEvent::Button::None, start);
        send(MouseEvent::Type::Down, MouseEvent::Button::Left, start);
        send(MouseEvent::Type::DragStart, MouseEvent::Button::None, start);
        send(MouseEvent::Type::Drag, MouseEvent::Button::None, end);
        REQUIRE(toolBox.dragging());
        send(MouseEvent::Type::DragEnd, MouseEvent::Button::None, end);
        send(MouseEvent::Type::Up, MouseEvent::Button::Left, end);
        view->grabFramebuffer();
        CHECK_FALSE(toolBox.dragging());
        return;
      }
      sendMouseMove(*view, start);
      view->grabFramebuffer();
      sendMousePress(*view, start);
      sendMouseMove(*view, end);
      // Input is processed while rendering; force a frame on native windows too.
      view->grabFramebuffer();
      REQUIRE(toolBox.dragging());
      // InputEventRecorder discards drags shorter than 100 ms as accidental motion.
      QTest::qWait(110);
      sendMouseRelease(*view, end);
      // A pending widget composition can defer the input-processing paint beyond
      // one grabFramebuffer call. Drain frames until release is handled.
      QTRY_VERIFY_WITH_TIMEOUT(
        ([&]() {
          view->grabFramebuffer();
          return !toolBox.dragging();
        })(),
        2000);
    };
    const auto start = QPoint{qRound(startX), qRound(startY)};
    const auto end = QPoint{qRound(endX), qRound(endY)};
    const auto count = map.worldNode().defaultLayer()->childCount();
    drag(start, end);
    CHECK(map.selection().nodes == std::vector<mdl::Node*>{brush});
    CHECK(map.worldNode().defaultLayer()->childCount() == count);

    const auto brushBounds = brush->logicalBounds();
    mdl::deselectAll(map);
    QApplication::processEvents();
    view->grabFramebuffer();
    const auto deselectedCenter = view->camera().project(vm::vec3f{0, 0, 0});
    const auto brushCenter =
      QPoint{qRound(deselectedCenter.x()), qRound(deselectedCenter.y())};
    if (showWindows)
    {
      sendMouseMove(*view, brushCenter);
      view->grabFramebuffer();
    }
    else
    {
      view->processEvent(MouseEvent{
        MouseEvent::Type::Motion,
        MouseEvent::Button::None,
        float(brushCenter.x()),
        float(brushCenter.y())});
    }
    REQUIRE_FALSE(view->pickResult().empty());
    drag(brushCenter, brushCenter + QPoint{60, 60});
    CHECK(map.selection().nodes == std::vector<mdl::Node*>{brush});
    CHECK(brush->logicalBounds() == brushBounds);
    CHECK(map.worldNode().defaultLayer()->childCount() == count);
    REQUIRE(toolBox.moveObjectsTool().active());
    QApplication::processEvents();
    view->grabFramebuffer();
    const auto selectedCenter = view->camera().project(vm::vec3f{0, 0, 0});
    const auto moveStart = QPoint{qRound(selectedCenter.x()), qRound(selectedCenter.y())};
    drag(moveStart, moveStart + QPoint{60, 60});
    CHECK(brush->logicalBounds() != brushBounds);
    map.undoCommand();
    CHECK(brush->logicalBounds() == brushBounds);

    activateModeAction(window, *view, false);
    CHECK(brushAction->isChecked());
    CHECK_FALSE(selectAction->isChecked());
    // The tool page can resize the viewport; project again after switching modes.
    const auto creationCenter = view->camera().project(vm::vec3f{0, 0, 0});
    drag(
      QPoint{qRound(creationCenter.x() - 60.0f), qRound(creationCenter.y() - 60.0f)},
      QPoint{qRound(creationCenter.x() + 60.0f), qRound(creationCenter.y() + 60.0f)});
    CHECK(map.worldNode().defaultLayer()->childCount() == count + 1);
    CHECK(toolBox.brushCreationModeActive());
    view->cancel();
    CHECK(toolBox.brushCreationModeActive());
    CHECK_FALSE(map.selection().hasAny());
  }

  SECTION("Escape cancels tools and selection while preserving the base mode")
  {
    auto* container = window.findChild<SwitchableMapViewContainer*>();
    REQUIRE(container);
    container->switchToMapView(MapViewLayout::FourPanes);
    auto& map = window.document().map();
    auto& toolBox = window.toolBox();
    auto* brush = new mdl::BrushNode{
      mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()}.createCube(
        32.0, "test")
      | kdl::value()};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {brush}}});

    for (auto* view : std::vector<MapViewBase*>{
           window.findChild<MapView2D*>(), window.findChild<MapView3D*>()})
    {
      REQUIRE(view);
      for (const auto defaultSelectMode : {false, true})
      {
        CAPTURE(view->objectName(), defaultSelectMode);
        setPref(Preferences::Default2DSelectMode, defaultSelectMode);
        mdl::selectNodes(map, {brush});
        toolBox.setSelectMode(!defaultSelectMode);
        view->cancel();
        CHECK(toolBox.selectMode() != defaultSelectMode);
        CHECK_FALSE(map.selection().hasAny());
        view->cancel();
        CHECK_FALSE(map.selection().hasAny());
        CHECK(toolBox.selectMode() != defaultSelectMode);

        mdl::selectNodes(map, {brush});
        toolBox.togglePathTool();
        REQUIRE(toolBox.pathToolActive());
        view->cancel();
        CHECK_FALSE(toolBox.pathToolActive());
        CHECK(toolBox.selectMode() != defaultSelectMode);
        CHECK(map.selection().nodes == std::vector<mdl::Node*>{brush});
        view->cancel();
        CHECK(toolBox.selectMode() != defaultSelectMode);
        CHECK_FALSE(map.selection().hasAny());
      }
    }
  }

  SECTION("3D selection mode blocks brush creation after Shift S")
  {
    window.resize(1200, 900);
    window.show();
    QApplication::processEvents();
    auto* view = window.findChild<MapView3D*>();
    REQUIRE(view);
    const auto showWindows = qEnvironmentVariableIntValue("TB_TEST_SHOW_WINDOWS") == 1;
    const auto originalCursorPos = QCursor::pos();
    const auto restoreCursor = qScopeGuard([&]() {
      if (showWindows)
      {
        QCursor::setPos(originalCursorPos);
      }
    });
    window.activateWindow();
    if (showWindows)
    {
      window.raise();
      REQUIRE(QTest::qWaitForWindowActive(&window));
      QCursor::setPos(view->mapToGlobal(view->rect().center()));
    }
    view->setFocus();
    QApplication::processEvents();
    QTest::mouseClick(view, Qt::LeftButton);
    auto* container = window.findChild<SwitchableMapViewContainer*>();
    REQUIRE(container);
    REQUIRE(container->active());
    auto& map = window.document().map();
    auto& toolBox = window.toolBox();
    toolBox.setSelectMode(false);
    activateModeAction(window, *view, true);
    REQUIRE(toolBox.selectModeActive());
    activateModeAction(window, *view, true);
    REQUIRE(toolBox.brushCreationModeActive());
    activateModeAction(window, *view, true);
    REQUIRE(toolBox.selectModeActive());
    const auto count = map.worldNode().defaultLayer()->childCount();
    const auto start = view->rect().center();
    const auto end = start + QPoint{60, 60};
    sendMouseMove(*view, start);
    view->grabFramebuffer();
    sendMousePress(*view, start);
    sendMouseMove(*view, end);
    view->grabFramebuffer();
    CHECK_FALSE(toolBox.dragging());
    QTest::qWait(110);
    sendMouseRelease(*view, end);
    view->grabFramebuffer();
    CHECK(map.worldNode().defaultLayer()->childCount() == count);

    activateModeAction(window, *view, false);
    REQUIRE(toolBox.brushCreationModeActive());
    sendMousePress(*view, start);
    sendMouseMove(*view, end);
    view->grabFramebuffer();
    REQUIRE(toolBox.dragging());
    toolBox.cancelMouseDrag();
    sendMouseRelease(*view, end);
    view->grabFramebuffer();
    CHECK(map.worldNode().defaultLayer()->childCount() == count);
  }

  SECTION("3D right click drag suppresses Shift+S mode toggle")
  {
    window.resize(1200, 900);
    window.show();
    QApplication::processEvents();
    auto* view = window.findChild<MapView3D*>();
    REQUIRE(view);
    view->setFocus();
    QApplication::processEvents();

    auto& toolBox = window.toolBox();
    toolBox.setSelectMode(false);
    REQUIRE(toolBox.brushCreationModeActive());

    const auto center = view->rect().center();
    auto rightPress = QMouseEvent{
      QEvent::MouseButtonPress,
      QPointF{center},
      QPointF{view->mapTo(view->window(), center)},
      QPointF{view->mapToGlobal(center)},
      Qt::RightButton,
      Qt::RightButton,
      Qt::NoModifier};
    QApplication::sendEvent(view, &rightPress);

    auto overrideEvent =
      QKeyEvent{QEvent::ShortcutOverride, Qt::Key_S, Qt::ShiftModifier};
    QApplication::sendEvent(view, &overrideEvent);
    CHECK(overrideEvent.isAccepted());
    CHECK(toolBox.brushCreationModeActive());

    auto rightRelease = QMouseEvent{
      QEvent::MouseButtonRelease,
      QPointF{center},
      QPointF{view->mapTo(view->window(), center)},
      QPointF{view->mapToGlobal(center)},
      Qt::RightButton,
      Qt::NoButton,
      Qt::NoModifier};
    QApplication::sendEvent(view, &rightRelease);

    auto overrideEventAfterRelease =
      QKeyEvent{QEvent::ShortcutOverride, Qt::Key_S, Qt::ShiftModifier};
    QApplication::sendEvent(view, &overrideEventAfterRelease);
    CHECK_FALSE(overrideEventAfterRelease.isAccepted());
  }


  SECTION("uses modern workbench surfaces and status controls")
  {
    auto* editorSurface = window.findChild<QWidget*>("MapWindow_EditorSurface");
    auto* infoPanelSurface = window.findChild<QWidget*>("MapWindow_InfoPanelSurface");
    auto* inspectorSurface = window.findChild<QWidget*>("MapWindow_InspectorSurface");
    REQUIRE(editorSurface != nullptr);
    REQUIRE(infoPanelSurface != nullptr);
    REQUIRE(inspectorSurface != nullptr);

    auto* gridChoice = window.findChild<QComboBox*>("MapWindow_GridChoice");
    auto* snapToggle = window.findChild<QToolButton*>("MapWindow_SnapToggle");
    REQUIRE(gridChoice != nullptr);
    REQUIRE(snapToggle != nullptr);
    CHECK(gridChoice->parentWidget() == window.statusBar());
    CHECK(snapToggle->parentWidget() == window.statusBar());
    CHECK(snapToggle->isChecked() == window.document().map().grid().snap());

    QTest::mouseClick(snapToggle, Qt::LeftButton);
    CHECK_FALSE(window.document().map().grid().snap());
    CHECK_FALSE(snapToggle->isChecked());

    const auto infoPanelWasHidden = infoPanelSurface->isHidden();
    window.toggleInfoPanel();
    CHECK(infoPanelSurface->isHidden() != infoPanelWasHidden);
    window.toggleInfoPanel();
    CHECK(infoPanelSurface->isHidden() == infoPanelWasHidden);

    const auto inspectorWasHidden = inspectorSurface->isHidden();
    window.toggleInspector();
    CHECK(inspectorSurface->isHidden() != inspectorWasHidden);
    window.toggleInspector();
    CHECK(inspectorSurface->isHidden() == inspectorWasHidden);
  }

  SECTION("switches to the supporting Assets surface")
  {
    auto* infoPanel = window.findChild<InfoPanel*>("MapWindow_InfoPanel");
    REQUIRE(infoPanel != nullptr);

    auto* infoPanelTabBar = infoPanel->findChild<QWidget*>("InfoPanel_TabBar");
    REQUIRE(infoPanelTabBar != nullptr);
    CHECK(infoPanelTabBar->property("regionAnchorTabs").toBool());

    auto tabNames = QStringList{};
    for (auto* tabLabel : infoPanelTabBar->findChildren<QLabel*>("TabBarButtonLabel"))
    {
      tabNames << tabLabel->text();
    }
    CHECK(tabNames == QStringList{"CONSOLE", "PYTHON CONSOLE", "ISSUES", "ASSETS"});

    window.switchToInfoPanelPage(InfoPanelPage::Assets);

    CHECK(infoPanel->currentPage() == InfoPanelPage::Assets);
    CHECK_FALSE(window.findChild<QWidget*>("MapWindow_InfoPanelSurface")->isHidden());
    CHECK(window.findChild<QWidget*>("InfoPanel_Assets") != nullptr);
    CHECK(window.findChild<QWidget*>("ModelBrowser_Controls") != nullptr);
    CHECK(window.findChild<QWidget*>("ModelBrowser_FolderTree") != nullptr);
  }

  SECTION("runs commands from the Python console")
  {
    auto* infoPanel = window.findChild<InfoPanel*>("MapWindow_InfoPanel");
    REQUIRE(infoPanel != nullptr);
    auto* pythonConsole = infoPanel->pythonConsole();
    REQUIRE(pythonConsole != nullptr);

    auto* input = pythonConsole->findChild<QPlainTextEdit*>("PythonConsole_Input");
    auto* runButton = infoPanel->findChild<QToolButton*>("PythonConsole_Run");
    auto* output = pythonConsole->findChild<QTextEdit*>("PythonConsole_Output");
    REQUIRE(input != nullptr);
    REQUIRE(runButton != nullptr);
    REQUIRE(output != nullptr);
    REQUIRE_FALSE(runButton->isEnabled());

    input->setPlainText(QStringLiteral("console_value = 41"));
    REQUIRE(runButton->isEnabled());
    runButton->click();
    input->setPlainText(QStringLiteral("console_value + 1"));
    runButton->click();
    input->setPlainText(
      QStringLiteral("trenchbroom.documents.current().entities[0].classname"));
    runButton->click();

    QTRY_VERIFY_WITH_TIMEOUT(output->toPlainText().contains(QStringLiteral("42")), 500);
    CHECK(output->toPlainText().contains(QStringLiteral("'worldspawn'")));
  }

  SECTION("exposes styled inspector and browser sections")
  {
    CHECK(window.findChild<QWidget*>("FaceAttribsEditor") != nullptr);
    CHECK(window.findChild<QWidget*>("FaceAttribsEditor_Tools") != nullptr);
    CHECK(window.findChild<QWidget*>("FaceAttribsEditor_Attributes") != nullptr);
    CHECK(window.findChild<QWidget*>("FaceAttribsEditor_MaterialName") != nullptr);
    CHECK(window.findChild<QWidget*>("UvEditor") != nullptr);
    CHECK(window.findChild<QWidget*>("UvEditor_Toolbar") != nullptr);
    for (const auto& browserName : {QString{"EntityBrowser"}, QString{"MaterialBrowser"}})
    {
      auto* browser = window.findChild<QWidget*>(browserName);
      auto* controls =
        window.findChild<QWidget*>(browserName + QStringLiteral("_Controls"));
      auto* search =
        window.findChild<QLineEdit*>(browserName + QStringLiteral("_Search"));
      auto* filterRow =
        window.findChild<QWidget*>(browserName + QStringLiteral("_FilterRow"));
      auto* sort = window.findChild<QComboBox*>(browserName + QStringLiteral("_Sort"));
      auto* iconSize =
        window.findChild<QComboBox*>(browserName + QStringLiteral("_IconSize"));
      auto* groupToggle =
        window.findChild<QToolButton*>(browserName + QStringLiteral("_GroupToggle"));
      auto* usedToggle =
        window.findChild<QToolButton*>(browserName + QStringLiteral("_UsedToggle"));

      REQUIRE(browser != nullptr);
      REQUIRE(controls != nullptr);
      REQUIRE(search != nullptr);
      REQUIRE(filterRow != nullptr);
      REQUIRE(sort != nullptr);
      REQUIRE(groupToggle != nullptr);
      REQUIRE(usedToggle != nullptr);

      CHECK(controls->parentWidget() == browser);
      CHECK(browser->layout()->indexOf(controls) == 0);
      CHECK(controls->layout()->count() == 2);
      CHECK(controls->layout()->indexOf(search) == 0);
      CHECK(controls->layout()->indexOf(filterRow) == 1);
      const auto isMaterialBrowser = browserName == QStringLiteral("MaterialBrowser");
      CHECK(filterRow->layout()->count() == (isMaterialBrowser ? 4 : 3));
      CHECK(filterRow->layout()->indexOf(sort) == 0);
      CHECK(filterRow->layout()->indexOf(groupToggle) == 1);
      CHECK(filterRow->layout()->indexOf(usedToggle) == 2);
      if (isMaterialBrowser)
      {
        const auto originalIconSize = pref(Preferences::MaterialBrowserIconSize);
        REQUIRE(iconSize != nullptr);
        CHECK(filterRow->layout()->indexOf(iconSize) == 3);
        CHECK(iconSize->count() == 9);
        CHECK(iconSize->itemText(0) == QStringLiteral("100%"));
        CHECK(iconSize->itemText(8) == QStringLiteral("500%"));
        CHECK(
          iconSize->currentData().toFloat()
          == pref(Preferences::MaterialBrowserIconSize));

        setPref(Preferences::MaterialBrowserIconSize, 1.5f);
        CHECK(iconSize->currentText() == QStringLiteral("150%"));

        iconSize->setCurrentIndex(2);
        iconSize->activated(2);
        CHECK(pref(Preferences::MaterialBrowserIconSize) == 2.0f);

        setPref(Preferences::MaterialBrowserIconSize, originalIconSize);
      }
      else
      {
        CHECK(iconSize == nullptr);
      }
      CHECK(search->placeholderText() == "Search...");
      CHECK(sort->count() == 2);
      CHECK(groupToggle->isCheckable());
      CHECK(usedToggle->isCheckable());
      CHECK(groupToggle->property("browserFilterToggle").toBool());
      CHECK(usedToggle->property("browserFilterToggle").toBool());
      CHECK(groupToggle->toolButtonStyle() == Qt::ToolButtonTextOnly);
      CHECK(usedToggle->toolButtonStyle() == Qt::ToolButtonTextOnly);
    }
  }

  SECTION("uses a synchronized vertical inspector navigation rail")
  {
    auto* navigationRail = window.findChild<QWidget*>("Inspector_NavigationRail");
    auto* pageTitle = window.findChild<QLabel*>("Inspector_PageTitle");
    auto* legacyTabBar = window.findChild<QWidget*>("Inspector_LegacyTabBar");
    auto* mapButton = window.findChild<QToolButton*>("Inspector_NavigationMap");
    auto* entityButton = window.findChild<QToolButton*>("Inspector_NavigationEntity");
    auto* faceButton = window.findChild<QToolButton*>("Inspector_NavigationFace");
    auto* outlinerButton = window.findChild<QToolButton*>("Inspector_NavigationOutliner");
    auto* pluginButton = window.findChild<QToolButton*>("Inspector_NavigationPlugin");

    REQUIRE(navigationRail != nullptr);
    REQUIRE(pageTitle != nullptr);
    REQUIRE(legacyTabBar != nullptr);
    REQUIRE(mapButton != nullptr);
    REQUIRE(entityButton != nullptr);
    REQUIRE(faceButton != nullptr);
    REQUIRE(outlinerButton != nullptr);
    REQUIRE(pluginButton != nullptr);
    CHECK(pageTitle->property("regionAnchor").toBool());

    CHECK(navigationRail->width() == 40);
    CHECK(legacyTabBar->isHidden());
    CHECK_FALSE(mapButton->icon().isNull());
    CHECK_FALSE(entityButton->icon().isNull());
    CHECK_FALSE(faceButton->icon().isNull());
    CHECK_FALSE(outlinerButton->icon().isNull());
    CHECK_FALSE(pluginButton->icon().isNull());
    for (const auto* button :
         {mapButton, entityButton, faceButton, outlinerButton, pluginButton})
    {
      const auto iconImage = button->icon().pixmap(QSize{20, 20}).toImage();
      CHECK(iconImage.deviceIndependentSize() == QSizeF{20, 20});
      CHECK(iconImage.isGrayscale());
    }
    CHECK(mapButton->accessibleName() == "Map Inspector");
    CHECK(mapButton->toolTip().contains("Ctrl+1"));
    CHECK(entityButton->toolTip().contains("Ctrl+2"));
    CHECK(faceButton->toolTip().contains("Ctrl+3"));

    window.switchToInspectorPage(InspectorPage::Outliner);
    CHECK(outlinerButton->isChecked());
    CHECK(pageTitle->text() == "OUTLINER");

    QTest::keyClick(entityButton, Qt::Key_Space);
    CHECK(entityButton->isChecked());
    CHECK_FALSE(outlinerButton->isChecked());
    CHECK(pageTitle->text() == "ENTITY");

    QTest::mouseClick(faceButton, Qt::LeftButton);
    CHECK(faceButton->isChecked());
    CHECK_FALSE(entityButton->isChecked());
    CHECK(pageTitle->text() == "FACE");

    window.switchToInspectorPage(InspectorPage::Plugin);
    CHECK(pluginButton->isChecked());
    CHECK_FALSE(faceButton->isChecked());
    CHECK(pageTitle->text() == "PLUGINS");
  }

  SECTION("uses a focused command palette structure")
  {
    auto context =
      ActionExecutionContext{appController, &window, window.currentMapViewBase()};
    auto dialog = CommandPaletteDialog{
      appController.actionManager(),
      context,
      std::filesystem::path{"Menu/View/Command Palette..."},
      &window};

    CHECK(dialog.objectName() == "CommandPalette_Dialog");
    auto* searchBox = dialog.findChild<QLineEdit*>("CommandPalette_SearchBox");
    auto* actionList = dialog.findChild<QListWidget*>("CommandPalette_ActionList");
    REQUIRE(searchBox != nullptr);
    REQUIRE(actionList != nullptr);
    CHECK(searchBox->placeholderText() == "Search commands...");
    CHECK_FALSE(actionList->alternatingRowColors());
    CHECK(actionList->selectionMode() == QAbstractItemView::SingleSelection);
    CHECK(actionList->count() > 0);
    CHECK(actionList->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff);
    CHECK(actionList->uniformItemSizes());
    CHECK(actionList->itemDelegate()->objectName() == "CommandPalette_ItemDelegate");

    const auto* firstItem = actionList->item(0);
    REQUIRE(firstItem != nullptr);
    CHECK_FALSE(firstItem->data(Qt::UserRole + 1).toString().isEmpty());
    CHECK_FALSE(firstItem->data(Qt::UserRole + 2).toString().isEmpty());
    CHECK_FALSE(firstItem->text().contains('\n'));

    auto itemOption = QStyleOptionViewItem{};
    itemOption.font = actionList->font();
    CHECK(
      actionList->itemDelegate()
        ->sizeHint(itemOption, actionList->model()->index(0, 0))
        .height()
      >= 46);
  }

  SECTION("copy prefixes worldspawn header when enabled")
  {
    auto& map = window.document().map();
    auto worldEntity = map.worldNode().entity();
    worldEntity.addOrUpdateProperty(mdl::EntityPropertyKeys::Wad, "textures/test.wad");
    map.worldNode().setEntity(std::move(worldEntity));

    const auto builder = mdl::BrushBuilder{
      map.worldNode().mapFormat(),
      map.worldBounds(),
      map.gameInfo().gameConfig.faceAttribsConfig.defaultUvAttributes,
      map.gameInfo().gameConfig.faceAttribsConfig.defaultSurfaceAttributes};
    auto* brushNode =
      new mdl::BrushNode{builder.createCube(64.0, "some_material") | kdl::value()};
    mdl::addNodes(map, {{map.worldNode().defaultLayer(), {brushNode}}});
    mdl::selectNodes(map, {brushNode});

    setPref(Preferences::PrefixWorldspawnHeaderOnCopy, true);
    window.copyToClipboard();

    CHECK(QApplication::clipboard()->text().contains(
      QStringLiteral("\"wad\" \"textures/test.wad\"")));
  }

  SECTION("load resets grid size dropdown")
  {
    auto* gridChoice = window.findChild<QComboBox*>("MapWindow_GridChoice");
    REQUIRE(gridChoice != nullptr);

    const auto changedGridSize = 5;
    const auto changedGridIndex = changedGridSize - mdl::Grid::MinSize;
    const auto path = getFixtureRoot() / "test/ui/MapDocument/emptyValveMap.map";

    window.setGridSize(changedGridSize);
    QApplication::processEvents();

    REQUIRE(window.document().map().grid().size() == changedGridSize);
    REQUIRE(gridChoice->currentIndex() == changedGridIndex);

    REQUIRE(window.document().load(
      appController.environmentConfig(),
      mdl::QuakeGameInfo,
      mdl::MapFormat::Unknown,
      vm::bbox3d{8192.0},
      path));

    QApplication::processEvents();

    const auto loadedGridSize = window.document().map().grid().size();
    const auto loadedGridIndex = loadedGridSize - mdl::Grid::MinSize;
    auto* loadedGridChoice = window.findChild<QComboBox*>("MapWindow_GridChoice");
    REQUIRE(loadedGridChoice != nullptr);

    CHECK(loadedGridSize != changedGridSize);
    CHECK(loadedGridChoice->currentIndex() == loadedGridIndex);
    CHECK(loadedGridChoice->currentData().toInt() == loadedGridSize);
  }

  SECTION("canReloadMaterialCollections returns false when resources need processing")
  {
    using TestResourceT = gl::Resource<TestResource>;

    // Verify initial state: no resources pending processing
    REQUIRE(!appController.glManager().resourceManager().needsProcessing());
    CHECK(window.canReloadMaterialCollections());

    // Add a resource that needs processing
    auto testResource = std::make_shared<TestResourceT>(
      []() { return Result<TestResource>{TestResource{}}; });
    appController.glManager().resourceManager().addResource(testResource);

    REQUIRE(appController.glManager().resourceManager().needsProcessing());
    CHECK(!window.canReloadMaterialCollections());

    // Process all resources synchronously
    auto testGl = gl::TestGl{};
    const auto processContext = gl::ProcessContext{testGl, [](auto, auto) {}};
    gl::processResourcesSync(appController.glManager().resourceManager(), processContext);

    REQUIRE(!appController.glManager().resourceManager().needsProcessing());
    CHECK(window.canReloadMaterialCollections());
  }

  SECTION("canReloadEntityDefinitions returns false when resources need processing")
  {
    using TestResourceT = gl::Resource<TestResource>;

    // Verify initial state: no resources pending processing
    REQUIRE(!appController.glManager().resourceManager().needsProcessing());
    CHECK(window.canReloadEntityDefinitions());

    // Add a resource that needs processing
    auto testResource = std::make_shared<TestResourceT>(
      []() { return Result<TestResource>{TestResource{}}; });
    appController.glManager().resourceManager().addResource(testResource);

    REQUIRE(appController.glManager().resourceManager().needsProcessing());
    CHECK(!window.canReloadEntityDefinitions());

    // Process all resources synchronously
    auto testGl = gl::TestGl{};
    const auto processContext = gl::ProcessContext{testGl, [](auto, auto) {}};
    gl::processResourcesSync(appController.glManager().resourceManager(), processContext);

    REQUIRE(!appController.glManager().resourceManager().needsProcessing());
    CHECK(window.canReloadEntityDefinitions());
  }

  SECTION("runs a minimal Python script with the active document")
  {
    auto env = fs::TestEnvironment{};
    env.createFile(
      "smoke.py",
      R"(
import trenchbroom as tb

doc = tb.documents.current()
assert doc is not None
assert len(doc.entities) >= 1
assert isinstance(doc.materials, list)
for material in doc.materials:
    assert isinstance(material.name, str)
    assert isinstance(material.width, int)
    assert isinstance(material.height, int)
print("python smoke ok")
with open("python-smoke-ok.txt", "w", encoding="utf-8") as f:
    f.write(doc.entities[0].classname)
)");

    const auto currentPathGuard = CurrentPathGuard{env.dir()};

    CHECK(PythonScripting::instance().runScript(window, env.dir() / "smoke.py"));
    CHECK(env.loadFile("python-smoke-ok.txt") == "worldspawn");
  }

  SECTION("runs a Python transaction against the active document")
  {
    auto env = fs::TestEnvironment{};
    env.createFile(
      "transaction.py",
      R"(
import trenchbroom as tb

doc = tb.documents.current()
entity = doc.entities[0]
with doc.transaction("python smoke transaction"):
    entity.set("codex_python_smoke", "ok")
)");

    const auto currentPathGuard = CurrentPathGuard{env.dir()};

    CHECK(PythonScripting::instance().runScript(window, env.dir() / "transaction.py"));
    const auto* value =
      window.document().map().worldNode().entity().property("codex_python_smoke");
    REQUIRE(value != nullptr);
    CHECK(*value == "ok");
  }

  SECTION("runs Python selection_changed callbacks")
  {
    auto env = fs::TestEnvironment{};
    env.createFile(
      "selection_callback.py",
      R"(
import trenchbroom as tb

count = 0

def on_selection_changed():
    global count
    count += 1
    with open("python-selection-callback-ok.txt", "w", encoding="utf-8") as f:
        f.write(str(count))
    tb.unregister_callback(callback_token)

callback_token = tb.register_callback("selection_changed", on_selection_changed)
)");

    const auto currentPathGuard = CurrentPathGuard{env.dir()};

    REQUIRE(
      PythonScripting::instance().runScript(window, env.dir() / "selection_callback.py"));

    auto& map = window.document().map();
    auto* entityNode = new mdl::EntityNode{
      mdl::Entity{{{mdl::EntityPropertyKeys::Classname, "info_player_start"}}}};
    mdl::addNodes(map, {{map.worldNode().defaultLayer(), {entityNode}}});
    mdl::selectNodes(map, {entityNode});
    QApplication::processEvents();

    CHECK(env.loadFile("python-selection-callback-ok.txt") == "1");

    mdl::deselectAll(map);
    QApplication::processEvents();

    CHECK(env.loadFile("python-selection-callback-ok.txt") == "1");
  }

  SECTION("executes Python action APIs against the active map window")
  {
    auto env = fs::TestEnvironment{};
    env.createFile(
      "actions.py",
      R"(
import trenchbroom as tb

actions = tb.actions.list()
assert len(actions) > 0
assert any("Menu/" in action or "View/" in action for action in actions)

tb.actions.execute("Menu/View/Grid/Set Grid Size 8")

try:
    tb.actions.execute("Codex/Missing/Action")
except KeyError:
    with open("python-actions-ok.txt", "w", encoding="utf-8") as f:
        f.write("ok")
else:
    raise AssertionError("missing action did not raise KeyError")
)");

    const auto currentPathGuard = CurrentPathGuard{env.dir()};
    const auto initialGridSize = window.document().map().grid().size();
    REQUIRE(initialGridSize != 3);

    CHECK(PythonScripting::instance().runScript(window, env.dir() / "actions.py"));
    CHECK(env.loadFile("python-actions-ok.txt") == "ok");
    CHECK(window.document().map().grid().size() == 3);
    window.setGridSize(initialGridSize);
    CHECK(window.document().map().grid().size() == initialGridSize);
    CHECK_FALSE(window.document().map().modified());
  }

  SECTION("creates a plugin panel from a Python script")
  {
    auto env = fs::TestEnvironment{};
    env.createFile(
      "plugin_panel.py",
      R"(
import trenchbroom as tb

panel = tb.create_plugin_panel("Codex Panel")
panel.add_label("Ready")
panel.add_button("Run", lambda: print("run"))
)");

    const auto currentPathGuard = CurrentPathGuard{env.dir()};

    REQUIRE(pluginPanels(window).empty());
    CHECK(PythonScripting::instance().runScript(window, env.dir() / "plugin_panel.py"));
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents();

    const auto panels = pluginPanels(window);
    REQUIRE(panels.size() == 1u);

    auto* panel = panels.front();
    auto* label = static_cast<QLabel*>(nullptr);
    for (auto* candidate : panel->findChildren<QLabel*>())
    {
      if (candidate->text() == QStringLiteral("Ready"))
      {
        label = candidate;
      }
    }
    const auto buttons = panel->findChildren<QPushButton*>();

    REQUIRE(label != nullptr);
    CHECK(label->text() == QStringLiteral("Ready"));
    CHECK(std::ranges::any_of(buttons, [](const auto* button) {
      return button->text() == QStringLiteral("Run");
    }));
  }

  SECTION("reloads manifest plugins when plugin directory preferences change")
  {
    auto env = fs::TestEnvironment{};
    env.createDirectory("plugin");
    env.createFile(
      "plugin/trenchbroom-plugin.json",
      R"({
        "id": "codex.mapwindow.reload",
        "name": "Codex Reload",
        "version": "1.0.0",
        "apiVersion": 2,
        "pluginType": "ui",
        "entry": "main.py"
      })");
    env.createFile(
      "plugin/main.py",
      R"(
import trenchbroom as tb

panel = tb.create_plugin_panel("Reloaded Plugin")
panel.add_label("Loaded from preferences")
)");

    REQUIRE(pluginPanels(window).empty());

    setPref(Preferences::PythonPluginDirectories, (env.dir() / "plugin").string());
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents();

    const auto panels = pluginPanels(window);
    REQUIRE(panels.size() == 1u);

    auto* label = static_cast<QLabel*>(nullptr);
    for (auto* candidate : panels.front()->findChildren<QLabel*>())
    {
      if (candidate->text() == QStringLiteral("Loaded from preferences"))
      {
        label = candidate;
      }
    }
    CHECK(label != nullptr);

    setPref(Preferences::PythonPluginDirectories, "");
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents();

    CHECK(pluginPanels(window).empty());
  }

  SECTION("executes a configured pie menu action from the current map view")
  {
    setPref(Preferences::PieMenuAction, "Menu/View/Grid/Set Grid Size 8");

    auto* mapView = window.currentMapViewBase();
    REQUIRE(mapView != nullptr);

    const auto initialGridSize = window.document().map().grid().size();
    REQUIRE(initialGridSize != 3);
    REQUIRE_FALSE(window.document().map().modified());

    QCursor::setPos({300, 300});
    mapView->showPieMenu();

    QTRY_VERIFY_WITH_TIMEOUT(findVisiblePieMenu() != nullptr, 1000);
    auto* menu = findVisiblePieMenu();
    REQUIRE(menu != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(menu->isVisible(), 1000);

    const auto center = QPoint{menu->width() / 2, menu->height() / 2};
    sendMouseMove(*menu, center + QPoint{0, -80});
    sendMouseRelease(*menu, center + QPoint{0, -80});
    QApplication::processEvents();
    menu->setParent(nullptr);
    menu->deleteLater();
    QApplication::sendPostedEvents(menu, QEvent::DeferredDelete);

    CHECK(window.document().map().grid().size() == 3);
    window.setGridSize(initialGridSize);
    CHECK(window.document().map().grid().size() == initialGridSize);
    CHECK_FALSE(window.document().map().modified());
  }

  SECTION("updates checkable action state when preferences change")
  {
    auto* textureLockAction = window.findAction("Menu/Edit/Texture Lock");
    auto* uvLockAction = window.findAction("Menu/Edit/UV Lock");
    REQUIRE(textureLockAction != nullptr);
    REQUIRE(uvLockAction != nullptr);

    setPref(Preferences::AlignmentLock, true);
    setPref(Preferences::UvLock, true);
    QApplication::processEvents();
    CHECK(textureLockAction->isChecked());
    CHECK(uvLockAction->isChecked());

    setPref(Preferences::AlignmentLock, false);
    setPref(Preferences::UvLock, false);
    QApplication::processEvents();
    CHECK_FALSE(textureLockAction->isChecked());
    CHECK_FALSE(uvLockAction->isChecked());

    setPref(Preferences::AlignmentLock, true);
    setPref(Preferences::UvLock, false);
  }

  SECTION("opens the configured pie menu from the current map view shortcut")
  {
    setPref(Preferences::PieMenuAction, "Menu/View/Grid/Set Grid Size 8");

    auto* mapView = window.currentMapViewBase();
    REQUIRE(mapView != nullptr);

    QCursor::setPos({300, 300});
    auto event = QKeyEvent{QEvent::KeyPress, Qt::Key_QuoteLeft, Qt::NoModifier};
    QApplication::sendEvent(mapView, &event);

    REQUIRE(event.isAccepted());
    QTRY_VERIFY_WITH_TIMEOUT(findVisiblePieMenu() != nullptr, 1000);
    auto* menu = findVisiblePieMenu();
    REQUIRE(menu != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(menu->isVisible(), 1000);

    menu->close();
    menu->setParent(nullptr);
    menu->deleteLater();
    QApplication::sendPostedEvents(menu, QEvent::DeferredDelete);
  }
}

TEST_CASE("PlayModeFullScreen")
{
  setPref(Preferences::DefaultPluginPaths, "");
  setPref(Preferences::PythonPluginDirectories, "");
  auto fixture = AppControllerFixture{};
  auto& appController = fixture.appController();
  auto document = MapDocument::createDocument(
                    appController.environmentConfig(),
                    mdl::QuakeGameInfo,
                    mdl::MapFormat::Valve,
                    vm::bbox3d{8192.0},
                    appController.taskManager(),
                    appController.glManager().resourceManager())
                  | kdl::value();
  auto window = MapWindow{appController, std::shared_ptr<MapDocument>{std::move(document)}};
  // restoreGeometry clamps off-screen windows. Keep the initial frame inside the
  // available screen, including on small CI desktops, before comparing restoration.
  const auto available = window.screen()->availableGeometry().adjusted(40, 40, -40, -40);
  window.resize(QSize{1100, 800}.boundedTo(available.size()));
  window.move(available.center() - window.rect().center());
  window.show();
  auto* container = window.findChild<SwitchableMapViewContainer*>();
  REQUIRE(container);

  for (const auto layout :
       {MapViewLayout::OnePane,
        MapViewLayout::TwoPanes,
        MapViewLayout::ThreePanes,
        MapViewLayout::FourPanes})
  {
    DYNAMIC_SECTION("restores layout " << static_cast<int>(layout))
    {
      container->switchToMapView(layout);
      auto& view = window.activate3DMapView();
      QApplication::processEvents();
      auto* parentBefore = view.parentWidget();
      const auto menuHiddenBefore = window.menuBar()->isHidden();
      const auto statusHiddenBefore = window.statusBar()->isHidden();
      auto* inspector = window.findChild<QWidget*>("MapWindow_InspectorSurface");
      REQUIRE(inspector);
      inspector->hide();
      QApplication::processEvents();
      // Settle Qt's frame/screen correction before taking the baseline.
      REQUIRE(window.restoreGeometry(window.saveGeometry()));
      QApplication::processEvents();
      const auto geometryBefore = window.geometry();
      const auto viewSizeBefore = view.size();
      CAPTURE(
        geometryBefore.x(),
        geometryBefore.y(),
        geometryBefore.width(),
        geometryBefore.height());

      const auto splitters = window.findChildren<QSplitter*>();
      auto sizes = std::vector<QList<int>>{};
      for (auto* splitter : splitters)
      {
        sizes.push_back(splitter->sizes());
      }

      auto presentation = PlayModeFullScreen{};
      presentation.enter(view);
      presentation.enter(view); // repeated entry must not overwrite the snapshot
      QApplication::processEvents();
      CHECK(window.isFullScreen());
      CHECK(view.isVisible());
      CHECK(view.parentWidget() == parentBefore);
      CHECK(window.menuBar()->isHidden());
      CHECK(window.statusBar()->isHidden());
      CHECK(view.size() == window.size());

      presentation.leave();
      presentation.leave();
      QApplication::processEvents();
      CHECK_FALSE(window.isFullScreen());
      QTRY_COMPARE(window.geometry(), geometryBefore);
      CHECK(window.menuBar()->isHidden() == menuHiddenBefore);
      CHECK(window.statusBar()->isHidden() == statusHiddenBefore);
      CHECK(inspector->isHidden());
      for (auto i = qsizetype{0}; i < splitters.size(); ++i)
      {
        CHECK(splitters[i]->sizes() == sizes[static_cast<size_t>(i)]);
      }
      // All existing GL views survive both transitions.
      CHECK(view.parentWidget() == parentBefore);
      CHECK(view.width() >= viewSizeBefore.width());
    }
  }

  SECTION("preserves an already maximized pane on destruction")
  {
    auto& view = window.activate3DMapView();
    auto focus = QFocusEvent{QEvent::FocusIn};
    QApplication::sendEvent(&view, &focus);
    if (!window.currentViewMaximized())
    {
      window.toggleMaximizeCurrentView();
    }
    REQUIRE(window.currentViewMaximized());
    QApplication::processEvents();
    {
      auto presentation = PlayModeFullScreen{};
      presentation.enter(view);
      CHECK(window.isFullScreen());
    }
    QApplication::processEvents();
    CHECK_FALSE(window.isFullScreen());
    CHECK(window.currentViewMaximized());
  }
}

#ifdef Q_OS_WIN
TEST_CASE("MapView3D native play mode mouse capture", "[desktop]")
{
  if (qEnvironmentVariableIntValue("TB_TEST_SHOW_WINDOWS") != 1)
  {
    SKIP("Native mouse capture requires TB_TEST_SHOW_WINDOWS=1");
  }

  setPref(Preferences::DefaultPluginPaths, "");
  setPref(Preferences::PythonPluginDirectories, "");
  auto fixture = AppControllerFixture{};
  auto& appController = fixture.appController();
  auto document = MapDocument::createDocument(
                    appController.environmentConfig(),
                    mdl::QuakeGameInfo,
                    mdl::MapFormat::Valve,
                    vm::bbox3d{8192.0},
                    appController.taskManager(),
                    appController.glManager().resourceManager())
                  | kdl::value();
  auto window = MapWindow{appController, std::shared_ptr<MapDocument>{std::move(document)}};

  const auto previousRawInput = pref(Preferences::PlayModeRawInput);
  const auto previousFullScreen = pref(Preferences::PlayModeFullScreen);
  const auto previousPosition = QCursor::pos();
  auto originalClip = RECT{};
  REQUIRE(GetClipCursor(&originalClip));
  const auto restore = qScopeGuard([&]() {
    setPref(Preferences::PlayModeRawInput, previousRawInput);
    setPref(Preferences::PlayModeFullScreen, previousFullScreen);
    QCursor::setPos(previousPosition);
  });
  window.resize(1000, 700);
  window.showNormal();
  window.raise();
  window.activateWindow();
  REQUIRE(QTest::qWaitForWindowActive(&window));

  auto* view = window.findChild<MapView3D*>();
  REQUIRE(view != nullptr);
  REQUIRE(view->isVisible());
  QCursor::setPos(view->mapToGlobal(view->rect().center()));
  QTest::mouseClick(view, Qt::LeftButton);

  for (const auto [raw, fullScreen] :
       {std::pair{true, false},
        std::pair{true, true},
        std::pair{false, false},
        std::pair{false, true}})
  {
    DYNAMIC_SECTION("raw input " << raw << ", full screen " << fullScreen)
    {
      setPref(Preferences::PlayModeRawInput, raw);
      setPref(Preferences::PlayModeFullScreen, fullScreen);
      if (fullScreen)
      {
        window.showMaximized();
        QApplication::processEvents();
        REQUIRE(window.isMaximized());
      }
      const auto maximizedBefore = window.isMaximized();
      view->startPlayMode(true);
      const auto stop = qScopeGuard([&]() { view->stopPlayMode(); });
      REQUIRE(QTest::qWaitFor([&]() { return view->playModeActive(); }, 10000));
      QApplication::processEvents();

      CHECK(window.isFullScreen() == fullScreen);
      if (fullScreen)
      {
        CHECK(view->size() == window.size());
      }

      const auto hwnd = reinterpret_cast<HWND>(window.winId());
      const auto hiddenCursor = GetCursor();
      CHECK(QWidget::mouseGrabber() == view);
      CHECK(GetCapture() == hwnd);

      // Activation tracking can update the widget cursor while playing. The capture
      // cursor must take precedence even after a native cursor-update message.
      view->setCursor(Qt::ArrowCursor);
      SendMessageW(
        hwnd,
        WM_SETCURSOR,
        reinterpret_cast<WPARAM>(hwnd),
        MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
      CHECK(GetCursor() == hiddenCursor);

      // Large relative moves can hit an edge before Qt handles WM_MOUSEMOVE. Exercise
      // every corner, including the lower left, without yielding between each burst.
      for (const auto dx : {-10000L, 10000L})
      {
        for (const auto dy : {-10000L, 10000L})
        {
          auto input = INPUT{};
          input.type = INPUT_MOUSE;
          input.mi.dx = dx;
          input.mi.dy = dy;
          input.mi.dwFlags = MOUSEEVENTF_MOVE;
          for (auto i = 0; i < 12; ++i)
          {
            REQUIRE(SendInput(1, &input, sizeof(INPUT)) == 1u);
          }
          QTest::qWait(30);
          CHECK(view->playModeActive());
          CHECK(QWidget::mouseGrabber() == view);
          CHECK(GetCursor() == hiddenCursor);
          const auto cursorInView = view->mapFromGlobal(QCursor::pos());
          INFO(
            "view " << view->width() << "x" << view->height() << ", cursor "
                    << cursorInView.x() << "," << cursorInView.y() << ", scale "
                    << view->devicePixelRatioF());
          CHECK(view->rect().contains(view->mapFromGlobal(QCursor::pos())));
        }
      }

      if (raw)
      {
        QTest::keyClick(view, Qt::Key_Escape);
      }
      else
      {
        // Real focus transfer exercises the same cleanup path as leaving the app.
        auto otherWindow = QWidget{};
        otherWindow.show();
        otherWindow.activateWindow();
        REQUIRE(QTest::qWaitForWindowActive(&otherWindow));
        QApplication::processEvents();
        CHECK(otherWindow.isActiveWindow());
      }
      CHECK_FALSE(view->playModeActive());
      CHECK_FALSE(window.isFullScreen());
      CHECK(window.isMaximized() == maximizedBefore);
      CHECK(QWidget::mouseGrabber() != view);
      CHECK(GetCapture() == nullptr);
      auto releasedClip = RECT{};
      REQUIRE(GetClipCursor(&releasedClip));
      CHECK(releasedClip.left == originalClip.left);
      CHECK(releasedClip.top == originalClip.top);
      CHECK(releasedClip.right == originalClip.right);
      CHECK(releasedClip.bottom == originalClip.bottom);
      SendMessageW(
        hwnd,
        WM_SETCURSOR,
        reinterpret_cast<WPARAM>(hwnd),
        MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
      CHECK(GetCursor() != hiddenCursor);
    }
  }
}
#endif

} // namespace tb::ui
