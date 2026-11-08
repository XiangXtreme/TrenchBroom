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

#include "ui/PreferencePane.h"

#include <QBoxLayout>
#include <QScrollArea>

#include "ui/PanelLayout.h"

namespace tb::ui
{

PreferencePane::PreferencePane(QWidget* parent)
  : QWidget{parent}
{
  setObjectName("PreferencePane");
  setAttribute(Qt::WA_StyledBackground);
}

PreferencePane::~PreferencePane() = default;

void PreferencePane::resetToDefaults()
{
  doResetToDefaults();
  updateControls();
}

QSize PreferencePane::contentSizeHint() const
{
  return m_content ? m_content->sizeHint() : sizeHint();
}

void PreferencePane::createScrollableContent(QLayout* contentLayout)
{
  m_content = new QWidget{};
  m_content->setObjectName("PreferencePane_Content");
  m_content->setAttribute(Qt::WA_StyledBackground);
  m_content->setLayout(contentLayout);

  auto* scrollArea = createPanelScrollArea(m_content, this);
  scrollArea->setObjectName("PreferencePane_ScrollArea");
  scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(QMargins{});
  layout->setSpacing(0);
  layout->addWidget(scrollArea);
  setLayout(layout);
}

} // namespace tb::ui
