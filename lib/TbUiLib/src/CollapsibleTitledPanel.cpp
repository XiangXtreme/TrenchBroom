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

#include "ui/CollapsibleTitledPanel.h"

#include <QByteArray>
#include <QDataStream>
#include <QIODevice>
#include <QLayout>
#include <QStyle>

#include "ui/BorderLine.h"
#include "ui/ClickableTitleBar.h"
#include "ui/ViewConstants.h"

namespace tb::ui
{

CollapsibleTitledPanel::CollapsibleTitledPanel(
  const QString& title, const bool initiallyExpanded, QWidget* parent)
  : QWidget{parent}
  , m_titleBar{new ClickableTitleBar{title, "hide"}}
  , m_divider{new BorderLine{}}
  , m_panel{new QWidget{}}
  , m_expanded{initiallyExpanded}
{
  setProperty("expanded", m_expanded);
  m_titleBar->setProperty("collapsible", true);

  auto* sizer = new QVBoxLayout{};
  sizer->setContentsMargins(0, 0, 0, 0);
  sizer->setSpacing(0);
  sizer->addWidget(m_titleBar, 0);
  sizer->addWidget(m_divider, 0);
  sizer->addWidget(m_panel, 1);
  setLayout(sizer);

  connect(m_titleBar, &ClickableTitleBar::titleBarClicked, this, [&]() {
    setExpanded(!m_expanded);
  });

  updateExpanded();
}

QWidget* CollapsibleTitledPanel::getPanel() const
{
  return m_panel;
}

void CollapsibleTitledPanel::expand()
{
  setExpanded(true);
}

void CollapsibleTitledPanel::collapse()
{
  setExpanded(false);
}

bool CollapsibleTitledPanel::expanded() const
{
  return m_expanded;
}

void CollapsibleTitledPanel::setExpanded(const bool expanded)
{
  if (expanded != m_expanded)
  {
    m_expanded = expanded;
    updateExpanded();
    emit toggled(m_expanded);
  }
}

QByteArray CollapsibleTitledPanel::saveState() const
{
  auto result = QByteArray{};
  auto stream = QDataStream{&result, QIODevice::WriteOnly};
  stream << m_expanded;
  return result;
}

bool CollapsibleTitledPanel::restoreState(const QByteArray& state)
{
  auto stream = QDataStream{state};
  bool expanded;
  stream >> expanded;

  if (stream.status() == QDataStream::Ok)
  {
    setExpanded(expanded);
    return true;
  }

  return false;
}

void CollapsibleTitledPanel::updateExpanded()
{
  setProperty("expanded", m_expanded);
  m_titleBar->setProperty("expanded", m_expanded);

  if (m_expanded)
  {
    m_divider->show();
    m_panel->show();
    m_titleBar->setStateIcon(style()->standardIcon(QStyle::SP_ArrowDown), tr("Collapse"));
  }
  else
  {
    m_divider->hide();
    m_panel->hide();
    m_titleBar->setStateIcon(style()->standardIcon(QStyle::SP_ArrowRight), tr("Expand"));
  }
}

} // namespace tb::ui
