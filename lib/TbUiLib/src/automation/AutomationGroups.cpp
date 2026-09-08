/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.
 */

#include "ui/automation/AutomationGroups.h"

#include "mdl/GroupNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Groups.h"
#include "mdl/Map_Selection.h"

namespace tb::ui::automation
{

std::vector<mdl::GroupNode*> selectedGroups(mdl::Map& map)
{
  const auto& groups = map.selection().groups;
  return {groups.begin(), groups.end()};
}

mdl::GroupNode* groupSelectedNodes(mdl::Map& map, const std::string& name)
{
  return mdl::groupSelectedNodes(map, name);
}

void renameSelectedGroups(mdl::Map& map, const std::string& name)
{
  mdl::renameSelectedGroups(map, name);
}

void ungroupSelectedNodes(mdl::Map& map)
{
  mdl::ungroupSelectedNodes(map);
}

} // namespace tb::ui::automation
