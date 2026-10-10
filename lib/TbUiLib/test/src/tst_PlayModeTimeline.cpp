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
#include "ui/PlayModeTimeline.h"

#include "vm/approx.h"
#include "vm/vec.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

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

using WorldPtr = std::unique_ptr<scs_world, WorldDeleter>;
using ContextPtr = std::unique_ptr<scs_context, ContextDeleter>;

WorldPtr createFloorWorld()
{
  // A large floor whose top is at z = 0.
  const scs_plane planes[] = {
    {{1, 0, 0}, 8192, 0},
    {{-1, 0, 0}, 8192, 0},
    {{0, 1, 0}, 8192, 0},
    {{0, -1, 0}, 8192, 0},
    {{0, 0, 1}, 0, 0},
    {{0, 0, -1}, 16, 0},
  };
  const auto brush = scs_brush{planes, 6, SCS_CONTENTS_SOLID, 0};
  auto desc = scs_world_desc{};
  desc.world_brushes = &brush;
  desc.world_brush_count = 1;

  scs_world* world = nullptr;
  REQUIRE(scs_world_build(&desc, &world, nullptr) == SCS_OK);
  return WorldPtr{world};
}

ContextPtr createPlayer(scs_world* world, const float height)
{
  auto context = ContextPtr{scs_init(nullptr)};
  scs_set_world(context.get(), world);
  const float origin[3] = {0, 0, height};
  scs_spawn(context.get(), origin, nullptr);
  return context;
}

struct ScriptedInput
{
  enum class Type
  {
    Press,
    Release,
    Look,
    Tap,
  };

  double timeMs;
  Type type;
  PlayModeButton button;
  float pitch;
  float yaw;
};

void push(PlayModeTimeline& timeline, const ScriptedInput& input)
{
  switch (input.type)
  {
  case ScriptedInput::Type::Press:
    timeline.button(input.timeMs, input.button, static_cast<int>(input.button) + 1, true);
    break;
  case ScriptedInput::Type::Release:
    timeline.button(
      input.timeMs, input.button, static_cast<int>(input.button) + 1, false);
    break;
  case ScriptedInput::Type::Look:
    timeline.look(input.timeMs, input.pitch, input.yaw);
    break;
  case ScriptedInput::Type::Tap:
    timeline.tap(input.timeMs, input.button);
    break;
  }
}

// Plays the script, delivering input and advancing the simulation every frameMs, the way
// the view does when it renders at 1000 / frameMs fps.
scs_player_state play(
  scs_world* world, const std::vector<ScriptedInput>& script, const double frameMs)
{
  auto context = createPlayer(world, 37);
  auto timeline = PlayModeTimeline{};
  timeline.reset(0.0, vm::vec3f{});

  auto next = script.begin();
  const auto endMs = script.back().timeMs + 100.0;
  for (auto now = 0.0; now < endMs; now += frameMs)
  {
    while (next != script.end() && next->timeMs <= now)
    {
      push(timeline, *next++);
    }
    timeline.advance(context.get(), now);
  }

  // Finish at the same time regardless of the frame length.
  while (next != script.end())
  {
    push(timeline, *next++);
  }
  timeline.advance(context.get(), endMs);

  auto state = scs_player_state{};
  scs_get_state(context.get(), &state);
  return state;
}

std::vector<ScriptedInput> randomStrafeScript()
{
  // Hold W, alternate A / D with matching mouse turns and wheel jumps, like air strafing.
  auto rng = std::mt19937{1234};
  auto jitter = std::uniform_real_distribution<double>{0.0, 9.99};
  auto turn = std::uniform_real_distribution<float>{0.2f, 3.0f};

  auto script = std::vector<ScriptedInput>{};
  script.push_back({1.3, ScriptedInput::Type::Press, PlayModeButton::Forward, 0, 0});
  auto left = true;
  for (auto t = 5.0; t < 3000.0; t += 10.0)
  {
    const auto time = t + jitter(rng);
    if (static_cast<int>(t) % 300 == 5)
    {
      const auto button = left ? PlayModeButton::MoveLeft : PlayModeButton::MoveRight;
      const auto other = left ? PlayModeButton::MoveRight : PlayModeButton::MoveLeft;
      script.push_back({time, ScriptedInput::Type::Release, other, 0, 0});
      script.push_back({time, ScriptedInput::Type::Press, button, 0, 0});
      left = !left;
    }
    script.push_back(
      {time,
       ScriptedInput::Type::Look,
       PlayModeButton::Jump,
       0.0f,
       (left ? -1.0f : 1.0f) * turn(rng)});
    if (static_cast<int>(t) % 70 == 5)
    {
      script.push_back({time, ScriptedInput::Type::Tap, PlayModeButton::Jump, 0, 0});
    }
  }
  return script;
}

} // namespace

