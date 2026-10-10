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

// A duck tap lifts the player off the ground on the tick after duck is released, so a
// takeoff shortly after a duck press counts as one.
constexpr auto DuckTapTicks = 3;

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
  m_ticksSinceDuckPress = 1000;
  m_duckHeld = false;
  m_pre.reset();
}

void PlayModeShowPre::tick(
  const scs_usercmd& cmd, const scs_player_state& state, const double timeMs)
{
  const auto duck = (cmd.buttons & SCS_IN_DUCK) != 0;
  m_ticksSinceDuckPress = duck && !m_duckHeld ? 0 : m_ticksSinceDuckPress + 1;
  m_duckHeld = duck;

  const auto grounded = onGround(state);
  if (m_wasOnGround && !grounded && state.movetype == SCS_MOVETYPE_WALK)
  {
    if (cmd.buttons & SCS_IN_JUMP)
    {
      m_pre = Pre{PlayModeTakeoff::Jump, horizontalSpeed(state), timeMs};
    }
    else if (!duck && m_ticksSinceDuckPress <= DuckTapTicks)
    {
      m_pre = Pre{PlayModeTakeoff::Duck, horizontalSpeed(state), timeMs};
    }
  }
  m_wasOnGround = grounded;
}

const std::optional<PlayModeShowPre::Pre>& PlayModeShowPre::pre() const
{
  return m_pre;
}

} // namespace tb::ui
