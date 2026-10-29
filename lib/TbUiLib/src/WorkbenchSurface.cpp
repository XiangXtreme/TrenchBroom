/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom.
*/

#include "ui/WorkbenchSurface.h"

#include <QPainterPath>
#include <QRegion>
#include <QResizeEvent>
#include <QVBoxLayout>

namespace tb::ui
{

WorkbenchSurface::WorkbenchSurface(const QString& objectName, QWidget* content)
{
  setObjectName(objectName);
  setAttribute(Qt::WA_StyledBackground);

  auto* layout = new QVBoxLayout{this};
  layout->setContentsMargins(1, 1, 1, 1);
  layout->setSpacing(0);
  layout->addWidget(content);
}

void WorkbenchSurface::resizeEvent(QResizeEvent* event)
{
  QWidget::resizeEvent(event);

  if (window()->isFullScreen())
  {
    clearMask();
    return;
  }

  auto path = QPainterPath{};
  path.addRoundedRect(QRectF{rect()}, 8.0, 8.0);
  setMask(QRegion{path.toFillPolygon().toPolygon()});
}

} // namespace tb::ui