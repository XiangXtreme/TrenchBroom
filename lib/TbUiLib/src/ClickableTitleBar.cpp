/*
 Copyright (C) 2024 Kristian Duske

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

#include "ui/ClickableTitleBar.h"

#include <QBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QToolButton>

#include "ui/QStyleUtils.h"
#include "ui/ViewConstants.h"

namespace tb::ui
{

ClickableTitleBar::ClickableTitleBar(
  const QString& title, const QString& stateText, QWidget* parent)
  : TitleBar{title, parent, LayoutConstants::NarrowHMargin, LayoutConstants::NarrowVMargin, true}
  , m_stateText{new QLabel{stateText}}
  , m_stateIcon{new QToolButton{this}}
{
  setObjectName("ClickableTitleBar");
  setAttribute(Qt::WA_StyledBackground, true);
  setFocusPolicy(Qt::StrongFocus);
  setAccessibleName(title);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

  m_titleLabel->setObjectName("ClickableTitleBar_Title");

  m_stateIcon->setObjectName("ClickableTitleBar_StateIcon");
  m_stateIcon->setProperty("tbControlRole", "compact");
  m_stateIcon->setProperty("tbDisclosure", true);
  m_stateIcon->setAutoRaise(true);
  m_stateIcon->setFixedSize(QSize{22, 22});
  m_stateIcon->setFocusPolicy(Qt::NoFocus);
  m_stateIcon->hide();

  if (auto* boxLayout = qobject_cast<QBoxLayout*>(layout()))
  {
    boxLayout->insertWidget(0, m_stateIcon);
    boxLayout->setSpacing(LayoutConstants::NarrowHMargin);
  }

  m_stateText->setObjectName("ClickableTitleBar_StateText");
  m_stateText->setFont(m_titleLabel->font());
  setInfoStyle(m_stateText);

  layout()->addWidget(m_stateText);

  connect(m_stateIcon, &QToolButton::clicked, this, [this] {
    setFocus(Qt::MouseFocusReason);
    emit titleBarClicked();
  });
}

void ClickableTitleBar::setStateText(const QString& stateText)
{
  m_stateText->setText(stateText);
  m_stateText->show();
  m_stateIcon->hide();
}

void ClickableTitleBar::setStateArrow(const Qt::ArrowType arrow, const QString& tooltip)
{
  m_stateIcon->setArrowType(arrow);
  m_stateIcon->setToolTip(tooltip);
  m_stateIcon->setAccessibleName(tooltip);
  m_stateIcon->show();
  m_stateText->hide();
}

void ClickableTitleBar::mousePressEvent(QMouseEvent* event)
{
  if (event->button() == Qt::LeftButton)
  {
    setFocus(Qt::MouseFocusReason);
    emit titleBarClicked();
    event->accept();
    return;
  }

  TitleBar::mousePressEvent(event);
}

void ClickableTitleBar::keyPressEvent(QKeyEvent* event)
{
  if (
    event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter
    || event->key() == Qt::Key_Space)
  {
    emit titleBarClicked();
    event->accept();
    return;
  }

  TitleBar::keyPressEvent(event);
}

} // namespace tb::ui
