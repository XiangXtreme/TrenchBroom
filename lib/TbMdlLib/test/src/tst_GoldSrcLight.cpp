/*
 Copyright (C) 2026 KreedzBroom contributors
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "mdl/Entity.h"
#include "mdl/GameConfig.h"
#include "mdl/GoldSrcLight.h"

#include "vm/approx.h"
#include "vm/mat_ext.h"
#include "vm/vec_io.h" // IWYU pragma: keep

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace tb::mdl
{
TEST_CASE("GoldSrcLight")
{
  SECTION("game detection uses the same material format for editing and rendering")
  {
    auto config = GameConfig{};
    CHECK_FALSE(usesGoldSrcLightDirections(config));
    config.materialConfig.extensions = {".wal", ".c"};
    CHECK(usesGoldSrcLightDirections(config));
    config.materialConfig.extensions = {".D"};
    CHECK_FALSE(usesGoldSrcLightDirections(config));
  }

  SECTION("rotations transform the compiled beam and synchronize scalar overrides")
  {
    const auto classname = GENERATE(
      "light_spot", "light_environment", "info_sunlight", "light_shadow", "light_bounce");
    const auto properties = GENERATE(values<std::vector<EntityProperty>>({
      {{"angles", "-45 90 27"}, {"pitch", "-90"}, {"angle", "180"}},
      {{"angles", "-45 90 27"}, {"pitch", "0"}, {"angle", "0"}},
      {{"pitch", "-90"}},
      {{"angles", "-23.5 126.25 0"}},
      {{"mangle", "90 90 0"}},
      {},
    }));
    auto entity = Entity{properties};
    entity.setClassname(classname);
    if (entity.classname() == "light_shadow" || entity.classname() == "light_bounce")
      entity.addOrUpdateProperty("convertto", "light_spot");
    const auto before = goldSrcLightDirection(entity);
    REQUIRE(before);
    const auto axis = GENERATE(
      vm::vec3d{1, 0, 0},
      vm::vec3d{0, 1, 0},
      vm::vec3d{0, 0, 1},
      vm::normalize(vm::vec3d{1, 2, 3}));
    const auto degrees = GENERATE(90.0, -90.0, 17.5);
    const auto rotation = vm::rotation_matrix(axis, vm::to_radians(degrees));
    entity.transform(rotation, true, true);
    const auto after = goldSrcLightDirection(entity);
    REQUIRE(after);
    CHECK(*after == vm::approx{rotation * *before, 0.00001});
    REQUIRE(entity.property("angles"));
    const auto angles = vm::parse<double, 3>(*entity.property("angles"));
    REQUIRE(angles);
    if (entity.hasProperty("pitch"))
      CHECK(std::stod(*entity.property("pitch")) == angles->x());
    if (entity.hasProperty("angle"))
      CHECK(std::stod(*entity.property("angle")) == angles->y());
    if (entity.hasProperty("mangle"))
      CHECK(*entity.property("mangle") == "90 90 0");
    entity.transform(vm::rotation_matrix(axis, vm::to_radians(-degrees)), true, true);
    REQUIRE(goldSrcLightDirection(entity));
    CHECK(*goldSrcLightDirection(entity) == vm::approx{*before, 0.00002});
  }

  SECTION("quarter turns clear overrides when the beam becomes horizontal or faces east")
  {
    auto entity = Entity{
      {{"classname", "light_spot"},
       {"angles", "45 180 0"},
       {"pitch", "-90"},
       {"angle", "180"}}};
    entity.transform(vm::mat4x4d::rot_90_y_ccw(), true, true);
    REQUIRE(goldSrcLightDirection(entity));
    CHECK(*goldSrcLightDirection(entity) == vm::approx{vm::vec3d{-1, 0, 0}});
    CHECK(*entity.property("pitch") == "0");
    entity.transform(vm::mat4x4d::rot_180_z(), true, true);
    CHECK(*goldSrcLightDirection(entity) == vm::approx{vm::vec3d{1, 0, 0}});
    CHECK(*entity.property("angle") == "0");
    CHECK(*entity.property("angles") == "0 0 0");
  }

  SECTION("target relationships and nondirectional lights retain their angle properties")
  {
    const auto classname =
      GENERATE("light", "light_surface", "light_shadow", "light_bounce", "light_spot");
    auto entity =
      Entity{{{"classname", classname}, {"pitch", "-90"}, {"angles", "0 0 0"}}};
    if (entity.classname() == "light_spot")
      entity.addOrUpdateProperty("target", "aim");
    const auto before = entity.properties();
    entity.transform(vm::mat4x4d::rot_90_y_ccw(), true, true);
    CHECK(entity.properties() == before);
  }

  SECTION("translation and disabled angle updates do not normalize properties")
  {
    auto entity =
      Entity{{{"classname", "light_spot"}, {"pitch", "-90"}, {"angles", "30 90 0"}}};
    const auto before = entity.properties();
    entity.transform(vm::translation_matrix(vm::vec3d{0, 0, 0}), true, true);
    CHECK(entity.properties() == before);
    entity.transform(vm::mat4x4d::rot_90_y_ccw(), false, true);
    CHECK(entity.properties() == before);
  }

  SECTION("ordinary entities and other games retain existing transform rules")
  {
    const auto classname = GENERATE("info_player_start", "func_door");
    auto ordinary = Entity{{{"classname", classname}, {"angles", "-45 90 0"}}};
    auto previous = ordinary;
    ordinary.transform(vm::mat4x4d::rot_90_y_ccw(), true, true);
    previous.transform(vm::mat4x4d::rot_90_y_ccw(), true);
    CHECK(ordinary.properties() == previous.properties());
    auto light =
      Entity{{{"classname", "light_spot"}, {"pitch", "-90"}, {"angles", "0 0 0"}}};
    light.transform(vm::mat4x4d::rot_90_y_ccw(), true);
    CHECK(*light.property("pitch") == "-90");
  }
}
} // namespace tb::mdl
