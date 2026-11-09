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

#include "ui/IssueBrowserView.h"

#include <QCheckBox>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardItemModel>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include "mdl/BrushNode.h"
#include "mdl/EditorContext.h"
#include "mdl/EntityNode.h"
#include "mdl/Issue.h"
#include "mdl/IssueQuickFix.h"
#include "mdl/Map.h"
#include "mdl/Map_Selection.h"
#include "mdl/Transaction.h"
#include "mdl/Validator.h"
#include "mdl/WorldNode.h"
#include "ui/FlagsPopupEditor.h"
#include "ui/MapDocument.h"
#include "ui/PanelLayout.h"
#include "ui/QStyleUtils.h"
#include "ui/SignalDelayer.h"
#include "ui/Splitter.h"

#include "kd/vector_utils.h"

#include <algorithm>
#include <chrono>
#include <unordered_set>

namespace tb::ui
{
namespace
{
constexpr auto EntryRole = Qt::UserRole + 1;
constexpr auto TypeRole = Qt::UserRole + 2;

QString explanation(const QString& category)
{
  if (category == "Non-integer vertices")
    return IssueBrowserView::tr(
      "Fractional vertices can be intentional after rotation, clipping or terrain "
      "editing. Inspect the brush before snapping; snapping to a 1-unit grid changes its "
      "geometry.");
  if (category == "Missing entity definition")
    return IssueBrowserView::tr(
      "The classname is not present in the loaded entity definitions. Check the game "
      "configuration and FGD before deleting the object.");
  if (category == "Empty property value" || category == "Empty property name")
    return IssueBrowserView::tr(
      "Check whether this property is intentional. Removing it may change entity "
      "behavior; consult the entity definition first.");
  if (
    category == "Missing entity link target" || category == "Missing entity link source")
    return IssueBrowserView::tr(
      "Check target and targetname together. A missing link may prevent a trigger or "
      "scripted sequence from working.");
  if (category == "Invalid UV scale")
    return IssueBrowserView::tr(
      "A zero texture scale is invalid. Resetting it changes texture alignment on the "
      "affected faces.");
  if (category == "Placeholder texture")
    return IssueBrowserView::tr(
      "Assign a material to this face using the material browser or face inspector.");
  return IssueBrowserView::tr(
    "Review the original diagnostic and the affected object before making changes. Check "
    "the entity definition or map settings when appropriate.");
}

// The source labels are owned by validators, not translated text or numeric type IDs.
const char* const categories[] = {
  QT_TRANSLATE_NOOP("tb::ui::IssueBrowserView", "Empty property value"),
  QT_TRANSLATE_NOOP("tb::ui::IssueBrowserView", "Long entity property keys"),
  QT_TRANSLATE_NOOP("tb::ui::IssueBrowserView", "Missing entity link source"),
  QT_TRANSLATE_NOOP("tb::ui::IssueBrowserView", "Missing entity link target"),
  QT_TRANSLATE_NOOP("tb::ui::IssueBrowserView", "Empty property name"),
  QT_TRANSLATE_NOOP("tb::ui::IssueBrowserView", "Missing entity classname"),
  QT_TRANSLATE_NOOP("tb::ui::IssueBrowserView", "Invalid UV scale"),
  QT_TRANSLATE_NOOP("tb::ui::IssueBrowserView", "Placeholder texture"),
  QT_TRANSLATE_NOOP("tb::ui::IssueBrowserView", "Missing mod directory"),
  QT_TRANSLATE_NOOP("tb::ui::IssueBrowserView", "Long entity property value"),
  QT_TRANSLATE_NOOP("tb::ui::IssueBrowserView", "Missing entity definition"),
  QT_TRANSLATE_NOOP("tb::ui::IssueBrowserView", "Mixed brush content flags"),
  QT_TRANSLATE_NOOP("tb::ui::IssueBrowserView", "Empty group"),
  QT_TRANSLATE_NOOP("tb::ui::IssueBrowserView", "Non-integer vertices"),
  QT_TRANSLATE_NOOP("tb::ui::IssueBrowserView", "Empty brush entity"),
  QT_TRANSLATE_NOOP("tb::ui::IssueBrowserView", "Point entity with brushes"),
  QT_TRANSLATE_NOOP("tb::ui::IssueBrowserView", "Invalid entity property keys"),
  QT_TRANSLATE_NOOP("tb::ui::IssueBrowserView", "Invalid entity property values"),
  QT_TRANSLATE_NOOP("tb::ui::IssueBrowserView", "Objects out of world bounds"),
  QT_TRANSLATE_NOOP("tb::ui::IssueBrowserView", "Objects out of soft map bounds"),
  QT_TRANSLATE_NOOP("tb::ui::IssueBrowserView", "Paths must use forward slashes"),
};
const char* const fixLabels[] = {
  QT_TRANSLATE_NOOP("tb::ui::IssueBrowserView", "Snap Vertices"),
  QT_TRANSLATE_NOOP("tb::ui::IssueBrowserView", "Delete Objects"),
  QT_TRANSLATE_NOOP("tb::ui::IssueBrowserView", "Delete Property"),
  QT_TRANSLATE_NOOP("tb::ui::IssueBrowserView", "Reset UV Scale"),
  QT_TRANSLATE_NOOP("tb::ui::IssueBrowserView", "Remove Mod"),
  QT_TRANSLATE_NOOP("tb::ui::IssueBrowserView", "Truncate Property Values"),
  QT_TRANSLATE_NOOP("tb::ui::IssueBrowserView", "Move Brushes to World"),
  QT_TRANSLATE_NOOP("tb::ui::IssueBrowserView", "Replace \" with '"),
  QT_TRANSLATE_NOOP("tb::ui::IssueBrowserView", "Replace \\ with /"),
};
QString fixLabel(const std::string& description)
{
  for (const auto* label : fixLabels)
    if (description == label)
      return IssueBrowserView::tr(label);
  return QString::fromStdString(description);
}
} // namespace

QString issueTypeLabel(const std::string& description)
{
  for (const auto* label : categories)
    if (description == label)
      return IssueBrowserView::tr(label);
  return QString::fromStdString(description);
}

IssueBrowserView::IssueBrowserView(MapDocument& document, QWidget* parent)
  : QWidget{parent}
  , m_document{document}
  , m_validateSignalDelayer{new SignalDelayer{std::chrono::milliseconds{500}, this}}
{
  createGui();
  updateFilterFlags();
  connect(
    m_validateSignalDelayer,
    &SignalDelayer::processSignal,
    this,
    &IssueBrowserView::validate);
}

void IssueBrowserView::createGui()
{
  auto* layout = createPanelColumn(this, PanelInsets::None);
  layout->setSpacing(0);
  auto* toolbar = new QWidget{this};
  auto* actions = createPanelActions(toolbar);
  setPanelInsets(*actions, PanelInsets::Toolbar);
  m_search = new QLineEdit{toolbar};
  m_search->setObjectName("IssueBrowser_Search");
  m_search->setPlaceholderText(tr("Search issues, objects or properties"));
  m_search->setAccessibleName(tr("Search issues"));
  m_search->setClearButtonEnabled(true);
  actions->addWidget(m_search, 1);
  m_status = new QLabel{toolbar};
  setInfoStyle(m_status);
  actions->addWidget(m_status);
  auto* showIgnored = new QCheckBox{tr("Show ignored"), toolbar};
  showIgnored->setObjectName("IssueBrowser_ShowHidden");
  actions->addWidget(showIgnored);
  connect(showIgnored, &QCheckBox::toggled, this, &IssueBrowserView::setShowHiddenIssues);
  m_filter = new FlagsPopupEditor{1, tr("Issue types"), false};
  m_filter->setObjectName("IssueBrowser_Filter");
  actions->addWidget(m_filter);
  connect(
    m_filter,
    &FlagsPopupEditor::flagChanged,
    this,
    [this](size_t, int, const int flags, int) { setHiddenIssueTypes(~flags); });
  layout->addWidget(toolbar);

  auto* splitter = new Splitter{Qt::Horizontal, this};
  splitter->setChildrenCollapsible(false);
  m_model = new QStandardItemModel{this};
  m_tree = new QTreeView{splitter};
  m_tree->setObjectName("IssueBrowser_Tree");
  m_tree->setModel(m_model);
  m_tree->setUniformRowHeights(true);
  m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_tree->setExpandsOnDoubleClick(false);
  m_tree->header()->setStretchLastSection(false);
  m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
  auto* details = new QWidget{splitter};
  auto* detailLayout = createPanelColumn(details);
  auto* content = new QWidget{details};
  auto* body = createPanelColumn(content, PanelInsets::None);
  m_title = new QLabel{tr("Select an issue to inspect"), content};
  m_title->setObjectName("IssueBrowser_Title");
  m_title->setWordWrap(true);
  auto font = m_title->font();
  font.setWeight(QFont::DemiBold);
  m_title->setFont(font);
  body->addWidget(m_title);
  m_details = new QLabel{content};
  m_details->setObjectName("IssueBrowser_Details");
  m_details->setTextFormat(Qt::PlainText);
  m_details->setWordWrap(true);
  m_details->setTextInteractionFlags(
    Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
  body->addWidget(m_details);
  detailLayout->addWidget(createPanelScrollArea(content, details), 1);

  auto* primary = createPanelActions();
  m_locate = new QPushButton{tr("Locate objects"), details};
  m_locate->setObjectName("IssueBrowser_Locate");
  m_editProperty = new QPushButton{tr("Edit property"), details};
  m_editProperty->setObjectName("IssueBrowser_EditProperty");
  primary->addWidget(m_locate);
  primary->addWidget(m_editProperty);
  primary->addStretch();
  body->addLayout(primary);
  auto* secondary = createPanelActions();
  m_fix = new QToolButton{details};
  m_fix->setText(tr("Fix selected"));
  m_fix->setObjectName("IssueBrowser_Fix");
  m_fix->setPopupMode(QToolButton::InstantPopup);
  auto* fixMenu = new QMenu{m_fix};
  m_fix->setMenu(fixMenu);
  m_ignore = new QPushButton{tr("Ignore"), details};
  m_ignore->setObjectName("IssueBrowser_Ignore");
  m_restore = new QPushButton{tr("Restore"), details};
  m_restore->setObjectName("IssueBrowser_Restore");
  const auto ignoreHint = tr(
    "Ignores this issue type on the affected objects for this session only. It does not "
    "hide objects or change the map file.");
  m_ignore->setToolTip(ignoreHint);
  m_restore->setToolTip(ignoreHint);
  secondary->addWidget(m_fix);
  secondary->addWidget(m_ignore);
  secondary->addWidget(m_restore);
  secondary->addStretch();
  body->addLayout(secondary);
  m_result = new QLabel{details};
  m_result->setObjectName("IssueBrowser_Result");
  m_result->setWordWrap(true);
  setInfoStyle(m_result);
  body->addWidget(m_result);
  body->addStretch();
  splitter->setStretchFactor(0, 3);
  splitter->setStretchFactor(1, 2);
  layout->addWidget(splitter, 1);

  connect(m_search, &QLineEdit::textChanged, this, [this] { rebuildModel(); });
  connect(m_tree->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
    if (!m_rebuilding)
    {
      m_result->clear();
      updateDetails();
    }
  });
  connect(m_tree, &QTreeView::doubleClicked, this, [this](const QModelIndex& index) {
    if (index.parent().isValid())
      locateSelection();
    else
      m_tree->setExpanded(index, !m_tree->isExpanded(index));
  });
  connect(m_locate, &QPushButton::clicked, this, &IssueBrowserView::locateSelection);
  connect(m_editProperty, &QPushButton::clicked, this, [this] {
    const auto entries = selectedEntries();
    const auto issues = collectIssues();
    if (entries.size() != 1 || issues.empty())
      return;
    auto transaction = mdl::Transaction{m_document.map(), "Inspect Issue Property"};
    if (!selectObjects(issues))
    {
      transaction.cancel();
      return;
    }
    transaction.commit();
    emit editPropertyRequested(m_entries[entries.front()].property);
  });
  connect(m_ignore, &QPushButton::clicked, this, [this] { setIssueVisibility(false); });
  connect(m_restore, &QPushButton::clicked, this, [this] { setIssueVisibility(true); });
  connect(
    fixMenu, &QMenu::aboutToShow, this, [this, fixMenu] { populateFixMenu(*fixMenu); });
  connect(m_tree, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
    auto* menu = new QMenu{m_tree};
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->addAction(tr("Locate objects"), this, &IssueBrowserView::locateSelection)
      ->setEnabled(m_locate->isEnabled());
    menu->addAction(tr("Ignore"), this, [this] { setIssueVisibility(false); })
      ->setEnabled(m_ignore->isEnabled());
    menu->addAction(tr("Restore"), this, [this] { setIssueVisibility(true); })
      ->setEnabled(m_restore->isEnabled());
    auto* fixesMenu = menu->addMenu(tr("Fix selected"));
    populateFixMenu(*fixesMenu);
    fixesMenu->setEnabled(!fixesMenu->isEmpty());
    menu->popup(m_tree->viewport()->mapToGlobal(pos));
  });
  connect(m_tree, &QTreeView::expanded, this, [this](const QModelIndex& index) {
    if (!m_rebuilding && m_search->text().trimmed().isEmpty())
      m_expansion[index.data(TypeRole).toInt()] = true;
  });
  connect(m_tree, &QTreeView::collapsed, this, [this](const QModelIndex& index) {
    if (!m_rebuilding && m_search->text().trimmed().isEmpty())
      m_expansion[index.data(TypeRole).toInt()] = false;
  });
  updateDetails();
}

