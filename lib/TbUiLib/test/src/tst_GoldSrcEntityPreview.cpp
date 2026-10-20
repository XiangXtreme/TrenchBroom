/*
 Copyright (C) 2026 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include <QDir>
#include <QImage>
#include <QScopeGuard>
#include <QtTest/QTest>

#include "base/PreferenceManager.h"
#include "gl/ActiveShader.h"
#include "gl/GlInterface.h"
#include "gl/Material.h"
#include "gl/MaterialIndexRangeRenderer.h"
#include "gl/OrthographicCamera.h"
#include "gl/Shaders.h"
#include "gl/Texture.h"
#include "gl/TextureBuffer.h"
#include "gl/VboManager.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/EntityNode.h"
#include "mdl/MapFormat.h"
#include "prefs/Preferences.h"
#include "render/BrushRenderer.h"
#include "render/GoldSrcEntityRenderer.h"
#include "render/RenderBatch.h"
#include "render/RenderContext.h"
#include "ui/AppControllerFixture.h"
#include "ui/GlFunctions.h"
#include "ui/GlQt.h"
#include "ui/RenderView.h"

#include "kd/result.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{
class GoldSrcPreviewView : public RenderView
{
private:
  std::shared_ptr<gl::TextureResource> m_texture;
  std::unique_ptr<gl::Material> m_material;
  mdl::EntityNode m_entity{
    mdl::Entity{{{"classname", "func_conveyor"}, {"speed", "64"}}}};
  mdl::BrushNode* m_brush = nullptr;
  render::BrushRenderer m_brushRenderer;
  std::unique_ptr<gl::MaterialIndexRangeRenderer> m_spriteRenderer;

public:
  bool sprite = false;
  bool animate = false;
  bool preview = true;
  double time = 0.0;
  int frames = 0;

  explicit GoldSrcPreviewView(AppController& controller)
    : RenderView{controller}
  {
    auto pixels = gl::TextureBuffer{128 * 128 * 4};
    for (size_t y = 0; y < 128; ++y)
    {
      for (size_t x = 0; x < 128; ++x)
      {
        const auto index = (y * 128 + x) * 4;
        pixels.data()[index] = x < 64 ? 255 : 0;
        pixels.data()[index + 1] = 0;
        pixels.data()[index + 2] = 0;
        pixels.data()[index + 3] = 255;
      }
    }
    m_texture = gl::createTextureResource(gl::Texture{
      128,
      128,
      RgbaF{1.0f, 0.0f, 0.0f, 1.0f},
      GL_RGBA,
      gl::NoEmbeddedDefaults{},
      std::move(pixels)});
    m_material = std::make_unique<gl::Material>("SCROLLarrows", m_texture);
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{4096.0}};
    auto brush =
      builder.createCuboid(vm::bbox3d{{-64, -8, -64}, {64, 8, 64}}, "SCROLLarrows")
      | kdl::value();
    for (auto& face : brush.faces())
    {
      face.setMaterial(m_material.get());
    }
    m_brush = new mdl::BrushNode{std::move(brush)};
    m_entity.addChild(m_brush);
    m_brushRenderer.addBrush(*m_brush);
    using Vertex = gl::VertexTypes::P3Uv2::Vertex;
    m_spriteRenderer = std::make_unique<gl::MaterialIndexRangeRenderer>(
      gl::VertexArray::move(std::vector{
        Vertex{{-64, 0, -64}, {0, 0}},
        Vertex{{64, 0, -64}, {1, 0}},
        Vertex{{64, 0, 64}, {1, 1}},
        Vertex{{-64, 0, 64}, {0, 1}}}),
      m_material.get(),
      gl::IndexRangeMap{gl::PrimType::Quads, 0, 4});
  }

  ~GoldSrcPreviewView() override
  {
    makeCurrent();
    auto gl = GlQt{getGlFunctions("GoldSrcPreviewView", context())};
    m_brushRenderer.clear();
    m_spriteRenderer.reset();
    vboManager().destroyPendingVbos(gl);
    m_texture->get()->drop(gl);
  }

  void setMode(
    const int mode, const int amount = 255, const std::string& color = "255 255 255")
  {
    auto entity = m_entity.entity();
    entity.addOrUpdateProperty("rendermode", std::to_string(mode));
    entity.addOrUpdateProperty("renderamt", std::to_string(amount));
    entity.addOrUpdateProperty("rendercolor", color);
    m_entity.setEntity(std::move(entity));
    m_brushRenderer.invalidateBrush(*m_brush);
  }

  void processEvent(const KeyEvent&) override {}
  void processEvent(const MouseEvent&) override {}
  void processEvent(const GestureEvent&) override {}
  void processEvent(const ScrollEvent&) override {}
  void processEvent(const CancelEvent&) override {}

private:
  bool shouldRenderFocusIndicator() const override { return false; }
  Color getBackgroundColor() override { return RgbaF{0.1f, 0.5f, 0.2f, 1.0f}; }
  void renderContents(gl::Gl& gl) override
  {
    ++frames;
    m_texture->uploadSync(gl);
    gl.viewport(
      0, 0, int(width() * devicePixelRatioF()), int(height() * devicePixelRatioF()));
    gl.enable(GL_DEPTH_TEST);
    gl.enable(GL_BLEND);
    gl.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl.depthMask(GL_TRUE);
    gl.disable(GL_MULTISAMPLE);
    auto camera = gl::OrthographicCamera{
      1, 1024, {0, 0, width(), height()}, {0, -256, 0}, {0, 1, 0}, {0, 0, 1}};
    auto context = render::RenderContext{
      gl, render::RenderMode::Render3D, camera, fontManager(), shaderManager()};
    context.setGoldSrcRendering(preview);
    context.setAnimationTime(time);
    context.setShowEdges(false);
    context.setShowGrid(false);
    context.setShadeFaces(false);
    context.setFilterMode(GL_NEAREST, GL_NEAREST);
    if (sprite)
    {
      gl.disable(GL_CULL_FACE);
      m_spriteRenderer->prepare(gl, vboManager());
      auto shader = gl::ActiveShader{gl, shaderManager(), gl::Shaders::EntityModelShader};
      shader.set("Brightness", 1.0f);
      shader.set("Material", 0);
      shader.set("ApplyTinting", false);
      shader.set("GrayScale", false);
      shader.set("ShowSoftMapBounds", false);
      shader.set("Orientation", 3);
      shader.set("ModelMatrix", vm::mat4x4f::identity());
      shader.set("ViewMatrix", camera.viewMatrix());
      auto func = render::GoldSrcEntityMaterialRenderFunc{
        shader,
        GL_NEAREST,
        GL_NEAREST,
        render::goldSrcEntityRenderSettings(&m_entity.entity())};
      m_spriteRenderer->render(gl, shader.program(), func);
      shader.set("EntityRenderMode", 0);
    }
    else
    {
      gl.enable(GL_CULL_FACE);
      gl.frontFace(GL_CW);
      auto batch = render::RenderBatch{vboManager()};
      m_brushRenderer.render(context, batch);
      batch.render(context);
    }
    animate = context.animationRequested();
  }
};

QColor centerPixel(const QImage& image, const int x = 160)
{
  return image.pixelColor(x * image.width() / 320, image.height() / 2);
}

void savePreview(const QImage& image, const QString& name)
{
  const auto directory = qEnvironmentVariable("TB_RENDER_TEST_ARTIFACT_DIR");
  if (!directory.isEmpty())
  {
    REQUIRE(QDir{}.mkpath(directory));
    REQUIRE(image.save(QDir{directory}.filePath(name + ".png")));
  }
}
} // namespace

TEST_CASE("GoldSrcEntityPreview real GL")
{
  auto fixture = AppControllerFixture{};
  const auto previousLimit = pref(Preferences::RenderFpsLimit);
  const auto previousBrightness = pref(Preferences::Brightness);
  const auto restorePreferences = qScopeGuard([=]() {
    setPref(Preferences::RenderFpsLimit, previousLimit);
    setPref(Preferences::Brightness, previousBrightness);
  });
  setPref(Preferences::RenderFpsLimit, 0);
  setPref(Preferences::Brightness, 1.0f);
  auto view = GoldSrcPreviewView{fixture.appController()};
  view.resize(320, 240);
  view.show();
  REQUIRE(QTest::qWaitFor([&]() { return view.frames > 0; }, 3000));
  REQUIRE(view.isValid());

  SECTION("brush transparency and render color update on the GPU")
  {
    view.setMode(0);
    const auto opaque = view.grabFramebuffer();
    const auto opaquePixel = centerPixel(opaque, 192);
    REQUIRE(opaquePixel.red() > 240);
    REQUIRE(opaquePixel.green() < 5);
    view.setMode(2, 100);
    const auto glass = view.grabFramebuffer();
    const auto glassPixel = centerPixel(glass, 192);
    CHECK(glassPixel.red() > 105);
    CHECK(glassPixel.red() < 125);
    CHECK(glassPixel.green() > 70);
    CHECK(glassPixel.green() < 85);
    savePreview(glass, "glass-100");
    view.setMode(1, 128, "0 0 255");
    const auto color = view.grabFramebuffer();
    CHECK(centerPixel(color).blue() > 145);
    CHECK(centerPixel(color).green() < 70);
    savePreview(color, "color-128");
    view.setMode(5, 160);
    const auto additive = view.grabFramebuffer();
    CHECK(centerPixel(additive, 128).green() >= 126);
    CHECK(centerPixel(additive, 192).red() > 175);
    CHECK(centerPixel(additive, 192).red() < 195);
    savePreview(additive, "brush-additive-160");
    view.setMode(0);
    CHECK(centerPixel(view.grabFramebuffer(), 192) == opaquePixel);
  }

  SECTION("additive sprites preserve the background under black texels")
  {
    view.sprite = true;
    view.setMode(0);
    const auto normal = view.grabFramebuffer();
    CHECK(centerPixel(normal, 192).green() < 5);
    view.setMode(5, 160);
    const auto additive = view.grabFramebuffer();
    const auto blackTexel = centerPixel(additive, 192);
    CHECK(blackTexel.red() >= 24);
    CHECK(blackTexel.red() <= 27);
    CHECK(blackTexel.green() >= 126);
    CHECK(blackTexel.green() <= 129);
    CHECK(centerPixel(additive, 128).red() > 175);
    CHECK(centerPixel(additive, 128).red() < 195);
    savePreview(additive, "sprite-additive-160");
  }

  SECTION("conveyor time changes rendered pixels and requests another frame")
  {
    view.setMode(0);
    view.time = 0.0;
    const auto start = view.grabFramebuffer();
    CHECK(view.animate);
    view.time = 0.5;
    const auto later = view.grabFramebuffer();
    CHECK(start != later);
    savePreview(start, "conveyor-t0");
    savePreview(later, "conveyor-t05");
    view.preview = false;
    const auto disabled = view.grabFramebuffer();
    CHECK_FALSE(view.animate);
    view.time = 1.0;
    CHECK(disabled == view.grabFramebuffer());
  }
}
} // namespace tb::ui
