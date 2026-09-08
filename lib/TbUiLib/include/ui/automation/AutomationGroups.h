/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.
 */

#pragma once

#include <string>
#include <vector>

namespace tb::mdl
{
class GroupNode;
class Map;
} // namespace tb::mdl

namespace tb::ui::automation
{

/** Native group commands shared by MCP and Python automation adapters. */
std::vector<mdl::GroupNode*> selectedGroups(mdl::Map& map);
mdl::GroupNode* groupSelectedNodes(mdl::Map& map, const std::string& name);
void renameSelectedGroups(mdl::Map& map, const std::string& name);
void ungroupSelectedNodes(mdl::Map& map);

} // namespace tb::ui::automation
