/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QImage>
#include <QPushButton>
#include <QScopeGuard>
#include <QSlider>
#include <QtTest/QTest>

#include "base/PreferenceManager.h"
#include "gl/GlInterface.h"
#include "gl/OrthographicCamera.h"
#include "gl/PerspectiveCamera.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushNode.h"
#include "mdl/EntityNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Entities.h"
#include "mdl/Map_Geometry.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/WorldNode.h"
#include "prefs/Preferences.h"
#include "render/RenderBatch.h"
#include "render/RenderContext.h"
#include "ui/AppControllerFixture.h"
#include "ui/EntityMotionPreview.h"
#include "ui/EntityMotionPreviewWidget.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"
#include "ui/RenderView.h"
#include "ui/SelectionPropertyEditor.h"

#include "kd/result.h"

#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace tb::ui
{
namespace
{
mdl::MapFixtureConfig goldSrcConfig()
{
  auto config = mdl::QuakeFixtureConfig;
  config.gameInfo.gameConfig.materialConfig.extensions = {".C"};
  return config;
}

mdl::BrushNode* addBox(
  mdl::Map& map,
  mdl::Node& parent,
  const vm::bbox3d& bounds,
  const std::string& material = "stone")
{
  auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};
  auto* brush = new mdl::BrushNode{builder.createCuboid(bounds, material) | kdl::value()};
  REQUIRE_FALSE(mdl::addNodes(map, {{&parent, {brush}}}).empty());
  return brush;
}

mdl::EntityNode* addDoor(mdl::Map& map, const std::string& classname = "func_door")
{
  auto* entity = new mdl::EntityNode{mdl::Entity{{{"classname", classname}}}};
  REQUIRE_FALSE(mdl::addNodes(map, {{&mdl::parentForNodes(map), {entity}}}).empty());
  addBox(map, *entity, {{0, 0, 0}, {128, 16, 64}});
  mdl::deselectAll(map);
  mdl::selectNodes(map, {entity});
  return entity;
}

void checkPoint(const vm::vec3d& actual, const vm::vec3d& expected)
{
  for (size_t i = 0; i < 3; ++i)
    CHECK(actual[i] == Catch::Approx(expected[i]).margin(1e-6));
}

class MotionPreviewView : public RenderView
{
public:
  EntityMotionPreview& preview;
  bool orthographic;
  int frames = 0;
  MotionPreviewView(
    AppController& app, EntityMotionPreview& motionPreview, const bool ortho)
    : RenderView{app}
    , preview{motionPreview}
    , orthographic{ortho}
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
    gl.enable(GL_BLEND);
    gl.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    auto perspective = gl::PerspectiveCamera{
      75,
      1,
      4096,
      {0, 0, width(), height()},
      {320, -320, 240},
      vm::normalize(vm::vec3f{-1, 2, -1}),
      {0, 0, 1}};
    auto ortho = gl::OrthographicCamera{
      1, 4096, {0, 0, width(), height()}, {160, 0, 512}, {0, 0, -1}, {0, 1, 0}};
    const auto& camera = orthographic ? static_cast<const gl::Camera&>(ortho)
                                      : static_cast<const gl::Camera&>(perspective);
    auto context = render::RenderContext{
      gl,
      orthographic ? render::RenderMode::Render2D : render::RenderMode::Render3D,
      camera,
      fontManager(),
      shaderManager()};
    auto batch = render::RenderBatch{vboManager()};
    preview.render(context, batch);
    batch.render(context);
  }
};
} // namespace

