/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom.
*/

#include "ui/MapEditorView.h"

#include <QVBoxLayout>

#include "ui/AppController.h"
#include "ui/Console.h"
#include "ui/InfoPanel.h"
#include "ui/Inspector.h"
#include "ui/MapDocument.h"
#include "ui/PythonConsole.h"
#include "ui/Splitter.h"
#include "ui/SwitchableMapViewContainer.h"
#include "ui/WidgetState.h"
#include "ui/WorkbenchSurface.h"

#include <cstdio>
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
  m_hSplitter->setHandleWidth(4);
  m_hSplitter->setObjectName("MapWindow_HorizontalSplitter");

  m_vSplitter = new Splitter{Qt::Vertical, DrawKnob::No, this};
  m_vSplitter->setChildrenCollapsible(false);
  m_vSplitter->setHandleWidth(4);
  m_vSplitter->setObjectName("MapWindow_VerticalSplitterSplitter");

  m_infoPanel = new InfoPanel{m_appController, m_document, this};
  m_infoPanel->setObjectName("MapWindow_InfoPanel");

  m_mapView = new SwitchableMapViewContainer{m_appController, m_document, this};
  m_inspector = new Inspector{m_appController, m_document, this};
  m_inspector->setObjectName("Inspector");

  m_mapView->connectTopWidgets(m_inspector);

  m_mapViewSurface =
    new WorkbenchSurface{QStringLiteral("MapWindow_EditorSurface"), m_mapView};
  m_infoPanelSurface =
    new WorkbenchSurface{QStringLiteral("MapWindow_InfoPanelSurface"), m_infoPanel};
  m_inspectorSurface =
    new WorkbenchSurface{QStringLiteral("MapWindow_InspectorSurface"), m_inspector};

  m_vSplitter->addWidget(m_mapViewSurface);
  m_vSplitter->addWidget(m_infoPanelSurface);

  m_hSplitter->addWidget(m_vSplitter);
  m_hSplitter->addWidget(m_inspectorSurface);

  m_mapView->setMinimumSize(100, 100);
  m_infoPanel->setMinimumSize(100, 100);

  m_vSplitter->setMinimumSize(100, 100);
  m_inspector->setMinimumSize(350, 100);

  // resize only the map view when the window resizes
  m_vSplitter->setStretchFactor(0, 1);
  m_vSplitter->setStretchFactor(1, 0);
  m_hSplitter->setStretchFactor(0, 1);
  m_hSplitter->setStretchFactor(1, 0);

  // give most of the space to the map view
  m_hSplitter->setSizes(QList<int>{1'000'000, 1});
  m_vSplitter->setSizes(QList<int>{1'000'000, 1});

  auto* layout = new QVBoxLayout{this};
  layout->setContentsMargins(6, 4, 6, 4);
  layout->addWidget(m_hSplitter);
}

void MapEditorView::restoreLayoutState()
{
  restoreWidgetState(m_hSplitter);
  restoreWidgetState(m_vSplitter);
  restoreWidgetState(m_inspector);
  restoreWidgetState(m_infoPanel);

  m_inspectorSurface->setHidden(m_inspector->isHidden());
  m_infoPanelSurface->setHidden(m_infoPanel->isHidden());
}

MapEditorView::~MapEditorView()
{
  saveWidgetState();
}

SwitchableMapViewContainer* MapEditorView::mapView() const
{
  return m_mapView;
}

MapViewBase* MapEditorView::currentMapViewBase() const
{
  return m_mapView->firstMapViewBase();
}

InfoPanel* MapEditorView::infoPanel() const
{
  return m_infoPanel;
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
  m_inspector->setHidden(hidden);
  m_inspectorSurface->setHidden(hidden);
  return hidden;
}

bool MapEditorView::isInspectorVisible() const
{
  return m_inspectorSurface->isVisible();
}

bool MapEditorView::toggleInfoPanel()
{
  const auto hidden = !m_infoPanelSurface->isHidden();
  m_infoPanel->setHidden(hidden);
  m_infoPanelSurface->setHidden(hidden);
  return hidden;
}

bool MapEditorView::isInfoPanelVisible() const
{
  return m_infoPanelSurface->isVisible();
}

bool MapEditorView::cancelDrag()
{
  return m_mapView->cancelMouseDrag() || m_inspector->cancelMouseDrag();
}

void MapEditorView::saveWidgetState() const
{
  tb::ui::saveWidgetState(m_hSplitter);
  tb::ui::saveWidgetState(m_vSplitter);
  tb::ui::saveWidgetState(m_inspector);
  tb::ui::saveWidgetState(m_infoPanel);
}

} // namespace tb::ui
