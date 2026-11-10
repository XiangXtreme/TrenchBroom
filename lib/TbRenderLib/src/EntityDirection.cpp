/*
 Copyright (C) 2026 KreedzBroom contributors
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "render/EntityDirection.h"

#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/LayerNode.h"

#include "kd/string_compare.h"
#include "kd/string_utils.h"

#include "vm/mat.h"
#include "vm/scalar.h"
#include "vm/vec_io.h"

#include <cmath>
#include <string_view>

namespace tb::render
{
namespace
{
double number(const mdl::Entity& entity, const std::string& key)
{
  const auto* value = entity.property(key);
  const auto parsed = value ? kdl::str_to_double(*value) : std::nullopt;
  return parsed && std::isfinite(*parsed) ? *parsed : 0.0;
}

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
  auto classname = std::string_view{entity.classname()};
  if (
    !m_goldSrc
    || (!kdl::cs::str_is_prefix(classname, "light") && classname != "info_sunlight"))
    return vm::vec3f{entity.rotation() * vm::vec3d{1, 0, 0}};

  // hltools CSG converts these control entities before RAD classifies the light.
  if (classname == "light_surface")
    return std::nullopt;
  if (classname == "light_shadow" || classname == "light_bounce")
  {
    const auto* convertTo = entity.property("convertto");
    classname = convertTo && !convertTo->empty() ? std::string_view{*convertTo} : "light";
  }
  if (!kdl::cs::str_is_prefix(classname, "light") && classname != "info_sunlight")
    return std::nullopt;

  // Texture emitters use the emitting face normal rather than entity angles.
  if (const auto* texture = entity.property("_tex"); texture && !texture->empty())
    return std::nullopt;

  const auto* target = entity.property("target");
  const auto targeted = target && !target->empty();
  if (
    classname != "light_spot" && classname != "light_environment"
    && classname != "info_sunlight" && !targeted)
    return std::nullopt; // Omnidirectional point light.
  if (targeted)
  {
    if (const auto origin = targetOrigin(node, *target))
      return unitDirection(*origin - entity.origin());
    // RAD falls back to angles when a named target is missing.
  }

  // hltools src/rad/lightmap.cpp:create_direct_lights and CSG output fixups:
  // nonzero angle/pitch override angles, roll is unused, positive pitch points UP.
  const auto* value = entity.property("angles");
  const auto angles =
    value ? vm::parse<double, 3>(*value).value_or(vm::vec3d{}) : vm::vec3d{};
  const auto angle = number(entity, "angle");
  const auto explicitPitch = number(entity, "pitch");
  const auto pitch = vm::to_radians(explicitPitch != 0.0 ? explicitPitch : angles.x());
  const auto yaw = vm::to_radians(angle != 0.0 ? angle : angles.y());
  // RAD overwrites Z with sin(pitch) even for legacy angle=-1/-2. When pitch=0
  // this leaves a zero normal, so there is no valid direction to draw.
  const auto horizontal = angle == -1.0 || angle == -2.0 ? 0.0 : std::cos(pitch);
  return unitDirection(
    {horizontal * std::cos(yaw), horizontal * std::sin(yaw), std::sin(pitch)});
}
} // namespace tb::render
