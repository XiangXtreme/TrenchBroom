/*
 Copyright (C) 2026 XiangXtreme
 SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <QLayout>
#include <QRectF>

#include <map>
#include <vector>

namespace tb::ui
{
/** Rectangular editor topology; areas need not form a binary splitter hierarchy. */
class MapViewAreaLayout : public QLayout
{
private:
  std::vector<QLayoutItem*> m_items;
  std::map<QWidget*, QRectF> m_rects;

public:
  explicit MapViewAreaLayout(QWidget* parent);
  ~MapViewAreaLayout() override;
  void setAreas(const std::map<QWidget*, QRectF>& rects);
  const std::map<QWidget*, QRectF>& areas() const;
  QRect pixelRect(const QRectF& rect) const;
  int handleWidth() const;
  void addItem(QLayoutItem* item) override;
  int count() const override;
  QLayoutItem* itemAt(int index) const override;
  QLayoutItem* takeAt(int index) override;
  QSize sizeHint() const override;
  QSize minimumSize() const override;
  void setGeometry(const QRect& rect) override;
};
} // namespace tb::ui
