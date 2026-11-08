/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QApplication>
#include <QImage>
#include <QtTest/QTest>

#include "mdl/BrushFaceHandle.h"
#include "mdl/Map.h"
#include "mdl/Map_Selection.h"
#include "ui/AppControllerFixture.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"
#include "ui/UvView.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
TEST_CASE("UvView")
{
  auto app = AppControllerFixture{};
  auto fixture = MapDocumentFixture{};
  auto& document = fixture.load("test/mapMaterialsQ2.map", mdl::Quake2FixtureConfig);
  auto& map = document.map();

  SECTION("renders a preselected face at the actual canvas size on first opening")
  {
    mdl::selectAllNodes(map);
    REQUIRE_FALSE(map.selection().brushes.empty());
    const auto face = mdl::BrushFaceHandle{*map.selection().brushes.front(), 0};
    mdl::deselectAll(map);
    mdl::selectBrushFaces(map, {face});
    auto view = UvView{app.appController(), document};
    view.resize(260, 220);
    view.show();
    REQUIRE(QTest::qWaitFor([&] { return view.isValid(); }));
    // Drain composition before comparing the same camera/selection across reloads.
    view.grabFramebuffer();
    const auto initial = view.grabFramebuffer();
    REQUIRE_FALSE(initial.isNull());
    mdl::deselectAll(map);
    QApplication::processEvents();
    view.grabFramebuffer();
    const auto empty = view.grabFramebuffer();
    CHECK(initial != empty);
    mdl::selectBrushFaces(map, {face});
    QApplication::processEvents();
    view.grabFramebuffer();
    CHECK(initial == view.grabFramebuffer());
  }
}
} // namespace tb::ui
