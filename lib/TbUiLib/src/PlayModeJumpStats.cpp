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
constexpr auto SameHeight = 0.03125f;
constexpr auto MaxFrames = 500u;
constexpr auto MaxStrafes = 50u;

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
// Mirrors calcJumpOff: both signed velocity axes must find an edge before the
// nearest edge is tested for the drop that distinguishes a HighJump.
bool highJumpEdge(scs_context* context, const scs_player_state& state)
{
  if (!context || speed(state) < 0.1f)
    return false;
  const auto origin = vm::vec3f{state.origin[0], state.origin[1], state.origin[2]};
  const auto hull = state.flags & SCS_FL_DUCKING ? SCS_HULL_DUCKED : SCS_HULL_STANDING;
  auto trace = scs_trace{};
  const auto below = origin - vm::vec3f{0, 0, 10};
  scs_trace_hull(context, origin.v, below.v, hull, &trace);
  if (trace.startsolid || trace.fraction == 1.0f || trace.plane_normal[2] < 1.0f)
    return false;
  const auto depth = 10.0f * trace.fraction + 0.1f;
  const auto traceHeight = depth;
  const auto destination = origin - vm::vec3f{0, 0, traceHeight};
  auto boundaries = std::array<std::optional<vm::vec3f>, 2>{};
  for (size_t axis = 0; axis < 2; ++axis)
  {
    if (state.velocity[axis] == 0.0f)
      continue;
    auto start = destination;
    start[axis] += std::copysign(56.0f, state.velocity[axis]);
    scs_trace_hull(context, start.v, destination.v, hull, &trace);
    if (!trace.startsolid && trace.plane_normal[2] <= 0.7f)
      boundaries[axis] = vm::vec3f{trace.endpos[0], trace.endpos[1], trace.endpos[2]};
  }
  if (!boundaries[0] || !boundaries[1])
    return false;
  const auto distance = [&](const vm::vec3f& edge) {
    return vm::squared_length(vm::vec3f{edge.x(), edge.y(), origin.z()} - origin);
  };
  const auto& boundary = distance(*boundaries[0]) < distance(*boundaries[1])
                           ? *boundaries[0]
                           : *boundaries[1];
  const auto start = boundary + vm::vec3f{0, 0, traceHeight};
  const auto end = start - vm::vec3f{0, 0,
    (state.flags & SCS_FL_DUCKING ? 18.0f : 36.0f) + 34.0f};
  scs_trace_hull(context, start.v, end.v, hull, &trace);
  return !trace.startsolid && trace.fraction == 1.0f;
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
  m_showPre = {};
}

