/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom.
*/

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSaveFile>
#include <QScopeGuard>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest/QTest>

#include "el/Expression.h"
#include "gl/Resource.h"
#include "img/GoldSrcTexture.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/EntityDefinition.h"
#include "mdl/EntityDefinitionManager.h"
#include "mdl/EntityModel.h"
#include "mdl/EntityModelManager.h"
#include "mdl/EntityNode.h"
#include "mdl/GameManager.h"
#include "mdl/Grid.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "mdl/Map_Brushes.h"
#include "mdl/Map_CopyPaste.h"
#include "mdl/Map_Entities.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/PropertyDefinition.h"
#include "mdl/WorldNode.h"
#include "ui/Action.h"
#include "ui/ActionExecutionContext.h"
#include "ui/ActionManager.h"
#include "ui/AppControllerFixture.h"
#include "ui/AssetInspector.h"
#include "ui/AssetLibraryItemDelegate.h"
#include "ui/AssetLibrarySource.h"
#include "ui/AssetLibraryWindow.h"
#include "ui/CatalogAssetDrop.h"
#include "ui/CreateEntityTool.h"
#include "ui/InfoPanel.h"
#include "ui/MapDocument.h"
#include "ui/MapEditorView.h"
#include "ui/MapStatus.h"
#include "ui/MapView3D.h"
#include "ui/MapViewBase.h"
#include "ui/MapViewLayout.h"
#include "ui/MapViewToolBox.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/ModelBrowser.h"
#include "ui/ModelBrowserView.h"
#include "ui/ProjectAssetPanel.h"
#include "ui/ProjectRecovery.h"
#include "ui/ProjectSession.h"
#include "ui/ProjectWindow.h"
#include "ui/QPathUtils.h"
#include "ui/Splitter.h"
#include "ui/WorkspaceLayout.h"
#include "ui/WorkspaceToolPanel.h"
#include "ui/outliner/OutlinerTreeWidget.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{

const auto GameName = std::string{"Workspace"};

const auto GameConfig = R"({
    "version": 9,
    "name": "Workspace",
    "icon": "Icon.png",
    "fileformats": [
        { "format": "Valve" }
    ],
    "filesystem": {
        "searchpath": "valve",
        "packageformat": { "extension": "pak", "format": "idpak" }
    },
    "materials": {
        "root": "textures",
        "extensions": [".D"],
        "palette": "gfx/palette.lmp",
        "attribute": "wad"
    },
    "entities": {
        "definitions": [],
        "defaultcolor": "0.6 0.6 0.6 1.0",
        "modelformats": [ "mdl" ]
    },
    "tags": {
        "brush": [],
        "brushface": []
    }
})";

template <typename T>
T require(Result<T> result)
{
  if (result.is_error())
    FAIL(std::get<Error>(result.error()).msg);
  return std::move(result).value();
}

void require(Result<void> result)
{
  if (result.is_error())
    FAIL(std::get<Error>(result.error()).msg);
}

MapWindow* windowFor(AppController& appController, const MapDocument& document)
{
  for (auto* window : appController.mapWindowManager().mapWindows())
    if (&window->document() == &document)
      return window;
  return nullptr;
}

/**
 * A minimal 16-bit mono WAVE. The optional `smpl` chunk is what separates a looping cue
 * from a one-shot cue for the sound browser filters.
 */
std::vector<std::uint8_t> wavBytes(const bool loop)
{
  auto data = std::vector<std::uint8_t>{};
  const auto u16 = [&](const std::uint16_t value) {
    data.push_back(std::uint8_t(value & 0xffu));
    data.push_back(std::uint8_t(value >> 8));
  };
  const auto u32 = [&](const std::uint32_t value) {
    for (auto i = 0u; i < 4; ++i)
      data.push_back(std::uint8_t(value >> (i * 8)));
  };
  const auto tag = [&](const char* text) { data.insert(data.end(), text, text + 4); };

  tag("RIFF");
  u32(0);
  tag("WAVE");
  tag("fmt ");
  u32(16);
  u16(1);
  u16(1);
  u32(22050);
  u32(44100);
  u16(2);
  u16(16);
  if (loop)
  {
    tag("smpl");
    u32(60);
    for (auto i = 0; i < 7; ++i)
      u32(0);
    u32(1); // loop count
    u32(0); // sampler data
    u32(0);
    u32(0);
    u32(0);
    u32(1000);
    u32(0);
    u32(1);
  }
  tag("data");
  u32(2205);
  data.resize(data.size() + 2205, 0);
  const auto size = std::uint32_t(data.size() - 8);
  for (auto i = 0u; i < 4; ++i)
    data[4 + i] = std::uint8_t(size >> (i * 8));
  return data;
}

// Emitting the signal is what a click on the tab's close button does.
void closeTab(QTabBar& tabs, const int index)
{
  QMetaObject::invokeMethod(&tabs, "tabCloseRequested", Q_ARG(int, index));
  QApplication::processEvents();
  QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QApplication::processEvents();
}

} // namespace

