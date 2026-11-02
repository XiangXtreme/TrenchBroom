/*
 Copyright (C) 2010 Kristian Duske
 Copyright (C) 2026 XiangXtreme

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

#include "ui/CyclingMapView.h"

#include <QActionGroup>
#include <QMenu>
#include <QResizeEvent>
#include <QShortcut>
#include <QStackedLayout>
#include <QStyle>
#include <QToolButton>

#include "gl/Camera.h"
#include "ui/ImageUtils.h"
#include "ui/MapDocument.h"
#include "ui/MapView2D.h"
#include "ui/MapView3D.h"
#include "ui/MapViewActivationTracker.h"
#include "ui/WorkbenchSurface.h"

#include "kd/contracts.h"

#include "vm/scalar.h"

#include <algorithm>
#include <array>

namespace tb::ui
{

CyclingMapView::CyclingMapView(
  AppController& appController,
  MapDocument& document,
  MapViewToolBox& toolBox,
  const int views,
  QWidget* parent)
  : MapViewContainer{parent}
  , m_document{document}
{
  setObjectName("CyclingMapView");
  createGui(appController, toolBox, views);
}

void CyclingMapView::createGui(
  AppController& appController, MapViewToolBox& toolBox, const int views)
{
  // Every area can switch types; the preset selects only its initial type.
  addMapView(new MapView3D{appController, m_document, toolBox}, View_3D);
  addMapView(
    new MapView2D{appController, m_document, toolBox, MapView2D::ViewPlane::XY}, View_XY);
  addMapView(
    new MapView2D{appController, m_document, toolBox, MapView2D::ViewPlane::XZ}, View_XZ);
  addMapView(
    new MapView2D{appController, m_document, toolBox, MapView2D::ViewPlane::YZ}, View_YZ);

  m_layout = new QStackedLayout{};
  m_layout->setContentsMargins(0, 0, 0, 0);
  // NOTE: It's important to setLayout() before adding widgets, rather than after.
  // Otherwise, they get setVisible immediately (and the first render calls happen during
  // the for loop), which breaks multisampling
  setLayout(m_layout);

  for (auto* mapView : m_mapViews)
  {
    m_layout->addWidget(mapView);
  }

  contract_assert(!m_mapViews.empty());
  m_viewButton = new QToolButton{this};
  m_viewButton->setObjectName("MapViewTypeButton");
  m_viewButton->setProperty("tbControlRole", "overlay");
  m_viewButton->setPopupMode(QToolButton::InstantPopup);
  m_viewButton->setAccessibleName(tr("View type"));
  m_viewButton->setToolTip(tr("Choose a 3D or orthographic view for this area"));
  m_viewButton->setFocusPolicy(Qt::StrongFocus);
  auto* menu = new QMenu{m_viewButton};
  menu->setObjectName("MapViewTypeMenu");
  auto* group = new QActionGroup{menu};
  const auto labels =
    std::array{tr("Perspective (3D)"), tr("Top (XY)"), tr("Front (XZ)"), tr("Side (YZ)")};
  for (auto i = size_t{0}; i < m_viewTypes.size(); ++i)
  {
    auto* action = menu->addAction(labels[i]);
    action->setCheckable(true);
    action->setData(m_viewTypes[i]);
    group->addAction(action);
    connect(action, &QAction::triggered, this, [this, type = m_viewTypes[i]]() {
      setViewType(type);
    });
  }
  m_viewButton->setMenu(menu);
  m_border = new WorkbenchBorder{this, true};
  for (const auto type : m_viewTypes)
    if (views & type)
    {
      setViewType(type);
      break;
    }
}

void CyclingMapView::addMapView(MapViewBase* mapView, const int type)
{
  m_mapViews.push_back(mapView);
  m_viewTypes.push_back(type);
  mapView->setContainer(this);
}

void CyclingMapView::switchToMapView(MapViewBase* mapView)
{
  contract_pre(std::ranges::find(m_mapViews, mapView) != m_mapViews.end());
  const auto previous = viewType();
  cancelMouseDrag();
  m_currentMapView = mapView;

  m_layout->setCurrentWidget(m_currentMapView);
  m_currentMapView->setFocus();
  updateChrome();
  m_viewButton->raise();
  if (m_border)
    m_border->raise();
  if (previous != viewType())
    emit viewChanged(previous, viewType());
}

int CyclingMapView::viewType() const
{
  const auto found = std::ranges::find(m_mapViews, m_currentMapView);
  return found != m_mapViews.end()
           ? m_viewTypes[static_cast<size_t>(found - m_mapViews.begin())]
           : 0;
}

MapViewBase* CyclingMapView::mapViewForType(const int type) const
{
  const auto found = std::ranges::find(m_viewTypes, type);
  return found != m_viewTypes.end()
           ? m_mapViews[static_cast<size_t>(found - m_viewTypes.begin())]
           : nullptr;
}

void CyclingMapView::setViewType(const int type)
{
  if (auto* view = mapViewForType(type))
    switchToMapView(view);
}

void CyclingMapView::updateChrome()
{
  if (!m_viewButton)
    return;
  for (auto* action : m_viewButton->menu()->actions())
  {
    const auto selected = action->data().toInt() == viewType();
    action->setChecked(selected);
    if (selected)
      m_viewButton->setText(action->text());
  }
  if (m_viewButton->property("viewType").toInt() != viewType())
  {
    m_viewButton->setIcon(
      loadSVGIcon(viewType() == View_3D ? "Map_cube.svg" : "InspectorFace.svg"));
    m_viewButton->setProperty("viewType", viewType());
  }
  const auto inset =
    std::max(1, style()->pixelMetric(QStyle::PM_LayoutLeftMargin, nullptr, this));
  constexpr auto GripWidth = 18;
  const auto available = std::max(0, width() - 2 * inset - 2 * GripWidth);
  m_viewButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  if (m_viewButton->sizeHint().width() > available)
    m_viewButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
  m_viewButton->move(inset + GripWidth, inset);
  m_viewButton->adjustSize();
  const auto targetVisible = !window()->isFullScreen()
                             && available >= m_viewButton->sizeHint().width()
                             && height() >= m_viewButton->height() + 2 * inset;
  if (m_viewButton->isVisible() != targetVisible)
    m_viewButton->setVisible(targetVisible);
}

void CyclingMapView::resizeEvent(QResizeEvent* event)
{
  MapViewContainer::resizeEvent(event);
  updateChrome();
}

void CyclingMapView::installActivationTracker(MapViewActivationTracker& activationTracker)
{
  for (auto* mapView : m_mapViews)
  {
    activationTracker.addWindow(mapView);
  }
}

bool CyclingMapView::isCurrent() const
{
  return m_currentMapView->isCurrent();
}

MapViewBase* CyclingMapView::firstMapViewBase()
{
  return m_currentMapView;
}

bool CyclingMapView::canSelectTall()
{
  return m_currentMapView->canSelectTall();
}

void CyclingMapView::selectTall()
{
  m_currentMapView->selectTall();
}

void CyclingMapView::reset2dCameras(const gl::Camera& masterCamera, bool animate)
{
  for (auto* mapView : m_mapViews)
  {
    mapView->reset2dCameras(masterCamera, animate);
  }
}

void CyclingMapView::focusCameraOnSelection(const bool animate)
{
  for (auto* mapView : m_mapViews)
  {
    mapView->focusCameraOnSelection(animate);
  }
}

void CyclingMapView::moveCameraToPosition(const vm::vec3f& position, const bool animate)
{
  for (auto* mapView : m_mapViews)
  {
    mapView->moveCameraToPosition(position, animate);
  }
}

void CyclingMapView::moveCameraToCurrentTracePoint()
{
  for (auto* mapView : m_mapViews)
  {
    mapView->moveCameraToCurrentTracePoint();
  }
}

bool CyclingMapView::cancelMouseDrag()
{
  auto result = false;
  for (auto* mapView : m_mapViews)
  {
    result = result || mapView->cancelMouseDrag();
  }
  return result;
}

void CyclingMapView::flashSelection()
{
  m_currentMapView->flashSelection();
}

void CyclingMapView::refreshViews()
{
  for (auto* mapView : m_mapViews)
  {
    mapView->refreshViews();
  }
}

const gl::PerspectiveCamera& CyclingMapView::perspectiveCamera() const
{
  const auto* mapView3D = [&]() -> const MapView3D* {
    for (const auto* mapView : m_mapViews)
    {
      if (const auto* candidate = dynamic_cast<const MapView3D*>(mapView))
      {
        return candidate;
      }
    }

    return nullptr;
  }();

  contract_assert(mapView3D != nullptr);

  return mapView3D->perspectiveCamera();
}

bool CyclingMapView::canMaximizeCurrentView() const
{
  return false;
}

bool CyclingMapView::currentViewMaximized() const
{
  return true;
}

void CyclingMapView::toggleMaximizeCurrentView() {}

MapView* CyclingMapView::currentMapView() const
{
  return m_currentMapView;
}

void CyclingMapView::linkCamera(CameraLinkHelper& helper)
{
  for (auto* mapView : m_mapViews)
  {
    mapView->linkCamera(helper);
  }
}

void CyclingMapView::cycleChildMapView(MapView* after)
{
  for (size_t i = 0; i < m_mapViews.size(); ++i)
  {
    if (after == m_mapViews[i])
    {
      switchToMapView(m_mapViews[vm::succ(i, m_mapViews.size())]);
      focusCameraOnSelection(false);
      break;
    }
  }
}

} // namespace tb::ui
