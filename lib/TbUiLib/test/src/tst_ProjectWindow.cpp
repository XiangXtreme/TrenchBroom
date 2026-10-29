/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom.
*/

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QSaveFile>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QtTest/QTest>

#include "mdl/EntityDefinition.h"
#include "mdl/EntityDefinitionManager.h"
#include "mdl/GameManager.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "mdl/Map_Entities.h"
#include "mdl/PropertyDefinition.h"
#include "ui/AppControllerFixture.h"
#include "ui/CatalogAssetDrop.h"
#include "ui/CreateEntityTool.h"
#include "ui/InfoPanel.h"
#include "ui/MapDocument.h"
#include "ui/MapViewBase.h"
#include "ui/MapViewToolBox.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/ModelBrowserView.h"
#include "ui/ProjectSession.h"
#include "ui/ProjectWindow.h"
#include "ui/QPathUtils.h"

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
  CHECK(alphaWindow->menuBar()->isHidden());
  CHECK(alphaWindow->findAction("Menu/Edit/Undo")->shortcut().isEmpty());

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
    auto* browser = alphaWindow->findChild<ModelBrowserView*>();
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
    auto* source = alphaWindow->findChild<QComboBox*>("ModelBrowser_Source");
    REQUIRE(source != nullptr);
    source->setCurrentIndex(source->findData(int(BrowserSource::AssetLibrary)));
    QApplication::processEvents();
    QTest::qWait(100);
    auto* browser = alphaWindow->findChild<ModelBrowserView*>();
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
