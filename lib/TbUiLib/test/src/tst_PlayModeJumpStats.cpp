/*
 Copyright (C) 2026 KreedzBroom contributors
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "cs/scs.h"
#include "ui/CatchConfig.h"
#include "ui/PlayModeJumpStats.h"
#include "ui/PlayModeTimeline.h"

#include "vm/approx.h"

#include <memory>
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

std::unique_ptr<scs_world, WorldDeleter> makeWorld(const bool ledge)
{
  const scs_plane planes[] = {
    {{1, 0, 0}, ledge ? 0.0 : 4096.0, 0},
    {{-1, 0, 0}, 4096, 0},
    {{0, 1, 0}, 4096, 0},
    {{0, -1, 0}, 4096, 0},
    {{0, 0, 1}, 0, 0},
    {{0, 0, -1}, 128, 0}};
  const auto brush = scs_brush{planes, 6, SCS_CONTENTS_SOLID, 0};
  auto desc = scs_world_desc{};
  desc.world_brushes = &brush;
  desc.world_brush_count = 1;
  desc.cliptype = SCS_CLIPTYPE_LEGACY;
  scs_world* world = nullptr;
  REQUIRE(scs_world_build(&desc, &world, nullptr) == SCS_OK);
  return std::unique_ptr<scs_world, WorldDeleter>{world};
}

std::unique_ptr<scs_context, ContextDeleter> makePlayer(scs_world* world)
{
  auto context = std::unique_ptr<scs_context, ContextDeleter>{scs_init(nullptr)};
  REQUIRE(context);
  scs_set_world(context.get(), world);
  const float origin[] = {-20, 0, 36};
  scs_spawn(context.get(), origin, nullptr);
  auto cmd = scs_usercmd{};
  cmd.msec = 10;
  scs_step(context.get(), &cmd);
  auto state = scs_player_state{};
  scs_get_state(context.get(), &state);
  state.velocity[0] = 250;
  scs_set_state(context.get(), &state);
  return context;
}

struct Recorder
{
  scs_context* context;
  PlayModeJumpStats stats;
  std::vector<scs_move_sample> samples;
  bool assisted = false;

  explicit Recorder(scs_context* player)
    : context{player}
  {
  }

  void step(const scs_usercmd& cmd)
  {
    scs_step_observed(
      context,
      &cmd,
      [](void* user, const scs_move_sample* sample) {
        auto& recorder = *static_cast<Recorder*>(user);
        recorder.samples.push_back(*sample);
        recorder.stats.tick(
          recorder.context, *sample, recorder.assisted);
      },
      this);
  }
};
} // namespace

TEST_CASE("PlayModeJumpStats")
{
  SECTION("native observations retain quantized input and do not change movement")
  {
    const auto world = makeWorld(false);
    const auto observed = makePlayer(world.get());
    const auto plain = makePlayer(world.get());
    auto recorder = Recorder{observed.get()};
    auto cmd = scs_usercmd{};
    cmd.msec = 100; // GoldSrc splits this into two commands.
    cmd.buttons = SCS_IN_JUMP;
    cmd.viewangles[1] = 12.34567f;
    cmd.forwardmove = 10.75f;
    recorder.step(cmd);
    scs_step(plain.get(), &cmd);
    REQUIRE(recorder.samples.size() == 2);
    CHECK(recorder.samples[0].command.msec == 50);
    CHECK(
      recorder.samples[1].time_seconds - recorder.samples[0].time_seconds
      == vm::approx{0.05, 0.00001});
    CHECK(recorder.samples[0].command.forwardmove == 10.0f);
    CHECK(recorder.samples[0].command.viewangles[1] != cmd.viewangles[1]);
    auto state = scs_player_state{};
    scs_get_state(plain.get(), &state);
    const auto& recorded = recorder.samples.back().after;
    for (size_t i = 0; i < 3; ++i)
    {
      CHECK(state.origin[i] == recorded.origin[i]);
      CHECK(state.velocity[i] == recorded.velocity[i]);
    }
  }

  SECTION("clean LJ shares its pre speed and records the first floor contact")
  {
    const auto world = makeWorld(false);
    const auto player = makePlayer(world.get());
    auto recorder = Recorder{player.get()};
    auto cmd = scs_usercmd{};
    cmd.msec = 10;
    cmd.buttons = SCS_IN_JUMP;
    recorder.step(cmd);
    REQUIRE(recorder.stats.pre());
    CHECK(recorder.stats.pre()->speed == 250.0f);
    cmd.buttons = 0;
    for (int i = 0; i < 100 && !recorder.stats.result(); ++i)
      recorder.step(cmd);
    REQUIRE(recorder.stats.result());
    const auto& result = *recorder.stats.result();
    CHECK(result.technique == PlayModeJumpStats::Technique::LongJump);
    CHECK_FALSE(result.failed);
    CHECK(result.distance > 88.0f);
    CHECK(result.distance < 248.0f);
    CHECK(result.preSpeed == recorder.stats.pre()->speed);
    CHECK(result.heightDifference == vm::approx{0.0f, 0.125f});
    CHECK(PlayModeJumpStats::visible(result, true));
  }

  SECTION("plugin distance thresholds use weapon-speed adjustment")
  {
    auto result = PlayModeJumpStats::Result{};
    result.technique = PlayModeJumpStats::Technique::LongJump;
    result.airFrames = 10;
    result.heightDifference = 0;
    result.weaponSpeed = 300;
    result.distance = 155.9f;
    CHECK_FALSE(PlayModeJumpStats::visible(result, false));
    result.distance = 156.0f;
    CHECK(PlayModeJumpStats::visible(result, false));
    result.distance = 316.0f;
    CHECK(PlayModeJumpStats::visible(result, false));
    result.distance = 316.1f;
    CHECK_FALSE(PlayModeJumpStats::visible(result, false));

    result.technique = PlayModeJumpStats::Technique::Bhop;
    result.distance = 185.9f;
    CHECK_FALSE(PlayModeJumpStats::visible(result, false));
    result.distance = 186.0f;
    CHECK(PlayModeJumpStats::visible(result, false));

    result.technique = PlayModeJumpStats::Technique::DuckBhop;
    result.distance = 156.0f;
    CHECK(PlayModeJumpStats::visible(result, false));

    result.technique = PlayModeJumpStats::Technique::MultiCountJump;
    result.distance = 155.9f;
    CHECK_FALSE(PlayModeJumpStats::visible(result, false));
    result.distance = 156.0f;
    CHECK(PlayModeJumpStats::visible(result, false));
  }

  SECTION("single-axis ledge matches the plugin HJ probe and stays an LJ")
  {
    const auto world = makeWorld(true);
    const auto player = makePlayer(world.get());
    auto recorder = Recorder{player.get()};
    auto cmd = scs_usercmd{};
    cmd.msec = 10;
    cmd.buttons = SCS_IN_JUMP;
    recorder.step(cmd);
    cmd.buttons = 0;
    for (int i = 0; i < 150 && !recorder.stats.result(); ++i)
      recorder.step(cmd);
    REQUIRE(recorder.stats.result());
    const auto result = *recorder.stats.result();
    CHECK(result.technique == PlayModeJumpStats::Technique::LongJump);
    CHECK(result.failed);
    CHECK(result.heightDifference == vm::approx{0.0f, 0.001f});
    CHECK(PlayModeJumpStats::visible(result, true));
    CHECK_FALSE(PlayModeJumpStats::visible(result, false));
    for (int i = 0; i < 100; ++i)
      recorder.step(cmd);
    CHECK(recorder.stats.result()->timeMs == result.timeMs);
  }

  SECTION("hook momentum remains excluded after release")
  {
    const auto world = makeWorld(false);
    const auto player = makePlayer(world.get());
    auto recorder = Recorder{player.get()};
    recorder.assisted = true;
    auto cmd = scs_usercmd{};
    cmd.msec = 10;
    cmd.buttons = SCS_IN_JUMP;
    recorder.step(cmd);
    recorder.assisted = false;
    cmd.buttons = 0;
    for (int i = 0; i < 100; ++i)
      recorder.step(cmd);
    CHECK_FALSE(recorder.stats.result());
    cmd.buttons = SCS_IN_JUMP;
    recorder.step(cmd);
    REQUIRE(recorder.stats.pre());
  }

  SECTION("ducking in flight keeps a level landing at zero height difference")
  {
    const auto world = makeWorld(false);
    const auto player = makePlayer(world.get());
    auto recorder = Recorder{player.get()};
    auto cmd = scs_usercmd{};
    cmd.msec = 10;
    cmd.buttons = SCS_IN_JUMP;
    recorder.step(cmd);
    cmd.buttons = SCS_IN_DUCK;
    for (int i = 0; i < 120 && !recorder.stats.result(); ++i)
      recorder.step(cmd);
    REQUIRE(recorder.stats.result());
    CHECK_FALSE(recorder.stats.result()->failed);
    CHECK(recorder.stats.result()->heightDifference == vm::approx{0.0f, 0.125f});
    CHECK(PlayModeJumpStats::visible(*recorder.stats.result(), true));
  }

  SECTION("holding jump while walking off an edge does not create a result")
  {
    const auto world = makeWorld(true);
    const auto player = makePlayer(world.get());
    auto state = scs_player_state{};
    scs_get_state(player.get(), &state);
    state.oldbuttons = SCS_IN_JUMP;
    scs_set_state(player.get(), &state);
    auto recorder = Recorder{player.get()};
    auto cmd = scs_usercmd{};
    cmd.msec = 10;
    cmd.buttons = SCS_IN_JUMP;
    for (int i = 0; i < 100; ++i)
      recorder.step(cmd);
    CHECK_FALSE(recorder.stats.result());
    CHECK_FALSE(recorder.stats.pre());
  }

  SECTION("CP, teleport, collisions and advanced jumps cannot become clean LJ")
  {
    auto stats = PlayModeJumpStats{};
    auto sample = scs_move_sample{};
    sample.command.msec = 10;
    sample.command.buttons = SCS_IN_JUMP;
    sample.before.movetype = sample.after.movetype = SCS_MOVETYPE_WALK;
    sample.before.gravity = sample.after.gravity = 1;
    sample.before.flags = SCS_FL_ONGROUND;
    sample.before.origin[2] = 36;
    sample.after.origin[2] = 40;
    sample.after.velocity[2] = 260;
    sample.after.ground_entity = -1;
    sample.before.velocity[0] = sample.after.velocity[0] = 250;
    sample.time_seconds = 0.01;
    stats.tick(nullptr, sample, false);
    REQUIRE(stats.pre());

    SECTION("explicit discontinuity")
    {
      stats.interrupt();
      CHECK_FALSE(stats.pre());
    }
    SECTION("teleport")
    {
      ++sample.after.teleport_count;
      stats.tick(nullptr, sample, false);
      CHECK_FALSE(stats.pre());
    }
    SECTION("noclip")
    {
      sample.after.movetype = SCS_MOVETYPE_NOCLIP;
      stats.tick(nullptr, sample, false);
      CHECK_FALSE(stats.pre());
    }
    SECTION("obstruction")
    {
      sample.obstructed = 1;
      stats.tick(nullptr, sample, false);
      sample.before = sample.after;
      sample.before.flags = 0;
      sample.before.ground_entity = -1;
      sample.before.origin[2] = 38;
      sample.after.flags = SCS_FL_ONGROUND;
      sample.after.ground_entity = 0;
      sample.after.origin[0] = 180;
      sample.after.origin[2] = 36;
      sample.after.velocity[2] = 0;
      sample.time_seconds = 0.7;
      stats.tick(nullptr, sample, false);
      REQUIRE(stats.result());
      CHECK(stats.result()->failed);
      CHECK(stats.result()->touched);
    }
    SECTION("recent jump stamina")
    {
      sample.before.fuser[1] = 500;
      stats.tick(nullptr, sample, false);
    }
    sample.before = sample.after;
    sample.before.flags = 0;
    sample.before.ground_entity = -1;
    sample.before.origin[2] = 38;
    sample.after.flags = SCS_FL_ONGROUND;
    sample.after.ground_entity = 0;
    sample.after.origin[0] = 180;
    sample.after.origin[2] = 36;
    sample.after.velocity[2] = 0;
    sample.time_seconds = 0.7;
    stats.tick(nullptr, sample, false);
    CHECK((!stats.result() || !PlayModeJumpStats::visible(*stats.result(), false)));
  }

  SECTION("native ground transitions classify bhop stand-up and duck bhop")
  {
    const auto world = makeWorld(false);
    const auto player = makePlayer(world.get());
    auto recorder = Recorder{player.get()};
    auto cmd = scs_usercmd{};
    cmd.msec = 10;
    cmd.buttons = SCS_IN_JUMP;
    recorder.step(cmd);
    auto duckLanding = false;
    auto keepDuck = false;
    SECTION("standing landing") {}
    SECTION("duck landing")
    {
      duckLanding = true;
    }
    SECTION("duck bhop")
    {
      duckLanding = true;
      keepDuck = true;
    }
    cmd.buttons = duckLanding ? SCS_IN_DUCK : 0;
    for (int i = 0; i < 120 && !recorder.stats.result(); ++i)
      recorder.step(cmd);
    REQUIRE(recorder.stats.result());
    cmd.buttons = static_cast<uint16_t>(SCS_IN_JUMP | (keepDuck ? SCS_IN_DUCK : 0));
    recorder.step(cmd);
    REQUIRE(recorder.stats.takeoff());
    CHECK(recorder.stats.takeoff()->technique == (keepDuck
      ? PlayModeJumpStats::Technique::DuckBhop : duckLanding
      ? PlayModeJumpStats::Technique::StandupBhop : PlayModeJumpStats::Technique::Bhop));
    CHECK(recorder.stats.takeoff()->fog == 1);
    CHECK(recorder.stats.takeoff()->preSpeed > 0);
    CHECK(recorder.stats.takeoff()->postSpeed > 0);
  }

  SECTION("native duck taps classify CJ SCJ DCJ and MCJ")
  {
    const auto world = makeWorld(false);
    const auto player = makePlayer(world.get());
    auto recorder = Recorder{player.get()};
    auto cmd = scs_usercmd{};
    cmd.msec = 10;
    auto taps = 1;
    auto crouchTicks = 1;
    auto standup = false;
    auto keepDuck = false;
    auto expected = PlayModeJumpStats::Technique::CountJump;
    SECTION("CJ") {}
    SECTION("SCJ")
    {
      standup = true;
      expected = PlayModeJumpStats::Technique::StandupCountJump;
    }
    SECTION("remaining crouched does not count as stand-up")
    {
      standup = true;
      keepDuck = true;
      expected = PlayModeJumpStats::Technique::Unsupported;
    }
    SECTION("DCJ")
    {
      taps = 2;
      expected = PlayModeJumpStats::Technique::DoubleCountJump;
    }
    SECTION("MCJ")
    {
      taps = 3;
      expected = PlayModeJumpStats::Technique::MultiCountJump;
    }
    for (auto tap = 0; tap < taps; ++tap)
    {
      cmd.buttons = SCS_IN_DUCK;
      for (auto i = 0; i < crouchTicks; ++i)
        recorder.step(cmd);
      cmd.buttons = 0;
      recorder.step(cmd);
      REQUIRE((recorder.samples.back().after.flags & SCS_FL_ONGROUND) == 0);
      if (standup)
        cmd.buttons = SCS_IN_DUCK;
      for (auto i = 0; i < 60 && !(recorder.samples.back().after.flags & SCS_FL_ONGROUND);
           ++i)
        recorder.step(cmd);
      REQUIRE((recorder.samples.back().after.flags & SCS_FL_ONGROUND) != 0);
    }
    cmd.buttons = static_cast<uint16_t>(SCS_IN_JUMP | (keepDuck ? SCS_IN_DUCK : 0));
    recorder.step(cmd);
    REQUIRE(recorder.stats.takeoff());
    CHECK(recorder.stats.takeoff()->technique == expected);
    CHECK(recorder.stats.takeoff()->duckTaps == static_cast<unsigned>(taps));
    CHECK(recorder.stats.takeoff()->fog == 1);
  }

  SECTION("ground delay and discontinuities expire the action chain")
  {
    const auto world = makeWorld(false);
    const auto player = makePlayer(world.get());
    auto recorder = Recorder{player.get()};
    auto cmd = scs_usercmd{};
    cmd.msec = 10;
    cmd.buttons = SCS_IN_JUMP;
    recorder.step(cmd);
    cmd.buttons = 0;
    for (int i = 0; i < 100 && !recorder.stats.result(); ++i)
      recorder.step(cmd);
    REQUIRE(recorder.stats.result());
    SECTION("more than ten ground commands")
    {
      for (int i = 0; i < 11; ++i)
        recorder.step(cmd);
    }
    SECTION("checkpoint")
    {
      recorder.stats.interrupt();
    }
    cmd.buttons = SCS_IN_JUMP;
    recorder.step(cmd);
    REQUIRE(recorder.stats.takeoff());
    CHECK(
      recorder.stats.takeoff()->technique == PlayModeJumpStats::Technique::Unsupported);
    CHECK(recorder.stats.takeoff()->fog == 0);
  }

  SECTION("strafe transitions credit overlapping keys only once")
  {
    auto stats = PlayModeJumpStats{};
    auto sample = scs_move_sample{};
    sample.command.msec = 10;
    sample.command.buttons = SCS_IN_JUMP;
    sample.before.movetype = sample.after.movetype = SCS_MOVETYPE_WALK;
    sample.before.flags = SCS_FL_ONGROUND;
    sample.before.origin[2] = 36;
    sample.before.velocity[0] = sample.after.velocity[0] = 250;
    sample.after.ground_entity = -1;
    sample.after.origin[2] = 40;
    sample.after.velocity[2] = 250;
    sample.time_seconds = 0.01;
    stats.tick(nullptr, sample, false);
    const auto air = [&](const uint16_t buttons, const float gain) {
      sample.before = sample.after;
      sample.before.oldbuttons = static_cast<int32_t>(sample.command.buttons);
      sample.command.buttons = buttons;
      sample.after.velocity[0] += gain;
      sample.time_seconds += 0.01;
      stats.tick(nullptr, sample, false);
    };
    air(SCS_IN_MOVELEFT, 5);
    air(SCS_IN_MOVELEFT | SCS_IN_MOVERIGHT, 3);
    air(SCS_IN_MOVERIGHT, -2);
    air(0, 0);
    REQUIRE(stats.current());
    const auto& result = *stats.current();
    REQUIRE(result.strafes.size() == 2);
    CHECK(result.strafes[0].key == 'A');
    CHECK(result.strafes[0].frames == 2);
    CHECK(result.strafes[0].gain == 3);
    CHECK(result.strafes[0].loss == 2);
    CHECK(result.strafes[0].efficiency() == 50);
    CHECK(result.strafes[1].key == 'D');
    CHECK(result.strafes[1].frames == 2);
    stats.interrupt();
    CHECK_FALSE(stats.current());
    CHECK_FALSE(stats.takeoff());
  }

  SECTION("strafe already held on the first airborne command is counted")
  {
    auto stats = PlayModeJumpStats{};
    auto sample = scs_move_sample{};
    sample.command.msec = 10;
    sample.command.buttons = static_cast<uint16_t>(SCS_IN_JUMP | SCS_IN_MOVELEFT);
    sample.before.oldbuttons = SCS_IN_MOVELEFT;
    sample.before.movetype = sample.after.movetype = SCS_MOVETYPE_WALK;
    sample.before.flags = SCS_FL_ONGROUND;
    sample.before.origin[2] = 36;
    sample.before.velocity[0] = sample.after.velocity[0] = 250;
    sample.after.ground_entity = -1;
    sample.after.origin[2] = 40;
    sample.after.velocity[2] = 250;
    sample.time_seconds = 0.01;
    stats.tick(nullptr, sample, false);
    sample.before = sample.after;
    sample.before.oldbuttons = SCS_IN_MOVELEFT;
    sample.command.buttons = 0;
    sample.after.velocity[0] += 2;
    sample.time_seconds += 0.01;
    stats.tick(nullptr, sample, false);
    REQUIRE(stats.current());
    REQUIRE(stats.current()->strafes.size() == 1);
    CHECK(stats.current()->strafes[0].key == 'A');
    CHECK(stats.current()->strafes[0].frames == 2);
    CHECK(stats.current()->strafes[0].gain == 2);
  }

  SECTION("render cadence does not change results or sample timing")
  {
    const auto world = makeWorld(false);
    const auto player1 = makePlayer(world.get());
    const auto player2 = makePlayer(world.get());
    auto fine = PlayModeTimeline{};
    auto coarse = PlayModeTimeline{};
    for (auto* timeline : {&fine, &coarse})
    {
      timeline->reset(0, {});
      timeline->button(1, PlayModeButton::Jump, 'J', true);
      timeline->button(11, PlayModeButton::Jump, 'J', false);
    }
    for (int now = 10; now <= 1000; now += 10)
      fine.advance(player1.get(), static_cast<double>(now));
    for (int now = 50; now <= 1000; now += 50)
      coarse.advance(player2.get(), static_cast<double>(now));
    REQUIRE(fine.jumpStats().result());
    REQUIRE(coarse.jumpStats().result());
    CHECK(fine.jumpStats().result()->distance == coarse.jumpStats().result()->distance);
    CHECK(fine.jumpStats().result()->timeMs == coarse.jumpStats().result()->timeMs);
    CHECK(fine.jumpStats().pre()->timeMs == coarse.jumpStats().pre()->timeMs);
  }
}
} // namespace tb::ui
