/*
 Copyright (C) 2010 Kristian Duske

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

#include "ui/MapWindow.h"

#include <QApplication>
#include <QChildEvent>
#include <QClipboard>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainterPath>
#include <QPushButton>
#include <QRegion>
#include <QResizeEvent>
#include <QSettings>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QtGlobal>

#include "base/PreferenceManager.h"
#include "gl/GlManager.h"
#include "gl/ResourceManager.h"
#include "mdl/Autosaver.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/EditorContext.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityNodeBase.h"
#include "mdl/EnvironmentConfig.h"
#include "mdl/ExportOptions.h"
#include "mdl/GameInfo.h"
#include "mdl/Grid.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "mdl/Map_Assets.h"
#include "mdl/Map_Brushes.h"
#include "mdl/ModelUtils.h"
#include "mdl/Node.h"
#include "mdl/NodeQueries.h"
#include "mdl/PasteType.h"
#include "mdl/PatchNode.h"
#include "mdl/VisualEffect.h"
#include "mdl/WorldNode.h"
#include "prefs/Preferences.h"
#include "ui/Action.h"
#include "ui/ActionBuilder.h"
#include "ui/ActionExecutionContext.h"
#include "ui/ActionManager.h"
#include "ui/AppController.h"
#include "ui/ChoosePathTypeDialog.h"
#include "ui/ColorButton.h"
#include "ui/CommandPaletteDialog.h"
#include "ui/CompilationDialog.h"
#include "ui/Console.h"
#include "ui/CrashReporter.h"
#include "ui/CyclingMapView.h"
#include "ui/FaceInspector.h"
#include "ui/InfoPanel.h"
#include "ui/Inspector.h"
#include "ui/LaunchGameEngineDialog.h"
#include "ui/MapCommandSurface.h"
#include "ui/MapDocument.h"
#include "ui/MapEditorController.h"
#include "ui/MapEditorView.h"
#include "ui/MapStatus.h"
#include "ui/MapView2D.h"
#include "ui/MapView3D.h"
#include "ui/MapViewBase.h"
#include "ui/MapViewLayout.h"
#include "ui/MapViewToolBox.h"
#include "ui/MapWindowManager.h"
#include "ui/ObjExportDialog.h"
#include "ui/PluginInspector.h"
#include "ui/ProjectSession.h"
#include "ui/PythonConsole.h"
#include "ui/QPathUtils.h"
#include "ui/QStringUtils.h"
#include "ui/QStyleUtils.h"
#include "ui/QVecUtils.h"
#include "ui/RecentDocuments.h"
#include "ui/RenderView.h"
#include "ui/SignalDelayer.h"
#include "ui/Splitter.h"
#include "ui/SwitchableMapViewContainer.h"
#include "ui/ViewUtils.h"
#include "ui/WadUtils.h"
#include "ui/WidgetState.h"
#include "ui/WorkbenchSurface.h"
#include "ui/WorkspaceToolPanel.h"
#include "ui/python/PythonHandleRegistry.h"
#include "ui/python/PythonPluginManifest.h"
#include "ui/python/PythonRuntime.h"
#include "ui/python/PythonScripting.h"
#include "update/Updater.h"

#include "kd/const_overload.h"
#include "kd/contracts.h"
#include "kd/k.h"
#include "kd/overload.h"
#include "kd/ranges/to.h"
#include "kd/string_format.h"
#include "kd/string_utils.h"

#include "vm/vec.h"
#include "vm/vec_io.h"

#include <fmt/format.h>
#include <fmt/std.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iterator>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>


namespace tb::ui
{
namespace
{


void showModelessDialog(QDialog* dialog)
{
  // https://doc.qt.io/qt-5/qdialog.html#code-examples
  dialog->show();
  dialog->raise();
  dialog->activateWindow();
}

QString lastCompilationProfileSettingsKey(const std::string& gameName)
{
  return QString::fromLatin1("Compilation/LastProfile/%1")
    .arg(QString::fromStdString(gameName));
}

} // namespace

using namespace std::chrono_literals;

MapWindow::MapWindow(
  AppController& appController,
  std::shared_ptr<MapDocument> document,
  std::shared_ptr<ProjectSession> project,
  const bool embedded)
  : m_appController{appController}
  , m_document{std::move(document)}
  , m_project{std::move(project)}
  , m_embedded{embedded}
  , m_lastInputTime{std::chrono::system_clock::now()}
  , m_autosaveTimer{new QTimer{this}}
  , m_updateTitleSignalDelayer{new SignalDelayer{500ms, this}}
  , m_updateActionStateSignalDelayer{new SignalDelayer{this}}
{
  contract_pre(m_document != nullptr);

  setAttribute(Qt::WA_DeleteOnClose, !embedded);
  setObjectName("MapWindow");

  installEventFilter(this);

  createGui();
  if (!embedded)
  {
    createMenus();
    createToolBar();
    createStatusBar();
  }
  if (embedded)
  {
    m_commands->setShortcutsEnabled(false);
  }

  updateShortcuts();
  updateActionState();
  updateUndoRedoActions();
  loadLastCompilationProfileName();

  m_document->setTargetLogger(m_console);

  m_autosaveTimer->start(1000);

  connectObservers();
  bindEvents();
  if (m_project)
    connect(
      m_project.get(),
      &ProjectSession::stateChanged,
      this,
      &MapWindow::updateTitleDelayed);

  if (!m_embedded)
  {
    restoreWidgetGeometry(this);
    restoreWidgetState(this);
  }

  setAcceptDrops(true);

  const auto defaultPluginPaths = QString::fromStdString(
    PreferenceManager::instance().get(Preferences::DefaultPluginPaths));
  if (!defaultPluginPaths.isEmpty())
  {
    const auto paths = defaultPluginPaths.split('|', Qt::SkipEmptyParts);
    QTimer::singleShot(0, this, [this, paths]() {
      for (const auto& pathStr : paths)
      {
        const auto path = pathFromQString(pathStr);
        if (std::filesystem::exists(path))
        {
          PythonScripting::instance().runScript(*this, path);
        }
      }
    });
  }

  reloadPythonPlugins();

  // act as if the document was loaded to update the UI
  m_document->documentWasLoadedNotifier();
}

MapWindow::~MapWindow()
{
  if (m_project && !documentRetainedByProject())
    m_project->unregisterDocument(*m_document);
  for (auto* view : findChildren<MapView3D*>())
  {
    view->stopPlayMode();
  }

  m_pythonPluginManager.unloadPlugins(*this);
  PythonRuntime::instance().cleanupDocument(*this);
  disconnect(qApp, &QApplication::focusChanged, this, &MapWindow::focusChange);

  // Stop the autosave timer
  m_autosaveTimer->stop();

  // Search for a RenderView (QOpenGLWindow subclass) and make it current in order to
  // allow for calling OpenGL methods in destructors.
  auto* renderView = findChild<RenderView*>();
  if (renderView)
  {
    renderView->makeCurrent();
  }

  // The MapDocument's CachingLogger has a pointer to m_console, which
  // is about to be destroyed (DestroyChildren()). Clear the pointer
  // so we don't try to log to a dangling pointer (#1885).
  m_document->setTargetLogger(nullptr);

  m_mapView->toolBox().deactivateCurrentTool();

  m_notifierConnection.disconnect();
  removeRecentDocumentsMenu();
  emit toolPanelsAboutToBeDestroyed();

  // The order of deletion here is important because both the document and the children
  // need the context manager (and its embedded VBO) to clean up their resources.

  // Destroy the children first because they might still access document resources.
  // The children must be deleted in reverse order!
  const auto children = this->children();
  qDeleteAll(std::rbegin(children), std::rend(children));

  // let's trigger a final autosave before releasing the document
  m_document->triggerAutosave();
  m_document.reset();

  // FIXME: m_contextManager is deleted via smart pointer; it may release openGL resources
  // in its destructor
}

bool MapWindow::documentRetainedByProject() const
{
  if (!m_project)
    return false;
  const auto assetId = m_project->assetFor(*m_document);
  return assetId && m_project->retainedDocument(*assetId) != nullptr;
}

MapEditorView& MapWindow::editorView() const
{
  return *m_editor;
}

void MapWindow::positionOnScreen(QWidget* reference)
{
  if (!m_embedded)
  {
    restoreWidgetGeometry(this);
    restoreWidgetState(this);
  }
  if (reference)
  {
    const auto offset = QApplication::style()->pixelMetric(QStyle::PM_TitleBarHeight);
    move(reference->window()->pos() + QPoint(offset, offset));
  }
}

const std::shared_ptr<ProjectSession>& MapWindow::project() const
{
  return m_project;
}

std::shared_ptr<MapDocument> MapWindow::documentHandle() const
{
  return m_document;
}

const MapDocument& MapWindow::document() const
{
  return *m_document;
}

MapDocument& MapWindow::document()
{
  return KDL_CONST_OVERLOAD(document());
}

const MapViewToolBox& MapWindow::toolBox() const
{
  return m_mapView->toolBox();
}

MapViewToolBox& MapWindow::toolBox()
{
  return KDL_CONST_OVERLOAD(toolBox());
}

Logger& MapWindow::logger() const
{
  return *m_console;
}

Logger& MapWindow::pythonLogger() const
{
  return m_pythonConsole != nullptr ? *m_pythonConsole : logger();
}

QAction* MapWindow::findAction(const std::filesystem::path& path)
{
  const auto& actionManager = m_appController.actionManager();
  auto& actionsMap = actionManager.actionsMap();
  if (const auto iAction = actionsMap.find(path); iAction != std::end(actionsMap))
  {
    const auto& action = iAction->second;
    if (m_embedded)
      return &m_commands->actionFor(action, *this);
    if (auto* qAction = m_commands->findAction(action))
    {
      return qAction;
    }
  }
  return nullptr;
}

void MapWindow::updateTitle()
{
  const auto& map = m_document->map();
  setWindowModified(m_project ? m_project->modified() : map.modified());
  setWindowTitle(
    m_project
      ? tr("%1 — %2[*] - TrenchBroom")
          .arg(pathAsQString(map.filename()), pathAsQString(m_project->path().filename()))
      : tr("%1[*] - TrenchBroom").arg(pathAsQString(map.filename())));
  setWindowFilePath(pathAsQPath(m_project ? m_project->path() : map.path()));
}

void MapWindow::updateTitleDelayed()
{
  m_updateTitleSignalDelayer->queueSignal();
}

void MapWindow::createMenus()
{
  auto createMenuResult = m_commands->populateMenuBar(*menuBar());

  m_recentDocumentsMenu = createMenuResult.recentDocumentsMenu;
  m_undoAction = createMenuResult.undoAction;
  m_redoAction = createMenuResult.redoAction;
  m_rerunAction = createMenuResult.rerunAction;

  addRecentDocumentsMenu();
  auto* workspaceMenu = menuBar()->addMenu(tr("Workspace"));
  workspaceMenu->addAction(tr("Reset Layout"), this, [this]() {
    m_editor->resetLayout();
    m_tools->resetLayout();
  });
}

void MapWindow::updateShortcuts()
{
  m_commands->updateShortcuts();
  emit commandStateChanged();
}

void MapWindow::updateActionState()
{
  m_commands->updateActionState();
  emit commandStateChanged();
}

void MapWindow::updateActionStateDelayed()
{
  m_updateActionStateSignalDelayer->queueSignal();
}

void MapWindow::setCommandToolBar(QToolBar& toolbar)
{
  m_toolBar = &toolbar;
  connect(
    m_toolBar, &QToolBar::visibilityChanged, this, [this]() { updateActionState(); });
}

void MapWindow::refreshCommandSurface(MapCommandSurface& commands)
{
  commands.updateActionState();
  commands.updateHistoryActions(m_document->map());
  if (auto* action = commands.rerunAction())
  {
    action->setText(
      m_lastCompilationProfileName
        ? tr("Re-run \"%1\"...")
            .arg(QString::fromStdString(*m_lastCompilationProfileName))
        : tr("Re-run compilation..."));
  }
}

void MapWindow::updateUndoRedoActions()
{
  m_commands->updateHistoryActions(m_document->map());
  emit commandStateChanged();
}

void MapWindow::addRecentDocumentsMenu()
{
  m_appController.recentDocuments().addMenu(*m_recentDocumentsMenu);
}

void MapWindow::removeRecentDocumentsMenu()
{
  if (m_recentDocumentsMenu)
    m_appController.recentDocuments().removeMenu(*m_recentDocumentsMenu);
}

void MapWindow::updateRecentDocumentsMenu()
{
  if (m_project)
  {
    m_appController.recentDocuments().updatePath(m_project->path());
    return;
  }
  const auto& map = m_document->map();
  const auto path = map.path();
  if (path.is_absolute())
  {
    m_appController.recentDocuments().updatePath(path);
  }
}

void MapWindow::createGui()
{
  setWindowIconTB(this);
  setWindowTitle("TrenchBroom");

  m_editor = new MapEditorView{m_appController, document()};
  connect(
    m_editor,
    &MapEditorView::infoPanelVisibilityChanged,
    this,
    &MapWindow::updateActionState);
  setCentralWidget(m_editor);

  // The view owns the workbench widgets; the window borrows them to route its commands.
  m_mapView = m_editor->mapView();
  m_currentMapView = m_mapView->firstMapViewBase();

  // SwitchableMapViewContainer should have constructed a MapViewBase
  contract_assert(m_currentMapView);

  m_infoPanel = m_editor->infoPanel();
  m_inspector = m_editor->inspector();
  if (!m_embedded)
  {
    takeCentralWidget();
    m_tools = new WorkspaceToolPanel{WorkspaceKind::Map, m_editor, m_infoPanel, this};
    m_tools->registerMapEditor(*m_editor);
    m_tools->setMapEditor(m_editor);
    setCentralWidget(m_tools);
    connect(this, &MapWindow::toolPanelsAboutToBeDestroyed, m_tools, [this]() {
      m_tools->unregisterMapEditor(*m_editor);
    });
    connect(
      m_editor,
      &MapEditorView::infoPanelPageRequested,
      m_tools,
      &WorkspaceToolPanel::showInfoPage);
    connect(
      m_editor,
      &MapEditorView::infoPanelToggleRequested,
      m_tools,
      &WorkspaceToolPanel::toggle);
    connect(m_tools, &WorkspaceToolPanel::extensionsRequested, this, [this]() {
      m_editor->showInspectorPage(InspectorPage::Plugin);
    });
  }
  m_console = m_infoPanel->console();
  m_pythonConsole = m_infoPanel->pythonConsole();

  m_pythonConsole->setCommandExecutor([this](const std::string& source) {
    PythonScripting::instance().runConsoleCommand(*this, source);
  });
  m_pythonConsole->setCompletionRootProvider([this](const std::string_view name) {
    return PythonScripting::instance().consoleCompletionRoot(*this, name);
  });

  m_commands = std::make_unique<MapCommandSurface>(m_appController, [this]() {
    return ActionExecutionContext{m_appController, this, currentMapViewBase()};
  });
}

void MapWindow::createToolBar()
{
  m_toolBar = addToolBar("Toolbar");
  m_toolBar->setObjectName("MapWindowToolBar");
  m_toolBar->setFloatable(false);
  m_toolBar->setMovable(false);
  // Keep toolbar icons compact and aligned with the shared application style metric.
  m_toolBar->setIconSize(QSize(20, 20));

  m_commands->populateToolBar(*m_toolBar);
}

void MapWindow::createStatusBar()
{
  auto* status = new MapStatus{this};
  status->setDocument(m_document);
  auto* statusSurface = new QWidget{m_tools};
  auto* layout = new QHBoxLayout{statusSurface};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(status, 1);
  layout->addWidget(m_appController.updater().createUpdateIndicator());
  m_tools->addStatusWidget(statusSurface);
}

void MapWindow::reloadPythonPlugins()
{
  m_pythonPluginManager.unloadPlugins(*this);

  const auto pluginDirectories =
    PreferenceManager::instance().get(Preferences::PythonPluginDirectories);
  m_pythonPluginManager.reload(splitPythonPluginDirectories(pluginDirectories));

  if (!pluginDirectories.empty())
  {
    QTimer::singleShot(0, this, [this]() { m_pythonPluginManager.loadPlugins(*this); });
  }
}

void MapWindow::connectObservers()
{
  auto& prefs = PreferenceManager::instance();
  m_notifierConnection +=
    prefs.preferenceDidChangeNotifier.connect(this, &MapWindow::preferenceDidChange);

  auto& resourceManager = m_appController.glManager().resourceManager();
  m_notifierConnection += resourceManager.resourcesWereProcessedNotifier.connect(
    this, &MapWindow::resourcesWereProcessed);

  m_notifierConnection +=
    m_document->documentWasLoadedNotifier.connect(this, &MapWindow::documentWasLoaded);
  m_notifierConnection +=
    m_document->documentWasSavedNotifier.connect(this, &MapWindow::documentWasSaved);

  m_notifierConnection +=
    m_document->pointFileWasLoadedNotifier.connect(this, &MapWindow::pointFileDidChange);
  m_notifierConnection += m_document->pointFileWasUnloadedNotifier.connect(
    this, &MapWindow::pointFileDidChange);
  m_notifierConnection += m_document->portalFileWasLoadedNotifier.connect(
    this, &MapWindow::portalFileDidChange);
  m_notifierConnection += m_document->portalFileWasUnloadedNotifier.connect(
    this, &MapWindow::portalFileDidChange);

  m_notifierConnection += m_document->modificationStateDidChangeNotifier.connect(
    this, &MapWindow::mapModificationStateDidChange);
  m_notifierConnection +=
    m_document->selectionDidChangeNotifier.connect(this, &MapWindow::selectionDidChange);
  m_notifierConnection +=
    m_document->nodesWillBeRemovedNotifier.connect([](const auto& nodes) {
      const auto removedNodes = mdl::collectNodesAndDescendants(nodes);
      auto& registry = PythonHandleRegistry::instance();
      registry.invalidateNodeLifetimes(removedNodes);
    });
  m_notifierConnection += m_document->triggerVisualEffectNotifier.connect(
    this, &MapWindow::triggerVisualEffect);

  m_notifierConnection +=
    m_document->transactionDoneNotifier.connect(this, &MapWindow::transactionDone);
  m_notifierConnection +=
    m_document->transactionUndoneNotifier.connect(this, &MapWindow::transactionUndone);

  m_notifierConnection +=
    m_document->gridDidChangeNotifier.connect(this, &MapWindow::gridDidChange);

  m_notifierConnection +=
    m_mapView->toolBox().toolActivatedNotifier.connect(this, &MapWindow::toolActivated);
  m_notifierConnection += m_mapView->toolBox().baseToolDidChangeNotifier.connect(
    this, &MapWindow::updateActionState);
  m_notifierConnection += m_mapView->toolBox().toolDeactivatedNotifier.connect(
    this, &MapWindow::toolDeactivated);
  m_notifierConnection += m_mapView->toolBox().toolHandleSelectionChangedNotifier.connect(
    this, &MapWindow::toolHandleSelectionChanged);
}

void MapWindow::documentWasLoaded()
{
  updateTitle();
  updateActionState();
  updateUndoRedoActions();
  updateRecentDocumentsMenu();
  loadLastCompilationProfileName();
  PythonRuntime::instance().emitEvent("document_loaded", *this, false);
}

void MapWindow::showCommandPalette()
{
  auto selectedActionPath = std::optional<std::filesystem::path>{};
  {
    auto context = ActionExecutionContext{m_appController, this, currentMapViewBase()};
    auto dialog = CommandPaletteDialog{
      m_appController.actionManager(),
      context,
      std::filesystem::path{"Menu/View/Command Palette..."},
      this};

    if (dialog.exec() == QDialog::Accepted)
    {
      selectedActionPath = dialog.selectedActionPath();
    }
  }

  if (!selectedActionPath)
  {
    return;
  }

  const auto& actionsMap = m_appController.actionManager().actionsMap();
  if (const auto it = actionsMap.find(*selectedActionPath); it != std::end(actionsMap))
  {
    auto executeContext =
      ActionExecutionContext{m_appController, this, currentMapViewBase()};
    it->second.execute(executeContext);
  }
}

void MapWindow::documentWasSaved()
{
  updateTitle();
  updateActionState();
  updateUndoRedoActions();
  updateRecentDocumentsMenu();
  PythonRuntime::instance().emitEvent("document_saved", *this, false);
}

void MapWindow::mapModificationStateDidChange()
{
  updateTitleDelayed();
}

void MapWindow::transactionDone(const std::string&, const bool, const bool)
{
  QTimer::singleShot(0, this, [this]() {
    // FIXME: Delaying this with QTimer::singleShot is a hack to work around the lack of
    // a notification that's called _after_ the CommandProcessor undo/redo stacks are
    // modified.
    //
    // The current transactionDoneNotifier is called after the transaction executes, but
    // before it's pushed onto the undo stack, but we need to read the undo stack in
    // updateUndoRedoActions(), so this QTimer::singleShot is needed for now.
    updateUndoRedoActions();
  });
}

void MapWindow::transactionUndone(const std::string&, const bool, const bool)
{
  QTimer::singleShot(0, this, [this]() {
    // FIXME: see MapWindow::transactionDone
    updateUndoRedoActions();
  });
}

void MapWindow::preferenceDidChange(const std::filesystem::path& path)
{
  if (path == Preferences::MapViewLayout.path)
  {
    m_mapView->switchToMapView(
      static_cast<MapViewLayout>(pref(Preferences::MapViewLayout)));
  }
  else if (path == Preferences::PythonPluginDirectories.path)
  {
    reloadPythonPlugins();
  }

  updateShortcuts();
  updateActionStateDelayed();
}

void MapWindow::resourcesWereProcessed(const std::vector<gl::ResourceId>&)
{
  updateActionState();
}

void MapWindow::gridDidChange()
{
  updateActionStateDelayed();
}

void MapWindow::toolActivated(Tool&)
{
  updateActionStateDelayed();
}

void MapWindow::toolDeactivated(Tool&)
{
  updateActionStateDelayed();
}

void MapWindow::toolHandleSelectionChanged(Tool&)
{
  updateActionStateDelayed();
}

void MapWindow::selectionDidChange(const mdl::SelectionChange&)
{
  updateActionStateDelayed();
  PythonRuntime::instance().emitEvent("selection_changed", *this, false);
}

void MapWindow::triggerVisualEffect(const mdl::VisualEffect visualEffect)
{
  switch (visualEffect)
  {
  case mdl::VisualEffect::FlashSelection:
    m_mapView->flashSelection();
    break;
  }
}

void MapWindow::pointFileDidChange()
{
  updateActionStateDelayed();
}

void MapWindow::portalFileDidChange()
{
  updateActionStateDelayed();
}

void MapWindow::bindEvents()
{
  connect(m_autosaveTimer, &QTimer::timeout, this, &MapWindow::triggerAutosave);
  connect(qApp, &QApplication::focusChanged, this, &MapWindow::focusChange);
  connect(QApplication::clipboard(), &QClipboard::dataChanged, this, [this]() {
    // update the "Paste" menu items
    this->updateActionState();
  });
  if (m_toolBar)
    connect(
      m_toolBar, &QToolBar::visibilityChanged, this, [this](const bool /* visible */) {
        // update the "Toggle Toolbar" menu item
        this->updateActionState();
      });

  connect(
    m_updateTitleSignalDelayer,
    &SignalDelayer::processSignal,
    this,
    &MapWindow::updateTitle);
  connect(
    m_updateActionStateSignalDelayer,
    &SignalDelayer::processSignal,
    this,
    &MapWindow::updateActionState);
}