TEST_CASE("GoldSrcEntityMotion")
{
  auto fixture = MapDocumentFixture{};
  auto& document = fixture.create(goldSrcConfig());
  auto& map = document.map();
  auto* entity = addDoor(map);
  auto motion = [&] {
    auto value = goldSrcEntityMotion(*entity);
    REQUIRE(value);
    return *value;
  };
  auto set = [&](const std::string& key, const std::string& value) {
    REQUIRE(mdl::setEntityProperty(map, key, value));
  };

  SECTION("raw brush bounds are not shortened by the engine bbox expansion")
  {
    set("lip", "8");
    CHECK(motion().distance == 120);
    checkPoint(motion().transform(0.5) * vm::vec3d{0, 0, 0}, {60, 0, 0});
    set("spawnflags", "1");
    checkPoint(motion().transform(0) * vm::vec3d{0, 0, 0}, {120, 0, 0});
    checkPoint(motion().transform(1) * vm::vec3d{0, 0, 0}, {0, 0, 0});
  }
  SECTION("direction handles legacy vertical angles and full pitch yaw")
  {
    set("angle", "-1");
    checkPoint(motion().transform(1) * vm::vec3d{0, 0, 0}, {0, 0, 64});
    set("angle", "-2");
    checkPoint(motion().transform(1) * vm::vec3d{0, 0, 0}, {0, 0, -64});
    set("angles", "0 90 0");
    checkPoint(motion().transform(1) * vm::vec3d{0, 0, 0}, {0, 16, 0});
    set("angles", "90 0 0");
    checkPoint(motion().transform(1) * vm::vec3d{0, 0, 0}, {0, 0, -64});
  }
  SECTION("negative door travel remains a signed movement")
  {
    set("lip", "160");
    CHECK(motion().problem == MotionProblem::None);
    checkPoint(motion().transform(1) * vm::vec3d{0, 0, 0}, {-32, 0, 0});
  }
  SECTION("buttons default zero lip to four and honor Dont Move")
  {
    set("classname", "func_button");
    CHECK(motion().distance == 124);
    set("lip", "0");
    CHECK(motion().distance == 124);
    set("lip", "127.5");
    CHECK(motion().problem == MotionProblem::Stationary);
    set("lip", "4");
    set("spawnflags", "1");
    CHECK(motion().problem == MotionProblem::Stationary);
    CHECK_FALSE(motion().startOpen);
  }
  SECTION("rotating door axis flags follow GoldSrc Euler components")
  {
    set("classname", "func_door_rotating");
    set("distance", "90");
    checkPoint(motion().transform(1) * vm::vec3d{10, 0, 0}, {0, 10, 0});
    CHECK(motion().bidirectional);
    checkPoint(motion().transform(1, true) * vm::vec3d{10, 0, 0}, {0, -10, 0});
    set("spawnflags", "2");
    checkPoint(motion().transform(1) * vm::vec3d{10, 0, 0}, {0, -10, 0});
    set("spawnflags", "64");
    checkPoint(motion().transform(1) * vm::vec3d{0, 10, 0}, {0, 0, 10});
    CHECK_FALSE(motion().bidirectional);
    set("spawnflags", "128");
    checkPoint(motion().transform(1) * vm::vec3d{10, 0, 0}, {0, 0, -10});
    set("spawnflags", "192"); // Z flag takes precedence in AxisDir.
    checkPoint(motion().transform(1) * vm::vec3d{0, 10, 0}, {0, 0, 10});
  }
  SECTION("ORIGIN brush defines the pivot and does not enlarge travel bounds")
  {
    addBox(map, *entity, {{-8, -8, -8}, {8, 8, 8}}, "ORIGIN");
    CHECK(motion().distance == 128);
    CHECK(motion().faces.size() == 6);
    set("classname", "func_door_rotating");
    set("distance", "90");
    checkPoint(motion().transform(1) * vm::vec3d{10, 0, 0}, {0, 10, 0});
    set("spawnflags", "1");
    checkPoint(motion().transform(0) * vm::vec3d{10, 0, 0}, {0, 10, 0});
    checkPoint(motion().transform(1) * vm::vec3d{10, 0, 0}, {10, 0, 0});
    set("origin", "0 0 0");
    CHECK(motion().problem == MotionProblem::OriginConflict);
  }
  SECTION("pivot keys and initial angles affect the complete runtime transform")
  {
    set("classname", "func_door_rotating");
    set("distance", "90");
    set("origin", "100 0 0");
    set("angles", "0 90 0");
    checkPoint(motion().transform(0) * vm::vec3d{110, 0, 0}, {100, 10, 0});
    checkPoint(motion().transform(1) * vm::vec3d{110, 0, 0}, {90, 0, 0});
    checkPoint(motion().transform(1, true) * vm::vec3d{110, 0, 0}, {90, 0, 0});
    checkPoint(
      motion().transform(0.5, true) * vm::vec3d{110, 0, 0},
      {100 + std::sqrt(50.0), -std::sqrt(50.0), 0});
    set("spawnflags", "16");
    CHECK_FALSE(motion().bidirectional);
  }
  SECTION("ORIGIN centers use the final integer key from the compiler")
  {
    addBox(map, *entity, {{92, -9, -8}, {109, 8, 8}}, "ORIGIN");
    checkPoint(motion().origin, {100, 0, 0});
    checkPoint(motion().transform(0) * vm::vec3d{110, 0, 0}, {110, 0, 0});
    set("origin", ""); // An empty key is not a compiler conflict.
    CHECK(motion().problem == MotionProblem::None);
    set("classname", "func_door_rotating");
    set("distance", "90");
    checkPoint(motion().transform(1) * vm::vec3d{110, 0, 0}, {100, 10, 0});
  }
  SECTION("collision-only brushes do not enlarge the visible model or travel")
  {
    addBox(map, *entity, {{-256, -256, -256}, {256, 256, 256}}, "CLIP");
    CHECK(motion().distance == 128);
    CHECK(motion().faces.size() == 6);
    set("zhlt_usemodel", "");
    set("zhlt_transform", "");
    CHECK(motion().problem == MotionProblem::None);
  }
  SECTION("invalid values and compiler overrides produce diagnostics")
  {
    set("lip", "nan");
    CHECK(motion().problem == MotionProblem::InvalidProperty);
    set("lip", "8garbage");
    CHECK(motion().problem == MotionProblem::InvalidProperty);
    set("lip", "8");
    set("zhlt_usemodel", "external");
    CHECK(motion().problem == MotionProblem::CompilerOverride);
    set("classname", "light");
    CHECK_FALSE(goldSrcEntityMotion(*entity));
  }
}

