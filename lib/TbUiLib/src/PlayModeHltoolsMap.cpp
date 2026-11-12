/*
 Copyright (C) 2026 KreedzBroom contributors

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

#include "PlayModeHltoolsMap.h"

#include "mdl/BrushNode.h"
#include "mdl/NodeWriter.h"
#include "mdl/PatchNode.h"
#include "mdl/WorldNode.h"

#include "kd/task_manager.h"

#include <memory>
#include <sstream>

namespace tb::ui
{
namespace
{

void removeHiddenGeometry(mdl::Node& node)
{
  // Serialization updates file positions; only modify the detached snapshot.
  const auto children = node.children();
  for (auto* child : children)
  {
    if (
      (dynamic_cast<mdl::BrushNode*>(child) && !child->visible())
      || dynamic_cast<mdl::PatchNode*>(child))
    {
      node.removeChild(child);
      delete child;
    }
    else
      removeHiddenGeometry(*child);
  }
}

} // namespace

std::string serializePlayModeHltoolsMap(
  const mdl::WorldNode& worldNode, const vm::bbox3d& worldBounds)
{
  auto snapshot = std::unique_ptr<mdl::WorldNode>{
    static_cast<mdl::WorldNode*>(worldNode.cloneRecursively(worldBounds))};
  removeHiddenGeometry(*snapshot);
  auto stream = std::ostringstream{};
  auto tasks = kdl::task_manager{0};
  auto writer = mdl::NodeWriter{*snapshot, stream};
  writer.setExporting(true);
  writer.setStripTbProperties(true);
  writer.writeMap(tasks);
  return stream.str();
}

} // namespace tb::ui