bool MapWindow::saveDocument()
{
  auto& map = m_document->map();

  if (map.persistent())
  {
    const auto startTime = std::chrono::high_resolution_clock::now();
    return map.save() | kdl::transform([&]() {
             const auto endTime = std::chrono::high_resolution_clock::now();

             logger().info() << "Saved " << map.path() << " in "
                             << std::chrono::duration_cast<std::chrono::milliseconds>(
                                  endTime - startTime)
                                  .count()
                             << "ms";
           })
           | kdl::transform_error([&](const auto& e) {
               QMessageBox::critical(
                 this,
                 "",
                 QString::fromStdString(
                   fmt::format("Error while saving {}: {}", map.path(), e.msg)),
                 QMessageBox::Ok);
             })
           | kdl::is_success();
  }
  return saveDocumentAs();
}

bool MapWindow::saveDocumentAs()
{
  if (m_project)
  {
    const auto path = QFileDialog::getSaveFileName(
      this,
      tr("Save Project Copy"),
      pathAsQString(m_project->path()),
      "TB projects (*.tbproj)");
    if (path.isEmpty())
      return false;
    const auto result = m_project->saveCopy(pathFromQString(path));
    if (result.is_error())
      QMessageBox::critical(
        this,
        tr("Save Project Copy"),
        QString::fromStdString(std::get<Error>(result.error()).msg));
    return result.is_success();
  }
  auto& map = m_document->map();

  const auto defaultPath = map.persistent()
                             ? map.path()
                             : m_appController.environmentConfig().userDataFolderPath;

  const auto newFileName = QFileDialog::getSaveFileName(
    this, tr("Save map file"), pathAsQPath(defaultPath), "Map files (*.map)");
  if (newFileName.isEmpty())
  {
    return false;
  }

  const auto path = pathFromQString(newFileName);

  const auto startTime = std::chrono::high_resolution_clock::now();
  return map.saveAs(path) | kdl::transform([&]() {
           const auto endTime = std::chrono::high_resolution_clock::now();

           logger().info() << "Saved " << map.path() << " in "
                           << std::chrono::duration_cast<std::chrono::milliseconds>(
                                endTime - startTime)
                                .count()
                           << "ms";
         })
         | kdl::transform_error([&](const auto& e) {
             QMessageBox::critical(
               this,
               "",
               QString::fromStdString(
                 fmt::format("Error while saving {}: ", path, e.msg)),
               QMessageBox::Ok);
           })
         | kdl::is_success();
}

