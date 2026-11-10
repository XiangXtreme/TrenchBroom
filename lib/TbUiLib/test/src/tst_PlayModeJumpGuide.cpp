/*
 Copyright (C) 2026 KreedzBroom contributors
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QDir>
#include <QImage>
#include <QtTest/QTest>

#include "base/PreferenceManager.h"
#include "cs/scs.h"
#include "gl/GlInterface.h"
#include "gl/PerspectiveCamera.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushNode.h"
#include "mdl/EntityNode.h"
#include "mdl/Map.h"
#include "mdl/MapFixture.h"
#include "mdl/Map_Nodes.h"
#include "mdl/WorldNode.h"
#include "prefs/Preferences.h"
#include "render/RenderBatch.h"
#include "render/RenderContext.h"
#include "render/RenderService.h"
#include "ui/AppControllerFixture.h"
#include "ui/CatchConfig.h"
#include "ui/PlayModeJumpGuide.h"
#include "ui/PlayModeWorld.h"
#include "ui/RenderView.h"

#include "kd/result.h"

#include <algorithm>
#include <cmath>
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

void addBox(
  mdl::Map& map, const vm::bbox3d& bounds, const std::string& material = "stone")
{
  const auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};
  auto* brush = new mdl::BrushNode{builder.createCuboid(bounds, material) | kdl::value()};
  REQUIRE_FALSE(mdl::addNodes(map, {{&mdl::parentForNodes(map), {brush}}}).empty());
}

ContextPtr contextFor(const PlayModeWorld& world)
{
  REQUIRE(world.world);
  auto context = ContextPtr{scs_init(nullptr)};
  REQUIRE(context);
  REQUIRE(scs_set_world(context.get(), world.world.get()) == SCS_OK);
  return context;
}

scs_player_state groundedAt(
  scs_context* context, const float x, const float y = 0, const float height = 0)
{
  const float origin[] = {x, y, height + 36.0f};
  scs_spawn(context, origin, nullptr);
  auto cmd = scs_usercmd{};
  cmd.msec = 10;
  scs_step(context, &cmd);
  auto state = scs_player_state{};
  scs_get_state(context, &state);
  REQUIRE(state.ground_entity != -1);
  return state;
}

class JumpGuideView : public RenderView
{
public:
  PlayModeJumpGuide guide;
  int frames = 0;
  explicit JumpGuideView(AppController& app)
    : RenderView{app}
  {
  }
  void processEvent(const KeyEvent&) override {}
  void processEvent(const MouseEvent&) override {}
  void processEvent(const GestureEvent&) override {}
  void processEvent(const ScrollEvent&) override {}
  void processEvent(const CancelEvent&) override {}

private:
  bool shouldRenderFocusIndicator() const override { return false; }
  void renderContents(gl::Gl& gl) override
  {
    ++frames;
    gl.viewport(
      0, 0, int(width() * devicePixelRatioF()), int(height() * devicePixelRatioF()));
    gl.enable(GL_DEPTH_TEST);
    gl.depthFunc(GL_LEQUAL);
    gl.enable(GL_BLEND);
    gl.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    auto camera = gl::PerspectiveCamera{
      90,
      1,
      4096,
      {0, 0, width(), height()},
      {-64, -24, 53},
      vm::normalize(vm::vec3f{1, 0.08f, -0.38f}),
      {0, 0, 1}};
    auto context = render::RenderContext{
      gl, render::RenderMode::Render3D, camera, fontManager(), shaderManager()};
    auto batch = render::RenderBatch{vboManager()};
    {
      auto service = render::RenderService{context, batch};
      service.setShowBackfaces();
      service.setHideOccludedObjects();
      service.setForegroundColor(pref(Preferences::FaceColor));
      service.renderFilledPolygon(
        {{-512, -128, 0}, {0, -128, 0}, {0, 128, 0}, {-512, 128, 0}});
      service.renderFilledPolygon(
        {{176, -128, 0}, {400, -128, 0}, {400, 128, 0}, {176, 128, 0}});
      service.setForegroundColor(pref(Preferences::LockedFaceColor));
      service.renderFilledPolygon(
        {{0, -128, -16}, {0, 128, -16}, {0, 128, 0}, {0, -128, 0}});
      service.renderFilledPolygon(
        {{176, -128, -16}, {176, 128, -16}, {176, 128, 0}, {176, -128, 0}});
    }
    renderPlayModeJumpGuide(guide, context, batch);
    batch.render(context);
  }
};
} // namespace

TEST_CASE("PlayModeJumpGuide")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.create();
  addBox(map, {{-512, -128, -16}, {0, 128, 0}});
  addBox(map, {{176, -128, -16}, {400, 128, 0}});
  // The pit floor must not be mistaken for a landing or determine the span's height.
  addBox(map, {{-512, -512, -256}, {512, 512, -200}});

  SECTION("176 is the edge gap rather than the changing player-to-target distance")
  {
    const auto world = buildPlayModeWorld(map);
    auto context = contextFor(world);
    for (const auto x : {-96.0f, -48.0f, -8.0f})
    {
      const auto state = groundedAt(context.get(), x);
      const auto guide =
        measurePlayModeJumpGuide(context.get(), world.jumpSurfaces, state, {1, 0, -0.8f});
      REQUIRE(guide.span);
      CHECK(guide.span->distance == Catch::Approx(176).margin(0.01));
      CHECK(guide.span->start.x() == Catch::Approx(0).margin(0.01));
      CHECK(guide.span->end.x() == Catch::Approx(176).margin(0.01));
      CHECK(guide.span->end.z() == Catch::Approx(0).margin(0.01));
    }
  }
  SECTION("joining coplanar brushes does not create a gap")
  {
    addBox(map, {{0, -128, -16}, {176, 128, 0}});
    const auto world = buildPlayModeWorld(map);
    auto context = contextFor(world);
    const auto guide = measurePlayModeJumpGuide(
      context.get(), world.jumpSurfaces, groundedAt(context.get(), -48), {1, 0, 0});
    CHECK_FALSE(guide.span);
  }
  SECTION("stacked landing faces select the platform top regardless of brush order")
  {
    const auto landingHeight = GENERATE(0.0, 32.0, -32.0);
    const auto floorInset = GENERATE(0.0, 0.05, -0.05);
    CAPTURE(landingHeight, floorInset);
    auto stackedFixture = mdl::MapFixture{};
    auto& stackedMap = stackedFixture.create();
    addBox(stackedMap, {{-512, -128, -104}, {0, 128, 0}});
    addBox(stackedMap, {{200 + floorInset, -512, -104}, {512, 512, -88}});
    addBox(stackedMap, {{200, -128, -88}, {400, 128, landingHeight}});
    auto world = buildPlayModeWorld(stackedMap);
    auto context = contextFor(world);
    const float origin[] = {-48, 0, 40};
    scs_spawn(context.get(), origin, nullptr);
    auto command = scs_usercmd{};
    command.msec = 10;
    for (auto i = 0; i < 50; ++i)
      scs_step(context.get(), &command);
    auto state = scs_player_state{};
    scs_get_state(context.get(), &state);
    REQUIRE(state.ground_entity != -1);
    REQUIRE(state.origin[2] == Catch::Approx(36.03125f).margin(0.001));
    for (auto order = 0; order < 2; ++order)
    {
      CAPTURE(order);
      const auto guide =
        measurePlayModeJumpGuide(context.get(), world.jumpSurfaces, state, {1, 0, 0});
      REQUIRE(guide.span);
      CHECK(guide.span->distance == Catch::Approx(200).margin(0.01));
      CHECK(guide.span->start.z() == Catch::Approx(0).margin(0.001));
      CHECK(guide.span->end.z() == Catch::Approx(landingHeight).margin(0.001));
      std::ranges::reverse(world.jumpSurfaces);
    }
  }
  SECTION("an inset platform excludes the floor lip where the player cannot fit")
  {
    // Bounds read from the affected pair in Lws_gxl_speedrun. The lower floor
    // starts 8 units before the landing block, producing a false 200 / Z -88.
    const auto floorLip = GENERATE(8.0, 32.0);
    const auto ducked = GENERATE(false, true);
    CAPTURE(floorLip, ducked);
    auto insetFixture = mdl::MapFixture{};
    auto& insetMap = insetFixture.create();
    addBox(insetMap, {{-424, -192, 448}, {-328, -96, 536}});
    addBox(insetMap, {{-128, -480, 384}, {192, 272, 448}});
    addBox(insetMap, {{-128 + floorLip, -192, 448}, {184, -96, 536}});
    auto world = buildPlayModeWorld(insetMap);
    auto context = contextFor(world);
    auto state = groundedAt(context.get(), -376, -144, 536);
    if (ducked)
    {
      state.flags |= SCS_FL_DUCKING;
      state.origin[2] -= 18;
      scs_set_state(context.get(), &state);
    }
    // A wider floor lip remains a real lower landing.
    const auto expectedEnd =
      floorLip < 16 ? vm::vec3f{-120, -144, 536} : vm::vec3f{-128, -144, 448};
    for (auto order = 0; order < 2; ++order)
    {
      for (const auto yaw : {-0.3f, 0.0f, 0.3f})
      {
        const auto guide = measurePlayModeJumpGuide(
          context.get(), world.jumpSurfaces, state, {1, yaw, -0.5f});
        REQUIRE(guide.span);
        CHECK(guide.span->distance == Catch::Approx(expectedEnd.x() + 328).margin(0.01));
        CHECK(guide.span->start == vm::vec3f{-328, -144, 536});
        CHECK(guide.span->end == expectedEnd);
      }
      std::ranges::reverse(world.jumpSurfaces);
    }
  }
  SECTION("thin landing faces and angled measurements are not skipped")
  {
    addBox(map, {{168, -128, -16}, {168.5, 128, 0}});
    const auto world = buildPlayModeWorld(map);
    auto context = contextFor(world);
    const auto guide = measurePlayModeJumpGuide(
      context.get(), world.jumpSurfaces, groundedAt(context.get(), -48), {1, 0.1f, 0});
    REQUIRE(guide.span);
    CHECK(guide.span->distance == Catch::Approx(168).margin(0.01));
    CHECK(guide.span->start.y() == Catch::Approx(guide.span->end.y()).margin(0.01));
  }
  SECTION("only the nearest gap in the 160 inclusive to 300 exclusive range is shown")
  {
    for (const auto gap : {16.0f, 100.0f, 159.9f, 160.0f, 176.0f, 299.9f, 300.0f, 512.0f})
    {
      CAPTURE(gap);
      auto rangeFixture = mdl::MapFixture{};
      auto& rangeMap = rangeFixture.create();
      addBox(rangeMap, {{-512, -128, -16}, {0, 128, 0}});
      addBox(rangeMap, {{gap, -128, -16}, {gap + 64, 128, 0}});
      const auto world = buildPlayModeWorld(rangeMap);
      auto context = contextFor(world);
      const auto guide = measurePlayModeJumpGuide(
        context.get(), world.jumpSurfaces, groundedAt(context.get(), -48), {1, 0, 0});
      CHECK(guide.span.has_value() == (gap >= 160 && gap < 300));
      if (guide.span)
        CHECK(guide.span->distance == Catch::Approx(gap).margin(0.01));
    }
  }
  SECTION("a nearby lower ledge does not fall through to a farther in-range platform")
  {
    addBox(map, {{16, -128, -104}, {32, 128, -88}});
    const auto world = buildPlayModeWorld(map);
    auto context = contextFor(world);
    const auto guide = measurePlayModeJumpGuide(
      context.get(), world.jumpSurfaces, groundedAt(context.get(), -48), {1, 0, 0});
    CHECK_FALSE(guide.span);
  }
  SECTION("turning the view keeps dimensions perpendicular to the platform edge")
  {
    const auto world = buildPlayModeWorld(map);
    auto context = contextFor(world);
    const auto state = groundedAt(context.get(), -48, 12);
    for (const auto yaw : {-0.8f, -0.3f, 0.0f, 0.3f, 0.8f})
    {
      const auto guide = measurePlayModeJumpGuide(
        context.get(), world.jumpSurfaces, state, {1, yaw, -0.5f});
      REQUIRE(guide.span);
      CHECK(guide.span->distance == Catch::Approx(176).margin(0.01));
      CHECK(guide.span->start.x() == Catch::Approx(0).margin(0.01));
      CHECK(guide.span->start.y() == Catch::Approx(12).margin(0.01));
      CHECK(guide.span->end.y() == Catch::Approx(12).margin(0.01));
    }
  }
  SECTION("rotated platforms follow their real edge rather than the world axes")
  {
    // Two separate 45-degree platforms, far from the axis-aligned fixture.
    const auto rotate = [](const double x, const double y, const double z) {
      const auto scale = std::sqrt(0.5);
      return vm::vec3d{2048 + (x - y) * scale, (x + y) * scale, z};
    };
    const auto builder =
      mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};
    for (const auto bounds : {vm::vec2d{-512, 0}, vm::vec2d{176, 400}})
    {
      auto points = std::vector<vm::vec3d>{};
      for (const auto x : {bounds.x(), bounds.y()})
        for (const auto y : {-128.0, 128.0})
          for (const auto z : {-16.0, 0.0})
            points.push_back(rotate(x, y, z));
      auto* brush =
        new mdl::BrushNode{builder.createBrush(points, "stone") | kdl::value()};
      REQUIRE_FALSE(mdl::addNodes(map, {{&mdl::parentForNodes(map), {brush}}}).empty());
    }
    const auto world = buildPlayModeWorld(map);
    auto context = contextFor(world);
    const auto origin = vm::vec3f{rotate(-48, 0, 36)};
    scs_spawn(context.get(), origin.v, nullptr);
    auto command = scs_usercmd{};
    command.msec = 10;
    scs_step(context.get(), &command);
    auto state = scs_player_state{};
    scs_get_state(context.get(), &state);
    REQUIRE(state.ground_entity != -1);
    for (const auto yaw : {-0.3, 0.0, 0.3})
    {
      const auto look = vm::vec3f{rotate(1, yaw, 0) - rotate(0, 0, 0)};
      const auto guide =
        measurePlayModeJumpGuide(context.get(), world.jumpSurfaces, state, look);
      REQUIRE(guide.span);
      CHECK(guide.span->distance == Catch::Approx(176).margin(0.02));
      const auto delta = guide.span->end - guide.span->start;
      CHECK(delta.x() == Catch::Approx(delta.y()).margin(0.01));
    }
  }
  SECTION("a lower landing reports its own signed height")
  {
    auto lowerFixture = mdl::MapFixture{};
    auto& lowerMap = lowerFixture.create();
    addBox(lowerMap, {{-512, -128, -16}, {0, 128, 0}});
    addBox(lowerMap, {{168, -128, -48}, {172, 128, -32}});
    const auto world = buildPlayModeWorld(lowerMap);
    auto context = contextFor(world);
    const auto guide = measurePlayModeJumpGuide(
      context.get(), world.jumpSurfaces, groundedAt(context.get(), -48), {1, 0, 0});
    REQUIRE(guide.span);
    CHECK(guide.span->distance == Catch::Approx(168).margin(0.01));
    CHECK(guide.span->end.z() - guide.span->start.z() == Catch::Approx(-32).margin(0.05));
  }
  SECTION("a hidden landing behind a wall is not measured")
  {
    addBox(map, {{60, -128, -32}, {64, 128, 512}});
    const auto world = buildPlayModeWorld(map);
    auto context = contextFor(world);
    const auto guide = measurePlayModeJumpGuide(
      context.get(), world.jumpSurfaces, groundedAt(context.get(), -48), {1, 0, 0});
    CHECK_FALSE(guide.span);
  }
  SECTION("an elevated landing still measures the horizontal gap")
  {
    addBox(map, {{168, -128, -16}, {172, 128, 32}});
    const auto world = buildPlayModeWorld(map);
    auto context = contextFor(world);
    const auto guide = measurePlayModeJumpGuide(
      context.get(), world.jumpSurfaces, groundedAt(context.get(), -48), {1, 0, 0});
    REQUIRE(guide.span);
    CHECK(guide.span->distance == Catch::Approx(168).margin(0.01));
    CHECK(guide.span->end.z() - guide.span->start.z() == Catch::Approx(32).margin(0.05));
  }
  SECTION("measuring does not advance physics and airborne states have no span")
  {
    const auto world = buildPlayModeWorld(map);
    auto context = contextFor(world);
    auto state = groundedAt(context.get(), -48);
    measurePlayModeJumpGuide(context.get(), world.jumpSurfaces, state, {1, 0, 0});
    auto after = scs_player_state{};
    scs_get_state(context.get(), &after);
    CHECK(after.origin[0] == state.origin[0]);
    CHECK(after.origin[2] == state.origin[2]);
    CHECK(after.velocity[0] == state.velocity[0]);
    CHECK(after.ground_entity == state.ground_entity);
    state.ground_entity = -1;
    const auto guide =
      measurePlayModeJumpGuide(context.get(), world.jumpSurfaces, state, {1, 0, 0});
    CHECK_FALSE(guide.span);
  }
  SECTION("a real jump hides the span until landing")
  {
    const auto world = buildPlayModeWorld(map);
    auto context = contextFor(world);
    auto state = groundedAt(context.get(), -48);
    const auto before =
      measurePlayModeJumpGuide(context.get(), world.jumpSurfaces, state, {1, 0, 0});
    REQUIRE(before.span);
    auto command = scs_usercmd{};
    command.msec = 10;
    command.buttons = SCS_IN_JUMP;
    scs_step(context.get(), &command);
    scs_get_state(context.get(), &state);
    REQUIRE(state.ground_entity == -1);
    const auto during =
      measurePlayModeJumpGuide(context.get(), world.jumpSurfaces, state, {1, 0, 0});
    CHECK_FALSE(during.span);
    command.buttons = 0;
    for (auto i = 0; i < 150 && state.ground_entity == -1; ++i)
    {
      scs_step(context.get(), &command);
      scs_get_state(context.get(), &state);
    }
    REQUIRE(state.ground_entity != -1);
    const auto after =
      measurePlayModeJumpGuide(context.get(), world.jumpSurfaces, state, {1, 0, 0});
    CHECK(after.span.has_value());
  }
  SECTION("non-colliding surfaces are excluded by the collision compiler")
  {
    const auto count = buildPlayModeWorld(map).jumpSurfaces.size();
    addBox(map, {{64, -128, -16}, {80, 128, 0}}, "noclip");
    addBox(map, {{90, -128, -16}, {96, 128, 0}}, "!water");
    CHECK(buildPlayModeWorld(map).jumpSurfaces.size() == count);
  }
  SECTION("span labels render in the 3D framebuffer")
  {
    const auto world = buildPlayModeWorld(map);
    auto context = contextFor(world);
    const auto guide = measurePlayModeJumpGuide(
      context.get(), world.jumpSurfaces, groundedAt(context.get(), -64), {1, 0, 0});
    REQUIRE(guide.span);
    auto app = AppControllerFixture{};
    auto view = JumpGuideView{app.appController()};
    view.resize(768, 480);
    view.show();
    REQUIRE(QTest::qWaitFor([&] { return view.frames > 0; }, 5000));
    REQUIRE(view.isValid());
    const auto plain = view.grabFramebuffer();
    view.guide = guide;
    const auto labeled = view.grabFramebuffer();
    REQUIRE_FALSE(labeled.isNull());
    auto changed = 0;
    for (auto y = 0; y < labeled.height(); ++y)
      for (auto x = 0; x < labeled.width(); ++x)
        changed += labeled.pixel(x, y) != plain.pixel(x, y) ? 1 : 0;
    CHECK(changed > 100);
    const auto directory = qEnvironmentVariable("TB_RENDER_TEST_ARTIFACT_DIR");
    if (!directory.isEmpty())
    {
      REQUIRE(QDir{}.mkpath(directory));
      REQUIRE(labeled.save(directory + "/play-jump-guides.png"));
    }
  }
}
} // namespace tb::ui
