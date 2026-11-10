/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/GoldSrcEntityMotion.h"

#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/EntityNode.h"

#include "vm/mat_ext.h"
#include "vm/vec_io.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <string>

namespace tb::ui
{
namespace
{
std::optional<double> number(
  const mdl::Entity& entity, const std::string& key, double fallback = 0)
{
  const auto* text = entity.property(key);
  if (!text || text->empty())
    return fallback;
  auto value = double{};
  const auto first = text->find_first_not_of(" \t");
  const auto last = text->find_last_not_of(" \t");
  if (first == std::string::npos)
    return std::nullopt;
  const auto* begin = text->data() + first;
  if (*begin == '+')
    ++begin;
  const auto* end = text->data() + last + 1;
  const auto result = std::from_chars(begin, end, value);
  if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(value))
    return std::nullopt;
  return value;
}

std::optional<vm::vec3d> vectorProperty(const mdl::Entity& entity, const std::string& key)
{
  const auto* text = entity.property(key);
  if (!text || text->empty())
    return vm::vec3d{0, 0, 0};
  const auto value = vm::parse<double, 3>(*text);
  if (!value)
    return std::nullopt;
  for (size_t i = 0; i < 3; ++i)
    if (!std::isfinite((*value)[i]))
      return std::nullopt;
  return value;
}

bool materialPrefix(const mdl::Brush& brush, const std::string& prefix)
{
  for (const auto& face : brush.faces())
  {
    auto name = face.materialName();
    std::transform(name.begin(), name.end(), name.begin(), [](const unsigned char c) {
      return char(std::toupper(c));
    });
    if (name.starts_with(prefix))
      return true;
  }
  return false;
}

bool hasValue(const mdl::Entity& entity, const std::string& key)
{
  const auto* value = entity.property(key);
  return value && !value->empty();
}
} // namespace

vm::mat4x4d GoldSrcEntityMotion::transform(double progress, const bool opposite) const
{
  progress = std::clamp(std::isfinite(progress) ? progress : 0.0, 0.0, 1.0);
  const auto initial = startOpen ? 1.0 : 0.0;
  const auto final = startOpen ? 0.0 : 1.0;
  if (!rotating)
    return vm::translation_matrix(
      direction * distance * (initial + (final - initial) * progress));

  const auto from = angles + direction * distance * initial;
  auto to = angles + direction * distance * final;
  // DoorGoUp negates the target Euler angles, not just the relative travel.
  if (bidirectional && opposite)
    to = -to;
  const auto at = from + (to - from) * progress;
  // GoldSrc AngleMatrix = yaw(Z) * pitch(Y) * roll(X), positive pitch looks down.
  const auto rotation = vm::rotation_matrix(
    vm::to_radians(at.z()), vm::to_radians(at.x()), vm::to_radians(at.y()));
  return vm::translation_matrix(origin) * rotation * vm::translation_matrix(-origin);
}

std::optional<GoldSrcEntityMotion> goldSrcEntityMotion(const mdl::EntityNode& node)
{
  const auto& entity = node.entity();
  const auto& classname = entity.classname();
  if (
    classname != "func_door" && classname != "func_button"
    && classname != "func_door_rotating")
    return std::nullopt;
  auto result = GoldSrcEntityMotion{};
  result.rotating = classname == "func_door_rotating";
  const auto flagsValue = number(entity, "spawnflags");
  const auto lipValue = number(entity, "lip");
  const auto distanceValue = number(entity, "distance");
  auto angles = vectorProperty(entity, "angles");
  const auto origin = vectorProperty(entity, "origin");
  const auto angle = number(entity, "angle");
  if (
    !flagsValue || *flagsValue < 0 || *flagsValue > 2147483647.0
    || std::trunc(*flagsValue) != *flagsValue || (!result.rotating && !lipValue)
    || (result.rotating && !distanceValue) || !angles || !origin
    || (!entity.hasProperty("angles") && !angle))
  {
    result.problem = MotionProblem::InvalidProperty;
    return result;
  }
  if (!entity.hasProperty("angles"))
    angles = vm::vec3d{0, *angle, 0};
  const auto flags = static_cast<int>(*flagsValue);
  result.startOpen = classname != "func_button" && (flags & 1) != 0;
  result.origin = *origin;
  auto bounds = std::optional<vm::bbox3d>{};
  auto originBrushes = 0;
  for (const auto* child : node.children())
  {
    const auto* brushNode = dynamic_cast<const mdl::BrushNode*>(child);
    if (!brushNode)
    {
      result.problem = MotionProblem::CompilerOverride;
      return result;
    }
    const auto& brush = brushNode->brush();
    if (materialPrefix(brush, "ORIGIN"))
    {
      ++originBrushes;
      const auto center = brush.bounds().center();
      // hltools emits integer origin keys, then reparses those keys before building
      // models.
      for (size_t i = 0; i < 3; ++i)
        result.origin[i] = std::trunc(center[i]);
      continue;
    }
    if (materialPrefix(brush, "BOUNDINGBOX"))
    {
      result.problem = MotionProblem::CompilerOverride;
      return result;
    }
    if (
      materialPrefix(brush, "CLIP") || materialPrefix(brush, "HINT")
      || materialPrefix(brush, "SKIP"))
      continue;
    bounds = bounds ? vm::merge(*bounds, brush.bounds()) : brush.bounds();
    for (const auto& face : brush.faces())
      result.faces.push_back(face.vertexPositions());
  }
  if (originBrushes > 1 || (originBrushes && hasValue(entity, "origin")))
  {
    result.problem = MotionProblem::OriginConflict;
    return result;
  }
  if (
    hasValue(entity, "zhlt_usemodel") || hasValue(entity, "zhlt_minsmaxs")
    || hasValue(entity, "zhlt_transform"))
  {
    result.problem = MotionProblem::CompilerOverride;
    return result;
  }
  if (!bounds)
  {
    result.problem = MotionProblem::NoGeometry;
    return result;
  }
  if (result.rotating)
  {
    result.angles = *angles;
    result.direction = (flags & 64)    ? vm::vec3d{0, 0, 1}
                       : (flags & 128) ? vm::vec3d{1, 0, 0}
                                       : vm::vec3d{0, 1, 0};
    if (flags & 2)
      result.direction = -result.direction;
    result.distance = *distanceValue;
    result.bidirectional = !(flags & 16) && result.direction.y() != 0;
  }
  else
  {
    if (*angles == vm::vec3d{0, -1, 0})
      result.direction = vm::vec3d{0, 0, 1};
    else if (*angles == vm::vec3d{0, -2, 0})
      result.direction = vm::vec3d{0, 0, -1};
    else
    {
      const auto pitch = vm::to_radians(angles->x());
      const auto yaw = vm::to_radians(angles->y());
      result.direction = vm::vec3d{
        std::cos(pitch) * std::cos(yaw),
        std::cos(pitch) * std::sin(yaw),
        -std::sin(pitch)};
    }
    const auto lip = classname == "func_button" && *lipValue == 0 ? 4.0 : *lipValue;
    // SDK pev->size includes a one-unit expansion on each side; authored brush bounds do
    // not.
    const auto size = bounds->size();
    result.distance = vm::dot(vm::abs(result.direction), size) - lip;
    if (classname == "func_button" && ((flags & 1) || std::abs(result.distance) < 1.0))
      result.distance = 0;
  }
  if (std::abs(result.distance) < 1e-6)
    result.problem = MotionProblem::Stationary;
  return result;
}
} // namespace tb::ui