void MapWindow::revertDocument()
{
  auto& map = m_document->map();
  if (map.persistent() && confirmRevertDocument())
  {
    if (m_project)
    {
      const auto id = m_project->assetFor(*m_document);
      if (!id)
        return;
      const auto prepared = m_project->prepareMap(*id);
      if (prepared.is_error())
      {
        logger().error() << "Failed to revert project map: "
                         << std::get<Error>(prepared.error()).msg;
        return;
      }
    }
    m_document->reload() | kdl::transform_error([&](auto e) {
      logger().error() << "Failed to revert document: " << e.msg;
    });
  }
}

bool MapWindow::exportDocumentAsObj()
{
  if (!m_objExportDialog)
  {
    m_objExportDialog = new ObjExportDialog{this};
  }

  m_objExportDialog->updateExportPath();
  showModelessDialog(m_objExportDialog);
  return true;
}

bool MapWindow::exportDocumentAsMap()
{
  const auto& map = m_document->map();
  const auto& originalPath = map.path();

  const auto newFileName = QFileDialog::getSaveFileName(
    this, tr("Export Map file"), pathAsQPath(originalPath), "Map files (*.map)");
  if (newFileName.isEmpty())
  {
    return false;
  }

  const auto options = mdl::MapExportOptions{
    pathFromQString(newFileName),
    !K(stripTbProperties),
    std::nullopt,
    std::nullopt,
  };
  return exportDocument(options);
}

