/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include "TestEnvironment.h"
#include "gl/PerspectiveCamera.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/CompilationProfile.h"
#include "mdl/EntityNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/MapFixture.h"
#include "mdl/Map_Nodes.h"
#include "mdl/WorldNode.h"
#include "ui/CompilationHltoolsTaskRunner.h"
#include "ui/CompilationVariables.h"
#include "ui/QPathUtils.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace tb::ui
{
namespace
{
void addCompileRoom(mdl::Map& map)
{
  const auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};
  for (const auto& bounds :
       {vm::bbox3d{{-144, -144, -16}, {144, 144, 0}},
        vm::bbox3d{{-144, -144, 128}, {144, 144, 144}},
        vm::bbox3d{{-144, -144, 0}, {-128, 144, 128}},
        vm::bbox3d{{128, -144, 0}, {144, 144, 128}},
        vm::bbox3d{{-128, -144, 0}, {128, -128, 128}},
        vm::bbox3d{{-128, 128, 0}, {128, 144, 128}}})
  {
    auto* node = new mdl::BrushNode{
      builder.createCuboid(bounds, mdl::BrushFace::NoMaterialName) | kdl::value()};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {node}}});
  }
  auto* light = new mdl::EntityNode{mdl::Entity{
    {{"classname", "light"}, {"origin", "0 0 80"}, {"_light", "255 210 150 300"}}}};
  auto* player = new mdl::EntityNode{
    mdl::Entity{{{"classname", "info_player_start"}, {"origin", "0 0 40"}}}};
  mdl::addNodes(map, {{&mdl::parentForNodes(map), {light, player}}});
}
} // namespace

