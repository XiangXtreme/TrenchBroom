/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/ProjectWindow.h"

#include <QCloseEvent>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "mdl/GameConfig.h"
#include "mdl/GameInfo.h"
#include "mdl/GameManager.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "mdl/MapHeader.h"
#include "project/AssetLibrary.h"
#include "ui/ActionManager.h"
#include "ui/AppController.h"
#include "ui/AssetLibraryWatcher.h"
#include "ui/MapCommandSurface.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/ProjectLibraryDialog.h"
#include "ui/ProjectResourceDialog.h"
#include "ui/ProjectSession.h"
#include "ui/ProjectTextureImport.h"
#include "ui/QPathUtils.h"
#include "ui/RecentDocuments.h"

#include <fmt/ranges.h>

#include <algorithm>
#include <map>
#include <sstream>

namespace tb::ui
{
namespace
{
Result<project::AssetImport> readImport(const QString& filename)
{
  auto file = QFile{filename};
  if (!file.open(QIODevice::ReadOnly))
    return Error{file.errorString().toStdString()};
  const auto bytes = file.readAll();
  if (file.error() != QFileDevice::NoError)
    return Error{file.errorString().toStdString()};
  const auto path = pathFromQString(filename);
  const auto ext = pathAsQString(path.extension()).toLower();
  auto asset = project::AssetImport{};
  auto folder = std::string{};
  if (ext == ".map")
  {
    asset.type = project::AssetType::Map;
    folder = "maps/";
  }
  else if (ext == ".mdl")
  {
    asset.type = project::AssetType::Model;
    folder = "models/";
  }
  else if (ext == ".spr")
  {
    asset.type = project::AssetType::Sprite;
    folder = "sprites/";
  }
  else if (ext == ".wav")
  {
    asset.type = project::AssetType::Sound;
    folder = "sound/";
  }
  else if (ext == ".tbprefab")
  {
    asset.type = project::AssetType::Prefab;
    folder = "prefabs/";
  }
  else if (
    ext == ".png" || ext == ".bmp" || ext == ".tga" || ext == ".jpg" || ext == ".wad")
  {
    asset.type = project::AssetType::Texture;
    folder = "textures/";
  }
  else
    return Error{"Unsupported project asset type."};
  asset.sourceName = pathAsQString(path.filename()).toStdString();
  asset.virtualPath = folder + asset.sourceName;
  asset.displayName = pathAsQString(path.stem()).toStdString();
  asset.original.assign(bytes.begin(), bytes.end());
  asset.compatible = asset.original;
  // Importing bytes is distinct from validating or converting engine resources.
  asset.conversionRecipe =
    R"({"importer":"native-copy","version":1,"engineValidated":false})";
  return asset;
}
} // namespace

ProjectWindow::ProjectWindow(AppController& app, std::shared_ptr<ProjectSession> session)
  : m_appController{app}
  , m_session{std::move(session)}
{
  setAttribute(Qt::WA_DeleteOnClose);
  setObjectName("ProjectWindow");
  resize(1000, 680);
  m_libraryWatcher = new AssetLibraryWatcher{this};
  connect(m_libraryWatcher, &AssetLibraryWatcher::foldersChanged, this, [this]() {
    statusBar()->showMessage(
      tr("Asset library files changed. Use Library > Rescan Library and Check for "
         "Updates to review them."));
  });
  m_commands = std::make_unique<MapCommandSurface>(m_appController, [this]() {
    auto* window = activeMapWindow();
    return ActionExecutionContext{
      m_appController, window, window ? window->currentMapViewBase() : nullptr};
  });
  m_commands->populateMenuBar(*menuBar());
  m_appController.recentDocuments().addMenu(*m_commands->recentDocumentsMenu());
  auto* toolbar = addToolBar(tr("Map"));
  m_mapToolbar = toolbar;
  toolbar->setObjectName("ProjectMapToolBar");
  toolbar->setFloatable(false);
  toolbar->setMovable(false);
  toolbar->setIconSize(QSize{20, 20});
  m_commands->populateToolBar(*toolbar);
  m_commands->updateShortcuts();
  auto* file = menuBar()->addMenu(tr("Project"));
  file->addAction(tr("New Map…"), this, &ProjectWindow::newMap);
  m_saveProject =
    file->addAction(tr("Save Project"), QKeySequence::Save, this, [this]() { save(); });
  // The workspace owns Save even when every map tab has been closed.
  auto* mapSave = m_commands->findAction(
    m_appController.actionManager().actionsMap().at("Menu/File/Save"));
  mapSave->setShortcut({});
  file->addAction(tr("Save Project Copy…"), this, [this]() {
    const auto path = QFileDialog::getSaveFileName(
      this, tr("Save Project Copy"), {}, "TB projects (*.tbproj)");
    if (!path.isEmpty())
      report(m_session->saveCopy(pathFromQString(path)));
  });
  file->addAction(tr("Import Files…"), this, &ProjectWindow::importFiles);
  file->addAction(tr("Check Resources…"), this, [this]() { checkResources(); });
  file->addAction(
    tr("Export Compile Snapshot…"), this, &ProjectWindow::exportCompileSnapshot);
  file->addAction(tr("Close Project"), this, &QWidget::close);
  auto* library = menuBar()->addMenu(tr("Library"));
  library->addAction(
    tr("Attach Asset Library…"), this, &ProjectWindow::attachAssetLibrary);
  library->addAction(
    tr("Check for Asset Updates…"), this, [this]() { checkLibraryUpdates(false); });
  library->addAction(tr("Rescan Library and Check for Updates…"), this, [this]() {
    checkLibraryUpdates(true);
  });
  auto* asset = menuBar()->addMenu(tr("Asset"));
  asset->addAction(tr("Open Map"), this, &ProjectWindow::openSelected);
  asset->addAction(tr("Reimport…"), this, &ProjectWindow::reimportSelected);
  asset->addAction(tr("Rename…"), this, &ProjectWindow::renameSelected);
  asset->addAction(tr("Move…"), this, &ProjectWindow::moveSelected);
  asset->addAction(tr("Export Copy…"), this, &ProjectWindow::exportSelected);
  asset->addAction(tr("Find Uses…"), this, [this]() {
    if (const auto id = selectedAsset())
      checkResources(id);
  });
  auto* history = menuBar()->addMenu(tr("History"));
  history->addAction(tr("Create Version…"), this, &ProjectWindow::createCheckpoint);
  history->addAction(
    tr("Compare with Working State"), this, &ProjectWindow::compareCheckpoint);
  history->addAction(tr("Restore Version"), this, &ProjectWindow::restoreCheckpoint);

  auto* body = new QWidget{this};
  auto* layout = new QVBoxLayout{body};

  // Workspace row: one tab per live map view, plus the entry point back to closed views.
  auto* workspace = new QWidget{body};
  auto* workspaceRow = new QHBoxLayout{workspace};
  workspaceRow->setContentsMargins(0, 0, 0, 0);
  m_mapTabs = new QTabBar{workspace};
  m_mapTabs->setObjectName("ProjectMapTabs");
  m_mapTabs->setDocumentMode(true);
  m_mapTabs->setExpanding(false);
  m_mapTabs->setTabsClosable(true);
  m_mapTabs->setMovable(true);
  m_reopenMaps = new QToolButton{workspace};
  m_reopenMaps->setObjectName("ProjectReopenMaps");
  m_reopenMaps->setText(tr("Reopen Map"));
  m_reopenMaps->setPopupMode(QToolButton::InstantPopup);
  m_reopenMapsMenu = new QMenu{m_reopenMaps};
  m_reopenMaps->setMenu(m_reopenMapsMenu);
  workspaceRow->addWidget(m_mapTabs, 1);
  workspaceRow->addWidget(m_reopenMaps, 0);
  layout->addWidget(workspace);

  m_maps = new QStackedWidget{body};
  m_maps->setObjectName("ProjectMapStack");
  layout->addWidget(m_maps, 4);

  auto* content = new QWidget{this};
  auto* contentLayout = new QVBoxLayout{content};
  m_search = new QLineEdit{content};
  m_search->setPlaceholderText(tr("Search project content"));
  contentLayout->addWidget(m_search);
  auto* splitter = new QSplitter{Qt::Vertical, content};
  m_assets = new QTreeWidget{splitter};
  m_assets->setObjectName("ProjectAssets");
  m_assets->setHeaderLabels(
    {tr("Asset"),
     tr("Type"),
     tr("Virtual Path"),
     tr("Revision"),
     tr("Origin"),
     tr("Import Status"),
     tr("Library Status")});
  m_assets->setRootIsDecorated(false);
  m_assets->setSortingEnabled(true);
  m_assets->header()->setSectionResizeMode(2, QHeaderView::Stretch);
  m_history = new QTreeWidget{splitter};
  m_history->setObjectName("ProjectHistory");
  m_history->setHeaderLabels({tr("Version"), tr("Created"), tr("Description")});
  m_history->setRootIsDecorated(false);
  splitter->setStretchFactor(0, 4);
  splitter->setStretchFactor(1, 1);
  contentLayout->addWidget(splitter);
  auto* contentDock = new QDockWidget{tr("Project Content"), this};
  contentDock->setObjectName("ProjectContentDock");
  contentDock->setWidget(content);
  addDockWidget(Qt::BottomDockWidgetArea, contentDock);
  resizeDocks({contentDock}, {200}, Qt::Vertical);
  splitter->setSizes({130, 50});
  file->addAction(contentDock->toggleViewAction());
  setCentralWidget(body);
  connect(m_search, &QLineEdit::textChanged, this, [this]() { refresh(); });
  connect(m_mapTabs, &QTabBar::currentChanged, this, [this](const int index) {
    if (index < 0)
      return;
    const auto assetId = m_mapTabs->tabData(index).toString().toStdString();
    if (assetId.empty())
      return;
    const auto* document = m_session->documentFor(assetId);
    if (!document)
      return;
    if (auto* window = mapWindowFor(*document))
    {
      m_appController.mapWindowManager().activateMapWindow(*window);
    }
  });
  connect(m_mapTabs, &QTabBar::tabCloseRequested, this, [this](const int index) {
    const auto assetId = m_mapTabs->tabData(index).toString().toStdString();
    // Closing the tab closes the view only: the document and its modifications stay in
    // the session, so saving the project still writes them and reopening restores the
    // state.
    if (const auto* document = m_session->documentFor(assetId))
    {
      // The session keeps the document, so the view can go away without losing the work.
      // Retaining first also means a later view destruction cannot unload it by accident.
      if (auto* window = mapWindowFor(*document))
      {
        if (
          !window->closeCompileDialog()
          || !report(m_session->retainDocument(window->documentHandle(), assetId)))
          return;
        m_appController.mapWindowManager().closeMapView(*window);
      }
    }
    refreshMapTabs();
    if (auto* active = activeMapWindow())
      m_appController.mapWindowManager().activateMapWindow(*active);
    refreshTitle();
  });
  connect(m_assets, &QTreeWidget::itemDoubleClicked, this, [this]() { openSelected(); });
  connect(
    m_session.get(), &ProjectSession::contentsChanged, this, &ProjectWindow::refresh);
  connect(
    m_session.get(), &ProjectSession::stateChanged, this, &ProjectWindow::refreshTitle);
  // Document registration and per-map modification state both surface as stateChanged, so
  // the tab strip follows edits without polling.
  connect(
    m_session.get(),
    &ProjectSession::stateChanged,
    this,
    &ProjectWindow::scheduleMapTabsRefresh);
  connect(
    m_session.get(), &ProjectSession::runtimeError, this, [this](const QString& message) {
      QMessageBox::critical(this, tr("Project Asset Cache"), message);
    });
  connect(
    m_session.get(), &ProjectSession::libraryLinksChanged, this, &ProjectWindow::refresh);
  connect(
    &m_appController.mapWindowManager(),
    &MapWindowManager::mapWindowsChanged,
    this,
    &ProjectWindow::scheduleMapTabsRefresh);
  connect(
    &m_appController.mapWindowManager(),
    &MapWindowManager::mapWindowActivated,
    this,
    &ProjectWindow::activateMapTab);
  for (auto* menu : menuBar()->findChildren<QMenu*>())
    connect(menu, &QMenu::aboutToShow, this, &ProjectWindow::refreshCommands);
  refresh();
  if (auto* window = activeMapWindow())
    m_appController.mapWindowManager().activateMapWindow(*window);
  refreshCommands();
  QTimer::singleShot(0, this, [this]() { maybePromptForLibraryUpdates(); });
}

ProjectWindow::~ProjectWindow()
{
  m_closing = true;
  disconnect(&m_appController.mapWindowManager(), nullptr, this, nullptr);
  // Destroy map widgets while the session and host command surface still exist.
  for (auto* window : m_appController.mapWindowManager().mapWindows())
  {
    if (window->project() != m_session)
      continue;
    disconnect(window, nullptr, this, nullptr);
    m_appController.mapWindowManager().closeMapView(*window);
    delete window;
  }
  for (auto* window :
       m_maps->findChildren<MapWindow*>(QString{}, Qt::FindDirectChildrenOnly))
    delete window;
  m_appController.recentDocuments().removeMenu(*m_commands->recentDocumentsMenu());
}

MapWindow* ProjectWindow::activeMapWindow() const
{
  return m_maps ? dynamic_cast<MapWindow*>(m_maps->currentWidget()) : nullptr;
}

void ProjectWindow::activateMapTab(MapWindow* window)
{
  if (m_closing || !window || window->project() != m_session)
    return;
  refreshMapTabs();
  m_maps->setCurrentWidget(window);
  const auto id = m_session->assetFor(window->document());
  const auto blocker = QSignalBlocker{m_mapTabs};
  for (int i = 0; id && i < m_mapTabs->count(); ++i)
    if (m_mapTabs->tabData(i).toString().toStdString() == *id)
      m_mapTabs->setCurrentIndex(i);
  refreshCommands();
}

void ProjectWindow::refreshCommands()
{
  if (m_closing)
    return;
  m_commands->updateShortcuts();
  auto* mapSave = m_commands->findAction(
    m_appController.actionManager().actionsMap().at("Menu/File/Save"));
  mapSave->setShortcut({});
  if (auto* window = activeMapWindow())
    window->refreshCommandSurface(*m_commands);
  else
  {
    m_commands->updateActionState();
    m_commands->undoAction()->setEnabled(false);
    m_commands->redoAction()->setEnabled(false);
  }
}

const std::shared_ptr<ProjectSession>& ProjectWindow::session() const
{
  return m_session;
}

void ProjectWindow::checkResources(std::optional<std::string> assetId)
{
  const auto* game =
    m_appController.gameManager().gameInfo(m_session->store().info().gameId);
  if (
    !game
    || !std::ranges::any_of(
      game->gameConfig.materialConfig.extensions, [](const auto& ext) {
        return pathAsQString(ext).compare(".c", Qt::CaseInsensitive) == 0;
      }))
  {
    report(Error{"Resource checking currently requires a GoldSrc game configuration."});
    return;
  }
  auto dialog = ProjectResourceDialog{
    *m_session,
    std::move(assetId),
    [this](const auto& id) {
      report(m_appController.mapWindowManager().loadProjectMap(m_session, id));
    },
    this};
  dialog.exec();
}

void ProjectWindow::exportCompileSnapshot()
{
  const auto id = selectedAsset();
  if (!id)
  {
    QMessageBox::information(
      this, tr("Export Compile Snapshot"), tr("Select a map in the project first."));
    return;
  }
  const auto asset = m_session->store().findAsset(*id);
  if (asset.is_error())
  {
    report(Error{std::get<Error>(asset.error()).msg});
    return;
  }
  if (!asset.value() || asset.value()->type != project::AssetType::Map)
  {
    QMessageBox::information(
      this, tr("Export Compile Snapshot"), tr("Select a map in the project first."));
    return;
  }
  const auto folder = QFileDialog::getExistingDirectory(
    this, tr("Export Compile Snapshot"), {}, QFileDialog::ShowDirsOnly);
  if (folder.isEmpty())
    return;
  report(m_session->exportCompileSnapshot(*id, pathFromQString(folder)));
}

void ProjectWindow::attachAssetLibrary()
{
  const auto folder = QFileDialog::getExistingDirectory(
    this, tr("Attach Asset Library"), {}, QFileDialog::ShowDirsOnly);
  if (folder.isEmpty())
    return;
  const auto path = pathFromQString(folder);
  // Attaching a plain folder is the normal way to start a library, but writing a catalog
  // into an arbitrary folder must be an explicit decision.
  const auto create = !project::AssetLibrary::isLibraryFolder(path)
                      && QMessageBox::question(
                           this,
                           tr("Create Asset Library"),
                           tr("'%1' is not an asset library yet.\n\n"
                              "Create a catalog inside it and import the resources it "
                              "contains?")
                             .arg(QFileInfo{folder}.fileName()))
                           == QMessageBox::Yes;
  const auto attached = m_session->attachLibrary(path, create);
  if (attached.is_error())
  {
    report(Error{std::get<Error>(attached.error()).msg});
    return;
  }
  const auto& summary = attached.value();
  statusBar()->showMessage(
    summary.created
      ? tr("Created asset library with %1 resource(s) in %2 group(s).")
          .arg(summary.assets)
          .arg(summary.groups)
      : tr("Attached asset library with %1 resource(s).").arg(summary.assets));
  if (!summary.warnings.empty())
    QMessageBox::warning(
      this,
      tr("Asset Library"),
      QString::fromStdString(fmt::format("{}", fmt::join(summary.warnings, "\n"))));
  refresh();
}

void ProjectWindow::checkLibraryUpdates(const bool rescan)
{
  auto dialog = ProjectLibraryDialog{*m_session, rescan, this};
  dialog.exec();
  refresh();
  if (rescan)
  {
    m_libraryWatcher->acknowledgeChanges();
    statusBar()->clearMessage();
  }
}

void ProjectWindow::maybePromptForLibraryUpdates()
{
  // Only a project that already references a library can have library updates, and the
  // user can defer the prompt for this session.
  if (m_session->libraryUpdatesDeferred())
    return;
  const auto updates = m_session->checkLibraryUpdates(false);
  if (updates.is_error() || std::ranges::none_of(updates.value(), [](const auto& update) {
        return update.needsAttention();
      }))
    return;
  checkLibraryUpdates(false);
}

bool ProjectWindow::report(const Result<void>& result)
{
  if (result.is_success())
    return true;
  QMessageBox::critical(
    this, tr("Project"), QString::fromStdString(std::get<Error>(result.error()).msg));
  return false;
}

MapWindow* ProjectWindow::mapWindowFor(const MapDocument& document) const
{
  for (auto* window : m_appController.mapWindowManager().mapWindows())
    if (&window->document() == &document)
      return window;
  return nullptr;
}

void ProjectWindow::scheduleMapTabsRefresh()
{
  if (m_closing || m_mapTabsRefreshScheduled)
    return;
  m_mapTabsRefreshScheduled = true;
  QTimer::singleShot(0, this, [this]() {
    m_mapTabsRefreshScheduled = false;
    refreshMapTabs();
  });
}

void ProjectWindow::refreshMapTabs()
{
  if (m_closing || !m_mapTabs)
    return;
  const auto assets = m_session->store().assets();
  if (assets.is_error())
    return;

  const auto previous = m_mapTabs->currentIndex() >= 0
                          ? m_mapTabs->tabData(m_mapTabs->currentIndex()).toString()
                          : QString{};
  auto order = std::vector<QString>{};
  for (int i = 0; i < m_mapTabs->count(); ++i)
    order.push_back(m_mapTabs->tabData(i).toString());
  auto sortedAssets = assets.value();
  std::stable_sort(
    sortedAssets.begin(), sortedAssets.end(), [&](const auto& a, const auto& b) {
      return std::ranges::find(order, QString::fromStdString(a.id))
             < std::ranges::find(order, QString::fromStdString(b.id));
    });
  // Remove views that have left the manager, including deferred deletions.
  for (int i = m_maps->count() - 1; i >= 0; --i)
  {
    auto* window = dynamic_cast<MapWindow*>(m_maps->widget(i));
    const auto windows = m_appController.mapWindowManager().mapWindows();
    if (std::ranges::find(windows, window) == windows.end())
      m_maps->removeWidget(window);
  }
  // Rebuilding the strip must not look like the user switching maps.
  const auto blocker = QSignalBlocker{m_mapTabs};
  while (m_mapTabs->count() > 0)
    m_mapTabs->removeTab(0);

  auto closed = std::vector<std::pair<std::string, QString>>{};
  auto openTabs = 0;
  for (const auto& asset : sortedAssets)
  {
    if (asset.type != project::AssetType::Map)
      continue;
    const auto name = QString::fromStdString(asset.displayName);
    const auto* document = m_session->documentFor(asset.id);
    auto* window = document ? mapWindowFor(*document) : nullptr;
    // Stack pages are hidden when inactive; manager membership determines open tabs.
    if (!document || !window)
    {
      closed.emplace_back(asset.id, name);
      continue;
    }
    if (m_maps->indexOf(window) < 0)
    {
      if (!report(m_session->retainDocument(window->documentHandle(), asset.id)))
        continue;
      window->setCommandToolBar(*m_mapToolbar);
      m_maps->addWidget(window);
      connect(
        window, &MapWindow::commandStateChanged, this, &ProjectWindow::refreshCommands);
    }
    const auto index =
      m_mapTabs->addTab(document->map().modified() ? name + tr(" *") : name);
    m_mapTabs->setTabData(index, QString::fromStdString(asset.id));
    ++openTabs;
    m_mapTabs->setTabToolTip(
      index, tr("%1 — %2").arg(name, QString::fromStdString(asset.publishPath)));
    if (m_mapTabs->tabData(index).toString() == previous)
      m_mapTabs->setCurrentIndex(index);
  }
  if (m_mapTabs->count() == 0)
  {
    const auto index = m_mapTabs->addTab(tr("No open map"));
    m_mapTabs->setTabData(index, QString{});
    m_mapTabs->setTabEnabled(index, false);
  }
  m_mapTabs->setTabsClosable(openTabs > 0);
  if (
    const auto* document = m_session->documentFor(
      m_mapTabs->tabData(m_mapTabs->currentIndex()).toString().toStdString()))
    if (auto* window = mapWindowFor(*document))
      m_maps->setCurrentWidget(window);
  refreshCommands();

  // The menu is rebuilt from inside a triggered action, so the old actions are removed
  // and scheduled for deletion instead of being destroyed while their signal is still on
  // the stack.
  for (auto* action : m_reopenMapsMenu->actions())
  {
    m_reopenMapsMenu->removeAction(action);
    action->deleteLater();
  }
  for (const auto& [assetId, name] : closed)
  {
    auto* action = m_reopenMapsMenu->addAction(name);
    connect(
      action, &QAction::triggered, this, [this, assetId]() { openMapTab(assetId); });
  }
  m_reopenMaps->setEnabled(!closed.empty());
  if (closed.empty())
    m_reopenMaps->setToolTip(tr("Every project map is already open"));
  else
    m_reopenMaps->setToolTip(tr("Reopen a map whose tab was closed"));
}

void ProjectWindow::openMapTab(const std::string& assetId)
{
  // A closed tab keeps its document, so reopening only has to show the same view again.
  if (const auto* document = m_session->documentFor(assetId))
  {
    if (auto* window = mapWindowFor(*document))
    {
      m_appController.mapWindowManager().activateMapWindow(*window);
      refreshMapTabs();
      return;
    }
  }
  if (!report(m_appController.mapWindowManager().loadProjectMap(m_session, assetId)))
    return;
  if (const auto* document = m_session->documentFor(assetId))
  {
    if (auto* window = mapWindowFor(*document))
      m_appController.mapWindowManager().activateMapWindow(*window);
  }
  refreshMapTabs();
}

void ProjectWindow::refreshTitle()
{
  setWindowModified(m_session->modified());
  setWindowTitle(
    QString::fromStdString(m_session->store().info().name) + "[*] — TrenchBroom Project");
  setWindowFilePath(pathAsQString(m_session->path()));
}

std::optional<std::string> ProjectWindow::selectedAsset() const
{
  const auto* item = m_assets->currentItem();
  return item ? std::optional{item->data(0, Qt::UserRole).toString().toStdString()}
              : std::nullopt;
}

void ProjectWindow::refresh()
{
  if (const auto libraries = m_session->store().libraries(); libraries.is_success())
  {
    auto folders = QStringList{};
    for (const auto& library : libraries.value())
      folders.push_back(QString::fromStdString(library.path));
    m_libraryWatcher->setFolders(std::move(folders));
  }
  const auto selected = selectedAsset();
  const auto historyId = m_history->currentItem()
                           ? m_history->currentItem()->data(0, Qt::UserRole).toString()
                           : QString{};
  const auto assets = m_session->store().assets();
  if (assets.is_error())
  {
    report(Error{std::get<Error>(assets.error()).msg});
    return;
  }
  // Library state is project state: a captured snapshot keeps working even when the
  // library is gone, so an unreadable library only downgrades this column.
  auto updateStates = std::map<std::string, QString>{};
  if (const auto updates = m_session->checkLibraryUpdates(); updates.is_success())
  {
    for (const auto& update : updates.value())
    {
      switch (update.state)
      {
      case project::LibraryUpdateState::UpToDate:
        break;
      case project::LibraryUpdateState::NewVersion:
        updateStates[update.projectAssetId] = update.ignored
                                                ? tr("Library update ignored")
                                                : tr("New library version available");
        break;
      case project::LibraryUpdateState::Missing:
        updateStates[update.projectAssetId] = tr("No longer readable in library");
        break;
      case project::LibraryUpdateState::LibraryUnavailable:
        updateStates[update.projectAssetId] = tr("Library not accessible");
        break;
      case project::LibraryUpdateState::DependenciesChanged:
        updateStates[update.projectAssetId] = tr("Library dependencies changed");
        break;
      }
    }
  }
  m_assets->clear();
  for (const auto& asset : assets.value())
  {
    const auto name = QString::fromStdString(asset.displayName);
    const auto path = QString::fromStdString(asset.virtualPath);
    if (
      !name.contains(m_search->text(), Qt::CaseInsensitive)
      && !path.contains(m_search->text(), Qt::CaseInsensitive))
      continue;
    const auto update = updateStates.find(asset.id);
    auto* item = new QTreeWidgetItem{
      m_assets,
      {name,
       QString::fromLatin1(project::assetTypeName(asset.type)),
       path,
       QString::number(asset.revision),
       asset.library.empty()
         ? tr("Imported")
         : tr("Library rev %1").arg(qulonglong(asset.library.libraryRevision)),
       asset.type == project::AssetType::Map ? tr("MAP document")
       : QJsonDocument::fromJson(QByteArray::fromStdString(asset.conversionRecipe))
           .object()
           .value("formatValidated")
           .toBool()
         ? tr("Converted; format checked")
         : tr("Stored; engine validation pending"),
       update == updateStates.end() ? QString{} : update->second}};
    item->setData(0, Qt::UserRole, QString::fromStdString(asset.id));
    item->setToolTip(0, QString::fromStdString(asset.id));
    if (!asset.library.empty())
      item->setToolTip(
        4,
        tr("Captured from library asset %1 (%2)")
          .arg(
            QString::fromStdString(asset.library.sourceContainer),
            QString::fromStdString(asset.library.libraryAssetId)));
    if (selected == asset.id)
      m_assets->setCurrentItem(item);
  }
  const auto history = m_session->store().checkpoints();
  if (history.is_error())
  {
    report(Error{std::get<Error>(history.error()).msg});
    return;
  }
  m_history->clear();
  for (const auto& node : history.value())
  {
    auto* item = new QTreeWidgetItem{
      m_history,
      {QString::fromStdString(node.name),
       QString::fromStdString(node.createdAt),
       QString::fromStdString(node.description)}};
    item->setData(0, Qt::UserRole, QString::fromStdString(node.id));
    if (historyId == QString::fromStdString(node.id))
      m_history->setCurrentItem(item);
  }
  refreshTitle();
  refreshMapTabs();
}

bool ProjectWindow::save()
{
  return report(m_session->save());
}

Result<bool> ProjectWindow::prepareImport(
  project::AssetImport& asset, const std::optional<project::Asset>& previous)
{
  const auto* game =
    m_appController.gameManager().gameInfo(m_session->store().info().gameId);
  if (!game)
    return Error{"Project game configuration is unavailable."};
  if (asset.type == project::AssetType::Texture)
  {
    const auto& extensions = game->gameConfig.materialConfig.extensions;
    const auto goldsrc = std::ranges::any_of(extensions, [](const auto& ext) {
      return pathAsQString(ext).compare(".c", Qt::CaseInsensitive) == 0;
    });
    if (!goldsrc)
      return Error{
        "Project image/WAD import currently requires a GoldSrc game configuration."};
    return prepareProjectTexture(this, asset, previous);
  }
  if (asset.type != project::AssetType::Map)
    return true;
  auto stream =
    std::istringstream{std::string{asset.original.begin(), asset.original.end()}};
  const auto header = mdl::readMapHeader(stream);
  if (header.is_error())
    return std::get<Error>(header.error());
  const auto& [gameName, existingFormat] = header.value();
  if (gameName && *gameName != game->gameConfig.name)
    return Error{"Map game does not match the project's game."};
  auto format = existingFormat;
  if (format == mdl::MapFormat::Unknown)
  {
    auto formats = QStringList{};
    for (const auto& candidate : game->gameConfig.fileFormats)
      formats.push_back(QString::fromStdString(candidate.format));
    if (formats.empty())
      return Error{"Game has no supported map formats."};
    bool accepted = false;
    const auto choice = QInputDialog::getItem(
      this,
      tr("Import Map"),
      tr("This MAP has no format header. Select its format:"),
      formats,
      0,
      false,
      &accepted);
    if (!accepted)
      return false;
    format = mdl::formatFromName(choice.toStdString());
  }
  if (!std::ranges::any_of(game->gameConfig.fileFormats, [&](const auto& candidate) {
        return mdl::formatFromName(candidate.format) == format;
      }))
    return Error{"Map format is not supported by the project's game."};
  auto output = std::ostringstream{};
  mdl::writeMapHeader(output, game->gameConfig.name, format, "TrenchBroom Project");
  const auto prefix = output.str();
  asset.compatible.assign(prefix.begin(), prefix.end());
  asset.compatible.insert(
    asset.compatible.end(), asset.original.begin(), asset.original.end());
  asset.conversionRecipe = R"({"importer":"map-header","version":1})";
  return true;
}

void ProjectWindow::importFiles()
{
  const auto files = QFileDialog::getOpenFileNames(
    this,
    tr("Import Project Assets"),
    {},
    "Project assets (*.map *.mdl *.spr *.wav *.tbprefab *.wad *.png *.bmp *.tga *.jpg)");
  for (const auto& file : files)
  {
    auto imported = readImport(file);
    if (imported.is_error())
    {
      report(Error{std::get<Error>(imported.error()).msg});
      return;
    }
    auto input = std::move(imported).value();
    const auto prepared = prepareImport(input);
    if (prepared.is_error())
    {
      report(Error{std::get<Error>(prepared.error()).msg});
      return;
    }
    if (!prepared.value())
      continue;
    const auto result = m_session->importAsset(input);
    if (result.is_error())
    {
      report(Error{std::get<Error>(result.error()).msg});
      return;
    }
  }
}

void ProjectWindow::openSelected()
{
  if (const auto id = selectedAsset())
  {
    const auto asset = m_session->store().findAsset(*id);
    if (asset.is_error())
    {
      report(Error{std::get<Error>(asset.error()).msg});
      return;
    }
    if (asset.value() && asset.value()->type == project::AssetType::Map)
      report(m_appController.mapWindowManager().loadProjectMap(m_session, *id));
  }
}

void ProjectWindow::newMap()
{
  const auto* game =
    m_appController.gameManager().gameInfo(m_session->store().info().gameId);
  if (!game)
    return;
  bool ok = false;
  const auto name = QInputDialog::getText(
                      this, tr("New Map"), tr("Map name:"), QLineEdit::Normal, {}, &ok)
                      .trimmed();
  if (!ok || name.isEmpty())
    return;
  auto formats = QStringList{};
  for (const auto& format : game->gameConfig.fileFormats)
    formats.push_back(QString::fromStdString(format.format));
  if (formats.empty())
  {
    report(Error{"Game has no supported map formats."});
    return;
  }
  const auto format =
    formats.size() == 1
      ? formats.front()
      : QInputDialog::getItem(
          this, tr("New Map"), tr("Map format:"), formats, 0, false, &ok);
  if (ok)
    report(m_appController.mapWindowManager().createProjectMap(
      m_session, name.toStdString(), mdl::formatFromName(format.toStdString())));
}

void ProjectWindow::renameSelected()
{
  const auto id = selectedAsset();
  if (!id)
    return;
  const auto asset = m_session->store().findAsset(*id);
  if (asset.is_error() || !asset.value())
    return;
  bool ok = false;
  const auto name = QInputDialog::getText(
    this,
    tr("Rename Asset"),
    tr("Display name:"),
    QLineEdit::Normal,
    QString::fromStdString(asset.value()->displayName),
    &ok);
  if (ok)
    report(m_session->renameAsset(*id, name.toStdString()) | kdl::transform([](auto) {}));
}

void ProjectWindow::moveSelected()
{
  const auto id = selectedAsset();
  if (!id)
    return;
  const auto asset = m_session->store().findAsset(*id);
  if (asset.is_error() || !asset.value())
    return;
  bool ok = false;
  const auto path = QInputDialog::getText(
    this,
    tr("Move Asset"),
    tr("Virtual path including filename:"),
    QLineEdit::Normal,
    QString::fromStdString(asset.value()->virtualPath),
    &ok);
  if (ok)
    report(m_session->moveAsset(*id, path.toStdString()) | kdl::transform([](auto) {}));
}

void ProjectWindow::reimportSelected()
{
  const auto id = selectedAsset();
  if (!id)
    return;
  const auto path = QFileDialog::getOpenFileName(this, tr("Reimport Asset"));
  if (path.isEmpty())
    return;
  auto imported = readImport(path);
  if (imported.is_error())
  {
    report(Error{std::get<Error>(imported.error()).msg});
    return;
  }
  const auto previous = m_session->store().findAsset(*id);
  if (previous.is_error())
  {
    report(Error{std::get<Error>(previous.error()).msg});
    return;
  }
  if (!previous.value())
    return;
  if (imported.value().type != previous.value()->type)
  {
    report(Error{"Reimport asset type does not match."});
    return;
  }
  if (
    previous.value()->conversionRecipe.find("goldsrc-texture") != std::string::npos
    && QString::fromStdString(imported.value().sourceName)
         .endsWith(".wad", Qt::CaseInsensitive))
  {
    report(Error{
      "Reimport this converted texture from an image to preserve its texture name."});
    return;
  }
  auto input = std::move(imported).value();
  const auto prepared = prepareImport(input, previous.value());
  if (prepared.is_error())
  {
    report(Error{std::get<Error>(prepared.error()).msg});
    return;
  }
  if (!prepared.value())
    return;
  report(m_session->replaceAsset(*id, input) | kdl::transform([](auto) {}));
}

void ProjectWindow::exportSelected()
{
  const auto id = selectedAsset();
  if (!id)
    return;
  const auto path = QFileDialog::getSaveFileName(this, tr("Export Asset Copy"));
  if (path.isEmpty())
    return;
  const auto bytes = m_session->store().readAsset(*id, project::ContentKind::Compatible);
  if (bytes.is_error())
  {
    report(Error{std::get<Error>(bytes.error()).msg});
    return;
  }
  auto file = QSaveFile{path};
  if (
    !file.open(QIODevice::WriteOnly)
    || file.write(
         reinterpret_cast<const char*>(bytes.value().data()),
         qint64(bytes.value().size()))
         != qint64(bytes.value().size())
    || !file.commit())
    report(Error{file.errorString().toStdString()});
}

void ProjectWindow::createCheckpoint()
{
  bool ok = false;
  const auto name = QInputDialog::getText(
    this, tr("Create Version"), tr("Version name:"), QLineEdit::Normal, {}, &ok);
  if (ok)
    report(m_session->checkpoint(name.toStdString()) | kdl::transform([](auto) {}));
}

void ProjectWindow::compareCheckpoint()
{
  const auto* item = m_history->currentItem();
  if (!item)
    return;
  const auto changes =
    m_session->compare(item->data(0, Qt::UserRole).toString().toStdString());
  if (changes.is_error())
  {
    report(Error{std::get<Error>(changes.error()).msg});
    return;
  }
  auto lines = QStringList{};
  for (const auto& asset : changes.value().assets)
    lines
      << (asset.after ? QString::fromStdString(asset.after->virtualPath)
                      : QString::fromStdString(asset.before->virtualPath));
  lines << tr("Dependencies added: %1; removed: %2")
             .arg(changes.value().addedDependencies.size())
             .arg(changes.value().removedDependencies.size());
  auto message = QMessageBox{
    QMessageBox::Information,
    tr("Version Comparison"),
    tr("%1 assets changed").arg(changes.value().assets.size()),
    QMessageBox::Ok,
    this};
  message.setDetailedText(lines.join('\n'));
  message.exec();
}

void ProjectWindow::restoreCheckpoint()
{
  const auto* item = m_history->currentItem();
  if (!item)
    return;
  const auto id = item->data(0, Qt::UserRole).toString().toStdString();
  if (
    QMessageBox::question(
      this,
      tr("Restore Version"),
      tr("Restore this version? Current project state will be preserved in a safety "
         "version."))
    == QMessageBox::Yes)
    report(m_session->restore(id));
}

void ProjectWindow::closeEvent(QCloseEvent* event)
{
  for (auto* window : m_appController.mapWindowManager().mapWindows())
    if (window->project() == m_session && !window->closeCompileDialog())
    {
      event->ignore();
      return;
    }
  if (m_session->modified())
  {
    const auto answer = QMessageBox::question(
      this,
      tr("Close Project"),
      tr("Save project changes?"),
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Cancel || (answer == QMessageBox::Save && !save()))
    {
      event->ignore();
      return;
    }
  }
  m_closing = true;
  for (auto* window : m_appController.mapWindowManager().mapWindows())
    if (window->project() == m_session)
      m_appController.mapWindowManager().closeMapView(*window);
  event->accept();
}
} // namespace tb::ui
