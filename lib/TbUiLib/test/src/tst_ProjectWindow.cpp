/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom.
*/

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
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
#include <QKeySequence>
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
#include "gl/GlManager.h"
#include "gl/PerspectiveCamera.h"
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
#include "mdl/Map_World.h"
#include "mdl/PropertyDefinition.h"
#include "mdl/WorldNode.h"
#include "ui/Action.h"
#include "ui/ActionExecutionContext.h"
#include "ui/ActionManager.h"
#include "ui/AppControllerFixture.h"
#include "ui/AssetInspection.h"
#include "ui/AssetInspector.h"
#include "ui/AssetLibraryItemDelegate.h"
#include "ui/AssetLibrarySource.h"
#include "ui/AssetLibraryWindow.h"
#include "ui/CatalogAssetDrop.h"
#include "ui/CreateEntityTool.h"
#include "ui/EntityBrowserView.h"
#include "ui/InfoPanel.h"
#include "ui/MapDocument.h"
#include "ui/MapEditorView.h"
#include "ui/MapSettingsDialog.h"
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
#include <catch2/generators/catch_generators.hpp>

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

void doubleClickTexture(ModelBrowserView& view, const std::filesystem::path& path = {})
{
  view.setScrollPosition(0);
  auto position = std::optional<QPoint>{};
  view.scrollToCell([&](const auto& cell) {
    const auto& data = cell.template itemAs<BrowserCellData>();
    if (data.type == BrowserCellType::Texture && (path.empty() || data.path == path))
    {
      const auto& bounds = cell.itemBounds();
      position = QPoint{
        int(bounds.left() + bounds.width / 2), int(bounds.top() + bounds.height / 2)};
    }
    return false;
  });
  REQUIRE(position);
  QTest::mouseDClick(&view, Qt::LeftButton, Qt::NoModifier, *position);
}
} // namespace

