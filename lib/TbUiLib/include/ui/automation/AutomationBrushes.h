/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.
 */

#pragma once

#include "vm/bbox.h"

#include <QString>

#include <optional>
#include <string>
#include <vector>

namespace tb::mdl
{
class BrushNode;
class Map;
} // namespace tb::mdl

namespace tb::ui::automation
{

/** A validated cuboid request owned by an automation adapter. */
struct AutomationBoxSpec
{
  vm::bbox3d bounds;
  std::string material;
};

/**
 * Builds cuboid brush nodes without attaching them to the map.
 *
 * The caller owns returned nodes until it passes them to addNodes. On failure
 * no nodes escape and error explains the rejected input or native build error.
 */
std::optional<std::vector<mdl::BrushNode*>> createBoxNodes(
  const mdl::Map& map, const std::vector<AutomationBoxSpec>& boxes, QString& error);

} // namespace tb::ui::automation
