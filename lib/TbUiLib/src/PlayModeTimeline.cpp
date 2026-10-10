/*
 Copyright (C) 2026 KreedzBroom contributors

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "ui/PlayModeTimeline.h"

#include <algorithm>
#include <cmath>

namespace tb::ui
{
namespace
{

constexpr auto MaxPitch = 89.0f; // cl_pitchup / cl_pitchdown
// Taps use a key code that no keyboard key uses, so they combine with a held key like a
// second key bound to the same action.
constexpr auto TapKeycode = 0x7fff0001;
// After a long stall, do not replay more than this many ticks at once.
constexpr auto MaxCatchUpTicks = 25.0;

vm::vec3f turned(vm::vec3f angles, const float pitchDelta, const float yawDelta)
{
  angles[0] = std::clamp(angles[0] + pitchDelta, -MaxPitch, MaxPitch);
  angles[1] = std::fmod(angles[1] + yawDelta, 360.0f);
  return angles;
}

} // namespace

PlayModeTimeline::PlayModeTimeline()
{
  scs_client_input_defaults(&m_input);
}

void PlayModeTimeline::reset(
  const double startMs, const vm::vec3f& viewAngles, const uint8_t msec)
{
  m_msec = std::max(msec, uint8_t{1});
  scs_client_input_defaults(&m_input);
  m_viewAngles = viewAngles;
  m_events.clear();
  m_nextTickMs = startMs + static_cast<double>(m_msec);
}

void PlayModeTimeline::button(
  const double timeMs, const PlayModeButton button, const int keycode, const bool down)
{
  auto event = Event{};
  event.timeMs = timeMs;
  event.type = Event::Type::Button;
  event.button = button;
  event.keycode = keycode;
  event.down = down;
  m_events.push_back(event);
}

void PlayModeTimeline::setYawSpeed(const float degreesPerSecond)
{
  m_input.yawspeed = degreesPerSecond;
}

void PlayModeTimeline::releaseAllButtons(const double timeMs)
{
  for (const auto button :
       {PlayModeButton::Forward,
        PlayModeButton::Back,
        PlayModeButton::MoveLeft,
        PlayModeButton::MoveRight,
        PlayModeButton::Jump,
        PlayModeButton::Duck,
        PlayModeButton::Walk,
        PlayModeButton::Use,
        PlayModeButton::TurnLeft,
        PlayModeButton::TurnRight})
  {
    this->button(timeMs, button, -1, false);
  }
}

void PlayModeTimeline::look(
  const double timeMs, const float pitchDelta, const float yawDelta)
{
  auto event = Event{};
  event.timeMs = timeMs;
  event.type = Event::Type::Look;
  event.pitchDelta = pitchDelta;
  event.yawDelta = yawDelta;
  m_events.push_back(event);
}

void PlayModeTimeline::tap(const double timeMs, const PlayModeButton button)
{
  auto event = Event{};
  event.timeMs = timeMs;
  event.type = Event::Type::Tap;
  event.button = button;
  m_events.push_back(event);
}

uint32_t PlayModeTimeline::advance(
  scs_context* context, const double nowMs, const uint32_t maxTicks)
{
  const auto tickMs = static_cast<double>(m_msec);
  if (nowMs - m_nextTickMs > MaxCatchUpTicks * tickMs)
  {
    // The view was blocked for a long time (e.g. a modal dialog): skip ahead instead of
    // fast-forwarding through seconds of simulation.
    m_nextTickMs = nowMs - MaxCatchUpTicks * tickMs;
  }

  auto ticks = uint32_t{0};
  while (m_nextTickMs <= nowMs && ticks < maxTicks)
  {
    // Input that happened before the end of this tick belongs to its usercmd. Events
    // are delivered in order, so the queue is sorted by time.
    while (!m_events.empty() && m_events.front().timeMs <= m_nextTickMs)
    {
      apply(m_events.front());
      m_events.pop_front();
    }

    auto state = scs_player_state{};
    scs_get_state(context, &state);

    m_input.viewangles[0] = m_viewAngles[0];
    m_input.viewangles[1] = m_viewAngles[1];
    m_input.viewangles[2] = 0.0f;

    auto cmd = scs_usercmd{};
    scs_build_usercmd(&m_input, state.maxspeed, m_msec, &cmd);
    m_viewAngles[1] = m_input.viewangles[1]; // +left / +right turned the view
    scs_step(context, &cmd);
    m_lastCommand = cmd;

    m_nextTickMs += tickMs;
    ++ticks;
  }
  return ticks;
}

float PlayModeTimeline::tickFraction(const double nowMs) const
{
  const auto tickMs = static_cast<double>(m_msec);
  const auto fraction = 1.0 - (m_nextTickMs - nowMs) / tickMs;
  return static_cast<float>(std::clamp(fraction, 0.0, 1.0));
}

vm::vec3f PlayModeTimeline::latestViewAngles() const
{
  auto angles = m_viewAngles;
  for (const auto& event : m_events)
  {
    if (event.type == Event::Type::Look)
    {
      angles = turned(angles, event.pitchDelta, event.yawDelta);
    }
  }
  return angles;
}

const scs_usercmd& PlayModeTimeline::lastCommand() const
{
  return m_lastCommand;
}

void PlayModeTimeline::apply(const Event& event)
{
  switch (event.type)
  {
  case Event::Type::Button:
    if (event.down)
    {
      scs_key_down(&kbutton(event.button), event.keycode);
    }
    else
    {
      scs_key_up(&kbutton(event.button), event.keycode);
    }
    break;
  case Event::Type::Look:
    m_viewAngles = turned(m_viewAngles, event.pitchDelta, event.yawDelta);
    break;
  case Event::Type::Tap:
    scs_key_down(&kbutton(event.button), TapKeycode);
    scs_key_up(&kbutton(event.button), TapKeycode);
    break;
  }
}

scs_kbutton& PlayModeTimeline::kbutton(const PlayModeButton button)
{
  switch (button)
  {
  case PlayModeButton::Forward:
    return m_input.forward;
  case PlayModeButton::Back:
    return m_input.back;
  case PlayModeButton::MoveLeft:
    return m_input.moveleft;
  case PlayModeButton::MoveRight:
    return m_input.moveright;
  case PlayModeButton::Jump:
    return m_input.jump;
  case PlayModeButton::Duck:
    return m_input.duck;
  case PlayModeButton::Walk:
    return m_input.speed;
  case PlayModeButton::Use:
    return m_input.use;
  case PlayModeButton::TurnLeft:
    return m_input.left;
  case PlayModeButton::TurnRight:
    return m_input.right;
  }
  return m_input.use;
}

} // namespace tb::ui
