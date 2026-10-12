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
#include "mdl/BrushBuilder.h"
#include "mdl/BrushNode.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityProperties.h"
#include "mdl/Map.h"
#include "mdl/MapFixture.h"
#include "mdl/Map_NodeVisibility.h"
#include "mdl/Map_Nodes.h"
#include "mdl/WorldNode.h"
#include "ui/CatchConfig.h"
#include "ui/PlayModeTimeline.h"
#include "ui/PlayModeWorld.h"

#include "kd/result.h"

#include "vm/approx.h"
#include "vm/bbox.h"
#include "vm/vec.h"

#include <memory>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{

struct ContextDeleter
{
  void operator()(scs_context* context) const { scs_shutdown(context); }
};

using ContextPtr = std::unique_ptr<scs_context, ContextDeleter>;

ContextPtr createContext(const PlayModeWorld& world)
{
  auto context = ContextPtr{scs_init(nullptr)};
  scs_set_world(context.get(), world.world.get());
  return context;
}

mdl::BrushNode* createCuboid(
  const mdl::Map& map, const vm::bbox3d& bounds, const std::string& material)
{
  const auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};
  return new mdl::BrushNode{builder.createCuboid(bounds, material) | kdl::value()};
}

mdl::EntityNode* createBrushEntity(
  mdl::Map& map, const std::string& classname, const vm::bbox3d& bounds)
{
  auto* entityNode =
    new mdl::EntityNode{mdl::Entity{{{mdl::EntityPropertyKeys::Classname, classname}}}};
  mdl::addNodes(map, {{&mdl::parentForNodes(map), {entityNode}}});
  mdl::addNodes(map, {{entityNode, {createCuboid(map, bounds, "wall")}}});
  return entityNode;
}

void setProperty(mdl::EntityNode& node, std::string key, std::string value)
{
  auto entity = node.entity();
  entity.addOrUpdateProperty(std::move(key), std::move(value));
  node.setEntity(std::move(entity));
}

// Sweeps the standing hull along +x at walking height and returns where it stops.
float blockedAtX(scs_context* context, const float toX)
{
  const float start[3] = {0, 0, 37};
  const float end[3] = {toX, 0, 37};
  auto trace = scs_trace{};
  scs_trace_hull(context, start, end, SCS_HULL_STANDING, &trace);
  return trace.endpos[0];
}

} // namespace

