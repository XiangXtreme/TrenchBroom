/*
 Copyright (C) 2026 KreedzBroom contributors
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/PlayModeTravel.h"

#include "vm/scalar.h"

#include <algorithm>
#include <cmath>

namespace tb::ui
{
namespace
{
void clearMotion(scs_player_state& state)
{
  std::fill_n(state.velocity, 3, 0.0f);
  std::fill_n(state.basevelocity, 3, 0.0f);
  state.flags &= ~SCS_FL_ONGROUND;
  state.ground_entity = -1;
  state.fall_velocity = 0;
  state.server_fall_velocity = 0;
  state.teleport_time = 0;
}
} // namespace

void PlayModeTravel::setHookSpeed(const float speed)
{
  m_hookSpeed = std::isfinite(speed) ? std::clamp(speed, 100.0f, 2000.0f) : 600.0f;
}

void PlayModeTravel::hook(
  scs_context* context, const vm::vec3f& viewAngles, const bool down)
{
  if (!down)
  {
    cancelHook();
    return;
  }
  if (m_hookHeld)
    return;
  m_hookHeld = true;
  auto state = scs_player_state{};
  scs_get_state(context, &state);
  if (state.movetype == SCS_MOVETYPE_NOCLIP)
    return;

  const auto pitch = vm::to_radians(viewAngles.x());
  const auto yaw = vm::to_radians(viewAngles.y());
  const auto forward = vm::vec3f{
    std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), -std::sin(pitch)};
  const auto from = vm::vec3f{state.origin[0], state.origin[1], state.origin[2]}
                    + vm::vec3f{state.view_ofs[0], state.view_ofs[1], state.view_ofs[2]};
  const auto to = from + forward * 8192.0f;
  auto trace = scs_trace{};
  scs_trace_hull(context, from.v, to.v, SCS_HULL_POINT, &trace);
  if (!trace.startsolid && !trace.allsolid && trace.fraction < 1.0f)
    m_hookAnchor = vm::vec3f{trace.endpos[0], trace.endpos[1], trace.endpos[2]};
}

void PlayModeTravel::cancelHook()
{
  m_hookAnchor.reset();
  m_hookHeld = false;
}

void PlayModeTravel::toggleNoclip(scs_context* context)
{
  auto state = scs_player_state{};
  scs_get_state(context, &state);
  const auto exiting = state.movetype == SCS_MOVETYPE_NOCLIP;
  const auto hull = state.flags & SCS_FL_DUCKING ? SCS_HULL_DUCKED : SCS_HULL_STANDING;
  if (exiting && !scs_position_is_free(context, state.origin, hull))
  {
    m_noclipExitBlocked = true;
    return;
  }
  cancelHook();
  m_noclipExitBlocked = false;
  state.movetype = exiting ? SCS_MOVETYPE_WALK : SCS_MOVETYPE_NOCLIP;
  clearMotion(state);
  scs_set_state(context, &state);
}

void PlayModeTravel::clearInput()
{
  cancelHook();
  m_noclipExitBlocked = false;
}

void PlayModeTravel::beforeStep(scs_context* context, const uint8_t msec)
{
  if (!m_hookAnchor)
    return;
  auto state = scs_player_state{};
  scs_get_state(context, &state);
  if (state.movetype == SCS_MOVETYPE_NOCLIP)
  {
    cancelHook();
    return;
  }
  const auto delta =
    *m_hookAnchor - vm::vec3f{state.origin[0], state.origin[1], state.origin[2]};
  const auto distance = vm::length(delta);
  // prokreedz.sma pulls toward a fixed aim point at 2 * kz_hook_speed (600 u/s
  // by default). Keep native collisions/gravity; bound the final tick to avoid
  // overshooting the anchor or dividing by zero, as guarded by New_Hook.sma.
  if (distance <= 25.0f)
  {
    m_hookAnchor.reset();
    std::fill_n(state.velocity, 3, 0.0f);
  }
  else
  {
    const auto speed =
      std::min(m_hookSpeed, (distance - 25.0f) / (static_cast<float>(msec) * 0.001f));
    const auto velocity = delta * (speed / distance);
    std::copy_n(velocity.v, 3, state.velocity);
  }
  scs_set_state(context, &state);
}

const std::optional<vm::vec3f>& PlayModeTravel::hookAnchor() const
{
  return m_hookAnchor;
}

bool PlayModeTravel::noclipExitBlocked() const
{
  return m_noclipExitBlocked;
}
} // namespace tb::ui
