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

#include <array>
#include <future>
#include <memory>
#include <sstream>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace tb::cs
{
namespace
{

using WorldPtr = std::unique_ptr<scs_world, decltype(&scs_world_release)>;
using ContextPtr = std::unique_ptr<scs_context, decltype(&scs_shutdown)>;

std::string box(
  const int x0,
  const int y0,
  const int z0,
  const int x1,
  const int y1,
  const int z1,
  const std::string& texture = "__TB_empty")
{
  using Point = std::array<int, 3>;
  const auto faces = std::array<std::array<Point, 3>, 6>{{
    {{{x0, y0, z0}, {x0, y1, z0}, {x0, y0, z1}}},
    {{{x1, y0, z0}, {x1, y0, z1}, {x1, y1, z0}}},
    {{{x0, y0, z0}, {x0, y0, z1}, {x1, y0, z0}}},
    {{{x0, y1, z0}, {x1, y1, z0}, {x0, y1, z1}}},
    {{{x0, y0, z0}, {x1, y0, z0}, {x0, y1, z0}}},
    {{{x0, y0, z1}, {x0, y1, z1}, {x1, y0, z1}}},
  }};
  auto text = std::ostringstream{};
  text << "{\n";
  for (const auto& face : faces)
  {
    for (const auto& point : face)
      text << "( " << point[0] << ' ' << point[1] << ' ' << point[2] << " ) ";
    text << texture << " 0 0 0 1 1\n";
  }
  text << "}\n";
  return text.str();
}

std::string entity(
  const std::string& classname,
  const std::string& brushes,
  const std::string& properties = {})
{
  return "{\n\"classname\" \"" + classname + "\"\n" + properties + brushes + "}\n";
}

WorldPtr compileMap(const std::string& text, const int clipType = SCS_CLIPTYPE_LEGACY)
{
  auto desc = scs_world_desc{};
  desc.compiler = SCS_COMPILER_HLTOOLS;
  desc.cliptype = clipType;
  desc.map_text = text.data();
  desc.map_text_size = text.size();
  auto info = scs_world_info{};
  scs_world* world = nullptr;
  const auto status = scs_world_build(&desc, &world, &info);
  INFO(info.error_message);
  REQUIRE(status == SCS_OK);
  return {world, scs_world_release};
}

ContextPtr contextFor(scs_world* world)
{
  auto context = ContextPtr{scs_init(nullptr), scs_shutdown};
  REQUIRE(context);
  REQUIRE(scs_set_world(context.get(), world) == SCS_OK);
  return context;
}

} // namespace

TEST_CASE("hltools collision world")
{
  const auto floor = entity("worldspawn", box(-512, -512, -16, 512, 512, 0));
  const auto wall = box(100, -64, 0, 116, 64, 128);
  const float start[] = {0, 0, 37};
  const float end[] = {300, 0, 37};

  SECTION("all cliptypes feed native movement and four trace hulls")
  {
    for (int clipType = 0; clipType <= SCS_CLIPTYPE_LEGACYTRIM; ++clipType)
    {
      CAPTURE(clipType);
      auto world = compileMap(floor + entity("func_wall", wall), clipType);
      auto context = contextFor(world.get());
      for (const auto hull :
           {SCS_HULL_STANDING, SCS_HULL_DUCKED, SCS_HULL_POINT, SCS_HULL_LARGE})
      {
        auto trace = scs_trace{};
        scs_trace_hull(context.get(), start, end, hull, &trace);
        // hltools' direct legacytrim tree aliases hull 2 to hull 1.
        const auto halfWidth =
          hull == SCS_HULL_POINT                                          ? 0.0f
          : hull == SCS_HULL_LARGE && clipType != SCS_CLIPTYPE_LEGACYTRIM ? 32.0f
                                                                          : 16.0f;
        CHECK(trace.endpos[0] == Catch::Approx(100.0f - halfWidth - 0.03125f));
      }
      const float spawn[] = {0, 0, 40};
      scs_spawn(context.get(), spawn, nullptr);
      auto cmd = scs_usercmd{};
      cmd.msec = 10;
      for (int i = 0; i < 50; ++i)
        scs_step(context.get(), &cmd);
      auto state = scs_player_state{};
      scs_get_state(context.get(), &state);
      CHECK(state.origin[2] == Catch::Approx(36.03125f));
      CHECK((state.flags & SCS_FL_ONGROUND) != 0);
    }
  }

  SECTION("origin brushes preserve submodel position and broad phase bounds")
  {
    const auto model = entity("func_wall", wall + box(104, -4, 40, 112, 4, 48, "ORIGIN"));
    auto world = compileMap(floor + model);
    auto context = contextFor(world.get());
    auto trace = scs_trace{};
    scs_trace_hull(context.get(), start, end, SCS_HULL_STANDING, &trace);
    CHECK(trace.endpos[0] == Catch::Approx(83.96875f));
    const float inside[] = {108, 0, 48};
    CHECK_FALSE(scs_position_is_free(context.get(), inside, SCS_HULL_STANDING));
  }

  SECTION("local legacytrim adds collision-only detail models")
  {
    auto world = compileMap(
      floor + entity("func_detail", wall, "\"zhlt_cliptype\" \"legacytrim\"\n"));
    auto context = contextFor(world.get());
    auto trace = scs_trace{};
    scs_trace_hull(context.get(), start, end, SCS_HULL_STANDING, &trace);
    CHECK(trace.endpos[0] == Catch::Approx(83.96875f));
  }

  SECTION("legacytrim thin roof ends remain inside the player box sweep")
  {
    const auto roof = std::string{R"MAP(
{
( -64 512 0 ) ( -64 520 0 ) ( -64 0 192 ) __TB_empty 0 0 0 1 1
( 64 512 0 ) ( 64 0 192 ) ( 64 520 0 ) __TB_empty 0 0 0 1 1
( -64 512 0 ) ( -64 0 192 ) ( 64 512 0 ) __TB_empty 0 0 0 1 1
( -64 520 0 ) ( 64 520 0 ) ( -64 8 192 ) __TB_empty 0 0 0 1 1
( -64 512 0 ) ( 64 512 0 ) ( -64 520 0 ) __TB_empty 0 0 0 1 1
( -64 0 192 ) ( -64 8 192 ) ( 64 0 192 ) __TB_empty 0 0 0 1 1
}
)MAP"};
    auto world = compileMap(
      entity("worldspawn", box(-32, -32, -128, 32, 32, -64))
      + entity("func_detail", roof, "\"zhlt_cliptype\" \"legacytrim\"\n"));
    auto context = contextFor(world.get());
    for (const auto hull : {SCS_HULL_STANDING, SCS_HULL_DUCKED})
    {
      CAPTURE(hull);
      const float beyondEnd[] = {0, 600, -16};
      const float onRoof[] = {0, 260, 96};
      const float insideEdge[] = {0, 535, 0};
      const float outsideEdge[] = {0, 537, 0};
      CHECK(scs_position_is_free(context.get(), beyondEnd, hull));
      CHECK_FALSE(scs_position_is_free(context.get(), onRoof, hull));
      CHECK_FALSE(scs_position_is_free(context.get(), insideEdge, hull));
      CHECK(scs_position_is_free(context.get(), outsideEdge, hull));
    }
  }

  SECTION("tool textures preserve hull masks")
  {
    auto world = compileMap(entity(
      "worldspawn",
      box(-512, -512, -16, 512, 512, 0) + box(100, -64, 0, 116, 64, 128, "NOCLIP")
        + box(150, -64, 0, 166, 64, 128, "CLIPHULL3")));
    auto context = contextFor(world.get());
    auto trace = scs_trace{};
    scs_trace_hull(context.get(), start, end, SCS_HULL_STANDING, &trace);
    CHECK(trace.fraction == 1.0f);
    scs_trace_hull(context.get(), start, end, SCS_HULL_DUCKED, &trace);
    CHECK(trace.endpos[0] == Catch::Approx(133.96875f));
    const float point[] = {108, 0, 64};
    CHECK(scs_point_contents(context.get(), point) == SCS_CONTENTS_SOLID);
  }

  SECTION("non-solid brush entities do not block")
  {
    auto world = compileMap(
      floor + entity("trigger_multiple", wall) + entity("func_illusionary", wall)
      + entity("func_ladder", wall));
    auto context = contextFor(world.get());
    auto trace = scs_trace{};
    scs_trace_hull(context.get(), start, end, SCS_HULL_STANDING, &trace);
    CHECK(trace.fraction == 1.0f);
  }

  SECTION("diagnostics cross the C boundary and the next build recovers")
  {
    auto desc = scs_world_desc{};
    desc.compiler = SCS_COMPILER_HLTOOLS;
    desc.map_text = "not a map";
    desc.map_text_size = 9;
    auto info = scs_world_info{};
    scs_world* world = nullptr;
    CHECK(scs_world_build(&desc, &world, &info) == SCS_ERR_COMPILE);
    CHECK(world == nullptr);
    CHECK(info.error_message[0] != '\0');
    CHECK(compileMap(floor));
    desc.map_text = nullptr;
    CHECK(scs_world_build(&desc, &world, nullptr) == SCS_ERR_INVALID_ARGUMENT);
  }

  SECTION("parallel callers compile independent immutable worlds")
  {
    const auto build = [&] {
      auto desc = scs_world_desc{};
      desc.compiler = SCS_COMPILER_HLTOOLS;
      desc.cliptype = SCS_CLIPTYPE_LEGACY;
      desc.map_text = floor.data();
      desc.map_text_size = floor.size();
      scs_world* world = nullptr;
      const auto status = scs_world_build(&desc, &world, nullptr);
      return std::pair{status, WorldPtr{world, scs_world_release}};
    };
    auto first = std::async(std::launch::async, build);
    auto second = std::async(std::launch::async, build);
    for (auto* future : {&first, &second})
    {
      const auto [status, world] = future->get();
      CHECK(status == SCS_OK);
      CHECK(world);
    }
  }
}

} // namespace tb::cs