bool MapWindow::exportDocument(const mdl::ExportOptions& options)
{
  const auto& map = m_document->map();
  const auto exportPath = std::visit([](const auto& o) { return o.exportPath; }, options);

  if (exportPath == map.path())
  {
    QMessageBox::critical(
      this,
      "",
      tr("You can't overwrite the current document.\nPlease choose a different file name "
         "to export "
         "to."));
    return false;
  }

  return map.exportAs(options) | kdl::transform([&]() {
           logger().info() << "Exported " << exportPath;
           return true;
         })
         | kdl::transform_error([&](auto e) {
             logger().error() << "Could not export '" << exportPath << "': " + e.msg;
             QMessageBox::critical(this, "", QString::fromStdString(e.msg));
             return false;
           })
         | kdl::value();
}

/**
 * Returns whether the window should close.
 */
bool MapWindow::confirmOrDiscardChanges()
{
  const auto& map = m_document->map();
  if (!map.modified())
  {
    return true;
  }

  const auto result = QMessageBox::question(
    this,
    "TrenchBroom",
    m_project ? tr("%1 has been modified. Save the entire project, including changes in "
                   "other open maps?")
                  .arg(pathAsQString(map.filename()))
              : tr("%1 has been modified. Do you want to save the changes?")
                  .arg(pathAsQString(map.filename())),
    QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);

  if (result == QMessageBox::Yes)
  {
    return saveDocument();
  }
  return result == QMessageBox::No;
}

/**
 * Returns whether the document should be reverted.
 */
bool MapWindow::confirmRevertDocument()
{
  const auto& map = m_document->map();
  if (!map.modified())
  {
    return true;
  }

  auto messageBox = QMessageBox{this};
  messageBox.setWindowTitle("TrenchBroom");
  messageBox.setIcon(QMessageBox::Question);
  messageBox.setText(tr("Revert %1 to %2?")
                       .arg(pathAsQString(map.filename()))
                       .arg(pathAsQString(map.path())));
  messageBox.setInformativeText(
    tr("This will discard all unsaved changes and reload the document from disk."));

  auto* revertButton = messageBox.addButton(tr("Revert"), QMessageBox::DestructiveRole);
  auto* cancelButton = messageBox.addButton(QMessageBox::Cancel);
  messageBox.setDefaultButton(cancelButton);

  messageBox.exec();

  return messageBox.clickedButton() == revertButton;
}

