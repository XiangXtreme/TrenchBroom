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

#pragma once

#include <QWidget>

class QAbstractButton;

namespace tb
{
namespace mdl
{
class LayerNode;
} // namespace mdl

namespace ui
{
class LayerListBox;
class MapDocument;

class LayerEditor : public QWidget
{
  Q_OBJECT
private:
  MapDocument& m_document;
  LayerListBox* m_layerList = nullptr;

  QAbstractButton* m_addLayerButton = nullptr;
  QAbstractButton* m_removeLayerButton = nullptr;
  QAbstractButton* m_moveLayerUpButton = nullptr;
  QAbstractButton* m_moveLayerDownButton = nullptr;

public:
  explicit LayerEditor(MapDocument& document, QWidget* parent = nullptr);

private:
  void onSetCurrentLayer(mdl::LayerNode* layer);

  void onLayerRightClick(mdl::LayerNode* layer);

  void toggleLayerVisible(mdl::LayerNode* layer);

  void toggleLayerLocked(mdl::LayerNode* layer);

  void toggleOmitLayerFromExport(mdl::LayerNode* layer);

  void onAddLayer();

  void onRemoveLayer();
  bool canRemoveLayer() const;

  bool canMoveLayer(int direction) const;
  void moveLayer(mdl::LayerNode* layer, int direction);

private:
  void createGui();
private slots:
  void updateButtons();
};

} // namespace ui
} // namespace tb
