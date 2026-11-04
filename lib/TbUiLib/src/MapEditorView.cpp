/*
  Copyright (C) 2026 XiangXtreme
 Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom.
*/

#include "ui/MapEditorView.h"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QSettings>
#include <QVBoxLayout>

#include "mdl/Map.h"
#include "ui/Action.h"
#include "ui/ActionBuilder.h"
#include "ui/ActionExecutionContext.h"
#include "ui/ActionManager.h"
#include "ui/AppController.h"
#include "ui/Console.h"
#include "ui/CyclingMapView.h"
#include "ui/InfoPanel.h"
#include "ui/Inspector.h"
#include "ui/MapDocument.h"
#include "ui/MapEditorController.h"
#include "ui/MapView2D.h"
#include "ui/MapView3D.h"
#include "ui/MapViewToolBox.h"
#include "ui/PythonConsole.h"
#include "ui/Splitter.h"
#include "ui/SwitchableMapViewContainer.h"
#include "ui/WorkbenchSurface.h"
#include "ui/outliner/OutlinerEntityPropertyEditor.h"
#include "ui/outliner/OutlinerTreeWidget.h"

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <utility>

namespace tb::ui
{

MapEditorView::MapEditorView(AppController& appController, MapDocument& document)
  : m_appController{appController}
  , m_document{document}
{
  // The workbench shell styling is keyed on this object name, so the view root keeps it.
  setObjectName("MapWindow_WorkbenchShell");
  setAttribute(Qt::WA_StyledBackground);

  m_hSplitter = new Splitter{Qt::Horizontal, DrawKnob::No, this};
  m_hSplitter->setChildrenCollapsible(false);
  m_hSplitter->setHandleWidth(6);
  m_hSplitter->setObjectName("MapWindow_HorizontalSplitter");

  m_vSplitter = new Splitter{Qt::Vertical, DrawKnob::No, this};
  m_vSplitter->setChildrenCollapsible(false);
  m_vSplitter->setHandleWidth(6);
  m_vSplitter->setObjectName("MapWindow_VerticalSplitterSplitter");

  m_infoPanel = new InfoPanel{m_appController, m_document, this};
  m_infoPanel->setObjectName("MapWindow_InfoPanel");

  m_mapView = new SwitchableMapViewContainer{m_appController, m_document, this};
  m_inspector = new Inspector{m_appController, m_document, this};
  m_inspector->setObjectName("Inspector");

  m_mapView->connectTopWidgets(m_inspector);

  m_mapViewSurface = new WorkbenchSurface{
    QStringLiteral("MapWindow_EditorSurface"),
    m_mapView,
    WorkbenchSurface::Kind::Workspace,
    this};
  m_infoPanelSurface = new WorkbenchSurface{
    QStringLiteral("MapWindow_InfoPanelSurface"),
    m_infoPanel,
    WorkbenchSurface::Kind::Panel,
    this};
  m_inspectorSurface = new WorkbenchSurface{
    QStringLiteral("MapWindow_InspectorSurface"),
    m_inspector,
    WorkbenchSurface::Kind::Panel,
    this};

  m_vSplitter->addWidget(m_mapViewSurface);
  m_vSplitter->addWidget(m_infoPanelSurface);

  m_hSplitter->addWidget(m_vSplitter);
  m_hSplitter->addWidget(m_inspectorSurface);

  m_mapView->setMinimumSize(100, 100);
  m_infoPanel->setMinimumSize(100, appController.projectFor(document) ? 100 : 240);

  m_vSplitter->setMinimumSize(100, 100);
  m_inspector->setMinimumSize(380, 100);

  // resize only the map view when the window resizes
  m_vSplitter->setStretchFactor(0, 1);
  m_vSplitter->setStretchFactor(1, 0);
  m_hSplitter->setStretchFactor(0, 1);
  m_hSplitter->setStretchFactor(1, 0);

  // default proportions give comfortable width to inspector and info panel
  m_hSplitter->setSizes(QList<int>{670, 330});
  m_vSplitter->setSizes(QList<int>{750, 250});

  auto* layout = new QVBoxLayout{this};
  layout->setContentsMargins(6, 6, 6, 6);
  layout->addWidget(m_hSplitter);
  m_controller = std::make_unique<MapEditorController>(appController, document, *this);
  for (auto* tree : m_inspector->findChildren<OutlinerTreeWidget*>())
  {
    connect(
      tree,
      &OutlinerTreeWidget::contextMenuActionsRequested,
      this,
      [this](QMenu* menu, const bool includeRename) {
        auto context = toolBox().actionContext(m_appController, currentMapViewBase());
        auto actions = std::unordered_map<const Action*, QAction*>{};
        const auto addAction = [&](const auto& path) {
          const auto& action = m_appController.actionManager().actionsMap().at(path);
          auto& qtAction = findOrCreateQtAction(
            actions,
            action,
            [context](const Action& triggered) mutable { triggered.execute(context); },
            menu);
          qtAction.setEnabled(action.enabled(context));
          menu->addAction(&qtAction);
        };
        if (includeRename)
          addAction("Menu/Edit/Rename Groups");
        if (!menu->actions().isEmpty())
          menu->addSeparator();
        addAction("Menu/View/Camera/Focus on Selection");
      });
    connect(tree, &OutlinerTreeWidget::pieMenuRequested, this, [this] {
      if (auto* view = currentMapViewBase())
        view->showPieMenu();
    });
  }
  for (auto* properties : m_inspector->findChildren<OutlinerEntityPropertyEditor*>())
  {
    connect(
      properties, &OutlinerEntityPropertyEditor::focusMapViewRequested, this, [this] {
        if (auto* view = currentMapViewBase())
          view->setFocus(Qt::OtherFocusReason);
      });
  }
  restoreLayoutState();
  connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* focus) {
    if (auto* view = qobject_cast<MapViewBase*>(focus); view && isAncestorOf(view))
      m_currentMapView = view;
  });
}

