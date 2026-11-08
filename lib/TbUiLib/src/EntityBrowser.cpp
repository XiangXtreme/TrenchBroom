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

#include "ui/EntityBrowser.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTextBrowser>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "mdl/EntityDefinition.h"
#include "mdl/EntityDefinitionGroup.h"
#include "mdl/EntityDefinitionManager.h"
#include "mdl/EntityDefinitionUtils.h"
#include "mdl/EntityProperties.h"
#include "mdl/Map.h"
#include "mdl/Map_Selection.h"
#include "mdl/WorldNode.h"
#include "ui/EntityBrowserView.h"
#include "ui/EntityDefinitionFileChooser.h"
#include "ui/MapDocument.h"
#include "ui/SearchBox.h"

#include "kd/string_compare.h"
#include "kd/string_utils.h"

#include <algorithm>

namespace tb::ui
{
namespace
{
// Only point definitions use the native entity drop protocol. Brush types
// require an explicit conversion of the current geometry selection.
class EntityDefinitionTree : public QTreeWidget
{
public:
  using QTreeWidget::QTreeWidget;

protected:
  QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override
  {
    auto* payload = new QMimeData{};
    if (items.size() == 1 && items.front()->data(0, Qt::UserRole).toBool())
      payload->setText("entity:" + items.front()->text(0));
    return payload;
  }
  QStringList mimeTypes() const override { return {"text/plain"}; }
  Qt::DropActions supportedDropActions() const override { return Qt::CopyAction; }
};
} // namespace

EntityBrowser::EntityBrowser(
  AppController& appController, MapDocument& document, QWidget* parent)
  : QWidget{parent}
  , m_document{&document}
  , m_appController{appController}
{
  setObjectName("EntityBrowser");
  auto* layout = new QVBoxLayout{this};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  createGui(appController);
  connectObservers();
  reload();
}

void EntityBrowser::setDocument(MapDocument& document)
{
  if (m_document == &document)
    return;
  m_notifierConnection.disconnect();
  delete m_content;
  m_document = &document;
  createGui(m_appController);
  connectObservers();
  reload();
}

void EntityBrowser::setMapActionsEnabled(const bool enabled)
{
  if (m_mapActionsEnabled == enabled)
    return;
  m_mapActionsEnabled = enabled;
  refreshDefinitions();
}

void EntityBrowser::focusSearch()
{
  m_filterBox->setFocus(Qt::ShortcutFocusReason);
}

void EntityBrowser::reload()
{
  m_view->setDefaultModelScaleExpression(
    m_document->map().worldNode().entityPropertyConfig().defaultModelScaleExpression);
  m_view->invalidate();
  refreshDefinitions();
}

void EntityBrowser::createGui(AppController& appController)
{
  m_content = new QWidget{this};
  auto* outer = new QVBoxLayout{m_content};
  outer->setContentsMargins(8, 4, 8, 4);
  outer->setSpacing(4);
  layout()->addWidget(m_content);

  auto* controls = new QHBoxLayout{};
  controls->setSpacing(6);
  m_filterBox = createSearchBox();
  m_filterBox->setObjectName("EntityBrowser_Search");
  m_filterBox->setPlaceholderText(tr("Search entity types"));
  controls->addWidget(m_filterBox, 1);
  m_typeChoice = new QComboBox{};
  m_typeChoice->setObjectName("EntityBrowser_Type");
  m_typeChoice->addItems(
    {tr("All entities"), tr("Point entities"), tr("Brush entities")});
  controls->addWidget(m_typeChoice);
  m_sortOrderChoice = new QComboBox{};
  m_sortOrderChoice->setObjectName("EntityBrowser_Sort");
  m_sortOrderChoice->addItems({tr("Name"), tr("Usage")});
  controls->addWidget(m_sortOrderChoice);
  m_groupButton = new QToolButton{};
  m_groupButton->setObjectName("EntityBrowser_GroupToggle");
  m_groupButton->setText(tr("Group"));
  m_groupButton->setProperty("tbControlRole", "chip");
  m_groupButton->setCheckable(true);
  controls->addWidget(m_groupButton);
  m_usedButton = new QToolButton{};
  m_usedButton->setObjectName("EntityBrowser_UsedToggle");
  m_usedButton->setText(tr("Used"));
  m_usedButton->setProperty("tbControlRole", "chip");
  m_usedButton->setCheckable(true);
  controls->addWidget(m_usedButton);
  auto* settings = new QToolButton{};
  settings->setObjectName("EntityBrowser_Settings");
  settings->setText(tr("Definitions…"));
  settings->setToolTip(tr("Configure entity definition files for the current map"));
  controls->addWidget(settings);
  outer->addLayout(controls);

  auto* splitter = new QSplitter{Qt::Horizontal};
  splitter->setChildrenCollapsible(false);
  m_definitions = new EntityDefinitionTree{splitter};
  m_definitions->setObjectName("EntityBrowser_Definitions");
  m_definitions->setHeaderLabels({tr("Classname"), tr("Type")});
  m_definitions->setUniformRowHeights(true);
  m_definitions->setDragEnabled(true);
  m_definitions->setDragDropMode(QAbstractItemView::DragOnly);
  m_definitions->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_definitions->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  auto* details = new QWidget{splitter};
  auto* detailLayout = new QVBoxLayout{details};
  detailLayout->setContentsMargins(8, 0, 0, 0);
  detailLayout->setSpacing(4);
  m_summary = new QLabel{};
  m_summary->setObjectName("EntityBrowser_Summary");
  m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_summary->setWordWrap(true);
  detailLayout->addWidget(m_summary);
  m_scrollBar = new QScrollBar{Qt::Vertical};
  m_view = new EntityBrowserView{appController, m_scrollBar, *m_document};
  m_view->setObjectName("EntityBrowser_Preview");
  m_view->setMinimumHeight(72);
  m_view->setMaximumHeight(140);
  detailLayout->addWidget(m_view);
  m_scrollBar->setParent(m_content);
  m_scrollBar->hide();
  m_description = new QTextBrowser{};
  m_description->setObjectName("EntityBrowser_Description");
  m_description->setOpenExternalLinks(false);
  detailLayout->addWidget(m_description, 1);
  m_create = new QPushButton{};
  m_create->setObjectName("EntityBrowser_Create");
  detailLayout->addWidget(m_create);
  splitter->setStretchFactor(0, 3);
  splitter->setStretchFactor(1, 2);
  outer->addWidget(splitter, 1);
  setFocusProxy(m_filterBox);

  connect(m_filterBox, &QLineEdit::textChanged, this, &EntityBrowser::refreshDefinitions);
  for (auto* choice : {m_typeChoice, m_sortOrderChoice})
    connect(
      choice, &QComboBox::currentIndexChanged, this, &EntityBrowser::refreshDefinitions);
  for (auto* button : {m_groupButton, m_usedButton})
    connect(button, &QToolButton::toggled, this, &EntityBrowser::refreshDefinitions);
  connect(
    m_definitions,
    &QTreeWidget::currentItemChanged,
    this,
    &EntityBrowser::refreshSelection);
  connect(m_create, &QPushButton::clicked, this, [this] {
    updateCreationAction();
    if (m_create->isEnabled())
      if (const auto* item = m_definitions->currentItem())
        emit createEntityRequested(item->text(0));
  });
  connect(settings, &QToolButton::clicked, this, &EntityBrowser::showSettings);
}

void EntityBrowser::refreshDefinitions()
{
  const auto* previous = m_definitions->currentItem();
  const auto name = previous ? previous->text(0) : QString{};
  const auto scroll = m_definitions->verticalScrollBar()->value();
  const auto blocker = QSignalBlocker{m_definitions};
  m_definitions->clear();
  QTreeWidgetItem* restoreSelection = nullptr;
  const auto& manager = m_document->map().entityDefinitionManager();
  const auto order = m_sortOrderChoice->currentIndex() == 0
                       ? mdl::EntityDefinitionSortOrder::Name
                       : mdl::EntityDefinitionSortOrder::Usage;
  const auto filter = m_filterBox->text().toStdString();
  const auto add = [&](const mdl::EntityDefinition& definition, QTreeWidgetItem* group) {
    const auto point = definition.pointEntityDefinition.has_value();
    if (
      definition.name == mdl::EntityPropertyValues::WorldspawnClassname
      || (m_typeChoice->currentIndex() == 1 && !point)
      || (m_typeChoice->currentIndex() == 2 && point)
      || (m_usedButton->isChecked() && definition.usageCount() == 0))
      return;
    const auto description = definition.name + " " + definition.description;
    if (!std::ranges::all_of(kdl::str_split(filter, " "), [&](const auto& word) {
          return kdl::ci::str_contains(description, word);
        }))
      return;
    auto* item = new QTreeWidgetItem{
      {QString::fromStdString(definition.name), point ? tr("Point") : tr("Brush")}};
    item->setData(0, Qt::UserRole, point);
    item->setFlags(
      Qt::ItemIsEnabled | Qt::ItemIsSelectable
      | (point && m_mapActionsEnabled ? Qt::ItemIsDragEnabled : Qt::NoItemFlags));
    if (group)
      group->addChild(item);
    else
      m_definitions->addTopLevelItem(item);
    if (item->text(0) == name)
      restoreSelection = item;
  };
  const auto addSorted =
    [&](std::vector<const mdl::EntityDefinition*> definitions, QTreeWidgetItem* group) {
      std::ranges::sort(definitions, [&](const auto* lhs, const auto* rhs) {
        if (
          order == mdl::EntityDefinitionSortOrder::Usage
          && lhs->usageCount() != rhs->usageCount())
          return lhs->usageCount() > rhs->usageCount();
        return kdl::ci::string_less_natural{}(lhs->name, rhs->name);
      });
      for (const auto* definition : definitions)
        add(*definition, group);
    };
  if (m_groupButton->isChecked())
  {
    for (const auto& group : manager.groups())
    {
      auto* parent = new QTreeWidgetItem{{QString::fromStdString(displayName(group))}};
      addSorted(group.definitions, parent);
      if (parent->childCount() == 0)
        delete parent;
      else
      {
        parent->setFlags(Qt::ItemIsEnabled);
        m_definitions->addTopLevelItem(parent);
        parent->setExpanded(true);
      }
    }
  }
  else
  {
    auto definitions = std::vector<const mdl::EntityDefinition*>{};
    definitions.reserve(manager.definitions().size());
    for (const auto& definition : manager.definitions())
      definitions.push_back(&definition);
    addSorted(std::move(definitions), nullptr);
  }
  if (restoreSelection)
    m_definitions->setCurrentItem(restoreSelection);
  m_definitions->verticalScrollBar()->setValue(scroll);
  refreshSelection();
}

void EntityBrowser::refreshSelection()
{
  const auto* item = m_definitions->currentItem();
  const auto* definition = item && !item->text(1).isEmpty()
                             ? m_document->map().entityDefinitionManager().definition(
                                 item->text(0).toStdString())
                             : nullptr;
  m_view->setVisible(definition && definition->pointEntityDefinition);
  if (definition)
  {
    m_summary->setText(QString::fromStdString(definition->name));
    m_description->setPlainText(QString::fromStdString(definition->description));
    m_view->setDefinitionName(definition->name);
  }
  else
  {
    m_summary->setText(tr("Select an entity type"));
    m_description->setPlainText(
      tr("Select a type to read its definition. Drag a point entity into a map view to "
         "place it."));
  }
  updateCreationAction();
}

void EntityBrowser::updateCreationAction()
{
  const auto* item = m_definitions->currentItem();
  const auto* definition = item && !item->text(1).isEmpty()
                             ? m_document->map().entityDefinitionManager().definition(
                                 item->text(0).toStdString())
                             : nullptr;
  const auto point = definition && definition->pointEntityDefinition.has_value();
  m_create->setText(
    point ? tr("Create in current view") : tr("Turn selection into entity"));
  m_create->setEnabled(
    m_mapActionsEnabled && definition
    && (point || m_document->map().selection().hasOnlyGeometryNodes()));
  m_create->setToolTip(
    !m_mapActionsEnabled ? tr("Open a map to create entities")
    : point
      ? tr(
          "Create at the current view position, or drag the type into a view to place it")
      : tr("Select brushes, then convert them to this entity type"));
  m_view->setEnabled(m_mapActionsEnabled);
  m_content->findChild<QToolButton*>("EntityBrowser_Settings")
    ->setEnabled(m_mapActionsEnabled);
}

void EntityBrowser::showSettings()
{
  if (auto* existing = m_content->findChild<QDialog*>("EntityBrowser_DefinitionSettings"))
  {
    existing->show();
    existing->raise();
    existing->activateWindow();
    return;
  }
  auto* dialog = new QDialog{m_content};
  dialog->setObjectName("EntityBrowser_DefinitionSettings");
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(tr("Entity Definitions"));
  dialog->resize(560, 400);
  auto* settingsLayout = new QVBoxLayout{dialog};
  settingsLayout->addWidget(new EntityDefinitionFileChooser{*m_document, dialog});
  auto* buttons = new QDialogButtonBox{QDialogButtonBox::Close};
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  settingsLayout->addWidget(buttons);
  dialog->show();
}

void EntityBrowser::connectObservers()
{
  m_notifierConnection +=
    m_document->documentWasLoadedNotifier.connect([this] { reload(); });
  m_notifierConnection +=
    m_document->documentDidChangeNotifier.connect([this] { reload(); });
  m_notifierConnection += m_document->resourcesWereProcessedNotifier.connect(
    [this](const auto&) { m_view->invalidate(); });
  m_notifierConnection +=
    m_document->entityDefinitionsDidChangeNotifier.connect([this] { reload(); });
  const auto updateUsage = [this](const auto&) {
    if (m_usedButton->isChecked() || m_sortOrderChoice->currentIndex() == 1)
      refreshDefinitions();
  };
  m_notifierConnection += m_document->commandDoneNotifier.connect(updateUsage);
  m_notifierConnection += m_document->commandUndoneNotifier.connect(updateUsage);
  m_notifierConnection += m_document->map().selectionDidChangeNotifier.connect(
    [this](const auto&) { updateCreationAction(); });
}
} // namespace tb::ui
