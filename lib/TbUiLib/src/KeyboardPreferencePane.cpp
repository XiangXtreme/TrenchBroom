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
 along with TrenchBroom.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ui/KeyboardPreferencePane.h"

#include <QBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTimer>

#include "base/PreferenceManager.h"
#include "prefs/Preferences.h"
#include "ui/ActionManager.h"
#include "ui/AppController.h"
#include "ui/KeyboardShortcutItemDelegate.h"
#include "ui/KeyboardShortcutModel.h"
#include "ui/MapDocument.h"
#include "ui/QStyleUtils.h"
#include "ui/SearchBox.h"
#include "ui/ViewConstants.h"

namespace tb::ui
{
namespace
{

constexpr auto ShortcutColumnWidth = 104;
constexpr auto AlternativeColumnWidth = 116;
constexpr auto ContextColumnWidth = 190;

} // namespace

KeyboardPreferencePane::KeyboardPreferencePane(
  AppController& appController, MapDocument* document, QWidget* parent)
  : PreferencePane{parent}
  , m_appController{appController}
  , m_table{new QTableView{}}
  , m_model{new KeyboardShortcutModel{m_appController.actionManager(), document, this}}
  , m_proxy{new QSortFilterProxyModel{this}}
{
  m_proxy->setSourceModel(m_model);
  m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
  m_proxy->setFilterKeyColumn(-1);
  m_proxy->setSortRole(KeyboardShortcutModel::ConflictRole);
  m_proxy->sort(0); // Sort rows with conflicts to the top

  m_table->setObjectName(QStringLiteral("KeyboardPreference_Table"));
  m_table->setCornerButtonEnabled(false);
  m_table->setModel(m_proxy);

  // Keep the edited row visible when resolving a conflict changes the sort order.
  connect(m_proxy, &QAbstractItemModel::layoutChanged, this, [this] {
    if (const auto currentIndex = m_table->currentIndex(); currentIndex.isValid())
    {
      m_table->scrollTo(currentIndex);
    }
  });

  auto* header = new QHeaderView{Qt::Horizontal};
  m_table->setHorizontalHeader(header);
  header->setSectionsMovable(false);
  header->setSectionResizeMode(
    KeyboardShortcutModel::DescriptionColumn, QHeaderView::ResizeMode::Stretch);
  header->setSectionResizeMode(
    KeyboardShortcutModel::ContextColumn, QHeaderView::ResizeMode::Fixed);
  header->setSectionResizeMode(
    KeyboardShortcutModel::ShortcutColumn, QHeaderView::ResizeMode::Fixed);
  header->setSectionResizeMode(
    KeyboardShortcutModel::AlternativeColumn, QHeaderView::ResizeMode::Fixed);
  header->resizeSection(KeyboardShortcutModel::ContextColumn, ContextColumnWidth);
  header->resizeSection(KeyboardShortcutModel::ShortcutColumn, ShortcutColumnWidth);
  header->resizeSection(KeyboardShortcutModel::AlternativeColumn, AlternativeColumnWidth);
  m_table->setTextElideMode(Qt::ElideRight);

  // Tighter than default vertical row height, without the overhead of autoresizing
  m_table->verticalHeader()->setVisible(false);
  m_table->verticalHeader()->setDefaultSectionSize(
    m_table->fontMetrics().lineSpacing() + 2);

  m_table->setSelectionMode(QAbstractItemView::SelectionMode::SingleSelection);
  m_table->setSelectionBehavior(QAbstractItemView::SelectionBehavior::SelectRows);

  m_table->setEditTriggers(
    QAbstractItemView::EditTrigger::SelectedClicked
    | QAbstractItemView::EditTrigger::DoubleClicked
    | QAbstractItemView::EditTrigger::EditKeyPressed);
  m_table->setItemDelegate(new KeyboardShortcutItemDelegate{*m_model});

  auto* searchBox = createSearchBox();
  searchBox->setProperty("preferenceTableFilter", true);
  searchBox->setPlaceholderText(tr("Filter shortcuts"));
  searchBox->setAccessibleName(tr("Filter shortcuts"));

  auto* infoLabel = new QLabel{
    tr("Double-click an item to begin editing it. Click anywhere else to end editing.")};
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
}

KeyboardPreferencePane::~KeyboardPreferencePane()
{
  m_table->closePersistentEditor(m_table->currentIndex());
  m_table->setItemDelegate(nullptr);
  m_table->setModel(nullptr);
  m_proxy->setSourceModel(nullptr);
}

bool KeyboardPreferencePane::canResetToDefaults()
{
  return true;
}

void KeyboardPreferencePane::doResetToDefaults()
{
  auto& prefs = PreferenceManager::instance();
  for (auto* preference : Preferences::keyPreferences())
  {
    prefs.resetToDefault(*preference);
  }

  m_appController.actionManager().resetAllKeySequences();
  m_model->reset();
}

void KeyboardPreferencePane::updateControls()
{
  m_table->update();
}

bool KeyboardPreferencePane::validate()
{
  return true;
}

} // namespace tb::ui
