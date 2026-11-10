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
  m_takeoff.reset();
  m_airAction = AirAction::None;
  m_landed = false;
  m_groundFrames = 0;
  m_duckTaps = 0;
  m_lastDuckReleaseMs = -1.0e9;
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
  const auto duckReleased =
    !(sample.command.buttons & SCS_IN_DUCK) && (before.oldbuttons & SCS_IN_DUCK);
  if (duckReleased)
    m_lastDuckReleaseMs = m_timeMs;
  const auto leftGround = grounded(before) && !grounded(after);
  const auto jumped = leftGround && after.velocity[2] > 0.0f
                      && (sample.command.buttons & SCS_IN_JUMP)
                      && !(before.oldbuttons & SCS_IN_JUMP);

  if (jumped)
  {
    auto takeoff = classifyTakeoff(sample);
    if (takeoff.technique == Technique::LongJump && context && speed(before) > 0.1f)
    {
      const auto direction =
        vm::normalize(vm::vec3f{before.velocity[0], before.velocity[1], 0});
      const auto guide = measurePlayModeJumpGuide(context, surfaces, before, direction);
      if (guide.edge && guide.edge->highJump)
      {
        const auto lip = guide.edge->center + guide.edge->normal * 13.0f;
        if (vm::dot(lip - feet(before), guide.edge->normal) <= 56.0f)
          takeoff.technique = Technique::HighJump;
      }
    }
    m_takeoff = takeoff;
    m_flight = Flight{
      takeoff.technique,
      feet(before),
      speed(before),
      std::max(speed(before), speed(after)),
      {},
      {}};
    m_flight->analysis.takeoff = takeoff;
    m_flight->analysis.technique = takeoff.technique;
    m_flight->analysis.preSpeed = takeoff.preSpeed;
    m_flight->analysis.maxSpeed = std::max(speed(before), speed(after));
    m_actionStartZ = feet(before).z();
    m_airAction = AirAction::Jump;
    m_duckTaps = 0;
  }
  else if (leftGround)
  {
    if (m_timeMs - m_lastDuckReleaseMs <= 30.001)
    {
      if (!m_landed || m_airAction != AirAction::DuckTap || m_groundFrames > 10)
        m_duckTaps = 0;
      ++m_duckTaps;
      m_actionStartZ = feet(before).z();
      m_airAction = AirAction::DuckTap;
    }
    else
    {
      m_airAction = AirAction::None;
      m_duckTaps = 0;
    }
  }

  if (!grounded(before) && grounded(after))
  {
    if (std::abs(feet(after).z() - m_actionStartZ) > 0.125f)
    {
      m_airAction = AirAction::None;
      m_duckTaps = 0;
    }
    m_landed = true;
    m_landedDucked = (after.flags & SCS_FL_DUCKING) != 0;
    m_landingSpeed = speed(after);
    m_groundFrames = 1;
  }
  else if (grounded(before) && grounded(after))
  {
    m_groundFrames = std::min(m_groundFrames + 1, 11u);
    if (m_groundFrames > 10)
    {
      m_landed = false;
      m_airAction = AirAction::None;
      m_duckTaps = 0;
    }
  }
  else if (leftGround)
  {
    m_groundFrames = 0;
    m_landed = false;
  }
  if (sample.obstructed)
  {
    // Walls, ceilings and slopes change the trajectory; don't label their
    // clipped movement as a clean jump or a same-height failure estimate.
    m_flight.reset();
    m_airAction = AirAction::None;
    m_duckTaps = 0;
    return;
  }
  if (!m_flight)
    return;
  recordStrafes(sample);
  m_flight->analysis.maxSpeed = std::max(m_flight->maxSpeed, speed(after));
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
  auto result = std::move(m_flight->analysis);
  result.technique = flight.technique;
  result.failed = failed;
  result.distance = std::hypot(delta.x(), delta.y()) + 32.0f;
  result.preSpeed = flight.preSpeed;
  result.maxSpeed = flight.maxSpeed;
  result.heightDifference = delta.z();
  result.timeMs = m_timeMs;
  m_result = std::move(result);
  m_flight.reset();
}

float PlayModeJumpStats::Strafe::efficiency() const
{
  return frames ? 100.0f * static_cast<float>(goodFrames) / static_cast<float>(frames)
                : 0.0f;
}

