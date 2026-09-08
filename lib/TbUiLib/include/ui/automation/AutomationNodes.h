/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.
 */

#pragma once

#include <vector>

namespace tb::mdl
{
class Map;
class Node;
} // namespace tb::mdl

namespace tb::ui::automation
{

/**
 * Adds nodes through the native undo command inside the caller's transaction.
 * The caller retains ownership when this function returns false.
 */
bool addNodes(mdl::Map& map, const std::vector<mdl::Node*>& nodes, bool selectCreated);

/** Removes top-level removable nodes through the caller's native transaction. */
bool removeNodes(mdl::Map& map, std::vector<mdl::Node*> nodes);

/** Replaces the editor selection; transaction ownership remains with the caller. */
void replaceSelection(mdl::Map& map, const std::vector<mdl::Node*>& nodes);

} // namespace tb::ui::automation
