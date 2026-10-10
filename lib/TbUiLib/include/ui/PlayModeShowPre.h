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

#include <optional>

namespace tb::ui
{

enum class PlayModeTakeoff
{
  Jump,
  Duck,
};

/**
 * Detects takeoffs and their prestrafe speed, like the showpre of KZ jump stats plugins:
 * the horizontal speed on the tick the player leaves the ground with a jump or a duck
 * tap. Leaving the ground any other way (walking off an edge, ladders) is not a takeoff.
 */
class PlayModeShowPre
{
public:
  struct Pre
  {
    PlayModeTakeoff takeoff;
    float speed;
    double timeMs;
  };

private:
  bool m_wasOnGround = true;
  int m_ticksSinceDuckPress = 1000;
  bool m_duckHeld = false;
  std::optional<Pre> m_pre;

public:
  void reset(const scs_player_state& state);

  /** Feed every simulated tick: its usercmd and the player state after it. */
  void tick(const scs_usercmd& cmd, const scs_player_state& state, double timeMs);

  /** The most recent takeoff, if any. */
  const std::optional<Pre>& pre() const;
};

} // namespace tb::ui
