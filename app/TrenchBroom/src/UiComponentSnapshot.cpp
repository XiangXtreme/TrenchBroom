/*
 Copyright (C) 2026 Kristian Duske
 Copyright (C) 2026 XiangXtreme

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

#include "UiComponentSnapshot.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "ui/CollapsibleTitledPanel.h"
#include "ui/PanelLayout.h"
#include "ui/QStyleUtils.h"
#include "ui/TaskNotification.h"
#include "ui/WorkbenchSurface.h"

#include <initializer_list>

namespace tb::ui
{
namespace
{

QWidget* makeControlRow(const std::initializer_list<QWidget*> controls)
{
  auto* row = new QWidget{};
  auto* layout = createPanelActions(row);
  for (auto* control : controls)
  {
    layout->addWidget(control);
  }
  layout->addStretch(1);
  return row;
}

QGroupBox* makeSection(const QString& title)
{
  auto* section = new QGroupBox{title};
  createPanelForm(section, PanelInsets::Content);
  return section;
}

QFormLayout* sectionLayout(QGroupBox& section)
{
  return static_cast<QFormLayout*>(section.layout());
}

} // namespace

std::unique_ptr<QWidget> createUiComponentSnapshot()
{
  auto result = std::make_unique<QWidget>();
  result->setObjectName(QStringLiteral("UiComponentSnapshot"));
  result->setWindowTitle(QStringLiteral("TrenchBroom UI Components"));

  auto* title = new QLabel{QStringLiteral("Foundational Components")};
  auto titleFont = title->font();
  titleFont.setWeight(QFont::DemiBold);
  titleFont.setPointSize(titleFont.pointSize() + 3);
  title->setFont(titleFont);

  auto* subtitle = new QLabel{QStringLiteral(
    "Canonical normal, editable, read-only, selected, and disabled states")};
  subtitle->setObjectName(QStringLiteral("UiComponentSnapshot_Subtitle"));

  auto* inputSection = makeSection(QStringLiteral("Input"));
  auto* inputLayout = sectionLayout(*inputSection);

  auto* focusedLineEdit = new QLineEdit{QStringLiteral("Focused value")};
  focusedLineEdit->setObjectName(QStringLiteral("UiComponentSnapshot_FocusedInput"));
  auto* readOnlyLineEdit = new QLineEdit{QStringLiteral("Read-only value")};
  readOnlyLineEdit->setReadOnly(true);
  auto* disabledLineEdit = new QLineEdit{QStringLiteral("Disabled value")};
  disabledLineEdit->setEnabled(false);
  inputLayout->addRow(
    QStringLiteral("Text"),
    makeControlRow({focusedLineEdit, readOnlyLineEdit, disabledLineEdit}));

  auto* info = new QLabel{QStringLiteral("Applies immediately")};
  setInfoStyle(info);
  auto* error = new QLabel{QStringLiteral("Path unavailable")};
  setErrorStyle(error);
  auto* disabledInfo = new QLabel{QStringLiteral("Unavailable")};
  setInfoStyle(disabledInfo);
  disabledInfo->setEnabled(false);
  inputLayout->addRow(
    QStringLiteral("Feedback"), makeControlRow({info, error, disabledInfo}));

  auto* combo = new QComboBox{};
  combo->addItems(
    {QStringLiteral("Quake"), QStringLiteral("Quake II"), QStringLiteral("Half-Life")});
  auto* editableCombo = new QComboBox{};
  editableCombo->setEditable(true);
  editableCombo->addItems({QStringLiteral("Custom"), QStringLiteral("Inherited")});
  auto* disabledCombo = new QComboBox{};
  disabledCombo->addItem(QStringLiteral("Disabled"));
  disabledCombo->setEnabled(false);
  inputLayout->addRow(
    QStringLiteral("Choice"), makeControlRow({combo, editableCombo, disabledCombo}));

  auto* spinBox = new QSpinBox{};
  spinBox->setRange(-64, 64);
  spinBox->setValue(16);
  auto* doubleSpinBox = new QDoubleSpinBox{};
  doubleSpinBox->setRange(0.0, 360.0);
  doubleSpinBox->setValue(45.0);
  doubleSpinBox->setSuffix(QStringLiteral(" deg"));
  auto* disabledSpinBox = new QSpinBox{};
  disabledSpinBox->setValue(8);
  disabledSpinBox->setEnabled(false);
  inputLayout->addRow(
    QStringLiteral("Number"), makeControlRow({spinBox, doubleSpinBox, disabledSpinBox}));

  auto* vectorRows = new QWidget{};
  auto* vectorLayout = new QHBoxLayout{vectorRows};
  vectorLayout->setContentsMargins(0, 0, 0, 0);
  for (int column = 0; column < 3; ++column)
  {
    auto* channels = new QVBoxLayout{};
    channels->setSpacing(0);
    for (int axis = 0; axis < 3; ++axis)
    {
      auto* channel = new QDoubleSpinBox{};
      channel->setProperty("tbControlRole", "vector");
      channel->setProperty(
        "tbVectorPosition",
        axis == 0   ? "first"
        : axis == 2 ? "last"
                    : "middle");
      channel->setAlignment(Qt::AlignCenter);
      channel->setButtonSymbols(QAbstractSpinBox::NoButtons);
      channel->setDecimals(3);
      channel->setValue(1.0);
      channel->setEnabled(column != 2);
      channel->setProperty("multiSelected", column == 1);
      channels->addWidget(channel);
    }
    vectorLayout->addLayout(channels);
  }
  inputLayout->addRow(QStringLiteral("Vector"), vectorRows);

  auto* plainTextEdit =
    new QPlainTextEdit{QStringLiteral("Multi-line input\nSecond line")};
  plainTextEdit->setFixedHeight(64);
  auto* readOnlyTextEdit = new QTextEdit{};
  readOnlyTextEdit->setPlainText(QStringLiteral("Read-only output\nSecond line"));
  readOnlyTextEdit->setReadOnly(true);
  readOnlyTextEdit->setFixedHeight(64);
  inputLayout->addRow(
    QStringLiteral("Text area"), makeControlRow({plainTextEdit, readOnlyTextEdit}));

  auto* selectionSection = makeSection(QStringLiteral("Selection"));
  auto* selectionLayout = sectionLayout(*selectionSection);

  auto* unchecked = new QCheckBox{QStringLiteral("Unchecked")};
  auto* checked = new QCheckBox{QStringLiteral("Checked")};
  checked->setChecked(true);
  auto* disabledChecked = new QCheckBox{QStringLiteral("Disabled")};
  disabledChecked->setChecked(true);
  disabledChecked->setEnabled(false);
  selectionLayout->addRow(
    QStringLiteral("Checkbox"), makeControlRow({unchecked, checked, disabledChecked}));

  auto* radioOff = new QRadioButton{QStringLiteral("Option A")};
  auto* radioOn = new QRadioButton{QStringLiteral("Option B")};
  radioOn->setChecked(true);
  auto* radioDisabled = new QRadioButton{QStringLiteral("Disabled")};
  radioDisabled->setEnabled(false);
  selectionLayout->addRow(
    QStringLiteral("Radio"), makeControlRow({radioOff, radioOn, radioDisabled}));

  auto* slider = new QSlider{Qt::Horizontal};
  slider->setRange(0, 100);
  slider->setValue(60);
  slider->setMinimumWidth(220);
  auto* disabledSlider = new QSlider{Qt::Horizontal};
  disabledSlider->setRange(0, 100);
  disabledSlider->setValue(35);
  disabledSlider->setMinimumWidth(160);
  disabledSlider->setEnabled(false);
  selectionLayout->addRow(
    QStringLiteral("Range"), makeControlRow({slider, disabledSlider}));

  auto* actionSection = makeSection(QStringLiteral("Commands"));
  auto* actionLayout = sectionLayout(*actionSection);
  auto* primaryButton = new QPushButton{QStringLiteral("Apply")};
  primaryButton->setDefault(true);
  auto* button = new QPushButton{QStringLiteral("Reset")};
  auto* disabledButton = new QPushButton{QStringLiteral("Disabled")};
  disabledButton->setEnabled(false);
  actionLayout->addRow(
    QStringLiteral("Button"), makeControlRow({primaryButton, button, disabledButton}));

  auto* toolButton = new QToolButton{};
  toolButton->setIcon(result->style()->standardIcon(QStyle::SP_BrowserReload));
  toolButton->setToolTip(QStringLiteral("Reload"));
  auto* checkedToolButton = new QToolButton{};
  checkedToolButton->setIcon(result->style()->standardIcon(QStyle::SP_DialogApplyButton));
  checkedToolButton->setCheckable(true);
  checkedToolButton->setChecked(true);
  checkedToolButton->setToolTip(QStringLiteral("Enabled"));
  auto* disabledToolButton = new QToolButton{};
  disabledToolButton->setIcon(
    result->style()->standardIcon(QStyle::SP_DialogCancelButton));
  disabledToolButton->setEnabled(false);
  auto* outlinedToolButton = new QToolButton{};
  outlinedToolButton->setIcon(
    result->style()->standardIcon(QStyle::SP_FileDialogDetailedView));
  outlinedToolButton->setProperty("tbControlRole", "outlined");
  outlinedToolButton->setToolTip(QStringLiteral("Outlined command"));
  actionLayout->addRow(
    QStringLiteral("Tool"),
    makeControlRow(
      {toolButton, checkedToolButton, outlinedToolButton, disabledToolButton}));

  // A labelled command keeps a persistent boundary when the word is what explains it,
  // for example a filter or view disclosure in a browser toolbar.
  auto* chipButton = new QToolButton{};
  chipButton->setText(QStringLiteral("Filters"));
  chipButton->setIcon(result->style()->standardIcon(QStyle::SP_FileDialogListView));
  chipButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  chipButton->setProperty("tbControlRole", "chip");
  auto* checkedChipButton = new QToolButton{};
  checkedChipButton->setText(QStringLiteral("View"));
  checkedChipButton->setIcon(
    result->style()->standardIcon(QStyle::SP_FileDialogDetailedView));
  checkedChipButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  checkedChipButton->setProperty("tbControlRole", "chip");
  checkedChipButton->setCheckable(true);
  checkedChipButton->setChecked(true);
  actionLayout->addRow(
    QStringLiteral("Labelled"), makeControlRow({chipButton, checkedChipButton}));

  auto* navigation = new QToolButton{};
  navigation->setText(QStringLiteral("Inspector"));
  navigation->setProperty("tbControlRole", "navigation");
  auto* activeNavigation = new QToolButton{};
  activeNavigation->setText(QStringLiteral("Console"));
  activeNavigation->setProperty("tbControlRole", "navigation");
  activeNavigation->setCheckable(true);
  activeNavigation->setChecked(true);
  auto* disabledNavigation = new QToolButton{};
  disabledNavigation->setText(QStringLiteral("Unavailable"));
  disabledNavigation->setProperty("tbControlRole", "navigation");
  disabledNavigation->setEnabled(false);
  actionLayout->addRow(
    QStringLiteral("Navigation"),
    makeControlRow({navigation, activeNavigation, disabledNavigation}));

  auto* drawerTab = new QToolButton{};
  auto* activeDrawerTab = new QToolButton{};
  auto* disabledDrawerTab = new QToolButton{};
  for (auto* tab : {drawerTab, activeDrawerTab, disabledDrawerTab})
  {
    tab->setProperty("tbControlRole", "navigation");
    tab->setCheckable(true);
  }
  drawerTab->setText(QStringLiteral("Assets"));
  activeDrawerTab->setText(QStringLiteral("Console"));
  activeDrawerTab->setChecked(true);
  disabledDrawerTab->setText(QStringLiteral("Unavailable"));
  disabledDrawerTab->setEnabled(false);
  auto* drawerTabs = makeControlRow({drawerTab, activeDrawerTab, disabledDrawerTab});
  drawerTabs->setObjectName("ProjectToolBar");
  drawerTabs->setAttribute(Qt::WA_StyledBackground);
  actionLayout->addRow(QStringLiteral("Drawer tabs"), drawerTabs);

  auto* viewSelector = new QToolButton{};
  viewSelector->setText(QStringLiteral("Perspective (3D)"));
  viewSelector->setProperty("tbControlRole", "overlay");
  viewSelector->setPopupMode(QToolButton::InstantPopup);
  auto* viewTypeMenu = new QMenu{viewSelector};
  viewTypeMenu->addAction(QStringLiteral("Perspective (3D)"))->setCheckable(true);
  viewTypeMenu->actions().front()->setChecked(true);
  viewTypeMenu->addAction(QStringLiteral("Top (XY)"));
  viewSelector->setMenu(viewTypeMenu);
  auto* card = new WorkbenchSurface{
    QStringLiteral("MapWindow_InspectorSurface"), makeControlRow({viewSelector})};
  actionLayout->addRow(QStringLiteral("View card"), card);

  auto* collectionSection = makeSection(QStringLiteral("Collections and navigation"));
  auto* collectionLayout = sectionLayout(*collectionSection);

  auto* tree = new QTreeWidget{};
  tree->setHeaderLabels({QStringLiteral("Scene")});
  auto* worldItem = new QTreeWidgetItem{tree, QStringList{QStringLiteral("Worldspawn")}};
  auto* layerItem =
    new QTreeWidgetItem{worldItem, QStringList{QStringLiteral("Gameplay")}};
  new QTreeWidgetItem{layerItem, QStringList{QStringLiteral("func_detail")}};
  auto* lighting =
    new QTreeWidgetItem{worldItem, QStringList{QStringLiteral("Lighting")}};
  new QTreeWidgetItem{lighting, QStringList{QStringLiteral("light")}};
  tree->expandAll();
  lighting->setExpanded(false);
  tree->setCurrentItem(layerItem);
  tree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

  auto* list = new QListWidget{};
  list->addItems(
    {QStringLiteral("Concrete"),
     QStringLiteral("Metal"),
     QStringLiteral("Glass"),
     QStringLiteral("Water"),
     QStringLiteral("Sky"),
     QStringLiteral("Trigger")});
  list->setAlternatingRowColors(true);
  list->setCurrentRow(1);
  list->item(5)->setFlags(list->item(5)->flags() & ~Qt::ItemIsEnabled);
  list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

  auto* navigationList = new QListWidget{};
  navigationList->setProperty("tbControlRole", "navigation");
  navigationList->setSpacing(2);
  navigationList->addItems(
    {QStringLiteral("Scene"), QStringLiteral("Face"), QStringLiteral("Plugins")});
  navigationList->setCurrentRow(0);
  navigationList->item(2)->setFlags(
    navigationList->item(2)->flags() & ~Qt::ItemIsEnabled);

  auto* collectionSplitter = new QSplitter{Qt::Horizontal};
  collectionSplitter->setChildrenCollapsible(false);
  collectionSplitter->addWidget(tree);
  collectionSplitter->addWidget(list);
  collectionSplitter->addWidget(navigationList);
  collectionSplitter->setMinimumWidth(420);
  collectionSplitter->setFixedHeight(126);
  collectionLayout->addRow(QStringLiteral("Views"), collectionSplitter);

  auto* table = new QTableWidget{3, 2};
  table->setHorizontalHeaderLabels({QStringLiteral("Property"), QStringLiteral("Value")});
  table->setItem(0, 0, new QTableWidgetItem{QStringLiteral("classname")});
  table->setItem(0, 1, new QTableWidgetItem{QStringLiteral("light")});
  table->setItem(1, 0, new QTableWidgetItem{QStringLiteral("origin")});
  table->setItem(1, 1, new QTableWidgetItem{QStringLiteral("128 64 32")});
  table->setItem(2, 0, new QTableWidgetItem{QStringLiteral("style")});
  auto* disabledTableItem = new QTableWidgetItem{QStringLiteral("Disabled")};
  disabledTableItem->setFlags(disabledTableItem->flags() & ~Qt::ItemIsEnabled);
  table->setItem(2, 1, disabledTableItem);
  table->setAlternatingRowColors(true);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->selectRow(1);
  table->horizontalHeader()->setStretchLastSection(true);
  table->verticalHeader()->hide();
  table->setFixedHeight(108);
  collectionLayout->addRow(QStringLiteral("Table"), table);

  auto* menuBar = new QMenuBar{};
  menuBar->setNativeMenuBar(false);
  auto* fileMenu = menuBar->addMenu(QStringLiteral("File"));
  fileMenu->addAction(QStringLiteral("Open"));
  fileMenu->addAction(QStringLiteral("Save"));
  auto* viewMenu = menuBar->addMenu(QStringLiteral("View"));
  auto* checkedMenuAction = viewMenu->addAction(QStringLiteral("Show grid"));
  checkedMenuAction->setCheckable(true);
  checkedMenuAction->setChecked(true);
  viewMenu->addSeparator();
  auto* disabledMenuAction = viewMenu->addAction(QStringLiteral("Unavailable"));
  disabledMenuAction->setEnabled(false);
  collectionLayout->addRow(QStringLiteral("Menu"), menuBar);

  auto* tabs = new QTabWidget{};
  tabs->addTab(new QLabel{QStringLiteral("General content")}, QStringLiteral("General"));
  tabs->addTab(
    new QLabel{QStringLiteral("Advanced content")}, QStringLiteral("Advanced"));
  tabs->addTab(
    new QLabel{QStringLiteral("Disabled content")}, QStringLiteral("Disabled"));
  tabs->tabBar()->setTabEnabled(2, false);
  tabs->setFixedHeight(76);
  collectionLayout->addRow(QStringLiteral("Tabs"), tabs);

  auto* horizontalScrollBar = new QScrollBar{Qt::Horizontal};
  horizontalScrollBar->setRange(0, 100);
  horizontalScrollBar->setValue(35);
  horizontalScrollBar->setMinimumWidth(260);
  auto* verticalScrollBar = new QScrollBar{Qt::Vertical};
  verticalScrollBar->setRange(0, 100);
  verticalScrollBar->setValue(60);
  verticalScrollBar->setFixedHeight(54);
  collectionLayout->addRow(
    QStringLiteral("Scroll"), makeControlRow({horizontalScrollBar, verticalScrollBar}));

  auto* layout = new QVBoxLayout{result.get()};
  layout->setContentsMargins(18, 16, 18, 18);
  layout->setSpacing(12);
  layout->addWidget(title);
  layout->addWidget(subtitle);

  auto* columns = new QHBoxLayout{};
  columns->setContentsMargins(0, 0, 0, 0);
  columns->setSpacing(12);

  auto* leftColumn = new QVBoxLayout{};
  leftColumn->setContentsMargins(0, 0, 0, 0);
  leftColumn->setSpacing(12);
  leftColumn->addWidget(inputSection);
  leftColumn->addWidget(selectionSection);
  auto* disclosureSection = makeSection(QStringLiteral("Collapsible sections"));
  auto* expandedPanel = new CollapsibleTitledPanel{QStringLiteral("Terrain properties")};
  expandedPanel->setPresentation(CollapsibleTitledPanel::Presentation::Card);
  auto* expandedLayout = createPanelForm();
  expandedPanel->createContentLayout()->addLayout(expandedLayout);
  auto* titleAction = new QToolButton{};
  titleAction->setText(QStringLiteral("⋯"));
  titleAction->setProperty("tbControlRole", "compact");
  expandedPanel->addTitleWidget(titleAction);
  expandedLayout->addRow(QStringLiteral("Radius"), new QSpinBox{});
  auto* collapsedPanel =
    new CollapsibleTitledPanel{QStringLiteral("Compile settings"), false};
  collapsedPanel->setPresentation(CollapsibleTitledPanel::Presentation::Card);
  auto* disabledPanel =
    new CollapsibleTitledPanel{QStringLiteral("Unavailable section"), false};
  disabledPanel->setPresentation(CollapsibleTitledPanel::Presentation::Card);
  disabledPanel->setEnabled(false);
  sectionLayout(*disclosureSection)->addRow(expandedPanel);
  sectionLayout(*disclosureSection)->addRow(collapsedPanel);
  sectionLayout(*disclosureSection)->addRow(disabledPanel);
  sectionLayout(*disclosureSection)
    ->addRow(new CollapsibleTitledPanel{QStringLiteral("Embedded section"), false});
  leftColumn->addWidget(disclosureSection);
  leftColumn->addStretch(1);

  auto* rightColumn = new QVBoxLayout{};
  rightColumn->setContentsMargins(0, 0, 0, 0);
  rightColumn->setSpacing(12);
  rightColumn->addWidget(actionSection);
  rightColumn->addWidget(collectionSection);
  auto* notificationSection = makeSection(QStringLiteral("Task notifications"));
  auto* progress = new QProgressBar{};
  progress->setValue(65);
  sectionLayout(*notificationSection)->addRow(QStringLiteral("Progress"), progress);
  auto* runningCard = new TaskNotificationCard{};
  auto running = TaskNotification{};
  running.title = QStringLiteral("Baking lightmap");
  running.detail = QStringLiteral("Calculating visibility · 42%");
  running.progress = 42;
  running.actions.push_back({QStringLiteral("Cancel"), {}});
  runningCard->setNotification(running);
  auto* failedCard = new TaskNotificationCard{};
  auto failed = TaskNotification{};
  failed.state = TaskNotification::State::Failed;
  failed.title = QStringLiteral("Lightmap bake failed");
  failed.detail = QStringLiteral("The map is not sealed.");
  failed.dismissible = true;
  failed.actions.push_back({QStringLiteral("View log…"), {}});
  failed.actions.push_back({QStringLiteral("Retry"), {}});
  failedCard->setNotification(failed);
  sectionLayout(*notificationSection)->addRow(runningCard);
  sectionLayout(*notificationSection)->addRow(failedCard);
  rightColumn->addWidget(notificationSection);
  rightColumn->addStretch(1);

  columns->addLayout(leftColumn, 1);
  columns->addLayout(rightColumn, 1);
  layout->addLayout(columns, 1);
  layout->addStretch(1);

  focusedLineEdit->setFocus(Qt::OtherFocusReason);
  focusedLineEdit->selectAll();
  return result;
}

} // namespace tb::ui
