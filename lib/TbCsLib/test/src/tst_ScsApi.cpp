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

// Behavioural tests against known CS 1.6 numbers. Uses only the public C API,
// the same way the TrenchBroom plugin will.
#include "cs/scs.h"

#include <chrono>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace tb::cs
{
namespace
{

// Brush storage so scs_brush::planes stays valid.
struct BrushSet
{
  std::vector<std::vector<scs_plane>> planes;
  std::vector<scs_brush> brushes;

  void box(float x0, float y0, float z0, float x1, float y1, float z1,
           int contents = SCS_CONTENTS_SOLID)
  {
    planes.push_back({{{1, 0, 0}, x1, 0}, {{-1, 0, 0}, -x0, 0}, {{0, 1, 0}, y1, 0},
                      {{0, -1, 0}, -y0, 0}, {{0, 0, 1}, z1, 0}, {{0, 0, -1}, -z0, 0}});
    brushes.push_back({nullptr, 6, contents, 0});
  }

  // Wedge rising along +x from (x0, z0) to (x1, z1); `degrees` from horizontal.
  void ramp(float x0, float x1, float y0, float y1, float zBase, float degrees)
  {
    const double rad = degrees * 3.14159265358979 / 180.0;
    const double nx = -std::sin(rad), nz = std::cos(rad);
    // plane through (x0, zBase) ... top surface: nx*x + nz*z = d
    const double d = nx * x0 + nz * zBase;
    planes.push_back({{{nx, 0, nz}, d, 0}, {{1, 0, 0}, x1, 0}, {{0, 1, 0}, y1, 0},
                      {{0, -1, 0}, -y0, 0}, {{0, 0, -1}, -zBase + 1, 0}});
    brushes.push_back({nullptr, 5, SCS_CONTENTS_SOLID, 0});
  }

  // Any convex brush; planes as {nx, ny, nz, dist} with unnormalized normals.
  void brush(std::vector<std::array<double, 4>> faces, uint32_t hullMask = 0,
             uint32_t bevelFaces = 0)
  {
    auto& out = planes.emplace_back();
    for (std::size_t i = 0; i < faces.size(); ++i)
    {
      const auto& [x, y, z, d] = faces[i];
      const double len = std::sqrt(x * x + y * y + z * z);
      out.push_back({{x / len, y / len, z / len}, d / len,
                     (bevelFaces & (1u << i)) ? SCS_PLANE_BEVEL : 0u});
    }
    brushes.push_back({nullptr, static_cast<uint32_t>(out.size()), SCS_CONTENTS_SOLID, hullMask});
  }

  const scs_brush* data()
  {
    for (std::size_t i = 0; i < brushes.size(); ++i)
    {
      brushes[i].planes = planes[i].data();
    }
    return brushes.data();
  }
};

scs_world* buildWorld(BrushSet& world, BrushSet* ladder = nullptr, scs_world_info* info = nullptr)
{
  scs_world_desc desc{};
  desc.world_brushes = world.data();
  desc.world_brush_count = static_cast<uint32_t>(world.brushes.size());
  scs_brush_entity ent{};
  if (ladder)
  {
    ent.brushes = ladder->data();
    ent.brush_count = static_cast<uint32_t>(ladder->brushes.size());
    ent.entity_contents = SCS_CONTENTS_LADDER;
    desc.entities = &ent;
    desc.entity_count = 1;
  }
  scs_world* out = nullptr;
  const scs_result r = scs_world_build(&desc, &out, info);
  CHECK(r == SCS_OK);
  return out;
}

scs_world* buildWorld(BrushSet& world, int cliptype, int compiler = SCS_COMPILER_VHLT)
{
  scs_world_desc desc{};
  desc.world_brushes = world.data();
  desc.world_brush_count = static_cast<uint32_t>(world.brushes.size());
  desc.cliptype = cliptype;
  desc.compiler = compiler;
  scs_world* out = nullptr;
  REQUIRE(scs_world_build(&desc, &out, nullptr) == SCS_OK);
  return out;
}

scs_usercmd cmd(float forward = 0, float side = 0, uint16_t buttons = 0, float yaw = 0, float pitch = 0)
{
  scs_usercmd c{};
  c.msec = 10;
  c.forwardmove = forward;
  c.sidemove = side;
  c.buttons = buttons;
  c.viewangles[0] = pitch;
  c.viewangles[1] = yaw;
  return c;
}

float hspeed(const scs_player_state& s)
{
  return std::sqrt(s.velocity[0] * s.velocity[0] + s.velocity[1] * s.velocity[1]);
}

void step(scs_context* ctx, scs_usercmd c)
{
  scs_step(ctx, &c);
}

void run(scs_context* ctx, const scs_usercmd& c, int ticks)
{
  for (int i = 0; i < ticks; ++i)
  {
    scs_step(ctx, &c);
  }
}

BrushSet flatRoom()
{
  BrushSet set;
  set.box(-1024, -1024, -16, 1024, 1024, 0);    // floor, top at z = 0
  set.box(512, -1024, 0, 528, 1024, 256);       // wall facing -x at x = 512
  return set;
}

} // namespace

TEST_CASE("stand and jump")
{
  BrushSet set = flatRoom();
  scs_world* world = buildWorld(set);
  scs_context* ctx = scs_init(nullptr);
  scs_set_world(ctx, world);

  const float spawn[3] = {0, 0, 40};
  scs_spawn(ctx, spawn, nullptr);
  run(ctx, cmd(), 100);

  scs_player_state s{};
  scs_get_state(ctx, &s);
  CHECK(s.flags & SCS_FL_ONGROUND);
  CHECK(s.ground_entity == 0);
  // Origin rests DIST_EPSILON above the expanded floor plane (z = 36).
  CHECK(static_cast<double>(s.origin[2]) == Catch::Approx(36.03125).margin(0.001));
  const float groundZ = s.origin[2];

  step(ctx, cmd(0, 0, SCS_IN_JUMP));
  scs_get_state(ctx, &s);
  CHECK(!(s.flags & SCS_FL_ONGROUND));
  // sqrt(2 * 800 * 45), then PM_FixupGravityVelocity runs twice on the jump
  // frame (once inside CS's PM_Jump, once at the end of PM_PlayerMove).
  CHECK(static_cast<double>(s.velocity[2]) == Catch::Approx(std::sqrt(2.0 * 800.0 * 45.0) - 800.0 * 0.01).margin(0.01));
  CHECK(static_cast<double>(s.fuser[1]) == Catch::Approx(1315.7894).margin(0.01)); // jump stamina

  float apex = groundZ;
  for (int i = 0; i < 100; ++i)
  {
    step(ctx, cmd(0, 0, SCS_IN_JUMP)); // held: no pogo
    scs_get_state(ctx, &s);
    apex = std::max(apex, s.origin[2]);
  }
  CHECK(static_cast<double>(apex - groundZ) == Catch::Approx(45.0).margin(1.5));
  CHECK(s.flags & SCS_FL_ONGROUND);
  CHECK(static_cast<double>(s.origin[2]) == Catch::Approx(groundZ).margin(0.001)); // held jump did not re-jump

  scs_shutdown(ctx);
  scs_world_release(world);
}

TEST_CASE("fall damage")
{
  // Kreedz articles: losses start at a 164 unit fall, 603 units kill at 100 HP.
  CHECK(scs_fall_damage(500.0f) == 0.0f);
  CHECK(static_cast<double>(scs_fall_damage(std::sqrt(1600.0f * 164.0f))) > 0.0);
  CHECK(static_cast<double>(scs_fall_damage(std::sqrt(1600.0f * 603.0f))) == Catch::Approx(100.0).margin(0.5));

  BrushSet set = flatRoom();
  scs_world* world = buildWorld(set);
  scs_context* ctx = scs_init(nullptr);
  scs_set_world(ctx, world);
  scs_player_state s{};

  SECTION("a short drop is safe")
  {
    const float spawn[3] = {0, 0, 36.03125f + 100};
    scs_spawn(ctx, spawn, nullptr);
    run(ctx, cmd(), 100);
    scs_get_state(ctx, &s);
    CHECK(s.fall_count == 0u);
  }

  SECTION("a high drop deals damage but takes no health")
  {
    const float spawn[3] = {0, 0, 36.03125f + 400};
    scs_spawn(ctx, spawn, nullptr);
    run(ctx, cmd(), 150);
    scs_get_state(ctx, &s);
    CHECK(s.fall_count == 1u);
    CHECK(static_cast<double>(s.last_fall_speed) == Catch::Approx(800.0).margin(8.0));
    CHECK(s.last_fall_damage == scs_fall_damage(s.last_fall_speed));
    CHECK(s.health == 100.0f);
    CHECK(s.server_fall_velocity == 0.0f);
  }

  scs_shutdown(ctx);
  scs_world_release(world);
}

TEST_CASE("run speed and wall")
{
  BrushSet set = flatRoom();
  scs_world* world = buildWorld(set);
  scs_context* ctx = scs_init(nullptr);
  scs_set_world(ctx, world);
  const float spawn[3] = {0, 0, 37};
  scs_spawn(ctx, spawn, nullptr);

  run(ctx, cmd(400), 150);
  scs_player_state s{};
  scs_get_state(ctx, &s);
  CHECK(static_cast<double>(hspeed(s)) == Catch::Approx(250.0).margin(0.01)); // knife speed, cmd clipped by maxspeed

  run(ctx, cmd(400), 200); // into the wall at x = 512
  scs_get_state(ctx, &s);
  CHECK(static_cast<double>(s.origin[0]) == Catch::Approx(512.0 - 16.0 - 0.03125).margin(0.01));
  CHECK(static_cast<double>(s.velocity[0]) == Catch::Approx(0.0).margin(0.001));

  scs_shutdown(ctx);
  scs_world_release(world);
}

TEST_CASE("bunnyhop cap")
{
  BrushSet set = flatRoom();
  scs_world* world = buildWorld(set);
  scs_context* ctx = scs_init(nullptr);
  scs_set_world(ctx, world);
  const float spawn[3] = {-400, 0, 37};
  scs_spawn(ctx, spawn, nullptr);
  run(ctx, cmd(), 20);

  scs_player_state s{};
  scs_get_state(ctx, &s);
  s.velocity[0] = 400.0f;
  scs_set_state(ctx, &s);
  step(ctx, cmd(0, 0, SCS_IN_JUMP));
  scs_get_state(ctx, &s);
  // Friction is skipped on the jump frame; PM_PreventMegaBunnyJumping scales
  // to 0.8 * 1.2 * 250 = 240 when above 1.2 * maxspeed. It measures the 3D
  // speed after half a frame of gravity was added (vz = -4), hence 239.988.
  CHECK(static_cast<double>(hspeed(s)) == Catch::Approx(400.0 * 0.8 * 300.0 / std::sqrt(400.0 * 400.0 + 4.0 * 4.0)).margin(0.001));

  scs_shutdown(ctx);
  scs_world_release(world);
}

TEST_CASE("surf ramp")
{
  BrushSet set;
  set.box(-2048, -2048, -1040, 2048, 2048, -1024);
  set.ramp(0, 1024, -256, 256, -1024, 60.0f); // 60 degree ramp, too steep to stand
  scs_world* world = buildWorld(set);
  scs_context* ctx = scs_init(nullptr);
  scs_set_world(ctx, world);

  const float spawn[3] = {700, 0, 300};
  scs_spawn(ctx, spawn, nullptr);
  scs_player_state s{};
  bool touchedRamp = false;
  for (int i = 0; i < 200; ++i)
  {
    step(ctx, cmd());
    scs_get_state(ctx, &s);
    touchedRamp |= s.velocity[0] < -1.0f;
    CHECK((!(s.flags & SCS_FL_ONGROUND) || s.origin[2] < -980.0f));
  }
  CHECK(touchedRamp); // gravity converted into -x velocity along the ramp

  scs_shutdown(ctx);
  scs_world_release(world);
}

TEST_CASE("ladder")
{
  BrushSet set = flatRoom();
  BrushSet ladder;
  ladder.box(200, -32, 0, 208, 32, 512);
  scs_world* world = buildWorld(set, &ladder);
  scs_context* ctx = scs_init(nullptr);
  scs_set_world(ctx, world);

  const float spawn[3] = {150, 0, 37};
  scs_spawn(ctx, spawn, nullptr);
  run(ctx, cmd(), 10);
  // PM_LadderMove reads the IN_FORWARD bit, not forwardmove; real clients
  // always send both (see scs_build_usercmd).
  run(ctx, cmd(400, 0, SCS_IN_FORWARD, 0, -60), 150); // walk into it looking up
  scs_player_state s{};
  scs_get_state(ctx, &s);
  CHECK(s.origin[2] > 200.0f);
  CHECK(s.movetype == SCS_MOVETYPE_FLY);

  scs_shutdown(ctx);
  scs_world_release(world);
}

TEST_CASE("duck")
{
  BrushSet set = flatRoom();
  scs_world* world = buildWorld(set);
  scs_context* ctx = scs_init(nullptr);
  scs_set_world(ctx, world);
  const float spawn[3] = {0, 0, 37};
  scs_spawn(ctx, spawn, nullptr);
  run(ctx, cmd(), 10);
  run(ctx, cmd(0, 0, SCS_IN_DUCK), 60);
  scs_player_state s{};
  scs_get_state(ctx, &s);
  CHECK(s.flags & SCS_FL_DUCKING);
  CHECK(static_cast<double>(s.view_ofs[2]) == Catch::Approx(12.0).margin(0.001));
  CHECK(static_cast<double>(s.origin[2]) == Catch::Approx(18.03125).margin(0.01)); // ducked hull rests 18 above floor

  scs_shutdown(ctx);
  scs_world_release(world);
}

TEST_CASE("determinism and state")
{
  BrushSet set = flatRoom();
  scs_world* world = buildWorld(set);

  auto simulate = [&](scs_context* ctx, int ticks) {
    std::mt19937 rng(42);
    for (int i = 0; i < ticks; ++i)
    {
      const auto r = rng();
      scs_usercmd c = cmd(r & 1 ? 400.0f : 0.0f, r & 2 ? -400.0f : 400.0f,
                          static_cast<uint16_t>(r & 4 ? SCS_IN_JUMP : 0),
                          static_cast<float>(i) * 1.7f);
      scs_step(ctx, &c);
    }
  };

  scs_context* a = scs_init(nullptr);
  scs_context* b = scs_init(nullptr);
  scs_set_world(a, world);
  scs_set_world(b, world);
  const float spawn[3] = {0, 0, 37};
  scs_spawn(a, spawn, nullptr);
  scs_spawn(b, spawn, nullptr);
  simulate(a, 500);
  simulate(b, 500);
  scs_player_state sa{}, sb{};
  scs_get_state(a, &sa);
  scs_get_state(b, &sb);
  CHECK(std::memcmp(&sa, &sb, sizeof(sa)) == 0);

  // Restore a snapshot into a fresh context and continue identically.
  scs_context* c = scs_init(nullptr);
  scs_set_world(c, world);
  scs_set_state(c, &sa);
  const scs_usercmd step = cmd(400, 400, 0, 45);
  run(a, step, 50);
  run(c, step, 50);
  scs_get_state(a, &sa);
  scs_get_state(c, &sb);
  CHECK(std::memcmp(&sa, &sb, sizeof(sa)) == 0);

  scs_shutdown(a);
  scs_shutdown(b);
  scs_shutdown(c);
  scs_world_release(world); // contexts held their own references
}

TEST_CASE("client input")
{
  scs_client_input in;
  scs_client_input_defaults(&in);
  scs_usercmd c{};

  scs_key_down(&in.forward, 'w');
  scs_build_usercmd(&in, 250.0f, 10, &c);
  CHECK(static_cast<double>(c.forwardmove) == Catch::Approx(200.0).margin(0.001)); // 0.5 * 400 on the press frame
  scs_build_usercmd(&in, 250.0f, 10, &c);
  CHECK(static_cast<double>(c.forwardmove) == Catch::Approx(250.0).margin(0.001)); // 400 clipped to maxspeed
  CHECK(c.buttons & SCS_IN_FORWARD);

  // Walking is 0.52 of the weapon speed, also diagonally (91 per axis once the
  // server quantizes the usercmd).
  scs_key_down(&in.speed, 's');
  scs_build_usercmd(&in, 250.0f, 10, &c);
  CHECK(static_cast<double>(c.forwardmove) == Catch::Approx(130.0).margin(0.001));
  scs_key_down(&in.moveright, 'd');
  scs_build_usercmd(&in, 250.0f, 10, &c);
  scs_build_usercmd(&in, 250.0f, 10, &c);
  CHECK(static_cast<double>(c.forwardmove) == Catch::Approx(91.92).margin(0.01));
  CHECK(static_cast<double>(c.sidemove) == Catch::Approx(91.92).margin(0.01));
  scs_key_up(&in.speed, 's');
  scs_key_up(&in.moveright, 'd');

  // +left turns at cl_yawspeed: half speed on the press frame (CL_KeyState), and
  // anglemod quantizes to 360 / 65536 degrees.
  in.viewangles[1] = 0.0f;
  scs_key_down(&in.left, 0x1000012);
  scs_build_usercmd(&in, 250.0f, 10, &c);
  CHECK(static_cast<double>(c.viewangles[1]) == Catch::Approx(1.05).margin(0.006));
  scs_build_usercmd(&in, 250.0f, 10, &c);
  CHECK(static_cast<double>(c.viewangles[1]) == Catch::Approx(3.15).margin(0.006));
  scs_key_up(&in.left, 0x1000012);
  scs_build_usercmd(&in, 250.0f, 10, &c);
  CHECK(static_cast<double>(c.viewangles[1]) == Catch::Approx(3.15).margin(0.006));

  // Mouse-wheel jump: pressed and released inside one frame still jumps.
  scs_key_down(&in.jump, 239);
  scs_key_up(&in.jump, 239);
  scs_build_usercmd(&in, 250.0f, 10, &c);
  CHECK(c.buttons & SCS_IN_JUMP);
  scs_build_usercmd(&in, 250.0f, 10, &c);
  CHECK(!(c.buttons & SCS_IN_JUMP));

  scs_tick_clock clock{0.0, 100};
  uint8_t msec = 0;
  CHECK(scs_tick_clock_advance(&clock, 0.016, &msec) == 1);
  CHECK(msec == 10);
  CHECK(scs_tick_clock_advance(&clock, 0.016, &msec) == 2);
}

TEST_CASE("compiler stress")
{
  BrushSet set;
  std::mt19937 rng(7);
  std::uniform_int_distribution<int> pos(-4000, 4000);
  std::uniform_int_distribution<int> size(16, 256);
  for (int i = 0; i < 1500; ++i)
  {
    // One draw per statement: argument evaluation order is unspecified.
    const float x = static_cast<float>(pos(rng));
    const float y = static_cast<float>(pos(rng));
    const float z = static_cast<float>(pos(rng) / 4);
    const float sx = static_cast<float>(size(rng));
    const float sy = static_cast<float>(size(rng));
    const float sz = static_cast<float>(size(rng));
    set.box(x, y, z, x + sx, y + sy, z + sz);
  }
  for (int i = 0; i < 100; ++i)
  {
    const float x = static_cast<float>(pos(rng));
    const float y = static_cast<float>(pos(rng));
    const float z = static_cast<float>(pos(rng) / 4);
    set.ramp(x, x + 256, y, y + 128, z, 30.0f + static_cast<float>(i % 40));
  }
  scs_world_info info{};
  scs_world* world = buildWorld(set, nullptr, &info);
  CHECK(world != nullptr);

  // Random traces must never end inside solid.
  scs_context* ctx = scs_init(nullptr);
  scs_set_world(ctx, world);
  int bad = 0;
  for (int i = 0; i < 2000; ++i)
  {
    float a[3];
    float b[3];
    for (float* v : {a, b})
    {
      v[0] = static_cast<float>(pos(rng));
      v[1] = static_cast<float>(pos(rng));
      v[2] = static_cast<float>(pos(rng) / 4);
    }
    if (!scs_position_is_free(ctx, a, SCS_HULL_STANDING))
    {
      continue;
    }
    scs_trace tr{};
    scs_trace_hull(ctx, a, b, SCS_HULL_STANDING, &tr);
    if (!tr.startsolid && !scs_position_is_free(ctx, tr.endpos, SCS_HULL_STANDING))
    {
      ++bad;
    }
  }
  CHECK(bad == 0);
  scs_shutdown(ctx);
  scs_world_release(world);
}

TEST_CASE("engine semantics")
{
  // Behaviour of the GoldSrc engine side (as reconstructed by ReHLDS) that a generic
  // implementation would get subtly different.
  BrushSet set = flatRoom();
  BrushSet ladder;
  ladder.box(200, -32, 0, 208, 32, 512);
  scs_world* world = buildWorld(set, &ladder);
  scs_context* ctx = scs_init(nullptr);
  scs_set_world(ctx, world);

  SECTION("ladders are not physents, so they do not report contents")
  {
    const float insideLadder[3] = {204, 0, 100};
    CHECK(scs_point_contents(ctx, insideLadder) == SCS_CONTENTS_EMPTY);
  }

  SECTION("an unobstructed trace ends exactly at the requested end point")
  {
    // In open space beside the room. For these values start + 1.0 * (end - start)
    // differs from end in the last bit of z, so recomputing the end point (as other
    // engines do) would be detected.
    const float start[3] = {-2762.442626953125f, 171.53558349609375f, -243.9846954345703f};
    const float end[3] = {-2937.502685546875f, 228.0668487548828f, -102.93155670166016f};
    auto tr = scs_trace{};
    scs_trace_hull(ctx, start, end, SCS_HULL_STANDING, &tr);
    CHECK(tr.fraction == 1.0f);
    CHECK(std::memcmp(tr.endpos, end, sizeof(end)) == 0);
  }

  scs_shutdown(ctx);
  scs_world_release(world);
}

TEST_CASE("cliptypes")
{
  // Compiles `set` with `cliptype` and runs `fn(ctx)` against it.
  int compiler = SCS_COMPILER_VHLT;
  const auto withWorld = [&](BrushSet& set, int cliptype, const auto& fn) {
    scs_world* world = buildWorld(set, cliptype, compiler);
    scs_context* ctx = scs_init(nullptr);
    scs_set_world(ctx, world);
    fn(ctx);
    scs_shutdown(ctx);
    scs_world_release(world);
  };
  // Standing player height (origin z) after dropping onto the world at (x, y).
  const auto landingHeight = [&](BrushSet& set, int cliptype, float x, float y) {
    float height = 0.0f;
    withWorld(set, cliptype, [&](scs_context* ctx) {
      const float start[3] = {x, y, 1000.0f};
      const float end[3] = {x, y, -1000.0f};
      auto tr = scs_trace{};
      scs_trace_hull(ctx, start, end, SCS_HULL_STANDING, &tr);
      height = tr.endpos[2];
    });
    return height;
  };
  const auto isFree = [&](BrushSet& set, int cliptype, std::array<float, 3> origin, int hull) {
    int result = 0;
    withWorld(set, cliptype, [&](scs_context* ctx) {
      result = scs_position_is_free(ctx, origin.data(), hull);
    });
    return result != 0;
  };

  // A 45 degree climb along +x up to z = 128, then a flat platform. The player
  // drops at x = 118, on the slope, 10 units before its top edge.
  BrushSet oneBrush;
  oneBrush.brush({{-1, 0, 1, 0}, {0, 0, 1, 128}, {1, 0, 0, 512}, {0, 0, -1, 0},
                  {0, 1, 0, 256}, {0, -1, 0, 256}});
  BrushSet twoBrushes; // the same shape split at the top of the slope
  twoBrushes.brush({{-1, 0, 1, 0}, {1, 0, 0, 128}, {0, 0, -1, 0}, {0, 1, 0, 256},
                    {0, -1, 0, 256}});
  twoBrushes.box(128, -256, 0, 512, 256, 128);

  SECTION("slopes grow by the cliptype's offset")
  {
    // exact Minkowski sum: the box corner touches the slope, z = x + 16 + 36
    // (capped by the platform's z = 128 + 36)
    CHECK(landingHeight(oneBrush, SCS_CLIPTYPE_SMALLEST, 118, 0) == Catch::Approx(164).margin(0.05));
    CHECK(landingHeight(oneBrush, SCS_CLIPTYPE_SIMPLE, 118, 0) == Catch::Approx(164).margin(0.05));
    // legacy / normalized: 15.23 units deeper than the corner, z = x + 36.77
    CHECK(landingHeight(oneBrush, SCS_CLIPTYPE_LEGACY, 118, 0) == Catch::Approx(154.77).margin(0.05));
    CHECK(landingHeight(oneBrush, SCS_CLIPTYPE_NORMALIZED, 118, 0) == Catch::Approx(154.77).margin(0.05));
    // precise: floors only move up by the hull height, z = x + 36
    CHECK(landingHeight(oneBrush, SCS_CLIPTYPE_PRECISE, 118, 0) == Catch::Approx(154).margin(0.05));
  }

  SECTION("legacy splits produce an invisible step")
  {
    // The platform brush's expanded box reaches 16 units over the slope; legacy
    // buries the slope deeper than that, leaving a 15.23 unit step at x = 112.
    CHECK(landingHeight(twoBrushes, SCS_CLIPTYPE_LEGACY, 118, 0) == Catch::Approx(164).margin(0.05));
    CHECK(landingHeight(twoBrushes, SCS_CLIPTYPE_LEGACY, 111, 0) == Catch::Approx(147.77).margin(0.05));
    CHECK(landingHeight(oneBrush, SCS_CLIPTYPE_LEGACY, 111, 0) == Catch::Approx(147.77).margin(0.05));
    // with simple the split does not matter
    CHECK(landingHeight(twoBrushes, SCS_CLIPTYPE_SIMPLE, 118, 0) == Catch::Approx(164).margin(0.05));
    CHECK(landingHeight(twoBrushes, SCS_CLIPTYPE_SIMPLE, 100, 0) == Catch::Approx(152).margin(0.05));
  }

  SECTION("edge bevels")
  {
    // A ramp rising diagonally from the edge x + y = 0 at z = 0. Its bottom edge
    // needs a bevel plane x + y >= -32 (standing hull); without it the expanded
    // slope reaches far over the floor in front of the ramp.
    BrushSet ramp;
    ramp.brush({{-1, -1, 2, 0}, {0, 0, -1, 0}, {1, 0, 0, 256}, {0, 1, 0, 256}});
    const auto inFront = std::array<float, 3>{-30, -30, 0}; // box spans x + y <= -28
    CHECK_FALSE(isFree(ramp, SCS_CLIPTYPE_SMALLEST, inFront, SCS_HULL_STANDING));
    CHECK_FALSE(isFree(ramp, SCS_CLIPTYPE_LEGACY, inFront, SCS_HULL_STANDING));
    CHECK(isFree(ramp, SCS_CLIPTYPE_SIMPLE, inFront, SCS_HULL_STANDING));
    CHECK(isFree(ramp, SCS_CLIPTYPE_PRECISE, inFront, SCS_HULL_STANDING));
    CHECK(isFree(ramp, SCS_CLIPTYPE_NORMALIZED, inFront, SCS_HULL_STANDING));
    // the bevel itself sits where the box really touches the ramp
    CHECK_FALSE(isFree(ramp, SCS_CLIPTYPE_SIMPLE, {-15, -15, 0}, SCS_HULL_STANDING));
  }

  SECTION("BEVEL faces are not expanded")
  {
    BrushSet block;
    block.brush({{1, 0, 0, 64}, {-1, 0, 0, 64}, {0, 1, 0, 64}, {0, -1, 0, 64},
                 {0, 0, 1, 64}, {0, 0, -1, 0}}, 0, 1u << 4); // BEVEL top
    CHECK(landingHeight(block, SCS_CLIPTYPE_LEGACY, 0, 0) == Catch::Approx(64).margin(0.05));
    // the other sides still grow
    CHECK_FALSE(isFree(block, SCS_CLIPTYPE_LEGACY, {79, 0, 32}, SCS_HULL_STANDING));
    CHECK(isFree(block, SCS_CLIPTYPE_LEGACY, {81, 0, 32}, SCS_HULL_STANDING));
  }

  SECTION("hull masks (CLIPHULLn, NOCLIP)")
  {
    BrushSet duckOnly;
    duckOnly.brush({{1, 0, 0, 64}, {-1, 0, 0, 64}, {0, 1, 0, 64}, {0, -1, 0, 64},
                    {0, 0, 1, 64}, {0, 0, -1, 0}}, 1u << 3);
    CHECK(isFree(duckOnly, SCS_CLIPTYPE_LEGACY, {0, 0, 32}, SCS_HULL_STANDING));
    CHECK_FALSE(isFree(duckOnly, SCS_CLIPTYPE_LEGACY, {0, 0, 32}, SCS_HULL_DUCKED));
    CHECK(isFree(duckOnly, SCS_CLIPTYPE_LEGACY, {0, 0, 32}, SCS_HULL_POINT));
  }

  SECTION("ZHLT")
  {
    compiler = SCS_COMPILER_ZHLT;
    // precise: floors (normal z > 0.5) get the legacy offset instead of VHLT's
    // straight-up shift
    CHECK(landingHeight(oneBrush, SCS_CLIPTYPE_PRECISE, 118, 0) == Catch::Approx(154.77).margin(0.05));
    CHECK(landingHeight(oneBrush, SCS_CLIPTYPE_SIMPLE, 118, 0) == Catch::Approx(164).margin(0.05));
    CHECK(landingHeight(oneBrush, SCS_CLIPTYPE_LEGACY, 118, 0) == Catch::Approx(154.77).margin(0.05));

    // A ridge between two faces with a zero normal component each (z <= x and
    // z >= -y). VHLT bevels its edge (x + y >= -32), ZHLT only bevels faces
    // whose normal has no zero component, so the expanded faces reach further.
    BrushSet ridge;
    ridge.brush({{-1, 0, 1, 0}, {0, -1, -1, 0}, {1, 0, 0, 256}, {0, 1, 0, 256},
                 {0, 0, 1, 256}, {0, 0, -1, 256}});
    const auto inFront = std::array<float, 3>{-30, -30, 0}; // box spans x + y <= -28
    CHECK_FALSE(isFree(ridge, SCS_CLIPTYPE_SIMPLE, inFront, SCS_HULL_STANDING));
    compiler = SCS_COMPILER_VHLT;
    CHECK(isFree(ridge, SCS_CLIPTYPE_SIMPLE, inFront, SCS_HULL_STANDING));

    // fully non-axial faces are bevelled by both
    BrushSet ramp;
    ramp.brush({{-1, -1, 2, 0}, {0, 0, -1, 0}, {1, 0, 0, 256}, {0, 1, 0, 256}});
    compiler = SCS_COMPILER_ZHLT;
    CHECK(isFree(ramp, SCS_CLIPTYPE_SIMPLE, {-30, -30, 0}, SCS_HULL_STANDING));
  }

  SECTION("invalid cliptype")
  {
    BrushSet set = flatRoom();
    scs_world_desc desc{};
    desc.world_brushes = set.data();
    desc.world_brush_count = static_cast<uint32_t>(set.brushes.size());
    desc.cliptype = 5;
    scs_world* out = nullptr;
    CHECK(scs_world_build(&desc, &out, nullptr) == SCS_ERR_INVALID_ARGUMENT);
  }
}

} // namespace tb::cs
