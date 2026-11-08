/*
 Copyright (C) 2010 Kristian Duske

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

#include "ui/QStyleUtils.h"

#include <QBoxLayout>
#include <QFont>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QString>
#include <QStyle>
#include <QVariant>
#include <QWidget>
#include <QtSystemDetection>

#include "ui/BorderLine.h" // IWYU pragma: keep
#include "ui/ImageUtils.h"

#include "kd/contracts.h"


namespace tb::ui
{
namespace
{
void setTextRole(QWidget& widget, const char* role)
{
  const auto value = role ? QVariant{QString::fromLatin1(role)} : QVariant{};
  if (widget.property("tbTextRole") == value)
    return;
  widget.setProperty("tbTextRole", value);
  widget.style()->unpolish(&widget);
  widget.style()->polish(&widget);
}
} // namespace

QWidget* setDefaultStyle(QWidget* widget)
{
  widget->setFont(QFont{});
  widget->setPalette(QPalette{});
  setTextRole(*widget, nullptr);
  return widget;
}

QWidget* setEmphasizedStyle(QWidget* widget)
{
  auto font = widget->font();
  font.setWeight(QFont::DemiBold);
  widget->setFont(font);
  return widget;
}

QWidget* setUnemphasizedSTyle(QWidget* widget)
{
  widget->setFont(QFont{});
  return widget;
}

QWidget* setInfoStyle(QWidget* widget)
{
  widget->setFont(QFont{});
  widget = setSmallStyle(widget);

  setTextRole(*widget, "secondary");
  return widget;
}

QWidget* setSmallStyle(QWidget* widget)
{
  widget->setAttribute(Qt::WA_MacSmallSize);
  return widget;
}

QWidget* setHeaderStyle(QWidget* widget)
{
  setDefaultStyle(widget);

  auto font = widget->font();
  font.setPointSize(2 * font.pointSize());
  font.setBold(true);
  widget->setFont(font);
  return widget;
}

QWidget* setErrorStyle(QWidget* widget)
{
  setTextRole(*widget, "error");
  return widget;
}

void setWindowIconTB(QWidget* window)
{
  contract_pre(window != nullptr);

  window->setWindowIcon(QIcon{loadPixmap("AppIcon.png")});
}

void setDefaultWindowColor(QWidget* widget)
{
  widget->setAutoFillBackground(true);
  widget->setBackgroundRole(QPalette::Window);
}

void setBaseWindowColor(QWidget* widget)
{
  widget->setAutoFillBackground(true);
  widget->setBackgroundRole(QPalette::Base);
}

void insertTitleBarSeparator([[maybe_unused]] QVBoxLayout* layout)
{
#if defined(Q_OS_WIN)
  layout->insertWidget(0, new BorderLine{}, 1);
#endif
}

QString nativeModifierLabel(const int modifier)
{
  contract_pre(
    modifier == Qt::META || modifier == Qt::SHIFT || modifier == Qt::CTRL
    || modifier == Qt::ALT);

  const auto keySequence = QKeySequence(modifier);

  // QKeySequence doesn't totally support being given just a modifier
  // but it does seem to handle the key codes like Qt::SHIFT, which
  // it turns into native text as "Shift+" or the Shift symbol on macOS,
  // and portable text as "Shift+".

  auto nativeLabel = keySequence.toString(QKeySequence::NativeText);
  if (nativeLabel.endsWith("+"))
  {
    // On Linux we get nativeLabel as something like "Ctrl+"
    // On macOS it's just the special Command character, with no +
    nativeLabel.chop(1); // Remove last character
  }

  return nativeLabel;
}

} // namespace tb::ui