TEST_CASE("EntityMotionPreview")
{
  auto app = AppControllerFixture{};
  auto fixture = MapDocumentFixture{};
  auto& document = fixture.create(goldSrcConfig());
  auto& map = document.map();
  auto preview = EntityMotionPreview{document};
  CHECK_FALSE(preview.motion());
  auto* entity = addDoor(map);
  REQUIRE(preview.motion());

  SECTION("scrubbing never changes geometry properties modification state or undo")
  {
    const auto count = map.modificationCount();
    const auto properties = entity->entity().properties();
    const auto bounds = entity->physicalBounds();
    const auto undo = *map.undoCommandName();
    for (int i = 0; i <= 100; ++i)
      preview.setProgress(double(i) / 100);
    CHECK(map.modificationCount() == count);
    CHECK(entity->entity().properties() == properties);
    CHECK(entity->physicalBounds() == bounds);
    CHECK(*map.undoCommandName() == undo);
    CHECK(preview.active());
    preview.reset();
    CHECK_FALSE(preview.active());
  }
  SECTION("property updates preserve progress and native undo recalculates movement")
  {
    preview.setProgress(0.5);
    REQUIRE(mdl::setEntityProperty(map, "lip", "16"));
    REQUIRE(preview.motion());
    CHECK(preview.progress() == 0.5);
    CHECK(preview.motion()->distance == 112);
    map.undoCommand();
    CHECK(preview.progress() == 0.5);
    CHECK(preview.motion()->distance == 128);
  }
  SECTION("geometry edits rebuild cached vertices and preserve progress")
  {
    preview.setProgress(0.5);
    const auto point = preview.motion()->faces.front().front();
    REQUIRE(mdl::translateSelection(map, {32, 0, 0}));
    REQUIRE(preview.motion());
    checkPoint(preview.motion()->faces.front().front(), point + vm::vec3d{32, 0, 0});
    CHECK(preview.progress() == 0.5);
    map.undoCommand();
    REQUIRE(preview.motion());
    checkPoint(preview.motion()->faces.front().front(), point);
    CHECK(preview.progress() == 0.5);
  }
  SECTION("changing entity class and clearing selection cannot leave a ghost")
  {
    preview.setProgress(1);
    REQUIRE(mdl::setEntityProperty(map, "classname", "func_wall"));
    CHECK_FALSE(preview.motion());
    CHECK_FALSE(preview.active());
    map.undoCommand();
    REQUIRE(preview.motion());
    CHECK_FALSE(preview.active());
    mdl::deselectAll(map);
    CHECK_FALSE(preview.motion());
    mdl::selectNodes(map, {entity});
    CHECK(preview.progress() == 0);
  }
  SECTION("non-GoldSrc games do not receive GoldSrc motion semantics")
  {
    auto otherFixture = MapDocumentFixture{};
    auto& other = otherFixture.create(mdl::QuakeFixtureConfig);
    auto otherPreview = EntityMotionPreview{other};
    addDoor(other.map());
    CHECK_FALSE(otherPreview.motion());
  }
  SECTION("deletion undo and load notifications clear transient state safely")
  {
    preview.setProgress(0.5);
    mdl::removeSelectedNodes(map);
    CHECK_FALSE(preview.motion());
    CHECK_FALSE(preview.active());
    map.undoCommand();
    REQUIRE(preview.motion());
    CHECK_FALSE(preview.active());
    preview.setProgress(1);
    document.documentWasLoadedNotifier();
    REQUIRE(preview.motion());
    CHECK_FALSE(preview.active());
  }
  SECTION("child selection resolves the same owner and mixed selection hides preview")
  {
    mdl::deselectAll(map);
    mdl::selectNodes(map, {entity->children().front()});
    REQUIRE(preview.motion());
    auto* worldBrush =
      addBox(map, mdl::parentForNodes(map), {{256, 0, 0}, {288, 32, 32}});
    mdl::selectNodes(map, {worldBrush});
    CHECK_FALSE(preview.motion());
  }
  SECTION("Play suspension and multiple documents do not share progress")
  {
    preview.setProgress(1);
    preview.setSuspended(true);
    preview.setProgress(1);
    CHECK_FALSE(preview.active());
    preview.setSuspended(false);
    CHECK_FALSE(preview.active());
    auto otherFixture = MapDocumentFixture{};
    auto& other = otherFixture.create(goldSrcConfig());
    auto otherPreview = EntityMotionPreview{other};
    addDoor(other.map());
    preview.setProgress(0.5);
    CHECK(otherPreview.progress() == 0);
    CHECK_FALSE(otherPreview.active());
  }
  SECTION("shared controls survive presentation changes and hide without a spacer")
  {
    const auto wasTable = pref(Preferences::EntityPropertiesTable);
    const auto restore =
      qScopeGuard([&] { setPref(Preferences::EntityPropertiesTable, wasTable); });
    auto editor = SelectionPropertyEditor{document, nullptr, &preview};
    auto* widget = editor.findChild<EntityMotionPreviewWidget*>();
    REQUIRE(widget);
    CHECK_FALSE(widget->isHidden());
    auto* slider = widget->findChild<QSlider*>();
    REQUIRE(slider);
    slider->setValue(500);
    CHECK(preview.progress() == 0.5);
    auto* presentation =
      editor.findChild<QComboBox*>("SelectionPropertyEditor_Presentation");
    REQUIRE(presentation);
    presentation->setCurrentIndex(1);
    QApplication::processEvents();
    CHECK(editor.findChild<EntityMotionPreviewWidget*>() == widget);
    CHECK(slider->value() == 500);
    presentation->setCurrentIndex(0);
    QApplication::processEvents();
    CHECK(editor.findChild<EntityMotionPreviewWidget*>() == widget);
    mdl::deselectAll(map);
    CHECK(widget->isHidden());
    CHECK_FALSE(preview.active());
  }
  SECTION("ghost renders at the preview position in 2D and 3D")
  {
    const auto ortho = GENERATE(false, true);
    auto view = MotionPreviewView{app.appController(), preview, ortho};
    view.resize(768, 480);
    view.show();
    REQUIRE(QTest::qWaitFor([&] { return view.frames > 0; }, 5000));
    REQUIRE(view.isValid());
    const auto before = view.grabFramebuffer();
    preview.setProgress(1);
    const auto after = view.grabFramebuffer();
    REQUIRE_FALSE(after.isNull());
    auto changed = 0;
    for (auto y = 0; y < after.height(); ++y)
      for (auto x = 0; x < after.width(); ++x)
        changed += before.pixel(x, y) != after.pixel(x, y) ? 1 : 0;
    CHECK(changed > 100);
    preview.reset();
    CHECK(view.grabFramebuffer() == before);
    const auto directory = qEnvironmentVariable("TB_RENDER_TEST_ARTIFACT_DIR");
    if (!directory.isEmpty())
    {
      REQUIRE(QDir{}.mkpath(directory));
      REQUIRE(after.save(directory + (ortho ? "/motion-2d.png" : "/motion-3d.png")));
    }
  }
}
} // namespace tb::ui