void IssueBrowserView::updateFilterFlags()
{
  auto flags = QList<int>{};
  auto labels = QStringList{};
  for (const auto* validator : m_document.map().worldNode().registeredValidators())
  {
    flags.push_back(validator->type());
    labels.push_back(issueTypeLabel(validator->description()));
  }
  m_filter->setFlags(flags, labels);
  m_filter->setFlagValue(~m_hiddenIssueTypes);
}

int IssueBrowserView::hiddenIssueTypes() const
{
  return m_hiddenIssueTypes;
}
void IssueBrowserView::setHiddenIssueTypes(const int types)
{
  m_hiddenIssueTypes = types;
  rebuildModel();
}
void IssueBrowserView::setShowHiddenIssues(const bool show)
{
  m_showHiddenIssues = show;
  rebuildModel();
}
void IssueBrowserView::reload()
{
  m_result->clear();
  m_valid = false;
  m_liveIssues.clear();
  m_status->setText(tr("Updating checks…"));
  updateDetails();
  m_validateSignalDelayer->queueSignal();
}
void IssueBrowserView::deselectAll()
{
  m_tree->clearSelection();
}

void IssueBrowserView::updateIssues()
{
  m_liveIssues.clear();
  m_entries.clear();
  auto& world = m_document.map().worldNode();
  const auto validators = world.registeredValidators();
  auto labels = std::unordered_map<mdl::IssueType, QString>{};
  for (const auto* validator : validators)
    labels.emplace(validator->type(), QString::fromStdString(validator->description()));
  const auto collect = [&](auto&& self, mdl::Node& node, const QString& path) -> void {
    for (const auto* issue : node.issues(validators))
    {
      const auto* property = dynamic_cast<const mdl::EntityPropertyIssue*>(issue);
      m_entries.push_back(
        {issue->seqId(),
         issue->type(),
         labels[issue->type()],
         path,
         QString::fromStdString(issue->description()),
         property ? QString::fromStdString(property->propertyKey()) : QString{},
         property ? QString::fromStdString(property->propertyValue()) : QString{},
         issue->lineNumber(),
         issue->hidden(),
         property != nullptr});
      m_liveIssues.emplace(issue->seqId(), issue);
    }
    auto index = 0;
    for (auto* child : node.children())
    {
      ++index;
      self(
        self,
        *child,
        path + " / " + QString::fromStdString(child->name())
          + QStringLiteral(" #%1").arg(index));
    }
  };
  collect(collect, world, QString::fromStdString(world.name()));
}