void PlayModeJumpStats::tick(
  scs_context* context,
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
  // PreThink sees the previous command's movement and the next buttons. Close
  // released segments before recording this command; gain belongs to this cmd,
  // not to before.oldbuttons (which additionally masks jump/duck transitions).
  const auto leftGround = grounded(before) && !grounded(after);
  const auto jumped = leftGround && after.velocity[2] > 0.0f
                      && (sample.command.buttons & SCS_IN_JUMP)
                      && !(before.oldbuttons & SCS_IN_JUMP);

  if (jumped)
  {
    auto takeoff = classifyTakeoff(sample);
    if (takeoff.technique == Technique::LongJump && highJumpEdge(context, before))
      takeoff.technique = Technique::HighJump;
    m_takeoff = takeoff;
    m_flight = Flight{feet(before)};
    auto& analysis = m_flight->analysis;
    analysis.takeoff = takeoff;
    analysis.technique = takeoff.technique;
    analysis.preSpeed = takeoff.preSpeed;
    analysis.weaponSpeed = before.maxspeed;
    analysis.maxSpeed = speed(after);
    m_airAction = AirAction::Jump;
    m_duckTaps = 0;
  }
  else if (leftGround)
  {
    if (duckReleased && !(before.flags & SCS_FL_DUCKING))
    {
      if (!m_landed || m_airAction != AirAction::DuckTap || m_groundFrames > 10)
        m_duckTaps = 0;
      ++m_duckTaps;
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
  if (!m_flight)
    return;
  auto& analysis = m_flight->analysis;
  analysis.touched = analysis.touched || sample.obstructed != 0;
  analysis.inDuck = (after.flags & SCS_FL_DUCKING) != 0;
  analysis.maxSpeed = std::max(analysis.maxSpeed, speed(after));
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
    !grounded(after) && after.velocity[2] <= 0.0f && from.z() > m_flight->startFeet.z()
    && to.z() <= m_flight->startFeet.z())
  {
    const auto fraction = (from.z() - m_flight->startFeet.z()) / (from.z() - to.z());
    // calcStrafes counts the bad final frame's duration, but not its gain/loss.
    recordStrafes(sample, true);
    finish(from + (to - from) * fraction, true);
  }
  else
  {
    recordStrafes(sample, false);
    if (grounded(after))
      finish(to, false);
  }
}

void PlayModeJumpStats::finish(const vm::vec3f& endFeet, const bool failed)
{
  for (size_t i = 0; i < m_flight->activeKeys.size(); ++i)
    closeStrafe(i);
  const auto delta = endFeet - m_flight->startFeet;
  auto result = std::move(m_flight->analysis);
  result.failed = failed || result.touched;
  result.distance = std::hypot(delta.x(), delta.y());
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
  if (linked)
  {
    if (m_airAction == AirAction::DuckTap && m_duckTaps)
    {
      type = (sample.after.flags & SCS_FL_DUCKING) ? Technique::Unsupported
             : m_duckTaps >= 3 ? Technique::MultiCountJump
             : m_duckTaps == 2 ? Technique::DoubleCountJump
             : m_landedDucked ? Technique::StandupCountJump
                              : Technique::CountJump;
    }
    else if (m_airAction == AirAction::Jump)
    {
      type = ((sample.before.flags & SCS_FL_DUCKING) && (sample.after.flags & SCS_FL_DUCKING))
               ? Technique::DuckBhop
             : m_landedDucked ? Technique::StandupBhop : Technique::Bhop;
    }
    else
      type = Technique::Unsupported;
  }
  else if (sample.before.fuser[1] > 0.0f || (sample.before.flags & SCS_FL_DUCKING))
    type = Technique::Unsupported;

  const auto post = speed(sample.before);
  // preBhop stores the first ground frame, not the speed after FOG frames of
  // friction. For LJ/HJ preFirstJump stores flPrevHorSpeed, i.e. cmd.before.
  auto takeoff = Takeoff{type, linked ? m_groundFrames : 0, m_duckTaps,
    linked ? m_landingSpeed : post, linked ? m_landingSpeed : post, post, m_timeMs};
  return takeoff;
}

void PlayModeJumpStats::closeStrafe(const size_t index)
{
  auto& flight = *m_flight;
  auto& active = flight.activeKeys[index];
  if (active && active->strafe.frames > active->overlaps)
  {
    if (flight.analysis.strafes.size() < MaxStrafes)
      flight.analysis.strafes.push_back(active->strafe);
    else
      flight.analysis.truncated = true;
  }
  active.reset();
}

void PlayModeJumpStats::recordStrafes(
  const scs_move_sample& sample, const bool badLastFrame)
{
  auto& flight = *m_flight;
  auto& result = flight.analysis;
  if (result.airFrames >= MaxFrames || result.strafes.size() >= MaxStrafes)
  {
    result.truncated = true;
    return;
  }
  // The plugin measures the previous command's movement/buttons, then uses the
  // current command only to detect a key transition.
  const auto frame = result.airFrames++;
  if (result.inDuck)
    ++result.duckFrames;
  constexpr auto keys = std::array{SCS_IN_FORWARD, SCS_IN_BACK, SCS_IN_MOVELEFT, SCS_IN_MOVERIGHT};
  constexpr auto names = std::array{'W', 'S', 'A', 'D'};
  const auto previousButtons = sample.before.oldbuttons;
  const auto currentButtons = sample.command.buttons;
  auto owner = std::optional<size_t>{};
  auto held = 0u;
  for (size_t i = 0; i < keys.size(); ++i)
  {
    if (previousButtons & keys[i])
      ++held;
  }
  for (size_t i = 0; i < keys.size(); ++i)
  {
    auto& active = flight.activeKeys[i];
    const auto previousHeld = (previousButtons & keys[i]) != 0;
    const auto currentHeld = (currentButtons & keys[i]) != 0;
    if (!active && ((frame == 0 && previousHeld) || (currentHeld && !previousHeld)))
      active = Flight::ActiveStrafe{Strafe{names[i]}, frame};
    if (!active)
      continue;
    if (previousHeld)
    {
      ++active->strafe.frames;
      active->overlaps = held > 1 ? active->overlaps + 1 : 0;
      if (!owner || active->start < flight.activeKeys[*owner]->start)
        owner = i;
    }
  }
  if (owner && !badLastFrame)
  {
    auto& strafe = flight.activeKeys[*owner]->strafe;
    const auto gain = speed(sample.after) - speed(sample.before);
    if (gain > 0.0f)
    {
      strafe.gain += gain;
      ++strafe.goodFrames;
    }
    else
      strafe.loss -= gain;
  }
  for (size_t i = 0; i < keys.size(); ++i)
  {
    if ((previousButtons & keys[i]) && (!(currentButtons & keys[i]) || badLastFrame))
      closeStrafe(i);
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
  const auto minDistance = result.technique == Technique::Bhop
                             || result.technique == Technique::StandupBhop
                             ? 150.0f
                             : 120.0f;
  const auto speedAdjustment = 0.7f * (result.weaponSpeed - 250.0f);
  return result.technique != Technique::Unsupported && (!result.failed || showFailed)
         && result.airFrames > 1
         && result.distance >= minDistance + speedAdjustment
         && (result.failed || result.distance <= 280.0f + speedAdjustment)
         && std::abs(result.heightDifference) < SameHeight;
}
} // namespace tb::ui
