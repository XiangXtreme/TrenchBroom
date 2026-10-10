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

#include "cs/scs.h"
#include "ui/CatchConfig.h"
#include "ui/PlayModeShowPre.h"

#include <memory>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{

struct WorldDeleter
{
  void operator()(scs_world* world) const { scs_world_release(world); }
};

struct ContextDeleter
{
  void operator()(scs_context* context) const { scs_shutdown(context); }
};

// A platform whose top is at z = 0 and that ends at x = 0 (a ledge facing +x).
std::unique_ptr<scs_world, WorldDeleter> createLedgeWorld()
{
  const scs_plane planes[] = {
    {{1, 0, 0}, 0, 0},
    {{-1, 0, 0}, 4096, 0},
    {{0, 1, 0}, 4096, 0},
    {{0, -1, 0}, 4096, 0},
    {{0, 0, 1}, 0, 0},
    {{0, 0, -1}, 16, 0},
  };
  const auto brush = scs_brush{planes, 6, SCS_CONTENTS_SOLID, 0};
  auto desc = scs_world_desc{};
  desc.world_brushes = &brush;
  desc.world_brush_count = 1;
  scs_world* world = nullptr;
  REQUIRE(scs_world_build(&desc, &world, nullptr) == SCS_OK);
  return std::unique_ptr<scs_world, WorldDeleter>{world};
}

class Player
{
private:
  std::unique_ptr<scs_context, ContextDeleter> m_context;
  PlayModeShowPre m_showPre;
  double m_timeMs = 0.0;

public:
  Player(scs_world* world, const float x)
    : m_context{scs_init(nullptr)}
  {
    scs_set_world(m_context.get(), world);
    const float origin[3] = {x, 0, 36};
    scs_spawn(m_context.get(), origin, nullptr);
    m_showPre.reset(state());
  }

  scs_player_state state() const
  {
    auto result = scs_player_state{};
    scs_get_state(m_context.get(), &result);
    return result;
  }

  void run(const uint16_t buttons, const int ticks)
  {
    for (int i = 0; i < ticks; ++i)
    {
      auto cmd = scs_usercmd{};
      cmd.msec = 10;
      cmd.buttons = buttons;
      if (buttons & SCS_IN_FORWARD)
      {
        cmd.forwardmove = 400.0f; // facing +x
      }
      scs_step(m_context.get(), &cmd);
      m_timeMs += 10.0;
      m_showPre.tick(cmd, state(), m_timeMs);
    }
  }

  const std::optional<PlayModeShowPre::Pre>& pre() const { return m_showPre.pre(); }
};

} // namespace

TEST_CASE("PlayModeShowPre")
{
  const auto world = createLedgeWorld();

  SECTION("a jump shows the takeoff speed")
  {
    auto player = Player{world.get(), -3000.0f};
    player.run(SCS_IN_FORWARD, 200); // reach knife speed
    CHECK_FALSE(player.pre());

    player.run(SCS_IN_FORWARD | SCS_IN_JUMP, 1);
    REQUIRE(player.pre());
    CHECK(player.pre()->takeoff == PlayModeTakeoff::Jump);
    CHECK(player.pre()->speed > 249.0f);
    CHECK(player.pre()->speed < 251.0f);
    CHECK(player.pre()->timeMs == 2010.0);
  }

  SECTION("a duck tap shows the takeoff speed")
  {
    auto player = Player{world.get(), -3000.0f};
    player.run(SCS_IN_FORWARD, 200);
    player.run(SCS_IN_FORWARD | SCS_IN_DUCK, 1); // the wheel tap
    player.run(SCS_IN_FORWARD, 2);               // released: lifted off the ground

    REQUIRE(player.pre());
    CHECK(player.pre()->takeoff == PlayModeTakeoff::Duck);
    CHECK(player.pre()->speed > 200.0f);
  }

  SECTION("walking off a ledge is not a takeoff")
  {
    auto player = Player{world.get(), -100.0f};
    player.run(SCS_IN_FORWARD, 100);
    CHECK(player.state().origin[2] < 0.0f); // fell off
    CHECK_FALSE(player.pre());
  }

  SECTION("walking off a ledge while holding duck is not a takeoff")
  {
    auto player = Player{world.get(), -100.0f};
    player.run(SCS_IN_FORWARD | SCS_IN_DUCK, 400); // ducked walking is slow
    CHECK(player.state().origin[2] < 0.0f);
    CHECK_FALSE(player.pre());
  }
}

} // namespace tb::ui