IssueBrowserView::SelectionState IssueBrowserView::selectionState() const
{
  auto state = SelectionState{};
  for (const auto& index : m_tree->selectionModel()->selectedRows())
  {
    if (index.parent().isValid())
      state.ids.insert(m_entries[index.data(EntryRole).value<size_t>()].id);
    else
      state.types.insert(index.data(TypeRole).toInt());
  }
  return state;
}
void IssueBrowserView::rebuildModel()
{
  rebuildModel(selectionState());
}
void IssueBrowserView::rebuildModel(const SelectionState& selection)
{
  const auto scroll = m_tree->verticalScrollBar()->value();
  m_rebuilding = true;
  const auto blocker = QSignalBlocker{m_tree->selectionModel()};
  m_model->clear();
  m_model->setHorizontalHeaderLabels({tr("Issue / object"), tr("Line")});
  m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  auto groups = std::unordered_map<int, QStandardItem*>{};
  auto orderedGroups = std::vector<QStandardItem*>{};
  const auto query = m_search->text().trimmed();
  auto visible = 0;
  auto restoredSelection = QItemSelection{};
  for (size_t i = 0; i < m_entries.size(); ++i)
  {
    const auto& entry = m_entries[i];
    if ((entry.type & m_hiddenIssueTypes) != 0 || (entry.hidden && !m_showHiddenIssues))
      continue;
    const auto title = issueTypeLabel(entry.category.toStdString());
    const auto haystack = title + " " + entry.category + " " + entry.path + " "
                          + entry.description + " " + entry.property + " " + entry.value;
    if (!haystack.contains(query, Qt::CaseInsensitive))
      continue;
    auto*& group = groups[entry.type];
    if (!group)
    {
      group = new QStandardItem{title};
      group->setData(entry.type, TypeRole);
      auto font = group->font();
      font.setWeight(QFont::DemiBold);
      group->setFont(font);
      orderedGroups.push_back(group);
    }
    auto* item = new QStandardItem{
      !entry.isProperty
        ? entry.path
        : entry.path + " · "
            + (entry.property.isEmpty() ? tr("(empty key)") : entry.property)};
    item->setData(QVariant::fromValue<qulonglong>(i), EntryRole);
    item->setToolTip(entry.description + "\n" + entry.path);
    if (entry.hidden)
    {
      auto font = item->font();
      font.setItalic(true);
      item->setFont(font);
    }
    group->appendRow(
      {item, new QStandardItem{entry.line ? QString::number(entry.line) : QString{}}});
    ++visible;
  }
  // Build children while detached, then publish each category in one insertion.
  // Large maps must not emit a model insertion and relayout for every diagnostic.
  for (auto* group : orderedGroups)
  {
    const auto type = group->data(TypeRole).toInt();
    group->setText(group->text() + QStringLiteral(" (%1)").arg(group->rowCount()));
    m_model->appendRow({group, new QStandardItem{}});
    if (selection.types.contains(type))
      restoredSelection.select(group->index(), group->index());
    for (int row = 0; row < group->rowCount(); ++row)
    {
      auto* item = group->child(row);
      if (selection.ids.contains(m_entries[item->data(EntryRole).value<size_t>()].id))
        restoredSelection.select(item->index(), item->index());
    }
    const auto previous = m_expansion.find(type);
    m_tree->setExpanded(
      group->index(),
      !query.isEmpty()
        || (previous != m_expansion.end() ? previous->second : group->rowCount() <= 8));
  }
  m_tree->selectionModel()->select(
    restoredSelection, QItemSelectionModel::Select | QItemSelectionModel::Rows);
  m_tree->doItemsLayout();
  m_tree->verticalScrollBar()->setValue(scroll);
  m_rebuilding = false;
  m_status->setText(
    m_valid ? tr("%1 shown / %2 total").arg(visible).arg(m_entries.size())
            : tr("Updating checks…"));
  updateDetails();
}