PlayModeJumpStats::Takeoff PlayModeJumpStats::classifyTakeoff(
  const scs_move_sample& sample)
{
  auto type = Technique::LongJump;
  const auto linked = m_landed && m_groundFrames <= 10;
  if (linked && m_airAction == AirAction::DuckTap && m_duckTaps)
  {
    type = (sample.after.flags & SCS_FL_DUCKING) ? Technique::Unsupported
           : m_duckTaps >= 3                     ? Technique::MultiCountJump
           : m_duckTaps == 2                     ? Technique::DoubleCountJump
           : m_landedDucked                      ? Technique::StandupCountJump
                                                 : Technique::CountJump;
  }
  else if (linked && m_airAction == AirAction::Jump)
  {
    type =
      ((sample.before.flags & SCS_FL_DUCKING) && (sample.after.flags & SCS_FL_DUCKING))
        ? Technique::DuckBhop
      : m_landedDucked ? Technique::StandupBhop
                       : Technique::Bhop;
  }
  else if (
    (m_landed && m_airAction == AirAction::None) || sample.before.fuser[1] > 0.0f
    || (sample.before.flags & SCS_FL_DUCKING))
    type = Technique::Unsupported;
  return {
    type,
    linked ? m_groundFrames : 0,
    m_duckTaps,
    linked ? m_landingSpeed : speed(sample.before),
    speed(sample.before),
    speed(sample.after),
    m_timeMs};
}

void PlayModeJumpStats::recordStrafes(const scs_move_sample& sample)
{
  if (grounded(sample.before))
    return; // Ground friction / jump stamina is part of the takeoff, not air gain.
  auto& flight = *m_flight;
  auto& result = flight.analysis;
  // Limit pathological flights and key chatter, without unbounded per-frame storage.
  if (result.airFrames >= 5000)
  {
    result.truncated = true;
    return;
  }
  ++result.airFrames;
  if (sample.after.flags & SCS_FL_DUCKING)
    ++result.duckFrames;
  const auto gain = speed(sample.after) - speed(sample.before);

  constexpr auto keys =
    std::array{SCS_IN_FORWARD, SCS_IN_BACK, SCS_IN_MOVELEFT, SCS_IN_MOVERIGHT};
  constexpr auto names = std::array{'W', 'S', 'A', 'D'};
  auto owner = std::optional<size_t>{};
  for (size_t i = 0; i < keys.size(); ++i)
  {
    if (!(sample.command.buttons & keys[i]))
    {
      flight.activeKeys[i].reset();
      continue;
    }
    if (!flight.activeKeys[i])
    {
      if (result.strafes.size() >= 64)
      {
        result.truncated = true;
        continue;
      }
      flight.activeKeys[i] = result.strafes.size();
      result.strafes.push_back(Strafe{names[i]});
    }
    if (!owner || *flight.activeKeys[i] < *owner)
      owner = flight.activeKeys[i];
  }
  for (size_t i = 0; i < keys.size(); ++i)
  {
    if (!flight.activeKeys[i])
      continue;
    auto& strafe = result.strafes[*flight.activeKeys[i]];
    ++strafe.frames;
    if (owner != flight.activeKeys[i])
      continue; // Credit gain once, to the earliest held key, as KZ-Rush does.
    if (gain > 0.0f)
    {
      strafe.gain += gain;
      ++strafe.goodFrames;
    }
    else
      strafe.loss -= gain;
  }
}

const PlayModeJumpStats::Result* PlayModeJumpStats::current() const
{
  return m_flight ? &m_flight->analysis : nullptr;
}

const std::optional<PlayModeJumpStats::Takeoff>& PlayModeJumpStats::takeoff() const
{
  return m_takeoff;
}

std::string_view PlayModeJumpStats::name(const Technique technique)
{
  switch (technique)
  {
  case Technique::LongJump:
    return "LongJump";
  case Technique::HighJump:
    return "HighJump";
  case Technique::CountJump:
    return "CountJump";
  case Technique::StandupCountJump:
    return "Stand-Up CountJump";
  case Technique::DoubleCountJump:
    return "Double CountJump";
  case Technique::MultiCountJump:
    return "Multi CountJump";
  case Technique::Bhop:
    return "BhopJump";
  case Technique::StandupBhop:
    return "Stand-Up Bhop";
  case Technique::DuckBhop:
    return "DuckBhop";
  case Technique::Unsupported:
    return "Unsupported";
  }
  return "Unsupported";
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
         && result.distance >= (result.technique == Technique::Bhop
                               || result.technique == Technique::StandupBhop
                               || result.technique == Technique::DuckBhop ? 150.0f : 120.0f)
         && result.distance <= 280.0f
         && std::abs(result.heightDifference) <= 0.125f;
}
} // namespace tb::ui
