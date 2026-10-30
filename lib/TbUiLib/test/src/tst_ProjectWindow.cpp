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
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QSaveFile>
#include <QScopeGuard>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest/QTest>

#include "img/GoldSrcTexture.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/EntityDefinition.h"
#include "mdl/EntityDefinitionManager.h"
#include "mdl/GameManager.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "mdl/Map_Brushes.h"
#include "mdl/Map_Entities.h"
#include "mdl/Map_Selection.h"
#include "mdl/PropertyDefinition.h"
#include "mdl/WorldNode.h"
#include "ui/Action.h"
#include "ui/ActionExecutionContext.h"
#include "ui/ActionManager.h"
#include "ui/AppControllerFixture.h"
#include "ui/CatalogAssetDrop.h"
#include "ui/CreateEntityTool.h"
#include "ui/InfoPanel.h"
#include "ui/MapDocument.h"
#include "ui/MapEditorView.h"
#include "ui/MapViewBase.h"
#include "ui/MapViewToolBox.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/ModelBrowser.h"
#include "ui/ModelBrowserView.h"
#include "ui/ProjectAssetPanel.h"
#include "ui/ProjectRecovery.h"
#include "ui/ProjectSession.h"
#include "ui/ProjectToolPanel.h"
#include "ui/ProjectWindow.h"
#include "ui/QPathUtils.h"

#include <algorithm>
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

  SECTION("one bottom surface hosts content and the active map tools")
  {
    auto* tools = window.findChild<ProjectToolPanel*>();
    auto* drawer = window.findChild<QWidget*>("ProjectToolDrawer");
    auto* pin = window.findChild<QToolButton*>("ProjectToolsPin");
    auto* close = window.findChild<QToolButton*>("ProjectToolsClose");
    auto* editor = alphaWindow->findChild<MapEditorView*>();
    REQUIRE(tools != nullptr);
    REQUIRE(drawer != nullptr);
    REQUIRE(pin != nullptr);
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
    pin->setChecked(false);
    close->click();
    REQUIRE_FALSE(drawer->isVisible());
    const auto workspaceHeight = stack->height();
    tools->showContent();
    QApplication::processEvents();
    CHECK(drawer->isVisible());
    CHECK(stack->height() == workspaceHeight);
    CHECK(window.findChild<ProjectAssetPanel*>()->isVisible());
    CHECK_FALSE(editor->infoPanel()->isVisible());
    CHECK(editor->isInfoPanelVisible());
    CHECK(alphaWindow->infoPanelVisible());
    CHECK(tools->isToolVisible(ProjectToolId::ContentBrowser));
    CHECK_FALSE(tools->isToolVisible(ProjectToolId::OutputLog));
    CHECK_FALSE(editor->findChild<QWidget*>("MapWindow_InfoPanelSurface")->isVisible());

    alphaWindow->switchToInfoPanelPage(InfoPanelPage::PythonConsole);
    CHECK(editor->infoPanel()->isVisible());
    CHECK(editor->infoPanel()->currentPage() == InfoPanelPage::PythonConsole);
    CHECK(tools->currentTool() == ProjectToolId::PythonConsole);
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
    pin->setChecked(true);
    QApplication::processEvents();
    CHECK(stack->height() < workspaceHeight);
    QMetaObject::invokeMethod(
      qApp,
      "focusChanged",
      Q_ARG(QWidget*, betaEditor->infoPanel()),
      Q_ARG(QWidget*, window.activeMapWindow()->currentMapViewBase()));
    CHECK(tools->isExpanded());
    pin->setChecked(false);
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
    auto* tools = window.findChild<ProjectToolPanel*>();
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
    CHECK(tools->isToolVisible(ProjectToolId::PythonConsole));
    CHECK(window.activeMapWindow()->findChild<MapEditorView*>() == betaEditor);
    closeTab(*tabs, 0);
    CHECK(betaPanel.isNull());
    CHECK(tools->isToolVisible(ProjectToolId::ContentBrowser));
    tools->showInfoPage(InfoPanelPage::Issues);
    CHECK(tools->currentTool() == ProjectToolId::ContentBrowser);
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

  SECTION("library disk changes prompt without scanning or dirtying the project")
  {
    const auto folder = directory.filePath("library");
    REQUIRE(QDir{}.mkdir(folder));
    auto library =
      require(project::AssetLibrary::create(pathFromQString(folder), "Library"));
    require(session->attachLibrary(pathFromQString(folder)));
    require(session->save());
    REQUIRE_FALSE(session->modified());
    auto source = QSaveFile{folder + "/new.wav"};
    REQUIRE(source.open(QIODevice::WriteOnly));
    REQUIRE(source.write("new source") == 10);
    REQUIRE(source.commit());
    auto elapsed = QElapsedTimer{};
    elapsed.start();
    while (window.statusBar()->currentMessage().isEmpty() && elapsed.elapsed() < 3000)
    {
      QApplication::processEvents();
      QThread::msleep(10);
    }
    CHECK(window.statusBar()->currentMessage().contains("library files changed"));
    CHECK_FALSE(session->modified());
    CHECK(require(library->assets()).empty());
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
