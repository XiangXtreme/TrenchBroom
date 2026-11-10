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
  uint32_t hullMask;        // BSP hull order, like scs_brush::hull_mask
  bool allowTakeoff = true; // func_door may be a moving bhop block
};

struct PlayModeJumpSpan
{
  vm::vec3f start;
  vm::vec3f end;
  float distance;
};

struct PlayModeJumpGuide
{
  std::optional<PlayModeJumpSpan> span;
  struct Edge
  {
    vm::vec3f center;
    vm::vec3f normal;
    bool highJump;
  };
  std::optional<Edge> edge;
};

/** Selects a flat platform edge within 128 units along lookDirection, then measures
 * a gap in [160, 300) units along its outward normal while grounded. The takeoff
 * guide uses the local drop to distinguish LJ from HJ; its HJ offset is 13 units.
 * No map mutation or collision-world rebuild is involved. */
PlayModeJumpGuide measurePlayModeJumpGuide(
  scs_context* context,
  const std::vector<PlayModeJumpSurface>& surfaces,
  const scs_player_state& state,
  const vm::vec3f& lookDirection);

void renderPlayModeJumpGuide(
  const PlayModeJumpGuide& guide,
  render::RenderContext& renderContext,
  render::RenderBatch& renderBatch);
} // namespace tb::ui
