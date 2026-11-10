/*
 Copyright (C) 2026 KreedzBroom contributors
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "render/EntityDirection.h"

#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/GoldSrcLight.h"
#include "mdl/LayerNode.h"

#include "vm/mat.h"

#include <cmath>

namespace tb::render
{
namespace
{
std::optional<vm::vec3f> unitDirection(const vm::vec3d& direction)
{
  const auto length = vm::length(direction);
  if (!std::isfinite(length) || length == 0.0)
    return std::nullopt;
  return vm::vec3f{direction / length};
}
} // namespace

EntityDirectionResolver::EntityDirectionResolver(const bool goldSrc)
  : m_goldSrc{goldSrc}
{
}

std::optional<vm::vec3d> EntityDirectionResolver::targetOrigin(
  const mdl::EntityNode& node, const std::string& name)
{
  const auto* root = static_cast<const mdl::Node*>(&node);
  while (root->parent())
    root = root->parent();
  auto [it, inserted] = m_targets.try_emplace(root);
  if (inserted)
  {
    const auto collect = [&](auto&& self, const mdl::Node& current) -> void {
      if (const auto* layer = dynamic_cast<const mdl::LayerNode*>(&current);
          layer && layer->layer().omitFromExport())
        return;
      if (const auto* entityNode = dynamic_cast<const mdl::EntityNodeBase*>(&current))
      {
        const auto& entity = entityNode->entity();
        if (const auto* targetname = entity.property("targetname");
            targetname && !targetname->empty())
          it->second.try_emplace(*targetname, entity.origin());
        // Entity children are brushes, not additional entities.
        if (dynamic_cast<const mdl::EntityNode*>(&current))
          return;
      }
      for (const auto* child : current.children())
        self(self, *child);
    };
    collect(collect, *root);
  }
  const auto target = it->second.find(name);
  return target != it->second.end() ? std::optional{target->second} : std::nullopt;
}

std::optional<vm::vec3f> EntityDirectionResolver::direction(const mdl::EntityNode& node)
{
  const auto& entity = node.entity();
  if (!m_goldSrc || !mdl::isGoldSrcLight(entity))
    return vm::vec3f{entity.rotation() * vm::vec3d{1, 0, 0}};
  if (!mdl::isDirectionalGoldSrcLight(entity))
    return std::nullopt;

  const auto* target = entity.property("target");
  const auto targeted = target && !target->empty();
  if (targeted)
  {
    if (const auto origin = targetOrigin(node, *target))
      return unitDirection(*origin - entity.origin());
    // RAD falls back to angles when a named target is missing.
  }

  const auto angular = mdl::goldSrcLightDirection(entity);
  return angular ? std::optional{vm::vec3f{*angular}} : std::nullopt;
}
} // namespace tb::render