namespace
{
int sourceTabIndex(const QTabBar& tabs, const BrowserSourceFilter source)
{
  for (auto i = 0; i < tabs.count(); ++i)
    if (tabs.tabData(i).toInt() == int(source))
      return i;
  return -1;
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
  // The project window owns the icon that its dialogs and the taskbar entry show.
  CHECK_FALSE(window.windowIcon().isNull());

  SECTION("unreferenced filter scans all maps and removal rechecks live references")
  {
    auto input = project::AssetImport{};
    input.type = project::AssetType::Sound;
    input.virtualPath = "sound/cleanup.wav";
    input.displayName = "Cleanup sound";
    input.original = input.compatible = {1, 2, 3};
    const auto asset = require(session->importAsset(input));
    emit session->browserFocusRequested();
    auto* browser = window.findChild<ModelBrowser*>();
    REQUIRE(browser);
    auto* source = browser->findChild<QTabBar*>("ModelBrowser_Source");
    auto* unused = browser->findChild<QToolButton*>("ModelBrowser_Unreferenced");
    auto* used = browser->findChild<QToolButton*>("ModelBrowser_UsedInMap");
    auto* cards = browser->findChild<ModelBrowserView*>();
    REQUIRE(source);
    REQUIRE(unused);
    REQUIRE(used);
    REQUIRE(cards);
    source->setCurrentIndex(sourceTabIndex(*source, BrowserSourceFilter::Project));
    unused->setChecked(true);
    const auto listed = [&]() {
      return std::ranges::any_of(
        cards->assets(), [&](const auto& entry) { return entry.assetId == asset.id; });
    };
    QTRY_VERIFY(listed());
    CHECK(std::ranges::none_of(cards->assets(), [](const auto& entry) {
      return entry.type == BrowserCellType::Map;
    }));
    auto* betaDocument = session->documentFor(tabs->tabData(1).toString().toStdString());
    REQUIRE(betaDocument);
    REQUIRE(
      mdl::setEntityProperty(betaDocument->map(), "custom_sound", "sound/cleanup.wav"));
    QTRY_VERIFY(!listed()); // An inactive tab's unsaved reference counts too.
    CHECK(session->removeAsset(asset.id).is_error());
    REQUIRE(mdl::setEntityProperty(betaDocument->map(), "custom_sound", ""));
    QTRY_VERIFY(listed());
    used->setChecked(true);
    CHECK_FALSE(unused->isChecked());
    unused->setChecked(true);
    CHECK_FALSE(used->isChecked());
    source->setCurrentIndex(sourceTabIndex(*source, BrowserSourceFilter::AssetLibrary));
    CHECK_FALSE(unused->isVisible());
    source->setCurrentIndex(sourceTabIndex(*source, BrowserSourceFilter::Project));
    CHECK(unused->isChecked());
    QTRY_VERIFY(listed());
    auto response = QMessageBox::Cancel;
    auto timer = QTimer{};
    QObject::connect(&timer, &QTimer::timeout, [&]() {
      for (auto* widget : QApplication::topLevelWidgets())
        if (auto* message = qobject_cast<QMessageBox*>(widget);
            message && message->isVisible())
          if (auto* button = message->button(response))
            button->click();
    });
    timer.start(10);
    emit cards->projectAssetRemoveRequested(QString::fromStdString(asset.id));
    CHECK(require(session->store().findAsset(asset.id)).has_value());
    response = QMessageBox::Yes;
    emit cards->projectAssetRemoveRequested(QString::fromStdString(asset.id));
    timer.stop();
    CHECK_FALSE(require(session->store().findAsset(asset.id)).has_value());
    QTRY_VERIFY(!listed());
    require(session->save());
    auto reopened = require(project::ProjectStore::open(session->path()));
    CHECK_FALSE(require(reopened->findAsset(asset.id)).has_value());
  }

  SECTION("map previews are saved and remain available after closing the view")
  {
    auto& view = alphaWindow->activate3DMapView();
    REQUIRE(QTest::qWaitFor([&]() { return view.isValid(); }, 3000));
    mdl::selectAllNodes(alphaDocument->map());
    const auto cameraPosition = view.perspectiveCamera().position();
    const auto selected = alphaDocument->map().selection().nodes;
    require(session->save());
    const auto savedAsset = require(session->store().findAsset(alphaId));
    REQUIRE(savedAsset);
    const auto png = require(session->store().mapThumbnail(alphaId));
    REQUIRE_FALSE(png.empty());
    auto image = QImage{};
    REQUIRE(image.loadFromData(png.data(), int(png.size()), "PNG"));
    CHECK(image.width() <= 512);
    CHECK(image.height() <= 512);
    CHECK_FALSE(image.hasAlphaChannel());
    CHECK(view.perspectiveCamera().position() == cameraPosition);
    CHECK(alphaDocument->map().selection().nodes == selected);
    CHECK_FALSE(alphaDocument->map().modified());
    emit session->mapThumbnailRequested(QString::fromStdString(alphaId));
    CHECK(require(session->store().findAsset(alphaId))->revision == savedAsset->revision);
    require(session->save());
    auto reopened = require(project::ProjectStore::open(session->path()));
    CHECK_FALSE(require(reopened->mapThumbnail(alphaId)).empty());
    reopened.reset();
    const auto lastPreview = require(session->store().mapThumbnail(alphaId));
    closeTab(*tabs, 0);
    CHECK(require(session->store().mapThumbnail(alphaId)) == lastPreview);
    emit session->browserFocusRequested();
    auto* browser = window.findChild<ModelBrowser*>();
    REQUIRE(browser);
    auto* source = browser->findChild<QTabBar*>("ModelBrowser_Source");
    source->setCurrentIndex(sourceTabIndex(*source, BrowserSourceFilter::Project));
    auto* cards = browser->findChild<ModelBrowserView*>();
    REQUIRE(cards);
    QTRY_VERIFY(std::ranges::any_of(cards->assets(), [&](const auto& entry) {
      return entry.assetId == alphaId
             && entry.previewIdentity.find("/thumbnail/") != std::string::npos;
    }));
    emit cards->folderActivated("maps");
    auto preview = QImage{128, 96, QImage::Format_RGB32};
    const auto hasPreviewPixels = [&](const QColor& expected) {
      const auto rendered = cards->grabFramebuffer();
      auto count = 0;
      for (auto y = 0; y < rendered.height(); y += 2)
        for (auto x = 0; x < rendered.width(); x += 2)
          if (rendered.pixelColor(x, y) == expected)
            ++count;
      return count > 100;
    };
    preview.fill(Qt::red);
    require(session->setMapThumbnail(alphaId, preview));
    REQUIRE(QTest::qWaitFor([&]() { return hasPreviewPixels(Qt::red); }, 3000));
    preview.fill(Qt::blue);
    require(session->setMapThumbnail(alphaId, preview));
    REQUIRE(QTest::qWaitFor([&]() { return hasPreviewPixels(Qt::blue); }, 3000));
    CHECK_FALSE(hasPreviewPixels(Qt::red));
  }

  SECTION("entity content follows the active map and cannot edit a closed target")
  {
    auto* browser = window.findChild<ModelBrowser*>();
    REQUIRE(browser);
    alphaDocument->map().entityDefinitionManager().setDefinitions({
      {"alpha_point",
       {},
       "Alpha",
       {},
       mdl::PointEntityDefinition{vm::bbox3d{8.0}, {}, {}}},
    });
    const auto betaId = tabs->tabData(1).toString().toStdString();
    auto* beta = session->documentFor(betaId);
    REQUIRE(beta);
    beta->map().entityDefinitionManager().setDefinitions({
      {"beta_point", {}, "Beta", {}, mdl::PointEntityDefinition{vm::bbox3d{8.0}, {}, {}}},
    });
    auto* assetSearch = browser->findChild<QLineEdit*>("ModelBrowser_Search");
    REQUIRE(assetSearch);
    assetSearch->setText("original asset search");
    auto* detailsToggle = window.findChild<QToolButton*>("ProjectAssetDetailsToggle");
    REQUIRE(detailsToggle);
    detailsToggle->setChecked(true);
    alphaWindow->editorView().showEntityBrowser();
    QApplication::processEvents();
    CHECK(browser->isShowingEntities());
    CHECK(window.findChild<QWidget*>("ProjectAssetDetailsSurface")->isHidden());
    auto* folders = browser->findChild<QTreeWidget*>("ModelBrowser_FolderTree");
    REQUIRE(folders);
    folders->setCurrentItem(folders->topLevelItem(0));
    CHECK_FALSE(browser->isShowingEntities());
    CHECK(assetSearch->text() == "original asset search");
    CHECK_FALSE(window.findChild<QWidget*>("ProjectAssetDetailsSurface")->isHidden());
    browser->showEntityBrowser();
    auto* grid = browser->findChild<EntityBrowserView*>("EntityBrowser_Grid");
    REQUIRE(grid);
    REQUIRE(grid->filteredDefinitions().size() == 1);
    CHECK(grid->filteredDefinitions().front()->name == "alpha_point");
    const auto alphaSelection = alphaDocument->map().selection().nodes;
    tabs->setCurrentIndex(1);
    QApplication::processEvents();
    grid = browser->findChild<EntityBrowserView*>("EntityBrowser_Grid");
    REQUIRE(grid);
    REQUIRE(grid->filteredDefinitions().size() == 1);
    CHECK(grid->filteredDefinitions().front()->name == "beta_point");
    REQUIRE(grid->selectDefinition("beta_point"));
    auto* create = browser->findChild<QPushButton*>("EntityBrowser_Create");
    REQUIRE(create);
    REQUIRE(create->isEnabled());
    create->click();
    CHECK(alphaDocument->map().selection().nodes == alphaSelection);
    REQUIRE(beta->map().selection().nodes.size() == 1);
    CHECK(
      dynamic_cast<mdl::EntityNode*>(beta->map().selection().nodes.front())
        ->entity()
        .classname()
      == "beta_point");
    closeTab(*tabs, 1);
    closeTab(*tabs, 0);
    CHECK(window.activeMapWindow() == nullptr);
    create = browser->findChild<QPushButton*>("EntityBrowser_Create");
    REQUIRE(create);
    CHECK_FALSE(create->isEnabled());
  }

  SECTION("one project browser survives map switches and closing all map tabs")
  {
    emit session->browserFocusRequested();
    auto* browser = window.findChild<ModelBrowser*>();
    REQUIRE(browser != nullptr);
    auto* source = browser->findChild<QTabBar*>("ModelBrowser_Source");
    REQUIRE(source);
    source->setCurrentIndex(sourceTabIndex(*source, BrowserSourceFilter::Project));
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

  SECTION("map settings close when their project map changes or closes")
  {
    alphaWindow->editorView().showMapSettings();
    auto alphaSettings =
      QPointer<MapSettingsDialog>{alphaWindow->findChild<MapSettingsDialog*>()};
    REQUIRE(alphaSettings);
    CHECK(alphaSettings->isVisible());
    tabs->setCurrentIndex(1);
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    CHECK(alphaSettings.isNull());
    auto* betaWindow = window.activeMapWindow();
    REQUIRE(betaWindow);
    CHECK(betaWindow != alphaWindow);
    betaWindow->editorView().showMapSettings();
    auto betaSettings =
      QPointer<MapSettingsDialog>{betaWindow->findChild<MapSettingsDialog*>()};
    REQUIRE(betaSettings);
    CHECK(betaSettings->isVisible());
    closeTab(*tabs, 1);
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    CHECK(betaSettings.isNull());
    CHECK(window.activeMapWindow() == alphaWindow);
    CHECK(alphaWindow->findChild<MapSettingsDialog*>() == nullptr);
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

    // The outliner page is built when its tab is first selected.
    editor.inspector()->switchToPage(InspectorPage::Outliner);
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
    CHECK(editor.inspector()->currentPage() == InspectorPage::Outliner);
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
    window.resize(1600, 900);
    QApplication::processEvents();
    auto settings = QSettings{};
    auto saved = EditorLayout{};
    saved.inspectorRatio = 0.42;
    SECTION("wide saved sidebar retains its proportion") {}
    SECTION("narrow saved sidebar respects the content minimum")
    {
      saved.inspectorRatio = 0.1;
    }
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
    const auto total = sizes[0] + sizes[1];
    const auto minimum = editor.hSplitter()->widget(1)->minimumSizeHint().width();
    const auto expected = std::max(saved.inspectorRatio * total, double(minimum));
    CHECK(std::abs(sizes[1] - expected) < 2.0);
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


  SECTION(
    "content browser states the search scope and keeps filters apart from view settings")
  {
    emit session->browserFocusRequested();
    auto* browser = window.findChild<ModelBrowser*>();
    REQUIRE(browser);
    auto* search = browser->findChild<QLineEdit*>("ModelBrowser_Search");
    auto* scope = browser->findChild<QComboBox*>("ModelBrowser_SearchScope");
    auto* filters = browser->findChild<QToolButton*>("ModelBrowser_FiltersButton");
    auto* viewSettings = browser->findChild<QToolButton*>("ModelBrowser_ViewButton");
    auto* filterPopup = browser->findChild<QWidget*>("ModelBrowser_FilterPopup");
    auto* viewPopup = browser->findChild<QWidget*>("ModelBrowser_ViewPopup");
    auto* count = browser->findChild<QLabel*>("ModelBrowser_ResultCount");
    auto* back = browser->findChild<QToolButton*>("ModelBrowser_Back");
    auto* forward = browser->findChild<QToolButton*>("ModelBrowser_Forward");
    REQUIRE(search);
    REQUIRE(scope);
    REQUIRE(filters);
    REQUIRE(viewSettings);
    REQUIRE(filterPopup);
    REQUIRE(viewPopup);
    REQUIRE(count);
    REQUIRE(back);
    REQUIRE(forward);

    // The scope is readable before anything is typed, and switching it restates the
    // placeholder instead of leaving the old promise in place.
    CHECK_FALSE(scope->currentData().toBool());
    CHECK(
      search->placeholderText().contains(QStringLiteral("folder"), Qt::CaseInsensitive));
    scope->setCurrentIndex(scope->findData(true));
    CHECK(search->placeholderText().contains(QStringLiteral("all"), Qt::CaseInsensitive));

    // Filters and view settings are different jobs, so they open as separate popovers
    // and never overlap.
    CHECK(filterPopup->isHidden());
    CHECK(viewPopup->isHidden());
    filters->click();
    CHECK_FALSE(filterPopup->isHidden());
    filters->click();
    CHECK(filterPopup->isHidden());
    viewSettings->click();
    CHECK_FALSE(viewPopup->isHidden());
    viewSettings->click();
    CHECK(viewPopup->isHidden());

    // A listing's size is stated, and history starts empty because the breadcrumb and the
    // folder tree own navigation now.
    CHECK_FALSE(count->text().isEmpty());
    CHECK_FALSE(back->isEnabled());
    CHECK_FALSE(forward->isEnabled());
  }

  SECTION(
    "project content browser excludes game files while standalone maps include them")
  {
    const auto gameRoot = directory.filePath("game");
    REQUIRE(QDir{}.mkpath(gameRoot + "/valve/sound"));
    const auto writeWave = [](const QString& path) {
      const auto bytes = wavBytes(false);
      auto file = QFile{path};
      REQUIRE(file.open(QIODevice::WriteOnly));
      REQUIRE(
        file.write(reinterpret_cast<const char*>(bytes.data()), qint64(bytes.size()))
        == qint64(bytes.size()));
    };
    writeWave(gameRoot + "/valve/sound/game-only.wav");

    const auto libraryRoot = directory.filePath("browser-library");
    REQUIRE(QDir{}.mkpath(libraryRoot + "/sound"));
    writeWave(libraryRoot + "/sound/library-only.wav");
    auto library =
      require(project::AssetLibrary::create(pathFromQString(libraryRoot), "Browser"));
    require(library->importDirectory(library->folder()));
    require(session->attachLibrary(pathFromQString(libraryRoot)));

    auto input = project::AssetImport{};
    input.type = project::AssetType::Sound;
    input.virtualPath = "sound/project-only.wav";
    input.displayName = "project-only.wav";
    input.original = input.compatible = wavBytes(false);
    require(session->importAsset(input));
    alphaDocument->map().setGamePath(pathFromQString(gameRoot));
    mdl::setEnabledMods(alphaDocument->map(), {"valve"});

    emit session->browserFocusRequested();
    auto* browser = window.findChild<ModelBrowser*>();
    REQUIRE(browser);
    auto* source = browser->findChild<QTabBar*>("ModelBrowser_Source");
    auto* type = browser->findChild<QComboBox*>("ModelBrowser_Type");
    auto* view = browser->findChild<ModelBrowserView*>();
    auto* savePrefab = browser->findChild<QToolButton*>("ModelBrowser_SavePrefabButton");
    REQUIRE(source);
    REQUIRE(type);
    REQUIRE(view);
    REQUIRE(savePrefab);
    CHECK(sourceTabIndex(*source, BrowserSourceFilter::GameFiles) == -1);
    CHECK(source->count() == 2);
    CHECK(
      source->tabData(source->currentIndex()).toInt()
      == int(BrowserSourceFilter::AssetLibrary));
    CHECK(savePrefab->isHidden());
    auto* filters = browser->findChild<QWidget*>("ModelBrowser_FilterPopup");
    REQUIRE(filters);
    CHECK_FALSE(filters->isAncestorOf(source));
    type->setCurrentIndex(type->findData(int(BrowserCellType::Sound)));
    const auto hasAsset = [](const auto& assets, const auto& name, const auto expected) {
      return std::ranges::any_of(assets, [&](const auto& asset) {
        return asset.path.filename() == name && asset.source == expected;
      });
    };
    QTRY_VERIFY(
      !hasAsset(view->assets(), "project-only.wav", BrowserSource::Project)
      && hasAsset(view->assets(), "library-only.wav", BrowserSource::AssetLibrary));
    CHECK_FALSE(hasAsset(view->assets(), "game-only.wav", BrowserSource::GameFiles));

    QTest::mouseClick(
      source,
      Qt::LeftButton,
      Qt::NoModifier,
      source->tabRect(sourceTabIndex(*source, BrowserSourceFilter::Project)).center());
    QTRY_VERIFY(
      hasAsset(view->assets(), "project-only.wav", BrowserSource::Project)
      && !hasAsset(view->assets(), "library-only.wav", BrowserSource::AssetLibrary));
    CHECK_FALSE(hasAsset(view->assets(), "game-only.wav", BrowserSource::GameFiles));
    CHECK_FALSE(savePrefab->isHidden());
    QTest::mouseClick(
      source,
      Qt::LeftButton,
      Qt::NoModifier,
      source->tabRect(sourceTabIndex(*source, BrowserSourceFilter::AssetLibrary))
        .center());
    QTRY_VERIFY(
      hasAsset(view->assets(), "library-only.wav", BrowserSource::AssetLibrary));
    CHECK_FALSE(hasAsset(view->assets(), "game-only.wav", BrowserSource::GameFiles));
    CHECK(savePrefab->isHidden());
    const auto resource = require(library->assets()).front();
    require(library->setGroup({resource.id}, "sound/Custom"));
    const auto captured =
      require(session->captureLibraryAsset(library->info().id, resource.id));
    QTRY_VERIFY(std::ranges::any_of(view->assets(), [&](const auto& entry) {
      return entry.source == BrowserSource::AssetLibrary
             && entry.projectAssetId == captured.id
             && entry.catalogPath
                  == std::filesystem::path{"sound/Custom/library-only.wav"};
    }));
    auto* used = browser->findChild<QToolButton*>("ModelBrowser_UsedInMap");
    auto* folders = browser->findChild<QTreeWidget*>("ModelBrowser_FolderTree");
    REQUIRE(used);
    REQUIRE(folders);
    CHECK(used->isHidden());
    source->setCurrentIndex(sourceTabIndex(*source, BrowserSourceFilter::Project));
    QTRY_VERIFY(hasAsset(view->assets(), "project-only.wav", BrowserSource::Project));
    CHECK_FALSE(used->isHidden());
    CHECK(
      folders->topLevelItem(0)->text(0)
      == QString::fromStdString(session->store().info().name));
    CHECK(std::ranges::all_of(view->assets(), [](const auto& entry) {
      return entry.source == BrowserSource::Project;
    }));
    CHECK(std::ranges::any_of(
      view->assets(), [&](const auto& entry) { return entry.assetId == captured.id; }));
    used->setChecked(true);
    QTRY_VERIFY(view->assets().empty());
    source->setCurrentIndex(sourceTabIndex(*source, BrowserSourceFilter::AssetLibrary));
    QTRY_VERIFY(
      hasAsset(view->assets(), "library-only.wav", BrowserSource::AssetLibrary));
    CHECK_FALSE(used->isChecked());
    source->setCurrentIndex(sourceTabIndex(*source, BrowserSourceFilter::Project));
    QTRY_VERIFY(view->assets().empty());
    CHECK(used->isChecked());
    used->setChecked(false);

    auto standaloneDocument = require(MapDocument::createDocument(
      appController.environmentConfig(),
      alphaDocument->map().gameInfo(),
      mdl::MapFormat::Valve,
      MapDocument::DefaultWorldBounds,
      appController.taskManager(),
      appController.glManager().resourceManager()));
    standaloneDocument->map().setGamePath(pathFromQString(gameRoot));
    mdl::setEnabledMods(standaloneDocument->map(), {"valve"});
    auto standaloneBrowser = ModelBrowser{appController, *standaloneDocument};
    standaloneBrowser.resize(800, 400);
    standaloneBrowser.show();
    auto* standaloneSource = standaloneBrowser.findChild<QTabBar*>("ModelBrowser_Source");
    auto* standaloneView = standaloneBrowser.findChild<ModelBrowserView*>();
    REQUIRE(standaloneSource);
    REQUIRE(standaloneView);
    CHECK(
      standaloneSource->tabData(standaloneSource->currentIndex()).toInt()
      == int(BrowserSourceFilter::GameFiles));
    CHECK(sourceTabIndex(*standaloneSource, BrowserSourceFilter::Project) == -1);
    QTRY_VERIFY(
      hasAsset(standaloneView->assets(), "game-only.wav", BrowserSource::GameFiles));
    CHECK_FALSE(
      hasAsset(standaloneView->assets(), "project-only.wav", BrowserSource::Project));

    alphaDocument->map().undoCommand();
    require(session->save());
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
    auto* source = browser->findChild<QTabBar*>("ModelBrowser_Source");
    auto* type = browser->findChild<QComboBox*>("ModelBrowser_Type");
    auto* scroll = browser->findChild<QScrollBar*>("ModelBrowser_ScrollBar");
    auto* view = browser->findChild<ModelBrowserView*>();
    auto* folders = browser->findChild<QTreeWidget*>("ModelBrowser_FolderTree");
    REQUIRE(source);
    REQUIRE(type);
    REQUIRE(scroll);
    REQUIRE(view);
    REQUIRE(folders);
    source->setCurrentIndex(sourceTabIndex(*source, BrowserSourceFilter::Project));
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
    source->setCurrentIndex(sourceTabIndex(*source, BrowserSourceFilter::AssetLibrary));
    QApplication::processEvents();
    source->setCurrentIndex(sourceTabIndex(*source, BrowserSourceFilter::Project));
    QTRY_COMPARE(scroll->value(), 100);
    auto* search = browser->findChild<QLineEdit*>("ModelBrowser_Search");
    REQUIRE(search);
    search->setText("Clip");
    source->setCurrentIndex(sourceTabIndex(*source, BrowserSourceFilter::AssetLibrary));
    CHECK(search->text().isEmpty());
    search->setText("Library query");
    source->setCurrentIndex(sourceTabIndex(*source, BrowserSourceFilter::Project));
    CHECK(search->text() == "Clip");
    search->clear();
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
    auto* source = browser->findChild<QTabBar*>("ModelBrowser_Source");
    auto* type = browser->findChild<QComboBox*>("ModelBrowser_Type");
    auto* sounds = browser->findChild<QComboBox*>("ModelBrowser_SoundKind");
    auto* view = browser->findChild<ModelBrowserView*>();
    REQUIRE(source);
    REQUIRE(type);
    REQUIRE(sounds);
    REQUIRE(view);
    source->setCurrentIndex(sourceTabIndex(*source, BrowserSourceFilter::Project));
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
    auto* source = browser->findChild<QTabBar*>("ModelBrowser_Source");
    auto* used = browser->findChild<QToolButton*>("ModelBrowser_UsedInMap");
    auto* folders = browser->findChild<QTreeWidget*>("ModelBrowser_FolderTree");
    REQUIRE(source);
    REQUIRE(used);
    REQUIRE(folders);
    source->setCurrentIndex(sourceTabIndex(*source, BrowserSourceFilter::Project));
    used->setChecked(true);
    QApplication::processEvents();
    CHECK(folders->findItems("Skybox", Qt::MatchRecursive).empty());
    REQUIRE(mdl::setEntityProperty(alphaDocument->map(), "skyname", "live"));
    QTRY_COMPARE(folders->findItems("Skybox", Qt::MatchRecursive).size(), 1);
    tabs->setCurrentIndex(1);
    QTRY_VERIFY(folders->findItems("Skybox", Qt::MatchRecursive).empty());
    tabs->setCurrentIndex(0);
    QTRY_COMPARE(folders->findItems("Skybox", Qt::MatchRecursive).size(), 1);
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
    require(library->importDirectory(library->folder()));
    require(session->attachLibrary(pathFromQString(folder)));
    const auto resource = require(library->assets()).front();
    auto libraryWindow = AssetLibraryWindow{folder, false, &window};
    libraryWindow.setProjectContext(session);
    libraryWindow.findChild<QPushButton*>("AssetInspector_Capture")->click();
    REQUIRE(
      require(session->projectAssetForLibraryAsset(library->info().id, resource.id)));
    writeSound(false);
    auto source = openLocalAssetLibrary(pathFromQString(folder));
    require(source->replaceFile(pathFromQString(path), "clip.wav"));
    auto* browser = window.findChild<ModelBrowser*>();
    REQUIRE(browser);
    auto* sourceChoice = browser->findChild<QTabBar*>("ModelBrowser_Source");
    auto* kind = browser->findChild<QComboBox*>("ModelBrowser_SoundKind");
    auto* view = browser->findChild<ModelBrowserView*>();
    REQUIRE(sourceChoice);
    REQUIRE(kind);
    REQUIRE(view);
    sourceChoice->setCurrentIndex(
      sourceTabIndex(*sourceChoice, BrowserSourceFilter::AssetLibrary));
    kind->setCurrentIndex(kind->findData(int(inspection::WaveKind::OneShot)));
    QTRY_COMPARE(view->assets().size(), 1u);
    sourceChoice->setCurrentIndex(
      sourceTabIndex(*sourceChoice, BrowserSourceFilter::Project));
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
    require(library->importDirectory(library->folder()));
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

  SECTION("library category renaming changes only catalog organization")
  {
    const auto folder = directory.filePath("category-library");
    REQUIRE(QDir{}.mkpath(folder + "/textures/nature"));
    auto image = QImage{32, 32, QImage::Format_RGB32};
    image.fill(Qt::green);
    REQUIRE(image.save(folder + "/textures/nature/grass.png"));
    auto library =
      require(project::AssetLibrary::create(pathFromQString(folder), "Library"));
    require(library->importDirectory(library->folder()));
    const auto asset = require(library->assets()).front();
    auto browser = AssetLibraryWindow{folder, false, &window};
    auto* folders = browser.findChild<QTreeWidget*>("AssetLibraryFolders");
    REQUIRE(folders);
    auto* category = static_cast<QTreeWidgetItem*>(nullptr);
    for (auto it = QTreeWidgetItemIterator{folders}; *it; ++it)
      if ((*it)->data(0, Qt::UserRole).toString() == "textures/nature")
        category = *it;
    REQUIRE(category);
    REQUIRE(category->flags().testFlag(Qt::ItemIsEditable));
    category->setText(0, "Landscape");
    QTRY_COMPARE(require(library->findAsset(asset.id))->group, "textures/Landscape");
    const auto renamed = require(library->findAsset(asset.id));
    CHECK(renamed->virtualPath == asset.virtualPath);
    CHECK(renamed->revision == asset.revision);
    REQUIRE(QFile::remove(folder + "/textures/nature/grass.png"));
    CHECK_FALSE(require(library->readContent(asset.id)).empty());
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
    require(library->importDirectory(library->folder()));
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
    require(source->replaceFile(pathFromQString(path), "color.png"));
    QTRY_COMPARE(thumbnail().pixelColor(0, 0), QColor{Qt::blue});
  }

  SECTION("deleting a library asset survives reopening the library")
  {
    const auto folder = directory.filePath("delete-library");
    REQUIRE(QDir{}.mkdir(folder));
    const auto path = folder + "/color.png";
    auto image = QImage{32, 32, QImage::Format_RGB32};
    image.fill(Qt::red);
    REQUIRE(image.save(path));
    auto library =
      require(project::AssetLibrary::create(pathFromQString(folder), "Library"));
    require(library->importDirectory(library->folder()));

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

    // Opening the catalog cannot restore assets from the original files.
    auto source = openLocalAssetLibrary(pathFromQString(folder));
    REQUIRE(source);
    CHECK(require(source->assets()).empty());
    CHECK(QFile::exists(path));
  }

  SECTION("bulk library deletion respects selection and preserves project copies")
  {
    const auto folder = directory.filePath("bulk-delete-library");
    REQUIRE(QDir{}.mkdir(folder));
    const auto names = std::array{"match-one.wav", "match-two.wav", "other.wav"};
    for (const auto* name : names)
    {
      auto file = QFile{folder + "/" + name};
      REQUIRE(file.open(QIODevice::WriteOnly));
      const auto bytes = wavBytes(false);
      REQUIRE(
        file.write(reinterpret_cast<const char*>(bytes.data()), qint64(bytes.size()))
        == qint64(bytes.size()));
    }
    auto library =
      require(project::AssetLibrary::create(pathFromQString(folder), "Library"));
    require(library->importDirectory(library->folder()));
    const auto libraryId = library->info().id;
    require(session->attachLibrary(pathFromQString(folder)));

    auto browser = AssetLibraryWindow{folder, false, &window};
    browser.setProjectContext(session);
    browser.show();
    auto* grid = browser.findChild<QListWidget*>("AssetLibraryGrid");
    auto* search = browser.findChild<QLineEdit*>("AssetLibrarySearch");
    auto* remove = browser.findChild<QPushButton*>("AssetLibraryDeleteButton");
    auto* removeAction = browser.findChild<QAction*>("AssetLibrary_DeleteSelected");
    auto* capture = browser.findChild<QPushButton*>("AssetInspector_Capture");
    REQUIRE(grid);
    REQUIRE(search);
    REQUIRE(remove);
    REQUIRE(removeAction);
    REQUIRE(capture);
    CHECK(grid->actions().contains(removeAction));
    CHECK(removeAction->shortcut() == QKeySequence{QKeySequence::Delete});

    search->setText("match");
    QTRY_COMPARE(grid->count(), 2);
    grid->setCurrentRow(0);
    const auto capturedId = grid->currentItem()
                              ->data(AssetLibraryItemDelegate::AssetIdRole)
                              .toString()
                              .toStdString();
    capture->click();
    const auto snapshot =
      require(session->projectAssetForLibraryAsset(libraryId, capturedId));
    REQUIRE(snapshot);
    QApplication::processEvents();

    auto changes = 0;
    QObject::connect(
      &AssetLibraryChanges::instance(),
      &AssetLibraryChanges::changed,
      &browser,
      [&](const QString& id) {
        if (id.toStdString() == libraryId)
          ++changes;
      });
    auto response = QMessageBox::Cancel;
    auto prompt = QString{};
    auto timer = QTimer{};
    QObject::connect(&timer, &QTimer::timeout, [&] {
      for (auto* widget : QApplication::topLevelWidgets())
        if (auto* message = dynamic_cast<QMessageBox*>(widget);
            message && message->isVisible())
          if (auto* button = message->button(response))
          {
            prompt = message->text();
            button->click();
          }
    });

    grid->clearSelection();
    CHECK_FALSE(remove->isEnabled());
    CHECK_FALSE(removeAction->isEnabled());
    grid->setFocus();
    if (qEnvironmentVariableIntValue("TB_TEST_SHOW_WINDOWS") == 1)
      QTest::keyClick(grid, Qt::Key_A, Qt::ControlModifier);
    else
      grid->selectAll();
    REQUIRE(grid->selectedItems().size() == 2);
    REQUIRE(removeAction->isEnabled());
    timer.start(10);
    removeAction->trigger();
    timer.stop();
    CHECK(prompt.contains("2"));
    CHECK(grid->count() == 2);
    CHECK(changes == 0);

    response = QMessageBox::Yes;
    timer.start(10);
    remove->click();
    timer.stop();
    CHECK(changes == 1);
    QTRY_COMPARE(grid->count(), 0);
    CHECK(require(session->store().findAsset(snapshot->id)).has_value());
    CHECK(QFile::exists(folder + "/match-one.wav"));
    CHECK(QFile::exists(folder + "/match-two.wav"));

    search->clear();
    QTRY_COMPARE(grid->count(), 1);
    CHECK(grid->item(0)->text().contains("other"));
    grid->setCurrentRow(0);
    grid->setFocus();
    timer.start(10);
    if (qEnvironmentVariableIntValue("TB_TEST_SHOW_WINDOWS") == 1)
      QTest::keyClick(grid, Qt::Key_Delete);
    else
      removeAction->trigger();
    timer.stop();
    CHECK(changes == 2);
    QTRY_COMPARE(grid->count(), 0);
    auto source = openLocalAssetLibrary(pathFromQString(folder));
    REQUIRE(source);
    CHECK(require(source->assets()).empty());
    for (const auto* name : names)
      CHECK(QFile::exists(folder + "/" + name));
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
    require(library->importDirectory(library->folder()));
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
    QTRY_COMPARE(folders->findItems("Sound", Qt::MatchRecursive).size(), 1);
    auto* favorite = linkedWindow->findChild<QPushButton*>("AssetLibraryFavoriteButton");
    REQUIRE(favorite);
    CHECK(favorite->isChecked());
    favorite->click();
    QTRY_VERIFY(folders->findItems("Sound", Qt::MatchRecursive).empty());
    QTRY_VERIFY(
      !libraryWindow.findChild<QPushButton*>("AssetLibraryFavoriteButton")->isChecked());
    require(source->setFavorite(resource.id, true));
    QTRY_COMPARE(folders->findItems("Sound", Qt::MatchRecursive).size(), 1);
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
    require(
      source->replaceFile(pathFromQString(folder + "/sound/clip.wav"), "sound/clip.wav"));
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
    require(library->importDirectory(library->folder()));
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

  SECTION("library model previews resolve only visible entries and invalidate revisions")
  {
    const auto modelFolder = directory.filePath("lazy-preview-models");
    REQUIRE(QDir{}.mkpath(modelFolder));
    const auto file = modelFolder + "/1000.obj";
    auto output = QSaveFile{file};
    REQUIRE(output.open(QIODevice::WriteOnly));
    REQUIRE(output.write("v 0 0 0\nv 8 0 0\nv 0 8 0\nf 1 2 3\n") > 0);
    REQUIRE(output.commit());
    alphaWindow->switchToInfoPanelPage(InfoPanelPage::Assets);
    QApplication::processEvents();
    auto* browser = window.findChild<ModelBrowserView*>();
    REQUIRE(browser);
    browser->setIncludeDefaultRoots(false);
    auto assets = std::vector<BrowserAsset>{};
    for (auto i = 0; i < 120; ++i)
    {
      if (i != 0)
        REQUIRE(
          QFile::copy(file, modelFolder + "/" + QString::number(1000 + i) + ".obj"));
      auto asset = BrowserAsset{};
      asset.type = BrowserCellType::Model;
      asset.path = std::to_string(1000 + i) + ".obj";
      asset.displayName = std::to_string(1000 + i);
      asset.source = BrowserSource::AssetLibrary;
      asset.libraryId = "library";
      asset.libraryAssetId = asset.displayName;
      asset.libraryRevision = 1;
      assets.push_back(std::move(asset));
    }
    auto resolved = std::vector<std::filesystem::path>{};
    browser->setPreviewPathResolver([&](const auto& entries) {
      auto files = std::map<std::filesystem::path, std::filesystem::path>{};
      for (const auto& entry : entries)
      {
        resolved.push_back(entry.path);
        files.emplace(entry.path, pathFromQString(modelFolder) / entry.path);
      }
      return Result<decltype(files)>{std::move(files)};
    });
    browser->setAssets({}, assets);
    REQUIRE(QTest::qWaitFor([&]() { return browser->isVisible(); }, 3000));
    REQUIRE_FALSE(browser->grabFramebuffer().isNull());
    REQUIRE_FALSE(resolved.empty());
    CHECK(resolved.size() < assets.size());
    const auto visible = resolved.front();
    const auto initialCount = resolved.size();
    browser->setAssets({}, assets);
    REQUIRE_FALSE(browser->grabFramebuffer().isNull());
    CHECK(resolved.size() == initialCount);
    const auto changed = std::ranges::find(assets, visible, &BrowserAsset::path);
    REQUIRE(changed != assets.end());
    ++changed->libraryRevision;
    browser->setAssets({}, assets);
    REQUIRE_FALSE(browser->grabFramebuffer().isNull());
    CHECK(resolved.size() == initialCount + 1);
    CHECK(resolved.back() == visible);
    auto* scroll =
      browser->parentWidget()->findChild<QScrollBar*>("ModelBrowser_ScrollBar");
    REQUIRE(scroll);
    REQUIRE(scroll->maximum() > 0);
    scroll->setValue(scroll->maximum());
    REQUIRE_FALSE(browser->grabFramebuffer().isNull());
    CHECK(resolved.size() > initialCount + 1);
    const auto afterScroll = resolved.size();
    scroll->setValue(0);
    REQUIRE_FALSE(browser->grabFramebuffer().isNull());
    CHECK(resolved.size() == afterScroll);
    browser->setPreviewPathResolver({});
    browser->setAssets({}, {});
  }

  SECTION("library browsing keeps Unicode folders and does not eagerly resolve models")
  {
    const auto folder = directory.filePath("unicode-model-library");
    const auto nature = QString::fromUtf8("自然与地形");
    const auto trees = QString::fromUtf8("树木");
    REQUIRE(QDir{}.mkpath(folder + "/" + nature + "/" + trees));
    for (auto i = 0; i < 3; ++i)
    {
      auto file = QFile{
        folder + "/" + nature + "/" + trees + "/tree" + QString::number(i) + ".mdl"};
      REQUIRE(file.open(QIODevice::WriteOnly));
      REQUIRE(file.write("IDST", 4) == 4);
    }
    auto library =
      require(project::AssetLibrary::create(pathFromQString(folder), "Models"));
    require(library->importDirectory(library->folder()));
    require(session->attachLibrary(pathFromQString(folder)));
    emit session->browserFocusRequested();
    auto* browser = window.findChild<ModelBrowser*>();
    REQUIRE(browser);
    auto* source = browser->findChild<QTabBar*>("ModelBrowser_Source");
    auto* folders = browser->findChild<QTreeWidget*>("ModelBrowser_FolderTree");
    auto* cards = browser->findChild<ModelBrowserView*>();
    REQUIRE(source);
    REQUIRE(folders);
    REQUIRE(cards);
    source->setCurrentIndex(sourceTabIndex(*source, BrowserSourceFilter::AssetLibrary));
    QTRY_COMPARE(cards->assets().size(), 3);
    for (const auto& asset : cards->assets())
      CHECK(asset.absolutePath.empty());
    const auto parents = folders->findItems(nature, Qt::MatchRecursive);
    const auto children = folders->findItems(trees, Qt::MatchRecursive);
    REQUIRE(parents.size() == 1);
    REQUIRE(children.size() == 1);
    REQUIRE(parents.front()->parent());
    CHECK(parents.front()->parent()->text(0) == "Model");
    CHECK(
      parents.front()->parent()->data(0, Qt::UserRole).toString()
      == QStringLiteral("models"));
    for (const auto& asset : cards->assets())
      CHECK(asset.catalogPath == std::filesystem::path{"models"} / asset.path);
    folders->setCurrentItem(children.front());
    const auto buttons = browser->findChildren<QToolButton*>();
    CHECK(std::ranges::any_of(
      buttons, [&](const auto* button) { return button->text() == trees; }));
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
    require(library->importDirectory(library->folder()));
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
    auto* source = window.findChild<QTabBar*>("ModelBrowser_Source");
    REQUIRE(source != nullptr);
    source->setCurrentIndex(sourceTabIndex(*source, BrowserSourceFilter::Project));

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

  SECTION("terrain materials appear and preview in both catalog sources")
  {
    const auto folder = directory.filePath("terrain-library");
    REQUIRE(QDir{}.mkdir(folder));
    const auto colors = std::array{QColor{Qt::red}, QColor{Qt::green}, QColor{Qt::blue}};
    for (auto i = size_t{0}; i < colors.size(); ++i)
    {
      auto image = QImage{64, 64, QImage::Format_RGBA8888};
      image.fill(colors[i]);
      REQUIRE(image.save(folder + "/layer" + QString::number(i) + ".png"));
    }
    auto library =
      require(project::AssetLibrary::create(pathFromQString(folder), "Terrain"));
    require(library->importDirectory(library->folder()));
    const auto textures = require(library->assets(project::AssetType::Texture));
    REQUIRE(textures.size() == 3);
    auto definition = project::AssetLibrary::TerrainMaterialDefinition{};
    definition.name = "Grass, dirt and rock";
    definition.folder = "materials";
    for (auto i = size_t{0}; i < colors.size(); ++i)
    {
      const auto name = "layer" + std::to_string(i) + ".png";
      const auto found =
        std::ranges::find(textures, name, &project::LibraryAsset::sourceContainer);
      REQUIRE(found != textures.end());
      definition.materialSlots[i] = {"Layer " + std::to_string(i), found->id};
    }
    const auto material = require(library->createTerrainMaterial(definition));
    require(session->attachLibrary(pathFromQString(folder)));

    emit session->browserFocusRequested();
    auto* browser = window.findChild<ModelBrowser*>();
    REQUIRE(browser);
    auto* source = browser->findChild<QTabBar*>("ModelBrowser_Source");
    auto* type = browser->findChild<QComboBox*>("ModelBrowser_Type");
    auto* folders = browser->findChild<QTreeWidget*>("ModelBrowser_FolderTree");
    auto* cards = browser->findChild<ModelBrowserView*>();
    REQUIRE(source);
    REQUIRE(type);
    REQUIRE(folders);
    REQUIRE(cards);
    const auto materialIndex = type->findData(int(BrowserCellType::Material));
    REQUIRE(materialIndex >= 0);
    type->setCurrentIndex(materialIndex);
    source->setCurrentIndex(sourceTabIndex(*source, BrowserSourceFilter::AssetLibrary));
    QTRY_COMPARE(cards->assets().size(), 1);
    auto libraryEntry = cards->assets().front();
    CHECK(libraryEntry.type == BrowserCellType::Material);
    CHECK(libraryEntry.libraryAssetId == material.id);
    CHECK_FALSE(folders->findItems("Material", Qt::MatchRecursive).empty());
    CHECK(catalogAssetDragData(libraryEntry, session->store().info().id).isEmpty());
    const auto libraryPreview =
      require(inspection::terrainMaterialPreview(libraryEntry, session.get()));
    CHECK(libraryPreview.pixelColor(0, 0).red() > 200);
    CHECK(libraryPreview.pixelColor(127, 0).blue() > 200);

    auto inspector = AssetInspector{};
    browser->bindInspector(inspector, true);
    inspector.setAsset(
      inspection::readBrowserAsset(libraryEntry, session.get(), *alphaDocument));
    REQUIRE(inspector.asset());
    CHECK(inspector.asset()->resource.type == BrowserCellType::Material);
    CHECK(inspector.findChild<QPushButton*>("AssetInspector_Apply")->isHidden());
    CHECK_FALSE(inspector.findChild<QPushButton*>("AssetInspector_Capture")->isHidden());

    const auto captured =
      require(session->captureLibraryAsset(library->info().id, material.id));
    source->setCurrentIndex(sourceTabIndex(*source, BrowserSourceFilter::Project));
    QTRY_VERIFY(
      cards->assets().size() == 1
      && cards->assets().front().source == BrowserSource::Project);
    const auto projectEntry = cards->assets().front();
    CHECK(projectEntry.type == BrowserCellType::Material);
    CHECK(projectEntry.projectAssetId == captured.id);
    CHECK(projectEntry.displayName == "Grass, dirt and rock");
    CHECK(catalogAssetDragData(projectEntry, session->store().info().id).isEmpty());
    const auto projectPreview =
      require(inspection::terrainMaterialPreview(projectEntry, session.get()));
    CHECK(projectPreview.pixelColor(0, 0).red() > 200);
    CHECK(projectPreview.pixelColor(127, 0).blue() > 200);
    inspector.setAsset(
      inspection::readBrowserAsset(projectEntry, session.get(), *alphaDocument));
    REQUIRE(inspector.asset());
    CHECK(inspector.asset()->resource.type == BrowserCellType::Material);
    CHECK(inspector.findChild<QPushButton*>("AssetInspector_Apply")->isHidden());
    CHECK(inspector.findChild<QLabel*>("AssetInspector_Summary")
            ->text()
            .contains("Terrain panel"));

    emit cards->folderActivated("materials");
    const auto hasBlendPixels = [&] {
      const auto image = cards->grabFramebuffer();
      auto red = 0;
      auto blue = 0;
      for (auto y = 0; y < image.height(); y += 2)
        for (auto x = 0; x < image.width(); x += 2)
        {
          const auto color = image.pixelColor(x, y);
          if (color.red() > 180 && color.green() < 70 && color.blue() < 70)
            ++red;
          if (color.blue() > 180 && color.red() < 70 && color.green() < 70)
            ++blue;
        }
      return red > 40 && blue > 40;
    };
    REQUIRE(QTest::qWaitFor(hasBlendPixels, 3000));

    auto previewRequests = 0;
    auto applyRequests = 0;
    QObject::disconnect(
      cards, &ModelBrowserView::assetPreviewRequested, browser, nullptr);
    QObject::connect(
      cards, &ModelBrowserView::assetPreviewRequested, &window, [&](const auto&) {
        ++previewRequests;
      });
    QObject::connect(
      cards, &ModelBrowserView::assetApplyRequested, &window, [&](const auto&) {
        ++applyRequests;
      });
    cards->setScrollPosition(0);
    auto position = std::optional<QPoint>{};
    cards->scrollToCell([&](const auto& cell) {
      if (cell.template itemAs<BrowserCellData>().type == BrowserCellType::Material)
      {
        const auto& bounds = cell.itemBounds();
        position = QPoint{
          int(bounds.left() + bounds.width / 2), int(bounds.top() + bounds.height / 2)};
      }
      return false;
    });
    REQUIRE(position);
    QTest::mouseDClick(cards, Qt::LeftButton, Qt::NoModifier, *position);
    CHECK(previewRequests == 1);
    CHECK(applyRequests == 0);

    type->setCurrentIndex(type->findData(int(BrowserCellType::Texture)));
    CHECK(std::ranges::none_of(cards->assets(), [](const auto& entry) {
      return entry.type == BrowserCellType::Material;
    }));

    type->setCurrentIndex(materialIndex);
    source->setCurrentIndex(sourceTabIndex(*source, BrowserSourceFilter::AssetLibrary));
    QTRY_VERIFY(
      cards->assets().size() == 1
      && cards->assets().front().source == BrowserSource::AssetLibrary);
    const auto previewIdentity = cards->assets().front().previewIdentity;
    REQUIRE(QFile::remove(folder + "/layer0.png"));
    auto librarySource = openLocalAssetLibrary(pathFromQString(folder));
    REQUIRE(librarySource);
    CHECK(cards->assets().front().previewIdentity == previewIdentity);
    const auto layer = std::ranges::find(
      textures, std::string{"layer0.png"}, &project::LibraryAsset::virtualPath);
    REQUIRE(layer != textures.end());
    require(librarySource->removeAsset(layer->id));
    QTRY_VERIFY(
      !cards->assets().empty()
      && cards->assets().front().previewIdentity != previewIdentity);
    CHECK(inspection::terrainMaterialPreview(cards->assets().front(), session.get())
            .is_error());
    inspector.setAsset(inspection::readBrowserAsset(
      cards->assets().front(), session.get(), *alphaDocument));
    REQUIRE(inspector.asset());
    CHECK(inspector.findChild<QLabel*>("AssetInspector_Summary")
            ->text()
            .contains("missing or invalid"));
    CHECK_FALSE(
      inspector.findChild<QComboBox*>("AssetInspector_ContentKind")->isEnabled());
    require(session->save());
  }

  SECTION("catalog skybox drops capture the group and set the world sky")
  {
    const auto doubleClick = GENERATE(false, true);
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
    require(library->importDirectory(library->folder()));
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
    emit session->browserFocusRequested();
    auto* browser = window.findChild<ModelBrowser*>();
    REQUIRE(browser);
    auto* sourceCombo = browser->findChild<QTabBar*>("ModelBrowser_Source");
    auto* folders = browser->findChild<QTreeWidget*>("ModelBrowser_FolderTree");
    auto* cards = browser->findChild<ModelBrowserView*>();
    auto* typeCombo = browser->findChild<QComboBox*>("ModelBrowser_Type");
    REQUIRE(sourceCombo);
    REQUIRE(folders);
    REQUIRE(cards);
    REQUIRE(typeCombo);
    sourceCombo->setCurrentIndex(
      sourceTabIndex(*sourceCombo, BrowserSourceFilter::AssetLibrary));
    QTRY_COMPARE(cards->assets().size(), 1);
    REQUIRE(cards->assets().front().skybox);
    CHECK(cards->assets().front().catalogPath == std::filesystem::path{"skybox/desert"});
    CHECK(folders->findItems("env", Qt::MatchRecursive).empty());
    const auto skyboxIndex = typeCombo->findText("Skyboxes");
    REQUIRE(skyboxIndex >= 0);
    typeCombo->setCurrentIndex(skyboxIndex);
    CHECK(cards->assets().size() == 1);
    typeCombo->setCurrentIndex(typeCombo->findData(int(BrowserCellType::Texture)));
    CHECK(cards->assets().empty());
    typeCombo->setCurrentIndex(skyboxIndex);
    REQUIRE(cards->assets().size() == 1);
    emit cards->folderActivated("skybox");
    const auto previewIdentity = cards->assets().front().previewIdentity;
    const auto hasSkyPixels = [&](const bool red) {
      const auto image = cards->grabFramebuffer();
      auto pixels = 0;
      for (auto y = 0; y < image.height(); y += 2)
        for (auto x = 0; x < image.width(); x += 2)
        {
          const auto color = image.pixelColor(x, y);
          if (
            red ? color.red() > 200 && color.green() < 40 && color.blue() < 40
                : color.blue() > color.red() + 32 && color.green() > color.red() + 16)
            ++pixels;
        }
      return pixels > 500;
    };
    REQUIRE(QTest::qWaitFor([&]() { return hasSkyPixels(false); }, 3000));
    for (const auto* face : {"bk", "rt", "lf", "ft", "up", "dn"})
    {
      auto image = QImage{8, 8, QImage::Format_RGBA8888};
      image.fill(Qt::red);
      REQUIRE(image.save(folder + "/env/desert" + face + ".png"));
    }
    auto source = openLocalAssetLibrary(pathFromQString(folder));
    REQUIRE(source);
    for (const auto* face : {"bk", "rt", "lf", "ft", "up", "dn"})
    {
      const auto relative = std::string{"env/desert"} + face + ".png";
      require(source->replaceFile(
        pathFromQString(folder + "/" + QString::fromStdString(relative)), relative));
    }
    QTRY_VERIFY(cards->assets().front().previewIdentity != previewIdentity);
    REQUIRE(QTest::qWaitFor([&]() { return hasSkyPixels(true); }, 3000));
    const auto payload = catalogAssetDragData(texture, session->store().info().id);
    REQUIRE(isCatalogAssetDrag(payload));

    const auto beforeCapture = require(session->store().assets()).size();
    if (doubleClick)
    {
      browser->setMapActionsEnabled(false);
      doubleClickTexture(*cards);
      CHECK(!alphaDocument->map().worldNode().entity().property("skyname"));
      CHECK(require(session->store().assets()).size() == beforeCapture);
      browser->setMapActionsEnabled(true);
      doubleClickTexture(*cards);
    }
    else
    {
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
    }

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
    const auto browsable = require(browsableProjectAssets(session->store()));
    CHECK(std::ranges::none_of(browsable, [](const auto& asset) {
      return asset.type == project::AssetType::Texture;
    }));
    sourceCombo->setCurrentIndex(
      sourceTabIndex(*sourceCombo, BrowserSourceFilter::Project));
    QTRY_VERIFY(std::ranges::any_of(
      cards->assets(), [](const auto& asset) { return asset.skybox; }));
    CHECK(std::ranges::none_of(cards->assets(), [](const auto& asset) {
      return asset.type == BrowserCellType::Texture && !asset.skybox;
    }));
    CHECK(folders->findItems("gfx", Qt::MatchRecursive).empty());
    emit cards->folderActivated("skybox");
    REQUIRE(QTest::qWaitFor([&]() { return hasSkyPixels(true); }, 3000));

    REQUIRE(map.canUndoCommand());
    map.undoCommand();
    CHECK(!map.worldNode().entity().property("skyname"));
    map.redoCommand();
    REQUIRE(map.worldNode().entity().property("skyname"));
    CHECK(*map.worldNode().entity().property("skyname") == "desert");
    map.undoCommand();
    if (doubleClick)
    {
      doubleClickTexture(*cards);
      REQUIRE(map.worldNode().entity().property("skyname"));
      CHECK(*map.worldNode().entity().property("skyname") == "desert");
      CHECK(require(session->store().assets()).size() == assets.size());
      map.undoCommand();
    }
  }

  SECTION("catalog texture drops capture the dropped revision and set the face material")
  {
    const auto doubleClick = GENERATE(false, true);
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
    require(library->importDirectory(library->folder()));
    const auto entries = require(library->assets());
    REQUIRE(entries.size() == 1);
    require(session->attachLibrary(pathFromQString(folder)));
    require(session->save());

    emit session->browserFocusRequested();
    auto* browser = window.findChild<ModelBrowser*>();
    REQUIRE(browser);
    auto* source = browser->findChild<QTabBar*>("ModelBrowser_Source");
    auto* groups = browser->findChild<QCheckBox*>("ModelBrowser_GroupWadTextures");
    auto* folders = browser->findChild<QTreeWidget*>("ModelBrowser_FolderTree");
    auto* assetView = browser->findChild<ModelBrowserView*>();
    REQUIRE(source);
    REQUIRE(groups);
    REQUIRE(folders);
    REQUIRE(assetView);
    CHECK_FALSE(groups->isChecked());
    source->setCurrentIndex(sourceTabIndex(*source, BrowserSourceFilter::AssetLibrary));
    QTRY_COMPARE(folders->findItems("Texture", Qt::MatchRecursive).size(), 1);
    CHECK(folders->findItems("custom.wad", Qt::MatchRecursive).empty());
    groups->setChecked(true);
    QTRY_COMPARE(folders->findItems("custom.wad", Qt::MatchRecursive).size(), 1);
    CHECK(
      folders->findItems("custom.wad", Qt::MatchRecursive).front()->parent()->text(0)
      == "Texture");
    emit assetView->folderActivated("textures/custom.wad");
    groups->setChecked(false);
    REQUIRE(folders->currentItem());
    CHECK(folders->currentItem()->data(0, Qt::UserRole).toString() == "textures");
    CHECK(folders->findItems("custom.wad", Qt::MatchRecursive).empty());

    auto texture = BrowserAsset{BrowserCellType::Texture, {}};
    texture.source = BrowserSource::AssetLibrary;
    texture.libraryId = library->info().id;
    texture.libraryAssetId = entries.front().id;
    texture.libraryRevision = entries.front().revision;
    const auto payload = catalogAssetDragData(texture, session->store().info().id);
    REQUIRE(isCatalogAssetDrag(payload));

    const auto beforeCapture = require(session->store().assets()).size();
    if (doubleClick)
    {
      const auto target =
        std::ranges::find_if(assetView->assets(), [&](const auto& asset) {
          return asset.libraryId == texture.libraryId
                 && asset.libraryAssetId == texture.libraryAssetId;
        });
      REQUIRE(target != assetView->assets().end());
      const auto texturePath = target->path;
      browser->setMapActionsEnabled(false);
      doubleClickTexture(*assetView, texturePath);
      CHECK(face.face().materialName() == previous);
      CHECK(require(session->store().assets()).size() == beforeCapture);
      browser->setMapActionsEnabled(true);
      mdl::deselectAll(map);
      doubleClickTexture(*assetView, texturePath);
      CHECK(face.face().materialName() == previous);
      CHECK(require(session->store().assets()).size() == beforeCapture);
      mdl::selectBrushFaces(map, {face});
      doubleClickTexture(*assetView, texturePath);
    }
    else
    {
      auto* view = alphaWindow->currentMapViewBase();
      REQUIRE(view != nullptr);
      auto mime = QMimeData{};
      mime.setText(payload);
      // Look away from the brush so the drop falls back to the face selection,
      // independently of the viewport's aspect ratio.
      view->camera().moveTo(vm::vec3f{512, 0, 0});
      view->camera().setDirection(vm::vec3f{1, 0, 0}, vm::vec3f{0, 0, 1});
      const auto position = view->rect().center();
      auto enter =
        QDragEnterEvent{position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier};
      QApplication::sendEvent(view, &enter);
      REQUIRE(enter.isAccepted());
      REQUIRE(view->pickResult().empty());
      auto drop = QDropEvent{
        QPointF{position}, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier};
      QApplication::sendEvent(view, &drop);
      CHECK(drop.isAccepted());
    }
    CHECK(map.modified());
    CHECK(face.face().materialName() == "WALL01");
    const auto assets = require(session->store().assets());
    const auto textureAsset = std::ranges::find_if(assets, [](const auto& asset) {
      return asset.type == project::AssetType::Texture;
    });
    REQUIRE(textureAsset != assets.end());
    CHECK(textureAsset->publishPath == "textures/WALL01.wad");
    CHECK(textureAsset->library.libraryAssetId == entries.front().id);
    auto capturedAsset =
      BrowserAsset{BrowserCellType::Texture, textureAsset->publishPath};
    capturedAsset.source = BrowserSource::Project;
    capturedAsset.projectId = session->store().info().id;
    capturedAsset.assetId = textureAsset->id;
    const auto captured =
      require(inspection::readBrowserAsset(capturedAsset, session.get(), *alphaDocument));
    CHECK(captured.nativeFormat == "goldsrc-miptex");
    CHECK(
      require(inspection::decodeImage(captured.original, captured.nativeFormat)).size()
      == QSize{16, 16});
    CHECK(
      require(inspection::decodeImage(captured.compatible, captured.nativeFormat)).size()
      == QSize{16, 16});

    // Applying a material is one native undo step, so the drop stays reversible.
    REQUIRE(map.canUndoCommand());
    map.undoCommand();
    CHECK(face.face().materialName() == previous);
    map.redoCommand();
    CHECK(face.face().materialName() == "WALL01");
    map.undoCommand();
    if (doubleClick)
    {
      source->setCurrentIndex(sourceTabIndex(*source, BrowserSourceFilter::Project));
      QTRY_VERIFY(std::ranges::any_of(assetView->assets(), [&](const auto& asset) {
        return asset.source == BrowserSource::Project
               && asset.assetId == textureAsset->id;
      }));
      emit assetView->folderActivated("textures");
      const auto target =
        std::ranges::find_if(assetView->assets(), [&](const auto& asset) {
          return asset.source == BrowserSource::Project
                 && asset.assetId == textureAsset->id;
        });
      REQUIRE(target != assetView->assets().end());
      doubleClickTexture(*assetView, target->path);
      CHECK(face.face().materialName() == "WALL01");
      CHECK(require(session->store().assets()).size() == assets.size());
      map.undoCommand();
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
    require(library->importDirectory(library->folder()));
    require(session->attachLibrary(pathFromQString(folder)));
    require(session->save());
    alphaWindow->switchToInfoPanelPage(InfoPanelPage::Assets);
    auto* source = window.findChild<QTabBar*>("ModelBrowser_Source");
    REQUIRE(source != nullptr);
    source->setCurrentIndex(sourceTabIndex(*source, BrowserSourceFilter::AssetLibrary));
    QApplication::processEvents();
    QTest::qWait(100);
    auto* browser = window.findChild<ModelBrowserView*>();
    REQUIRE(browser != nullptr);
    emit browser->folderActivated("sound");
    QApplication::processEvents();
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
