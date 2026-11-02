/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom.
*/

#include "ui/WorkbenchSurface.h"

#include <QApplication>
#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

#include <algorithm>

namespace tb::ui
{

WorkbenchBorder::WorkbenchBorder(QWidget* host, const bool trackFocus)
  : QWidget{host}
{
  setObjectName("WorkbenchBorder");
  setProperty("tbSurfaceFrame", true);
  setAttribute(Qt::WA_TransparentForMouseEvents);
  host->installEventFilter(this);
  setGeometry(host->rect());
  if (trackFocus)
    connect(
      qApp, &QApplication::focusChanged, this, [this, host](QWidget*, QWidget* focus) {
        m_active = focus && (focus == host || host->isAncestorOf(focus));
        update();
      });
}

bool WorkbenchBorder::eventFilter(QObject* watched, QEvent* event)
{
  if (
    watched == parentWidget()
    && (event->type() == QEvent::Resize || event->type() == QEvent::Show))
  {
    setGeometry(parentWidget()->rect());
    raise();
  }
  return QWidget::eventFilter(watched, event);
}

void WorkbenchBorder::paintEvent(QPaintEvent*)
{
  auto painter = QPainter{this};
  painter.setRenderHint(QPainter::Antialiasing);
  const auto bounds = QRectF{rect()};
  auto corners = QPainterPath{};
  corners.addRect(bounds);
  auto surface = QPainterPath{};
  surface.addRoundedRect(bounds, m_radius, m_radius);
  // Cover only the corner cutouts. QWidget masks are integer regions and would
  // produce stair-stepped edges, especially against an OpenGL canvas.
  painter.fillPath(corners.subtracted(surface), m_outsideColor);
  if (m_borderWidth > 0)
  {
    const auto inset = m_borderWidth / 2;
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen{m_active ? m_activeBorderColor : m_borderColor, m_borderWidth});
    painter.drawRoundedRect(
      bounds.adjusted(inset, inset, -inset, -inset),
      std::max(qreal{0}, m_radius - inset),
      std::max(qreal{0}, m_radius - inset));
  }
}

WorkbenchSurface::WorkbenchSurface(
  const QString& objectName, QWidget* content, const Kind kind, QWidget* parent)
  : QWidget{parent}
{
  setObjectName(objectName);
  setAttribute(Qt::WA_StyledBackground);
  auto* layout = new QVBoxLayout{this};
  const auto inset = kind == Kind::Panel ? 1 : 0;
  layout->setContentsMargins(inset, inset, inset, inset);
  layout->setSpacing(0);
  layout->addWidget(content);
  if (kind == Kind::Panel)
    new WorkbenchBorder{this};
}

} // namespace tb::ui