void MapWindow::loadPointFile()
{
  const auto& map = m_document->map();
  const auto path = map.path();
  const auto defaultDir = !path.empty() ? pathAsQPath(path.parent_path()) : QString{};

  const auto fileName = QFileDialog::getOpenFileName(
    this,
    tr("Load Point File"),
    defaultDir,
    "Point files (*.pts *.lin);;Any files (*.*)");

  if (!fileName.isEmpty())
  {
    m_document->loadPointFile(pathFromQString(fileName));
  }
}

void MapWindow::reloadPointFile()
{
  if (canReloadPointFile())
  {
    m_document->reloadPointFile();
  }
}

void MapWindow::unloadPointFile()
{
  if (canUnloadPointFile())
  {
    m_document->unloadPointFile();
  }
}

bool MapWindow::canUnloadPointFile() const
{
  return m_document->isPointFileLoaded();
}

bool MapWindow::canReloadPointFile() const
{
  return m_document->canReloadPointFile();
}

void MapWindow::loadPortalFile()
{
  const auto& map = m_document->map();
  const auto path = map.path();
  const auto defaultDir = !path.empty() ? pathAsQPath(path.parent_path()) : QString{};

  const auto fileName = QFileDialog::getOpenFileName(
    this, tr("Load Portal File"), defaultDir, "Portal files (*.prt);;Any files (*.*)");

  if (!fileName.isEmpty())
  {
    m_document->loadPortalFile(pathFromQString(fileName));
  }
}

void MapWindow::reloadPortalFile()
{
  if (canReloadPortalFile())
  {
    m_document->reloadPortalFile();
  }
}

void MapWindow::unloadPortalFile()
{
  if (canUnloadPortalFile())
  {
    m_document->unloadPortalFile();
  }
}

bool MapWindow::canUnloadPortalFile() const
{
  return m_document->isPortalFileLoaded();
}

bool MapWindow::canReloadPortalFile() const
{
  return m_document->canReloadPortalFile();
}

void MapWindow::reloadMaterialCollections()
{
  mdl::reloadMaterialCollections(m_document->map());
}

void MapWindow::reloadEntityDefinitions()
{
  mdl::reloadEntityDefinitions(m_document->map());
}

bool MapWindow::canReloadMaterialCollections() const
{
  return !m_appController.glManager().resourceManager().needsProcessing();
}

bool MapWindow::canReloadEntityDefinitions() const
{
  return !m_appController.glManager().resourceManager().needsProcessing();
}

void MapWindow::refreshMapViews()
{
  if (m_mapView)
  {
    m_mapView->refreshViews();
  }
}

MapViewLayout MapWindow::currentMapViewLayout() const
{
  return m_mapView->currentMapViewLayout();
}

void MapWindow::switchMapViewLayout(const MapViewLayout layout)
{
  m_mapView->switchToMapView(layout);
  m_currentMapView = m_mapView->firstMapViewBase();
  refreshMapViews();
}

void MapWindow::closeDocument(const bool discardChanges)
{
  if (m_project && documentRetainedByProject())
  {
    if (!closeCompileDialog())
      return;
    m_appController.mapWindowManager().closeMapView(*this);
    return;
  }
  m_discardChangesOnClose = discardChanges;
  close();
}

void MapWindow::undo()
{
  m_editor->undo();
}

void MapWindow::redo()
{
  m_editor->redo();
}

bool MapWindow::canUndo() const
{
  return m_editor->canUndo();
}

bool MapWindow::canRedo() const
{
  return m_editor->canRedo();
}

void MapWindow::repeatLastCommands()
{
  m_editor->repeatLastCommands();
}

void MapWindow::clearRepeatableCommands()
{
  m_editor->clearRepeatableCommands();
}

bool MapWindow::hasRepeatableCommands() const
{
  return m_editor->hasRepeatableCommands();
}

void MapWindow::cutSelection()
{
  m_editor->controller().cutSelection();
}

void MapWindow::copySelection()
{
  m_editor->controller().copySelection();
}

void MapWindow::copyToClipboard()
{
  m_editor->controller().copyToClipboard();
}

bool MapWindow::canCutSelection() const
{
  return m_editor->controller().canCutSelection();
}

bool MapWindow::canCopySelection() const
{
  return m_editor->controller().canCopySelection();
}

void MapWindow::pasteAtCursorPosition()
{
  m_editor->controller().pasteAtCursorPosition();
}

void MapWindow::pasteAtOriginalPosition()
{
  m_editor->controller().pasteAtOriginalPosition();
}

mdl::PasteType MapWindow::paste()
{
  return m_editor->controller().paste();
}

bool MapWindow::canPaste() const
{
  return m_editor->controller().canPaste();
}

void MapWindow::duplicateSelection()
{
  m_editor->controller().duplicateSelection();
}

bool MapWindow::canDuplicateSelection() const
{
  return m_editor->controller().canDuplicateSelection();
}

void MapWindow::deleteSelection()
{
  m_editor->controller().deleteSelection();
}

bool MapWindow::canDeleteSelection() const
{
  return m_editor->controller().canDeleteSelection();
}

void MapWindow::selectAll()
{
  m_editor->controller().selectAll();
}

void MapWindow::selectSiblings()
{
  m_editor->controller().selectSiblings();
}

void MapWindow::selectTouching()
{
  m_editor->controller().selectTouching();
}

void MapWindow::selectInside()
{
  m_editor->controller().selectInside();
}

void MapWindow::selectTall()
{
  m_editor->controller().selectTall();
}

void MapWindow::selectByLineNumber()
{
  m_editor->controller().selectByLineNumber();
}

void MapWindow::selectInverse()
{
  m_editor->controller().selectInverse();
}

void MapWindow::selectNone()
{
  m_editor->controller().selectNone();
}

bool MapWindow::canSelect() const
{
  return m_editor->controller().canSelect();
}

bool MapWindow::canSelectSiblings() const
{
  return m_editor->controller().canSelectSiblings();
}

bool MapWindow::canSelectByBrush() const
{
  return m_editor->controller().canSelectByBrush();
}

bool MapWindow::canSelectTall() const
{
  return m_editor->controller().canSelectTall();
}

bool MapWindow::canDeselect() const
{
  return m_editor->controller().canDeselect();
}

bool MapWindow::canChangeSelection() const
{
  return m_editor->controller().canChangeSelection();
}

bool MapWindow::canSelectInverse() const
{
  return m_editor->controller().canSelectInverse();
}

void MapWindow::groupSelectedObjects()
{
  m_editor->controller().groupSelectedObjects();
}

bool MapWindow::canGroupSelectedObjects() const
{
  return m_editor->controller().canGroupSelectedObjects();
}

void MapWindow::ungroupSelectedObjects()
{
  m_editor->controller().ungroupSelectedObjects();
}

bool MapWindow::canUngroupSelectedObjects() const
{
  return m_editor->controller().canUngroupSelectedObjects();
}

void MapWindow::renameSelectedGroups()
{
  m_editor->controller().renameSelectedGroups();
}

bool MapWindow::canRenameSelectedGroups() const
{
  return m_editor->controller().canRenameSelectedGroups();
}

void MapWindow::replaceMaterial()
{
  m_editor->controller().replaceMaterial();
}

void MapWindow::moveSelectedObjects()
{
  m_editor->controller().moveSelectedObjects();
}

bool MapWindow::canMoveSelectedObjects() const
{
  return m_editor->controller().canMoveSelectedObjects();
}

void MapWindow::togglePathTool()
{
  m_editor->controller().togglePathTool();
}

bool MapWindow::canTogglePathTool() const
{
  return m_editor->controller().canTogglePathTool();
}

bool MapWindow::pathToolActive() const
{
  return m_editor->controller().pathToolActive();
}

void MapWindow::performPathCreation()
{
  m_editor->controller().performPathCreation();
}

