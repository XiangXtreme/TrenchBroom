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

#pragma once

#include "base/NotifierConnection.h"
#include "ui/CameraLinkHelper.h"
#include "ui/MapViewContainer.h"

#include <vector>

class QStackedLayout;
class QToolButton;

namespace tb::ui
{
class AppController;
class MapDocument;
class MapViewToolBox;
class WorkbenchBorder;

class CyclingMapView : public MapViewContainer, public CameraLinkableView
{
  Q_OBJECT
public:
  static constexpr auto View_3D = 1;
  static constexpr auto View_XY = 2;
  static constexpr auto View_XZ = 4;
  static constexpr auto View_YZ = 8;
  static constexpr auto View_ZZ = View_XZ | View_YZ;
  static constexpr auto View_2D = View_XY | View_ZZ;
  static constexpr auto View_ALL = View_3D | View_2D;

private:
  MapDocument& m_document;

  std::vector<MapViewBase*> m_mapViews;
  std::vector<int> m_viewTypes;
  MapViewBase* m_currentMapView = nullptr;

  QStackedLayout* m_layout = nullptr;
  QToolButton* m_viewButton = nullptr;
  QToolButton* m_lightmapButton = nullptr;
  WorkbenchBorder* m_border = nullptr;
  NotifierConnection m_preferenceConnection;

public:
  CyclingMapView(
    AppController& appController,
    MapDocument& document,
    MapViewToolBox& toolBox,
    int views,
    QWidget* parent = nullptr);

private:
  void createGui(AppController& appController, MapViewToolBox& toolBox, int views);
  void addMapView(MapViewBase* mapView, int type);
  void updateChrome();
  void updateLightmapButton();

public:
  void switchToMapView(MapViewBase* mapView);
  void setViewType(int type);
  int viewType() const;
  MapViewBase* mapViewForType(int type) const;

signals:
  void viewChanged(int previousType, int currentType);

protected:
  void resizeEvent(QResizeEvent* event) override;

public: // implement MapView interface
  void installActivationTracker(MapViewActivationTracker& activationTracker) override;
  bool isCurrent() const override;
  MapViewBase* firstMapViewBase() override;
  bool canSelectTall() override;
  void selectTall() override;
  void reset2dCameras(const gl::Camera& masterCamera, bool animate) override;
  void focusCameraOnSelection(bool animate) override;
  void moveCameraToPosition(const vm::vec3f& position, bool animate) override;
  void moveCameraToCurrentTracePoint() override;

  bool cancelMouseDrag() override;
  void flashSelection() override;
  void refreshViews() override;

public: // implement MapViewContainer interface
  const gl::PerspectiveCamera& perspectiveCamera() const override;

  bool canMaximizeCurrentView() const override;
  bool currentViewMaximized() const override;
  void toggleMaximizeCurrentView() override;

protected:
  MapView* currentMapView() const override;

public:
  void cycleChildMapView(MapView* after) override;

public: // implement CameraLinkableView interface
  void linkCamera(CameraLinkHelper& linkHelper) override;
};

} // namespace tb::ui
