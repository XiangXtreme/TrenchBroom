/*
 Copyright (C) 2026 Kristian Duske

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

#include "gl/FontManager.h"
#include "gl/OrthographicCamera.h"
#include "gl/ShaderManager.h"
#include "gl/TestGl.h"
#include "gl/VboManager.h"
#include "render/RenderBatch.h"
#include "render/RenderContext.h"
#include "render/RenderTiming.h"
#include "render/Renderable.h"

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace tb::render
{

namespace
{

// Records "prepared"/"rendered" into a shared log (rather than separate booleans) so
// each SECTION can assert inline that prepare() ran before render(), instead of that
// invariant being checked once, centrally, inside the class itself.

class RecordingRenderable : public Renderable
{
public:
  std::vector<std::string>* order;
  bool* destroyed;

  RecordingRenderable(std::vector<std::string>& orderLog, bool& destroyedFlag)
    : order{&orderLog}
    , destroyed{&destroyedFlag}
  {
  }

  ~RecordingRenderable() override { *destroyed = true; }

  void render(RenderContext&) override { order->emplace_back("rendered"); }
};

class RecordingDirectRenderable : public DirectRenderable
{
public:
  std::vector<std::string>* order;
  bool* destroyed;

  RecordingDirectRenderable(std::vector<std::string>& orderLog, bool& destroyedFlag)
    : order{&orderLog}
    , destroyed{&destroyedFlag}
  {
  }

  ~RecordingDirectRenderable() override { *destroyed = true; }

  void prepare(gl::Gl&, gl::VboManager&) override { order->emplace_back("prepared"); }
  void render(RenderContext&) override { order->emplace_back("rendered"); }
};

class RecordingIndexedRenderable : public IndexedRenderable
{
public:
  std::vector<std::string>* order;
  bool* destroyed;

  RecordingIndexedRenderable(std::vector<std::string>& orderLog, bool& destroyedFlag)
    : order{&orderLog}
    , destroyed{&destroyedFlag}
  {
  }

  ~RecordingIndexedRenderable() override { *destroyed = true; }

  void prepare(gl::Gl&, gl::VboManager&) override { order->emplace_back("prepared"); }
  void render(RenderContext&) override { order->emplace_back("rendered"); }
};

} // namespace

TEST_CASE("RenderBatch")
{
  auto testGl = gl::TestGl{};
  auto vboManager = gl::VboManager{};
  auto camera = gl::OrthographicCamera{};
  auto fontManager = gl::FontManager{[](const auto& path) { return path; }};
  auto shaderManager = gl::ShaderManager{[](const auto& path) { return path; }};
  auto renderContext =
    RenderContext{testGl, RenderMode::Render3D, camera, fontManager, shaderManager};

  SECTION("add")
  {
    SECTION("Renderable* renders it but does not take ownership")
    {
      auto order = std::vector<std::string>{};
      auto destroyed = false;
      auto* renderable = new RecordingRenderable{order, destroyed};

      {
        auto batch = RenderBatch{vboManager};
        batch.add(renderable);
        batch.render(renderContext);
        CHECK(order == std::vector<std::string>{"rendered"});
      }

      CHECK(!destroyed);
      delete renderable;
      CHECK(destroyed);
    }

    SECTION("DirectRenderable* prepares and renders it but does not take ownership")
    {
      auto order = std::vector<std::string>{};
      auto destroyed = false;
      auto* renderable = new RecordingDirectRenderable{order, destroyed};

      {
        auto batch = RenderBatch{vboManager};
        batch.add(renderable);
        batch.render(renderContext);
        CHECK(order == std::vector<std::string>{"prepared", "rendered"});
      }

      CHECK(!destroyed);
      delete renderable;
      CHECK(destroyed);
    }

    SECTION(
      "IndexedRenderable* prepares and renders it through a wrapper, but does "
      "not take ownership of the original")
    {
      auto order = std::vector<std::string>{};
      auto destroyed = false;
      auto* renderable = new RecordingIndexedRenderable{order, destroyed};

      {
        auto batch = RenderBatch{vboManager};
        batch.add(renderable);
        batch.render(renderContext);
        CHECK(order == std::vector<std::string>{"prepared", "rendered"});
      }

      CHECK(!destroyed);
      delete renderable;
      CHECK(destroyed);
    }
  }

  SECTION("addOneShot")
  {
    SECTION("Renderable* renders it and takes ownership")
    {
      auto order = std::vector<std::string>{};
      auto destroyed = false;
      auto* renderable = new RecordingRenderable{order, destroyed};

      {
        auto batch = RenderBatch{vboManager};
        batch.addOneShot(renderable);
        batch.render(renderContext);
        CHECK(order == std::vector<std::string>{"rendered"});
        CHECK(!destroyed);
      }

      CHECK(destroyed); // the batch deleted it on destruction
    }

    SECTION("DirectRenderable* prepares, renders and takes ownership")
    {
      auto order = std::vector<std::string>{};
      auto destroyed = false;
      auto* renderable = new RecordingDirectRenderable{order, destroyed};

      {
        auto batch = RenderBatch{vboManager};
        batch.addOneShot(renderable);
        batch.render(renderContext);
        CHECK(order == std::vector<std::string>{"prepared", "rendered"});
        CHECK(!destroyed);
      }

      CHECK(destroyed);
    }

    SECTION("IndexedRenderable* prepares, renders and takes ownership of the original")
    {
      auto order = std::vector<std::string>{};
      auto destroyed = false;
      auto* renderable = new RecordingIndexedRenderable{order, destroyed};

      {
        auto batch = RenderBatch{vboManager};
        batch.addOneShot(renderable);
        batch.render(renderContext);
        CHECK(order == std::vector<std::string>{"prepared", "rendered"});
        CHECK(!destroyed);
      }

      CHECK(destroyed);
    }
  }

  SECTION("render renders every added renderable in the order they were added")
  {
    auto order = std::vector<int>{};

    class OrderRecordingRenderable : public Renderable
    {
    public:
      std::vector<int>& order;
      int id;

      OrderRecordingRenderable(std::vector<int>& orderVec, const int recordedId)
        : order{orderVec}
        , id{recordedId}
      {
      }

      void render(RenderContext&) override { order.push_back(id); }
    };

    auto first = OrderRecordingRenderable{order, 1};
    auto second = OrderRecordingRenderable{order, 2};

    auto batch = RenderBatch{vboManager};
    batch.add(&first);
    batch.add(&second);
    batch.render(renderContext);

    CHECK(order == std::vector<int>{1, 2});
  }
}

TEST_CASE("RenderTiming")
{
  auto outer = RenderTiming{};
  auto inner = RenderTiming{};
  REQUIRE(RenderTiming::current == nullptr);
  {
    const auto disabled = RenderTimingScope{"disabled"};
    RenderTiming::count("disabled", 1);
  }
  CHECK(outer.milliseconds.empty());
  CHECK(outer.counters.empty());
  {
    const auto capture = RenderTiming::Capture{outer};
    const auto total = RenderTimingScope{"total"};
    RenderTiming::count("items", 2);
    {
      const auto nestedCapture = RenderTiming::Capture{inner};
      const auto nested = RenderTimingScope{"nested"};
      RenderTiming::count("items", 5);
    }
    CHECK(RenderTiming::current == &outer);
    RenderTiming::count("items", 3);
  }
  CHECK(RenderTiming::current == nullptr);
  CHECK(outer.counters.at("items") == 5);
  CHECK(inner.counters.at("items") == 5);
  CHECK(outer.milliseconds.contains("total"));
  CHECK_FALSE(outer.milliseconds.contains("nested"));
  CHECK(inner.milliseconds.contains("nested"));
  CHECK(outer.milliseconds.at("total") >= inner.milliseconds.at("nested"));

  SECTION("GL workload scopes measure deltas and accumulate repeated passes")
  {
    {
      const auto disabled = RenderGlScope{"disabled"};
      RenderTiming::countGl(GlCounter::DrawCalls);
    }
    CHECK_FALSE(outer.counters.contains("disabled.draw_calls"));
    const auto capture = RenderTiming::Capture{outer};
    {
      const auto frame = RenderGlScope{"gl.frame"};
      RenderTiming::countGl(GlCounter::DrawCalls, 2);
      for (size_t i = 0; i < 2; ++i)
      {
        const auto models = RenderGlScope{"gl.models"};
        RenderTiming::countGl(GlCounter::DrawCalls, 3);
        RenderTiming::countGl(GlCounter::DrawRanges, 7);
        RenderTiming::countGl(GlCounter::SubmittedVertices, 42);
      }
      {
        const auto nestedCapture = RenderTiming::Capture{inner};
        const auto nested = RenderGlScope{"gl.other"};
        RenderTiming::countGl(GlCounter::DrawCalls, 100);
      }
    }
    CHECK(outer.counters.at("gl.frame.draw_calls") == 8);
    CHECK(outer.counters.at("gl.models.draw_calls") == 6);
    CHECK(outer.counters.at("gl.models.draw_ranges") == 14);
    CHECK(outer.counters.at("gl.models.submitted_vertices") == 84);
    CHECK(outer.counters.at("gl.frame.uniforms") == 0);
    CHECK(inner.counters.at("gl.other.draw_calls") == 100);
  }

  SECTION("GL call wall timing is opt-in and follows the captured frame")
  {
    const auto index = static_cast<size_t>(GlCounter::DrawCalls);
    {
      const auto disabled = RenderGlCallScope{GlCounter::DrawCalls};
    }
    {
      const auto capture = RenderTiming::Capture{outer};
      const auto pass = RenderGlScope{"gl.models"};
      const auto disabled = RenderGlCallScope{GlCounter::DrawCalls};
    }
    CHECK(outer.glMilliseconds[index] == 0.0);
    CHECK_FALSE(outer.milliseconds.contains("cpu.gl.models.draw_calls"));
    outer.timeGlCalls = true;
    inner.timeGlCalls = true;
    {
      const auto capture = RenderTiming::Capture{outer};
      const auto frame = RenderGlScope{"gl.frame"};
      // Deterministic deltas verify aggregation without clock-resolution assumptions.
      outer.glMilliseconds[index] += 1.0;
      {
        const auto pass = RenderGlScope{"gl.models"};
        outer.glMilliseconds[index] += 2.0;
      }
      {
        const auto nestedCapture = RenderTiming::Capture{inner};
        const auto pass = RenderGlScope{"gl.other"};
        inner.glMilliseconds[index] += 4.0;
        const auto call = RenderGlCallScope{GlCounter::Uniforms};
      }
    }
    CHECK(outer.milliseconds.at("cpu.gl.frame.draw_calls") == 3.0);
    CHECK(outer.milliseconds.at("cpu.gl.models.draw_calls") == 2.0);
    CHECK(inner.milliseconds.at("cpu.gl.other.draw_calls") == 4.0);
    CHECK(inner.glMilliseconds[static_cast<size_t>(GlCounter::Uniforms)] >= 0.0);
    CHECK(outer.glMilliseconds[static_cast<size_t>(GlCounter::Uniforms)] == 0.0);
    CHECK(RenderTiming::current == nullptr);
  }
}

} // namespace tb::render
