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
  double m_lastDuckPressMs = -1.0e9;
  bool m_duckHeld = false;
  std::optional<Pre> m_pre;

public:
  void reset(const scs_player_state& state);

  /** Feed native commands with the state before and after movement. Pre is the
   * horizontal speed before the takeoff command, matching the stats record. */
  void tick(const scs_move_sample& sample);

  /** The most recent takeoff, if any. */
  const std::optional<Pre>& pre() const;
};

} // namespace tb::ui