void MapWindow::removeLastPathPoint()
{
  m_editor->controller().removeLastPathPoint();
}

void MapWindow::csgConvexMerge()
{
  m_editor->controller().csgConvexMerge();
}

bool MapWindow::canDoCsgConvexMerge() const
{
  return m_editor->controller().canDoCsgConvexMerge();
}

void MapWindow::csgSubtract()
{
  m_editor->controller().csgSubtract();
}

bool MapWindow::canDoCsgSubtract() const
{
  return m_editor->controller().canDoCsgSubtract();
}

void MapWindow::csgHollow()
{
  m_editor->controller().csgHollow();
}

bool MapWindow::canDoCsgHollow() const
{
  return m_editor->controller().canDoCsgHollow();
}

void MapWindow::csgIntersect()
{
  m_editor->controller().csgIntersect();
}

bool MapWindow::canDoCsgIntersect() const
{
  return m_editor->controller().canDoCsgIntersect();
}

void MapWindow::snapVerticesToInteger()
{
  m_editor->controller().snapVerticesToInteger();
}

void MapWindow::snapVerticesToGrid()
{
  m_editor->controller().snapVerticesToGrid();
}

bool MapWindow::canSnapVertices() const
{
  return m_editor->controller().canSnapVertices();
}

void MapWindow::convertSelectionToPatches()
{
  m_editor->controller().convertSelectionToPatches();
}

bool MapWindow::canConvertSelectionToPatches() const
{
  return m_editor->controller().canConvertSelectionToPatches();
}

void MapWindow::toggleAlignmentLock()
{
  m_editor->controller().toggleAlignmentLock();
}

void MapWindow::toggleUvLock()
{
  m_editor->controller().toggleUvLock();
}

void MapWindow::toggleShowGrid()
{
  m_editor->controller().toggleShowGrid();
}

void MapWindow::toggleSnapToGrid()
{
  m_editor->controller().toggleSnapToGrid();
}

void MapWindow::incGridSize()
{
  m_editor->controller().incGridSize();
}

bool MapWindow::canIncGridSize() const
{
  return m_editor->controller().canIncGridSize();
}

void MapWindow::decGridSize()
{
  m_editor->controller().decGridSize();
}

bool MapWindow::canDecGridSize() const
{
  return m_editor->controller().canDecGridSize();
}

void MapWindow::setGridSize(const int size)
{
  m_editor->controller().setGridSize(size);
}

void MapWindow::moveCameraToNextPoint()
{
  m_editor->moveCameraToNextTracePoint();
}

bool MapWindow::canMoveCameraToNextPoint() const
{
  return m_editor->canMoveCameraToNextTracePoint();
}

void MapWindow::moveCameraToPreviousPoint()
{
  m_editor->moveCameraToPreviousTracePoint();
}

bool MapWindow::canMoveCameraToPreviousPoint() const
{
  return m_editor->canMoveCameraToPreviousTracePoint();
}

void MapWindow::reset2dCameras()
{
  m_editor->reset2dCameras(*currentMapViewBase());
}

void MapWindow::focusCameraOnSelection()
{
  focusCameraOnSelection(true);
}

void MapWindow::focusCameraOnSelection(const bool animate)
{
  m_editor->focusCameraOnSelection(animate);
}

bool MapWindow::canFocusCamera() const
{
  return m_editor->canFocusCamera();
}

void MapWindow::moveCameraToPosition()
{
  auto ok = false;
  const auto str = QInputDialog::getText(
    this,
    "Move Camera",
    "Enter a position (x y z) for the camera.",
    QLineEdit::Normal,
    toString(vm::vec3d{}),
    &ok);
  if (ok)
  {
    if (const auto position = parse<float, 3>(str))
    {
      m_mapView->moveCameraToPosition(*position, true);
    }
  }
}

void MapWindow::isolateSelection()
{
  m_editor->controller().isolateSelection();
}

bool MapWindow::canIsolateSelection() const
{
  return m_editor->controller().canIsolateSelection();
}

void MapWindow::hideSelection()
{
  m_editor->controller().hideSelection();
}

bool MapWindow::canHideSelection() const
{
  return m_editor->controller().canHideSelection();
}

void MapWindow::showAll()
{
  m_editor->controller().showAll();
}

void MapWindow::switchToInspectorPage(const InspectorPage page)
{
  m_editor->showInspectorPage(page);
}

void MapWindow::switchToInfoPanelPage(const InfoPanelPage page)
{
  m_editor->showInfoPanelPage(page);
}

void MapWindow::toggleToolbar()
{
  if (m_toolBar)
    m_toolBar->setVisible(!m_toolBar->isVisible());
}

bool MapWindow::toolbarVisible() const
{
  return m_toolBar && m_toolBar->isVisible();
}

void MapWindow::toggleInfoPanel()
{
  m_editor->toggleInfoPanel();
}

bool MapWindow::infoPanelVisible() const
{
  return m_editor->isInfoPanelVisible();
}

void MapWindow::toggleInspector()
{
  m_editor->toggleInspector();
}

bool MapWindow::inspectorVisible() const
{
  return m_editor->isInspectorVisible();
}

void MapWindow::toggleMaximizeCurrentView()
{
  m_editor->toggleMaximizeCurrentView();
}

bool MapWindow::currentViewMaximized() const
{
  return m_editor->currentViewMaximized();
}

void MapWindow::showCompileDialog()
{
  if (!m_compilationDialog)
  {
    const auto& camera = m_mapView->perspectiveCamera();

    m_compilationDialog =
      new CompilationDialog{m_appController, *m_document, camera, this};
    connect(
      m_compilationDialog,
      &CompilationDialog::compilationProfileStarted,
      this,
      [this](const std::string& profileName) {
        setLastCompilationProfileName(profileName);
      });
  }

  if (const auto* profile = lastCompilationProfile())
  {
    m_compilationDialog->selectProfile(*profile);
  }
  else
  {
    m_compilationDialog->selectFirstProfile();
  }

  showModelessDialog(m_compilationDialog);
}

void MapWindow::rerunLastCompilation()
{
  if (m_lastCompilationProfileName)
  {
    showCompileDialog();

    if (const auto* profile = lastCompilationProfile())
    {
      m_compilationDialog->selectProfile(*profile);
      m_compilationDialog->runSelectedProfile();
    }
  }
}

bool MapWindow::runCompilationProfile(const std::string& profileName)
{
  showCompileDialog();
  if (!m_compilationDialog)
  {
    return false;
  }
  return m_compilationDialog->runProfileByName(profileName);
}

bool MapWindow::compilationRunning() const
{
  return m_compilationDialog && m_compilationDialog->isRunning();
}

QString MapWindow::compilationOutputText() const
{
  return m_compilationDialog ? m_compilationDialog->outputText() : QString{};
}

bool MapWindow::hasLastCompilationProfile() const
{
  return lastCompilationProfile() != nullptr;
}

const mdl::CompilationProfile* MapWindow::lastCompilationProfile() const
{
  if (m_lastCompilationProfileName)
  {
    const auto& compilationProfiles =
      m_document->map().gameInfo().compilationConfig.profiles;

    if (const auto iLastCompilationProfile = std::ranges::find_if(
          compilationProfiles,
          [&](const auto& profile) {
            return profile.name == *m_lastCompilationProfileName;
          });
        iLastCompilationProfile != compilationProfiles.end())
    {
      return &*iLastCompilationProfile;
    }
  }

  return nullptr;
}

