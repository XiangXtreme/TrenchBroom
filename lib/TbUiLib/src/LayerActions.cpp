/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/LayerActions.h"

#include <QCoreApplication>
#include <QMenu>

#include "mdl/EditorContext.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Layers.h"
#include "mdl/Map_NodeLocking.h"
#include "mdl/Map_NodeVisibility.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/Transaction.h"
#include "mdl/WorldNode.h"
#include "ui/ViewUtils.h"

#include "kd/vector_utils.h"

#include <algorithm>

namespace tb::ui
{
namespace
{
bool containsLayer(const mdl::Map& map, const mdl::LayerNode* layer)
{
  const auto layers = map.worldNode().allLayers();
  return std::ranges::find(layers, layer) != layers.end();
}
} // namespace

bool canRemoveLayer(const mdl::Map& map, const mdl::LayerNode* layer)
{
  if (!containsLayer(map, layer) || layer == map.worldNode().defaultLayer())
    return false;
  const auto layers = map.worldNode().allLayers();
  return std::ranges::any_of(layers, [layer](const auto* candidate) {
    return candidate != layer && candidate->visible() && !candidate->locked();
  });
}

bool removeLayer(mdl::Map& map, mdl::LayerNode* layer)
{
  if (!canRemoveLayer(map, layer))
    return false;
  auto transaction = mdl::Transaction{map, "Remove Layer " + layer->name()};
  auto* defaultLayer = map.worldNode().defaultLayer();
  mdl::deselectAll(map);
  if (
    layer->hasChildren() && !mdl::reparentNodes(map, {{defaultLayer, layer->children()}}))
  {
    transaction.cancel();
    return false;
  }
  if (map.editorContext().currentLayer() == layer)
    mdl::setCurrentLayer(map, defaultLayer);
  mdl::removeNodes(map, {layer});
  return transaction.commit();
}

void populateLayerActions(QMenu& menu, mdl::Map& map, mdl::LayerNode* layer)
{
  const auto add =
    [&](const char* id, const QString& label, const bool enabled, auto execute) {
      auto* action = menu.addAction(label, &menu, [&, layer, execute]() {
        // A popup or rename dialog may process document changes before activation.
        if (containsLayer(map, layer))
          execute();
      });
      action->setObjectName(QString::fromLatin1(id));
      action->setEnabled(enabled);
      return action;
    };

  add(
    "Layer_MakeActive",
    QCoreApplication::translate("tb::ui::LayerEditor", "Make active layer"),
    mdl::canSetCurrentLayer(map, layer),
    [&map, layer]() {
      if (mdl::canSetCurrentLayer(map, layer))
        mdl::setCurrentLayer(map, layer);
    });
  add(
    "Layer_MoveSelection",
    QCoreApplication::translate("tb::ui::LayerEditor", "Move selection to layer"),
    mdl::canMoveSelectedNodesToLayer(map, layer),
    [&map, layer]() {
      if (mdl::canMoveSelectedNodesToLayer(map, layer))
        mdl::moveSelectedNodesToLayer(map, layer);
    });
  add(
    "Layer_SelectAll",
    QCoreApplication::translate("tb::ui::LayerEditor", "Select all in layer"),
    mdl::canSelectAllInLayers(map, {layer}),
    [&map, layer]() {
      if (mdl::canSelectAllInLayers(map, {layer}))
        mdl::selectAllInLayers(map, {layer});
    });
  menu.addSeparator();
  add(
    "Layer_Visibility",
    layer->hidden() ? QCoreApplication::translate("tb::ui::LayerEditor", "Show layer")
                    : QCoreApplication::translate("tb::ui::LayerEditor", "Hide layer"),
    true,
    [&map, layer]() {
      if (layer->hidden())
        mdl::resetNodeVisibility(map, {layer});
      else
        mdl::hideNodes(map, {layer});
    });
  add(
    "Layer_Isolate",
    QCoreApplication::translate("tb::ui::LayerEditor", "Isolate layer"),
    mdl::canIsolateLayers(map, {layer}),
    [&map, layer]() {
      if (mdl::canIsolateLayers(map, {layer}))
        mdl::isolateLayers(map, {layer});
    });
  add(
    "Layer_Locking",
    layer->locked() ? QCoreApplication::translate("tb::ui::LayerEditor", "Unlock layer")
                    : QCoreApplication::translate("tb::ui::LayerEditor", "Lock layer"),
    true,
    [&map, layer]() {
      if (layer->locked())
        mdl::resetNodeLockingState(map, {layer});
      else
        mdl::lockNodes(map, {layer});
    });
  auto* omit = add(
    "Layer_OmitFromExport",
    QCoreApplication::translate("tb::ui::LayerEditor", "Omit From Export"),
    true,
    [&map, layer]() {
      mdl::setOmitLayerFromExport(map, layer, !layer->layer().omitFromExport());
    });
  omit->setCheckable(true);
  omit->setChecked(layer->layer().omitFromExport());
  menu.addSeparator();
  const auto layers = map.worldNode().allLayers();
  add(
    "Layer_ShowAll",
    QCoreApplication::translate("tb::ui::LayerEditor", "Show All Layers"),
    std::ranges::any_of(layers, [](const auto* l) { return !l->visible(); }),
    [&map]() {
      mdl::resetNodeVisibility(
        map, kdl::vec_static_cast<mdl::Node*>(map.worldNode().allLayers()));
    });
  add(
    "Layer_HideAll",
    QCoreApplication::translate("tb::ui::LayerEditor", "Hide All Layers"),
    std::ranges::any_of(layers, [](const auto* l) { return l->visible(); }),
    [&map]() {
      mdl::hideNodes(map, kdl::vec_static_cast<mdl::Node*>(map.worldNode().allLayers()));
    });
  add(
    "Layer_UnlockAll",
    QCoreApplication::translate("tb::ui::LayerEditor", "Unlock All Layers"),
    std::ranges::any_of(layers, [](const auto* l) { return l->locked(); }),
    [&map]() {
      mdl::resetNodeLockingState(
        map, kdl::vec_static_cast<mdl::Node*>(map.worldNode().allLayers()));
    });
  add(
    "Layer_LockAll",
    QCoreApplication::translate("tb::ui::LayerEditor", "Lock All Layers"),
    std::ranges::any_of(layers, [](const auto* l) { return !l->locked(); }),
    [&map]() {
      mdl::lockNodes(map, kdl::vec_static_cast<mdl::Node*>(map.worldNode().allLayers()));
    });
  menu.addSeparator();
  add(
    "Layer_MoveUp",
    QCoreApplication::translate("tb::ui::LayerEditor", "Move layer up"),
    mdl::canMoveLayer(map, layer, -1),
    [&map, layer]() {
      if (mdl::canMoveLayer(map, layer, -1))
        mdl::moveLayer(map, layer, -1);
    });
  add(
    "Layer_MoveDown",
    QCoreApplication::translate("tb::ui::LayerEditor", "Move layer down"),
    mdl::canMoveLayer(map, layer, 1),
    [&map, layer]() {
      if (mdl::canMoveLayer(map, layer, 1))
        mdl::moveLayer(map, layer, 1);
    });
  const auto isDefault = layer == map.worldNode().defaultLayer();
  add(
    "Layer_Rename",
    QCoreApplication::translate("tb::ui::LayerEditor", "Rename Layer"),
    !isDefault,
    [&map, layer, &menu]() {
      const auto name = queryLayerName(&menu, layer->name());
      if (
        !name.empty() && containsLayer(map, layer)
        && layer != map.worldNode().defaultLayer())
        mdl::renameLayer(map, layer, name);
    });
  add(
    "Layer_Remove",
    QCoreApplication::translate("tb::ui::LayerEditor", "Remove Layer"),
    canRemoveLayer(map, layer),
    [&map, layer]() { removeLayer(map, layer); });
}
} // namespace tb::ui
