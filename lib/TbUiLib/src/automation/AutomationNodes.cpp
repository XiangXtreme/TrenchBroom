/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.
 */

#include "ui/automation/AutomationNodes.h"

#include "mdl/AddRemoveNodesCommand.h"
#include "mdl/Map.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/Map_World.h"
#include "mdl/Node.h"

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

} // namespace tb::ui::automation