std::vector<size_t> IssueBrowserView::selectedEntries() const
{
  auto result = std::vector<size_t>{};
  for (const auto& index : m_tree->selectionModel()->selectedRows())
  {
    if (index.parent().isValid())
      result.push_back(index.data(EntryRole).value<size_t>());
    else
      for (int row = 0; row < m_model->rowCount(index); ++row)
        result.push_back(m_model->index(row, 0, index).data(EntryRole).value<size_t>());
  }
  kdl::vec_sort_and_remove_duplicates(result);
  return result;
}
std::vector<const mdl::Issue*> IssueBrowserView::collectIssues() const
{
  auto result = std::vector<const mdl::Issue*>{};
  if (!m_valid)
    return result;
  for (const auto entry : selectedEntries())
    if (const auto it = m_liveIssues.find(m_entries[entry].id); it != m_liveIssues.end())
      result.push_back(it->second);
  return result;
}
std::vector<const mdl::IssueQuickFix*> IssueBrowserView::collectQuickFixes() const
{
  const auto issues = collectIssues();
  if (issues.empty())
    return {};
  auto types = ~static_cast<mdl::IssueType>(0);
  for (const auto* issue : issues)
    types &= issue->type();
  return m_document.map().worldNode().quickFixes(types);
}

void IssueBrowserView::updateDetails()
{
  const auto entries = selectedEntries();
  m_title->setText(
    entries.empty()
      ? (m_valid && m_model->rowCount() == 0 ? tr("No visible issues")
                                             : tr("Select an issue to inspect"))
      : tr("%1 selected issues").arg(entries.size()));
  m_details->setText(
    entries.empty()
      ? tr("Select a row to read details. Double-click or use Locate objects to select "
           "and focus it in the map. Select a category to act on its visible results.")
      : QString{});
  if (m_valid && m_model->rowCount() == 0)
    m_details->setText(
      m_entries.empty()
        ? tr("The current map checks found no issues. This does not replace a compile or "
             "in-game test.")
        : tr("No results match the current search and filters. Clear the search or "
             "adjust Issue types and Show ignored to review other results."));
  if (!entries.empty())
  {
    const auto& entry = m_entries[entries.front()];
    m_title->setText(
      issueTypeLabel(entry.category.toStdString())
      + (entries.size() > 1 ? QStringLiteral(" (%1)").arg(entries.size()) : QString{}));
    const auto sameCategory = std::ranges::all_of(
      entries, [&](const auto i) { return m_entries[i].type == entry.type; });
    if (!sameCategory)
      m_title->setText(tr("%1 selected issues").arg(entries.size()));
    auto text = sameCategory ? explanation(entry.category)
                             : tr(
                                 "Multiple issue types are selected. Select one type to "
                                 "see its explanation and available fixes.");
    if (entries.size() == 1)
    {
      text = entry.path + "\n\n" + text + "\n\n"
             + tr("Original diagnostic: %1").arg(entry.description);
      if (entry.isProperty)
        text += "\n\n" + tr("Property: %1\nValue: %2").arg(entry.property, entry.value);
      if (entry.hidden)
        text += "\n\n" + tr("Ignored for this session on this object and issue type.");
    }
    else
      text += "\n\n"
              + tr(
                "Actions apply only to the selected, currently visible results. Ignoring "
                "applies to each object's issue type for this session.");
    m_details->setText(text);
  }
  const auto active = m_valid && !entries.empty();
  m_locate->setEnabled(active);
  m_editProperty->setEnabled(
    active && entries.size() == 1 && m_entries[entries.front()].isProperty);
  m_fix->setEnabled(active && !collectQuickFixes().empty());
  m_ignore->setEnabled(active && std::ranges::any_of(entries, [&](const auto i) {
                         return !m_entries[i].hidden;
                       }));
  m_restore->setEnabled(active && std::ranges::any_of(entries, [&](const auto i) {
                          return m_entries[i].hidden;
                        }));
}