void MapWindow::setLastCompilationProfileName(std::string name)
{
  m_lastCompilationProfileName = std::move(name);

  const auto key =
    lastCompilationProfileSettingsKey(m_document->map().gameInfo().gameConfig.name);

  auto settings = QSettings{};
  settings.setValue(key, QString::fromStdString(*m_lastCompilationProfileName));

  updateRerunAction();
}

void MapWindow::loadLastCompilationProfileName()
{
  const auto key =
    lastCompilationProfileSettingsKey(m_document->map().gameInfo().gameConfig.name);

  const auto settings = QSettings{};
  const auto value = settings.value(key);

  m_lastCompilationProfileName =
    value.isValid() ? std::optional{value.toString().toStdString()} : std::nullopt;

  updateRerunAction();
}

void MapWindow::updateRerunAction()
{
  if (m_rerunAction)
  {
    m_rerunAction->setText(
      m_lastCompilationProfileName
        ? tr("Re-run \"%1\"...")
            .arg(QString::fromStdString(*m_lastCompilationProfileName))
        : tr("Re-run compilation..."));
  }

  updateActionState();
}

bool MapWindow::closeCompileDialog()
{
  if (!m_compilationDialog)
  {
    return true;
  }

  if (m_compilationDialog->close())
  {
    m_compilationDialog = nullptr;
    return true;
  }

  return false;
}

void MapWindow::showLaunchEngineDialog()
{
  auto dialog = LaunchGameEngineDialog{m_appController, document(), this};
  dialog.exec();
}

void MapWindow::runPythonScript()
{
  const auto pathStr = QFileDialog::getOpenFileName(
    this, tr("Run Python Script"), QString{}, tr("Python Scripts (*.py);;All Files (*)"));

  if (pathStr.isEmpty())
  {
    return;
  }

  if (!PythonScripting::instance().runScript(*this, pathFromQString(pathStr)))
  {
    QMessageBox::warning(
      this,
      tr("Python Script Failed"),
      tr("The Python script failed. See the Python console for details."));
  }
}

PluginPanelRegistration MapWindow::addPluginPanel(
  const QString& title, const QString& content)
{
  auto registration = m_inspector->pluginInspector()->addPluginPanel(title);
  auto* container = registration.content();
  if (!content.isEmpty())
  {
    auto* layout = new QVBoxLayout{};
    auto* label = new QLabel{content};
    label->setWordWrap(true);
    layout->addWidget(label);
    container->setLayout(layout);
  }
  return registration;
}

MapView3D* MapWindow::playModeView()
{
  // Prefer the view the user is working in, otherwise the first visible 3D view.
  if (auto* view3D = dynamic_cast<MapView3D*>(currentMapViewBase());
      view3D && view3D->isVisible())
  {
    return view3D;
  }
  for (auto* view3D : findChildren<MapView3D*>())
  {
    if (view3D->isVisible())
    {
      return view3D;
    }
  }
  return nullptr;
}

void MapWindow::startPlayMode(const bool fromCamera)
{
  if (auto* view3D = playModeView())
  {
    view3D->startPlayMode(fromCamera);
  }
}

namespace
{

const gl::Material* materialToReveal(const mdl::Map& map)
{
  const auto& selection = map.selection();

  const auto& allBrushFaces = selection.allBrushFaces();
  const auto* firstMaterial = allBrushFaces.front().face().material();
  const auto allFacesHaveIdenticalMaterial = std::ranges::all_of(
    allBrushFaces,
    [&](const auto& face) { return face.face().material() == firstMaterial; });

  return allFacesHaveIdenticalMaterial ? firstMaterial : nullptr;
}

} // namespace

bool MapWindow::canRevealMaterial() const
{
  return materialToReveal(m_document->map()) != nullptr;
}

void MapWindow::revealMaterial()
{
  if (const auto* material = materialToReveal(m_document->map()))
  {
    revealMaterial(material);
  }
}

void MapWindow::revealMaterial(const gl::Material* material)
{
  m_inspector->switchToPage(InspectorPage::Face);
  m_inspector->faceInspector()->revealMaterial(material);
}

AppController& MapWindow::appController()
{
  return m_appController;
}

void MapWindow::debugPrintVertices()
{
  const auto& selection = m_document->map().selection();
  if (selection.hasBrushFaces())
  {
    for (const auto& handle : selection.brushFaces)
    {
      auto str = std::stringstream{};
      str.precision(17);
      for (const auto* vertex : handle.face().vertices())
      {
        str << "(" << vertex->position() << ") ";
      }
      logger().info() << str.str();
    }
  }
  else if (selection.hasBrushes())
  {
    for (const auto* brushNode : selection.brushes)
    {
      const auto& brush = brushNode->brush();

      auto str = std::stringstream{};
      str.precision(17);
      for (const auto* vertex : brush.vertices())
      {
        str << vertex->position() << " ";
      }
      logger().info() << str.str();
    }
  }
}

void MapWindow::debugCreateBrush()
{
  auto ok = false;
  const auto str = QInputDialog::getText(
    this,
    "Create Brush",
    "Enter a list of at least 4 points (x y z) (x y z) ...",
    QLineEdit::Normal,
    "",
    &ok);
  if (ok)
  {
    auto positions = std::vector<vm::vec3d>{};
    vm::parse_all<double, 3>(str.toStdString(), std::back_inserter(positions));

    createBrush(m_document->map(), positions);
  }
}

void MapWindow::debugCreateCube()
{
  auto ok = false;
  const auto str = QInputDialog::getText(
    this, "Create Cube", "Enter bounding box size", QLineEdit::Normal, "", &ok);
  if (ok)
  {
    const auto size = str.toDouble();
    const auto bounds = vm::bbox3d{size / 2.0};
    const auto positions = bounds.vertices() | kdl::ranges::to<std::vector>();

    createBrush(m_document->map(), positions);
  }
}

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wcast-qual"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif
static void debugSegfault()
{
  volatile void* test = nullptr;
  printf("%p\n", *((void**)test));
}
#ifdef __clang__
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

[[noreturn]] static void debugException()
{
  const auto e = std::runtime_error{"Let's crash!"};
  throw e;
}

void MapWindow::debugCrash()
{
  auto items = QStringList{};
  items << "Null pointer dereference"
        << "Unhandled exception"
        << "Contract failed";

  bool ok;
  const auto item =
    QInputDialog::getItem(this, "Crash", "Choose a crash type", items, 0, false, &ok);
  if (ok)
  {
    const auto idx = items.indexOf(item);
    if (idx == 0)
    {
      debugSegfault();
    }
    else if (idx == 1)
    {
      debugException();
    }
    else if (idx == 2)
    {
      contract_assert(false);
    }
  }
}

void MapWindow::debugThrowExceptionDuringCommand()
{
  auto& map = m_document->map();
  map.throwExceptionDuringCommand();
}

void MapWindow::debugSetWindowSize()
{
  auto ok = false;
  const auto str = QInputDialog::getText(
    this, "Window Size", "Enter Size (W H)", QLineEdit::Normal, "1920 1080", &ok);
  if (ok)
  {
    if (const auto size = parse<int, 2>(str))
    {
      resize(size->x(), size->y());
    }
  }
}

void MapWindow::debugShowPalette()
{
  auto* window = new DebugPaletteWindow{this};
  showModelessDialog(window);
}

void MapWindow::focusChange(QWidget* /* oldFocus */, QWidget* newFocus)
{
  if (auto* newMapView = dynamic_cast<MapViewBase*>(newFocus);
      newMapView && isAncestorOf(newMapView))
  {
    m_currentMapView = newMapView;
  }

  updateActionState();
  updateUndoRedoActions();
}

