/*
 Copyright (C) 2026 KreedzBroom contributors
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include "vm/vec.h"

#include <optional>
#include <string>
#include <unordered_map>

namespace tb::mdl
{
class EntityNode;
class Node;
} // namespace tb::mdl

namespace tb::render
{
/** Resolves overlay directions for one render pass, independently of model rotation.
 * Named targets are indexed lazily, once per scene root, in map traversal order.
 * Create a fresh resolver each pass so edits, undo and target moves are visible.
 */
class EntityDirectionResolver
{
private:
  bool m_goldSrc;
  using Targets = std::unordered_map<std::string, vm::vec3d>;
  std::unordered_map<const mdl::Node*, Targets> m_targets;

public:
  explicit EntityDirectionResolver(bool goldSrc);
  std::optional<vm::vec3f> direction(const mdl::EntityNode& node);

private:
  std::optional<vm::vec3d> targetOrigin(
    const mdl::EntityNode& node, const std::string& name);
};
} // namespace tb::render