bool IssueBrowserView::selectObjects(const std::vector<const mdl::Issue*>& issues)
{
  if (issues.empty())
    return false;
  auto nodes = std::vector<mdl::Node*>{};
  for (const auto* issue : issues)
    issue->addSelectableNodes(nodes);
  kdl::vec_sort_and_remove_duplicates(nodes);
  auto& map = m_document.map();
  if (std::ranges::any_of(
        nodes, [&](const auto* node) { return !map.editorContext().selectable(*node); }))
  {
    m_result->setText(
      tr("Some affected objects are hidden, locked or inside a closed group. Make them "
         "selectable before locating or fixing them."));
    return false;
  }
  mdl::deselectAll(map);
  mdl::selectNodes(map, nodes);
  return true; // An empty selection intentionally exposes worldspawn properties.
}
void IssueBrowserView::locateSelection()
{
  m_result->clear();
  const auto issues = collectIssues();
  if (issues.empty())
    return;
  auto& map = m_document.map();
  auto transaction = mdl::Transaction{map, "Locate Issues"};
  if (!selectObjects(issues))
  {
    transaction.cancel();
    return;
  }
  const auto* face = issues.size() == 1
                       ? dynamic_cast<const mdl::BrushFaceIssue*>(issues.front())
                       : nullptr;
  if (face)
  {
    auto& brush = static_cast<mdl::BrushNode&>(face->node());
    mdl::deselectAll(map);
    mdl::selectBrushFaces(map, {{brush, face->faceIndex()}});
  }
  transaction.commit();
  if (map.selection().hasAny())
    emit focusObjectsRequested(face != nullptr);
  else
    m_result->setText(
      tr("This diagnostic concerns map properties or an object without selectable "
         "geometry. Use the scene property editor to inspect it."));
}
void IssueBrowserView::setIssueVisibility(const bool show)
{
  const auto issues = collectIssues();
  if (issues.empty())
    return;
  for (const auto* issue : issues)
    m_document.map().setIssueHidden(*issue, !show);
  reload();
  validate();
  m_result->setText(
    show ? tr("Restored for this session.")
         : tr("Ignored on the affected objects for this session only."));
}
void IssueBrowserView::populateFixMenu(QMenu& menu)
{
  menu.clear();
  for (const auto* fix : collectQuickFixes())
  {
    const auto description = fix->description();
    menu.addAction(
      fixLabel(description), this, [this, description] { applyQuickFix(description); });
  }
}
void IssueBrowserView::applyQuickFix(const std::string& description)
{
  auto expectedIds = std::vector<size_t>{};
  for (const auto* issue : collectIssues())
    expectedIds.push_back(issue->seqId());
  const auto count = expectedIds.size();
  if (!count)
    return;
  const auto answer = QMessageBox::question(
    this,
    tr("Fix selected issues"),
    tr("Apply “%1” to %2 selected issues? This can change or remove map data. You can "
       "undo this action.")
      .arg(fixLabel(description))
      .arg(count));
  if (answer != QMessageBox::Yes)
    return;
  // A modal confirmation can process document changes. Resolve again afterwards.
  const auto fixes = collectQuickFixes();
  const auto it = std::ranges::find_if(
    fixes, [&](const auto* fix) { return fix->description() == description; });
  const auto issues = collectIssues();
  if (it == fixes.end() || issues.empty())
    return;
  auto currentIds = std::vector<size_t>{};
  for (const auto* issue : issues)
    currentIds.push_back(issue->seqId());
  if (currentIds != expectedIds)
    return;
  const auto label = fixLabel(description);
  auto& map = m_document.map();
  auto transaction = mdl::Transaction{map, "Apply Quick Fix (" + description + ")"};
  if (!selectObjects(issues))
  {
    transaction.cancel();
    return;
  }
  const auto selectedCount = issues.size();
  const auto previousModificationCount = map.modificationCount();
  reload();
  (*it)->apply(map, issues);
  const auto committed = transaction.commit();
  const auto changed = map.modificationCount() != previousModificationCount;
  reload();
  validate();
  const auto result =
    tr("Ran “%1” for %2 issues. Checks refreshed: %3 issues remain in the map.")
      .arg(label)
      .arg(selectedCount)
      .arg(m_entries.size());
  auto feedback = QString{};
  if (!committed)
    feedback = tr("The fix was rolled back. Check the output log.");
  else if (changed)
    feedback = tr("Changes can be undone.");
  else
    feedback =
      tr("No map data changed. Review the remaining results and the output log.");
  m_result->setText(result + " " + feedback);
}
void IssueBrowserView::validate()
{
  if (m_valid)
    return;
  const auto selection = selectionState();
  updateIssues();
  m_valid = true;
  rebuildModel(selection);
}
} // namespace tb::ui
