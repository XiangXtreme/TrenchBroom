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

#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QScopeGuard>
#include <QSet>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest/QTest>

#include "base/PreferenceManager.h"
#include "gl/Camera.h"
#include "gl/GlInterface.h"
#include "gl/GlManager.h"
#include "gl/ResourceManager.h"
#include "mdl/EditorContext.h"
#include "mdl/EnvironmentConfig.h"
#include "mdl/GameInfo.h"
#include "mdl/GameManager.h"
#include "mdl/Grid.h"
#include "mdl/Hit.h"
#include "mdl/HitFilter.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "mdl/Map_Selection.h"
#include "mdl/ModelUtils.h"
#include "prefs/Preferences.h"
#include "ui/AppControllerFixture.h"
#include "ui/MapDocument.h"
#include "ui/MapView2D.h"
#include "ui/MapView3D.h"
#include "ui/MapViewLayout.h"
#include "ui/MapViewToolBox.h"
#include "ui/MapWindow.h"
#include "ui/QPathUtils.h"
#include "ui/RenderView.h"
#include "ui/SystemPaths.h"

#include "kd/task_manager.h"

#include "vm/vec_io.h"

#include <algorithm>
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
  void renderContents(gl::Gl& gl) override
  {
    gl.bindBuffer(GL_ARRAY_BUFFER, 0);
    ++frames;
  }
};

