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

#include <QTimer>
#include <QtTest/QTest>

#include "base/PreferenceManager.h"
#include "prefs/Preferences.h"
#include "ui/AppControllerFixture.h"
#include "ui/RenderView.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{
class TestRenderView : public RenderView
{
public:
  int frames = 0;

  explicit TestRenderView(AppController& appController)
    : RenderView{appController}
  {
  }

  void processEvent(const KeyEvent&) override {}
  void processEvent(const MouseEvent&) override {}
  void processEvent(const GestureEvent&) override {}
  void processEvent(const ScrollEvent&) override {}
  void processEvent(const CancelEvent&) override {}

private:
  bool shouldRenderFocusIndicator() const override { return false; }
  void renderContents(gl::Gl&) override { ++frames; }
};
} // namespace

TEST_CASE("RenderView")
{
  auto fixture = AppControllerFixture{};
  auto& prefs = PreferenceManager::instance();
  const auto previousLimit = pref(Preferences::RenderFpsLimit);
  setPref(Preferences::RenderFpsLimit, 5);

  {
    auto view = TestRenderView{fixture.appController()};
    view.resize(200, 200);
    view.show();
    REQUIRE(QTest::qWaitFor([&]() { return view.frames > 0; }, 2000));
    REQUIRE(view.isValid());

    SECTION("coalesces repaint requests and delivers the trailing frame")
    {
      auto elapsed = QElapsedTimer{};
      elapsed.start();
      const auto initialFrames = view.frames;
      for (auto i = 0; i < 20; ++i)
      {
        view.repaint();
      }
      CHECK(view.frames - initialFrames <= elapsed.elapsed() / 200);
      // Under parallel GPU load, the synchronous burst can span several frame
      // intervals. Leave an explicit asynchronous request to test trailing delivery.
      view.update();
      const auto framesAfterBurst = view.frames;
      REQUIRE(QTest::qWaitFor([&]() { return view.frames > framesAfterBurst; }, 1000));
      const auto finalFrames = view.frames;
      QTest::qWait(250);
      CHECK(view.frames == finalFrames);
    }

    SECTION("disabling the limit releases a pending frame")
    {
      view.repaint();
      const auto initialFrames = view.frames;
      setPref(Preferences::RenderFpsLimit, 0);
      REQUIRE(QTest::qWaitFor([&]() { return view.frames > initialFrames; }, 1000));
      const auto unlimitedFrames = view.frames;
      view.repaint();
      view.repaint();
      CHECK(view.frames >= unlimitedFrames + 2);
    }

    SECTION("continuous updates stay capped while the event loop remains responsive")
    {
      auto requests = 0;
      auto updateTimer = QTimer{};
      QObject::connect(&updateTimer, &QTimer::timeout, &view, [&]() {
        ++requests;
        view.update();
      });
      const auto initialFrames = view.frames;
      auto elapsed = QElapsedTimer{};
      elapsed.start();
      updateTimer.start(1);
      QTest::qWait(650);
      updateTimer.stop();
      CHECK(requests > 5);
      CHECK(view.frames >= initialFrames + 2);
      CHECK(view.frames - initialFrames <= (elapsed.elapsed() + 199) / 200);
    }

    SECTION("a hidden view stops deferred work and redraws when shown")
    {
      view.repaint();
      view.hide();
      const auto hiddenFrames = view.frames;
      QTest::qWait(250);
      CHECK(view.frames == hiddenFrames);
      view.show();
      REQUIRE(QTest::qWaitFor([&]() { return view.frames > hiddenFrames; }, 1000));
    }
  }

  prefs.set(Preferences::RenderFpsLimit, previousLimit);
}
} // namespace tb::ui
