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

#include "ui/LayerEditor.h"

#include <QMenu>
#include <QToolButton>
#include <QVBoxLayout>

#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Layers.h"
#include "mdl/Map_NodeLocking.h"
#include "mdl/Map_NodeVisibility.h"
#include "ui/BitmapButton.h"
#include "ui/BorderLine.h"
#include "ui/LayerActions.h"
#include "ui/LayerListBox.h"
#include "ui/MapDocument.h"
#include "ui/ViewUtils.h"

#include "kd/contracts.h"

#include <vector>

namespace tb::ui
{

LayerEditor::LayerEditor(MapDocument& document, QWidget* parent)
  : QWidget{parent}
  , m_document{document}
{
  createGui();

  updateButtons();
}

void LayerEditor::onSetCurrentLayer(mdl::LayerNode* layerNode)
{
  setCurrentLayer(m_document.map(), layerNode);

  updateButtons();
}

void LayerEditor::onLayerRightClick(mdl::LayerNode* layerNode)
{
  auto menu = QMenu{this};
  populateLayerActions(menu, m_document.map(), layerNode);
  menu.exec(QCursor::pos());
  updateButtons();
}

void LayerEditor::toggleLayerVisible(mdl::LayerNode* layerNode)
{
  contract_pre(layerNode != nullptr);

  auto& map = m_document.map();
  if (!layerNode->hidden())
  {
    hideNodes(map, std::vector<mdl::Node*>{layerNode});
  }
  else
  {
    resetNodeVisibility(map, std::vector<mdl::Node*>{layerNode});
  }
}

void LayerEditor::toggleLayerLocked(mdl::LayerNode* layerNode)
{
  contract_pre(layerNode != nullptr);

  auto& map = m_document.map();
  if (!layerNode->locked())
  {
    lockNodes(map, std::vector<mdl::Node*>{layerNode});
  }
  else
  {
    resetNodeLockingState(map, std::vector<mdl::Node*>{layerNode});
  }
}

void LayerEditor::toggleOmitLayerFromExport(mdl::LayerNode* layerNode)
{
  contract_pre(layerNode != nullptr);

  setOmitLayerFromExport(
    m_document.map(), layerNode, !layerNode->layer().omitFromExport());
}

void LayerEditor::onAddLayer()
{
  const auto name = queryLayerName(this, "Unnamed");
  if (name.empty())
  {
    return;
  }

  if (auto* layerNode = mdl::createLayer(m_document.map(), name))
  {
    m_layerList->setSelectedLayer(layerNode);
    updateButtons();
  }
}

void LayerEditor::onRemoveLayer()
{
  if (ui::removeLayer(m_document.map(), m_layerList->selectedLayer()))
    updateButtons();
}

bool LayerEditor::canRemoveLayer() const
{
  return ui::canRemoveLayer(m_document.map(), m_layerList->selectedLayer());
}

bool LayerEditor::canMoveLayer(const int direction) const
{
  if (auto* layerNode = m_layerList->selectedLayer(); layerNode && direction != 0)
  {
    return mdl::canMoveLayer(m_document.map(), layerNode, direction);
  }
  return false;
}

void LayerEditor::moveLayer(mdl::LayerNode* layerNode, const int direction)
{
  contract_pre(direction == 0 || layerNode != nullptr);

  if (direction != 0)
  {
    mdl::moveLayer(m_document.map(), layerNode, direction);
  }
}

void LayerEditor::createGui()
{
  m_layerList = new LayerListBox{m_document, this};
  connect(
    m_layerList, &LayerListBox::layerSetCurrent, this, &LayerEditor::onSetCurrentLayer);
  connect(
    m_layerList, &LayerListBox::layerRightClicked, this, &LayerEditor::onLayerRightClick);
  connect(
    m_layerList,
    &LayerListBox::layerOmitFromExportToggled,
    this,
    &LayerEditor::toggleOmitLayerFromExport);
  connect(m_layerList, &LayerListBox::layerVisibilityToggled, this, [&](auto* layer) {
    toggleLayerVisible(layer);
  });
  connect(m_layerList, &LayerListBox::layerLockToggled, this, [&](auto* layer) {
    toggleLayerLocked(layer);
  });
  connect(
    m_layerList, &LayerListBox::itemSelectionChanged, this, &LayerEditor::updateButtons);

  m_addLayerButton = createBitmapButton("Add.svg", tr("Add a new layer"));
  m_removeLayerButton = createBitmapButton(
    "Remove.svg",
    tr("Remove the selected layer and move its objects to the default layer"));
  m_moveLayerUpButton = createBitmapButton("Up.svg", "Move the selected layer up");
  m_moveLayerDownButton = createBitmapButton("Down.svg", "Move the selected layer down");

  connect(m_addLayerButton, &QAbstractButton::pressed, this, &LayerEditor::onAddLayer);
  connect(
    m_removeLayerButton, &QAbstractButton::pressed, this, &LayerEditor::onRemoveLayer);
  connect(m_moveLayerUpButton, &QAbstractButton::pressed, this, [&]() {
    moveLayer(m_layerList->selectedLayer(), -1);
  });
  connect(m_moveLayerDownButton, &QAbstractButton::pressed, this, [&]() {
    moveLayer(m_layerList->selectedLayer(), 1);
  });

  auto* buttonSizer = new QHBoxLayout{};
  buttonSizer->addWidget(m_addLayerButton);
  buttonSizer->addWidget(m_removeLayerButton);
  buttonSizer->addWidget(m_moveLayerUpButton);
  buttonSizer->addWidget(m_moveLayerDownButton);
  buttonSizer->addStretch(1);

  auto* sizer = new QVBoxLayout{};
  sizer->setContentsMargins(0, 0, 0, 0);
  sizer->setSpacing(0);
  sizer->addWidget(m_layerList, 1);
  sizer->addWidget(new BorderLine(BorderLine::Direction::Horizontal), 0);
  sizer->addLayout(buttonSizer, 0);
  setLayout(sizer);
}

void LayerEditor::updateButtons()
{
  m_removeLayerButton->setEnabled(canRemoveLayer());
  m_moveLayerUpButton->setEnabled(canMoveLayer(-1));
  m_moveLayerDownButton->setEnabled(canMoveLayer(1));
}

} // namespace tb::ui
