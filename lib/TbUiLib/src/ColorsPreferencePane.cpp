/*
 Copyright (C) 2020 MaxED

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

#include "ui/ColorsPreferencePane.h"

#include <QBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QSize>
#include <QSortFilterProxyModel>
#include <QTableView>

#include "ui/ColorModel.h"
#include "ui/QStyleUtils.h"
#include "ui/SearchBox.h"
#include "ui/ViewConstants.h"

namespace tb::ui
{

ColorsPreferencePane::ColorsPreferencePane(QWidget* parent)
  : PreferencePane{parent}
{
  m_model = new ColorModel{this};
  m_proxy = new QSortFilterProxyModel{this};
  m_proxy->setSourceModel(m_model);
  m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
  m_proxy->setFilterKeyColumn(-1); // Filter based on all columns

  m_table = new QTableView{};
  m_table->setObjectName(QStringLiteral("ColorsPreference_Table"));
  m_table->setCornerButtonEnabled(false);
  m_table->setIconSize(QSize{48, 12});
  m_table->setModel(m_proxy);

  m_table->setHorizontalHeader(new QHeaderView{Qt::Horizontal});
  m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeMode::Fixed);
  m_table->horizontalHeader()->resizeSection(0, 80);
  m_table->horizontalHeader()->setSectionResizeMode(
    1, QHeaderView::ResizeMode::ResizeToContents);
  m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeMode::Stretch);

  // Tighter than default vertical row height, without the overhead of autoresizing
  m_table->verticalHeader()->setVisible(false);
  m_table->verticalHeader()->setDefaultSectionSize(
    m_table->fontMetrics().lineSpacing() + 2);

  m_table->setSelectionMode(QAbstractItemView::SingleSelection);
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);

  auto* searchBox = createSearchBox();
  searchBox->setProperty("preferenceTableFilter", true);
  searchBox->setPlaceholderText(tr("Filter colors"));
  searchBox->setAccessibleName(tr("Filter colors"));

  auto* infoLabel = new QLabel{tr("Double-click a color to begin editing it.")};
  setInfoStyle(infoLabel);

  infoLabel->setWordWrap(true);
  auto* filterLayout = new QHBoxLayout{};
  filterLayout->setContentsMargins(16, 0, 16, 8);
  filterLayout->addWidget(searchBox);

  auto* hintLayout = new QHBoxLayout{};
  hintLayout->setContentsMargins(16, 8, 16, 8);
  hintLayout->addWidget(infoLabel);

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addLayout(filterLayout);
  layout->addWidget(m_table, 1);
  layout->addLayout(hintLayout);
  setLayout(layout);

  connect(searchBox, &QLineEdit::textChanged, this, [&](const QString& newText) {
    m_proxy->setFilterFixedString(newText);
  });

  connect(m_table, &QTableView::doubleClicked, this, [&](const QModelIndex& index) {
    m_model->pickColor(m_proxy->mapToSource(index));
  });
}

bool ColorsPreferencePane::canResetToDefaults()
{
  return true;
}

void ColorsPreferencePane::doResetToDefaults()
{
  m_model->reset();
}

void ColorsPreferencePane::updateControls()
{
  m_table->update();
}

bool ColorsPreferencePane::validate()
{
  return true;
}

} // namespace tb::ui