namespace
{
WorkspaceKind layoutKind(AppController& app, MapDocument& document)
{
  return app.projectFor(document) ? WorkspaceKind::Project : WorkspaceKind::Map;
}

double splitRatio(const Splitter& splitter, const double fallback)
{
  const auto sizes = splitter.sizes();
  const auto total = sizes[0] + sizes[1];
  return total > 0 && sizes[1] > 0
           ? std::clamp(static_cast<double>(sizes[1]) / total, 0.1, 0.8)
           : fallback;
}

void setSplitRatio(Splitter& splitter, const double ratio)
{
  const auto total = std::max(
    1,
    (splitter.orientation() == Qt::Horizontal ? splitter.width() : splitter.height())
      - splitter.handleWidth());
  const auto second = static_cast<int>(ratio * total);
  splitter.setSizes({total - second, second});
}
} // namespace

void MapEditorView::restoreLayoutState()
{
  auto settings = QSettings{};
  m_layout = loadEditorLayout(settings, layoutKind(m_appController, m_document));
  m_inspector->setVisible(m_layout.inspectorVisible);
  m_inspectorSurface->setVisible(m_layout.inspectorVisible);
  m_inspector->switchToPage(m_layout.inspectorPage);
  if (!m_externalInfoPanel)
  {
    m_infoPanel->setVisible(m_layout.infoVisible);
    m_infoPanel->switchToPage(m_layout.infoPage);
  }
  m_infoPanelSurface->setVisible(!m_externalInfoPanel && m_layout.infoVisible);
  m_layoutPending = !isVisible();
  if (!m_layoutPending)
  {
    applyLayoutState();
  }
}

void MapEditorView::applyLayoutState()
{
  if (
    !m_layout.legacyHorizontal.isEmpty()
    && m_hSplitter->restoreState(m_layout.legacyHorizontal))
  {
    m_layout.inspectorRatio = splitRatio(*m_hSplitter, m_layout.inspectorRatio);
  }
  if (
    !m_layout.legacyVertical.isEmpty()
    && m_vSplitter->restoreState(m_layout.legacyVertical))
    m_layout.infoRatio = splitRatio(*m_vSplitter, m_layout.infoRatio);
  m_layout.legacyHorizontal.clear();
  m_layout.legacyVertical.clear();
  setSplitRatio(*m_hSplitter, m_layout.inspectorRatio);
  setSplitRatio(*m_vSplitter, m_layout.infoRatio);
  m_layoutPending = false;
}

void MapEditorView::showEvent(QShowEvent* event)
{
  QWidget::showEvent(event);
  if (m_layoutPending)
  {
    layout()->activate();
    if (m_hSplitter->width() >= 800)
    {
      applyLayoutState();
    }
  }
}

