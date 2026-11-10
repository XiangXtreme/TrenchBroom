/*
 Copyright (C) 2026 KreedzBroom contributors
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include "cs/scs.h"

#include "vm/vec.h"

#include <optional>
#include <vector>

namespace tb::render
{
class RenderBatch;
class RenderContext;
} // namespace tb::render

namespace tb::ui
{
/** Flat, convex support faces retained with the compiled Play collision world. */
struct PlayModeJumpSurface
{
  std::vector<vm::vec3f> vertices;
  uint32_t hullMask; // BSP hull order, like scs_brush::hull_mask
};

struct PlayModeJumpSpan
{
  vm::vec3f start;
  vm::vec3f end;
  float distance;
};

struct PlayModeEdgeGuide
{
  vm::vec3f position;  // Takeoff hint, inset from the support-probe transition
  vm::vec3f direction; // Outward platform-edge normal
  bool active;
};

struct PlayModeJumpGuide
{
  std::optional<PlayModeJumpSpan> span;
  std::optional<PlayModeEdgeGuide> edge;
};

/** PM_Friction's support probe, using the current standing/ducked collision hull.
 * Does not advance or change the player state. The caller controls whether the
 * configured friction multiplier actually causes extra slowdown. */
bool playModeEdgeFriction(scs_context* context, const scs_player_state& state);

/** Selects a flat platform edge within 128 units along lookDirection, then measures
 * a gap in [160, 300) units along its outward normal. Edge prediction follows
 * velocity; at rest, it previews movement perpendicular to the selected edge.
 * No map mutation or collision-world rebuild is involved. */
PlayModeJumpGuide measurePlayModeJumpGuide(
  scs_context* context,
  const std::vector<PlayModeJumpSurface>& surfaces,
  const scs_player_state& state,
  const vm::vec3f& lookDirection,
  bool measureSpan,
  bool measureEdge);

void renderPlayModeJumpGuide(
  const PlayModeJumpGuide& guide,
  render::RenderContext& renderContext,
  render::RenderBatch& renderBatch);
} // namespace tb::ui