// Keep external keyboard/mouse activity out of scripted captures. Synthetic
// key events still traverse the normal view/controller input path.
class ProfileInputFilter : public QObject
{
  bool eventFilter(QObject* receiver, QEvent* event) override
  {
    // Automated runs must keep simulating even if another desktop window activates.
    return (event->type() == QEvent::FocusOut && qobject_cast<MapViewBase*>(receiver))
           || (event->spontaneous() && dynamic_cast<QInputEvent*>(event) != nullptr);
  }
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
      // Parallel GL tests can delay frame delivery beyond a fixed wall-clock wait.
      // Wait for progress, then check the cap against the actual elapsed time.
      const auto progressed = QTest::qWaitFor(
        [&]() { return requests > 5 && view.frames >= initialFrames + 2; }, 5000);
      updateTimer.stop();
      CHECK(progressed);
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
  const auto previousGlTiming = qgetenv("TB_RENDER_PROFILE_GL_TIMING");
  const auto restore = qScopeGuard([&]() {
    if (previous.isNull())
    {
      qunsetenv("TB_RENDER_PROFILE");
    }
    else
    {
      qputenv("TB_RENDER_PROFILE", previous);
    }
    if (previousGlTiming.isNull())
    {
      qunsetenv("TB_RENDER_PROFILE_GL_TIMING");
    }
    else
    {
      qputenv("TB_RENDER_PROFILE_GL_TIMING", previousGlTiming);
    }
  });
  qputenv("TB_RENDER_PROFILE", path.toUtf8());
  auto glTiming = false;
  auto multipleViews = false;
  SECTION("default GL workload counters") {}
  SECTION("opt-in GL call wall timing")
  {
    glTiming = true;
  }
  SECTION("multiple views append complete records")
  {
    multipleViews = true;
  }
  qputenv("TB_RENDER_PROFILE_GL_TIMING", glTiming ? "1" : "0");
  auto fixture = AppControllerFixture{};
  {
    auto view = TestRenderView{fixture.appController()};
    auto other =
      multipleViews ? std::make_unique<TestRenderView>(fixture.appController()) : nullptr;
    if (other)
    {
      other->resize(200, 200);
      other->show();
      REQUIRE(QTest::qWaitFor([&]() { return other->frames > 0; }, 2000));
    }
    view.resize(200, 200);
    view.show();
    REQUIRE(QTest::qWaitFor([&]() { return view.frames > 0; }, 2000));
    for (auto i = 0; i < 5; ++i)
    {
      view.repaint();
      if (other)
      {
        other->repaint();
      }
      QTest::qWait(15);
    }
  }
  auto file = QFile{path};
  REQUIRE(file.open(QIODevice::ReadOnly));
  auto sawMetadata = false;
  auto cpuSamples = qint64{0};
  auto paintSamples = qint64{0};
  auto paintToRenderSamples = qint64{0};
  auto renderToPaintEndSamples = qint64{0};
  auto gpuSupported = false;
  auto sawGpu = false;
  auto metadataViews = QSet<QString>{};
  auto reportedViews = QSet<QString>{};
  while (!file.atEnd())
  {
    const auto object = QJsonDocument::fromJson(file.readLine()).object();
    REQUIRE_FALSE(object.isEmpty());
    if (object["kind"] == "metadata")
    {
      sawMetadata = true;
      metadataViews.insert(object["view_id"].toString());
      gpuSupported = object["gpu_supported"].toBool();
      CHECK(object["gl_call_timing"].toBool() == glTiming);
      for (const auto* field :
           {"swap_interval_requested",
            "default_swap_interval_requested",
            "context_samples",
            "framebuffer_samples",
            "surface_depth_bits",
            "default_samples_requested"})
      {
        REQUIRE(object[field].isDouble());
      }
      CHECK(object["framebuffer_samples"].toInt() >= 0);
    }
    if (object["kind"] == "window")
    {
      reportedViews.insert(object["view_id"].toString());
      const auto timings = object["timings_ms"].toObject();
      // Parallel test load can spread the requested repaints across reporting windows.
      cpuSamples += timings["cpu.frame"].toObject()["samples"].toInteger();
      paintSamples += timings["cpu.paint_event"].toObject()["samples"].toInteger();
      paintToRenderSamples +=
        timings["present.paint_to_render"].toObject()["samples"].toInteger();
      renderToPaintEndSamples +=
        timings["present.render_to_paint_end"].toObject()["samples"].toInteger();
      sawGpu |= timings["gpu.frame"].toObject()["samples"].toInt() > 0;
      CHECK(timings["cpu.frame"].toObject()["max"].toDouble() >= 0.0);
      const auto counters = object["counters"].toObject();
      REQUIRE(counters.contains("gl.frame.buffer_binds"));
      CHECK(
        counters["gl.frame.buffer_binds"].toInteger() == object["frames"].toInteger());
      CHECK(counters["gl.frame.draw_calls"].toInteger() == 0);
      if (!glTiming)
      {
        CHECK_FALSE(timings.contains("cpu.gl.frame.buffer_binds"));
      }
      else
      {
        CHECK(timings.contains("cpu.gl.frame.buffer_binds"));
      }
    }
  }
  CHECK(sawMetadata);
  CHECK(metadataViews.size() == (multipleViews ? 2 : 1));
  CHECK(reportedViews == metadataViews);
  CHECK(cpuSamples >= 5);
  // Initialization may render outside paintEvent, and a final reporting window
  // may close before endPaint. Verify real boundaries without equating counts.
  CHECK(paintSamples > 0);
  CHECK(paintToRenderSamples > 0);
  CHECK(renderToPaintEndSamples > 0);
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
  const auto fourPanes = qEnvironmentVariableIsSet("TB_RENDER_PROFILE_FOUR_PANES");
  setPref(
    Preferences::MapViewLayout,
    int(fourPanes ? MapViewLayout::FourPanes : MapViewLayout::OnePane));
  // Native raw motion bypasses QObject event filters. Zero sensitivity keeps
  // that backend active without letting physical mouse motion turn the camera.
  setPref(Preferences::PlayModeMouseSensitivity, 0.0f);
  setPref(
    Preferences::ShowPointEntityModels,
    !qEnvironmentVariableIsSet("TB_RENDER_PROFILE_HIDE_MODELS"));
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
  auto loadTimer = QElapsedTimer{};
  loadTimer.start();
  const auto loadTiming = qEnvironmentVariableIsSet("TB_RENDER_PROFILE_LOAD_TIMING");
  auto notificationTimer = QElapsedTimer{};
  auto notificationNs = qint64{0};
  auto notificationMaxNs = qint64{0};
  auto notificationBatches = 0;
  auto& resourceManager = app->glManager().resourceManager();
  const auto beginNotifications =
    resourceManager.resourcesWereProcessedNotifier.connect([&](const auto&) {
      if (loadTiming)
      {
        notificationTimer.start();
      }
    });
  auto document = MapDocument::loadDocument(
                    app->environmentConfig(),
                    *game,
                    mdl::MapFormat::Valve,
                    MapDocument::DefaultWorldBounds,
                    pathFromQString(mapPath),
                    app->taskManager(),
                    app->glManager().resourceManager())
                  | kdl::value();
  const auto documentLoadedMs = loadTimer.elapsed();
  auto inputFilter = ProfileInputFilter{};
  QApplication::instance()->installEventFilter(&inputFilter);
  const auto removeInputFilter =
    qScopeGuard([&]() { QApplication::instance()->removeEventFilter(&inputFilter); });
  auto window = MapWindow{*app, std::move(document)};
  const auto windowConstructedMs = loadTimer.elapsed();
  const auto endNotifications =
    resourceManager.resourcesWereProcessedNotifier.connect([&](const auto&) {
      if (loadTiming && notificationTimer.isValid())
      {
        const auto ns = notificationTimer.nsecsElapsed();
        notificationNs += ns;
        notificationMaxNs = std::max(notificationMaxNs, ns);
        ++notificationBatches;
      }
    });
  window.resize(1400, 900);
  auto* view = window.findChild<MapView3D*>();
  REQUIRE(view != nullptr);
  auto* dragView = fourPanes ? static_cast<MapViewBase*>(window.findChild<MapView2D*>())
                             : static_cast<MapViewBase*>(view);
  REQUIRE(dragView);
  qint64 firstFrameMs = -1;
  auto presentedFrames = 0;
  const auto frameConnection =
    QObject::connect(view, &QOpenGLWidget::frameSwapped, view, [&]() {
      ++presentedFrames;
      if (firstFrameMs < 0)
      {
        firstFrameMs = loadTimer.elapsed();
      }
    });
  const auto disconnectFrames =
    qScopeGuard([&]() { QObject::disconnect(frameConnection); });
  window.show();
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
  // Loading can request more resources on a subsequent frame. Require two
  // presented frames and a quiet interval; failed resources also settle, so the
  // accompanying asset warnings remain part of interpreting this measurement.
  const auto waitForResources = [&]() {
    const auto initialFrames = presentedFrames;
    auto quietTimer = QElapsedTimer{};
    quietTimer.start();
    auto& resources = app->glManager().resourceManager();
    auto resourceCount = resources.resources().size();
    REQUIRE(QTest::qWaitFor(
      [&]() {
        view->update();
        const auto count = resources.resources().size();
        if (resources.needsProcessing() || count != resourceCount)
        {
          quietTimer.restart();
          resourceCount = count;
        }
        return presentedFrames >= initialFrames + 2 && quietTimer.elapsed() >= 250;
      },
      120000));
  };
  if (loadTiming)
  {
    waitForResources();
  }
  const auto editorReadyMs = loadTimer.elapsed();
  const auto stopPlay = qScopeGuard([&]() { view->stopPlayMode(); });
  const auto play = qEnvironmentVariableIsSet("TB_RENDER_PROFILE_PLAY");
  const auto moveSelection =
    qEnvironmentVariableIsSet("TB_RENDER_PROFILE_MOVE_SELECTION");
  const auto selectAll =
    moveSelection || qEnvironmentVariableIsSet("TB_RENDER_PROFILE_SELECT_ALL");
  REQUIRE_FALSE((play && selectAll));
  const auto playStartMs = loadTimer.elapsed();
  if (play)
  {
    window.raise();
    window.activateWindow();
    view->setFocus();
    view->startPlayMode(true);
    REQUIRE(QTest::qWaitFor([&]() { return view->playModeActive(); }, 120000));
  }
  const auto playActiveMs = loadTimer.elapsed();
  if (loadTiming)
  {
    if (play)
    {
      waitForResources();
    }
    const auto readyMs = loadTimer.elapsed();
    auto loading = QFile{qEnvironmentVariable("TB_RENDER_PROFILE") + ".loading.json"};
    REQUIRE(loading.open(QIODevice::WriteOnly));
    loading.write(QJsonDocument{
      QJsonObject{
        {"document_loaded_ms", documentLoadedMs},
        {"window_constructed_ms", windowConstructedMs},
        {"first_frame_ms", firstFrameMs},
        {"editor_resources_settled_ms", editorReadyMs},
        {"play_activation_ms", play ? playActiveMs - playStartMs : 0},
        {"total_ready_ms", readyMs},
        {"resource_quiet_interval_ms", 250},
        {"resource_notification_ms", static_cast<double>(notificationNs) / 1e6},
        {"resource_notification_max_ms", static_cast<double>(notificationMaxNs) / 1e6},
        {"resource_notification_batches", notificationBatches},
        {"play",
         play}}}.toJson());
  }
  QObject::connect(
    view, &QOpenGLWidget::frameSwapped, view, [view]() { view->update(); });
  const auto runFor = [&](const int seconds) {
    auto loop = QEventLoop{};
    QTimer::singleShot(seconds * 1000, &loop, &QEventLoop::quit);
    view->update();
    loop.exec();
  };
  runFor(std::clamp(qEnvironmentVariableIntValue("TB_RENDER_PROFILE_WARMUP"), 1, 60));
  const auto presentFrame = [&]() {
    auto loop = QEventLoop{};
    auto presented = false;
    const auto connection =
      QObject::connect(view, &QOpenGLWidget::frameSwapped, &loop, [&]() {
        presented = true;
        loop.quit();
      });
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    view->update();
    loop.exec();
    QObject::disconnect(connection);
    REQUIRE(presented);
  };
  const auto vectorJson = [](const auto& v) { return QJsonArray{v.x(), v.y(), v.z()}; };
  auto& map = window.document().map();
  auto selectionInfo = QJsonObject{};
  if (selectAll)
  {
    auto timer = QElapsedTimer{};
    timer.start();
    mdl::selectAllNodes(map);
    const auto selectMs = double(timer.nsecsElapsed()) / 1e6;
    presentFrame();
    selectionInfo = {
      {"nodes", qint64(map.selection().nodes.size())},
      {"brushes", qint64(map.selection().allBrushes().size())},
      {"entities", qint64(map.selection().entities.size())},
      {"select_sync_ms", selectMs},
      {"select_to_present_ms", double(timer.nsecsElapsed()) / 1e6},
      {"grid_size", map.grid().actualSize()},
      {"alignment_lock", map.editorContext().alignmentLock()}};
    REQUIRE(map.selectionBounds());
    selectionInfo.insert("bounds_min", vectorJson(map.selectionBounds()->min));
    selectionInfo.insert("bounds_max", vectorJson(map.selectionBounds()->max));
    runFor(std::clamp(qEnvironmentVariableIntValue("TB_RENDER_PROFILE_WARMUP"), 1, 60));
  }
  const auto originalBounds = map.selectionBounds();
  auto dragOrigin = QPoint{};
  auto mouseEvent = [&](const MouseEvent::Type type, const QPoint& pos) {
    dragView->processEvent(
      MouseEvent{type, MouseEvent::Button::Left, float(pos.x()), float(pos.y())});
  };
  const auto cancelDrag = qScopeGuard([&]() {
    if (moveSelection && window.toolBox().dragging())
    {
      window.toolBox().cancelMouseDrag();
    }
  });
  if (moveSelection)
  {
    using namespace mdl::HitFilters;
    window.raise();
    window.activateWindow();
    dragView->setFocus();
    window.toolBox().setSelectMode(true);
    auto found = false;
    for (const auto y : {0.5, 0.65, 0.35, 0.8, 0.2})
    {
      for (const auto x : {0.5, 0.65, 0.35, 0.8, 0.2})
      {
        dragOrigin =
          QPoint{qRound(x * dragView->width()), qRound(y * dragView->height())};
        mouseEvent(MouseEvent::Type::Motion, dragOrigin);
        if (dragView->pickResult()
              .first(type(mdl::nodeHitType()) && transitivelySelected())
              .isMatch())
        {
          found = true;
          break;
        }
      }
      if (found)
      {
        break;
      }
    }
    REQUIRE(found);
    mouseEvent(MouseEvent::Type::Down, dragOrigin);
    mouseEvent(MouseEvent::Type::DragStart, dragOrigin);
    REQUIRE(window.toolBox().dragging());
  }
  if (play)
  {
    REQUIRE(view->playModeActive());
  }
  const auto moving = play && qEnvironmentVariableIsSet("TB_RENDER_PROFILE_MOVE_FORWARD");
  if (moving)
  {
    auto key = QKeyEvent{QEvent::KeyPress, Qt::Key_W, Qt::NoModifier};
    QApplication::sendEvent(view, &key);
  }
  const auto positionStart = camera.position();
  const auto directionStart = camera.direction();
  const auto measurementStart = QDateTime::currentDateTimeUtc();
  const auto seconds =
    std::clamp(qEnvironmentVariableIntValue("TB_RENDER_PROFILE_SECONDS"), 2, 300);
  auto updates = QJsonArray{};
  auto dragEnd = dragOrigin;
  if (moveSelection)
  {
    auto duration = QElapsedTimer{};
    duration.start();
    do
    {
      const auto before = map.selectionBounds();
      dragEnd = dragOrigin + QPoint{updates.size() % 2 == 0 ? 64 : -64, 0};
      auto timer = QElapsedTimer{};
      timer.start();
      mouseEvent(MouseEvent::Type::Drag, dragEnd);
      const auto syncMs = double(timer.nsecsElapsed()) / 1e6;
      const auto after = map.selectionBounds();
      REQUIRE(after);
      REQUIRE(after != before);
      REQUIRE(window.toolBox().dragging());
      presentFrame();
      updates.append(QJsonObject{
        {"sync_ms", syncMs},
        {"to_present_ms", double(timer.nsecsElapsed()) / 1e6},
        {"delta", vectorJson(after->min - before->min)}});
    } while (duration.elapsed() < seconds * 1000);
  }
  else
  {
    runFor(seconds);
  }
  const auto measurementEnd = QDateTime::currentDateTimeUtc();
  if (moveSelection)
  {
    auto timer = QElapsedTimer{};
    timer.start();
    mouseEvent(MouseEvent::Type::DragEnd, dragEnd);
    mouseEvent(MouseEvent::Type::Up, dragEnd);
    selectionInfo.insert("release_sync_ms", double(timer.nsecsElapsed()) / 1e6);
    presentFrame();
    selectionInfo.insert("release_to_present_ms", double(timer.nsecsElapsed()) / 1e6);
    REQUIRE_FALSE(window.toolBox().dragging());
    CHECK(map.modified());
    timer.restart();
    map.undoCommand();
    selectionInfo.insert("undo_sync_ms", double(timer.nsecsElapsed()) / 1e6);
    CHECK(map.selectionBounds() == originalBounds);
    selectionInfo.insert("updates", updates);
  }
  CHECK(vm::squared_distance(directionStart, camera.direction()) < 0.000001f);
  if (moving)
  {
    auto key = QKeyEvent{QEvent::KeyRelease, Qt::Key_W, Qt::NoModifier};
    QApplication::sendEvent(view, &key);
  }
  if (play)
  {
    CHECK(view->playModeActive());
  }
  auto measurement =
    QFile{qEnvironmentVariable("TB_RENDER_PROFILE") + ".measurement.json"};
  REQUIRE(measurement.open(QIODevice::WriteOnly));
  const auto measurementInfo = QJsonObject{
    {"start_utc", measurementStart.toString(Qt::ISODateWithMs)},
    {"end_utc", measurementEnd.toString(Qt::ISODateWithMs)},
    {"position_start", vectorJson(positionStart)},
    {"position_end", vectorJson(camera.position())},
    {"direction_start", vectorJson(directionStart)},
    {"direction_end", vectorJson(camera.direction())},
    {"play", play},
    {"move_forward", moving},
    {"select_all", selectAll},
    {"move_selection", moveSelection},
    {"four_panes", fourPanes},
    {"selection", selectionInfo}};
  measurement.write(QJsonDocument{measurementInfo}.toJson());
  CHECK(view->isValid());
  CHECK_FALSE(window.document().map().modified());
  CHECK(view->grabFramebuffer().save(qEnvironmentVariable("TB_RENDER_PROFILE") + ".png"));
  CHECK(QFile::copy(
    pathAsQString(SystemPaths::logFilePath()),
    qEnvironmentVariable("TB_RENDER_PROFILE") + ".app.log"));
}
} // namespace tb::ui
