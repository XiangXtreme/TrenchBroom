/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.
 */

#include "ui/automation/AutomationNodes.h"

#include "mdl/AddRemoveNodesCommand.h"
#include "mdl/BrushFaceHandle.h"
#include "mdl/BrushNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/Map_World.h"
#include "mdl/Node.h"
#include "mdl/WorldNode.h"

#include <algorithm>
#include <map>

namespace tb::ui::automation
{

bool addNodes(
  mdl::Map& map, const std::vector<mdl::Node*>& nodes, const bool selectCreated)
{
  if (nodes.empty())
  {
    return false;
  }
  auto& parent = mdl::parentForNodes(map);
  if (!parent.canAddChildren(std::begin(nodes), std::end(nodes)))
  {
    return false;
  }
  if (selectCreated)
  {
    mdl::deselectAll(map);
  }
  auto nodesToAdd = std::map<mdl::Node*, std::vector<mdl::Node*>>{};
  nodesToAdd.emplace(&parent, nodes);
  if (!map.executeAndStore(mdl::AddRemoveNodesCommand::add(nodesToAdd)))
  {
    return false;
  }
  if (selectCreated)
  {
    mdl::selectNodes(map, nodes);
  }
  return true;
}

bool removeNodes(mdl::Map& map, std::vector<mdl::Node*> nodes)
{
  std::ranges::sort(nodes);
  nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
  nodes.erase(
    std::remove_if(
      nodes.begin(),
      nodes.end(),
      [&](const auto* node) {
        return node == nullptr || node == &map.worldNode()
               || std::ranges::any_of(nodes, [&](const auto* other) {
                    return node != other && other != nullptr
                           && node->isDescendantOf(*other);
                  });
      }),
    nodes.end());
  if (nodes.empty())
  {
    return false;
  }
  auto nodesByParent = std::map<mdl::Node*, std::vector<mdl::Node*>>{};
  for (auto* node : nodes)
  {
    auto* parent = node->parent();
    if (parent == nullptr || !parent->canRemoveChild(*node))
    {
      return false;
    }
    nodesByParent[parent].push_back(node);
  }
  const auto removed = [&](const mdl::Node* candidate) {
    return std::ranges::any_of(nodes, [&](const auto* node) {
      return candidate == node || candidate->isDescendantOf(*node);
    });
  };
  auto selectedNodes = map.selection().nodes;
  std::erase_if(selectedNodes, [&](const auto* node) { return !removed(node); });
  auto selectedFaces = map.selection().brushFaces;
  std::erase_if(selectedFaces, [&](const auto& face) { return !removed(face.node()); });
  mdl::deselectBrushFaces(map, selectedFaces);
  mdl::deselectNodes(map, selectedNodes);
  return map.executeAndStore(mdl::AddRemoveNodesCommand::remove(nodesByParent));
}

void replaceSelection(mdl::Map& map, const std::vector<mdl::Node*>& nodes)
{
  mdl::deselectAll(map);
  if (!nodes.empty())
  {
    mdl::selectNodes(map, nodes);
  }
}

} // namespace tb::ui::automation
