/*
 Copyright (C) 2023 Kristian Duske

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

#include "ui/SmartWadEditor.h"

#include <QFileDialog>
#include <QListWidget>
#include <QSignalBlocker>
#include <QToolButton>

#include "mdl/EntityNodeBase.h"
#include "mdl/Map_Assets.h"
#include "mdl/Map_World.h"
#include "mdl/WadPropertyUtils.h"
#include "ui/BitmapButton.h"
#include "ui/BorderLine.h"
#include "ui/ChoosePathTypeDialog.h"
#include "ui/FileDialogDefaultDir.h"
#include "ui/MapDocument.h"
#include "ui/MiniToolBarLayout.h"
#include "ui/PanelLayout.h"
#include "ui/TitleBar.h"
#include "ui/ViewConstants.h"
#include "ui/WadUtils.h"

#include "kd/vector_utils.h"

#include <algorithm>
#include <filesystem>
#include <iterator>

namespace tb::ui
{

namespace
{

std::vector<std::string> getWadPaths(
  const std::vector<mdl::EntityNodeBase*>& nodes, const std::string& propertyKey)
{
  if (nodes.size() == 1)
  {
    if (const auto* wadPathsStr = nodes.front()->entity().property(propertyKey))
    {
      return mdl::splitWadProperty(*wadPathsStr);
    }
  }
  return {};
}

std::string getWadPathStr(const std::vector<std::string>& wadPaths)
{
  return mdl::joinWadProperty(wadPaths);
}

} // namespace

SmartWadEditor::SmartWadEditor(MapDocument& document, QWidget* parent)
  : SmartPropertyEditor{document, parent}
{
  auto* header = new TitleBar{tr("WAD files")};

  m_wadPaths = new PanelItemList{4, 6};
  m_wadPaths->setObjectName("SmartWadEditor_Paths");
  m_wadPaths->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_wadPaths->setTextElideMode(Qt::ElideMiddle);

  m_addWadsButton = createBitmapButton("Add.svg", tr("Add WAD files"));
  m_removeWadsButton = createBitmapButton("Remove.svg", tr("Remove selected WAD files"));
  m_moveWadUpButton = createBitmapButton("Up.svg", tr("Move selected WAD file up"));
  m_moveWadDownButton = createBitmapButton("Down.svg", tr("Move selected WAD file down"));
  m_reloadWadsButton = createBitmapButton("Refresh.svg", tr("Reload WAD files"));
  for (auto* button :
       {m_addWadsButton,
        m_removeWadsButton,
        m_moveWadUpButton,
        m_moveWadDownButton,
        m_reloadWadsButton})
  {
    button->setProperty("tbControlRole", "compact");
    button->setAccessibleName(button->toolTip());
  }

  auto* toolBar = createMiniToolBarLayout(
    m_addWadsButton,
    m_removeWadsButton,
    LayoutConstants::WideHMargin,
    m_moveWadUpButton,
    m_moveWadDownButton,
    LayoutConstants::WideHMargin,
    m_reloadWadsButton);

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(header);
  layout->addWidget(m_wadPaths, 1);
  layout->addWidget(new BorderLine{}, 0);
  layout->addLayout(toolBar, 0);

  setLayout(layout);

  connect(
    m_wadPaths, &QListWidget::itemSelectionChanged, this, &SmartWadEditor::updateButtons);

  connect(m_addWadsButton, &QAbstractButton::clicked, this, &SmartWadEditor::addWads);
  connect(
    m_removeWadsButton,
    &QAbstractButton::clicked,
    this,
    &SmartWadEditor::removeSelectedWads);
  connect(
    m_moveWadUpButton,
    &QAbstractButton::clicked,
    this,
    &SmartWadEditor::moveSelectedWadsUp);
  connect(
    m_moveWadDownButton,
    &QAbstractButton::clicked,
    this,
    &SmartWadEditor::moveSelectedWadsDown);
  connect(
    m_reloadWadsButton, &QAbstractButton::clicked, this, &SmartWadEditor::reloadWads);

  setAcceptDrops(true);
  updateButtons();
}

void SmartWadEditor::addWads()
{
  const auto pathQStrs = QFileDialog::getOpenFileNames(
    nullptr,
    tr("Load Wad Files"),
    fileDialogDefaultDirectory(FileDialogDir::MaterialCollection),
    tr("Wad files (*.wad);;All files (*.*)"));

  if (pathQStrs.isEmpty())
  {
    return;
  }

  auto& map = document().map();

  updateFileDialogDefaultDirectoryWithFilename(
    FileDialogDir::MaterialCollection, pathQStrs.front());

  if (addWadPaths(pathQStrs, map, window()))
  {
    m_wadPaths->setCurrentRow(
      m_wadPaths->count() - 1, QItemSelectionModel::ClearAndSelect);
  }
}

void SmartWadEditor::removeSelectedWads()
{
  if (!canRemoveWads())
  {
    return;
  }

  auto indicesToRemove = std::vector<size_t>{};
  std::ranges::transform(
    m_wadPaths->selectedItems(),
    std::back_inserter(indicesToRemove),
    [&](const auto* item) { return size_t(m_wadPaths->row(item)); });
  std::ranges::sort(indicesToRemove, std::greater<size_t>{});

  auto wadPaths = getWadPaths(nodes(), propertyKey());
  for (const auto index : indicesToRemove)
  {
    wadPaths = kdl::vec_erase_at(std::move(wadPaths), index);
  }

  setWorldProperty(document().map(), propertyKey(), getWadPathStr(wadPaths));
  m_wadPaths->setCurrentRow(
    std::min(int(indicesToRemove.back()), m_wadPaths->count() - 1),
    QItemSelectionModel::ClearAndSelect);
}

void SmartWadEditor::moveSelectedWadsUp()
{
  if (!canMoveWadsUp())
  {
    return;
  }

  const auto index = size_t(m_wadPaths->currentRow());
  if (index > 0)
  {
    auto wadPaths = getWadPaths(nodes(), propertyKey());

    using std::swap;
    swap(wadPaths[index], wadPaths[index - 1]);

    setWorldProperty(document().map(), propertyKey(), getWadPathStr(wadPaths));
    m_wadPaths->setCurrentRow(int(index) - 1, QItemSelectionModel::ClearAndSelect);
  }
}

void SmartWadEditor::moveSelectedWadsDown()
{
  if (!canMoveWadsDown())
  {
    return;
  }

  auto wadPaths = getWadPaths(nodes(), propertyKey());
  const auto index = size_t(m_wadPaths->currentRow());
  if (index < wadPaths.size() - 1)
  {
    using std::swap;
    swap(wadPaths[index], wadPaths[index + 1]);

    setWorldProperty(document().map(), propertyKey(), getWadPathStr(wadPaths));
    m_wadPaths->setCurrentRow(int(index) + 1, QItemSelectionModel::ClearAndSelect);
  }
}

void SmartWadEditor::reloadWads()
{
  reloadMaterialCollections(document().map());
}

bool SmartWadEditor::canRemoveWads() const
{
  auto selections = std::vector<int>{};
  for (const auto* item : m_wadPaths->selectedItems())
  {
    selections.push_back(m_wadPaths->row(item));
  }

  const auto wadPaths = getWadPaths(nodes(), propertyKey());
  return !selections.empty() && std::ranges::all_of(selections, [&](const auto s) {
    return size_t(s) < wadPaths.size();
  });
}

bool SmartWadEditor::canMoveWadsUp() const
{
  return m_wadPaths->selectedItems().size() == 1 && size_t(m_wadPaths->currentRow()) > 0;
}

bool SmartWadEditor::canMoveWadsDown() const
{
  return m_wadPaths->selectedItems().size() == 1
         && size_t(m_wadPaths->currentRow())
              < getWadPaths(nodes(), propertyKey()).size() - 1;
}

bool SmartWadEditor::canReloadWads() const
{
  return m_wadPaths->count() > 0;
}

void SmartWadEditor::doUpdateVisual(const std::vector<mdl::EntityNodeBase*>& nodes)
{
  const auto paths = getWadPaths(nodes, propertyKey());
  auto displayedPaths = std::vector<std::string>{};
  for (auto i = 0; i < m_wadPaths->count(); ++i)
    displayedPaths.push_back(
      m_wadPaths->item(i)->data(Qt::UserRole).toString().toStdString());
  if (paths == displayedPaths)
  {
    updateButtons();
    return;
  }

  auto cachedSelection = std::vector<std::tuple<int, QString>>{};
  std::ranges::transform(
    m_wadPaths->selectedItems(),
    std::back_inserter(cachedSelection),
    [&](const auto* selectedItem) {
      return std::tuple{
        m_wadPaths->row(selectedItem), selectedItem->data(Qt::UserRole).toString()};
    });

  {
    const auto blocker = QSignalBlocker{m_wadPaths};
    m_wadPaths->clear();

    for (const auto& path : paths)
    {
      const auto fullPath = QString::fromStdString(path);
      auto normalizedPath = fullPath;
      normalizedPath.replace('\\', '/');
      auto* item = new QListWidgetItem{normalizedPath.section('/', -1), m_wadPaths};
      item->setData(Qt::UserRole, fullPath);
      item->setToolTip(fullPath);
    }

    for (const auto& [index, path] : cachedSelection)
    {
      if (
        index < m_wadPaths->count()
        && m_wadPaths->item(index)->data(Qt::UserRole).toString() == path)
      {
        m_wadPaths->setCurrentRow(index, QItemSelectionModel::Select);
      }
      else
      {
        m_wadPaths->clearSelection();
        break;
      }
    }
  }
  updateButtons();
}

void SmartWadEditor::updateButtons()
{
  m_removeWadsButton->setEnabled(canRemoveWads());
  m_moveWadUpButton->setEnabled(canMoveWadsUp());
  m_moveWadDownButton->setEnabled(canMoveWadsDown());
  m_reloadWadsButton->setEnabled(canReloadWads());
}

} // namespace tb::ui