TEST_CASE("buildPlayModeWorld")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.create();

  // A floor whose top is at z = 0.
  mdl::addNodes(
    map,
    {{&mdl::parentForNodes(map),
      {createCuboid(map, vm::bbox3d{{-512, -512, -16}, {512, 512, 0}}, "floor")}}});

  SECTION("the player stands on world brushes")
  {
    const auto world = buildPlayModeWorld(map);
    REQUIRE(world.world);
    CHECK(world.worldBrushCount == 1u);
    CHECK(world.entityCount == 0u);

    auto context = createContext(world);
    const float spawn[3] = {0, 0, 40};
    scs_spawn(context.get(), spawn, nullptr);
    auto cmd = scs_usercmd{};
    cmd.msec = 10;
    for (int i = 0; i < 50; ++i)
    {
      scs_step(context.get(), &cmd);
    }

    auto state = scs_player_state{};
    scs_get_state(context.get(), &state);
    CHECK((state.flags & SCS_FL_ONGROUND) != 0);
    CHECK(state.origin[2] == vm::approx{36.03125f, 0.001f});
  }

  SECTION("CLIP blocks the player but not hull 0, ORIGIN and HINT brushes vanish")
  {
    auto* clip = createCuboid(map, vm::bbox3d{{100, -64, 0}, {116, 64, 128}}, "CLIP");
    auto* origin = createCuboid(map, vm::bbox3d{{-200, -8, 0}, {-184, 8, 16}}, "ORIGIN");
    auto* hint = createCuboid(map, vm::bbox3d{{200, -64, 0}, {216, 64, 128}}, "HINT");
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {clip, origin, hint}}});

    const auto world = buildPlayModeWorld(map);
    REQUIRE(world.world);
    CHECK(world.worldBrushCount == 2u);
    CHECK(world.skippedBrushCount == 2u);

    auto context = createContext(world);
    CHECK(
      blockedAtX(context.get(), 300.0f) == vm::approx{100.0f - 16.0f - 0.03125f, 0.01f});

    const float insideClip[3] = {108, 0, 64};
    CHECK(scs_point_contents(context.get(), insideClip) == SCS_CONTENTS_EMPTY);
  }

  SECTION("NOCLIP and CLIPHULLn decide which hulls a brush is in")
  {
    auto* noclip = createCuboid(map, vm::bbox3d{{100, -64, 0}, {116, 64, 128}}, "NOCLIP");
    auto* duckClip =
      createCuboid(map, vm::bbox3d{{150, -64, 0}, {166, 64, 128}}, "CLIPHULL3");
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {noclip, duckClip}}});

    const auto world = buildPlayModeWorld(map);
    REQUIRE(world.world);
    auto context = createContext(world);

    // standing walks through both, only the ducked hull meets CLIPHULL3
    CHECK(blockedAtX(context.get(), 300.0f) == vm::approx{300.0f});
    const float inNoclip[3] = {108, 0, 64};
    CHECK(scs_point_contents(context.get(), inNoclip) == SCS_CONTENTS_SOLID);
    const float nearDuckClip[3] = {140, 0, 37};
    CHECK(scs_position_is_free(context.get(), nearDuckClip, SCS_HULL_STANDING) != 0);
    CHECK(scs_position_is_free(context.get(), nearDuckClip, SCS_HULL_DUCKED) == 0);

    // ZHLT has neither: NOCLIP is a plain solid texture, CLIPHULL3 a plain CLIP
    const auto zhltWorld = buildPlayModeWorld(map, {.compiler = SCS_COMPILER_ZHLT});
    REQUIRE(zhltWorld.world);
    CHECK(
      blockedAtX(createContext(zhltWorld).get(), 300.0f)
      == vm::approx{100.0f - 16.0f - 0.03125f, 0.01f});
  }

  SECTION("cliptype comes from the options")
  {
    const auto world = buildPlayModeWorld(map, {.clipType = SCS_CLIPTYPE_SIMPLE});
    CHECK(world.clipType == SCS_CLIPTYPE_SIMPLE);
  }

  SECTION("large hull is omitted by default and can be enabled explicitly")
  {
    auto* wall = createCuboid(map, vm::bbox3d{{100, -64, 0}, {116, 64, 128}}, "wall");
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {wall}}});

    const auto defaultWorld = buildPlayModeWorld(map);
    REQUIRE(defaultWorld.world);
    auto defaultContext = createContext(defaultWorld);
    auto trace = scs_trace{};
    const float start[3] = {0, 0, 0};
    const float end[3] = {300, 0, 0};
    scs_trace_hull(defaultContext.get(), start, end, SCS_HULL_LARGE, &trace);
    CHECK(trace.fraction == 1.0f);

    const auto allHullsWorld = buildPlayModeWorld(map, {.hullMask = 0b1111u});
    REQUIRE(allHullsWorld.world);
    auto allHullsContext = createContext(allHullsWorld);
    trace = scs_trace{};
    scs_trace_hull(allHullsContext.get(), start, end, SCS_HULL_LARGE, &trace);
    CHECK(trace.fraction < 1.0f);
  }

  SECTION("liquid materials become water contents")
  {
    mdl::addNodes(
      map,
      {{&mdl::parentForNodes(map),
        {createCuboid(map, vm::bbox3d{{-64, -64, 0}, {64, 64, 64}}, "!waterblue")}}});

    const auto world = buildPlayModeWorld(map);
    REQUIRE(world.world);

    auto context = createContext(world);
    const float inside[3] = {0, 0, 32};
    CHECK(scs_point_contents(context.get(), inside) == SCS_CONTENTS_WATER);
    CHECK(blockedAtX(context.get(), 300.0f) == vm::approx{300.0f});
  }

  SECTION("triggers are not solid, func_wall is")
  {
    createBrushEntity(map, "trigger_multiple", vm::bbox3d{{100, -64, 0}, {116, 64, 128}});

    auto world = buildPlayModeWorld(map);
    REQUIRE(world.world);
    CHECK(world.entityCount == 0u);
    CHECK(blockedAtX(createContext(world).get(), 300.0f) == vm::approx{300.0f});

    createBrushEntity(map, "func_wall", vm::bbox3d{{150, -64, 0}, {166, 64, 128}});
    world = buildPlayModeWorld(map);
    REQUIRE(world.world);
    CHECK(world.entityCount == 1u);
    CHECK(
      blockedAtX(createContext(world).get(), 300.0f)
      == vm::approx{150.0f - 16.0f - 0.03125f, 0.01f});
  }

  SECTION("func_ladder can be climbed but does not block")
  {
    createBrushEntity(map, "func_ladder", vm::bbox3d{{100, -32, 0}, {108, 32, 512}});

    const auto world = buildPlayModeWorld(map);
    REQUIRE(world.world);
    CHECK(world.entityCount == 1u);

    auto context = createContext(world);
    const float spawn[3] = {60, 0, 36};
    scs_spawn(context.get(), spawn, nullptr);

    auto cmd = scs_usercmd{};
    cmd.msec = 10;
    cmd.viewangles[0] = -60.0f; // look up
    cmd.forwardmove = 250.0f;
    cmd.buttons = SCS_IN_FORWARD;
    for (int i = 0; i < 150; ++i)
    {
      scs_step(context.get(), &cmd);
    }

    auto state = scs_player_state{};
    scs_get_state(context.get(), &state);
    CHECK(state.movetype == SCS_MOVETYPE_FLY);
    CHECK(state.origin[2] > 200.0f);
  }

  SECTION("trigger_teleport")
  {
    auto* trigger = createBrushEntity(
      map, "trigger_teleport", vm::bbox3d{{100, -64, 0}, {116, 64, 128}});
    setProperty(*trigger, "target", "exit");
    auto* destination = new mdl::EntityNode{mdl::Entity{{
      {"classname", "info_teleport_destination"},
      {"targetname", "exit"},
      {"origin", "300 100 64"},
      {"angles", "10 90 0"},
    }}};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {destination}}});

    SECTION("touching with the player hull teleports without blocking")
    {
      const auto world = buildPlayModeWorld(map);
      REQUIRE(world.world);
      CHECK(world.teleportCount == 1u);
      CHECK(world.entityCount == 0u);
      CHECK(world.warnings.empty());
      auto context = createContext(world);
      CHECK(blockedAtX(context.get(), 300.0f) == vm::approx{300.0f});

      // The center is outside the brush, but the standing player overlaps it.
      const float spawn[3] = {90, 0, 40};
      scs_spawn(context.get(), spawn, nullptr);
      auto state = scs_player_state{};
      scs_get_state(context.get(), &state);
      state.velocity[0] = 100;
      state.basevelocity[1] = 20;
      scs_set_state(context.get(), &state);
      auto cmd = scs_usercmd{};
      cmd.msec = 10;
      scs_step(context.get(), &cmd);
      scs_get_state(context.get(), &state);

      CHECK(state.teleport_count == 1u);
      CHECK(
        vm::vec3f{state.origin[0], state.origin[1], state.origin[2]}
        == vm::vec3f{300, 100, 101});
      CHECK(
        vm::vec3f{state.v_angle[0], state.v_angle[1], state.v_angle[2]}
        == vm::vec3f{10, 90, 0});
      for (int i = 0; i < 3; ++i)
      {
        CHECK(state.velocity[i] == 0.0f);
        CHECK(state.basevelocity[i] == 0.0f);
      }
      CHECK((state.flags & SCS_FL_ONGROUND) == 0);
      CHECK(state.ground_entity == -1);
    }

    SECTION("ducking uses the ducked destination height")
    {
      const auto world = buildPlayModeWorld(map);
      REQUIRE(world.world);
      auto context = createContext(world);
      const float spawn[3] = {90, 0, 40};
      scs_spawn(context.get(), spawn, nullptr);
      auto cmd = scs_usercmd{};
      cmd.msec = 10;
      cmd.buttons = SCS_IN_DUCK; // completes immediately in the air
      scs_step(context.get(), &cmd);
      auto state = scs_player_state{};
      scs_get_state(context.get(), &state);
      CHECK((state.flags & SCS_FL_DUCKING) != 0);
      CHECK(state.teleport_count == 1u);
      CHECK(state.origin[2] == 83.0f);
    }

    SECTION("gaps between trigger brushes do not teleport")
    {
      mdl::addNodes(
        map,
        {{trigger,
          {createCuboid(map, vm::bbox3d{{200, -64, 0}, {216, 64, 128}}, "wall")}}});
      const auto world = buildPlayModeWorld(map);
      REQUIRE(world.world);
      auto context = createContext(world);
      const float spawn[3] = {150, 0, 40};
      scs_spawn(context.get(), spawn, nullptr);
      auto cmd = scs_usercmd{};
      cmd.msec = 10;
      scs_step(context.get(), &cmd);
      auto state = scs_player_state{};
      scs_get_state(context.get(), &state);
      CHECK(state.teleport_count == 0u);
      CHECK(state.origin[0] == 150.0f);
    }

    SECTION("the first matching targetname wins")
    {
      auto* other = new mdl::EntityNode{mdl::Entity{{
        {"classname", "info_teleport_destination"},
        {"targetname", "exit"},
        {"origin", "400 200 64"},
      }}};
      mdl::addNodes(map, {{&mdl::parentForNodes(map), {other}}});
      const auto world = buildPlayModeWorld(map);
      auto context = createContext(world);
      const float spawn[3] = {90, 0, 40};
      scs_spawn(context.get(), spawn, nullptr);
      auto cmd = scs_usercmd{};
      cmd.msec = 100; // second half must preserve the server's new view angles
      scs_step(context.get(), &cmd);
      auto state = scs_player_state{};
      scs_get_state(context.get(), &state);
      CHECK(state.teleport_count == 1u);
      CHECK(state.origin[0] == 300.0f);
      CHECK(state.origin[1] == 100.0f);
      CHECK(state.v_angle[0] == 10.0f);
      CHECK(state.v_angle[1] == 90.0f);
    }

    SECTION("the timeline adopts destination angles and keeps future mouse input")
    {
      const auto world = buildPlayModeWorld(map);
      auto context = createContext(world);
      const float spawn[3] = {90, 0, 40};
      scs_spawn(context.get(), spawn, nullptr);
      auto timeline = PlayModeTimeline{};
      timeline.reset(0, {});
      timeline.look(15, -5, 20);
      timeline.advance(context.get(), 10);
      CHECK(timeline.latestViewAngles() == vm::vec3f{5, 110, 0});
      timeline.advance(context.get(), 30);
      auto state = scs_player_state{};
      scs_get_state(context.get(), &state);
      CHECK(state.teleport_count == 1u);
      CHECK(state.v_angle[0] == vm::approx{5.0f, 0.01f});
      CHECK(state.v_angle[1] == vm::approx{110.0f, 0.01f});
    }

    SECTION("hidden trigger brushes are ignored")
    {
      mdl::hideNodes(map, {trigger->children().front()});
      const auto world = buildPlayModeWorld(map);
      REQUIRE(world.world);
      CHECK(world.teleportCount == 0u);
    }

    SECTION("NOCLIENTS excludes the player")
    {
      setProperty(*trigger, "spawnflags", "2");
      const auto world = buildPlayModeWorld(map);
      REQUIRE(world.world);
      CHECK(world.teleportCount == 0u);
    }

    SECTION("unresolved targets and unsupported masters are reported")
    {
      SECTION("missing target")
      {
        setProperty(*trigger, "target", "");
      }
      SECTION("unknown target")
      {
        setProperty(*trigger, "target", "unknown");
      }
      SECTION("master gate")
      {
        setProperty(*trigger, "master", "gate");
      }
      const auto world = buildPlayModeWorld(map);
      REQUIRE(world.world);
      CHECK(world.teleportCount == 0u);
      CHECK(world.warnings.size() == 1u);
    }
  }

  SECTION("hidden brushes are ignored")
  {
    auto* wall = createCuboid(map, vm::bbox3d{{100, -64, 0}, {116, 64, 128}}, "wall");
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {wall}}});
    mdl::hideNodes(map, {wall});

    const auto world = buildPlayModeWorld(map);
    REQUIRE(world.world);
    CHECK(world.worldBrushCount == 1u);
    CHECK(blockedAtX(createContext(world).get(), 300.0f) == vm::approx{300.0f});
  }

  SECTION("spawn points are collected")
  {
    auto* spawn = new mdl::EntityNode{mdl::Entity{{
      {mdl::EntityPropertyKeys::Classname, "info_player_start"},
      {mdl::EntityPropertyKeys::Origin, "10 20 30"},
      {"angle", "90"},
    }}};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {spawn}}});

    const auto world = buildPlayModeWorld(map);
    REQUIRE(world.spawnPoints.size() == 1u);
    CHECK(world.spawnPoints[0].classname == "info_player_start");
    CHECK(world.spawnPoints[0].origin == vm::vec3f{10, 20, 30});
    CHECK(world.spawnPoints[0].angles == vm::vec3f{0, 90, 0});
  }
}

} // namespace tb::ui