MapView3D* MapWindow::currentOrFirstVisible3DMapView()
{
  if (auto* current3D = dynamic_cast<MapView3D*>(currentMapViewBase());
      current3D && current3D->isVisible())
  {
    return current3D;
  }

  const auto views = findChildren<MapView3D*>();
  if (auto it = std::ranges::find_if(views, [](auto* view) { return view->isVisible(); });
      it != std::end(views))
  {
    return *it;
  }

  return nullptr;
}

MapView3D& MapWindow::activate3DMapView()
{
  const auto views = m_mapView->findChildren<MapView3D*>();
  if (views.empty())
  {
    throw std::runtime_error{"No 3D viewport is available"};
  }
  auto* view = views.front();
  if (auto* cycling = qobject_cast<CyclingMapView*>(view->parentWidget()))
  {
    cycling->switchToMapView(view);
  }
  m_currentMapView = view;
  view->setFocus();
  return *view;
}

MapViewBase* MapWindow::currentMapViewBase()
{
  if (!m_currentMapView)
  {
    // This happens when the current map view is deleted (e.g. 4-pane to 1-pane layout)
    m_currentMapView = m_mapView->firstMapViewBase();

    // SwitchableMapViewContainer should have constructed a MapViewBase
    contract_assert(m_currentMapView != nullptr);
  }
  return m_currentMapView;
}

bool MapWindow::canCompile() const
{
  const auto& map = m_document->map();
  return map.persistent();
}

bool MapWindow::canLaunch() const
{
  const auto& map = m_document->map();
  return map.persistent();
}

void MapWindow::dragEnterEvent(QDragEnterEvent* event)
{
  const auto& map = m_document->map();
  if (
    map.gameInfo().gameConfig.materialConfig.property && event->mimeData()->hasUrls()
    && std::ranges::all_of(event->mimeData()->urls(), [](const auto& url) {
         if (!url.isLocalFile())
         {
           return false;
         }

         const auto fileInfo = QFileInfo{url.toLocalFile()};
         return fileInfo.isFile() && fileInfo.fileName().toLower().endsWith(".wad");
       }))
  {
    event->accept();
  }
}

void MapWindow::dropEvent(QDropEvent* event)
{
  const auto urls = event->mimeData()->urls();
  if (urls.empty())
  {
    return;
  }

  auto pathQStrs = QStringList{};
  pathQStrs.reserve(urls.size());
  for (const auto& url : urls)
  {
    pathQStrs.push_back(url.toLocalFile());
  }

  if (addWadPaths(pathQStrs, m_document->map(), this))
  {
    event->acceptProposedAction();
  }
}

void MapWindow::changeEvent(QEvent*)
{
  if (m_mapView)
  {
    m_mapView->windowActivationStateChanged(isActiveWindow());
  }
}

void MapWindow::closeEvent(QCloseEvent* event)
{
  for (auto* view : findChildren<MapView3D*>())
  {
    view->stopPlayMode();
  }

  if (!closeCompileDialog() || (!m_discardChangesOnClose && !confirmOrDiscardChanges()))
  {
    m_discardChangesOnClose = false;
    event->ignore();
    return;
  }

  disconnect(qApp, &QApplication::focusChanged, this, &MapWindow::focusChange);

  if (!m_embedded)
  {
    saveWidgetGeometry(this);
    saveWidgetState(this);
  }
  m_editor->saveWidgetState();

  if (m_project && !documentRetainedByProject())
    m_project->unregisterDocument(*m_document);
  m_appController.mapWindowManager().removeMapWindow(this);
  event->accept();
  if (m_embedded)
    deleteLater();
  m_discardChangesOnClose = false;
  // Don't call superclass implementation
}

template <typename F>
static void applyRecursively(QObject* object, const F& f)
{
  f(object);
  for (auto* child : object->children())
  {
    applyRecursively(child, f);
  }
}

bool MapWindow::eventFilter(QObject* target, QEvent* event)
{
  if (
    event->type() == QEvent::MouseButtonPress
    || event->type() == QEvent::MouseButtonRelease
    || event->type() == QEvent::MouseButtonDblClick || event->type() == QEvent::MouseMove
    || event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)
  {
    m_lastInputTime = std::chrono::system_clock::now();
  }
  else if (event->type() == QEvent::ChildAdded)
  {
    auto* childEvent = static_cast<QChildEvent*>(event);
    applyRecursively(
      childEvent->child(), [&](auto* object) { object->installEventFilter(this); });
  }
  else if (event->type() == QEvent::ChildRemoved)
  {
    auto* childEvent = static_cast<QChildEvent*>(event);
    applyRecursively(
      childEvent->child(), [&](auto* object) { object->removeEventFilter(this); });
  }
  return QMainWindow::eventFilter(target, event);
}

void MapWindow::triggerAutosave()
{
  using namespace std::chrono_literals;
  if (
    QGuiApplication::mouseButtons() == Qt::NoButton
    && std::chrono::system_clock::now() - m_lastInputTime > 2s)
  {
    m_document->triggerAutosave();
  }
}

// DebugPaletteWindow

DebugPaletteWindow::DebugPaletteWindow(QWidget* parent)
  : QDialog(parent)
{
  setWindowTitle(tr("Palette"));

  const auto roles = std::vector<std::pair<QPalette::ColorRole, QString>>{
    {QPalette::Window, "Window"},
    {QPalette::WindowText, "WindowText"},
    {QPalette::Base, "Base"},
    {QPalette::AlternateBase, "AlternateBase"},
    {QPalette::ToolTipBase, "ToolTipBase"},
    {QPalette::ToolTipText, "ToolTipText"},
    {QPalette::PlaceholderText, "PlaceholderText"},
    {QPalette::Text, "Text"},
    {QPalette::Button, "Button"},
    {QPalette::ButtonText, "ButtonText"},
    {QPalette::BrightText, "BrightText"},
    {QPalette::Light, "Light"},
    {QPalette::Midlight, "Midlight"},
    {QPalette::Dark, "Dark"},
    {QPalette::Mid, "Mid"},
    {QPalette::Shadow, "Shadow"},
    {QPalette::Highlight, "Highlight"},
    {QPalette::HighlightedText, "HighlightedText"}};

  const auto groups = std::vector<std::pair<QPalette::ColorGroup, QString>>{
    {QPalette::Disabled, "Disabled"},
    {QPalette::Active, "Active"},
    {QPalette::Inactive, "Inactive"}};

  auto verticalHeaderLabels = QStringList{};
  for (const auto& [role, roleLabel] : roles)
  {
    verticalHeaderLabels.append(roleLabel);
  }

  auto horizontalHeaderLabels = QStringList{};
  for (const auto& [group, groupLabel] : groups)
  {
    horizontalHeaderLabels.append(groupLabel);
  }

  auto* table = new QTableWidget{int(roles.size()), int(groups.size())};
  table->setHorizontalHeaderLabels(horizontalHeaderLabels);
  table->setVerticalHeaderLabels(verticalHeaderLabels);

  for (int x = 0; x < table->columnCount(); ++x)
  {
    for (int y = 0; y < table->rowCount(); ++y)
    {
      const auto role = roles.at(size_t(y)).first;
      const auto group = groups.at(size_t(x)).first;

      auto* button = new ColorButton{};
      table->setCellWidget(y, x, button);

      button->setColor(qApp->palette().color(group, role));

      connect(button, &ColorButton::colorChangedByUser, this, [=](const QColor& color) {
        auto palette = qApp->palette();
        palette.setColor(group, role, color);
        qApp->setPalette(palette);
      });
    }
  }

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(table);
  setLayout(layout);
}

DebugPaletteWindow::~DebugPaletteWindow() = default;

} // namespace tb::ui
