/*
 Copyright (C) 2026 KreedzBroom contributors
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "cs/scs.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushNode.h"
#include "mdl/Map.h"
#include "mdl/MapFixture.h"
#include "mdl/Map_Nodes.h"
#include "mdl/WorldNode.h"
#include "ui/CatchConfig.h"
#include "ui/PlayModeTimeline.h"
#include "ui/PlayModeTravel.h"
#include "ui/PlayModeWorld.h"

#include "kd/result.h"

#include <cmath>
#include <cstring>
#include <memory>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace tb::ui
{
namespace
{
struct ContextDeleter
{
  void operator()(scs_context* context) const { scs_shutdown(context); }
};
using ContextPtr = std::unique_ptr<scs_context, ContextDeleter>;

void addBox(mdl::Map& map, const vm::bbox3d& bounds)
{
  const auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};
  auto* brush = new mdl::BrushNode{builder.createCuboid(bounds, "stone") | kdl::value()};
  REQUIRE_FALSE(mdl::addNodes(map, {{&mdl::parentForNodes(map), {brush}}}).empty());
}

scs_player_state stateOf(scs_context* context)
{
  auto state = scs_player_state{};
  scs_get_state(context, &state);
  return state;
}
} // namespace

TEST_CASE("PlayModeTravel")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.create();
  addBox(map, {{-512, -256, -32}, {512, 256, 0}});
  addBox(map, {{256, -256, 0}, {288, 256, 512}});
  const auto world = buildPlayModeWorld(map);
  REQUIRE(world.world);
  auto config = scs_config{};
  scs_config_defaults(&config);
  config.noclip_speed_boost = 3;
  auto context = ContextPtr{scs_init(&config)};
  REQUIRE(context);
  REQUIRE(scs_set_world(context.get(), world.world.get()) == SCS_OK);
  const float origin[] = {0, 0, 128};
  scs_spawn(context.get(), origin, nullptr);
  auto travel = PlayModeTravel{};

  SECTION("hook fixes the aim point and applies the configured pull speed")
  {
    const auto speed = GENERATE(400.0f, 600.0f, 1200.0f);
    travel.setHookSpeed(speed);
    travel.hook(context.get(), {0, 0, 0}, true);
    REQUIRE(travel.hookAnchor());
    const auto anchor = *travel.hookAnchor();
    CHECK(anchor.x() == Catch::Approx(256).margin(0.05));
    // GoldSrc spawn lifts the origin by one unit before adding the 17-unit eye.
    CHECK(anchor.z() == Catch::Approx(146).margin(0.05));
    travel.hook(context.get(), {0, 90, 0}, true);
    CHECK(travel.hookAnchor() == anchor);
    travel.beforeStep(context.get(), 10);
    const auto state = stateOf(context.get());
    CHECK(
      vm::length(vm::vec3f{state.velocity[0], state.velocity[1], state.velocity[2]})
      == Catch::Approx(speed));
    CHECK(state.movetype == SCS_MOVETYPE_WALK);
    CHECK(state.velocity[0] > 0);
    CHECK(state.velocity[2] > 0);
    travel.hook(context.get(), {}, false);
    CHECK_FALSE(travel.hookAnchor());
    travel.beforeStep(context.get(), 10);
    CHECK(stateOf(context.get()).velocity[0] == state.velocity[0]);
  }
  SECTION("aiming into empty space does not attach or retry until a new press")
  {
    travel.hook(context.get(), {-89, 0, 0}, true);
    CHECK_FALSE(travel.hookAnchor());
    travel.hook(context.get(), {}, true);
    CHECK_FALSE(travel.hookAnchor());
    travel.hook(context.get(), {}, false);
    travel.hook(context.get(), {}, true);
    CHECK(travel.hookAnchor().has_value());
  }
  SECTION("hook keeps native collisions and does not cross the wall")
  {
    travel.hook(context.get(), {}, true);
    auto command = scs_usercmd{};
    command.msec = 10;
    for (auto i = 0; i < 100; ++i)
    {
      travel.beforeStep(context.get(), command.msec);
      scs_step(context.get(), &command);
    }
    CHECK(stateOf(context.get()).origin[0] <= 240.0f);
    CHECK(stateOf(context.get()).origin[0] > 220.0f);
  }
  SECTION("arriving at the anchor remains finite and does not reattach while held")
  {
    travel.hook(context.get(), {}, true);
    REQUIRE(travel.hookAnchor());
    auto state = stateOf(context.get());
    state.origin[0] = travel.hookAnchor()->x();
    state.origin[2] = travel.hookAnchor()->z();
    scs_set_state(context.get(), &state);
    travel.beforeStep(context.get(), 10);
    CHECK_FALSE(travel.hookAnchor());
    CHECK(stateOf(context.get()).velocity[0] == 0);
    CHECK(std::isfinite(stateOf(context.get()).velocity[2]));
    travel.hook(context.get(), {}, true);
    CHECK_FALSE(travel.hookAnchor());
  }
  SECTION("noclip blocks exit inside solids and restores walking in free space")
  {
    travel.hook(context.get(), {}, true);
    travel.toggleNoclip(context.get());
    CHECK_FALSE(travel.hookAnchor());
    CHECK(stateOf(context.get()).movetype == SCS_MOVETYPE_NOCLIP);
    const float inside[] = {272, 0, 128};
    scs_set_position(context.get(), inside);
    travel.toggleNoclip(context.get());
    CHECK(stateOf(context.get()).movetype == SCS_MOVETYPE_NOCLIP);
    CHECK(travel.noclipExitBlocked());
    scs_set_position(context.get(), origin);
    travel.toggleNoclip(context.get());
    CHECK(stateOf(context.get()).movetype == SCS_MOVETYPE_WALK);
    CHECK_FALSE(travel.noclipExitBlocked());
    CHECK(stateOf(context.get()).velocity[0] == 0);
    CHECK(stateOf(context.get()).server_fall_velocity == 0);
  }
  SECTION("noclip flies through the wall and Shift triples speed")
  {
    const auto boosted = GENERATE(false, true);
    auto timeline = PlayModeTimeline{};
    timeline.reset(0, {});
    timeline.toggleNoclip(1);
    timeline.button(2, PlayModeButton::Forward, 'W', true);
    if (boosted)
      timeline.button(3, PlayModeButton::Walk, 1, true);
    for (auto time = 10.0; time <= 1500; time += 10)
      timeline.advance(context.get(), time);
    const auto state = stateOf(context.get());
    CHECK(state.movetype == SCS_MOVETYPE_NOCLIP);
    CHECK(state.origin[0] > 288);
    // The first +forward command is 400 * 0.5; the next 149 are capped at 250.
    CHECK(state.origin[0] == Catch::Approx(boosted ? 1123.5 : 374.5).margin(0.1));
    CHECK(state.maxspeed == 250);
    if (boosted)
    {
      timeline.button(1501, PlayModeButton::Walk, 1, false);
      timeline.advance(context.get(), 1510);
      CHECK(
        stateOf(context.get()).origin[0] - state.origin[0]
        == Catch::Approx(2.5).margin(0.01));
    }
  }
  SECTION("normal Shift walking and movement state are unaffected by travel setup")
  {
    const auto booster = GENERATE(1.0f, 3.0f);
    config.noclip_speed_boost = booster;
    auto player = ContextPtr{scs_init(&config)};
    REQUIRE(scs_set_world(player.get(), world.world.get()) == SCS_OK);
    const float floorOrigin[] = {-256, 0, 36};
    scs_spawn(player.get(), floorOrigin, nullptr);
    auto timeline = PlayModeTimeline{};
    timeline.reset(0, {});
    timeline.button(1, PlayModeButton::Forward, 'W', true);
    timeline.button(2, PlayModeButton::Walk, 1, true);
    for (auto time = 10.0; time <= 1000; time += 10)
      timeline.advance(player.get(), time);
    CHECK(stateOf(player.get()).velocity[0] == Catch::Approx(130).margin(1));
    CHECK(stateOf(player.get()).maxspeed == 250);
  }
  SECTION("travel inputs are deterministic across render rates and reset cleanly")
  {
    const auto simulate = [&](const double frameMs) {
      auto player = ContextPtr{scs_init(&config)};
      REQUIRE(scs_set_world(player.get(), world.world.get()) == SCS_OK);
      scs_spawn(player.get(), origin, nullptr);
      auto scheduled = PlayModeTimeline{};
      scheduled.reset(0, {});
      scheduled.hook(5, true);
      scheduled.look(15, 0, 90);
      scheduled.hook(200, false);
      scheduled.toggleNoclip(300);
      scheduled.toggleNoclip(600);
      for (auto time = 0.0; time < 700; time += frameMs)
        scheduled.advance(player.get(), time);
      scheduled.advance(player.get(), 700);
      return stateOf(player.get());
    };
    const auto smooth = simulate(10);
    const auto slow = simulate(37);
    CHECK(std::memcmp(&smooth, &slow, sizeof(smooth)) == 0);
    auto timeline = PlayModeTimeline{};
    timeline.reset(0, {});
    timeline.hook(5, true);
    timeline.toggleNoclip(7);
    timeline.clearTravelInput();
    timeline.advance(context.get(), 10);
    CHECK_FALSE(timeline.travel().hookAnchor());
    CHECK(stateOf(context.get()).movetype == SCS_MOVETYPE_WALK);
  }
  SECTION("focus release cancels a held hook on the next tick")
  {
    auto timeline = PlayModeTimeline{};
    timeline.reset(0, {});
    timeline.hook(5, true);
    timeline.advance(context.get(), 10);
    REQUIRE(timeline.travel().hookAnchor());
    timeline.releaseAllButtons(15);
    timeline.advance(context.get(), 20);
    CHECK_FALSE(timeline.travel().hookAnchor());
  }
}
} // namespace tb::ui
