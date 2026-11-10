/*
 Copyright (C) 2026 KreedzBroom contributors
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/PlayModeJumpStats.h"

#include <algorithm>
#include <cmath>

namespace tb::ui
{
namespace
{
bool grounded(const scs_player_state& state)
{
  return (state.flags & SCS_FL_ONGROUND) != 0 && state.ground_entity != -1;
}

float speed(const scs_player_state& state)
{
  return std::hypot(state.velocity[0], state.velocity[1]);
}

vm::vec3f feet(const scs_player_state& state)
{
  return vm::vec3f{state.origin[0], state.origin[1], state.origin[2]}
         - vm::vec3f{0, 0, state.flags & SCS_FL_DUCKING ? 18.0f : 36.0f};
}

bool walking(const scs_player_state& state)
{
  return state.movetype == SCS_MOVETYPE_WALK && state.waterlevel < 2
         && state.teleport_time == 0.0f;
}
} // namespace

void PlayModeJumpStats::reset()
{
  *this = PlayModeJumpStats{};
}

void PlayModeJumpStats::setGravity(const float gravity)
{
  m_gravity = gravity;
}

void PlayModeJumpStats::interrupt()
{
  m_initialized = false;
  m_flight.reset();
  m_result.reset();
  m_lastDuckMs = -1.0e9;
  m_showPre = {};
}

void PlayModeJumpStats::tick(
  scs_context* context,
  const std::vector<PlayModeJumpSurface>& surfaces,
  const scs_move_sample& sample,
  const bool assisted)
{
  const auto& before = sample.before;
  const auto& after = sample.after;
  m_timeMs = sample.time_seconds * 1000.0;
  if (assisted || !walking(before) || !walking(after))
  {
    m_assisted = true;
    interrupt();
    return;
  }
  if (before.teleport_count != after.teleport_count)
  {
    interrupt();
    return;
  }
  if (m_assisted)
  {
    // KZ-Rush also waits for normal ground movement below 260 after hook use.
    // Releasing the hook in midair must not authorize the remaining flight.
    interrupt();
    if (
      grounded(before) && grounded(after) && speed(before) < 260.0f
      && speed(after) < 260.0f)
      m_assisted = false;
    return;
  }
  if (!m_initialized)
  {
    m_showPre.reset(before);
    m_initialized = true;
  }
  m_showPre.tick(sample);
  if (grounded(before) && (sample.command.buttons & SCS_IN_DUCK))
    m_lastDuckMs = m_timeMs;

  if (
    grounded(before) && !grounded(after) && after.velocity[2] > 0.0f
    && (sample.command.buttons & SCS_IN_JUMP) && !(before.oldbuttons & SCS_IN_JUMP))
  {
    auto technique = Technique::LongJump;
    // Until action-chain classification ships, CJ/Bhop and duck starts must not
    // silently become LJ records. fuser2 is CS's previous-jump stamina timer.
    if (
      before.fuser[1] > 0.0f || m_timeMs - m_lastDuckMs < 500.0
      || (before.flags & SCS_FL_DUCKING))
      technique = Technique::Unsupported;
    else if (context && speed(before) > 0.1f)
    {
      const auto direction =
        vm::normalize(vm::vec3f{before.velocity[0], before.velocity[1], 0});
      const auto guide = measurePlayModeJumpGuide(context, surfaces, before, direction);
      if (guide.edge && guide.edge->highJump)
      {
        const auto lip = guide.edge->center + guide.edge->normal * 13.0f;
        if (vm::dot(lip - feet(before), guide.edge->normal) <= 56.0f)
          technique = Technique::HighJump;
      }
    }
    m_flight = Flight{
      technique, feet(before), speed(before), std::max(speed(before), speed(after))};
  }
  if (!m_flight)
    return;

  if (sample.obstructed)
  {
    // Walls, ceilings and slopes change the trajectory; don't label their
    // clipped movement as a clean jump or a same-height failure estimate.
    m_flight.reset();
    return;
  }
  m_flight->maxSpeed = std::max(m_flight->maxSpeed, speed(after));
  const auto from = feet(before);
  auto to = feet(after);
  if (grounded(after))
  {
    if (sample.has_floor_impact)
    {
      to = vm::vec3f{
        sample.floor_impact[0],
        sample.floor_impact[1],
        sample.floor_impact[2] - (after.flags & SCS_FL_DUCKING ? 18.0f : 36.0f)};
    }
    else if (!grounded(before))
    {
      // CategorizePosition can snap down up to two units without a FlyMove
      // impact. Reconstruct only that final airborne command and trace its hull.
      const auto dt = static_cast<float>(sample.command.msec) * 0.001f;
      const auto halfHeight = after.flags & SCS_FL_DUCKING ? 18.0f : 36.0f;
      const auto start = from + vm::vec3f{0, 0, halfHeight};
      auto destination = vm::vec3f{
        start.x() + after.velocity[0] * dt,
        start.y() + after.velocity[1] * dt,
        start.z() + (before.velocity[2] - 0.5f * m_gravity * before.gravity * dt) * dt};
      if (context)
      {
        auto trace = scs_trace{};
        scs_trace_hull(
          context,
          start.v,
          destination.v,
          after.flags & SCS_FL_DUCKING ? SCS_HULL_DUCKED : SCS_HULL_STANDING,
          &trace);
        if (trace.fraction < 1.0f && trace.plane_normal[2] > 0.999f)
        {
          to = vm::vec3f{
            trace.endpos[0],
            trace.endpos[1],
            trace.endpos[2] - (after.flags & SCS_FL_DUCKING ? 18.0f : 36.0f)};
        }
      }
    }
  }
  // Record a failure at the takeoff height, rather than measuring to a distant
  // pit floor. Interpolate feet, so switching to duck cannot introduce 18u Z.
  if (
    after.velocity[2] <= 0.0f && from.z() > m_flight->startFeet.z()
    && to.z() < m_flight->startFeet.z() - 0.125f)
  {
    const auto fraction = (from.z() - m_flight->startFeet.z()) / (from.z() - to.z());
    finish(from + (to - from) * fraction, true);
  }
  else if (grounded(after))
    finish(to, false);
}

void PlayModeJumpStats::finish(const vm::vec3f& endFeet, const bool failed)
{
  const auto& flight = *m_flight;
  const auto delta = endFeet - flight.startFeet;
  // KZ convention: horizontal center displacement plus the 32u player width.
  // This is not the geometrical platform gap, nor distance travelled along a path.
  m_result = Result{
    flight.technique,
    failed,
    std::hypot(delta.x(), delta.y()) + 32.0f,
    flight.preSpeed,
    flight.maxSpeed,
    delta.z(),
    m_timeMs};
  m_flight.reset();
}

const std::optional<PlayModeShowPre::Pre>& PlayModeJumpStats::pre() const
{
  return m_showPre.pre();
}

const std::optional<PlayModeJumpStats::Result>& PlayModeJumpStats::result() const
{
  return m_result;
}

double PlayModeJumpStats::timeMs() const
{
  return m_timeMs;
}

bool PlayModeJumpStats::visible(const Result& result, const bool showFailed)
{
  return result.technique != Technique::Unsupported && (!result.failed || showFailed)
         && result.distance >= 120.0f && result.distance <= 280.0f
         && std::abs(result.heightDifference) <= 0.125f;
}
} // namespace tb::ui
