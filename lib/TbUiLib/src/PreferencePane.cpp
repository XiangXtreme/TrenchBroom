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

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QBoxLayout>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSortFilterProxyModel>
#include <QTabWidget>

#include "ui/PanelLayout.h"

#include <algorithm>

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

QWidget* PreferencePane::searchMatch(const QString& query, QModelIndex* tableIndex) const
{
  const auto words =
    query.simplified().split(QRegularExpression{"\\s+"}, Qt::SkipEmptyParts);
  if (words.isEmpty())
    return nullptr;

  const auto matches = [&](const QString& text) {
    return std::ranges::all_of(
      words, [&](const auto& word) { return text.contains(word, Qt::CaseInsensitive); });
  };

  // Index captions and choices, not editable values or current configuration paths.
  // Discover on demand because game settings can rebuild their child controls.
  for (auto* widget : findChildren<QWidget*>())
  {
    auto text = widget->property("preferenceSearchTerms").toString();
    if (const auto* label = qobject_cast<QLabel*>(widget))
      text += " " + label->text();
    else if (const auto* button = qobject_cast<QAbstractButton*>(widget))
      text += " " + button->text();
    else if (const auto* group = qobject_cast<QGroupBox*>(widget))
      text += " " + group->title();
    else if (const auto* combo = qobject_cast<QComboBox*>(widget))
      for (int i = 0; i < combo->count(); ++i)
        text += " " + combo->itemText(i);

    auto context = QString{};
    for (auto* parent = widget->parentWidget(); parent && parent != this;
         parent = parent->parentWidget())
      if (const auto* tabs = qobject_cast<QTabWidget*>(parent))
        for (int i = 0; i < tabs->count(); ++i)
          if (tabs->widget(i) == widget || tabs->widget(i)->isAncestorOf(widget))
            context += " " + tabs->tabText(i);

    if (!text.isEmpty() && matches(context + " " + text))
      return widget;

    if (auto* view = qobject_cast<QAbstractItemView*>(widget); view && view->model())
    {
      const auto* model = view->model();
      // Global discovery must still find rows hidden by a table's local filter.
      if (!tableIndex)
        if (const auto* proxy = qobject_cast<const QSortFilterProxyModel*>(model);
            proxy && proxy->sourceModel())
          model = proxy->sourceModel();
      for (int row = 0; row < model->rowCount(); ++row)
      {
        auto rowText = context;
        for (int column = 0; column < model->columnCount(); ++column)
          rowText += " " + model->data(model->index(row, column)).toString();
        if (matches(rowText))
        {
          if (tableIndex)
            *tableIndex = model->index(row, 0);
          return view;
        }
      }
    }
  }
  return nullptr;
}

bool PreferencePane::matchesSearch(const QString& query) const
{
  return searchMatch(query) != nullptr;
}

void PreferencePane::revealSearch(const QString& query)
{
  if (!query.trimmed().isEmpty())
    for (auto* filter : findChildren<QLineEdit*>())
      if (filter->property("preferenceTableFilter").toBool())
        filter->clear();
  auto index = QModelIndex{};
  auto* match = searchMatch(query, &index);
  if (!match)
    return;

  for (auto* parent = match->parentWidget(); parent && parent != this;
       parent = parent->parentWidget())
  {
    if (auto* tabs = qobject_cast<QTabWidget*>(parent))
      for (int i = 0; i < tabs->count(); ++i)
        if (tabs->widget(i) == match || tabs->widget(i)->isAncestorOf(match))
          tabs->setCurrentIndex(i);
  }
  for (auto* parent = match->parentWidget(); parent && parent != this;
       parent = parent->parentWidget())
    if (auto* scroll = qobject_cast<QScrollArea*>(parent))
      scroll->ensureWidgetVisible(match, 16, 24);
  if (index.isValid())
  {
    auto* view = static_cast<QAbstractItemView*>(match);
    view->setCurrentIndex(index);
    view->scrollTo(index, QAbstractItemView::PositionAtCenter);
  }
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
