/*
 Copyright (C) 2026 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "TestWindowPolicy.h"

#include <QApplication>
#include <QEvent>
#include <QWidget>

namespace tb::ui
{

TestWindowPolicy::TestWindowPolicy(QApplication& app)
{
  app.installEventFilter(this);
}

bool TestWindowPolicy::eventFilter(QObject* watched, QEvent* event)
{
  if (event->type() == QEvent::Polish || event->type() == QEvent::Show)
  {
    if (auto* widget = qobject_cast<QWidget*>(watched); widget && widget->isWindow())
    {
      // QWidget delivers Show before mapping its native window. Keep Qt visibility,
      // layouts and rendering intact, including for popups and promoted children.
      widget->setAttribute(Qt::WA_DontShowOnScreen);
    }
  }
  return QObject::eventFilter(watched, event);
}

} // namespace tb::ui