TEST_CASE("ProjectWindow")
{
  // Layout tests must neither read user preferences nor race other CTest processes.
  const auto applicationName = QApplication::applicationName();
  QApplication::setApplicationName(
    QString{"TrenchBroom-ProjectWindowTest-%1"}.arg(QCoreApplication::applicationPid()));
  const auto restoreSettings = qScopeGuard([&]() {
    QSettings{}.clear();
    QApplication::setApplicationName(applicationName);
  });
  auto fixture = AppControllerFixture{[](const auto& writeGameConfig) {
    writeGameConfig(GameName, GameConfig, std::nullopt, std::nullopt);
  }};
  auto& appController = fixture.appController();

  auto directory = QTemporaryDir{};
  REQUIRE(directory.isValid());
  const auto projectPath = pathFromQString(directory.filePath("workspace.tbproj"));
  {
    auto created = require(ProjectSession::create(projectPath, "Workspace", GameName));
  }
  REQUIRE(appController.openProject(projectPath));
  ProjectWindow* host = nullptr;
  for (auto* widget : QApplication::topLevelWidgets())
    if (auto* candidate = dynamic_cast<ProjectWindow*>(widget);
        candidate
        && candidate->session()->path() == std::filesystem::weakly_canonical(projectPath))
      host = candidate;
  REQUIRE(host != nullptr);
  auto& window = *host;
  auto session = window.session();
  auto& manager = appController.mapWindowManager();
  require(manager.createProjectMap(session, "alpha", mdl::MapFormat::Valve));
  require(manager.createProjectMap(session, "beta", mdl::MapFormat::Valve));
  window.setAttribute(Qt::WA_DeleteOnClose, false);
  window.resize(1000, 700);
  window.show();
  QApplication::processEvents();

  // Close clean views while their workspace and focus routing still exist.
  const auto teardown = [&]() {
    window.hide();
    for (auto* mapWindow : manager.mapWindows())
    {
      if (mapWindow->document().map().modified())
        continue;
      mapWindow->close();
    }
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents();
    delete host;
  };
  struct TeardownGuard
  {
    std::function<void()> fn;
    ~TeardownGuard()
    {
      if (fn)
        fn();
    }
  };
  auto teardownGuard = TeardownGuard{teardown};

  auto* tabs = window.findChild<QTabBar*>("ProjectMapTabs");
  REQUIRE(tabs != nullptr);
  REQUIRE(tabs->count() == 2);
  tabs->setCurrentIndex(0);
  CHECK(tabs->tabText(0) == "alpha");
  CHECK(tabs->tabText(1) == "beta");

  const auto alphaId = tabs->tabData(0).toString().toStdString();
  auto* alphaDocument = session->documentFor(alphaId);
  REQUIRE(alphaDocument != nullptr);
  auto* alphaWindow = windowFor(appController, *alphaDocument);
  REQUIRE(alphaWindow != nullptr);
  CHECK(alphaWindow->isVisible());

  auto* stack = window.findChild<QStackedWidget*>("ProjectMapStack");
  REQUIRE(stack != nullptr);
  REQUIRE(stack->count() == 2);
  CHECK_FALSE(alphaWindow->isWindow());
  CHECK(alphaWindow->findChildren<QMenuBar*>().empty());
  CHECK(alphaWindow->findChildren<QToolBar*>().empty());
  CHECK(alphaWindow->findAction("Menu/Edit/Undo")->shortcut().isEmpty());

  SECTION("one project browser survives map switches and closing all map tabs")
  {
    emit session->browserFocusRequested();
    auto* browser = window.findChild<ModelBrowser*>();
    REQUIRE(browser != nullptr);
    CHECK(window.findChildren<ModelBrowser*>().size() == 1);
    CHECK(alphaWindow->findChildren<ModelBrowser*>().empty());
    auto* search = browser->findChild<QLineEdit*>("ModelBrowser_Search");
    REQUIRE(search != nullptr);
    search->setText("alpha");
    tabs->setCurrentIndex(1);
    QApplication::processEvents();
    CHECK(window.findChild<ModelBrowser*>() == browser);
    CHECK(search->text() == "alpha");
    closeTab(*tabs, 1);
    closeTab(*tabs, 0);
    CHECK(window.activeMapWindow() == nullptr);
    CHECK(window.findChild<ModelBrowser*>() == browser);
    CHECK(browser->isVisible());
    CHECK(search->text() == "alpha");
    emit browser->findChild<ModelBrowserView*>()->mapActivated("maps/alpha.map");
    QApplication::processEvents();
    CHECK(window.activeMapWindow() != nullptr);
    CHECK(window.findChild<ModelBrowser*>() == browser);
  }

  SECTION("save status follows the active map and ignores inactive edits")
  {
    auto* state = window.findChild<QLabel*>("MapWindow_SaveState");
    REQUIRE(state);
    tabs->setCurrentIndex(1);
    auto& betaMap = window.activeMapWindow()->document().map();
    REQUIRE(mdl::setEntityProperty(betaMap, "message", "status edit"));
    QTRY_VERIFY_WITH_TIMEOUT(state->property("tbStatus").toString() == "modified", 1000);
    tabs->setCurrentIndex(0);
    const auto alphaState = alphaDocument->map().modified() ? "modified" : "saved";
    CHECK(state->property("tbStatus").toString() == alphaState);
    REQUIRE(mdl::setEntityProperty(betaMap, "message", "inactive edit"));
    QTest::qWait(150);
    CHECK(state->property("tbStatus").toString() == alphaState);
    tabs->setCurrentIndex(1);
    CHECK(state->property("tbStatus").toString() == "modified");
  }

  SECTION("compact content views preserve navigation and optional details")
  {
    auto* tools = window.findChild<WorkspaceToolPanel*>();
    auto* views = window.findChild<QComboBox*>("ProjectContentView");
    auto* pages = window.findChild<QTabWidget*>("ProjectContentTabs");
    auto* details = window.findChild<QWidget*>("ProjectAssetDetailsSurface");
    auto* toggle = window.findChild<QToolButton*>("ProjectAssetDetailsToggle");
    REQUIRE(views);
    REQUIRE(pages);
    REQUIRE(details);
    REQUIRE(toggle);
    tools->showContent();
    QTRY_VERIFY_WITH_TIMEOUT(!tools->isAnimating(), 1000);
    CHECK(views->isVisible());
    CHECK_FALSE(pages->tabBar()->isVisible());
    CHECK_FALSE(details->isVisible());
    toggle->click();
    CHECK(details->isVisible());
    toggle->click();
    CHECK_FALSE(details->isVisible());
    for (int i = 0; i < views->count(); ++i)
    {
      views->setCurrentIndex(i);
      CHECK(pages->currentIndex() == i);
      CHECK(pages->currentWidget()->isVisible());
      CHECK(views->isVisible());
    }
    pages->setCurrentIndex(0);
    CHECK(views->currentIndex() == 0);
    auto* browser = window.findChild<ModelBrowser*>();
    auto* path = browser->findChild<QWidget*>("ModelBrowser_PathStack");
    auto* search = browser->findChild<QLineEdit*>("ModelBrowser_Search");
    REQUIRE(path);
    REQUIRE(search);
    CHECK(path->parentWidget() == search->parentWidget());
    CHECK(search->width() >= 140);
  }

  SECTION("one bottom surface hosts content and the active map tools")
  {
    auto* tools = window.findChild<WorkspaceToolPanel*>();
    auto* drawer = window.findChild<QWidget*>("ProjectToolDrawer");
    auto* mode = window.findChild<QComboBox*>("ProjectToolsMode");
    auto* close = window.findChild<QToolButton*>("ProjectToolsClose");
    auto* editor = alphaWindow->findChild<MapEditorView*>();
    REQUIRE(tools != nullptr);
    REQUIRE(drawer != nullptr);
    REQUIRE(mode != nullptr);
    REQUIRE(close != nullptr);
    REQUIRE(editor != nullptr);
    auto settings = QSettings{};
    const auto previousPin = settings.value("ProjectTools/Pinned");
    const auto restorePin = qScopeGuard([&]() {
      if (previousPin.isValid())
        settings.setValue("ProjectTools/Pinned", previousPin);
      else
        settings.remove("ProjectTools/Pinned");
    });
    mode->setCurrentIndex(0);
    if (tools->isExpanded())
      close->click();
    QTRY_VERIFY_WITH_TIMEOUT(!tools->isAnimating(), 1000);
    REQUIRE(drawer->isVisible());
    CHECK_FALSE(window.findChild<ProjectAssetPanel*>()->isVisible());
    auto* bar = tools->findChild<QWidget*>("ProjectToolBar");
    auto* status = tools->findChild<QWidget*>("WorkspaceStatusBar");
    REQUIRE(bar);
    REQUIRE(status);
    CHECK(drawer->isAncestorOf(bar));
    CHECK_FALSE(drawer->isAncestorOf(status));
    const auto workspaceHeight = stack->height();
    tools->showContent();
    QApplication::processEvents();
    CHECK(drawer->isVisible());
    CHECK(drawer->isAncestorOf(bar));
    CHECK_FALSE(drawer->isAncestorOf(status));
    CHECK(stack->height() == workspaceHeight);
    CHECK(window.findChild<ProjectAssetPanel*>()->isVisible());
    CHECK_FALSE(editor->infoPanel()->isVisible());
    CHECK(editor->isInfoPanelVisible());
    CHECK(alphaWindow->infoPanelVisible());
    CHECK(tools->isToolVisible(WorkspaceToolId::ContentBrowser));
    CHECK_FALSE(tools->isToolVisible(WorkspaceToolId::OutputLog));
    close->click();
    CHECK_FALSE(tools->isExpanded());
    CHECK(drawer->isAncestorOf(bar));
    close->click();
    CHECK(tools->isExpanded());
    CHECK(drawer->isAncestorOf(bar));
    CHECK_FALSE(editor->findChild<QWidget*>("MapWindow_InfoPanelSurface")->isVisible());

    alphaWindow->switchToInfoPanelPage(InfoPanelPage::PythonConsole);
    CHECK(editor->infoPanel()->isVisible());
    CHECK(editor->infoPanel()->currentPage() == InfoPanelPage::PythonConsole);
    CHECK(tools->currentTool() == WorkspaceToolId::PythonConsole);
    CHECK_FALSE(window.findChild<ProjectAssetPanel*>()->isVisible());
    const auto alphaPanel = QPointer{editor->infoPanel()};
    tabs->setCurrentIndex(1);
    auto* betaEditor = window.activeMapWindow()->findChild<MapEditorView*>();
    CHECK(betaEditor->infoPanel()->isVisible());
    CHECK(betaEditor->infoPanel()->currentPage() == InfoPanelPage::PythonConsole);
    CHECK_FALSE(alphaPanel->isVisible());
    CHECK_FALSE(editor->isInfoPanelVisible());
    CHECK(betaEditor->isInfoPanelVisible());
    auto* input = betaEditor->infoPanel()->findChild<QWidget*>("PythonConsole_Input");
    REQUIRE(input != nullptr);
    input->setFocus();
    mode->setCurrentIndex(1);
    QApplication::processEvents();
    CHECK(stack->height() < workspaceHeight);
    QMetaObject::invokeMethod(
      qApp,
      "focusChanged",
      Q_ARG(QWidget*, betaEditor->infoPanel()),
      Q_ARG(QWidget*, window.activeMapWindow()->currentMapViewBase()));
    CHECK(tools->isExpanded());
    mode->setCurrentIndex(0);
    QApplication::processEvents();
    CHECK(tools->isExpanded());
    tools->showContent();
    QMetaObject::invokeMethod(
      qApp,
      "focusChanged",
      Q_ARG(QWidget*, drawer),
      Q_ARG(QWidget*, window.activeMapWindow()->currentMapViewBase()));
    CHECK_FALSE(tools->isExpanded());
    CHECK_FALSE(betaEditor->isInfoPanelVisible());
    tabs->setCurrentIndex(0);
    closeTab(*tabs, 0);
    CHECK(alphaPanel.isNull());
    closeTab(*tabs, 0);
    tools->showContent();
    CHECK(window.findChild<ProjectAssetPanel*>()->isVisible());
    CHECK_FALSE(window.findChild<QToolButton*>("ProjectTool1")->isEnabled());
  }

  SECTION("closing a background map releases only its hosted tools before its document")
  {
    auto* tools = window.findChild<WorkspaceToolPanel*>();
    auto* alphaEditor = alphaWindow->findChild<MapEditorView*>();
    const auto alphaPanel = QPointer{alphaEditor->infoPanel()};
    tabs->setCurrentIndex(1);
    auto* betaEditor = window.activeMapWindow()->findChild<MapEditorView*>();
    const auto betaPanel = QPointer{betaEditor->infoPanel()};
    tools->showInfoPage(InfoPanelPage::PythonConsole);
    auto releasedBeforeDocument = false;
    QObject::connect(
      alphaWindow, &MapWindow::toolPanelsAboutToBeDestroyed, &window, [&]() {
        releasedBeforeDocument = alphaPanel.isNull() && !alphaEditor->infoPanel()
                                 && session->assetFor(alphaWindow->document()) == alphaId;
      });
    closeTab(*tabs, 0);
    CHECK(releasedBeforeDocument);
    CHECK_FALSE(betaPanel.isNull());
    CHECK(betaPanel->isVisible());
    CHECK(tools->isToolVisible(WorkspaceToolId::PythonConsole));
    CHECK(window.activeMapWindow()->findChild<MapEditorView*>() == betaEditor);
    closeTab(*tabs, 0);
    CHECK(betaPanel.isNull());
    CHECK(tools->isToolVisible(WorkspaceToolId::ContentBrowser));
    tools->showInfoPage(InfoPanelPage::Issues);
    CHECK(tools->currentTool() == WorkspaceToolId::ContentBrowser);
  }

  SECTION("destroying the workspace releases hosted tools before retained documents")
  {
    tabs->setCurrentIndex(1);
    auto* betaWindow = window.activeMapWindow();
    REQUIRE(betaWindow != nullptr);
    const auto alphaPanel =
      QPointer{alphaWindow->findChild<MapEditorView*>()->infoPanel()};
    const auto betaPanel = QPointer{betaWindow->findChild<MapEditorView*>()->infoPanel()};
    const auto alphaHandle = alphaWindow->documentHandle();
    const auto betaHandle = betaWindow->documentHandle();
    teardownGuard.fn = {};
    delete host;
    host = nullptr;
    CHECK(alphaPanel.isNull());
    CHECK(betaPanel.isNull());
    CHECK(manager.mapWindows().empty());
    CHECK(session->retainedDocument(alphaId) == alphaHandle);
    CHECK(session->assetFor(*betaHandle).has_value());
  }

  SECTION("captured editor commands keep their original map after a tab switch")
  {
    REQUIRE(mdl::setEntityProperty(alphaDocument->map(), "message", "alpha edit"));
    auto context = ActionExecutionContext{
      appController, alphaWindow, alphaWindow->currentMapViewBase()};
    auto* alphaEditor = alphaWindow->findChild<MapEditorView*>();
    tabs->setCurrentIndex(1);
    auto& betaMap = window.activeMapWindow()->document().map();
    REQUIRE(mdl::setEntityProperty(betaMap, "message", "beta edit"));
    auto* betaEditor = window.activeMapWindow()->findChild<MapEditorView*>();
    CHECK(&context.map() == &alphaWindow->document().map());
    CHECK(&context.editorView() == alphaEditor);
    CHECK(&context.editorView() != betaEditor);
    const auto& undo = appController.actionManager().actionsMap().at("Menu/Edit/Undo");
    const auto& redo = appController.actionManager().actionsMap().at("Menu/Edit/Redo");
    const auto& repeat =
      appController.actionManager().actionsMap().at("Menu/Edit/Repeat");
    const auto& clearRepeat = appController.actionManager().actionsMap().at(
      "Menu/Edit/Clear Repeatable Commands");
    const auto& nextTrace = appController.actionManager().actionsMap().at(
      "Menu/View/Camera/Move to Next Point");
    const auto& previousTrace = appController.actionManager().actionsMap().at(
      "Menu/View/Camera/Move to Previous Point");
    const auto& focusSelection = appController.actionManager().actionsMap().at(
      "Menu/View/Camera/Focus on Selection");
    const auto& maximize =
      appController.actionManager().actionsMap().at("Menu/View/Maximize Current View");
    CHECK(undo.enabled(context) == alphaEditor->canUndo());
    CHECK(redo.enabled(context) == alphaEditor->canRedo());
    CHECK(repeat.enabled(context));
    CHECK(clearRepeat.enabled(context) == alphaEditor->hasRepeatableCommands());
    CHECK(nextTrace.enabled(context) == alphaEditor->canMoveCameraToNextTracePoint());
    CHECK(
      previousTrace.enabled(context) == alphaEditor->canMoveCameraToPreviousTracePoint());
    CHECK(focusSelection.enabled(context) == alphaEditor->canFocusCamera());
    CHECK(maximize.checked(context) == alphaEditor->currentViewMaximized());
    undo.execute(context);
    CHECK_FALSE(alphaDocument->map().worldNode().entity().property("message"));
    REQUIRE(betaMap.worldNode().entity().property("message"));
    CHECK(*betaMap.worldNode().entity().property("message") == "beta edit");
    CHECK(window.activeMapWindow()->documentHandle().get() != alphaDocument);
    redo.execute(context);
    REQUIRE(alphaDocument->map().worldNode().entity().property("message"));
    CHECK(*alphaDocument->map().worldNode().entity().property("message") == "alpha edit");
    CHECK(*betaMap.worldNode().entity().property("message") == "beta edit");
    clearRepeat.execute(context);
    nextTrace.execute(context);
    previousTrace.execute(context);
    focusSelection.execute(context);
    maximize.execute(context);
    CHECK(maximize.checked(context) == alphaEditor->currentViewMaximized());
    context.editorView().showInfoPanelPage(InfoPanelPage::PythonConsole);
    const auto& toggle =
      appController.actionManager().actionsMap().at("Menu/View/Toggle Info Panel");
    REQUIRE(toggle.checked(context));
    toggle.execute(context);
    CHECK_FALSE(toggle.checked(context));
    toggle.execute(context);
    CHECK(toggle.checked(context));
    CHECK(window.activeMapWindow() == alphaWindow);
    CHECK(alphaEditor->infoPanel()->currentPage() == InfoPanelPage::PythonConsole);
    CHECK_FALSE(betaEditor->infoPanel()->isVisible());
    auto empty = ActionExecutionContext{appController, nullptr, nullptr};
    CHECK_FALSE(empty.hasDocument());
    CHECK_FALSE(undo.enabled(empty));
    CHECK_FALSE(redo.enabled(empty));
    require(session->save());
  }

  SECTION("viewport bindings and tool commands survive layout and host changes")
  {
    auto& editor = alphaWindow->editorView();
    auto* view = &editor.activate3DMapView();
    CHECK(editor.currentMapViewBase() == view);
    CHECK(alphaWindow->currentMapViewBase() == view);
    auto context = editor.toolBox().actionContext(appController, view);
    REQUIRE(context.hasDocument());
    CHECK(&context.editorView() == &editor);

    auto* outliner = editor.inspector()->findChild<OutlinerTreeWidget*>();
    REQUIRE(outliner);
    auto outlinerMenu = QMenu{};
    Q_EMIT outliner->contextMenuActionsRequested(&outlinerMenu, true);
    REQUIRE(outlinerMenu.actions().size() == 3);
    CHECK(outlinerMenu.actions()[0]->text() == QStringLiteral("Rename Selected Groups"));
    CHECK(
      outlinerMenu.actions()[2]->text() == QStringLiteral("Focus Camera on Selection"));

    // Reparenting the editor must not change the target of viewport commands.
    auto* parent = editor.parentWidget();
    editor.setParent(&window);
    auto rebound = editor.toolBox().actionContext(appController, view);
    CHECK(rebound.hasDocument());
    CHECK(&rebound.map() == &alphaDocument->map());
    editor.setParent(parent);
    editor.show();

    tabs->setCurrentIndex(1);
    auto& betaTools = window.activeMapWindow()->editorView().toolBox();
    const auto betaSelect = betaTools.selectMode();
    const auto originalSelect = editor.toolBox().selectMode();
    const auto& select =
      appController.actionManager().actionsMap().at("Menu/Edit/Tools/Select Mode");
    REQUIRE(select.enabled(context));
    select.execute(context);
    CHECK(editor.toolBox().selectMode() != originalSelect);
    CHECK(betaTools.selectMode() == betaSelect);

    auto* tools = &editor.toolBox();
    alphaWindow->switchMapViewLayout(MapViewLayout::FourPanes);
    CHECK(&editor.toolBox() == tools);
    auto rebuilt = tools->actionContext(appController, editor.currentMapViewBase());
    REQUIRE(rebuilt.hasDocument());
    CHECK(&rebuilt.map() == &alphaDocument->map());
    CHECK_FALSE(context.hasDocument());
    CHECK_FALSE(select.enabled(context));
  }

  SECTION("captured commands expire when their map view closes")
  {
    auto context = ActionExecutionContext{
      appController, alphaWindow, alphaWindow->currentMapViewBase()};
    REQUIRE(mdl::setEntityProperty(alphaDocument->map(), "message", "retained edit"));
    const auto& undo = appController.actionManager().actionsMap().at("Menu/Edit/Undo");
    REQUIRE(undo.enabled(context));
    closeTab(*tabs, 0);
    CHECK_FALSE(context.hasDocument());
    CHECK_FALSE(undo.enabled(context));
    undo.execute(context);
    REQUIRE(alphaDocument->map().worldNode().entity().property("message"));
    CHECK(
      *alphaDocument->map().worldNode().entity().property("message") == "retained edit");
    CHECK(window.activeMapWindow() != nullptr);
    CHECK(&window.activeMapWindow()->document() != alphaDocument);
    require(session->save());
  }

  SECTION("editing controller commands retain their document across tab switches")
  {
    auto& alphaMap = alphaDocument->map();
    const auto points = std::vector<vm::vec3d>{
      {-64, -64, -64},
      {-64, -64, 64},
      {-64, 64, -64},
      {-64, 64, 64},
      {64, -64, -64},
      {64, -64, 64},
      {64, 64, -64},
      {64, 64, 64},
    };
    REQUIRE(mdl::createBrush(alphaMap, points));
    REQUIRE(alphaMap.selection().brushes.size() == 1);
    auto* original = alphaMap.selection().brushes.front();
    auto context = ActionExecutionContext{
      appController, alphaWindow, alphaWindow->currentMapViewBase()};
    tabs->setCurrentIndex(1);
    auto& betaMap = window.activeMapWindow()->document().map();
    REQUIRE(mdl::createBrush(betaMap, points));
    const auto betaSelection = betaMap.selection().nodes;
    const auto betaChanges = betaMap.modificationCount();
    const auto& actions = appController.actionManager().actionsMap();
    const auto& duplicate = actions.at("Menu/Edit/Duplicate");
    REQUIRE(duplicate.enabled(context));
    duplicate.execute(context);
    REQUIRE(alphaMap.selection().brushes.size() == 1);
    auto* copy = alphaMap.selection().brushes.front();
    CHECK(copy != original);
    CHECK(betaMap.selection().nodes == betaSelection);
    CHECK(betaMap.modificationCount() == betaChanges);

    const auto& deselect = actions.at("Menu/Edit/Deselect All");
    REQUIRE(deselect.enabled(context));
    deselect.execute(context);
    CHECK_FALSE(alphaMap.selection().hasNodes());
    CHECK(betaMap.selection().nodes == betaSelection);
    const auto& select = actions.at("Menu/Edit/Select All");
    REQUIRE(select.enabled(context));
    select.execute(context);
    CHECK(
      std::ranges::find(alphaMap.selection().brushes, original)
      != alphaMap.selection().brushes.end());
    CHECK(
      std::ranges::find(alphaMap.selection().brushes, copy)
      != alphaMap.selection().brushes.end());
    CHECK(betaMap.selection().nodes == betaSelection);
    CHECK(betaMap.modificationCount() == betaChanges);
    require(session->save());
  }

  SECTION("closing an embedded map preserves standalone info panel visibility")
  {
    // CTest runs other map-window tests concurrently against the same QSettings store.
    auto* panel = alphaWindow->findChild<MapEditorView*>()->infoPanel();
    panel->setObjectName("ProjectWindow_StandaloneVisibilityTest");
    auto settings = QSettings{};
    const auto key = QString{"Windows/ProjectWindow_StandaloneVisibilityTest/State"};
    const auto previous = settings.value(key);
    const auto restore = qScopeGuard([&]() {
      if (previous.isValid())
        settings.setValue(key, previous);
      else
        settings.remove(key);
    });
    const auto standaloneState = QByteArray{"standalone-info-panel"};
    settings.setValue(key, standaloneState);
    alphaWindow->switchToInfoPanelPage(InfoPanelPage::Console);
    REQUIRE(alphaWindow->close());
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents();
    CHECK(settings.value(key).toByteArray() == standaloneState);
  }

  SECTION("layout survives inactive views and reset preserves document edits")
  {
    auto& editor = alphaWindow->editorView();
    editor.showInspectorPage(InspectorPage::Outliner);
    editor.hSplitter()->setSizes({550, 400});
    QApplication::processEvents();
    const auto sizes = editor.hSplitter()->sizes();
    tabs->setCurrentIndex(1);
    editor.saveWidgetState();
    auto settings = QSettings{};
    const auto layout = loadEditorLayout(settings, WorkspaceKind::Project);
    CHECK(layout.inspectorVisible);
    CHECK(layout.inspectorPage == InspectorPage::Outliner);
    CHECK(
      std::abs(
        layout.inspectorRatio - static_cast<double>(sizes[1]) / (sizes[0] + sizes[1]))
      < 0.01);
    CHECK_FALSE(settings.contains("WorkspaceLayouts/Map/Editor"));
    editor.toggleInspector();
    editor.saveWidgetState();
    CHECK_FALSE(loadEditorLayout(settings, WorkspaceKind::Project).inspectorVisible);
    editor.restoreLayoutState();
    CHECK(editor.inspector()->isHidden());
    REQUIRE(mdl::setEntityProperty(alphaDocument->map(), "message", "layout keeps this"));
    const auto modified = alphaDocument->map().modificationCount();
    const auto undoName = alphaDocument->map().undoCommandName();
    editor.resetLayout();
    CHECK_FALSE(editor.inspector()->isHidden());
    CHECK(editor.inspector()->currentPage() == InspectorPage::Map);
    CHECK(alphaDocument->map().modificationCount() == modified);
    CHECK(alphaDocument->map().undoCommandName() == undoName);
    CHECK(
      *alphaDocument->map().worldNode().entity().property("message")
      == "layout keeps this");
    require(session->save());
  }

  SECTION("one workspace status follows the active document and disables without maps")
  {
    const auto statuses = window.findChildren<MapStatus*>();
    REQUIRE(statuses.size() == 1);
    CHECK(alphaWindow->findChildren<QStatusBar*>().isEmpty());
    auto* grid = statuses.front()->findChild<QComboBox*>("MapWindow_GridChoice");
    auto* snap = statuses.front()->findChild<QToolButton*>("MapWindow_SnapToggle");
    REQUIRE(grid);
    REQUIRE(snap);
    alphaDocument->map().grid().setSize(2);
    CHECK(grid->currentIndex() == 2 - mdl::Grid::MinSize);
    tabs->setCurrentIndex(1);
    auto& betaMap = window.activeMapWindow()->document().map();
    betaMap.grid().setSize(4);
    CHECK(grid->currentIndex() == 4 - mdl::Grid::MinSize);
    alphaDocument->map().grid().setSize(1);
    CHECK(grid->currentIndex() == 4 - mdl::Grid::MinSize);
    QMetaObject::invokeMethod(grid, "activated", Q_ARG(int, 3 - mdl::Grid::MinSize));
    CHECK(betaMap.grid().size() == 3);
    CHECK(alphaDocument->map().grid().size() == 1);
    const auto wasSnapping = betaMap.grid().snap();
    snap->click();
    CHECK(betaMap.grid().snap() != wasSnapping);
    closeTab(*tabs, 1);
    CHECK(grid->currentIndex() == 1 - mdl::Grid::MinSize);
    closeTab(*tabs, 0);
    CHECK_FALSE(grid->isEnabled());
    CHECK_FALSE(snap->isEnabled());
    alphaDocument->map().grid().setSize(5);
    CHECK(grid->currentIndex() == -1);
  }

  SECTION("reopened views apply saved proportions after host layout")
  {
    auto settings = QSettings{};
    auto saved = EditorLayout{};
    saved.inspectorRatio = 0.42;
    saved.inspectorPage = InspectorPage::Outliner;
    saveEditorLayout(settings, WorkspaceKind::Project, saved);
    closeTab(*tabs, 0);
    // Closing saves the old view. Seed the layout of the next newly constructed view.
    saveEditorLayout(settings, WorkspaceKind::Project, saved);
    auto* reopen = window.findChild<QToolButton*>("ProjectReopenMaps");
    REQUIRE(reopen);
    REQUIRE(reopen->menu()->actions().size() == 1);
    reopen->menu()->actions().front()->trigger();
    QApplication::processEvents();
    auto& editor = window.activeMapWindow()->editorView();
    const auto sizes = editor.hSplitter()->sizes();
    CHECK(editor.inspector()->currentPage() == InspectorPage::Outliner);
    REQUIRE(sizes[0] + sizes[1] > 0);
    CHECK(std::abs(static_cast<double>(sizes[1]) / (sizes[0] + sizes[1]) - 0.42) < 0.02);
    tabs->setCurrentIndex(0);
    tabs->setCurrentIndex(1);
    CHECK(editor.hSplitter()->sizes() == sizes);
  }

  SECTION("tool layout retains pinned height and uses stable tool ids")
  {
    auto* tools = window.findChild<WorkspaceToolPanel*>();
    auto* mode = window.findChild<QComboBox*>("ProjectToolsMode");
    auto* splitter = window.findChild<QSplitter*>("ProjectToolsSplitter");
    REQUIRE(tools);
    REQUIRE(mode);
    REQUIRE(splitter);
    tools->showInfoPage(InfoPanelPage::PythonConsole);
    mode->setCurrentIndex(1);
    QApplication::processEvents();
    splitter->setSizes({350, 200});
    QMetaObject::invokeMethod(splitter, "splitterMoved", Q_ARG(int, 350), Q_ARG(int, 1));
    auto settings = QSettings{};
    const auto saved = loadToolLayout(settings, WorkspaceKind::Project);
    CHECK(saved.pinned);
    CHECK(saved.expanded);
    CHECK(saved.tool == "python-console");
    const auto sizes = splitter->sizes();
    CHECK(
      std::abs(saved.heightRatio - static_cast<double>(sizes[1]) / (sizes[0] + sizes[1]))
      < 0.01);
    mode->setCurrentIndex(0);
    mode->setCurrentIndex(1);
    QApplication::processEvents();
    const auto restored = splitter->sizes();
    CHECK(
      std::abs(
        saved.heightRatio
        - static_cast<double>(restored[1]) / (restored[0] + restored[1]))
      < 0.02);
    const auto modified = session->modified();
    tools->resetLayout();
    CHECK(mode->currentIndex() == 0);
    CHECK_FALSE(tools->isExpanded());
    CHECK(tools->currentTool() == WorkspaceToolId::ContentBrowser);
    CHECK(session->modified() == modified);
  }

  SECTION("content browser remembers search independently by asset type")
  {
    auto* panel = window.findChild<ProjectAssetPanel*>();
    REQUIRE(panel);
    auto* type = panel->findChild<QComboBox*>("ModelBrowser_Type");
    auto* search = panel->findChild<QLineEdit*>("ModelBrowser_Search");
    REQUIRE(type);
    REQUIRE(search);
    search->setText("all resources");
    type->setCurrentIndex(type->findData(int(BrowserCellType::Sound)));
    CHECK(search->text().isEmpty());
    search->setText("ambience");
    type->setCurrentIndex(type->findData(int(BrowserCellType::Model)));
    CHECK(search->text().isEmpty());
    search->setText("props");
    type->setCurrentIndex(type->findData(int(BrowserCellType::Sound)));
    CHECK(search->text() == "ambience");
    type->setCurrentIndex(type->findData(-1));
    CHECK(search->text() == "all resources");
  }

  SECTION("content browser restores folder and scrolling after a type or map switch")
  {
    for (auto i = 0; i < 60; ++i)
    {
      auto input = project::AssetImport{};
      input.type = project::AssetType::Sound;
      input.virtualPath = "sound/clip" + std::to_string(i) + ".wav";
      input.displayName = "Clip " + std::to_string(i);
      input.original = input.compatible = {1, 2, 3};
      require(session->importAsset(input));
    }
    emit session->browserFocusRequested();
    auto* browser = window.findChild<ModelBrowser*>();
    REQUIRE(browser);
    auto* source = browser->findChild<QComboBox*>("ModelBrowser_Source");
    auto* type = browser->findChild<QComboBox*>("ModelBrowser_Type");
    auto* scroll = browser->findChild<QScrollBar*>("ModelBrowser_ScrollBar");
    auto* view = browser->findChild<ModelBrowserView*>();
    auto* folders = browser->findChild<QTreeWidget*>("ModelBrowser_FolderTree");
    REQUIRE(source);
    REQUIRE(type);
    REQUIRE(scroll);
    REQUIRE(view);
    REQUIRE(folders);
    source->setCurrentIndex(source->findData(int(BrowserSourceFilter::Project)));
    type->setCurrentIndex(type->findData(int(BrowserCellType::Sound)));
    emit view->folderActivated("sound");
    QTRY_VERIFY(scroll->maximum() > 100);
    scroll->setValue(100);
    type->setCurrentIndex(type->findData(int(BrowserCellType::Model)));
    QApplication::processEvents();
    type->setCurrentIndex(type->findData(int(BrowserCellType::Sound)));
    QTRY_COMPARE(scroll->value(), 100);
    REQUIRE(folders->currentItem());
    CHECK(folders->currentItem()->data(0, Qt::UserRole).toString() == "sound");
    source->setCurrentIndex(source->findData(int(BrowserSourceFilter::GameFiles)));
    QApplication::processEvents();
    source->setCurrentIndex(source->findData(int(BrowserSourceFilter::Project)));
    QTRY_COMPARE(scroll->value(), 100);
    tabs->setCurrentIndex(1);
    QApplication::processEvents();
    QTRY_COMPARE(scroll->value(), 100);
    require(session->save());
  }

  SECTION("sound kind filter narrows the browser to looping or one-shot cues")
  {
    for (const auto& [path, bytes] : std::array{
           std::pair{"sound/loop.wav", wavBytes(true)},
           std::pair{"sound/hit.wav", wavBytes(false)}})
    {
      auto input = project::AssetImport{};
      input.type = project::AssetType::Sound;
      input.virtualPath = path;
      input.displayName = path;
      input.original = input.compatible = bytes;
      require(session->importAsset(input));
    }

    emit session->browserFocusRequested();
    auto* browser = window.findChild<ModelBrowser*>();
    REQUIRE(browser);
    auto* source = browser->findChild<QComboBox*>("ModelBrowser_Source");
    auto* type = browser->findChild<QComboBox*>("ModelBrowser_Type");
    auto* sounds = browser->findChild<QComboBox*>("ModelBrowser_SoundKind");
    auto* view = browser->findChild<ModelBrowserView*>();
    REQUIRE(source);
    REQUIRE(type);
    REQUIRE(sounds);
    REQUIRE(view);
    source->setCurrentIndex(source->findData(int(BrowserSourceFilter::Project)));
    type->setCurrentIndex(type->findData(int(BrowserCellType::Sound)));
    emit view->folderActivated("sound");
    QTRY_COMPARE(view->assets().size(), 2);

    const auto selectKind = [&](const inspection::WaveKind kind) {
      sounds->setCurrentIndex(sounds->findData(int(kind)));
      QApplication::processEvents();
    };
    selectKind(inspection::WaveKind::Looping);
    REQUIRE(view->assets().size() == 1);
    CHECK(view->assets().front().path.filename() == "loop.wav");
    selectKind(inspection::WaveKind::OneShot);
    REQUIRE(view->assets().size() == 1);
    CHECK(view->assets().front().path.filename() == "hit.wav");
    selectKind(inspection::WaveKind::All);
    CHECK(view->assets().size() == 2);
    require(session->save());
  }

  SECTION("current map filter refreshes unsaved sky references and map context")
  {
    auto input = project::AssetImport{};
    input.type = project::AssetType::Skybox;
    input.virtualPath = "skyboxes/live.tbsky";
    input.displayName = "Live sky";
    input.original = input.compatible = {'{', '}'};
    require(session->importAsset(input));
    emit session->browserFocusRequested();
    auto* browser = window.findChild<ModelBrowser*>();
    REQUIRE(browser);
    auto* source = browser->findChild<QComboBox*>("ModelBrowser_Source");
    auto* used = browser->findChild<QToolButton*>("ModelBrowser_UsedInMap");
    auto* folders = browser->findChild<QTreeWidget*>("ModelBrowser_FolderTree");
    REQUIRE(source);
    REQUIRE(used);
    REQUIRE(folders);
    source->setCurrentIndex(source->findData(int(BrowserSourceFilter::Project)));
    used->setChecked(true);
    QApplication::processEvents();
    CHECK(folders->findItems("skyboxes", Qt::MatchRecursive).empty());
    REQUIRE(mdl::setEntityProperty(alphaDocument->map(), "skyname", "live"));
    QTRY_COMPARE(folders->findItems("skyboxes", Qt::MatchRecursive).size(), 1);
    tabs->setCurrentIndex(1);
    QTRY_VERIFY(folders->findItems("skyboxes", Qt::MatchRecursive).empty());
    tabs->setCurrentIndex(0);
    QTRY_COMPARE(folders->findItems("skyboxes", Qt::MatchRecursive).size(), 1);
    require(session->save());
    closeTab(*tabs, 1);
    closeTab(*tabs, 0);
    CHECK_FALSE(used->isEnabled());
    CHECK(used->toolTip().contains("active project map"));
  }

  SECTION("resource details track metadata dependencies and revisions")
  {
    auto input = project::AssetImport{};
    input.type = project::AssetType::Sound;
    input.virtualPath = "sound/details.wav";
    input.displayName = "Details";
    input.sourceName = "original.wav";
    input.original = input.compatible = {1, 2, 3};
    const auto asset = require(session->importAsset(input));
    auto* panel = window.findChild<ProjectAssetPanel*>();
    REQUIRE(panel != nullptr);
    panel->showProjectAsset(QString::fromStdString(asset.id));
    auto* details = panel->findChild<QTreeWidget*>("ProjectAssetDetails");
    REQUIRE(details != nullptr);
    const auto valueFor = [details](const QString& key) {
      const auto items = details->findItems(key, Qt::MatchExactly, 0);
      return items.empty() ? QString{} : items.front()->text(1);
    };
    CHECK(valueFor("Original Source") == "original.wav");
    CHECK(valueFor("Published Path") == "sound/details.wav");
    CHECK(valueFor("Revision") == "1");
    require(session->renameAsset(asset.id, "Renamed"));
    CHECK(valueFor("Name") == "Renamed");
    const auto updated = require(session->replaceAsset(asset.id, input));
    CHECK(valueFor("Revision") == QString::number(updated.revision));
    auto dependent = input;
    dependent.virtualPath = "sound/dependent.wav";
    dependent.displayName = "Dependent";
    const auto captured = require(session->applyAssetCapturePlan(
      {{std::nullopt, dependent, {{0, asset.id, "companion"}}}}));
    CHECK(valueFor("Used By") == "sound/dependent.wav");
    panel->showProjectAsset(QString::fromStdString(captured.front().id));
    CHECK(valueFor("Depends On") == "sound/details.wav");
    require(session->save());

    const auto assetCount = require(session->store().assets()).size();
    const auto mapChanges = alphaDocument->map().modificationCount();
    panel->showProjectAsset(QString::fromStdString(asset.id));
    CHECK(valueFor("Name") == QString::fromStdString(updated.displayName));
    CHECK_FALSE(session->modified());
    CHECK(require(session->store().assets()).size() == assetCount);
    CHECK(alphaDocument->map().modificationCount() == mapChanges);

    auto* browser = panel->findChild<ModelBrowserView*>();
    REQUIRE(browser != nullptr);
    auto foreign = BrowserAsset{};
    foreign.source = BrowserSource::Project;
    foreign.projectId = "another-project";
    foreign.projectAssetId = asset.id;
    emit browser->assetSelected(foreign);
    CHECK(valueFor("Selection") == "The selected asset belongs to another project.");
    CHECK(valueFor("Asset ID").isEmpty());
    CHECK(valueFor("Revision").isEmpty());
    CHECK_FALSE(session->modified());

    auto gameAsset = BrowserAsset{BrowserCellType::Sound, "sound/local.wav"};
    gameAsset.displayName = "Game sound";
    emit browser->assetSelected(gameAsset);
    CHECK(valueFor("Source") == "Game Files");
    CHECK(valueFor("Name") == "Game sound");
    CHECK(valueFor("Asset ID").isEmpty());
    CHECK(valueFor("Depends On").isEmpty());
    CHECK_FALSE(session->modified());

    panel->showProjectAsset(QString::fromStdString(captured.front().id));
    require(session->removeAsset(captured.front().id));
    CHECK(valueFor("Project Snapshot") == "Asset no longer exists");
    CHECK(valueFor("Name").isEmpty());
    CHECK(valueFor("Revision").isEmpty());
    CHECK(valueFor("Depends On").isEmpty());
    require(session->save());
    CHECK(alphaDocument->map().modificationCount() == mapChanges);
  }

  SECTION("sound filters isolate current library content from project snapshots")
  {
    const auto folder = directory.filePath("sound-identity-library");
    REQUIRE(QDir{}.mkdir(folder));
    const auto path = folder + "/clip.wav";
    const auto writeSound = [&](const bool loop) {
      auto file = QFile{path};
      REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      const auto bytes = wavBytes(loop);
      REQUIRE(
        file.write(reinterpret_cast<const char*>(bytes.data()), qint64(bytes.size()))
        == qint64(bytes.size()));
    };
    writeSound(true);
    auto library =
      require(project::AssetLibrary::create(pathFromQString(folder), "Library"));
    require(library->scan());
    require(session->attachLibrary(pathFromQString(folder)));
    const auto resource = require(library->assets()).front();
    auto libraryWindow = AssetLibraryWindow{folder, false, &window};
    libraryWindow.setProjectContext(session);
    libraryWindow.findChild<QPushButton*>("AssetInspector_Capture")->click();
    REQUIRE(
      require(session->projectAssetForLibraryAsset(library->info().id, resource.id)));
    writeSound(false);
    auto source = openLocalAssetLibrary(pathFromQString(folder));
    require(source->rescan());
    auto* browser = window.findChild<ModelBrowser*>();
    REQUIRE(browser);
    auto* sourceChoice = browser->findChild<QComboBox*>("ModelBrowser_Source");
    auto* kind = browser->findChild<QComboBox*>("ModelBrowser_SoundKind");
    auto* view = browser->findChild<ModelBrowserView*>();
    REQUIRE(sourceChoice);
    REQUIRE(kind);
    REQUIRE(view);
    sourceChoice->setCurrentIndex(
      sourceChoice->findData(int(BrowserSourceFilter::AssetLibrary)));
    kind->setCurrentIndex(kind->findData(int(inspection::WaveKind::OneShot)));
    QTRY_COMPARE(view->assets().size(), 1u);
    sourceChoice->setCurrentIndex(
      sourceChoice->findData(int(BrowserSourceFilter::Project)));
    QTRY_VERIFY(view->assets().empty());
    kind->setCurrentIndex(kind->findData(int(inspection::WaveKind::Looping)));
    QTRY_COMPARE(view->assets().size(), 1u);
  }

  SECTION("library prefab inspection uses the bound project reference context")
  {
    const auto folder = directory.filePath("prefab-context-library");
    REQUIRE(QDir{}.mkdir(folder));
    auto file = QFile{folder + "/empty.tbprefab"};
    REQUIRE(file.open(QIODevice::WriteOnly));
    REQUIRE(file.write("{\n\"classname\" \"worldspawn\"\n}\n") > 0);
    file.close();
    auto library =
      require(project::AssetLibrary::create(pathFromQString(folder), "Library"));
    require(library->scan());
    require(session->attachLibrary(pathFromQString(folder)));
    auto source = openLocalAssetLibrary(pathFromQString(folder));
    const auto resource = require(library->assets()).front();
    CHECK_FALSE(
      require(inspection::readLibraryAsset(*source, resource)).prefabIssues.isEmpty());
    CHECK(require(inspection::readLibraryAsset(*source, resource, session.get()))
            .prefabIssues.isEmpty());
    auto asset = BrowserAsset{};
    asset.source = BrowserSource::AssetLibrary;
    asset.libraryId = library->info().id;
    asset.libraryAssetId = resource.id;
    const auto inspected =
      require(inspection::readBrowserAsset(asset, session.get(), *alphaDocument));
    REQUIRE(inspected.prefab);
    CHECK(inspected.prefabIssues.isEmpty());
  }

  SECTION("library thumbnails survive filtering and invalidate on source reload")
  {
    const auto folder = directory.filePath("thumbnail-library");
    REQUIRE(QDir{}.mkdir(folder));
    const auto path = folder + "/color.png";
    auto image = QImage{32, 32, QImage::Format_RGB32};
    image.fill(Qt::red);
    REQUIRE(image.save(path));
    auto library =
      require(project::AssetLibrary::create(pathFromQString(folder), "Library"));
    require(library->scan());
    auto browser = AssetLibraryWindow{folder, false, &window};
    auto* grid = browser.findChild<QListWidget*>("AssetLibraryGrid");
    auto* search = browser.findChild<QLineEdit*>("AssetLibrarySearch");
    REQUIRE(grid);
    REQUIRE(search);
    const auto thumbnail = [grid]() {
      REQUIRE(grid->count() == 1);
      return grid->item(0)
        ->data(AssetLibraryItemDelegate::ThumbnailRole)
        .value<QPixmap>()
        .toImage();
    };
    REQUIRE_FALSE(thumbnail().isNull());
    CHECK(thumbnail().pixelColor(0, 0) == QColor{Qt::red});
    // Filtering must reuse the card image without reading the changed file.
    image.fill(Qt::blue);
    REQUIRE(image.save(path));
    search->setText("absent");
    CHECK(grid->count() == 0);
    search->clear();
    CHECK(thumbnail().pixelColor(0, 0) == QColor{Qt::red});
    auto source = openLocalAssetLibrary(pathFromQString(folder));
    REQUIRE(source);
    require(source->rescan());
    QTRY_COMPARE(thumbnail().pixelColor(0, 0), QColor{Qt::blue});
  }

  SECTION("deleting a library asset survives a rescan")
  {
    const auto folder = directory.filePath("delete-library");
    REQUIRE(QDir{}.mkdir(folder));
    const auto path = folder + "/color.png";
    auto image = QImage{32, 32, QImage::Format_RGB32};
    image.fill(Qt::red);
    REQUIRE(image.save(path));
    auto library =
      require(project::AssetLibrary::create(pathFromQString(folder), "Library"));
    require(library->scan());

    auto browser = AssetLibraryWindow{folder, false, &window};
    auto* grid = browser.findChild<QListWidget*>("AssetLibraryGrid");
    auto* remove = browser.findChild<QPushButton*>("AssetLibraryDeleteButton");
    REQUIRE(grid);
    REQUIRE(remove);
    QTRY_COMPARE(grid->count(), 1);
    grid->setCurrentRow(0);
    QTRY_VERIFY(remove->isEnabled());

    auto timer = QTimer{};
    QObject::connect(&timer, &QTimer::timeout, [] {
      for (auto* widget : QApplication::topLevelWidgets())
        if (auto* message = dynamic_cast<QMessageBox*>(widget);
            message && message->isVisible())
          if (auto* yes = message->button(QMessageBox::Yes))
            yes->click();
    });
    timer.start(10);
    remove->click();
    timer.stop();
    QTRY_COMPARE(grid->count(), 0);

    // The source file stays on disk, but rescanning the folder must not resurrect the
    // catalog entry that the user deleted.
    auto source = openLocalAssetLibrary(pathFromQString(folder));
    REQUIRE(source);
    require(source->rescan());
    CHECK(require(source->assets()).empty());
    CHECK(QFile::exists(path));
  }

  SECTION("library and project browsers share metadata capture and identity navigation")
  {
    const auto folder = directory.filePath("shared-library");
    REQUIRE(QDir{}.mkpath(folder + "/sound"));
    for (const auto& [name, loop] :
         std::array{std::pair{"clip", true}, std::pair{"unused", false}})
    {
      auto file = QFile{folder + "/sound/" + name + ".wav"};
      REQUIRE(file.open(QIODevice::WriteOnly));
      const auto bytes = wavBytes(loop);
      REQUIRE(
        file.write(reinterpret_cast<const char*>(bytes.data()), qint64(bytes.size()))
        == qint64(bytes.size()));
    }
    auto library =
      require(project::AssetLibrary::create(pathFromQString(folder), "Shared library"));
    require(library->scan());
    const auto resources = require(library->assets());
    const auto found =
      std::ranges::find(resources, "sound/clip.wav", &project::LibraryAsset::virtualPath);
    REQUIRE(found != resources.end());
    const auto resource = *found;
    const auto libraryId = library->info().id;
    require(session->attachLibrary(pathFromQString(folder)));
    auto source = openLocalAssetLibrary(pathFromQString(folder));
    REQUIRE(source);
    require(source->setFavorite(resource.id, true));
    require(source->setTags(resource.id, {"Industrial", "Ambience"}));
    auto libraryWindow = AssetLibraryWindow{folder, false, &window};
    libraryWindow.setProjectContext(session);
    require(libraryWindow.revealAsset(libraryId, resource.id));
    auto* grid = libraryWindow.findChild<QListWidget*>("AssetLibraryGrid");
    auto* capture = libraryWindow.findChild<QPushButton*>("AssetInspector_Capture");
    auto* usage = libraryWindow.findChild<QListWidget*>("AssetLibraryUsage");
    REQUIRE(grid);
    REQUIRE(capture);
    REQUIRE(usage);
    REQUIRE(grid->currentItem());
    CHECK(libraryWindow.revealAsset("different-library", resource.id).is_error());
    CHECK(libraryWindow.revealAsset(libraryId, "missing-resource").is_error());
    const auto visibleIds = [grid]() {
      auto ids = std::vector<std::string>{};
      for (auto row = 0; row < grid->count(); ++row)
        ids.push_back(grid->item(row)
                        ->data(AssetLibraryItemDelegate::AssetIdRole)
                        .toString()
                        .toStdString());
      return ids;
    };
    auto* soundKind = libraryWindow.findChild<QComboBox*>("AssetLibrary_SoundKind");
    REQUIRE(soundKind);
    REQUIRE(visibleIds().size() == 2);
    soundKind->setCurrentIndex(soundKind->findData(int(inspection::WaveKind::Looping)));
    QApplication::processEvents();
    REQUIRE(visibleIds().size() == 1);
    CHECK(visibleIds().front() == resource.id);
    soundKind->setCurrentIndex(soundKind->findData(int(inspection::WaveKind::OneShot)));
    QApplication::processEvents();
    REQUIRE(visibleIds().size() == 1);
    CHECK(visibleIds().front() != resource.id);
    soundKind->setCurrentIndex(0);
    QApplication::processEvents();
    CHECK(visibleIds().size() == 2);
    auto* retry = libraryWindow.findChild<QPushButton*>("AssetLibraryRetryFailed");
    REQUIRE(retry);
    CHECK(retry->isHidden());
    CHECK(
      grid->currentItem()
        ->data(AssetLibraryItemDelegate::AssetIdRole)
        .toString()
        .toStdString()
      == resource.id);
    CHECK_FALSE(capture->isHidden());
    capture->click();
    const auto snapshot =
      require(session->projectAssetForLibraryAsset(libraryId, resource.id));
    REQUIRE(snapshot);
    auto* libraryInspector = libraryWindow.findChild<AssetInspector*>();
    REQUIRE(libraryInspector);
    REQUIRE(libraryInspector->asset());
    CHECK(libraryInspector->asset()->resource.projectRevision == snapshot->revision);
    CHECK(capture->isHidden());
    CHECK_FALSE(alphaDocument->map().modified());
    require(session->save());
    const auto before =
      require(session->store().readAsset(snapshot->id, project::ContentKind::Compatible));
    (void)mdl::paste(
      alphaDocument->map(),
      R"({"classname" "ambient_generic" "origin" "0 0 32" "message" "clip.wav"})");
    REQUIRE(alphaDocument->map().modified());
    QTRY_COMPARE(usage->findItems("maps/alpha.map", Qt::MatchContains).size(), 1);
    REQUIRE(mdl::setEntityProperty(alphaDocument->map(), "message", "unused.wav"));
    QTRY_VERIFY(usage->findItems("maps/alpha.map", Qt::MatchContains).empty());
    REQUIRE(mdl::setEntityProperty(alphaDocument->map(), "message", "clip.wav"));
    QTRY_COMPARE(usage->findItems("maps/alpha.map", Qt::MatchContains).size(), 1);
    const auto* scope = libraryWindow.findChild<QLabel*>("AssetLibrary_ProjectScope");
    REQUIRE(scope);
    CHECK(scope->text().contains(pathAsQString(projectPath)));
    tabs->setCurrentIndex(1);
    QApplication::processEvents();
    emit usage->itemDoubleClicked(
      usage->findItems("maps/alpha.map", Qt::MatchContains).front());
    CHECK(&window.activeMapWindow()->document() == alphaDocument);
    auto* showSnapshot =
      libraryWindow.findChild<QPushButton*>("AssetInspector_ShowSnapshot");
    REQUIRE(showSnapshot);
    CHECK_FALSE(showSnapshot->isHidden());
    showSnapshot->click();
    auto* panel = window.findChild<ProjectAssetPanel*>();
    auto* browser = panel->findChild<ModelBrowser*>();
    auto* inspected = panel->findChild<AssetInspector*>();
    REQUIRE(browser);
    REQUIRE(inspected);
    REQUIRE(inspected->asset());
    CHECK(inspected->asset()->resource.projectAssetId == snapshot->id);
    auto* showLibrary = inspected->findChild<QPushButton*>("AssetInspector_ShowLibrary");
    REQUIRE(showLibrary);
    CHECK_FALSE(showLibrary->isHidden());
    showLibrary->click();
    auto* linkedWindow = browser->findChild<AssetLibraryWindow*>();
    REQUIRE(linkedWindow);
    auto* linkedGrid = linkedWindow->findChild<QListWidget*>("AssetLibraryGrid");
    REQUIRE(linkedGrid);
    REQUIRE(linkedGrid->currentItem());
    CHECK(
      linkedGrid->currentItem()
        ->data(AssetLibraryItemDelegate::AssetIdRole)
        .toString()
        .toStdString()
      == resource.id);
    auto* favoriteFilter = browser->findChild<QToolButton*>("ModelBrowser_Favorites");
    auto* tags = browser->findChild<QComboBox*>("ModelBrowser_Tag");
    auto* folders = browser->findChild<QTreeWidget*>("ModelBrowser_FolderTree");
    REQUIRE(favoriteFilter);
    REQUIRE(tags);
    REQUIRE(folders);
    favoriteFilter->setChecked(true);
    QTRY_VERIFY(tags->findData("Industrial") >= 0);
    tags->setCurrentIndex(tags->findData("Industrial"));
    QTRY_COMPARE(folders->findItems("sound", Qt::MatchRecursive).size(), 1);
    auto* favorite = linkedWindow->findChild<QPushButton*>("AssetLibraryFavoriteButton");
    REQUIRE(favorite);
    CHECK(favorite->isChecked());
    favorite->click();
    QTRY_VERIFY(folders->findItems("sound", Qt::MatchRecursive).empty());
    QTRY_VERIFY(
      !libraryWindow.findChild<QPushButton*>("AssetLibraryFavoriteButton")->isChecked());
    require(source->setFavorite(resource.id, true));
    QTRY_COMPARE(folders->findItems("sound", Qt::MatchRecursive).size(), 1);
    auto* librarySearch = libraryWindow.findChild<QLineEdit*>("AssetLibrarySearch");
    REQUIRE(librarySearch);
    librarySearch->setText("ambience");
    CHECK(grid->count() == 1);
    require(source->setTags(resource.id, {"Mechanical"}));
    QTRY_COMPARE(grid->count(), 0);
    CHECK(
      require(session->store().findAsset(snapshot->id))->revision == snapshot->revision);
    CHECK(
      require(session->store().readAsset(snapshot->id, project::ContentKind::Compatible))
      == before);
    require(libraryWindow.revealAsset(libraryId, resource.id));
    auto replacement = QFile{folder + "/sound/clip.wav"};
    REQUIRE(replacement.open(QIODevice::WriteOnly | QIODevice::Truncate));
    REQUIRE(replacement.write("RIFF changed source") > 0);
    replacement.close();
    require(source->rescan());
    auto* update = libraryWindow.findChild<QPushButton*>("AssetInspector_Update");
    REQUIRE(update);
    QTRY_VERIFY(!update->isHidden());
    CHECK(
      require(session->store().readAsset(snapshot->id, project::ContentKind::Compatible))
      == before);
    update->click();
    CHECK(
      require(session->store().findAsset(snapshot->id))->revision > snapshot->revision);
    CHECK(
      libraryInspector->asset()->resource.projectRevision
      == require(session->store().findAsset(snapshot->id))->revision);
    CHECK(
      require(session->store().readAsset(snapshot->id, project::ContentKind::Compatible))
      != before);
    libraryWindow.setProjectContext(nullptr);
    CHECK(
      libraryWindow.findChild<QPushButton*>("AssetInspector_ShowSnapshot")->isHidden());
    CHECK(capture->isHidden());
    require(session->save());
  }

  SECTION("library details distinguish unavailable sources from captured snapshots")
  {
    const auto folder = directory.filePath("details-library");
    REQUIRE(QDir{}.mkdir(folder));
    auto file = QFile{folder + "/clip.wav"};
    REQUIRE(file.open(QIODevice::WriteOnly));
    REQUIRE(file.write("RIFF preview") > 0);
    file.close();
    auto library =
      require(project::AssetLibrary::create(pathFromQString(folder), "Library"));
    require(library->scan());
    const auto resources = require(library->assets());
    REQUIRE(resources.size() == 1);
    const auto resource = resources.front();
    const auto libraryId = library->info().id;
    require(session->attachLibrary(pathFromQString(folder)));
    const auto snapshot = require(session->captureLibraryAsset(libraryId, resource.id));
    require(session->save());
    auto* panel = window.findChild<ProjectAssetPanel*>();
    REQUIRE(panel != nullptr);
    auto* browser = panel->findChild<ModelBrowserView*>();
    auto* details = panel->findChild<QTreeWidget*>("ProjectAssetDetails");
    REQUIRE(browser != nullptr);
    REQUIRE(details != nullptr);
    const auto valueFor = [details](const QString& key) {
      const auto items = details->findItems(key, Qt::MatchExactly, 0);
      return items.empty() ? QString{} : items.front()->text(1);
    };
    auto selected = BrowserAsset{BrowserCellType::Sound, "clip.wav"};
    selected.source = BrowserSource::AssetLibrary;
    selected.libraryId = libraryId;
    selected.libraryAssetId = resource.id;
    selected.libraryRevision = 987;
    selected.libraryContentHash = "stale-hash";
    emit browser->assetSelected(selected);
    CHECK(valueFor("Library Revision") == QString::number(resource.revision));
    CHECK(
      valueFor("Library Content Hash") == QString::fromStdString(resource.contentHash));
    CHECK(valueFor("Asset ID") == QString::fromStdString(snapshot.id));
    CHECK(valueFor("Availability") == "Available");

    library.reset();
    REQUIRE(QDir{}.rename(folder, folder + "-moved"));
    emit session->libraryLinksChanged();
    CHECK(valueFor("Availability") == "Library unavailable");
    CHECK(details->findItems("Availability", Qt::MatchExactly, 0).size() == 1);
    CHECK(valueFor("Library Revision").isEmpty());
    CHECK(valueFor("Library Content Hash").isEmpty());
    CHECK(valueFor("Asset ID") == QString::fromStdString(snapshot.id));
    CHECK(valueFor("Captured Revision") == QString::number(resource.revision));
    CHECK(valueFor("Content Hash") == QString::fromStdString(snapshot.contentHash));

    REQUIRE(QDir{}.rename(folder + "-moved", folder));
    selected.libraryAssetId = "removed-entry";
    selected.projectAssetId = snapshot.id;
    emit browser->assetSelected(selected);
    CHECK(valueFor("Availability") == "Asset no longer exists in library");
    CHECK(valueFor("Library Revision").isEmpty());
    CHECK(valueFor("Library Content Hash").isEmpty());
    CHECK(valueFor("Asset ID").isEmpty());
    CHECK(valueFor("Project Snapshot") == "Not captured");
    CHECK_FALSE(session->modified());
  }

  SECTION("published rename works after saved map tabs close")
  {
    auto input = project::AssetImport{};
    input.type = project::AssetType::Sound;
    input.virtualPath = "sound/test.wav";
    input.displayName = "Test";
    input.original = input.compatible = {1, 2, 3};
    const auto sound = require(session->importAsset(input));
    require(session->save());
    closeTab(*tabs, 1);
    closeTab(*tabs, 0);
    REQUIRE(session->retainedDocument(alphaId) != nullptr);
    emit session->browserAssetSelected(QString::fromStdString(sound.id));
    QAction* rename = nullptr;
    for (auto* action : window.findChildren<QAction*>())
      if (action->text() == "Rename Published Path…")
        rename = action;
    REQUIRE(rename != nullptr);
    QTimer::singleShot(0, [] {
      if (auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget()))
      {
        dialog->setTextValue("sound/renamed.wav");
        dialog->accept();
      }
    });
    rename->trigger();
    const auto updated = require(session->store().findAsset(sound.id));
    REQUIRE(updated.has_value());
    CHECK(updated->publishPath == "sound/renamed.wav");
    CHECK(session->retainedDocument(alphaId) == nullptr);
  }

  SECTION("browser selection drives the project asset command target")
  {
    auto* assets = window.findChild<QTreeWidget*>("ProjectAssets");
    REQUIRE(assets != nullptr);
    emit session->browserAssetSelected(QString::fromStdString(alphaId));
    REQUIRE(assets->currentItem() != nullptr);
    CHECK(
      assets->currentItem()->data(0, Qt::UserRole).toString().toStdString() == alphaId);
    emit session->browserAssetSelected(QString{});
    CHECK(assets->currentItem() == nullptr);
    emit session->browserAssetSelected("missing-resource");
    CHECK(assets->currentItem() == nullptr);
  }

  SECTION("tabs switch embedded pages and preserve reordered tabs after edits")
  {
    tabs->setCurrentIndex(1);
    auto* betaWindow = window.activeMapWindow();
    REQUIRE(betaWindow != nullptr);
    CHECK(betaWindow != alphaWindow);
    CHECK(manager.topMapWindow() == betaWindow);
    CHECK_FALSE(alphaWindow->isVisible());
    tabs->moveTab(1, 0);
    REQUIRE(mdl::setEntityProperty(betaWindow->document().map(), "message", "beta edit"));
    QApplication::processEvents();
    CHECK(tabs->tabText(0) == "beta *");
    CHECK(stack->count() == 2);
    CHECK(window.activeMapWindow() == betaWindow);
    tabs->setCurrentIndex(1);
    CHECK(window.activeMapWindow() == alphaWindow);
    CHECK(manager.topMapWindow() == alphaWindow);
    CHECK_FALSE(betaWindow->isVisible());
    require(session->save());
  }

  SECTION("focus keeps each map's current viewport and project controls select their map")
  {
    auto* alphaView = alphaWindow->currentMapViewBase();
    tabs->setCurrentIndex(1);
    auto* betaWindow = window.activeMapWindow();
    REQUIRE(betaWindow != nullptr);
    auto* betaView = betaWindow->currentMapViewBase();
    QMetaObject::invokeMethod(
      qApp, "focusChanged", Q_ARG(QWidget*, nullptr), Q_ARG(QWidget*, betaView));
    CHECK(alphaWindow->currentMapViewBase() == alphaView);
    CHECK(betaWindow->currentMapViewBase() == betaView);
    tabs->setCurrentIndex(0);
    auto child = QWidget{&window};
    QMetaObject::invokeMethod(
      qApp, "focusChanged", Q_ARG(QWidget*, nullptr), Q_ARG(QWidget*, &child));
    CHECK(manager.topMapWindow() == alphaWindow);
  }

  SECTION("the project window no longer offers per-project library management")
  {
    for (auto* menu : window.findChildren<QMenu*>())
      CHECK(menu->title() != "Library");
  }

  SECTION("maps created after the host opens become embedded active tabs")
  {
    require(manager.createProjectMap(session, "gamma", mdl::MapFormat::Valve));
    QApplication::processEvents();
    REQUIRE(stack->count() == 3);
    REQUIRE(window.activeMapWindow() != nullptr);
    CHECK(window.activeMapWindow()->window() == &window);
    CHECK(tabs->tabText(tabs->currentIndex()) == "gamma");
    CHECK(manager.topMapWindow() == window.activeMapWindow());
  }


  SECTION(
    "catalog models render from isolated files and a missing catalog model never uses "
    "game files")
  {
    const auto file = directory.filePath("catalog.obj");
    auto output = QSaveFile{file};
    REQUIRE(output.open(QIODevice::WriteOnly));
    REQUIRE(output.write("v 0 0 0\nv 8 0 0\nv 0 8 0\nf 1 2 3\n") > 0);
    REQUIRE(output.commit());
    alphaWindow->switchToInfoPanelPage(InfoPanelPage::Assets);
    QApplication::processEvents();
    auto* browser = window.findChild<ModelBrowserView*>();
    REQUIRE(browser != nullptr);
    browser->setIncludeDefaultRoots(false);
    auto asset =
      BrowserAsset{BrowserCellType::Model, "catalog.obj", pathFromQString(file)};
    asset.source = BrowserSource::Project;
    asset.projectId = "preview-project";
    asset.assetId = "preview-model";
    browser->setAssets({}, {asset});
    REQUIRE(QTest::qWaitFor([&]() { return browser->isVisible(); }, 3000));
    QTest::qWait(100);
    const auto rendered = browser->grabFramebuffer();
    REQUIRE_FALSE(rendered.isNull());
    asset.absolutePath = pathFromQString(directory.filePath("missing/catalog.obj"));
    browser->setAssets({}, {asset});
    QApplication::processEvents();
    const auto missing = browser->grabFramebuffer();
    REQUIRE_FALSE(missing.isNull());
    CHECK(rendered != missing);
    // Switch back after a failed load to exercise retiring model renderers.
    asset.absolutePath = pathFromQString(file);
    browser->setAssets({}, {asset});
    QApplication::processEvents();
    CHECK(
      QTest::qWaitFor([&]() { return browser->grabFramebuffer() == rendered; }, 3000));
  }

  SECTION("refreshing asset cards preserves live map model renderers")
  {
    auto input = project::AssetImport{};
    input.type = project::AssetType::Model;
    input.virtualPath = "models/refresh.obj";
    input.displayName = "Refresh model";
    const auto obj = std::string{"v 0 0 0\nv 32 0 0\nv 0 32 0\nf 1 2 3\n"};
    input.original.assign(obj.begin(), obj.end());
    input.compatible = input.original;
    (void)require(session->importAsset(input));

    auto& map = alphaDocument->map();
    map.entityDefinitionManager().setDefinitions(
      {{"refresh_model",
        {},
        {},
        {},
        mdl::PointEntityDefinition{
          vm::bbox3d{16.0},
          mdl::ModelDefinition{
            el::ExpressionNode{el::LiteralExpression{el::Value{"models/refresh.obj"}}}},
          {}}}});
    auto* entity = new mdl::EntityNode{mdl::Entity{{{"classname", "refresh_model"}}}};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {entity}}});
    require(session->save());
    auto& view = alphaWindow->activate3DMapView();
    REQUIRE(QTest::qWaitFor(
      [&]() {
        view.grabFramebuffer();
        return entity->entity().model() && entity->entity().model()->data()
               && map.entityModelManager().renderer({"models/refresh.obj"});
      },
      3000));
    REQUIRE_FALSE(view.grabFramebuffer().isNull());

    auto notifications = 0;
    auto connection = map.nodesDidChangeNotifier.connect([&](const auto& nodes) {
      if (std::ranges::find(nodes, entity) != nodes.end())
        ++notifications;
    });
    alphaWindow->switchToInfoPanelPage(InfoPanelPage::Assets);
    auto* type = window.findChild<QComboBox*>("ModelBrowser_Type");
    REQUIRE(type);
    type->setCurrentIndex(type->findData(int(BrowserCellType::Texture)));
    auto* reload = window.findChild<QToolButton*>("ModelBrowser_ReloadButton");
    REQUIRE(reload);
    auto resourceId = entity->entity().model()->dataResource().id();
    for (auto i = 0; i < 5; ++i)
    {
      reload->click();
      // Unchanged project revisions must not retire model resources merely because
      // the user refreshes the browser, including while viewing only textures.
      REQUIRE(notifications == i);
      REQUIRE(entity->entity().model()->dataResource().id() == resourceId);
      REQUIRE_FALSE(view.grabFramebuffer().isNull());

      map.reloadEntityModels("models/refresh.obj");
      // Assert notification before repainting: stale model/frame pointers must be
      // replaced even while their asynchronous replacement is not ready yet.
      REQUIRE(notifications == i + 1);
      REQUIRE_FALSE(view.grabFramebuffer().isNull());
      REQUIRE(QTest::qWaitFor(
        [&]() {
          view.grabFramebuffer();
          return entity->entity().model()->data()
                 && map.entityModelManager().renderer({"models/refresh.obj"});
        },
        3000));
      resourceId = entity->entity().model()->dataResource().id();
    }
    CHECK_FALSE(map.modified());
  }

  SECTION("catalog titles render distinct Chinese glyphs")
  {
    auto* browser = window.findChild<ModelBrowserView*>();
    REQUIRE(browser != nullptr);
    browser->setIncludeDefaultRoots(false);
    auto asset = BrowserAsset{BrowserCellType::Map, "unicode.map"};
    asset.source = BrowserSource::Project;
    asset.projectId = "unicode";
    asset.assetId = "map";
    asset.displayName = "测试";
    browser->setAssets({}, {asset});
    QApplication::processEvents();
    const auto first = browser->grabFramebuffer();
    REQUIRE_FALSE(first.isNull());
    asset.displayName = "资源";
    browser->setAssets({}, {asset});
    QApplication::processEvents();
    const auto second = browser->grabFramebuffer();
    REQUIRE_FALSE(second.isNull());
    CHECK(first != second);
  }

  SECTION("catalog resource drag captures only when the native drop commits")
  {
    const auto folder = directory.filePath("drop-library");
    REQUIRE(QDir{}.mkdir(folder));
    auto output = QFile{folder + "/clip.wav"};
    REQUIRE(output.open(QIODevice::WriteOnly));
    REQUIRE(output.write("RIFF data") == 9);
    output.close();
    auto library =
      require(project::AssetLibrary::create(pathFromQString(folder), "Library"));
    require(library->scan());
    const auto resources = require(library->assets());
    REQUIRE(resources.size() == 1);
    require(session->attachLibrary(pathFromQString(folder)));
    require(session->save());
    auto& map = alphaDocument->map();
    map.entityDefinitionManager().setDefinitions(
      {{"ambient_generic",
        Color{},
        "sound",
        {{"message", mdl::PropertyValueTypes::String{}, "", "", false}},
        mdl::PointEntityDefinition{vm::bbox3d{16.0}, {}, {}}}});
    auto asset = BrowserAsset{BrowserCellType::Sound, "clip.wav"};
    asset.source = BrowserSource::AssetLibrary;
    asset.libraryId = library->info().id;
    asset.libraryAssetId = resources.front().id;
    asset.libraryRevision = resources.front().revision;
    const auto payload = catalogAssetDragData(asset, session->store().info().id);
    auto mime = QMimeData{};
    mime.setText(payload);
    auto* view = alphaWindow->currentMapViewBase();
    REQUIRE(view != nullptr);
    REQUIRE(
      alphaWindow->toolBox().createEntityTool().canCreateSoundEntity("sound/clip.wav"));
    auto prepared = require(PreparedAssetDrop::prepare(session, payload));
    CHECK(prepared->nativePayload() == "sound:sound/clip.wav");
    REQUIRE(view->dragEnter(40.0f, 40.0f, prepared->nativePayload()));
    view->dragLeave();
    const auto position = view->rect().center();
    const auto before = require(session->store().assets()).size();
    auto enter =
      QDragEnterEvent{position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier};
    QApplication::sendEvent(view, &enter);
    REQUIRE(enter.isAccepted());
    CHECK(require(session->store().assets()).size() == before);
    auto leave = QDragLeaveEvent{};
    QApplication::sendEvent(view, &leave);
    CHECK(require(session->store().assets()).size() == before);
    CHECK_FALSE(session->modified());

    SECTION("successful drop")
    {
      auto again =
        QDragEnterEvent{position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier};
      QApplication::sendEvent(view, &again);
      REQUIRE(again.isAccepted());
      auto drop = QDropEvent{
        QPointF{position}, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier};
      QApplication::sendEvent(view, &drop);
      CHECK(drop.isAccepted());
      CHECK(require(session->store().assets()).size() == before + 1);
      CHECK(map.modified());
      CHECK(session->modified());
      require(session->save());
    }
    SECTION("a conflicting capture rolls back the provisional entity")
    {
      auto input = project::AssetImport{};
      input.type = project::AssetType::Sound;
      input.virtualPath = "sound/clip.wav";
      input.displayName = "Existing";
      input.original = {'x'};
      input.compatible = {'x'};
      require(session->importAsset(input));
      require(session->save());
      auto again =
        QDragEnterEvent{position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier};
      QApplication::sendEvent(view, &again);
      REQUIRE(again.isAccepted());
      auto drop = QDropEvent{
        QPointF{position}, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier};
      QApplication::sendEvent(view, &drop);
      CHECK_FALSE(drop.isAccepted());
      CHECK(require(session->store().assets()).size() == before + 1);
      CHECK_FALSE(map.modified());
      CHECK_FALSE(session->modified());
    }
  }

  SECTION("the browser reopens a closed project map as a workspace tab")
  {
    auto* document = alphaDocument;
    const auto mapAsset = require(session->store().findAsset(alphaId));
    REQUIRE(mapAsset);
    REQUIRE(mapAsset->type == project::AssetType::Map);

    tabs->setCurrentIndex(1);
    QApplication::processEvents();
    auto* betaWindow = window.activeMapWindow();
    REQUIRE(betaWindow != nullptr);
    betaWindow->switchToInfoPanelPage(InfoPanelPage::Assets);
    auto* browser = window.findChild<ModelBrowserView*>();
    REQUIRE(browser != nullptr);
    auto* source = window.findChild<QComboBox*>("ModelBrowser_Source");
    REQUIRE(source != nullptr);
    source->setCurrentIndex(source->findData(int(BrowserSourceFilter::Project)));

    closeTab(*tabs, 0);
    REQUIRE(tabs->count() == 1);
    REQUIRE(session->documentFor(alphaId) == document);

    // The browser asks the map window manager for the map the same way a double click
    // does.
    emit browser->mapActivated(pathAsGenericQString(mapAsset->publishPath));
    QApplication::processEvents();
    REQUIRE(tabs->count() == 2);
    auto names = std::vector<QString>{};
    for (auto index = 0; index < tabs->count(); ++index)
      names.push_back(tabs->tabText(index));
    CHECK(std::ranges::find(names, QString{"alpha"}) != names.end());
    CHECK(tabs->tabText(tabs->currentIndex()) == "alpha");
    CHECK(session->documentFor(alphaId) == document);
  }

  SECTION("auto-saves a recovery snapshot for unsaved project work")
  {
    CHECK_FALSE(hasRecoverableProjectSnapshot(session->path()));
    auto* timer = window.findChild<QTimer*>("ProjectAutosaveTimer");
    REQUIRE(timer != nullptr);

    auto input = project::AssetImport{};
    input.type = project::AssetType::Sound;
    input.virtualPath = "sound/recovery.wav";
    input.displayName = "Recovery";
    input.original = {9};
    input.compatible = {9};
    (void)require(session->importAsset(input));
    REQUIRE(session->modified());

    QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection);
    CHECK(std::filesystem::exists(projectRecoveryPath(session->path())));
    CHECK(hasRecoverableProjectSnapshot(session->path()));

    QAction* saveAction = nullptr;
    for (auto* menuAction : window.menuBar()->actions())
      if (auto* menu = menuAction->menu())
        for (auto* action : menu->actions())
          if (action->text() == "Save Project")
            saveAction = action;
    REQUIRE(saveAction != nullptr);
    saveAction->trigger();

    CHECK_FALSE(session->modified());
    CHECK_FALSE(std::filesystem::exists(projectRecoveryPath(session->path())));
  }

  SECTION("catalog skybox drops capture the group and set the world sky")
  {
    const auto folder = directory.filePath("skybox-library");
    REQUIRE(QDir{}.mkdir(folder));
    REQUIRE(QDir{}.mkdir(folder + "/env"));
    for (const auto* face : {"bk", "rt", "lf", "ft", "up", "dn"})
    {
      auto image = QImage{8, 8, QImage::Format_RGBA8888};
      image.fill(QColor{80, 120, 160});
      auto buffer = QBuffer{};
      REQUIRE(buffer.open(QIODevice::WriteOnly));
      REQUIRE(image.save(&buffer, "PNG"));
      auto output = QFile{folder + "/env/desert" + face + ".png"};
      REQUIRE(output.open(QIODevice::WriteOnly));
      REQUIRE(output.write(buffer.data()) == buffer.data().size());
    }
    auto library =
      require(project::AssetLibrary::create(pathFromQString(folder), "Skyboxes"));
    require(library->scan());
    const auto entries = require(library->assets());
    const auto skybox = std::ranges::find_if(entries, [](const auto& entry) {
      return entry.type == project::AssetType::Skybox;
    });
    REQUIRE(skybox != entries.end());
    require(session->attachLibrary(pathFromQString(folder)));
    require(session->save());

    auto texture = BrowserAsset{BrowserCellType::Texture, {}};
    texture.skybox = true;
    texture.source = BrowserSource::AssetLibrary;
    texture.libraryId = library->info().id;
    texture.libraryAssetId = skybox->id;
    texture.libraryRevision = skybox->revision;
    const auto payload = catalogAssetDragData(texture, session->store().info().id);
    REQUIRE(isCatalogAssetDrag(payload));

    auto* view = alphaWindow->currentMapViewBase();
    REQUIRE(view != nullptr);
    auto mime = QMimeData{};
    mime.setText(payload);
    const auto position = QPoint{1, 1};
    auto enter =
      QDragEnterEvent{position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier};
    QApplication::sendEvent(view, &enter);
    REQUIRE(enter.isAccepted());
    auto drop = QDropEvent{
      QPointF{position}, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier};
    QApplication::sendEvent(view, &drop);
    CHECK(drop.isAccepted());

    auto& map = alphaDocument->map();
    REQUIRE(map.worldNode().entity().property("skyname"));
    CHECK(*map.worldNode().entity().property("skyname") == "desert");
    CHECK(map.modified());
    const auto assets = require(session->store().assets());
    CHECK(
      std::ranges::count_if(
        assets,
        [](const auto& asset) { return asset.type == project::AssetType::Skybox; })
      == 1);

    REQUIRE(map.canUndoCommand());
    map.undoCommand();
    CHECK(!map.worldNode().entity().property("skyname"));
  }

  SECTION("catalog texture drops capture the dropped revision and set the face material")
  {
    auto& map = alphaDocument->map();
    const auto points = std::vector<vm::vec3d>{
      {-64, -64, -64},
      {-64, -64, +64},
      {-64, +64, -64},
      {-64, +64, +64},
      {+64, -64, -64},
      {+64, -64, +64},
      {+64, +64, -64},
      {+64, +64, +64},
    };
    REQUIRE(mdl::createBrush(map, points));
    REQUIRE(map.selection().brushes.size() == 1);
    auto* brushNode = map.selection().brushes.front();
    const auto face = mdl::BrushFaceHandle{*brushNode, 0};
    mdl::selectBrushFaces(map, {face});
    REQUIRE(map.selection().brushFaces.size() == 1);
    const auto previous = face.face().materialName();

    const auto folder = directory.filePath("texture-library");
    REQUIRE(QDir{}.mkdir(folder));
    const auto converted = require(img::convertGoldSrcTexture(
      {16, 16, std::vector<unsigned char>(16 * 16 * 4, 255)}, "WALL01"));
    auto wadFile = QFile{folder + "/custom.wad"};
    REQUIRE(wadFile.open(QIODevice::WriteOnly));
    REQUIRE(
      wadFile.write(
        reinterpret_cast<const char*>(converted.wad.data()), qint64(converted.wad.size()))
      == qint64(converted.wad.size()));
    wadFile.close();
    auto library =
      require(project::AssetLibrary::create(pathFromQString(folder), "Textures"));
    require(library->scan());
    const auto entries = require(library->assets());
    REQUIRE(entries.size() == 1);
    require(session->attachLibrary(pathFromQString(folder)));
    require(session->save());

    auto texture = BrowserAsset{BrowserCellType::Texture, {}};
    texture.source = BrowserSource::AssetLibrary;
    texture.libraryId = library->info().id;
    texture.libraryAssetId = entries.front().id;
    texture.libraryRevision = entries.front().revision;
    const auto payload = catalogAssetDragData(texture, session->store().info().id);
    REQUIRE(isCatalogAssetDrag(payload));

    auto* view = alphaWindow->currentMapViewBase();
    REQUIRE(view != nullptr);
    auto mime = QMimeData{};
    mime.setText(payload);
    // The corner is over the void, so the drop falls back to the face selection.
    const auto position = QPoint{1, 1};
    auto enter =
      QDragEnterEvent{position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier};
    QApplication::sendEvent(view, &enter);
    REQUIRE(enter.isAccepted());
    REQUIRE(view->pickResult().empty());
    auto drop = QDropEvent{
      QPointF{position}, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier};
    QApplication::sendEvent(view, &drop);
    CHECK(drop.isAccepted());
    CHECK(map.modified());
    CHECK(face.face().materialName() == "WALL01");
    const auto assets = require(session->store().assets());
    const auto textureAsset = std::ranges::find_if(assets, [](const auto& asset) {
      return asset.type == project::AssetType::Texture;
    });
    REQUIRE(textureAsset != assets.end());
    CHECK(textureAsset->publishPath == "textures/WALL01.wad");
    CHECK(textureAsset->library.libraryAssetId == entries.front().id);

    // Applying a material is one native undo step, so the drop stays reversible.
    REQUIRE(map.canUndoCommand());
    map.undoCommand();
    CHECK(face.face().materialName() == previous);
  }

  SECTION("double-clicking a library resource opens a read-only preview without capture")
  {
    const auto folder = directory.filePath("preview-library");
    REQUIRE(QDir{}.mkdir(folder));
    auto file = QFile{folder + "/clip.wav"};
    REQUIRE(file.open(QIODevice::WriteOnly));
    REQUIRE(file.write("RIFF preview") > 0);
    file.close();
    auto library =
      require(project::AssetLibrary::create(pathFromQString(folder), "Library"));
    require(library->scan());
    require(session->attachLibrary(pathFromQString(folder)));
    require(session->save());
    alphaWindow->switchToInfoPanelPage(InfoPanelPage::Assets);
    auto* source = window.findChild<QComboBox*>("ModelBrowser_Source");
    REQUIRE(source != nullptr);
    source->setCurrentIndex(source->findData(int(BrowserSourceFilter::AssetLibrary)));
    QApplication::processEvents();
    QTest::qWait(100);
    auto* browser = window.findChild<ModelBrowserView*>();
    REQUIRE(browser != nullptr);
    auto sawPreview = false;
    auto close = QTimer{};
    QObject::connect(&close, &QTimer::timeout, [&]() {
      for (auto* widget : QApplication::topLevelWidgets())
        if (auto* dialog = dynamic_cast<QDialog*>(widget); dialog && dialog->isVisible())
        {
          sawPreview = dialog->objectName() == "AssetPreviewDialog";
          dialog->reject();
        }
    });
    const auto before = require(session->store().assets()).size();
    close.start(10);
    QTest::mouseDClick(browser, Qt::LeftButton, Qt::NoModifier, QPoint{40, 40});
    close.stop();
    CHECK(sawPreview);
    CHECK(require(session->store().assets()).size() == before);
    CHECK_FALSE(session->modified());
  }

  SECTION("embedded viewport popup uses its map window actions")
  {
    auto* view = alphaWindow->currentMapViewBase();
    REQUIRE(view != nullptr);
    REQUIRE(view->window() == &window);
    auto sawGroupAction = false;
    auto close = QTimer{};
    QObject::connect(&close, &QTimer::timeout, [&]() {
      for (auto* widget : QApplication::topLevelWidgets())
        if (auto* menu = qobject_cast<QMenu*>(widget); menu && menu->isVisible())
        {
          sawGroupAction =
            menu->actions().contains(alphaWindow->findAction("Menu/Edit/Group"));
          menu->close();
        }
    });
    close.start(10);
    REQUIRE(QMetaObject::invokeMethod(view, "showPopupMenuLater", Qt::DirectConnection));
    close.stop();
    CHECK(sawGroupAction);
  }

  SECTION("host undo follows the active map")
  {
    auto* editMenu = window.menuBar()->actions().at(1)->menu();
    REQUIRE(editMenu != nullptr);
    auto* undo = editMenu->actions().front();
    REQUIRE(mdl::setEntityProperty(alphaDocument->map(), "message", "alpha edit"));
    QApplication::processEvents();
    CHECK(undo->isEnabled());
    tabs->setCurrentIndex(1);
    CHECK_FALSE(undo->isEnabled());
    tabs->setCurrentIndex(0);
    CHECK(undo->isEnabled());
    undo->trigger();
    CHECK_FALSE(alphaWindow->canUndo());
    require(session->save());
  }

  SECTION("failed project save on close preserves views and dirty documents")
  {
    REQUIRE(mdl::setEntityProperty(alphaDocument->map(), "message", "unsaved"));
    const auto path = directory.filePath("workspace.tbproj");
    const auto backup = directory.filePath("backup.tbproj");
    REQUIRE(QFile::rename(path, backup));
    REQUIRE(QDir{}.mkdir(path));
    auto timer = QTimer{};
    QObject::connect(&timer, &QTimer::timeout, [&]() {
      for (auto* widget : QApplication::topLevelWidgets())
        if (auto* message = dynamic_cast<QMessageBox*>(widget);
            message && message->isVisible())
          message
            ->button(
              message->standardButtons().testFlag(QMessageBox::Save) ? QMessageBox::Save
                                                                     : QMessageBox::Ok)
            ->click();
    });
    timer.start(10);
    CHECK_FALSE(window.close());
    timer.stop();
    CHECK(window.isVisible());
    CHECK(window.activeMapWindow() == alphaWindow);
    CHECK(stack->count() == 2);
    CHECK(session->modified());
    CHECK(alphaDocument->map().modified());
    REQUIRE(QDir{}.rmdir(path));
    REQUIRE(QFile::rename(backup, path));
    require(session->save());
  }

  SECTION("all tabs can close while project save remains available")
  {
    closeTab(*tabs, 1);
    closeTab(*tabs, 0);
    CHECK(stack->count() == 0);
    CHECK(window.activeMapWindow() == nullptr);
    REQUIRE(mdl::setEntityProperty(alphaDocument->map(), "message", "closed edit"));
    QMenu* projectMenu = nullptr;
    for (auto* action : window.menuBar()->actions())
      if (action->text() == "Project")
        projectMenu = action->menu();
    REQUIRE(projectMenu != nullptr);
    auto* save = projectMenu->actions().at(1);
    CHECK(save->shortcut() == QKeySequence{QKeySequence::Save});
    save->trigger();
    CHECK_FALSE(session->modified());
    CHECK_FALSE(alphaDocument->map().modified());
  }

  SECTION("focus inside an embedded map activates that map")
  {
    CHECK(alphaWindow->window() == &window);
    auto child = QWidget{alphaWindow};
    QMetaObject::invokeMethod(
      qApp, "focusChanged", Q_ARG(QWidget*, nullptr), Q_ARG(QWidget*, &child));
    CHECK(manager.topMapWindow() == alphaWindow);
  }

  SECTION("closing a tab keeps the document and its modifications")
  {
    closeTab(*tabs, 0);
    // The view is gone from the workspace, but the session keeps the document alive.
    CHECK(tabs->count() == 1);
    CHECK(tabs->tabText(0) == "beta");
    CHECK(windowFor(appController, *alphaDocument) == nullptr);
    CHECK(session->documentFor(alphaId) == alphaDocument);
    CHECK(session->retainedDocument(alphaId) != nullptr);
    CHECK(session->assetFor(*alphaDocument) == std::optional{alphaId});

    // Editing the closed map still dirties the project, because the working copy lives in
    // the session, not in the view.
    REQUIRE(mdl::setEntityProperty(alphaDocument->map(), "message", "kept"));
    QApplication::processEvents();
    CHECK(session->modified());

    // Saving writes the closed map too.
    require(session->save());
    CHECK_FALSE(session->modified());
    CHECK_FALSE(alphaDocument->map().modified());
  }

  SECTION("a closed tab is reopened as the same document")
  {
    closeTab(*tabs, 0);
    auto* reopen = window.findChild<QToolButton*>("ProjectReopenMaps");
    REQUIRE(reopen != nullptr);
    REQUIRE(reopen->menu() != nullptr);
    REQUIRE(reopen->menu()->actions().size() == 1);
    CHECK(reopen->menu()->actions().front()->text() == "alpha");

    reopen->menu()->actions().front()->trigger();
    QApplication::processEvents();
    CHECK(tabs->count() == 2);
    // Reopening restores the same editing state in a new view instead of reloading.
    CHECK(session->documentFor(alphaId) == alphaDocument);
    auto* reopenedWindow = windowFor(appController, *alphaDocument);
    REQUIRE(reopenedWindow != nullptr);
    CHECK(reopenedWindow->isVisible());
  }

  SECTION("the tab strip follows the map modification state")
  {
    REQUIRE(mdl::setEntityProperty(alphaDocument->map(), "message", "edited"));
    QApplication::processEvents();
    const auto index =
      tabs->count() > 0 && tabs->tabData(0).toString().toStdString() == alphaId ? 0 : 1;
    CAPTURE(tabs->tabText(index).toStdString());
    CHECK(tabs->tabText(index).endsWith("*"));

    require(session->save());
    QApplication::processEvents();
    const auto savedIndex = tabs->tabData(0).toString().toStdString() == alphaId ? 0 : 1;
    CHECK_FALSE(tabs->tabText(savedIndex).endsWith("*"));
  }
}

} // namespace tb::ui
