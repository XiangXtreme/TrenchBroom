/*
 Copyright (C) 2026 XiangXtreme
 SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ui/MapViewAreaLayout.h"

#include <QStyle>
#include <QWidget>

#include "ui/MapViewAreaGeometry.h"

#include <algorithm>
#include <cmath>

namespace tb::ui
{
MapViewAreaLayout::MapViewAreaLayout(QWidget* parent)
  : QLayout{parent}
{
  setContentsMargins(0, 0, 0, 0);
  setSpacing(0);
}
MapViewAreaLayout::~MapViewAreaLayout()
{
  while (auto* item = takeAt(0))
    delete item;
}
void MapViewAreaLayout::setAreas(const std::map<QWidget*, QRectF>& rects)
{
  const auto newRects = rects;
  while (auto* item = takeAt(0))
    delete item;
  m_rects = newRects;
  for (const auto& [widget, rect] : m_rects)
  {
    addChildWidget(widget);
    addItem(new QWidgetItem{widget});
  }
  invalidate();
  setGeometry(geometry());
}
const std::map<QWidget*, QRectF>& MapViewAreaLayout::areas() const
{
  return m_rects;
}
int MapViewAreaLayout::handleWidth() const
{
  return std::max(
    parentWidget()->style()->pixelMetric(
      QStyle::PM_SplitterWidth, nullptr, parentWidget()),
    parentWidget()->style()->pixelMetric(
      QStyle::PM_LayoutHorizontalSpacing, nullptr, parentWidget()));
}
QRect MapViewAreaLayout::pixelRect(const QRectF& rect) const
{
  const auto bounds = contentsRect();
  const auto half = handleWidth() / 2;
  const auto left =
    qRound(rect.left() * bounds.width()) + (rect.left() > 0.000001 ? half : 0);
  const auto top =
    qRound(rect.top() * bounds.height()) + (rect.top() > 0.000001 ? half : 0);
  const auto right = qRound(rect.right() * bounds.width())
                     - (rect.right() < 0.999999 ? handleWidth() - half : 0);
  const auto bottom = qRound(rect.bottom() * bounds.height())
                      - (rect.bottom() < 0.999999 ? handleWidth() - half : 0);
  return QRect{
    bounds.topLeft() + QPoint{left, top},
    QSize{std::max(0, right - left), std::max(0, bottom - top)}};
}
void MapViewAreaLayout::addItem(QLayoutItem* item)
{
  m_items.push_back(item);
}
int MapViewAreaLayout::count() const
{
  return static_cast<int>(m_items.size());
}
QLayoutItem* MapViewAreaLayout::itemAt(const int index) const
{
  return index >= 0 && index < count() ? m_items[static_cast<size_t>(index)] : nullptr;
}
QLayoutItem* MapViewAreaLayout::takeAt(const int index)
{
  auto* result = itemAt(index);
  if (result)
  {
    m_items.erase(m_items.begin() + index);
    m_rects.erase(result->widget());
  }
  return result;
}
QSize MapViewAreaLayout::sizeHint() const
{
  return QSize{800, 600}.expandedTo(minimumSize());
}
QSize MapViewAreaLayout::minimumSize() const
{
  auto result = QSize{areaGeometry::MinimumWidth, areaGeometry::HeaderHeight};
  auto visible = 0;
  for (auto* item : m_items)
    if (!item->isEmpty())
      ++visible;
  if (visible <= 1)
    return result;
  for (auto* item : m_items)
  {
    if (item->isEmpty())
      continue;
    const auto rect = m_rects.at(item->widget());
    result = result.expandedTo(QSize{
      static_cast<int>(
        std::ceil((areaGeometry::MinimumWidth + handleWidth()) / rect.width())),
      static_cast<int>(
        std::ceil((areaGeometry::HeaderHeight + handleWidth()) / rect.height()))});
  }
  return result;
}
void MapViewAreaLayout::setGeometry(const QRect& rect)
{
  QLayout::setGeometry(rect);
  auto visible = 0;
  for (auto* item : m_items)
    if (!item->isEmpty())
      ++visible;
  for (auto* item : m_items)
  {
    if (item->isEmpty())
      continue;
    item->setGeometry(
      visible == 1 ? contentsRect() : pixelRect(m_rects.at(item->widget())));
  }
}
} // namespace tb::ui
