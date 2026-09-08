/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 */

#include "ui/automation/AutomationMapSnapshot.h"

#include "mdl/BrushNode.h"
#include "mdl/EntityNode.h"
#include "mdl/Grid.h"
#include "mdl/Map.h"
#include "mdl/Node.h"
#include "mdl/PatchNode.h"
#include "mdl/WorldNode.h"

namespace tb::ui
{
namespace
{

void collectMapContent(const mdl::Node& node, AutomationMapSnapshot& snapshot)
{
  const auto isEntity = dynamic_cast<const mdl::EntityNode*>(&node) != nullptr;
  const auto isBrush = dynamic_cast<const mdl::BrushNode*>(&node) != nullptr;
  const auto isPatch = dynamic_cast<const mdl::PatchNode*>(&node) != nullptr;
  if (isEntity)
  {
    ++snapshot.pointEntityCount;
  }
  else if (isBrush)
  {
    ++snapshot.brushCount;
  }
  else if (isPatch)
  {
    ++snapshot.patchCount;
  }
  if (isEntity || isBrush || isPatch)
  {
    snapshot.contentBounds = snapshot.contentBounds
                               ? vm::merge(*snapshot.contentBounds, node.logicalBounds())
                               : node.logicalBounds();
  }
  for (const auto* child : node.children())
  {
    if (child != nullptr)
    {
      collectMapContent(*child, snapshot);
    }
  }
}

} // namespace

AutomationMapSnapshot collectAutomationMapSnapshot(const mdl::Map& map)
{
  auto snapshot = AutomationMapSnapshot{};
  collectMapContent(map.worldNode(), snapshot);
  snapshot.nodeCount = static_cast<int>(map.worldNode().descendantCount() + 1u);
  snapshot.selectedNodeCount = static_cast<int>(map.selection().nodes.size());
  snapshot.selectedEntityCount = static_cast<int>(map.selection().entities.size());
  snapshot.selectedBrushCount = static_cast<int>(map.selection().brushes.size());
  snapshot.selectedFaceCount = static_cast<int>(map.selection().brushFaces.size());
  snapshot.gridSize = map.grid().size();
  snapshot.gridActualSize = map.grid().actualSize();
  snapshot.gridSnap = map.grid().snap();
  snapshot.gridVisible = map.grid().visible();
  return snapshot;
}

} // namespace tb::ui
