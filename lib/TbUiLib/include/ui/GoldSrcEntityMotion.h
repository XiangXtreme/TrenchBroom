/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include "vm/mat.h"
#include "vm/vec.h"

#include <optional>
#include <vector>

namespace tb::mdl
{
class EntityNode;
}

namespace tb::ui
{
enum class MotionProblem
{
  None,
  NoGeometry,
  InvalidProperty,
  Stationary,
  CompilerOverride,
  OriginConflict
};

/** Immutable preview data in authored world coordinates. No map nodes are retained. */
struct GoldSrcEntityMotion
{
  bool rotating = false;
  bool startOpen = false;
  bool bidirectional = false;
  MotionProblem problem = MotionProblem::None;
  double distance = 0;
  vm::vec3d direction{0, 0, 0};
  vm::vec3d angles{0, 0, 0};
  vm::vec3d origin{0, 0, 0};
  std::vector<std::vector<vm::vec3d>> faces;

  vm::mat4x4d transform(double progress, bool opposite = false) const;
};

// nullopt means that the entity class is outside this preview's scope.
std::optional<GoldSrcEntityMotion> goldSrcEntityMotion(const mdl::EntityNode& entity);
} // namespace tb::ui
