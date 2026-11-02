/*
 Copyright (C) 2026 XiangXtreme
 SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QPoint>
#include <QPointer>
#include <QRectF>

#include "gl/PerspectiveCamera.h"
#include "ui/MapViewAreaGeometry.h"
#include "ui/MapViewLayout.h"
#include "ui/MultiPaneMapView.h"

#include <map>
#include <vector>

class QJsonObject;

namespace tb::ui
{
class AppController;
class CyclingMapView;
class MapDocument;
class MapViewToolBox;
class MapViewAreaLayout;

/** Editor areas retain their native views while a rectangular topology is edited. */
class AreaMapView : public MultiPaneMapView
{
  Q_OBJECT
private:
  AppController& m_appController;
  MapDocument& m_document;
  MapViewToolBox& m_toolBox;
  MapViewLayout m_preset;
  QWidget* m_root = nullptr;
  MapViewAreaLayout* m_areaLayout = nullptr;
  std::map<CyclingMapView*, int> m_views;
  MapViewActivationTracker* m_tracker = nullptr;
  gl::PerspectiveCamera m_lastPerspective;
  QPointer<CyclingMapView> m_dragSource;
  QPointer<CyclingMapView> m_dragTarget;
  QPointer<QWidget> m_dragGrip;
  QPoint m_dragStart;
  Qt::Orientation m_splitOrientation = Qt::Horizontal;
  double m_splitRatio = 0.5;
  bool m_dragMoved = false;
  QWidget* m_preview = nullptr;
  QWidget* m_closedPreview = nullptr;
  areaGeometry::Plan m_gesturePlan;
  areaGeometry::Dock m_dock = areaGeometry::Dock::None;
  std::vector<QWidget*> m_edges;
  QPointer<QWidget> m_edgeGrip;
  areaGeometry::Rects m_edgeStartRects;
  QPoint m_edgeStart;
  bool m_edgesPending = false;

public:
  AreaMapView(
    AppController& appController,
    MapDocument& document,
    MapViewToolBox& toolBox,
    MapViewLayout preset,
    bool restoreLayout,
    QWidget* parent = nullptr);
  ~AreaMapView() override;

  std::vector<CyclingMapView*> areas() const;
  bool splitArea(
    CyclingMapView* area,
    Qt::Orientation orientation,
    double ratio = 0.5,
    bool newFirst = false);
  bool canJoinAreas(CyclingMapView* source, CyclingMapView* target) const;
  bool joinAreas(CyclingMapView* source, CyclingMapView* target);
  bool dockArea(
    CyclingMapView* source,
    CyclingMapView* target,
    areaGeometry::Dock side,
    double fraction = 0.5);
  QRectF areaBounds(CyclingMapView* area) const;
  void installActivationTracker(MapViewActivationTracker& tracker) override;
  const gl::PerspectiveCamera& perspectiveCamera() const override;
  void toggleMaximizeCurrentView() override;

  enum class FourPaneCenterMode
  {
    None,
    Unified,
    UnboundLR,
    UnboundTB
  };

  FourPaneCenterMode fourPaneCenterMode() const;
  bool canUnifyCenters() const;
  bool canUnbindCenters() const;
  void unifyCenters(double targetCoord = -1.0);
  void unbindCenters(bool horizontal = true);
  void showIntersectionMenu(QWidget* junction, const QPoint& globalPosition);

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  CyclingMapView* createArea(int mask);
  void createPreset(bool restoreLegacy);
  CyclingMapView* copyArea(CyclingMapView* source);
  void rebuildConnections();
  void finishLayoutChange(CyclingMapView* focus);
  void maximizeView(MapView* view) override;
  void restoreViews() override;
  void cancelGesture();
  void updateGesture(const QPoint& globalPosition, Qt::KeyboardModifiers modifiers);
  void showAreaMenu(CyclingMapView* area, const QPoint& globalPosition);
  QString settingsKey() const;
  void saveLayout() const;
  bool restoreLayout(const QJsonObject& saved);
  areaGeometry::Rects geometryRects() const;
  bool applyPlan(const areaGeometry::Plan& plan, CyclingMapView* focus);
  void setRects(const areaGeometry::Rects& rects);
  void scheduleEdges();
  void updateEdges();
  bool edgeEvent(QWidget* edge, QEvent* event);
};
} // namespace tb::ui
