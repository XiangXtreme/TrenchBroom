/*
 Copyright (C) 2026 XiangXtreme
 SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <QPointF>
#include <QRectF>
#include <QSizeF>

#include <map>
#include <vector>

namespace tb::ui::areaGeometry
{
using Rects = std::map<int, QRectF>;
enum class Dock
{
  None,
  Center,
  Left,
  Right,
  Top,
  Bottom
};
struct Plan
{
  Rects rects;
  std::map<int, int> copies;
  std::vector<QRectF> closed;
  QRectF highlight;
  bool valid = false;
  int nextId = 0;
};

// Coordinates are normalized; metrics are device-independent pixels, scaled by Qt.
constexpr auto MinimumWidth = 29;
constexpr auto HeaderHeight = 26;
constexpr auto SplitMinimum = 2 * HeaderHeight;
constexpr auto MaximumAreas = 16;
bool validTiling(const Rects& rects);
int direction(const QRectF& source, const QRectF& target, const QSizeF& size);
Plan join(const Rects& rects, int source, int target, const QSizeF& size);
Plan dock(
  const Rects& rects,
  int source,
  int target,
  Dock side,
  double fraction,
  const QSizeF& size);
Dock dockingTarget(
  const QRectF& source,
  const QRectF& target,
  const QPointF& point,
  const QSizeF& size,
  bool alt,
  bool ctrl,
  double& fraction);
} // namespace tb::ui::areaGeometry
