/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/SelectionPropertyEditor.h"

#include <QComboBox>
#include <QLabel>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

#include "base/PreferenceManager.h"
#include "prefs/Preferences.h"
#include "ui/EntityPropertyEditor.h"
#include "ui/PanelLayout.h"
#include "ui/outliner/OutlinerEntityPropertyEditor.h"

namespace tb::ui
{
SelectionPropertyEditor::SelectionPropertyEditor(MapDocument& document, QWidget* parent)
  : QWidget{parent}
  , m_document{document}
{
  setObjectName("SelectionPropertyEditor");
  m_layout = createPanelColumn(this, PanelInsets::None);
  m_layout->setSpacing(0);
  auto* toolbar = new QWidget{this};
  auto* actions = createPanelActions(toolbar);
  setPanelInsets(*actions, PanelInsets::Toolbar);
  actions->addWidget(new QLabel{tr("Properties"), toolbar});
  actions->addStretch();
  m_presentation = new QComboBox{toolbar};
  m_presentation->setObjectName("SelectionPropertyEditor_Presentation");
  m_presentation->setProperty("tbControlRole", "compact");
  m_presentation->setAccessibleName(tr("Property presentation"));
  m_presentation->addItems({tr("Form"), tr("Table")});
  m_presentation->setToolTip(
    tr("Table includes property keys, protection and default values"));
  actions->addWidget(m_presentation);
  m_layout->addWidget(toolbar);
  connect(m_presentation, &QComboBox::currentIndexChanged, this, [](const int index) {
    setPref(Preferences::EntityPropertiesTable, index == 1);
  });
  m_notifierConnection +=
    PreferenceManager::instance().preferenceDidChangeNotifier.connect(
      [this](const auto& path) {
        if (path == Preferences::EntityPropertiesTable.path)
          updatePresentation();
      });
  updatePresentation();
}

void SelectionPropertyEditor::focusProperty(const QString& key)
{
  // Both property presentations coalesce native selection notifications. Run after
  // their queued refresh, without retaining a pointer to a map node.
  QTimer::singleShot(0, this, [this, key] {
    if (m_presentation->currentIndex() == 1 && m_table)
      m_table->focusProperty(key);
    else if (m_form)
      m_form->focusProperty(key);
  });
}

void SelectionPropertyEditor::updatePresentation()
{
  const auto table = pref(Preferences::EntityPropertiesTable);
  const auto blocker = QSignalBlocker{m_presentation};
  m_presentation->setCurrentIndex(table ? 1 : 0);
  if (table && !m_table)
  {
    m_table = new EntityPropertyEditor{m_document, this};
    m_layout->addWidget(m_table, 1);
  }
  if (!table && !m_form)
  {
    m_form = new OutlinerEntityPropertyEditor{m_document, this};
    m_form->embedInSelectionEditor();
    m_layout->addWidget(m_form, 1);
    connect(
      m_form,
      &OutlinerEntityPropertyEditor::focusMapViewRequested,
      this,
      &SelectionPropertyEditor::focusMapViewRequested);
  }
  if (m_form)
    m_form->setVisible(!table);
  if (m_table)
    m_table->setVisible(table);
}
} // namespace tb::ui
