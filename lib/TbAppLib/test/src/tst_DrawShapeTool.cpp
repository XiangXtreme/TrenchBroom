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

#include "base/PreferenceManager.h"
#include "gl/OrthographicCamera.h"
#include "gl/PerspectiveCamera.h"
#include "mdl/BrushNode.h"
#include "mdl/GroupNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Selection.h"
#include "prefs/Preferences.h"
#include "ui/DrawShapeTool.h"
#include "ui/DrawShapeToolController2D.h"
#include "ui/DrawShapeToolController3D.h"
#include "ui/DrawShapeToolExtensionManager.h"
#include "ui/GestureTracker.h"
#include "ui/InputState.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"
#include "ui/ToolChain.h"

#include "kd/range_utils.h"

#include "vm/bbox.h"

#include <algorithm>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

namespace
{

size_t indexOfExtensionNamed(
  DrawShapeToolExtensionManager& extensionManager, const std::string& name)
{
  return *kdl::index_of(extensionManager.extensions(), [&](const auto* extension) {
    return extension->name() == name;
  });
}

} // namespace

TEST_CASE("DrawShapeTool")
{
  auto fixture = MapDocumentFixture{};
  auto& document = fixture.create();
  auto& map = document.map();

  auto tool = DrawShapeTool{document};

  constexpr auto bounds = vm::bbox3d{{0, 0, 0}, {64, 64, 64}};

  SECTION("2D drawing follows the current base mode")
  {
    auto canDraw = false;
    auto chain = ToolChain{};
    chain.append(std::make_unique<DrawShapeToolController2D>(
      tool, document, [&]() { return canDraw; }));
    auto camera = gl::OrthographicCamera{};
    camera.setDirection({0, 0, -1}, {0, 1, 0});
    auto input = InputState{0, 0};
    input.mouseDown(MouseButtons::Left);
    input.setPickRequest({vm::ray3d{{0, 0, 128}, {0, 0, -1}}, camera});
    CHECK_FALSE(chain.acceptMouseDrag(input));
    canDraw = true;
    auto tracker = chain.acceptMouseDrag(input);
    REQUIRE(tracker);
    input.setPickRequest({vm::ray3d{{64, 64, 128}, {0, 0, -1}}, camera});
    REQUIRE(tracker->update(input));
    input.mouseUp(MouseButtons::Left);
    tracker->end(input);
    CHECK(map.selection().nodes.size() == 1);
    auto* firstBrush = map.selection().nodes.front();
    input.mouseDown(MouseButtons::Left);
    input.setPickRequest({vm::ray3d{{96, 0, 128}, {0, 0, -1}}, camera});
    tracker = chain.acceptMouseDrag(input);
    REQUIRE(tracker);
    input.setPickRequest({vm::ray3d{{160, 64, 128}, {0, 0, -1}}, camera});
    REQUIRE(tracker->update(input));
    input.mouseUp(MouseButtons::Left);
    tracker->end(input);
    REQUIRE(map.selection().nodes.size() == 1);
    CHECK(map.selection().nodes.front() != firstBrush);
    map.undoCommand();
    CHECK(map.selection().nodes == std::vector<mdl::Node*>{firstBrush});
    deselectAll(map);
    REQUIRE(tool.extensionManager().setCurrentExtensionIndex(
      indexOfExtensionNamed(tool.extensionManager(), "Stairs")));
    canDraw = false;
    CHECK_FALSE(chain.cancel());
    CHECK(tool.extensionManager().currentExtension().name() == "Stairs");
    canDraw = true;
    CHECK(chain.cancel());
  }

  SECTION("3D drawing follows the current base mode")
  {
    auto canDraw = false;
    auto chain = ToolChain{};
    chain.append(std::make_unique<DrawShapeToolController3D>(
      tool, document, [&]() { return canDraw; }));
    auto camera = gl::PerspectiveCamera{};
    camera.moveTo({0, -128, 128});
    camera.lookAt({0, 0, 0}, {0, 0, 1});
    auto input = InputState{0, 0};
    input.mouseDown(MouseButtons::Left);
    input.setPickRequest(
      {vm::ray3d{vm::vec3d{camera.position()}, vm::vec3d{camera.direction()}}, camera});
    CHECK_FALSE(chain.acceptMouseDrag(input));
    canDraw = true;
    auto tracker = chain.acceptMouseDrag(input);
    REQUIRE(tracker);
    tracker->cancel();
    canDraw = false;
    CHECK_FALSE(chain.acceptMouseDrag(input));
    REQUIRE(tool.extensionManager().setCurrentExtensionIndex(
      indexOfExtensionNamed(tool.extensionManager(), "Stairs")));
    CHECK_FALSE(chain.cancel());
    CHECK(tool.extensionManager().currentExtension().name() == "Stairs");
    canDraw = true;
    CHECK(chain.cancel());
  }

  SECTION("createBrushes groups multiple brushes and names the group after the shape")
  {
    REQUIRE(pref(Preferences::GroupBrushesCreatedByShapeTool));

    const auto stairsIndex = indexOfExtensionNamed(tool.extensionManager(), "Stairs");
    REQUIRE(tool.extensionManager().setCurrentExtensionIndex(stairsIndex));

    tool.update(bounds);
    tool.createBrushes(tool.groupNameForCreatedBrushes());

    const auto& selectedNodes = map.selection().nodes;
    REQUIRE(selectedNodes.size() == 1);

    auto* groupNode = dynamic_cast<mdl::GroupNode*>(selectedNodes.front());
    REQUIRE(groupNode != nullptr);
    CHECK(groupNode->group().name() == "Stairs");
    CHECK(groupNode->childCount() > 1);
  }

  SECTION(
    "createBrushes does not group multiple brushes when "
    "GroupBrushesCreatedByShapeTool is disabled")
  {
    setPref(Preferences::GroupBrushesCreatedByShapeTool, false);

    const auto stairsIndex = indexOfExtensionNamed(tool.extensionManager(), "Stairs");
    REQUIRE(tool.extensionManager().setCurrentExtensionIndex(stairsIndex));

    tool.update(bounds);
    tool.createBrushes(tool.groupNameForCreatedBrushes());

    const auto& selectedNodes = map.selection().nodes;
    CHECK(selectedNodes.size() > 1);
    for (auto* node : selectedNodes)
    {
      CHECK(dynamic_cast<mdl::BrushNode*>(node) != nullptr);
    }

    PreferenceManager::instance().resetToDefault(
      Preferences::GroupBrushesCreatedByShapeTool);
  }

  SECTION("createBrushes does not create a group for a single brush")
  {
    // Cuboid is the default extension and always creates a single brush.
    REQUIRE(tool.extensionManager().currentExtension().name() == "Cuboid");

    tool.update(bounds);
    tool.createBrushes(tool.groupNameForCreatedBrushes());

    const auto& selectedNodes = map.selection().nodes;
    REQUIRE(selectedNodes.size() == 1);
    CHECK(dynamic_cast<mdl::BrushNode*>(selectedNodes.front()) != nullptr);
  }
}

} // namespace tb::ui
