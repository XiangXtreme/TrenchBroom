/*
 Copyright (C) 2026 KreedzBroom contributors
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "mdl/GoldSrcLight.h"

#include "mdl/Entity.h"
#include "mdl/GameConfig.h"

#include "kd/string_compare.h"
#include "kd/string_utils.h"

#include "vm/scalar.h"
#include "vm/vec_io.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <string_view>

namespace tb::mdl
{
namespace
{
double number(const Entity& entity, const std::string& key)
{
  const auto* value = entity.property(key);
  const auto parsed = value ? kdl::str_to_double(*value) : std::nullopt;
  return parsed && std::isfinite(*parsed) ? *parsed : 0.0;
}
} // namespace

bool usesGoldSrcLightDirections(const GameConfig& config)
{
  // WAD lump type 0x43 (".C") identifies GoldSrc, including custom mods.
  return std::ranges::any_of(config.materialConfig.extensions, [](const auto& extension) {
    return kdl::ci::str_is_equal(extension.string(), ".C");
  });
}

bool isGoldSrcLight(const Entity& entity)
{
  return kdl::cs::str_is_prefix(entity.classname(), "light")
         || entity.classname() == "info_sunlight";
}

bool isDirectionalGoldSrcLight(const Entity& entity)
{
  if (!isGoldSrcLight(entity))
    return false;
  auto classname = std::string_view{entity.classname()};
  if (classname == "light_surface")
    return false;
  if (classname == "light_shadow" || classname == "light_bounce")
  {
    const auto* convertTo = entity.property("convertto");
    classname = convertTo && !convertTo->empty() ? std::string_view{*convertTo} : "light";
  }
  if (!kdl::cs::str_is_prefix(classname, "light") && classname != "info_sunlight")
    return false;
  if (const auto* texture = entity.property("_tex"); texture && !texture->empty())
    return false;
  const auto* target = entity.property("target");
  if (
    classname != "light_spot" && classname != "light_environment"
    && classname != "info_sunlight" && (!target || target->empty()))
    return false;
  return true;
}

std::optional<vm::vec3d> goldSrcLightDirection(const Entity& entity)
{
  if (!isDirectionalGoldSrcLight(entity))
    return std::nullopt;
  // hltools RAD create_direct_lights / CSG entity output fixups. Nonzero scalar
  // angle/pitch override angles; positive light pitch points UP, roll is ignored.
  const auto* value = entity.property("angles");
  const auto angles =
    value ? vm::parse<double, 3>(*value).value_or(vm::vec3d{}) : vm::vec3d{};
  const auto angle = number(entity, "angle");
  const auto explicitPitch = number(entity, "pitch");
  const auto pitch = vm::to_radians(explicitPitch != 0.0 ? explicitPitch : angles.x());
  const auto yaw = vm::to_radians(angle != 0.0 ? angle : angles.y());
  // RAD overwrites Z even for legacy angle=-1/-2; pitch=0 leaves a zero normal.
  const auto horizontal = angle == -1.0 || angle == -2.0 ? 0.0 : std::cos(pitch);
  const auto direction =
    vm::vec3d{horizontal * std::cos(yaw), horizontal * std::sin(yaw), std::sin(pitch)};
  const auto length = vm::length(direction);
  return std::isfinite(length) && length != 0.0 ? std::optional{direction / length}
                                                : std::nullopt;
}

void applyGoldSrcLightRotation(Entity& entity, const vm::mat4x4d& transformation)
{
  // Preserve aiming relationships. Rotating/moving the light or its target updates
  // the displayed direction through their origins, without detaching the target.
  if (const auto* target = entity.property("target"); target && !target->empty())
    return;
  const auto direction = goldSrcLightDirection(entity);
  if (!direction)
    return;
  const auto transformed = transformation * *direction;
  const auto length = vm::length(transformed);
  if (!std::isfinite(length) || length == 0.0)
    return;
  const auto normal = transformed / length;
  const auto pitch =
    vm::correct(vm::to_degrees(std::asin(std::clamp(normal.z(), -1.0, 1.0))));
  const auto* value = entity.property("angles");
  const auto oldAngles =
    value ? vm::parse<double, 3>(*value).value_or(vm::vec3d{}) : vm::vec3d{};
  // A vertical beam has no yaw; retain the previous yaw instead of deriving one
  // from floating point noise at the pole.
  const auto oldAngle = number(entity, "angle");
  const auto oldYaw = oldAngle != 0.0 ? oldAngle : oldAngles.y();
  const auto yaw = vm::normalize_degrees(vm::correct(vm::normalize_degrees(
    std::hypot(normal.x(), normal.y()) < 1e-8
      ? oldYaw
      : vm::to_degrees(std::atan2(normal.y(), normal.x())))));
  entity.addOrUpdateProperty(
    "angles",
    kdl::str_to_string(
      std::setprecision(std::numeric_limits<double>::max_digits10),
      vm::vec3d{pitch, yaw, oldAngles.z()}));
  // Keep existing FGD fields useful and prevent stale scalar overrides. Write
  // angles as well, because zero scalar values fall back to its components.
  if (entity.hasProperty("pitch"))
    entity.addOrUpdateProperty(
      "pitch",
      kdl::str_to_string(
        std::setprecision(std::numeric_limits<double>::max_digits10), pitch));
  if (entity.hasProperty("angle"))
    entity.addOrUpdateProperty(
      "angle",
      kdl::str_to_string(
        std::setprecision(std::numeric_limits<double>::max_digits10), yaw));
}
} // namespace tb::mdl
