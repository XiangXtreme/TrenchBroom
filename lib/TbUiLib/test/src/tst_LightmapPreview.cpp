/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QScopeGuard>
#include <QtTest/QTest>

#include "base/PreferenceManager.h"
#include "gl/GlManager.h"
#include "gl/ResourceManager.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/CommandProcessor.h"
#include "mdl/EntityNode.h"
#include "mdl/GameConfigFixture.h"
#include "mdl/Map.h"
#include "mdl/Map_Nodes.h"
#include "mdl/TransactionScope.h"
#include "mdl/WorldNode.h"
#include "prefs/Preferences.h"
#include "ui/AppControllerFixture.h"
#include "ui/LightmapPreview.h"
#include "ui/MapDocument.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
TEST_CASE("LightmapPreview")
{
  const auto show = pref(Preferences::ShowLightmap);
  const auto automatic = pref(Preferences::LightmapAutoUpdate);
  const auto delay = pref(Preferences::LightmapUpdateDelay);
  const auto quality = pref(Preferences::LightmapQuality);
  const auto gamma = pref(Preferences::LightmapGamma);
  const auto restore = qScopeGuard([&] {
    setPref(Preferences::ShowLightmap, show);
    setPref(Preferences::LightmapAutoUpdate, automatic);
    setPref(Preferences::LightmapUpdateDelay, delay);
    setPref(Preferences::LightmapQuality, quality);
    setPref(Preferences::LightmapGamma, gamma);
  });
  setPref(Preferences::ShowLightmap, true);
  setPref(Preferences::LightmapAutoUpdate, false);
  setPref(Preferences::LightmapUpdateDelay, 100);
  setPref(Preferences::LightmapQuality, Preferences::LightmapQualityFast);
  auto fixture = AppControllerFixture{};
  auto& app = fixture.appController();
  auto document = MapDocument::createDocument(
                    app.environmentConfig(),
                    mdl::QuakeGameInfo,
                    mdl::MapFormat::Valve,
                    vm::bbox3d{8192.0},
                    app.taskManager(),
                    app.glManager().resourceManager())
                  | kdl::value();
  auto& map = document->map();
  const auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};
  for (const auto& bounds :
       {vm::bbox3d{{-144, -144, -16}, {144, 144, 0}},
        vm::bbox3d{{-144, -144, 128}, {144, 144, 144}},
        vm::bbox3d{{-144, -144, 0}, {-128, 144, 128}},
        vm::bbox3d{{128, -144, 0}, {144, 144, 128}},
        vm::bbox3d{{-128, -144, 0}, {128, -128, 128}},
        vm::bbox3d{{-128, 128, 0}, {128, 144, 128}}})
    mdl::addNodes(
      map,
      {{&mdl::parentForNodes(map),
        {new mdl::BrushNode{
          builder.createCuboid(bounds, mdl::BrushFace::NoMaterialName)
          | kdl::value()}}}});
  auto* light = new mdl::EntityNode{mdl::Entity{
    {{"classname", "light"}, {"origin", "0 0 80"}, {"_light", "255 210 150 300"}}}};
  mdl::addNodes(map, {{&mdl::parentForNodes(map), {light}}});
  auto preview = LightmapPreview{*document};
  auto starts = 0;
  auto fail = false;
  auto editDuringBake = false;
  preview.setProjectInfoProvider(
    [&](const auto&) -> Result<std::optional<ProjectCompileInfo>> {
      ++starts;
      if (editDuringBake && starts == 1)
        for (auto i = 0; i < 5; ++i)
          map.incModificationCount(1);
      if (fail)
        return Error{"test export failure"};
      return std::optional<ProjectCompileInfo>{};
    });
  const auto waitForResult = [&] {
    REQUIRE(
      QTest::qWaitFor([&] { return !preview.baking() && preview.hasResult(); }, 30000));
    INFO(preview.error().toStdString());
    CHECK(preview.error().isEmpty());
  };

  SECTION("manual mode preserves a result across edits and failed rebakes")
  {
    preview.requestBake();
    waitForResult();
    CHECK_FALSE(preview.stale());
    map.incModificationCount(1);
    QTest::qWait(250);
    CHECK(starts == 1);
    CHECK(preview.stale());
    CHECK(preview.hasResult());
    fail = true;
    preview.requestBake();
    REQUIRE(QTest::qWaitFor([&] { return !preview.error().isEmpty(); }, 5000));
    CHECK(preview.hasResult());
    CHECK(preview.stale());
    fail = false;
    preview.requestBake();
    REQUIRE(QTest::qWaitFor([&] { return !preview.stale(); }, 30000));
    CHECK(preview.hasResult());
    CHECK(preview.error().isEmpty());
  }

  SECTION("edits during baking queue one follow-up without restarting the worker")
  {
    setPref(Preferences::LightmapAutoUpdate, true);
    editDuringBake = true;
    preview.requestBake();
    REQUIRE(QTest::qWaitFor(
      [&] { return starts == 2 && !preview.baking() && !preview.stale(); }, 30000));
    QTest::qWait(250);
    CHECK(starts == 2);
    CHECK(preview.hasResult());
    CHECK(preview.error().isEmpty());
  }

  SECTION("active edit transactions defer even a manually requested bake")
  {
    map.commandProcessor().startTransaction("Drag", mdl::TransactionScope::LongRunning);
    preview.requestBake();
    QTest::qWait(250);
    CHECK(starts == 0);
    map.commandProcessor().commitTransaction();
    waitForResult();
    CHECK(starts == 1);
  }

  SECTION("cancellation permits another bake and display changes do not bake")
  {
    preview.requestBake();
    REQUIRE(QTest::qWaitFor([&] { return starts == 1; }, 5000));
    preview.cancel();
    CHECK_FALSE(preview.baking());
    preview.requestBake();
    waitForResult();
    CHECK(starts == 2);
    setPref(Preferences::LightmapGamma, 2.0f);
    QTest::qWait(250);
    CHECK(starts == 2);
    CHECK_FALSE(preview.stale());
  }
}
} // namespace tb::ui
