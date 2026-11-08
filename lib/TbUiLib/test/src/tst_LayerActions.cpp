/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QMenu>

#include "mdl/EditorContext.h"
#include "mdl/EntityNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Layers.h"
#include "mdl/Map_NodeVisibility.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/WorldNode.h"
#include "ui/LayerActions.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
TEST_CASE("LayerActions")
{
  auto fixture = MapDocumentFixture{};
  auto& map = fixture.create().map();
  auto* first = mdl::createLayer(map, "Buildings");
  auto* second = mdl::createLayer(map, "Details");
  REQUIRE(first);
  REQUIRE(second);
  auto menu = QMenu{};
  populateLayerActions(menu, map, first);
  const auto action = [&](const char* id) {
    auto* result = menu.findChild<QAction*>(QString::fromLatin1(id));
    REQUIRE(result);
    return result;
  };

  SECTION("layer ordering and export changes use native undo")
  {
    CHECK_FALSE(action("Layer_MoveUp")->isEnabled());
    REQUIRE(action("Layer_MoveDown")->isEnabled());
    action("Layer_MoveDown")->trigger();
    CHECK(map.worldNode().customLayersUserSorted() == std::vector{second, first});
    map.undoCommand();
    CHECK(map.worldNode().customLayersUserSorted() == std::vector{first, second});
    action("Layer_OmitFromExport")->trigger();
    CHECK(first->layer().omitFromExport());
    map.undoCommand();
    CHECK_FALSE(first->layer().omitFromExport());
  }

  SECTION("removing an active populated layer preserves objects and supports undo")
  {
    auto* entity = new mdl::EntityNode{mdl::Entity{{{"classname", "light"}}}};
    mdl::addNodes(map, {{first, {entity}}});
    mdl::setCurrentLayer(map, first);
    action("Layer_Remove")->trigger();
    CHECK(entity->parent() == map.worldNode().defaultLayer());
    CHECK(map.editorContext().currentLayer() == map.worldNode().defaultLayer());
    map.undoCommand();
    CHECK(entity->parent() == first);
    CHECK(map.editorContext().currentLayer() == first);
    CHECK(map.worldNode().customLayersUserSorted() == std::vector{first, second});
  }

  SECTION("default and unavailable layers cannot be removed")
  {
    CHECK_FALSE(canRemoveLayer(map, map.worldNode().defaultLayer()));
    CHECK_FALSE(removeLayer(map, nullptr));
    mdl::hideNodes(map, {map.worldNode().defaultLayer(), second});
    CHECK_FALSE(canRemoveLayer(map, first));
    CHECK_FALSE(removeLayer(map, first));
    CHECK(map.worldNode().customLayers().size() == 2);
  }

  SECTION("a stale popup cannot operate on a removed layer")
  {
    REQUIRE(removeLayer(map, first));
    action("Layer_MakeActive")->trigger();
    action("Layer_OmitFromExport")->trigger();
    action("Layer_Remove")->trigger();
    CHECK(map.worldNode().customLayersUserSorted() == std::vector{second});
  }
}
} // namespace tb::ui
