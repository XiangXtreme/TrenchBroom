/*
 Copyright (C) 2026 XiangXtreme
 SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QPoint>
#include <QPointer>
#include <QRectF>

#include "gl/PerspectiveCamera.h"
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

/** A binary splitter tree whose leaves retain their native map views during layout edits.
 */
class AreaMapView : public MultiPaneMapView
{
  Q_OBJECT
private:
  AppController& m_appController;
  MapDocument& m_document;
  MapViewToolBox& m_toolBox;
  MapViewLayout m_preset;
  QWidget* m_root = nullptr;
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
  bool splitArea(CyclingMapView* area, Qt::Orientation orientation, double ratio = 0.5);
  bool canJoinAreas(CyclingMapView* source, CyclingMapView* target) const;
  bool joinAreas(CyclingMapView* source, CyclingMapView* target);
  void installActivationTracker(MapViewActivationTracker& tracker) override;
  const gl::PerspectiveCamera& perspectiveCamera() const override;
  void toggleMaximizeCurrentView() override;

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  CyclingMapView* createArea(int mask);
  void createPreset();
  void rebuildConnections();
  void finishLayoutChange(CyclingMapView* focus);
  void maximizeView(MapView* view) override;
  void restoreViews() override;
  void cancelGesture();
  void updateGesture(const QPoint& globalPosition);
  void showAreaMenu(CyclingMapView* area, const QPoint& globalPosition);
  QString settingsKey() const;
  void saveLayout() const;
  QJsonObject serialize(QWidget* node) const;
  QWidget* deserialize(const QJsonObject& node);
  using AreaRects = std::map<CyclingMapView*, QRectF>;
  AreaRects areaRects() const;
  AreaRects joinedRects(CyclingMapView* source, CyclingMapView* target) const;
  QJsonObject partition(const AreaRects& rects) const;
};
} // namespace tb::ui
