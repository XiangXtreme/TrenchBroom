/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.
 */

#pragma once

#include "vm/bbox.h"

#include <QString>

#include <map>
#include <string>
#include <vector>

namespace tb::mdl
{
class EntityNode;
class Map;
} // namespace tb::mdl

namespace tb::ui::automation
{

struct AutomationPointEntitySpec
{
  std::string classname;
  std::map<std::string, std::string> properties;
  vm::vec3d origin = vm::vec3d{0.0, 0.0, 0.0};
};

struct AutomationPointEntityBuildResult
{
  std::vector<mdl::EntityNode*> nodes;
  std::vector<std::string> classnames;
  int removedEmptyPropertyCount = 0;
  int failedIndex = -1;
  QString error;
};

/**
 * Validates FGD point entity definitions and creates unattached entity nodes.
 * The caller owns all returned nodes until it passes them to addNodes.
 */
AutomationPointEntityBuildResult buildCheckedPointEntities(
  const mdl::Map& map, const std::vector<AutomationPointEntitySpec>& entities);

void deletePointEntityNodes(std::vector<mdl::EntityNode*>& nodes);

} // namespace tb::ui::automation
