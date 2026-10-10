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

#pragma once

#include "cs/scs.h"

#include "vm/vec.h"

#include <cstdint>
#include <deque>

namespace tb::ui
{

enum class PlayModeButton
{
  Forward,
  Back,
  MoveLeft,
  MoveRight,
  Jump,
  Duck,
  Walk,
  Use,
  TurnLeft,
  TurnRight,
};

/**
 * Schedules timestamped input onto fixed 10 ms simulation ticks.
 *
 * The real CS client samples input once per frame and sends one usercmd per frame, so
 * at fps_max 100 every key press and mouse movement lands in the 10 ms command during
 * which it happened. The editor renders at a variable (and in debug builds, low) frame
 * rate, so input is queued with the time it happened and replayed tick by tick: each
 * tick sees exactly the input that occurred before its end time. Simulation results are
 * therefore independent of how often the view is rendered.
 */
class PlayModeTimeline
{
private:
  struct Event
  {
    enum class Type
    {
      Button,
      Look,
      Tap,
    };

    double timeMs = 0.0;
    Type type = Type::Button;
    PlayModeButton button = PlayModeButton::Jump;
    int keycode = 0;
    bool down = false;
    float pitchDelta = 0.0f;
    float yawDelta = 0.0f;
  };

  scs_client_input m_input{};
  vm::vec3f m_viewAngles{};
  std::deque<Event> m_events;
  double m_nextTickMs = 0.0;
  uint8_t m_msec = 10;
  scs_usercmd m_lastCommand{};

public:
  PlayModeTimeline();

  /**
   * Restarts the timeline with ticks of the given length (usercmd msec, 10 at fps_max
   * 100); the first tick ends one tick after startMs.
   */
  void reset(double startMs, const vm::vec3f& viewAngles, uint8_t msec = 10);

  /** cl_yawspeed: degrees per second turned by TurnLeft / TurnRight. */
  void setYawSpeed(float degreesPerSecond);

  void button(double timeMs, PlayModeButton button, int keycode, bool down);
  void releaseAllButtons(double timeMs);
  /** Turns the view by the given angles (degrees, positive pitch looks down). */
  void look(double timeMs, float pitchDelta, float yawDelta);
  /**
   * Presses and releases a button at the same instant, like an action bound to the mouse
   * wheel ("bind mwheelup +jump"): the button is down for exactly one usercmd.
   */
  void tap(double timeMs, PlayModeButton button);

  /**
   * Runs the ticks that end at or before nowMs, at most maxTicks of them; returns the
   * number of ticks run.
   */
  uint32_t advance(scs_context* context, double nowMs, uint32_t maxTicks = UINT32_MAX);

  /** Fraction of the current tick that has elapsed at nowMs, for interpolation. */
  float tickFraction(double nowMs) const;

  /** The view angles including input that has not been simulated yet. */
  vm::vec3f latestViewAngles() const;

  /** The usercmd of the most recent tick. */
  const scs_usercmd& lastCommand() const;

private:
  void apply(const Event& event);
  scs_kbutton& kbutton(PlayModeButton button);
};

} // namespace tb::ui
