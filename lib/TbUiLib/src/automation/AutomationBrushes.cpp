/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.
 */

#include "ui/automation/AutomationBrushes.h"

#include "mdl/BrushBuilder.h"
#include "mdl/BrushNode.h"
#include "mdl/Map.h"
#include "mdl/Map_World.h"
#include "mdl/WorldNode.h"

#include <cmath>

namespace tb::ui::automation
{

std::optional<std::vector<mdl::BrushNode*>> createBoxNodes(
  const mdl::Map& map, const std::vector<AutomationBoxSpec>& boxes, QString& error)
{
  if (boxes.empty())
  {
    error = "boxes must not be empty";
    return std::nullopt;
  }

  const auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};
  auto result = std::vector<mdl::BrushNode*>{};
  result.reserve(boxes.size());
  for (const auto& box : boxes)
  {
    const auto& min = box.bounds.min;
    const auto& max = box.bounds.max;
    if (
      !std::isfinite(min.x()) || !std::isfinite(min.y()) || !std::isfinite(min.z())
      || !std::isfinite(max.x()) || !std::isfinite(max.y()) || !std::isfinite(max.z())
      || min.x() >= max.x() || min.y() >= max.y() || min.z() >= max.z())
    {
      error = "box min must be smaller than max on all finite axes";
      for (auto* node : result)
      {
        delete node;
      }
      return std::nullopt;
    }

    auto brush = builder.createCuboid(box.bounds, box.material);
    if (brush.is_error())
    {
      error = "Could not create one or more box brushes";
      for (auto* node : result)
      {
        delete node;
      }
      return std::nullopt;
    }
    result.push_back(new mdl::BrushNode{std::move(brush).value()});
  }
  return result;
}

} // namespace tb::ui::automation
