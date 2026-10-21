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

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedLayout>
#include <QWidget>

#include "base/PreferenceManager.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushNode.h"
#include "mdl/EditorContext.h"
#include "mdl/Entity.h"
#include "mdl/EntityDefinition.h"
#include "mdl/EntityDefinitionManager.h"
#include "mdl/EntityNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/MapFixture.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/NodeHandleManager.h"
#include "mdl/NodeHandles.h"
#include "mdl/PropertyDefinition.h"
#include "mdl/WorldNode.h"
#include "prefs/Preferences.h"
#include "ui/CatchConfig.h"
#include "ui/ChamferTool.h"
#include "ui/ChamferToolPage.h"
#include "ui/CreateEntityTool.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"
#include "ui/MapViewToolBox.h"
#include "ui/PathTool.h"
#include "ui/SweepTool.h"
#include "ui/SweepToolPage.h"

#include "kd/result.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

namespace tb::ui
{
using namespace Catch::Matchers;

namespace
{
void setPathCornerDefinition(mdl::Map& map)
{
  map.entityDefinitionManager().setDefinitions({
    {
      "path_corner",
      Color{},
      "path node",
      {},
      mdl::PointEntityDefinition{vm::bbox3d{16.0}, {}, {}},
    },
  });
}

void setAssetEntityDefinitions(mdl::Map& map)
{
  map.entityDefinitionManager().setDefinitions({
    {
      "path_corner",
      Color{},
      "path node",
      {},
      mdl::PointEntityDefinition{vm::bbox3d{16.0}, {}, {}},
    },
    {
      "cycler",
      Color{},
      "model entity",
      {
        {"model", mdl::PropertyValueTypes::String{}, "", "", false},
      },
      mdl::PointEntityDefinition{vm::bbox3d{16.0}, {}, {}},
    },
    {
      "cycler_sprite",
      Color{},
      "preferred model entity",
      {
        {"model", mdl::PropertyValueTypes::String{}, "", "", false},
      },
      mdl::PointEntityDefinition{vm::bbox3d{16.0}, {}, {}},
    },
    {
      "cycler_mdl",
      Color{},
      "mdl model entity",
      {
        {"mdl", mdl::PropertyValueTypes::String{}, "", "", false},
      },
      mdl::PointEntityDefinition{vm::bbox3d{16.0}, {}, {}},
    },
    {
      "env_sprite",
      Color{},
      "sprite entity",
      {
        {"model", mdl::PropertyValueTypes::String{}, "", "", false},
      },
      mdl::PointEntityDefinition{vm::bbox3d{16.0}, {}, {}},
    },
    {
      "ambient_generic",
      Color{},
      "sound entity",
      {
        {"message", mdl::PropertyValueTypes::String{}, "", "", false},
        {"noise", mdl::PropertyValueTypes::String{}, "", "", false},
        {"sound", mdl::PropertyValueTypes::String{}, "", "", false},
      },
      mdl::PointEntityDefinition{vm::bbox3d{16.0}, {}, {}},
    },
  });
}

std::vector<mdl::EntityNode*> selectedEntityNodes(mdl::Map& map)
{
  auto result = std::vector<mdl::EntityNode*>{};
  for (auto* node : map.selection().nodes)
  {
    if (auto* entityNode = dynamic_cast<mdl::EntityNode*>(node))
    {
      result.push_back(entityNode);
    }
  }
  return result;
}
} // namespace

TEST_CASE("MapViewToolBox")
{
  auto fixture = MapDocumentFixture{};
  auto& document = fixture.create();
  auto& map = document.map();
  setPathCornerDefinition(map);

  auto parent = QWidget{};
  auto bookCtrl = QStackedLayout{&parent};
  auto toolBox = MapViewToolBox{document, &bookCtrl};

  SECTION("base mode survives modal tools and document refresh")
  {
    toolBox.setSelectMode(true);
    CHECK(toolBox.selectModeActive());
    CHECK_FALSE(toolBox.brushCreationModeActive());
    toolBox.togglePathTool();
    CHECK_FALSE(toolBox.selectModeActive());
    CHECK_FALSE(toolBox.brushCreationModeActive());
    toolBox.deactivateCurrentTool();
    CHECK(toolBox.selectModeActive());
    toolBox.togglePathTool();
    toolBox.setSelectMode(false);
    CHECK_FALSE(toolBox.pathToolActive());
    CHECK(toolBox.brushCreationModeActive());
    document.documentWasLoadedNotifier();
    CHECK(toolBox.brushCreationModeActive());
    setPref(Preferences::Default2DSelectMode, true);
    CHECK(toolBox.selectModeActive());
  }

  SECTION("default base mode is used by newly created toolboxes")
  {
    setPref(Preferences::Default2DSelectMode, true);
    auto secondParent = QWidget{};
    auto secondBook = QStackedLayout{&secondParent};
    auto secondToolBox = MapViewToolBox{document, &secondBook};
    CHECK(secondToolBox.selectModeActive());
  }

  SECTION("path tool entry toggles and creates path entities")
  {
    CHECK_FALSE(toolBox.pathToolActive());

    toolBox.togglePathTool();

    CHECK(toolBox.pathToolActive());

    toolBox.pathTool().addPoint({64.0, 0.0, 0.0});
    toolBox.pathTool().addPoint({128.0, 0.0, 0.0});
    toolBox.pathTool().addPoint({128.0, 64.0, 0.0});

    toolBox.removeLastPathPoint();

    CHECK_THAT(
      toolBox.pathTool().points(),
      Equals(std::vector<vm::vec3d>{{64.0, 0.0, 0.0}, {128.0, 0.0, 0.0}}));

    toolBox.performPathCreation();

    CHECK(toolBox.pathTool().points().empty());
    CHECK(toolBox.pathToolActive());
    CHECK(map.worldNode().defaultLayer()->children().size() == 2u);
    CHECK(selectedEntityNodes(map).size() == 2u);

    toolBox.togglePathTool();

    CHECK_FALSE(toolBox.pathToolActive());
  }

  SECTION("path tool is exclusive with other modal tools")
  {
    auto* entityNode = new mdl::EntityNode{mdl::Entity{{{"classname", "path_corner"}}}};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {entityNode}}});
    mdl::selectNodes(map, {entityNode});

    toolBox.toggleRotateTool();
    REQUIRE(toolBox.rotateToolActive());

    toolBox.togglePathTool();

    CHECK(toolBox.pathToolActive());
    CHECK_FALSE(toolBox.rotateToolActive());
  }

  SECTION("assemble, clip, and path tools are reported as modal tools")
  {
    CHECK_FALSE(toolBox.anyModalToolActive());

    toolBox.toggleAssembleBrushTool();
    CHECK(toolBox.anyModalToolActive());
    toolBox.toggleAssembleBrushTool();
    CHECK_FALSE(toolBox.anyModalToolActive());

    toolBox.togglePathTool();
    CHECK(toolBox.anyModalToolActive());
    toolBox.togglePathTool();
    CHECK_FALSE(toolBox.anyModalToolActive());

    const auto builder =
      mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};
    auto* brushNode = new mdl::BrushNode{
      builder.createCuboid(vm::bbox3d{{-16, -16, -16}, {16, 16, 16}}, "material")
      | kdl::value()};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {brushNode}}});
    mdl::selectNodes(map, {brushNode});

    toolBox.toggleClipTool();
    CHECK(toolBox.anyModalToolActive());
  }

  SECTION("vertex tool coexists with transforms but not sweep")
  {
    const auto builder =
      mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};
    auto* brushNode = new mdl::BrushNode{
      builder.createCuboid(vm::bbox3d{{-16, -16, -16}, {16, 16, 16}}, "material")
      | kdl::value()};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {brushNode}}});
    mdl::selectNodes(map, {brushNode});

    toolBox.toggleVertexTool();
    toolBox.toggleRotateTool();
    CHECK(toolBox.vertexToolActive());
    CHECK(toolBox.rotateToolActive());

    toolBox.toggleRotateTool();
    const auto faceIndex = *brushNode->brush().findFace(vm::vec3d{1, 0, 0});
    mdl::deselectAll(map);
    mdl::selectBrushFaces(map, {{brushNode, faceIndex}});
    toolBox.toggleSweepTool();

    CHECK(toolBox.sweepToolActive());
    CHECK_FALSE(toolBox.vertexToolActive());
  }

  SECTION("sweep page exposes bridge controls for two selected faces")
  {
    const auto builder =
      mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};
    auto* sourceNode = new mdl::BrushNode{
      builder.createCuboid(vm::bbox3d{{-16, -16, -16}, {16, 16, 16}}, "source")
      | kdl::value()};
    auto* targetNode = new mdl::BrushNode{
      builder.createCuboid(vm::bbox3d{{80, -24, -12}, {112, 24, 12}}, "target")
      | kdl::value()};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {sourceNode, targetNode}}});
    const auto sourceFaceIndex = *sourceNode->brush().findFace(vm::vec3d{1, 0, 0});
    const auto targetFaceIndex = *targetNode->brush().findFace(vm::vec3d{-1, 0, 0});
    mdl::selectBrushFaces(
      map, {{sourceNode, sourceFaceIndex}, {targetNode, targetFaceIndex}});

    toolBox.toggleSweepTool();
    REQUIRE(toolBox.sweepToolActive());
    auto* page = dynamic_cast<SweepToolPage*>(bookCtrl.currentWidget());
    REQUIRE(page != nullptr);

    auto* mode = page->findChild<QComboBox*>(QStringLiteral("sweepMode"));
    REQUIRE(mode != nullptr);
    mode->setCurrentIndex(mode->findText("Bridge"));

    CHECK(toolBox.sweepTool().constructionMode() == SweepConstructionMode::Bridge);
    auto* iterations = page->findChild<QSpinBox*>(QStringLiteral("sweepIterations"));
    auto* swapEnds = page->findChild<QPushButton*>(QStringLiteral("sweepSwapEnds"));
    auto* reset = page->findChild<QPushButton*>(QStringLiteral("sweepReset"));
    REQUIRE(iterations != nullptr);
    REQUIRE(swapEnds != nullptr);
    REQUIRE(reset != nullptr);
    CHECK_FALSE(iterations->isEnabled());
    CHECK(swapEnds->isVisibleTo(page));
    CHECK(swapEnds->isEnabled());
    CHECK_FALSE(reset->isEnabled());

    mode->setCurrentIndex(mode->findText("Sweep"));
    CHECK(iterations->isEnabled());
    CHECK_FALSE(swapEnds->isVisibleTo(page));
    CHECK(reset->isEnabled());
  }

  SECTION("chamfer page applies edge chamfers and switches handle targets")
  {
    const auto builder =
      mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};
    auto* brushNode = new mdl::BrushNode{
      builder.createCuboid(vm::bbox3d{{-16, -16, -16}, {16, 16, 16}}, "material")
      | kdl::value()};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {brushNode}}});
    mdl::selectNodes(map, {brushNode});

    toolBox.toggleChamferTool();
    REQUIRE(toolBox.chamferToolActive());
    CHECK(map.editorContext().blockSelection());

    auto* page = dynamic_cast<ChamferToolPage*>(bookCtrl.currentWidget());
    REQUIRE(page != nullptr);

    auto* target = page->findChild<QComboBox*>(QStringLiteral("chamferTarget"));
    auto* distance = page->findChild<QDoubleSpinBox*>(QStringLiteral("chamferDistance"));
    auto* segments = page->findChild<QSpinBox*>(QStringLiteral("chamferSegments"));
    auto* apply = page->findChild<QPushButton*>(QStringLiteral("chamferApply"));
    REQUIRE(target != nullptr);
    REQUIRE(distance != nullptr);
    REQUIRE(segments != nullptr);
    REQUIRE(apply != nullptr);
    CHECK_FALSE(apply->isEnabled());

    const auto edge = mdl::EdgeHandle{vm::segment3d{{-16, -16, 16}, {16, -16, 16}}};
    const auto edgeHit =
      mdl::Hit{mdl::EdgeHandle::HandleHitType, 0.0, edge.position.center(), edge};
    toolBox.chamferTool().edgeTool().select({edgeHit}, false);
    REQUIRE(toolBox.chamferTool().hasPreview());

    distance->setValue(4.0);
    segments->setValue(2);
    CHECK((toolBox.chamferTool().parameters() == ChamferParameters{4.0, 2}));
    REQUIRE(apply->isEnabled());

    const auto oldFaceCount = brushNode->brush().faceCount();
    apply->click();
    CHECK(brushNode->brush().faceCount() == oldFaceCount + 2u);

    target->setCurrentIndex(target->findText("Vertices"));
    CHECK(toolBox.chamferTool().target() == ChamferTarget::Vertices);
    CHECK(segments->isHidden());
    CHECK(
      map.nodeHandles().allHandles<mdl::VertexHandle>().size()
      == brushNode->brush().vertexCount());

    toolBox.toggleChamferTool();
    CHECK_FALSE(toolBox.chamferToolActive());
    CHECK_FALSE(map.editorContext().blockSelection());
  }

  SECTION("create entity tool creates model entities from browser payloads")
  {
    setAssetEntityDefinitions(map);

    REQUIRE(toolBox.createEntityTool().canCreateModelEntity("models/player.mdl"));
    REQUIRE(toolBox.createEntityTool().createModelEntity("models/player.mdl"));
    toolBox.createEntityTool().commitEntity();

    const auto selectedEntities = selectedEntityNodes(map);
    REQUIRE(selectedEntities.size() == 1u);
    CHECK(selectedEntities.front()->entity().classname() == "cycler_sprite");
    REQUIRE(selectedEntities.front()->entity().property("model") != nullptr);
    CHECK(*selectedEntities.front()->entity().property("model") == "models/player.mdl");
  }

  SECTION("create entity tool creates sprite entities from browser payloads")
  {
    setAssetEntityDefinitions(map);

    REQUIRE(toolBox.createEntityTool().canCreateSpriteEntity("sprites/glow01.spr"));
    REQUIRE(toolBox.createEntityTool().createSpriteEntity("sprites/glow01.spr"));
    toolBox.createEntityTool().commitEntity();

    const auto selectedEntities = selectedEntityNodes(map);
    REQUIRE(selectedEntities.size() == 1u);
    CHECK(selectedEntities.front()->entity().classname() == "cycler_sprite");
    REQUIRE(selectedEntities.front()->entity().property("model") != nullptr);
    CHECK(*selectedEntities.front()->entity().property("model") == "sprites/glow01.spr");
  }

  SECTION("create entity tool creates sound entities from browser payloads")
  {
    setAssetEntityDefinitions(map);

    REQUIRE(toolBox.createEntityTool().canCreateSoundEntity("sound/ambience/hum.wav"));
    REQUIRE(toolBox.createEntityTool().createSoundEntity("sound/ambience/hum.wav"));
    toolBox.createEntityTool().commitEntity();

    const auto selectedEntities = selectedEntityNodes(map);
    REQUIRE(selectedEntities.size() == 1u);
    CHECK(selectedEntities.front()->entity().classname() == "ambient_generic");
    REQUIRE(selectedEntities.front()->entity().property("message") != nullptr);
    CHECK(
      *selectedEntities.front()->entity().property("message")
      == "sound/ambience/hum.wav");
  }
}

} // namespace tb::ui
