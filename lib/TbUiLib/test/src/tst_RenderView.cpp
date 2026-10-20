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

#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest/QTest>

#include "base/PreferenceManager.h"
#include "gl/Camera.h"
#include "gl/GlManager.h"
#include "gl/ResourceManager.h"
#include "mdl/EnvironmentConfig.h"
#include "mdl/GameInfo.h"
#include "mdl/GameManager.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "prefs/Preferences.h"
#include "ui/AppControllerFixture.h"
#include "ui/MapDocument.h"
#include "ui/MapView3D.h"
#include "ui/MapWindow.h"
#include "ui/QPathUtils.h"
#include "ui/RenderView.h"
#include "ui/SystemPaths.h"

#include "kd/task_manager.h"

#include "vm/vec_io.h"

#include <thread>

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

TEST_CASE("RenderView profiling")
{
  auto directory = QTemporaryDir{};
  const auto path = directory.filePath("render.jsonl");
  const auto previous = qgetenv("TB_RENDER_PROFILE");
  const auto restore = qScopeGuard([&]() {
    if (previous.isNull())
    {
      qunsetenv("TB_RENDER_PROFILE");
    }
    else
    {
      qputenv("TB_RENDER_PROFILE", previous);
    }
  });
  qputenv("TB_RENDER_PROFILE", path.toUtf8());
  auto fixture = AppControllerFixture{};
  {
    auto view = TestRenderView{fixture.appController()};
    view.resize(200, 200);
    view.show();
    REQUIRE(QTest::qWaitFor([&]() { return view.frames > 0; }, 2000));
    for (auto i = 0; i < 5; ++i)
    {
      view.repaint();
      QTest::qWait(15);
    }
  }
  auto file = QFile{path};
  REQUIRE(file.open(QIODevice::ReadOnly));
  auto sawMetadata = false;
  auto sawCpu = false;
  auto gpuSupported = false;
  auto sawGpu = false;
  while (!file.atEnd())
  {
    const auto object = QJsonDocument::fromJson(file.readLine()).object();
    REQUIRE_FALSE(object.isEmpty());
    if (object["kind"] == "metadata")
    {
      sawMetadata = true;
      gpuSupported = object["gpu_supported"].toBool();
    }
    if (object["kind"] == "window")
    {
      const auto timings = object["timings_ms"].toObject();
      sawCpu |= timings["cpu.frame"].toObject()["samples"].toInt() >= 5;
      sawGpu |= timings["gpu.frame"].toObject()["samples"].toInt() > 0;
      CHECK(timings["cpu.frame"].toObject()["max"].toDouble() >= 0.0);
    }
  }
  CHECK(sawMetadata);
  CHECK(sawCpu);
  CHECK((!gpuSupported || sawGpu));
}

// Explicitly invoked by scripts/profile-render.ps1; external maps are never CI fixtures.
TEST_CASE("RenderView profile external map", "[.][render-profile]")
{
  const auto mapPath = qEnvironmentVariable("TB_RENDER_PROFILE_MAP");
  REQUIRE_FALSE(mapPath.isEmpty());
  REQUIRE(qEnvironmentVariableIsSet("TB_RENDER_PROFILE"));
  setPref(Preferences::DefaultPluginPaths, "");
  setPref(Preferences::PythonPluginDirectories, "");
  setPref(Preferences::RenderFpsLimit, 0);
  setPref(
    Preferences::ShowEntityClassnames,
    !qEnvironmentVariableIsSet("TB_RENDER_PROFILE_HIDE_LABELS"));
  setPref(
    Preferences::ShowEdges, !qEnvironmentVariableIsSet("TB_RENDER_PROFILE_HIDE_EDGES"));
  auto state = QTemporaryDir{};
  REQUIRE(state.isValid());
  const auto statePath = pathFromQString(state.path());
  auto manager = mdl::initializeGameManager(
                   {SystemPaths::appDirectory() / "fixture" / "games"}, statePath)
                 | kdl::transform([](auto value, const auto&) { return value; })
                 | kdl::value();
  auto app = std::make_unique<AppController>(
    std::make_unique<kdl::task_manager>(std::thread::hardware_concurrency()),
    std::make_unique<mdl::EnvironmentConfig>(mdl::EnvironmentConfig{
      .appFolderPath = SystemPaths::appDirectory(),
      .userDataFolderPath = statePath,
      .tempFolderPath = statePath,
      .defaultAssetFolderPaths = {}}),
    std::make_unique<mdl::GameManager>(std::move(manager)),
    AppControllerOptions{false, false, true});
  const auto gameName = qEnvironmentVariable("TB_RENDER_PROFILE_GAME", "Half-Life");
  auto* game = app->gameManager().gameInfo(gameName.toStdString());
  REQUIRE(game != nullptr);
  const auto gamePath = qEnvironmentVariable("TB_RENDER_PROFILE_GAME_PATH");
  if (!gamePath.isEmpty())
  {
    setPref(game->gamePathPreference, pathFromQString(gamePath));
  }
  auto document = MapDocument::loadDocument(
                    app->environmentConfig(),
                    *game,
                    mdl::MapFormat::Valve,
                    MapDocument::DefaultWorldBounds,
                    pathFromQString(mapPath),
                    app->taskManager(),
                    app->glManager().resourceManager())
                  | kdl::value();
  auto window = MapWindow{*app, std::move(document)};
  window.resize(1400, 900);
  window.show();
  auto* view = window.findChild<MapView3D*>();
  REQUIRE(view != nullptr);
  REQUIRE(QTest::qWaitFor([&]() { return view->isValid(); }, 5000));
  auto& camera = static_cast<MapViewBase&>(*view).camera();
  if (
    const auto position = vm::parse<float, 3>(
      qEnvironmentVariable("TB_RENDER_PROFILE_POSITION").toStdString()))
  {
    camera.moveTo(*position);
  }
  if (
    const auto direction = vm::parse<float, 3>(
      qEnvironmentVariable("TB_RENDER_PROFILE_DIRECTION").toStdString()))
  {
    camera.lookAt(camera.position() + *direction, vm::vec3f{0, 0, 1});
  }
  QObject::connect(
    view, &QOpenGLWidget::frameSwapped, view, [view]() { view->update(); });
  auto loop = QEventLoop{};
  const auto seconds =
    std::clamp(qEnvironmentVariableIntValue("TB_RENDER_PROFILE_SECONDS"), 2, 300);
  QTimer::singleShot(seconds * 1000, &loop, &QEventLoop::quit);
  view->update();
  loop.exec();
  CHECK(view->isValid());
  CHECK_FALSE(window.document().map().modified());
  CHECK(view->grabFramebuffer().save(qEnvironmentVariable("TB_RENDER_PROFILE") + ".png"));
  CHECK(QFile::copy(
    pathAsQString(SystemPaths::logFilePath()),
    qEnvironmentVariable("TB_RENDER_PROFILE") + ".app.log"));
}
} // namespace tb::ui