void MapEditorView::resizeEvent(QResizeEvent* event)
{
  QWidget::resizeEvent(event);
  if (m_layoutPending && m_hSplitter->width() > 0)
  {
    layout()->activate();
    applyLayoutState();
  }
}

void MapEditorView::resetLayout()
{
  auto settings = QSettings{};
  saveEditorLayout(settings, layoutKind(m_appController, m_document), EditorLayout{});
  restoreLayoutState();
  emit infoPanelVisibilityChanged();
}

MapEditorView::~MapEditorView()
{
  saveWidgetState();
}

MapEditorController& MapEditorView::controller() const
{
  return *m_controller;
}

SwitchableMapViewContainer* MapEditorView::mapView() const
{
  return m_mapView;
}

MapViewBase* MapEditorView::currentMapViewBase() const
{
  return m_currentMapView ? m_currentMapView.data() : m_mapView->firstMapViewBase();
}

MapView3D* MapEditorView::currentOrFirstVisible3DMapView() const
{
  if (auto* view = qobject_cast<MapView3D*>(currentMapViewBase());
      view && view->isVisible())
    return view;
  const auto views = m_mapView->findChildren<MapView3D*>();
  const auto it =
    std::ranges::find_if(views, [](auto* view) { return view->isVisible(); });
  return it != views.end() ? *it : nullptr;
}

MapView3D& MapEditorView::activate3DMapView()
{
  const auto views = m_mapView->findChildren<MapView3D*>();
  if (views.empty())
    throw std::runtime_error{"No 3D viewport is available"};
  auto* view = currentOrFirstVisible3DMapView();
  if (!view)
  {
    if (auto* area = qobject_cast<CyclingMapView*>(currentMapViewBase()->parentWidget()))
      view = qobject_cast<MapView3D*>(area->mapViewForType(CyclingMapView::View_3D));
  }
  if (!view)
    view = views.front();
  if (auto* cycling = qobject_cast<CyclingMapView*>(view->parentWidget()))
    cycling->switchToMapView(view);
  m_currentMapView = view;
  view->setFocus();
  return *view;
}

MapViewToolBox& MapEditorView::toolBox() const
{
  return m_mapView->toolBox();
}

InfoPanel* MapEditorView::infoPanel() const
{
  return m_infoPanel;
}

void MapEditorView::useExternalInfoPanel()
{
  m_externalInfoPanel = true;
  m_infoPanelSurface->hide();
  m_infoPanel->setMinimumSize(100, 100);
  m_infoPanel->setNavigationVisible(false);
}

void MapEditorView::setExternalInfoPanelExpanded(const bool expanded)
{
  if (m_externalInfoPanelExpanded == expanded)
    return;
  m_externalInfoPanelExpanded = expanded;
  emit infoPanelVisibilityChanged();
}

Inspector* MapEditorView::inspector() const
{
  return m_inspector;
}

Console& MapEditorView::console() const
{
  return *m_infoPanel->console();
}

PythonConsole* MapEditorView::pythonConsole() const
{
  return m_infoPanel->pythonConsole();
}

Splitter* MapEditorView::hSplitter() const
{
  return m_hSplitter;
}

Splitter* MapEditorView::vSplitter() const
{
  return m_vSplitter;
}

void MapEditorView::setPythonConsoleCommandExecutor(
  std::function<void(const std::string&)> executor)
{
  pythonConsole()->setCommandExecutor(std::move(executor));
}

void MapEditorView::setPythonConsoleCompletionRootProvider(
  PythonCompletionRootProvider provider)
{
  pythonConsole()->setCompletionRootProvider(std::move(provider));
}

void MapEditorView::showInfoPanelPage(const InfoPanelPage page)
{
  if (m_externalInfoPanel)
  {
    emit infoPanelPageRequested(page);
    return;
  }
  m_infoPanelSurface->show();
  m_infoPanel->show();
  m_infoPanel->switchToPage(page);
}

void MapEditorView::showInspectorPage(const InspectorPage page)
{
  m_inspectorSurface->show();
  m_inspector->show();
  m_inspector->switchToPage(page);
}

