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

#include "ui/PlayModeShowPre.h"

#include <cmath>

namespace tb::ui
{
namespace
{

// The grace period is measured in simulation time, also at non-100 Hz tick rates.
constexpr auto DuckTapMs = 30.0;

bool onGround(const scs_player_state& state)
{
  return (state.flags & SCS_FL_ONGROUND) != 0;
}

float horizontalSpeed(const scs_player_state& state)
{
  return std::sqrt(
    state.velocity[0] * state.velocity[0] + state.velocity[1] * state.velocity[1]);
}

} // namespace

void PlayModeShowPre::reset(const scs_player_state& state)
{
  m_wasOnGround = onGround(state);
  m_lastDuckPressMs = -1.0e9;
  m_duckHeld = false;
  m_pre.reset();
}

void PlayModeShowPre::tick(const scs_move_sample& sample)
{
  const auto& cmd = sample.command;
  const auto& state = sample.after;
  const auto timeMs = sample.time_seconds * 1000.0;
  const auto duck = (cmd.buttons & SCS_IN_DUCK) != 0;
  if (duck && !m_duckHeld)
    m_lastDuckPressMs = timeMs;
  m_duckHeld = duck;

  const auto grounded = onGround(state);
  if (m_wasOnGround && !grounded && state.movetype == SCS_MOVETYPE_WALK)
  {
    if (
      (cmd.buttons & SCS_IN_JUMP) && !(sample.before.oldbuttons & SCS_IN_JUMP)
      && state.velocity[2] > 0.0f)
    {
      m_pre = Pre{PlayModeTakeoff::Jump, horizontalSpeed(sample.before), timeMs};
    }
    else if (!duck && timeMs - m_lastDuckPressMs <= DuckTapMs + 0.001)
    {
      m_pre = Pre{PlayModeTakeoff::Duck, horizontalSpeed(sample.before), timeMs};
    }
  }
  m_wasOnGround = grounded;
}

const std::optional<PlayModeShowPre::Pre>& PlayModeShowPre::pre() const
{
  return m_pre;
}

} // namespace tb::ui