TEST_CASE("CompilationHltoolsTaskRunner")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.create();
  addCompileRoom(map);
  auto directory = QTemporaryDir{};
  REQUIRE(directory.isValid());
  const auto outputPath = directory.filePath("output with spaces/custom.bsp");
  auto output = QTextEdit{};
  auto camera = gl::PerspectiveCamera{};
  auto variables = CompilationVariables{
    map, directory.path().toStdString(), pathFromQString(directory.filePath("room.map"))};
  auto task = mdl::CompilationHltools{
    true, outputPath.toStdString(), "normal", false, "-threads 2"};

  SECTION("compiles the unsaved map through all stages and publishes BSP and log")
  {
    task.quality = GENERATE("normal", "fast", "extra");
    task.gpu = GENERATE(false, true);
    REQUIRE(QDir{}.mkpath(directory.filePath("output with spaces")));
    auto oldLeak = QFile{directory.filePath("output with spaces/custom.lin")};
    REQUIRE(oldLeak.open(QIODevice::WriteOnly));
    oldLeak.write("old leak trail");
    oldLeak.close();
    auto context =
      CompilationContext{map, camera, variables, TextOutputAdapter{&output}, false};
    auto runner = CompilationHltoolsTaskRunner{context, task};
    auto stages = QSignalSpy{&runner, &CompilationTaskRunner::stageChanged};
    auto ended = QSignalSpy{&runner, &CompilationTaskRunner::end};
    auto failed = QSignalSpy{&runner, &CompilationTaskRunner::error};
    runner.execute();
    REQUIRE(QTest::qWaitFor([&] { return !ended.empty() || !failed.empty(); }, 30000));
    INFO(output.toPlainText().toStdString());
    REQUIRE(failed.empty());
    REQUIRE(ended.count() == 1);
    auto reported = QStringList{};
    for (const auto& event : stages)
      reported.push_back(event.front().toString());
    INFO(reported.join(", ").toStdString());
    CHECK(reported.contains("CSG"));
    CHECK(reported.contains("BSP"));
    CHECK(reported.contains("VIS"));
    CHECK(reported.contains("RAD"));
    auto bsp = QFile{outputPath};
    REQUIRE(bsp.open(QIODevice::ReadOnly));
    CHECK(bsp.read(4) == QByteArray::fromHex("1e000000"));
    CHECK(bsp.size() > 1024);
    CHECK(context.bspPath() == pathFromQString(outputPath));
    CHECK(
      context.variableValue(CompilationVariableNames::BSP_PATH).value()
      == pathFromQString(outputPath).string());
    auto log = QFile{directory.filePath("output with spaces/custom.log")};
    REQUIRE(log.open(QIODevice::ReadOnly));
    const auto logText = log.readAll();
    CHECK(logText.contains("building bsp tree"));
    CHECK(logText.contains("computing visibility"));
    CHECK(logText.contains("finalizing lightmaps"));
    CHECK(logText.contains("Detailed compiler log"));
    CHECK_FALSE(oldLeak.exists());
    CHECK_FALSE(QFile::exists(directory.filePath("room.map")));
  }

  SECTION("visual option flags reach the bundled compiler")
  {
    task.parameterSpec =
      "-threads 2 -cliptype precise -noskyclip -allleaks -bounce 2 -rgbtransfers";
    auto context =
      CompilationContext{map, camera, variables, TextOutputAdapter{&output}, false};
    auto runner = CompilationHltoolsTaskRunner{context, task};
    auto ended = QSignalSpy{&runner, &CompilationTaskRunner::end};
    auto failed = QSignalSpy{&runner, &CompilationTaskRunner::error};
    runner.execute();
    REQUIRE(QTest::qWaitFor([&] { return !ended.empty() || !failed.empty(); }, 30000));
    INFO(output.toPlainText().toStdString());
    REQUIRE(failed.empty());
    REQUIRE(ended.count() == 1);
    CHECK(QFile::exists(outputPath));
    CHECK(output.toPlainText().contains("precise"));
  }

  SECTION("test mode plans the compile without creating output or scratch")
  {
    auto context =
      CompilationContext{map, camera, variables, TextOutputAdapter{&output}, true};
    auto runner = CompilationHltoolsTaskRunner{context, task};
    auto ended = QSignalSpy{&runner, &CompilationTaskRunner::end};
    runner.execute();
    REQUIRE(ended.count() == 1);
    CHECK(output.toPlainText().contains("CSG/BSP/VIS/RAD"));
    CHECK_FALSE(QDir{directory.filePath("output with spaces")}.exists());
    CHECK(context.bspPath() == pathFromQString(outputPath));
  }

  SECTION("standalone model collision follows the original MAP search roots")
  {
    REQUIRE(QDir{}.mkpath(directory.filePath("game/models")));
    REQUIRE(QDir{}.mkpath(directory.filePath("game/maps")));
    REQUIRE(QDir{}.mkpath(directory.filePath("work")));
    REQUIRE(QFile::copy(
      pathAsQString(getFixtureRoot() / "test/publish-model.mdl"),
      directory.filePath("game/models/prop.mdl")));
    REQUIRE(
      map.saveAs(pathFromQString(directory.filePath("game/maps/room.map"))).is_success());
    auto* model = new mdl::EntityNode{mdl::Entity{
      {{"classname", "cycler"},
       {"origin", "64 0 8"},
       {"model", "models/prop.mdl"},
       {"zhlt_studiobsp", "2"}}}};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {model}}});
    auto omitted = mdl::Layer{"Omitted"};
    omitted.setOmitFromExport(true);
    auto* layer = new mdl::LayerNode{std::move(omitted)};
    layer->addChild(new mdl::EntityNode{
      mdl::Entity{{{"classname", "cycler"}, {"model", "../omitted.mdl"}}}});
    mdl::addNodes(map, {{&map.worldNode(), {layer}}});
    variables = CompilationVariables{map, directory.filePath("work").toStdString()};
    auto context =
      CompilationContext{map, camera, variables, TextOutputAdapter{&output}, false};
    auto runner = CompilationHltoolsTaskRunner{context, task};
    auto ended = QSignalSpy{&runner, &CompilationTaskRunner::end};
    auto failed = QSignalSpy{&runner, &CompilationTaskRunner::error};
    runner.execute();
    REQUIRE(QTest::qWaitFor([&] { return !ended.empty() || !failed.empty(); }, 30000));
    INFO(output.toPlainText().toStdString());
    REQUIRE(failed.empty());
    auto bsp = QFile{outputPath};
    REQUIRE(bsp.open(QIODevice::ReadOnly));
    const auto bytes = bsp.readAll();
    CHECK(bytes.contains("func_wall"));
    CHECK_FALSE(bytes.contains("omitted.mdl"));
  }

  SECTION("invalid settings fail and leave a prior BSP intact")
  {
    REQUIRE(QDir{}.mkpath(directory.filePath("output with spaces")));
    auto previous = QFile{outputPath};
    REQUIRE(previous.open(QIODevice::WriteOnly));
    previous.write("previous BSP");
    previous.close();
    task.quality = "invalid";
    auto context =
      CompilationContext{map, camera, variables, TextOutputAdapter{&output}, false};
    auto runner = CompilationHltoolsTaskRunner{context, task};
    auto failed = QSignalSpy{&runner, &CompilationTaskRunner::error};
    runner.execute();
    REQUIRE(failed.count() == 1);
    REQUIRE(previous.open(QIODevice::ReadOnly));
    CHECK(previous.readAll() == "previous BSP");
  }

  SECTION("worker failures preserve prior output and stop later tasks")
  {
    REQUIRE(QDir{}.mkpath(directory.filePath("output with spaces")));
    auto previous = QFile{outputPath};
    REQUIRE(previous.open(QIODevice::WriteOnly));
    previous.write("previous BSP");
    previous.close();
    task.parameterSpec += " -wadcfgfile missing-compile-wads.cfg";
    auto profile = mdl::CompilationProfile{
      "Fail",
      directory.path().toStdString(),
      {task, mdl::CompilationExportMap{true, false, {}, {}, "should-not-exist.map"}}};
    auto context =
      CompilationContext{map, camera, variables, TextOutputAdapter{&output}, false};
    auto runner = CompilationRunner{std::move(context), profile};
    runner.execute();
    REQUIRE(QTest::qWaitFor([&] { return !runner.running(); }, 30000));
    INFO(output.toPlainText().toStdString());
    CHECK(output.toPlainText().contains("Compilation failed"));
    CHECK_FALSE(QFile::exists(directory.filePath("should-not-exist.map")));
    REQUIRE(previous.open(QIODevice::ReadOnly));
    CHECK(previous.readAll() == "previous BSP");
    CHECK(QFile::exists(directory.filePath("output with spaces/custom.log")));
  }

  SECTION("cancelling a starting worker prevents artifact publication")
  {
    auto context =
      CompilationContext{map, camera, variables, TextOutputAdapter{&output}, false};
    auto runner = CompilationHltoolsTaskRunner{context, task};
    auto ended = QSignalSpy{&runner, &CompilationTaskRunner::end};
    runner.execute();
    runner.terminate();
    QTest::qWait(100);
    CHECK(ended.empty());
    CHECK_FALSE(QFile::exists(outputPath));
    CHECK(output.toPlainText().contains("Terminated"));
  }
}
} // namespace tb::ui