bool MapEditorView::toggleInspector()
{
  const auto hidden = !m_inspectorSurface->isHidden();
  if (hidden)
    m_layout.inspectorRatio = splitRatio(*m_hSplitter, m_layout.inspectorRatio);
  m_inspector->setHidden(hidden);
  m_inspectorSurface->setHidden(hidden);
  return hidden;
}

bool MapEditorView::isInspectorVisible() const
{
  return !m_inspectorSurface->isHidden();
}

bool MapEditorView::toggleInfoPanel()
{
  if (m_externalInfoPanel)
  {
    emit infoPanelToggleRequested();
    return !m_externalInfoPanelExpanded;
  }
  const auto hidden = !m_infoPanelSurface->isHidden();
  if (hidden)
    m_layout.infoRatio = splitRatio(*m_vSplitter, m_layout.infoRatio);
  m_infoPanel->setHidden(hidden);
  m_infoPanelSurface->setHidden(hidden);
  return hidden;
}

bool MapEditorView::isInfoPanelVisible() const
{
  return m_externalInfoPanel ? m_externalInfoPanelExpanded
                             : !m_infoPanelSurface->isHidden();
}

void MapEditorView::undo()
{
  if (canUndo() && !cancelDrag())
    m_document.map().undoCommand();
}

void MapEditorView::redo()
{
  if (canRedo())
    m_document.map().redoCommand();
}

bool MapEditorView::canUndo() const
{
  return m_document.map().canUndoCommand();
}

bool MapEditorView::canRedo() const
{
  return m_document.map().canRedoCommand();
}

void MapEditorView::repeatLastCommands()
{
  m_document.map().repeatCommands();
}

void MapEditorView::clearRepeatableCommands()
{
  if (hasRepeatableCommands())
    m_document.map().clearRepeatableCommands();
}

bool MapEditorView::hasRepeatableCommands() const
{
  return m_document.map().canRepeatCommands();
}

void MapEditorView::moveCameraToNextTracePoint()
{
  if (canMoveCameraToNextTracePoint())
    m_mapView->moveCameraToNextTracePoint();
}

void MapEditorView::moveCameraToPreviousTracePoint()
{
  if (canMoveCameraToPreviousTracePoint())
    m_mapView->moveCameraToPreviousTracePoint();
}

bool MapEditorView::canMoveCameraToNextTracePoint() const
{
  return m_mapView->canMoveCameraToNextTracePoint();
}

bool MapEditorView::canMoveCameraToPreviousTracePoint() const
{
  return m_mapView->canMoveCameraToPreviousTracePoint();
}

void MapEditorView::reset2dCameras(MapViewBase& sourceView)
{
  if (auto* mapView2d = dynamic_cast<MapView2D*>(&sourceView))
    m_mapView->reset2dCameras(mapView2d->camera(), true);
}

void MapEditorView::focusCameraOnSelection(const bool animate)
{
  if (canFocusCamera())
    m_mapView->focusCameraOnSelection(animate);
}

bool MapEditorView::canFocusCamera() const
{
  return m_document.map().selection().hasNodes();
}

void MapEditorView::toggleMaximizeCurrentView()
{
  m_mapView->toggleMaximizeCurrentView();
}

bool MapEditorView::currentViewMaximized() const
{
  return m_mapView->currentViewMaximized();
}

bool MapEditorView::cancelDrag()
{
  return m_mapView->cancelMouseDrag() || m_inspector->cancelMouseDrag();
}

void MapEditorView::saveWidgetState() const
{
  // An unopened background view must not replace the last visible workspace layout.
  if (m_layoutPending)
    return;
  auto layout = m_layout;
  layout.inspectorVisible = !m_inspectorSurface->isHidden();
  layout.inspectorPage = m_inspector->currentPage();
  if (layout.inspectorVisible && !m_layoutPending)
    layout.inspectorRatio = splitRatio(*m_hSplitter, layout.inspectorRatio);
  if (!m_externalInfoPanel && m_infoPanel)
  {
    layout.infoVisible = !m_infoPanelSurface->isHidden();
    layout.infoPage = m_infoPanel->currentPage();
    if (layout.infoVisible && !m_layoutPending)
      layout.infoRatio = splitRatio(*m_vSplitter, layout.infoRatio);
  }
  auto settings = QSettings{};
  saveEditorLayout(settings, layoutKind(m_appController, m_document), layout);
}

} // namespace tb::ui
