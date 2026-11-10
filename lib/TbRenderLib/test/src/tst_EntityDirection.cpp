/*
 Copyright (C) 2026 KreedzBroom contributors
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "gl/OrthographicCamera.h"
#include "gl/PerspectiveCamera.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/WorldNode.h"
#include "render/EntityDirection.h"
#include "render/EntityDirectionIndicator.h"

#include "vm/approx.h"
#include "vm/mat.h"
#include "vm/vec_io.h" // IWYU pragma: keep

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace tb::render
{
TEST_CASE("EntityDirectionResolver")
{
  auto resolver = EntityDirectionResolver{true};

  SECTION("GoldSrc light angles match hltools instead of model pitch")
  {
    using Case = std::pair<std::vector<mdl::EntityProperty>, vm::vec3d>;
    const auto [properties, expected] = GENERATE(values<Case>({
      {{{"angles", "0 90 0"}}, {0, 1, 0}},
      {{{"angles", "-45 0 0"}}, {1, 0, -1}},
      {{{"angles", "45 90 0"}}, {0, 1, 1}},
      {{{"angles", "0 0 0"}, {"pitch", "-90"}}, {0, 0, -1}},
      {{{"angles", "-45 0 0"}, {"pitch", "90"}}, {0, 0, 1}},
      {{{"angles", "-45 90 0"}, {"pitch", "0"}}, {0, 1, -1}},
      {{{"angles", "0 90 0"}, {"angle", "180"}}, {-1, 0, 0}},
      {{{"angles", "0 90 0"}, {"angle", "0"}}, {0, 1, 0}},
      {{{"angles", "-45 90 75"}}, {0, 1, -1}},
      {{{"angles", "0 0 0"}, {"mangle", "90 90 0"}}, {1, 0, 0}},
      {{{"pitch", "-90"}}, {0, 0, -1}},
      {{{"angle", "-1"}, {"pitch", "-45"}}, {0, 0, -1}},
      {{{"angle", "-2"}, {"pitch", "45"}}, {0, 0, 1}},
      {{}, {1, 0, 0}},
    }));
    const auto classname = GENERATE("light_spot", "light_environment", "info_sunlight");
    auto entity = mdl::Entity{properties};
    entity.setClassname(classname);
    const auto node = mdl::EntityNode{std::move(entity)};
    CAPTURE(classname, properties);
    const auto direction = resolver.direction(node);
    REQUIRE(direction);
    CHECK(vm::vec3d{*direction} == vm::approx{vm::normalize(expected), 0.00001});
  }

  SECTION("point and texture lights have no single illumination direction")
  {
    const auto classname =
      GENERATE("light", "light_surface", "light_shadow", "light_bounce");
    const auto node = mdl::EntityNode{
      mdl::Entity{{{"classname", classname}, {"angles", "30 90 0"}, {"pitch", "-90"}}}};
    CHECK_FALSE(resolver.direction(node));
    const auto textureLight = mdl::EntityNode{
      mdl::Entity{{{"classname", "light_spot"}, {"_tex", "lamp"}, {"pitch", "-90"}}}};
    CHECK_FALSE(resolver.direction(textureLight));
  }

  SECTION("converted shadow and bounce spotlights use the compiled class")
  {
    const auto classname = GENERATE("light_shadow", "light_bounce");
    const auto node = mdl::EntityNode{mdl::Entity{
      {{"classname", classname}, {"convertto", "light_spot"}, {"pitch", "-90"}}}};
    REQUIRE(resolver.direction(node));
    CHECK(*resolver.direction(node) == vm::approx{vm::vec3f{0, 0, -1}});
  }

  SECTION("ordinary entities and other games retain their own rotations")
  {
    const auto classname = GENERATE("light_spot", "info_player_start", "func_door");
    const auto node = mdl::EntityNode{
      mdl::Entity{{{"classname", classname}, {"angles", "-45 90 0"}, {"pitch", "90"}}}};
    auto otherGame = EntityDirectionResolver{false};
    CHECK(
      *otherGame.direction(node)
      == vm::vec3f{node.entity().rotation() * vm::vec3d{1, 0, 0}});
    if (std::string_view{classname} != "light_spot")
      CHECK(resolver.direction(node) == otherGame.direction(node));
  }

  SECTION("named targets override angles without relying on FGD links or visibility")
  {
    auto world = mdl::WorldNode{{}, {}, mdl::MapFormat::Valve};
    auto* group = new mdl::GroupNode{mdl::Group{"targets"}};
    world.defaultLayer()->addChild(group);
    auto* target = new mdl::EntityNode{mdl::Entity{
      {{"classname", "info_target"}, {"targetname", "aim"}, {"origin", "100 60 40"}}}};
    group->addChild(target);
    target->setVisibilityState(mdl::VisibilityState::Hidden);
    auto* second = new mdl::EntityNode{mdl::Entity{
      {{"classname", "info_target"}, {"targetname", "aim"}, {"origin", "-100 0 0"}}}};
    world.defaultLayer()->addChild(second);
    const auto classname =
      GENERATE("light", "light_spot", "light_environment", "info_sunlight");
    auto* light = new mdl::EntityNode{mdl::Entity{
      {{"classname", classname},
       {"origin", "20 0 40"},
       {"target", "aim"},
       {"angles", "-45 180 0"},
       {"pitch", "90"}}}};
    world.defaultLayer()->addChild(light);
    CHECK_FALSE(target->entity().definition());
    REQUIRE(resolver.direction(*light));
    CHECK(*resolver.direction(*light) == vm::approx{vm::vec3f{0.8f, 0.6f, 0}});

    // A new render pass must reflect target edits without modifying the light.
    auto edited = target->entity();
    edited.setOrigin({20, 0, -40});
    target->setEntity(std::move(edited));
    auto nextFrame = EntityDirectionResolver{true};
    CHECK(*nextFrame.direction(*light) == vm::approx{vm::vec3f{0, 0, -1}});

    // A removed/renamed target falls back to the light's own direction.
    edited = target->entity();
    edited.addOrUpdateProperty("targetname", "renamed");
    target->setEntity(std::move(edited));
    world.defaultLayer()->removeChild(second);
    delete second;
    auto missingFrame = EntityDirectionResolver{true};
    CHECK(*missingFrame.direction(*light) == vm::approx{vm::vec3f{0, 0, 1}});
  }

  SECTION("targets in layers omitted from compilation do not override angles")
  {
    auto world = mdl::WorldNode{{}, {}, mdl::MapFormat::Valve};
    auto* layer = new mdl::LayerNode{mdl::Layer{"excluded"}};
    auto data = layer->layer();
    data.setOmitFromExport(true);
    layer->setLayer(std::move(data));
    world.addChild(layer);
    layer->addChild(new mdl::EntityNode{mdl::Entity{
      {{"classname", "info_target"}, {"targetname", "aim"}, {"origin", "0 0 100"}}}});
    auto* light = new mdl::EntityNode{
      mdl::Entity{{{"classname", "light_spot"}, {"target", "aim"}, {"pitch", "-90"}}}};
    world.defaultLayer()->addChild(light);
    REQUIRE(resolver.direction(*light));
    CHECK(*resolver.direction(*light) == vm::approx{vm::vec3f{0, 0, -1}});
  }

  SECTION("coincident targets and degenerate legacy angles suppress invalid arrows")
  {
    auto world = mdl::WorldNode{{}, {}, mdl::MapFormat::Valve};
    auto* light = new mdl::EntityNode{mdl::Entity{
      {{"classname", "light_spot"}, {"targetname", "self"}, {"target", "self"}}}};
    world.defaultLayer()->addChild(light);
    CHECK_FALSE(resolver.direction(*light));
    const auto angle = GENERATE("-1", "-2");
    const auto node = mdl::EntityNode{
      mdl::Entity{{{"classname", "light_spot"}, {"angle", angle}, {"pitch", "0"}}}};
    CHECK_FALSE(resolver.direction(node));
    const auto malformed =
      mdl::EntityNode{mdl::Entity{{{"classname", "light_spot"}, {"angles", "nan 90 0"}}}};
    CHECK_FALSE(resolver.direction(malformed));
  }

  SECTION("compiled downward light direction projects consistently in 2D and 3D")
  {
    const auto node =
      mdl::EntityNode{mdl::Entity{{{"classname", "light_spot"}, {"angles", "-45 0 0"}}}};
    const auto direction = resolver.direction(node);
    REQUIRE(direction);
    auto side = gl::OrthographicCamera{
      1, 8192, {0, 0, 800, 600}, {0, -256, 0}, {0, 1, 0}, {0, 0, 1}};
    auto perspective = gl::PerspectiveCamera{
      90, 1, 8192, {0, 0, 800, 600}, {0, -256, 0}, {0, 1, 0}, {0, 0, 1}};
    for (const auto* camera :
         {static_cast<const gl::Camera*>(&side),
          static_cast<const gl::Camera*>(&perspective)})
    {
      const auto indicator = makeEntityDirectionIndicator(*camera, {}, *direction);
      REQUIRE(indicator);
      CHECK(indicator->kind == EntityDirectionIndicatorKind::Arrow);
      auto tip = vm::vec2f{};
      for (const auto& vertex : indicator->fill)
      {
        const auto delta = (camera->project(vertex) - camera->project({})).xy();
        if (vm::squared_length(delta) > vm::squared_length(tip))
          tip = delta;
      }
      CHECK(tip.x() > 0);
      CHECK(tip.y() < 0);
    }
    auto top = gl::OrthographicCamera{
      1, 8192, {0, 0, 800, 600}, {0, 0, 256}, {0, 0, -1}, {0, 1, 0}};
    const auto down = mdl::EntityNode{
      mdl::Entity{{{"classname", "light_environment"}, {"pitch", "-90"}}}};
    const auto indicator =
      makeEntityDirectionIndicator(top, {}, *resolver.direction(down));
    REQUIRE(indicator);
    CHECK(indicator->kind == EntityDirectionIndicatorKind::AwayFromViewer);
  }
}
} // namespace tb::render