TEST_CASE("PlayModeTimeline")
{
  const auto world = createFloorWorld();

  SECTION("results do not depend on the frame rate")
  {
    const auto script = randomStrafeScript();
    const auto smooth = play(world.get(), script, 1.0); // 1000 fps
    const auto debug = play(world.get(), script, 37.0); // a slow debug build
    const auto vsync = play(world.get(), script, 1000.0 / 60.0);

    CHECK(std::memcmp(&smooth, &debug, sizeof(smooth)) == 0);
    CHECK(std::memcmp(&smooth, &vsync, sizeof(smooth)) == 0);

    // sanity: the script actually moved the player (in circles, it turns constantly)
    CHECK(
      std::sqrt(smooth.origin[0] * smooth.origin[0] + smooth.origin[1] * smooth.origin[1])
      > 100.0f);
  }

  SECTION("a jump timed to the landing tick hops even at a low frame rate")
  {
    // Find the tick on which a player dropped from z = 100 lands.
    auto probe = createPlayer(world.get(), 100);
    auto probeTimeline = PlayModeTimeline{};
    probeTimeline.reset(0.0, vm::vec3f{});
    auto landingTickEnd = 0.0;
    for (auto tickEnd = 10.0; tickEnd < 2000.0; tickEnd += 10.0)
    {
      probeTimeline.advance(probe.get(), tickEnd);
      auto state = scs_player_state{};
      scs_get_state(probe.get(), &state);
      if (state.flags & SCS_FL_ONGROUND)
      {
        landingTickEnd = tickEnd;
        break;
      }
    }
    REQUIRE(landingTickEnd > 0.0);

    // Jump with the wheel during the tick after landing, but only render every 40 ms.
    // Before input was timestamped, the jump landed in the first tick of the frame
    // (still in the air) and was lost.
    auto player = createPlayer(world.get(), 100);
    auto timeline = PlayModeTimeline{};
    timeline.reset(0.0, vm::vec3f{});
    timeline.tap(landingTickEnd + 3.0, PlayModeButton::Jump);

    auto jumped = false;
    for (auto now = 0.0; now < landingTickEnd + 200.0; now += 40.0)
    {
      timeline.advance(player.get(), now);
      auto state = scs_player_state{};
      scs_get_state(player.get(), &state);
      jumped = jumped || state.velocity[2] > 200.0f;
    }
    CHECK(jumped);
  }

  SECTION("a tap presses the button for exactly one usercmd")
  {
    auto player = createPlayer(world.get(), 37);
    auto timeline = PlayModeTimeline{};
    timeline.reset(0.0, vm::vec3f{});
    timeline.tap(25.0, PlayModeButton::Duck); // duck scroll

    auto duckCommands = 0;
    for (auto tickEnd = 10.0; tickEnd <= 100.0; tickEnd += 10.0)
    {
      timeline.advance(player.get(), tickEnd);
      if (timeline.lastCommand().buttons & SCS_IN_DUCK)
      {
        ++duckCommands;
        CHECK(tickEnd == 30.0); // the tick in which the tap happened
      }
    }
    CHECK(duckCommands == 1);
  }

  SECTION("a tap does not release a held key of the same action")
  {
    auto player = createPlayer(world.get(), 37);
    auto timeline = PlayModeTimeline{};
    timeline.reset(0.0, vm::vec3f{});
    timeline.button(5.0, PlayModeButton::Duck, 'C', true);
    timeline.tap(15.0, PlayModeButton::Duck);

    for (auto tickEnd = 10.0; tickEnd <= 50.0; tickEnd += 10.0)
    {
      timeline.advance(player.get(), tickEnd);
      CHECK((timeline.lastCommand().buttons & SCS_IN_DUCK) != 0);
    }
  }

  SECTION("turn keys rotate the view at cl_yawspeed")
  {
    auto player = createPlayer(world.get(), 37);
    auto timeline = PlayModeTimeline{};
    timeline.reset(0.0, vm::vec3f{});
    timeline.setYawSpeed(100.0f);
    timeline.button(5.0, PlayModeButton::TurnLeft, 'L', true);
    timeline.advance(player.get(), 30.0);
    // half a frame's turn on the press tick, then 1 degree per 10 ms tick
    CHECK(timeline.latestViewAngles()[1] == vm::approx{2.5f, 0.01f});
    CHECK(timeline.lastCommand().viewangles[1] == vm::approx{2.5f, 0.01f});
  }

  SECTION("latestViewAngles includes input that is not simulated yet")
  {
    auto timeline = PlayModeTimeline{};
    timeline.reset(0.0, vm::vec3f{0, 90, 0});
    timeline.look(5.0, 100.0f, 10.0f);
    CHECK(timeline.latestViewAngles() == vm::vec3f{89, 100, 0}); // pitch is clamped
  }
}

} // namespace tb::ui
