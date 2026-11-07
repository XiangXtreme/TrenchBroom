/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFocusEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPixmap>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QRubberBand>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include "base/PreferenceManager.h"
#include "gl/OrthographicCamera.h"
#include "gl/PerspectiveCamera.h"
#include "img/GoldSrcTexture.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/CompilationProfile.h"
#include "mdl/EditorContext.h"
#include "mdl/EntityDefinition.h"
#include "mdl/EntityDefinitionFileSpec.h"
#include "mdl/EntityNode.h"
#include "mdl/Grid.h"
#include "mdl/GroupNode.h"
#include "mdl/HitAdapter.h"
#include "mdl/HitFilter.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "mdl/Map_Assets.h"
#include "mdl/Map_Entities.h"
#include "mdl/Map_Geometry.h"
#include "mdl/Map_Groups.h"
#include "mdl/Map_NodeLocking.h"
#include "mdl/Map_NodeVisibility.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Picking.h"
#include "mdl/Map_Selection.h"
#include "mdl/PickResult.h"
#include "mdl/WorldNode.h"
#include "prefs/Preferences.h"
#include "project/ProjectStore.h"
#include "render/RenderTiming.h"
#include "ui/AppControllerFixture.h"
#include "ui/ClickableTitleBar.h"
#include "ui/CollapsibleTitledPanel.h"
#include "ui/CompilationDialog.h"
#include "ui/Inspector.h"
#include "ui/MapDocument.h"
#include "ui/MapEditorController.h"
#include "ui/MapEditorView.h"
#include "ui/MapView2D.h"
#include "ui/MapView3D.h"
#include "ui/MapViewToolBox.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/ProjectSession.h"
#include "ui/ProjectWindow.h"
#include "ui/QPathUtils.h"
#include "ui/TerrainChunks.h"
#include "ui/TerrainExport.h"
#include "ui/TerrainInspector.h"
#include "ui/TerrainTool.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace tb::ui
{
namespace
{

const auto GameName = std::string{"CompileWorkspace"};

const auto GameConfig = R"({
    "version": 9,
    "name": "CompileWorkspace",
    "icon": "Icon.png",
    "fileformats": [
        { "format": "Valve" }
    ],
    "filesystem": {
        "searchpath": "valve",
        "packageformat": { "extension": "pak", "format": "idpak" }
    },
    "materials": {
        "root": "textures",
        "extensions": [".D"],
        "palette": "gfx/palette.lmp",
        "attribute": "wad"
    },
    "entities": {
        "definitions": [],
        "defaultcolor": "0.6 0.6 0.6 1.0",
        "modelformats": [ "mdl" ]
    },
    "tags": {
        "brush": [],
        "brushface": []
    }
})";

// A real tool task that needs no compiler: it echoes the variables the tools would see,
// so the test can prove which map file a project compilation actually consumed.
#if defined(_WIN32)
const auto CompilationProfiles = R"({
    "version": 1,
    "profiles": [
        {
            "name": "Probe",
            "workdir": "${MAP_DIR_PATH}",
            "tasks": [
                {
                    "type": "tool",
                    "tool": "cmd.exe",
                    "parameters": "/c echo snapshot=${MAP_DIR_PATH} map=${MAP_FULL_NAME} base=${MAP_BASE_NAME}",
                    "treatNonZeroResultCodeAsError": true
                },
                {
                    "type": "export",
                    "target": "exported.map",
                    "stripTbProperties": true,
                    "entityToAdd": [ { "key": "classname", "value": "info_player_start" } ]
                }
            ]
        },
        {
            "name": "ProjectAware",
            "workdir": "${MAP_DIR_PATH}",
            "tasks": [
                {
                    "type": "tool",
                    "tool": "cmd.exe",
                    "parameters": "/c echo project=${PROJECT_PATH} name=${MAP_NAME}",
                    "treatNonZeroResultCodeAsError": true
                }
            ]
        },
        {
            "name": "Mixed",
            "workdir": "${MAP_DIR_PATH}",
            "tasks": [
                { "type": "tool", "tool": "cmd.exe", "parameters": "/c echo snapshot=${MAP_DIR_PATH} map=${MAP_FULL_NAME} base=${MAP_BASE_NAME} project=${PROJECT_PATH}", "treatNonZeroResultCodeAsError": true },
                { "type": "export", "target": "exported.map", "stripTbProperties": true, "entityToAdd": [ { "key": "classname", "value": "info_player_start" } ] }
            ]
        },
        {
            "name": "DisabledMarker",
            "workdir": "${MAP_DIR_PATH}",
            "tasks": [
                { "type": "tool", "enabled": false, "tool": "cmd.exe", "parameters": "/c echo ${PROJECT_PATH}" },
                { "type": "tool", "tool": "cmd.exe", "parameters": "/c echo snapshot=${MAP_DIR_PATH} map=${MAP_FULL_NAME} base=${MAP_BASE_NAME}", "treatNonZeroResultCodeAsError": true },
                { "type": "export", "target": "exported.map", "stripTbProperties": true, "entityToAdd": [ { "key": "classname", "value": "info_player_start" } ] }
            ]
        }
    ]
})";
#else
const auto CompilationProfiles = R"({
    "version": 1,
    "profiles": [
        {
            "name": "Probe",
            "workdir": "${MAP_DIR_PATH}",
            "tasks": [
                {
                    "type": "tool",
                    "tool": "/bin/echo",
                    "parameters": "snapshot=${MAP_DIR_PATH} map=${MAP_FULL_NAME} base=${MAP_BASE_NAME}",
                    "treatNonZeroResultCodeAsError": true
                },
                {
                    "type": "export",
                    "target": "exported.map",
                    "stripTbProperties": true,
                    "entityToAdd": [ { "key": "classname", "value": "info_player_start" } ]
                }
            ]
        },
        {
            "name": "ProjectAware",
            "workdir": "${MAP_DIR_PATH}",
            "tasks": [
                {
                    "type": "tool",
                    "tool": "/bin/echo",
                    "parameters": "project=${PROJECT_PATH} name=${MAP_NAME}",
                    "treatNonZeroResultCodeAsError": true
                }
            ]
        },
        {
            "name": "Mixed",
            "workdir": "${MAP_DIR_PATH}",
            "tasks": [
                { "type": "tool", "tool": "/bin/echo", "parameters": "snapshot=${MAP_DIR_PATH} map=${MAP_FULL_NAME} base=${MAP_BASE_NAME} project=${PROJECT_PATH}", "treatNonZeroResultCodeAsError": true },
                { "type": "export", "target": "exported.map", "stripTbProperties": true, "entityToAdd": [ { "key": "classname", "value": "info_player_start" } ] }
            ]
        },
        {
            "name": "DisabledMarker",
            "workdir": "${MAP_DIR_PATH}",
            "tasks": [
                { "type": "tool", "enabled": false, "tool": "/bin/echo", "parameters": "${PROJECT_PATH}" },
                { "type": "tool", "tool": "/bin/echo", "parameters": "snapshot=${MAP_DIR_PATH} map=${MAP_FULL_NAME} base=${MAP_BASE_NAME}", "treatNonZeroResultCodeAsError": true },
                { "type": "export", "target": "exported.map", "stripTbProperties": true, "entityToAdd": [ { "key": "classname", "value": "info_player_start" } ] }
            ]
        }
    ]
})";
#endif

template <typename T>
T require(Result<T> result)
{
  if (result.is_error())
    FAIL(std::get<Error>(result.error()).msg);
  return std::move(result).value();
}
void require(Result<void> result)
{
  if (result.is_error())
    FAIL(std::get<Error>(result.error()).msg);
}

ProjectWindow* findProjectWindow(const std::filesystem::path& path)
{
  for (auto* widget : QApplication::topLevelWidgets())
    if (auto* candidate = dynamic_cast<ProjectWindow*>(widget);
        candidate
        && candidate->session()->path() == std::filesystem::weakly_canonical(path))
      return candidate;
  return nullptr;
}

std::unique_ptr<TerrainRegion> testTerrainRegion(const mdl::GroupNode& group)
{
  return std::make_unique<TerrainRegion>(
    require(TerrainRegion::decode(group.group().terrainPaint())));
}

std::unique_ptr<TerrainChunks> testTerrainChunks(const mdl::GroupNode& group)
{
  return std::make_unique<TerrainChunks>(require(readTerrainChunks(group)));
}

// Supply both widget and window coordinates, as native Qt mouse events do.
// The viewport recorder deliberately uses window coordinates for full precision.
void terrainMouseEvent(
  MapViewBase* view,
  const QEvent::Type type,
  const Qt::MouseButton button,
  const Qt::KeyboardModifiers modifiers,
  const QPoint& position)
{
  const auto buttons = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
  auto event = QMouseEvent{
    type,
    QPointF{position},
    QPointF{view->mapTo(view->window(), position)},
    QPointF{view->mapToGlobal(position)},
    button,
    buttons,
    modifiers};
  QApplication::sendEvent(view, &event);
}
void terrainMouseClick(
  MapViewBase* view,
  const Qt::MouseButton button,
  const Qt::KeyboardModifiers modifiers,
  const QPoint& position)
{
  terrainMouseEvent(view, QEvent::MouseButtonPress, button, modifiers, position);
  terrainMouseEvent(view, QEvent::MouseButtonRelease, button, modifiers, position);
}

void testChunkedTerrainEditing(
  AppController& app,
  MapWindow* window,
  MapView3D* view,
  TerrainInspector* panel,
  const std::string& materialId)
{
  auto& map = window->document().map();
  auto* editMode = panel->findChild<QComboBox*>("TerrainInspector_EditMode");
  auto* drawNew = panel->findChild<QPushButton*>("TerrainInspector_DrawNew");
  auto* activate = panel->findChild<QPushButton*>("TerrainInspector_PaintTerrain");
  auto* terrainMaterial =
    panel->findChild<QComboBox*>("TerrainInspector_TerrainMaterial");
  auto* material = panel->findChild<QComboBox*>("TerrainInspector_SlotSelect");
  auto* radius = panel->findChild<QSpinBox*>("TerrainInspector_Radius");
  auto* newHeight = panel->findChild<QSpinBox*>("TerrainInspector_NewHeight");
  auto* newThickness = panel->findChild<QSpinBox*>("TerrainInspector_NewThickness");
  REQUIRE(editMode);
  REQUIRE(drawNew);
  REQUIRE(activate);
  REQUIRE(terrainMaterial);
  REQUIRE(material);
  REQUIRE(radius);
  REQUIRE(newHeight);
  REQUIRE(newThickness);
  auto* ancestor = view->parentWidget();
  while (ancestor && !qobject_cast<MapEditorView*>(ancestor))
    ancestor = ancestor->parentWidget();
  REQUIRE(ancestor);
  auto& editor = *qobject_cast<MapEditorView*>(ancestor);
  auto& controller = editor.controller();
  REQUIRE(controller.canToggleTerrainTool());
  REQUIRE_FALSE(window->toolBox().terrainToolActive());
  editor.showInspectorPage(InspectorPage::Terrain);
  REQUIRE_FALSE(window->toolBox().terrainToolActive());
  controller.toggleTerrainTool();
  REQUIRE(panel->drawingTerrain());
  REQUIRE(editMode->currentIndex() == 1);
  REQUIRE(newHeight->isVisible());
  REQUIRE(drawNew->isVisible());
  REQUIRE(panel->isVisible());
  controller.toggleTerrainTool();
  REQUIRE_FALSE(window->toolBox().terrainToolActive());

  editMode->setCurrentIndex(1);
  newHeight->setValue(0);
  newThickness->setValue(64);
  {
    auto top =
      MapView2D{app, window->document(), window->toolBox(), MapView2D::ViewPlane::XY};
    top.setParent(ancestor);
    top.resize(400, 400);
    top.show();
    QApplication::processEvents();
    auto& camera = top.camera();
    camera.setViewport({0, 0, top.width(), top.height()});
    camera.setZoom(0.2f);
    auto position = camera.position();
    position[0] = 1728;
    camera.moveTo(position);
    const auto screen = [&](const vm::vec3f& p) {
      const auto projected = camera.project(p);
      return QPoint{
        int(std::lround(projected.x())),
        int(std::lround(float(top.height()) - projected.y()))};
    };
    const auto first = screen({1024, -64, 0});
    const auto second = screen({2432, 64, 0});
    REQUIRE(top.rect().contains(first));
    REQUIRE(top.rect().contains(second));
    drawNew->click();
    REQUIRE(panel->drawingTerrain());
    terrainMouseEvent(
      &top, QEvent::MouseButtonPress, Qt::LeftButton, Qt::NoModifier, first);
    top.grabFramebuffer();
    terrainMouseEvent(&top, QEvent::MouseMove, Qt::NoButton, Qt::NoModifier, second);
    top.grabFramebuffer();
    QTest::qWait(110); // Pass the editor's accidental-drag threshold.
    terrainMouseEvent(
      &top, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoModifier, second);
    top.grabFramebuffer();
    REQUIRE_FALSE(panel->drawingTerrain());
    top.hide();
  }
  REQUIRE(map.selection().groups.size() == 1);
  auto* group = map.selection().groups.front();
  REQUIRE(isTerrainChunkGroup(*group));
  auto initial = testTerrainChunks(*group);
  REQUIRE(initial->chunks.size() == 2);
  const auto seam = initial->combined->heightField->index(20, 1);
  auto& camera = static_cast<MapViewBase&>(*view).camera();
  camera.setViewport({0, 0, view->width(), view->height()});
  camera.moveTo({2304, 0, 512});
  camera.lookAt({2304, 0, 0}, {0, 1, 0});
  const auto point = view->rect().center();
  radius->setValue(128);

  editMode->setCurrentIndex(0);
  const auto materialIndex =
    terrainMaterial->findData(QString::fromStdString(materialId));
  REQUIRE(materialIndex > 0);
  terrainMaterial->setCurrentIndex(materialIndex);
  material->setCurrentIndex(1);
  REQUIRE(activate->isEnabled());
  REQUIRE(window->toolBox().canToggleRotateTool());
  window->toolBox().toggleRotateTool();
  REQUIRE(window->toolBox().rotateToolActive());
  activate->click();
  REQUIRE(panel->painting());
  REQUIRE(window->toolBox().anyModalToolActive());
  REQUIRE_FALSE(window->toolBox().rotateToolActive());
  window->toolBox().toggleRotateTool();
  REQUIRE(window->toolBox().rotateToolActive());
  REQUIRE_FALSE(panel->painting());
  window->toolBox().toggleRotateTool();
  activate->click();
  REQUIRE(panel->painting());
  REQUIRE(testTerrainChunks(*group)->chunks[0].region.materialAssetId == materialId);
  REQUIRE(selectedTerrainGroup(map) == group);
  // The first click activates the viewport after using the terrain inspector.
  terrainMouseClick(view, Qt::LeftButton, Qt::NoModifier, point);
  view->grabFramebuffer();
  terrainMouseEvent(
    view, QEvent::MouseButtonPress, Qt::LeftButton, Qt::NoModifier, point);
  view->grabFramebuffer();
  REQUIRE(panel->stroking());
  QTest::qWait(110); // Pass the editor's accidental-drag threshold.
  terrainMouseEvent(
    view, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoModifier, point);
  view->grabFramebuffer();
  auto painted = testTerrainChunks(*group);
  CHECK(painted->chunks[0].region.weights != initial->chunks[0].region.weights);
  CHECK(painted->chunks[1].region.weights != initial->chunks[1].region.weights);
  activate->click();

  // Brush entities inside individual chunks must still be pickable by terrain tools.
  mdl::openGroup(map, *group);
  const auto detailDefinition = mdl::EntityDefinition{
    "func_detail", RgbB{255, 255, 255}, "Detail brushes", {}, std::nullopt};
  for (auto* child : group->children())
  {
    auto* chunk = dynamic_cast<mdl::GroupNode*>(child);
    REQUIRE(chunk);
    mdl::openGroup(map, *chunk);
    mdl::selectNodes(map, chunk->children());
    REQUIRE(mdl::createBrushEntity(map, detailDefinition));
    mdl::closeGroup(map);
  }
  mdl::closeGroup(map);
  mdl::selectNodes(map, {group});
  material->setCurrentIndex(2);
  REQUIRE(activate->isEnabled());
  activate->click();
  terrainMouseEvent(
    view, QEvent::MouseButtonPress, Qt::LeftButton, Qt::NoModifier, point);
  view->grabFramebuffer();
  REQUIRE(panel->stroking());
  QTest::qWait(110); // Pass the editor's accidental-drag threshold.
  terrainMouseEvent(
    view, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoModifier, point);
  view->grabFramebuffer();
  const auto repainted = testTerrainChunks(*group);
  CHECK(repainted->chunks[0].region.weights != painted->chunks[0].region.weights);
  CHECK(repainted->chunks[1].region.weights != painted->chunks[1].region.weights);
  painted = testTerrainChunks(*group);
  activate->click();

  editMode->setCurrentIndex(1);
  REQUIRE(activate->isEnabled());
  activate->click();
  window->toolBox().setSelectMode(true);
  editMode->setCurrentIndex(0);
  editor.toggleInspector();
  controller.toggleTerrainTool();
  REQUIRE(window->toolBox().terrainTool().mode() == TerrainToolMode::Sculpt);
  REQUIRE(editMode->currentIndex() == 1);
  REQUIRE(activate->isChecked());
  REQUIRE(activate->isEnabled());
  REQUIRE(panel->isVisible());
  terrainMouseEvent(
    view, QEvent::MouseButtonPress, Qt::LeftButton, Qt::NoModifier, point);
  view->grabFramebuffer();
  REQUIRE(panel->stroking());
  QTest::qWait(110); // Pass the editor's accidental-drag threshold.
  terrainMouseEvent(
    view, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoModifier, point);
  view->grabFramebuffer();
  auto sculpted = testTerrainChunks(*group);
  CHECK(
    sculpted->combined->heightField->heights[seam]
    > initial->combined->heightField->heights[seam]);
  CHECK(sculpted->chunks[0].region.weights == painted->chunks[0].region.weights);
  CHECK(sculpted->chunks[1].region.weights == painted->chunks[1].region.weights);
  activate->click();

  editMode->setCurrentIndex(2);
  activate->click();
  const auto vertex =
    camera.project({2304, 0, float(sculpted->combined->heightField->heights[seam])});
  const auto handle = QPoint{
    int(std::lround(vertex.x())), int(std::lround(float(view->height()) - vertex.y()))};
  terrainMouseEvent(
    view, QEvent::MouseButtonPress, Qt::LeftButton, Qt::NoModifier, handle);
  view->grabFramebuffer();
  REQUIRE(panel->vertexInteracting());
  terrainMouseEvent(
    view, QEvent::MouseMove, Qt::NoButton, Qt::NoModifier, handle + QPoint{0, -80});
  view->grabFramebuffer();
  QTest::qWait(110); // Pass the editor's accidental-drag threshold.
  terrainMouseEvent(
    view,
    QEvent::MouseButtonRelease,
    Qt::LeftButton,
    Qt::NoModifier,
    handle + QPoint{0, -80});
  view->grabFramebuffer();
  auto edited = testTerrainChunks(*group);
  CHECK(
    edited->combined->heightField->heights[seam]
    > sculpted->combined->heightField->heights[seam]);
  CHECK(edited->chunks[0].region.weights == painted->chunks[0].region.weights);
  CHECK(edited->chunks[1].region.weights == painted->chunks[1].region.weights);

  auto directory = QTemporaryDir{};
  REQUIRE(directory.isValid());
  const auto path = pathFromQString(directory.filePath("chunked-terrain.map"));
  require(map.saveTo(path));
  auto file = QFile{pathAsQString(path)};
  REQUIRE(file.open(QIODevice::ReadOnly));
  const auto bytes = file.readAll();
  const auto project = app.projectFor(window->document());
  REQUIRE(project);
  const auto snapshot = require(bakeTerrainSnapshot(
    std::vector<std::uint8_t>{bytes.begin(), bytes.end()}, &project->store()));
  REQUIRE(snapshot);
  CHECK_FALSE(snapshot->textures.empty());
  const auto exported = std::string{snapshot->map.begin(), snapshot->map.end()};
  CHECK(exported.find(terrainChunkGroupMarker()) == std::string::npos);
  map.undoCommand();
  CHECK(
    testTerrainChunks(*group)->combined->heightField->heights[seam]
    == sculpted->combined->heightField->heights[seam]);
  activate->click();
}

void testTerrainVertexEditing(
  AppController& app,
  MapWindow* window,
  MapView3D* view,
  TerrainInspector* panel,
  mdl::GroupNode* group,
  QComboBox* editMode,
  QPushButton* activate,
  gl::Camera& camera,
  const TerrainRegion& sculpted)
{
  auto& map = window->document().map();
  // Terrain vertices are manipulated through the height field in both views,
  // and each drag is one undoable operation that retains the paint.
  editMode->setCurrentIndex(2);
  activate->click();
  REQUIRE(panel->vertexEditing());
  const auto vertexIndex = sculpted.heightField->index(2, 2);
  const auto centerVertex =
    vm::vec3f{0, 0, float(sculpted.heightField->heights[vertexIndex])};
  const auto projected = camera.project(centerVertex);
  const auto vertexPosition = QPoint{
    int(std::lround(projected.x())),
    int(std::lround(float(view->height()) - projected.y()))};
  terrainMouseEvent(
    view, QEvent::MouseButtonPress, Qt::LeftButton, Qt::NoModifier, vertexPosition);
  view->grabFramebuffer();
  REQUIRE(panel->vertexInteracting());
  const auto raisedPosition = vertexPosition + QPoint{0, -80};
  terrainMouseEvent(
    view, QEvent::MouseMove, Qt::NoButton, Qt::NoModifier, raisedPosition);
  view->grabFramebuffer();
  QTest::qWait(110); // Pass the editor's accidental-drag threshold.
  terrainMouseEvent(
    view, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoModifier, raisedPosition);
  view->grabFramebuffer();
  auto vertexEdited = testTerrainRegion(*group);
  REQUIRE(vertexEdited->heightField);
  CHECK(
    vertexEdited->heightField->heights[vertexIndex]
    > sculpted.heightField->heights[vertexIndex]);
  CHECK(vertexEdited->weights == sculpted.weights);
  CHECK(terrainSurfaces(*group, *vertexEdited).is_success());
  map.undoCommand();
  CHECK(testTerrainRegion(*group)->heightField->heights == sculpted.heightField->heights);
  map.redoCommand();
  CHECK(
    testTerrainRegion(*group)->heightField->heights
    == vertexEdited->heightField->heights);
  map.undoCommand();
  terrainMouseEvent(
    view, QEvent::MouseButtonPress, Qt::LeftButton, Qt::NoModifier, vertexPosition);
  view->grabFramebuffer();
  terrainMouseEvent(
    view, QEvent::MouseMove, Qt::NoButton, Qt::NoModifier, raisedPosition);
  view->grabFramebuffer();
  QTest::keyClick(view, Qt::Key_Escape);
  view->grabFramebuffer();
  CHECK_FALSE(panel->vertexInteracting());
  CHECK(testTerrainRegion(*group)->heightField->heights == sculpted.heightField->heights);

  auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, map.worldBounds()};
  const auto coverCenter = (vm::vec3d{camera.position()} + vm::vec3d{centerVertex}) / 2.0;
  auto* cover = new mdl::BrushNode{require(builder.createCuboid(
    vm::bbox3d{coverCenter - vm::vec3d{32, 32, 32}, coverCenter + vm::vec3d{32, 32, 32}},
    "grass"))};
  mdl::addNodes(map, {{map.editorContext().currentLayer(), {cover}}});
  auto* through = panel->findChild<QCheckBox*>("TerrainInspector_SelectOccluded");
  REQUIRE(through);
  panel->beginVertexInteraction(
    camera, *view, vertexPosition, false, mdl::PickResult::byDistance());
  panel->moveVertexInteraction(raisedPosition);
  panel->endVertexInteraction(camera, raisedPosition, true);
  CHECK(testTerrainRegion(*group)->heightField->heights == sculpted.heightField->heights);
  through->setChecked(true);
  panel->beginVertexInteraction(
    camera, *view, vertexPosition, false, mdl::PickResult::byDistance());
  panel->moveVertexInteraction(raisedPosition);
  panel->endVertexInteraction(camera, raisedPosition, true);
  CHECK(
    testTerrainRegion(*group)->heightField->heights[vertexIndex]
    > sculpted.heightField->heights[vertexIndex]);
  map.undoCommand();
  map.undoCommand(); // Remove the occluder.
  through->setChecked(false);

  auto* soft = panel->findChild<QCheckBox*>("TerrainInspector_SoftSelection");
  auto* softRadius = panel->findChild<QSpinBox*>("TerrainInspector_SelectionRadius");
  REQUIRE(soft);
  REQUIRE(softRadius);
  softRadius->setValue(128);
  soft->setChecked(true);
  terrainMouseEvent(
    view, QEvent::MouseButtonPress, Qt::LeftButton, Qt::NoModifier, vertexPosition);
  view->grabFramebuffer();
  REQUIRE(panel->vertexInteracting());
  const auto capturePath = qEnvironmentVariable("TB_TERRAIN_VERTEX_CAPTURE_PATH");
  if (!capturePath.isEmpty())
    REQUIRE(view->grabFramebuffer().save(capturePath));
  terrainMouseEvent(
    view, QEvent::MouseMove, Qt::NoButton, Qt::NoModifier, raisedPosition);
  view->grabFramebuffer();
  QTest::qWait(110);
  terrainMouseEvent(
    view, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoModifier, raisedPosition);
  view->grabFramebuffer();
  const auto softened = testTerrainRegion(*group);
  const auto centerDelta = softened->heightField->heights[vertexIndex]
                           - sculpted.heightField->heights[vertexIndex];
  CHECK(centerDelta > 0.0);
  const auto neighbor = sculpted.heightField->index(3, 2);
  const auto neighborDelta =
    softened->heightField->heights[neighbor] - sculpted.heightField->heights[neighbor];
  CHECK(neighborDelta > 0.0);
  CHECK(neighborDelta < centerDelta);
  CHECK(softened->heightField->heights[0] == sculpted.heightField->heights[0]);
  CHECK(softened->weights == sculpted.weights);
  map.undoCommand();
  CHECK(testTerrainRegion(*group)->heightField->heights == sculpted.heightField->heights);
  map.redoCommand();
  CHECK(
    testTerrainRegion(*group)->heightField->heights == softened->heightField->heights);
  map.undoCommand();
  terrainMouseEvent(
    view, QEvent::MouseButtonPress, Qt::LeftButton, Qt::NoModifier, vertexPosition);
  view->grabFramebuffer();
  terrainMouseEvent(
    view, QEvent::MouseMove, Qt::NoButton, Qt::NoModifier, raisedPosition);
  view->grabFramebuffer();
  QTest::keyClick(view, Qt::Key_Escape);
  view->grabFramebuffer();
  CHECK_FALSE(panel->vertexInteracting());
  CHECK(testTerrainRegion(*group)->heightField->heights == sculpted.heightField->heights);
  soft->setChecked(false);

  auto* vertexAncestor = view->parentWidget();
  while (vertexAncestor && !qobject_cast<MapEditorView*>(vertexAncestor))
    vertexAncestor = vertexAncestor->parentWidget();
  REQUIRE(vertexAncestor);
  {
    auto side =
      MapView2D{app, window->document(), window->toolBox(), MapView2D::ViewPlane::XZ};
    side.setParent(vertexAncestor);
    side.resize(400, 400);
    side.show();
    QApplication::processEvents();
    auto& sideCamera = side.camera();
    sideCamera.setViewport({0, 0, side.width(), side.height()});
    const auto screen = [&](const vm::vec3f& p) {
      const auto result = sideCamera.project(p);
      return QPoint{
        int(std::lround(result.x())),
        int(std::lround(float(side.height()) - result.y()))};
    };
    auto selectedBounds = QRect{};
    for (auto row = 1; row < 4; ++row)
      for (auto column = 1; column < 4; ++column)
      {
        const auto i = sculpted.heightField->index(column, row);
        const auto p = screen(
          {float(-128 + column * 64),
           float(128 - row * 64),
           float(sculpted.heightField->heights[i])});
        selectedBounds |= QRect{p, QSize{1, 1}};
      }
    selectedBounds.adjust(-12, -12, 12, 12);
    REQUIRE(side.rect().contains(selectedBounds));
    terrainMouseEvent(
      &side,
      QEvent::MouseButtonPress,
      Qt::LeftButton,
      Qt::NoModifier,
      selectedBounds.topLeft());
    side.grabFramebuffer();
    terrainMouseEvent(
      &side,
      QEvent::MouseMove,
      Qt::NoButton,
      Qt::NoModifier,
      selectedBounds.bottomRight());
    side.grabFramebuffer();
    QTest::qWait(110); // Pass the editor's accidental-drag threshold.
    terrainMouseEvent(
      &side,
      QEvent::MouseButtonRelease,
      Qt::LeftButton,
      Qt::NoModifier,
      selectedBounds.bottomRight());
    side.grabFramebuffer();
    const auto handle = screen(centerVertex);
    terrainMouseEvent(
      &side, QEvent::MouseButtonPress, Qt::LeftButton, Qt::NoModifier, handle);
    side.grabFramebuffer();
    REQUIRE(panel->vertexInteracting());
    terrainMouseEvent(
      &side, QEvent::MouseMove, Qt::NoButton, Qt::NoModifier, handle + QPoint{0, -32});
    side.grabFramebuffer();
    QTest::qWait(110); // Pass the editor's accidental-drag threshold.
    terrainMouseEvent(
      &side,
      QEvent::MouseButtonRelease,
      Qt::LeftButton,
      Qt::NoModifier,
      handle + QPoint{0, -32});
    side.grabFramebuffer();
    vertexEdited = testTerrainRegion(*group);
    auto changed = 0;
    for (auto i = size_t{0}; i < vertexEdited->heightField->heights.size(); ++i)
      if (vertexEdited->heightField->heights[i] != sculpted.heightField->heights[i])
        ++changed;
    CHECK(changed >= 3);
    CHECK(vertexEdited->weights == sculpted.weights);
    CHECK(terrainSurfaces(*group, *vertexEdited).is_success());
    map.undoCommand();
    CHECK(
      testTerrainRegion(*group)->heightField->heights == sculpted.heightField->heights);
    side.hide();
  }
  activate->click();
  editMode->setCurrentIndex(1);
}

} // namespace

namespace
{
// TerrainRegion carries an inline paint grid. Keep the long framebuffer scenario's
// snapshots on the heap so nested Qt layout events retain enough Windows stack.
std::unique_ptr<TerrainRegion> terrainRegionSnapshot(const mdl::GroupNode& group)
{
  return std::make_unique<TerrainRegion>(
    require(TerrainRegion::decode(group.group().terrainPaint())));
}

void testTerrainPanelInteraction(MapWindow* window, TerrainInspector* panel)
{
  auto* mode = panel->findChild<QComboBox*>("TerrainInspector_EditMode");
  auto* sculpt = panel->findChild<QComboBox*>("TerrainInspector_SculptMode");
  auto* activate = panel->findChild<QPushButton*>("TerrainInspector_PaintTerrain");
  auto* radius = panel->findChild<QSpinBox*>("TerrainInspector_Radius");
  auto* strength = panel->findChild<QSpinBox*>("TerrainInspector_Strength");
  auto* hardness = panel->findChild<QSpinBox*>("TerrainInspector_Hardness");
  auto* soft = panel->findChild<QCheckBox*>("TerrainInspector_SoftSelection");
  auto* lock = panel->findChild<QCheckBox*>("TerrainInspector_LockEdges");
  auto* help = panel->findChild<QLabel*>("TerrainInspector_Help");
  auto* compile =
    panel->findChild<CollapsibleTitledPanel*>("TerrainInspector_Compilation");
  REQUIRE(mode);
  REQUIRE(sculpt);
  REQUIRE(activate);
  REQUIRE(radius);
  REQUIRE(strength);
  REQUIRE(hardness);
  REQUIRE(soft);
  REQUIRE(lock);
  REQUIRE(help);
  REQUIRE(compile);
  REQUIRE_FALSE(compile->expanded());

  mode->setCurrentIndex(0);
  REQUIRE_FALSE(window->toolBox().terrainToolActive());
  const auto paintRadius = radius->value();
  const auto paintStrength = strength->value();
  const auto paintHardness = hardness->value();
  radius->setValue(80);
  strength->setValue(85);
  hardness->setValue(45);
  mode->setCurrentIndex(1);
  CHECK_FALSE(window->toolBox().terrainToolActive());

  // Every sculpt operation has an independent brush, and editing switches directly.
  activate->click();
  CHECK(activate->text() == "Finish Editing");
  for (auto i = 0; i < sculpt->count(); ++i)
  {
    sculpt->setCurrentIndex(i);
    radius->setValue(100 + i * 10);
    strength->setValue(20 + i);
    hardness->setValue(30 + i);
  }
  for (auto i = 0; i < sculpt->count(); ++i)
  {
    sculpt->setCurrentIndex(i);
    CHECK(radius->value() == 100 + i * 10);
    CHECK(strength->value() == 20 + i);
    CHECK(hardness->value() == 30 + i);
    CHECK(help->text().contains(sculpt->currentText()));
  }
  mode->setCurrentIndex(0);
  CHECK(window->toolBox().terrainTool().mode() == TerrainToolMode::Paint);
  CHECK(radius->value() == 80);
  CHECK(strength->value() == 85);
  CHECK(hardness->value() == 45);
  mode->setCurrentIndex(2);
  CHECK(window->toolBox().terrainTool().mode() == TerrainToolMode::Vertices);
  soft->setChecked(true);
  lock->setChecked(false);
  lock->setChecked(true); // Refreshing the panel must keep the soft selection help.
  CHECK(help->text().contains("sphere"));
  compile->expand();
  CHECK_FALSE(panel->findChild<QCheckBox*>("TerrainInspector_BrushEntity")->isHidden());
  CHECK_FALSE(panel->findChild<QSpinBox*>("TerrainInspector_FaceBudget")->isHidden());
  {
    const auto minWidth = panel->minimumWidth();
    const auto maxWidth = panel->maximumWidth();
    const auto restoreWidth = qScopeGuard([&] {
      panel->setMinimumWidth(minWidth);
      panel->setMaximumWidth(maxWidth);
      QApplication::processEvents();
    });
    panel->setFixedWidth(320);
    QApplication::processEvents();
    auto* scroll = panel->findChild<QScrollArea*>("TerrainInspector_Scroll");
    REQUIRE(scroll);
    CHECK(scroll->horizontalScrollBar()->maximum() == 0);
    auto* creation =
      panel->findChild<CollapsibleTitledPanel*>("TerrainInspector_Creation");
    auto* editing = panel->findChild<CollapsibleTitledPanel*>("TerrainInspector_Editing");
    REQUIRE(creation);
    REQUIRE(editing);
    for (auto cycle = 0; cycle < 12; ++cycle)
    {
      panel->setFixedWidth(cycle % 2 == 0 ? 320 : 480);
      // Mode changes also show/hide form rows while their section is collapsed.
      mode->setCurrentIndex(cycle % 3);
      for (auto* section : {creation, editing, compile})
      {
        section->expand();
        auto* bar = section->findChild<ClickableTitleBar*>();
        REQUIRE(bar);
        QTest::mouseClick(bar, Qt::LeftButton);
        REQUIRE_FALSE(section->expanded());
      }
      REQUIRE(QTest::qWaitFor(
        [&] {
          return std::ranges::all_of(
            std::initializer_list{creation, editing, compile}, [](auto* section) {
              return section->height()
                     == section->template findChild<ClickableTitleBar*>()->height();
            });
        },
        2000));
    }
    panel->setFixedWidth(320);
    mode->setCurrentIndex(2);
    creation->collapse();
    editing->collapse();
    compile->collapse();
    REQUIRE(QTest::qWaitFor(
      [&] { return compile->height() <= compile->sizeHint().height(); }, 2000));
    const auto compactHeight = compile->height();
    CHECK(compactHeight <= compile->sizeHint().height());
    CHECK(editing->height() == compactHeight);
    CHECK(creation->height() == compactHeight);
    // Test the user's all-collapsed case as well as expanding and collapsing again.
    CHECK(compile->geometry().bottom() < scroll->viewport()->height() / 2);
    const auto capturePath = qEnvironmentVariable("TB_TERRAIN_PANEL_CAPTURE_PATH");
    if (!capturePath.isEmpty())
      REQUIRE(panel->grab().save(capturePath + ".collapsed.png"));
    editing->expand();
    compile->expand();
    QApplication::processEvents();
    CHECK(creation->height() == compactHeight);
    CHECK(scroll->horizontalScrollBar()->maximum() == 0);
    if (!capturePath.isEmpty())
      REQUIRE(panel->grab().save(capturePath));
  }
  compile->collapse();
  soft->setChecked(false);
  mode->setCurrentIndex(1);
  CHECK(window->toolBox().terrainTool().mode() == TerrainToolMode::Sculpt);
  activate->click();
  CHECK_FALSE(window->toolBox().terrainToolActive());
  // Restore the brushes used by the framebuffer scenario below.
  for (auto i = 0; i < sculpt->count(); ++i)
  {
    sculpt->setCurrentIndex(i);
    radius->setValue(60);
    strength->setValue(25);
    hardness->setValue(35);
  }
  sculpt->setCurrentIndex(0);
  mode->setCurrentIndex(0);
  radius->setValue(paintRadius);
  strength->setValue(paintStrength);
  hardness->setValue(paintHardness);
  mode->setCurrentIndex(1);
}
} // namespace

TEST_CASE("Project terrain panel paints a picked brush")
{
  if (!qEnvironmentVariableIsSet("TB_TERRAIN_PREVIEW_TEST_CHILD"))
  {
    // The ordinary test process does not share GL contexts. Run this framebuffer
    // test in a fresh process with the same sharing policy as the application.
    auto process = QProcess{};
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("TB_TEST_SHARED_OPENGL", "1");
    environment.insert("TB_TERRAIN_PREVIEW_TEST_CHILD", "1");
    process.setProcessEnvironment(environment);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(
      QCoreApplication::applicationFilePath(),
      {"Project terrain panel paints a picked brush", "--reporter", "compact"});
    REQUIRE(process.waitForStarted(10000));
    const auto finished = process.waitForFinished(55000);
    INFO(process.readAllStandardOutput().toStdString());
    REQUIRE(finished);
    REQUIRE(process.exitStatus() == QProcess::NormalExit);
    REQUIRE(process.exitCode() == 0);
    return;
  }
  auto directory = QTemporaryDir{};
  REQUIRE(directory.isValid());
  const auto path = pathFromQString(directory.filePath("terrain-panel.tbproj"));
  require(ProjectSession::create(path, "Terrain Panel", "Half-Life"));
  auto fixture = AppControllerFixture{[](const auto& writeGameConfig) {
    auto config = std::string{GameConfig};
    config.replace(config.find(GameName), GameName.size(), "Half-Life");
    writeGameConfig("Half-Life", config, std::nullopt, std::nullopt);
  }};
  auto& app = fixture.appController();
  REQUIRE(app.openProject(path));
  auto* project = findProjectWindow(path);
  REQUIRE(project);
  const auto colors = std::array{
    std::array<unsigned char, 3>{30, 210, 40},
    std::array<unsigned char, 3>{210, 35, 30},
    std::array<unsigned char, 3>{30, 40, 210}};
  auto textures = std::array<std::string, 3>{};
  for (auto slot = size_t{0}; slot < colors.size(); ++slot)
  {
    auto pixels = std::vector<unsigned char>(64 * 64 * 4, 255);
    for (auto pixel = size_t{0}; pixel < 64 * 64; ++pixel)
      for (auto channel = size_t{0}; channel < 3; ++channel)
        pixels[pixel * 4 + channel] = colors[slot][channel];
    const auto name = "terrain_slot" + std::to_string(slot);
    const auto converted =
      require(img::convertGoldSrcTexture({64, 64, std::move(pixels)}, name));
    auto input = project::AssetImport{};
    input.type = project::AssetType::Texture;
    input.virtualPath = "textures/" + name + ".wad";
    input.displayName = "Terrain texture";
    input.original = input.compatible = converted.wad;
    textures[slot] = require(project->session()->importAsset(input)).id;
  }
  const auto json =
    std::string{R"({"format":"goldsrc-terrain-material","version":1,"slots":[)"
                R"({"name":"Moss"},{"name":"Sand"},{"name":"Stone"}]})"};
  auto materialInput = project::AssetImport{};
  materialInput.type = project::AssetType::Material;
  materialInput.virtualPath = "materials/test.tbmat";
  materialInput.displayName = "Test blend";
  materialInput.original.assign(json.begin(), json.end());
  materialInput.compatible = materialInput.original;
  const auto capturedMaterial = require(project->session()->importAsset(materialInput));
  {
    auto store = require(project::ProjectStore::open(project->session()->store().path()));
    for (auto slot = 0; slot < 3; ++slot)
      require(store->addDependency(
        capturedMaterial.id,
        textures[size_t(slot)],
        "terrain-slot-" + std::to_string(slot)));
  }
  require(app.mapWindowManager().createProjectMap(
    project->session(), "Terrain", mdl::MapFormat::Valve));
  auto* window = app.mapWindowManager().topMapWindow();
  REQUIRE(window);
  auto& map = window->document().map();
  mdl::selectAllNodes(map);
  mdl::removeSelectedNodes(map);
  auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, map.worldBounds()};
  auto* brush = new mdl::BrushNode{
    require(builder.createCuboid(vm::bbox3d{{-128, -128, -64}, {128, 128, 0}}, "grass"))};
  mdl::addNodes(map, {{map.editorContext().currentLayer(), {brush}}});
  mdl::selectNodes(map, {brush});
  auto* group = mdl::groupSelectedNodes(map, "Terrain");
  REQUIRE(group);
  auto* navigation = window->findChild<QToolButton*>("Inspector_NavigationTerrain");
  REQUIRE(navigation);
  project->resize(1200, 820);
  project->show();
  window->switchToInspectorPage(InspectorPage::Terrain);
  navigation->click();
  auto* panel = window->findChild<TerrainInspector*>();
  REQUIRE(panel);
  auto* activate = panel->findChild<QPushButton*>("TerrainInspector_PaintTerrain");
  REQUIRE(activate);
  const auto controls = panel->findChildren<QPushButton*>();
  const auto button = [&](const QString& text) {
    const auto found = std::ranges::find_if(
      controls, [&](const auto* candidate) { return candidate->text() == text; });
    REQUIRE(found != controls.end());
    return *found;
  };
  REQUIRE(button("Create Terrain Region")->isEnabled());
  button("Create Terrain Region")->click();
  auto* terrainMaterial =
    panel->findChild<QComboBox*>("TerrainInspector_TerrainMaterial");
  REQUIRE(terrainMaterial);
  const auto materialIndex =
    terrainMaterial->findData(QString::fromStdString(capturedMaterial.id));
  REQUIRE(materialIndex > 0);
  terrainMaterial->setCurrentIndex(materialIndex);
  CHECK(
    require(TerrainRegion::decode(group->group().terrainPaint())).materialAssetId
    == capturedMaterial.id);
  CHECK(panel->findChild<QSpinBox*>("TerrainInspector_Strength"));
  CHECK(panel->findChild<QSpinBox*>("TerrainInspector_Hardness"));
  auto* faceBudget = panel->findChild<QSpinBox*>("TerrainInspector_FaceBudget");
  auto* wadBudget = panel->findChild<QSpinBox*>("TerrainInspector_WadBudget");
  auto* minWidth = panel->findChild<QSpinBox*>("TerrainInspector_MinFaceWidth");
  auto* density = panel->findChild<QComboBox*>("TerrainInspector_PatchDensity");
  REQUIRE(faceBudget);
  REQUIRE(wadBudget);
  REQUIRE(minWidth);
  REQUIRE(density);
  faceBudget->setValue(20);
  wadBudget->setValue(128);
  minWidth->setValue(12);
  density->setCurrentIndex(density->findData(200));
  CHECK(
    require(TerrainRegion::decode(group->group().terrainPaint())).maxAddedFaces == 20);
  CHECK(require(TerrainRegion::decode(group->group().terrainPaint())).maxWadKiB == 128);
  CHECK(require(TerrainRegion::decode(group->group().terrainPaint())).minFaceWidth == 12);
  CHECK(
    require(TerrainRegion::decode(group->group().terrainPaint())).patchDensityPercent
    == 200);
  button("Analyze Terrain")->click();
  const auto* estimate = panel->findChild<QLabel*>("TerrainInspector_Estimate");
  auto* showCuts = panel->findChild<QCheckBox*>("TerrainInspector_ShowCuts");
  REQUIRE(estimate);
  REQUIRE(showCuts);
  CHECK(estimate->text().contains("WAD:"));
  CHECK(estimate->text().contains("Narrowest top face:"));
  CHECK(estimate->text().contains("Base:"));
  CHECK(showCuts->isEnabled());
  CHECK(showCuts->isChecked());
  const auto analysis = estimate->text();
  auto* modes = panel->findChild<QComboBox*>("TerrainInspector_EditMode");
  REQUIRE(modes);
  modes->setCurrentIndex(1);
  modes->setCurrentIndex(0);
  CHECK(estimate->text() == analysis);
  CHECK(showCuts->isEnabled());
  CHECK(showCuts->isChecked());
  wadBudget->setValue(0);
  CHECK_FALSE(showCuts->isEnabled());
  CHECK_FALSE(showCuts->isChecked());
  CHECK(estimate->text().contains("Analyze again"));
  button("Analyze Terrain")->click();
  CHECK(estimate->text().contains("exceeds the configured limit"));
  faceBudget->setValue(-1);
  wadBudget->setValue(-1);
  minWidth->setValue(0);
  density->setCurrentIndex(density->findData(100));
  const auto initial = group->group().terrainPaint();
  REQUIRE_FALSE(initial.empty());
  auto* material = panel->findChild<QComboBox*>("TerrainInspector_SlotSelect");
  if (!material)
    material = panel->findChild<QComboBox*>();
  REQUIRE(material);
  CHECK(material->itemText(0).contains("Moss"));
  CHECK(material->itemText(1).contains("Terrain texture"));
  material->setCurrentIndex(1);
  activate->click();
  QApplication::processEvents();
  REQUIRE(panel->painting());
  CHECK(window->toolBox().terrainToolActive());
  CHECK_FALSE(window->toolBox().selectModeActive());
  CHECK_FALSE(window->toolBox().brushCreationModeActive());
  window->switchToInspectorPage(InspectorPage::Map);
  CHECK(panel->painting());
  window->toolBox().setSelectMode(true);
  CHECK_FALSE(panel->painting());
  CHECK_FALSE(activate->isChecked());
  CHECK(window->toolBox().selectModeActive());
  window->switchToInspectorPage(InspectorPage::Terrain);
  activate->click();
  window->toolBox().togglePathTool();
  CHECK_FALSE(panel->painting());
  CHECK(window->toolBox().pathToolActive());
  activate->click();
  CHECK_FALSE(window->toolBox().pathToolActive());
  REQUIRE(panel->painting());
  auto* view = window->currentOrFirstVisible3DMapView();
  REQUIRE(view);
  auto& camera = static_cast<MapViewBase&>(*view).camera();
  camera.setViewport({0, 0, view->width(), view->height()});
  camera.moveTo(vm::vec3f{0, 0, 256});
  camera.lookAt(vm::vec3f{0, 0, 0}, vm::vec3f{0, 1, 0});
  const auto centerIs = [&](const bool red) {
    const auto image = view->grabFramebuffer();
    if (image.isNull())
      return false;
    // Stay off the world axes and grid at the exact center.
    const auto color = image.pixelColor(image.width() / 2 + 10, image.height() / 2 + 10);
    return red ? color.red() > color.green() * 2 : color.green() > color.red() * 2;
  };
  // Preview must use the captured texture even before painting.
  REQUIRE(QTest::qWaitFor([&] { return centerIs(false); }, 15000));
  // Thumbnail capture must preserve the editing tool and selection.
  REQUIRE(panel->painting());
  const auto selectedNodes = map.selection().nodes;
  const auto thumbnail = view->grabMapThumbnailFramebuffer();
  REQUIRE_FALSE(thumbnail.isNull());
  const auto thumbnailColor =
    thumbnail.pixelColor(thumbnail.width() / 2 + 10, thumbnail.height() / 2 + 10);
  CHECK(thumbnailColor.green() > thumbnailColor.red() * 2);
  CHECK(panel->painting());
  CHECK(map.selection().nodes == selectedNodes);

  const auto center = view->rect().center();
  const auto withoutCursor = view->grabFramebuffer();
  auto hover = QMouseEvent{
    QEvent::MouseMove,
    QPointF{center},
    QPointF{view->mapToGlobal(center)},
    Qt::NoButton,
    Qt::NoButton,
    Qt::NoModifier};
  QApplication::sendEvent(view, &hover);
  auto cursorTiming = render::RenderTiming{};
  auto withCursor = QImage{};
  {
    const auto capture = render::RenderTiming::Capture{cursorTiming};
    withCursor = view->grabFramebuffer();
  }
  CHECK(cursorTiming.counters["terrain.brush_cursors"] == 1);
  CHECK(withCursor != withoutCursor);
  const auto movedPosition = center + QPoint{30, 0};
  auto movedHover = QMouseEvent{
    QEvent::MouseMove,
    QPointF{movedPosition},
    QPointF{view->mapToGlobal(movedPosition)},
    Qt::NoButton,
    Qt::NoButton,
    Qt::NoModifier};
  QApplication::sendEvent(view, &movedHover);
  auto movedTiming = render::RenderTiming{};
  {
    const auto capture = render::RenderTiming::Capture{movedTiming};
    CHECK(view->grabFramebuffer() != withCursor);
  }
  CHECK(movedTiming.counters["terrain.brush_cursors"] == 1);
  CHECK(movedTiming.counters["terrain.region_decodes"] == 0);
  QApplication::sendEvent(view, &hover);
  auto* radius = panel->findChild<QSpinBox*>("TerrainInspector_Radius");
  REQUIRE(radius);
  const auto originalRadius = radius->value();
  radius->setValue(originalRadius + 20);
  CHECK(view->grabFramebuffer() != withCursor);
  radius->setValue(originalRadius);
  auto leave = QEvent{QEvent::Leave};
  QApplication::sendEvent(view, &leave);
  auto leaveTiming = render::RenderTiming{};
  {
    const auto capture = render::RenderTiming::Capture{leaveTiming};
    view->grabFramebuffer();
  }
  CHECK(leaveTiming.counters["terrain.brush_cursors"] == 0);
  auto pick = mdl::PickResult::byDistance();
  mdl::pick(map, vm::ray3d{camera.pickRay(float(center.x()), float(center.y()))}, pick);
  const auto face =
    mdl::hitToFaceHandle(pick.first(mdl::HitFilters::type(mdl::BrushNode::BrushHitType)));
  REQUIRE(face);
  REQUIRE(&face->node() == brush);
  REQUIRE(face->face().normal().z() > 0);

  // A continuously changing stroke must start baking immediately and show progress
  // before the user releases the mouse, even while a newer sample is being prepared.
  auto* strength = panel->findChild<QSpinBox*>("TerrainInspector_Strength");
  REQUIRE(strength);
  const auto originalStrength = strength->value();
  strength->setValue(5);
  panel->beginStroke(pick);
  REQUIRE(panel->stroking());
  auto startTiming = render::RenderTiming{};
  {
    const auto capture = render::RenderTiming::Capture{startTiming};
    view->grabFramebuffer();
  }
  CHECK(startTiming.counters["terrain.bake_starts"] == 1);
  auto strokeTiming = render::RenderTiming{};
  auto changedDuringStroke = false;
  {
    const auto capture = render::RenderTiming::Capture{strokeTiming};
    for (auto i = 0; i < 150 && !changedDuringStroke; ++i)
    {
      panel->continueStroke(pick);
      QTest::qWait(10);
      changedDuringStroke = centerIs(true);
    }
  }
  CHECK(changedDuringStroke);
  CHECK(strokeTiming.counters["terrain.scene_builds"] > 0);
  CHECK(strokeTiming.counters["terrain.surface_extractions"] == 0);
  CHECK(strokeTiming.counters["terrain.texture_resources_reused"] > 0);
  CHECK(panel->stroking());
  const auto firstStroke = panel->strokeRevision();
  panel->endStroke(false);
  // These two transitions intentionally happen before the renderer sees a frame.
  panel->beginStroke(pick);
  CHECK(panel->strokeRevision() != firstStroke);
  CHECK(centerIs(false));
  QTest::qWait(250);
  CHECK(centerIs(false));
  panel->endStroke(false);
  CHECK(centerIs(false));
  CHECK(group->group().terrainPaint() == initial);
  panel->beginStroke(pick);
  REQUIRE(panel->stroking());
  window->toolBox().setSelectMode(false);
  CHECK_FALSE(panel->stroking());
  CHECK_FALSE(panel->painting());
  CHECK(group->group().terrainPaint() == initial);
  CHECK(window->toolBox().brushCreationModeActive());
  activate->click();
  terrainMouseClick(view, Qt::LeftButton, Qt::NoModifier, center);
  view->grabFramebuffer();
  terrainMouseEvent(
    view, QEvent::MouseButtonPress, Qt::LeftButton, Qt::NoModifier, center);
  view->grabFramebuffer();
  REQUIRE(panel->stroking());
  CHECK_FALSE(window->toolBox().dragging());
  CHECK(view->cancelMouseDrag());
  CHECK_FALSE(panel->stroking());
  CHECK(panel->painting());
  terrainMouseEvent(
    view, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoModifier, center);
  view->grabFramebuffer();
  terrainMouseEvent(
    view, QEvent::MouseButtonPress, Qt::LeftButton, Qt::NoModifier, center);
  view->grabFramebuffer();
  REQUIRE(panel->stroking());
  auto focusOut = QFocusEvent{QEvent::FocusOut};
  QApplication::sendEvent(view, &focusOut);
  CHECK_FALSE(panel->stroking());
  CHECK(panel->painting());
  CHECK(group->group().terrainPaint() == initial);
  terrainMouseEvent(
    view, QEvent::MouseButtonPress, Qt::LeftButton, Qt::NoModifier, center);
  view->grabFramebuffer();
  strength->setValue(originalStrength);

  // Exercise the uncommitted stroke separately from native focus, which the
  // background-window test policy intentionally does not establish.
  view->clearFocus();
  QApplication::processEvents();
  panel->beginStroke(pick);
  REQUIRE(panel->stroking());
  CHECK(group->group().terrainPaint() == initial);
  auto blankFrames = 0;
  REQUIRE(QTest::qWaitFor(
    [&] {
      const auto image = view->grabFramebuffer();
      if (image.isNull())
        return false;
      const auto color =
        image.pixelColor(image.width() / 2 + 10, image.height() / 2 + 10);
      const auto red = color.red() > color.green() * 2;
      const auto green = color.green() > color.red() * 2;
      if (!red && !green)
        ++blankFrames;
      return red;
    },
    15000));
  CHECK(blankFrames == 0);
  REQUIRE(panel->stroking());
  panel->endStroke(false);
  REQUIRE(QTest::qWaitFor([&] { return centerIs(false); }, 15000));
  // The first left click activates a viewport after working in the inspector.
  terrainMouseClick(view, Qt::LeftButton, Qt::NoModifier, center);
  view->grabFramebuffer();
  terrainMouseEvent(
    view, QEvent::MouseButtonPress, Qt::LeftButton, Qt::NoModifier, center);
  view->grabFramebuffer();
  REQUIRE(panel->stroking());
  QTest::qWait(110); // Pass the editor's accidental-drag threshold.
  terrainMouseEvent(
    view, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoModifier, center);
  view->grabFramebuffer();
  const auto painted = group->group().terrainPaint();
  REQUIRE(painted != initial);
  REQUIRE(QTest::qWaitFor([&] { return centerIs(true); }, 15000));
  CHECK(material->currentIndex() == 1);
  button("Analyze Terrain")->click();
  REQUIRE(showCuts->isChecked());
  const auto withCuts = view->grabFramebuffer();
  REQUIRE_FALSE(withCuts.isNull());
  showCuts->setChecked(false);
  const auto withoutCuts = view->grabFramebuffer();
  REQUIRE_FALSE(withoutCuts.isNull());
  CHECK(withCuts != withoutCuts);
  map.undoCommand();
  const auto restored = group->group().terrainPaint() == initial;
  CHECK(restored);
  REQUIRE(QTest::qWaitFor([&] { return centerIs(false); }, 15000));
  map.redoCommand();
  const auto repainted = group->group().terrainPaint() == painted;
  CHECK(repainted);
  REQUIRE(QTest::qWaitFor([&] { return centerIs(true); }, 15000));
  activate->click();
  mdl::deselectAll(map);
  window->switchToInspectorPage(InspectorPage::Entity);
  REQUIRE(QTest::qWaitFor([&] { return centerIs(true); }, 15000));
  mdl::hideNodes(map, {group});
  REQUIRE(QTest::qWaitFor([&] { return !centerIs(true); }, 15000));
  mdl::showNodes(map, {group});
  REQUIRE(QTest::qWaitFor([&] { return centerIs(true); }, 15000));

  // A second 3D viewport for the same document must use the existing preview.
  {
    auto sibling = MapView3D{app, window->document(), window->toolBox()};
    sibling.resize(view->size());
    sibling.show();
    QApplication::processEvents();
    auto& siblingCamera = static_cast<MapViewBase&>(sibling).camera();
    siblingCamera.setViewport({0, 0, sibling.width(), sibling.height()});
    siblingCamera.moveTo(vm::vec3f{0, 0, 256});
    siblingCamera.lookAt(vm::vec3f{0, 0, 0}, vm::vec3f{0, 1, 0});
    auto timing = render::RenderTiming{};
    const auto capture = render::RenderTiming::Capture{timing};
    const auto image = sibling.grabFramebuffer();
    REQUIRE_FALSE(image.isNull());
    const auto color = image.pixelColor(image.width() / 2 + 10, image.height() / 2 + 10);
    CHECK(color.red() > color.green() * 2);
    CHECK(timing.counters["terrain.scene_builds"] == 0);
  }

  // Moving other objects must not rebuild the terrain or drop its ready texture.
  auto* other = new mdl::BrushNode{
    require(builder.createCuboid(vm::bbox3d{{512, 512, -64}, {576, 576, 0}}, "grass"))};
  mdl::addNodes(map, {{map.editorContext().currentLayer(), {other}}});
  REQUIRE(QTest::qWaitFor([&] { return centerIs(true); }, 15000));
  const auto drag = [&](mdl::Node* node, const vm::vec3d& delta) {
    auto timing = render::RenderTiming{};
    const auto capture = render::RenderTiming::Capture{timing};
    REQUIRE(mdl::transformNodes(
      map, {node}, "Move test object", vm::translation_matrix(delta)));
    const auto image = view->grabFramebuffer();
    REQUIRE_FALSE(image.isNull());
    const auto color = image.pixelColor(image.width() / 2 + 10, image.height() / 2 + 10);
    CHECK(color.red() > color.green() * 2);
    CHECK(timing.counters["terrain.scene_builds"] == 0);
    CHECK(timing.counters["terrain.texture_resources_created"] == 0);
    CHECK(timing.counters["terrain.inspector_refreshes"] == 0);
  };
  for (auto i = 0; i < 4; ++i)
    drag(other, vm::vec3d{1, 0, 0});
  // Moving the terrain as a whole should update its existing geometry in place.
  const auto redCentroid = [](const QImage& image) {
    auto sum = 0.0;
    auto count = 0;
    for (auto y = 0; y < image.height(); y += 3)
      for (auto x = 0; x < image.width(); x += 3)
      {
        const auto color = image.pixelColor(x, y);
        if (
          color.red() > 80 && color.red() > color.green() * 2
          && color.red() > color.blue() * 2)
        {
          sum += x;
          ++count;
        }
      }
    return count > 0 ? sum / count : -1.0;
  };
  const auto initialCentroid = redCentroid(view->grabFramebuffer());
  REQUIRE(initialCentroid >= 0);
  for (auto i = 0; i < 4; ++i)
    drag(group, vm::vec3d{4, 0, 0});
  const auto movedCentroid = redCentroid(view->grabFramebuffer());
  REQUIRE(movedCentroid >= 0);
  CHECK(std::abs(movedCentroid - initialCentroid) > 3.0);
  drag(group, vm::vec3d{-16, 0, 0});

  mdl::selectNodes(map, {group});
  {
    auto timing = render::RenderTiming{};
    const auto capture = render::RenderTiming::Capture{timing};
    REQUIRE(mdl::transformNodes(
      map, {group}, "Move selected terrain", vm::translation_matrix(vm::vec3d{2, 0, 0})));
    view->grabFramebuffer();
    CHECK(timing.counters["terrain.scene_builds"] == 0);
    CHECK(timing.counters["terrain.inspector_refreshes"] == 0);
  }
  REQUIRE(mdl::transformNodes(
    map,
    {group},
    "Restore selected terrain",
    vm::translation_matrix(vm::vec3d{-2, 0, 0})));
  mdl::deselectAll(map);
  REQUIRE(QTest::qWaitFor([&] { return centerIs(true); }, 15000));

  // Moving an individual terrain brush changes clipping, but must reuse ready textures.
  {
    auto timing = render::RenderTiming{};
    const auto capture = render::RenderTiming::Capture{timing};
    REQUIRE(mdl::transformNodes(
      map, {brush}, "Move terrain brush", vm::translation_matrix(vm::vec3d{2, 0, 0})));
    CHECK(centerIs(true));
    CHECK(timing.counters["terrain.scene_builds"] == 1);
    CHECK(timing.counters["terrain.surface_extractions"] == 1);
    CHECK(timing.counters["terrain.texture_resources_created"] == 0);
  }
  REQUIRE(mdl::transformNodes(
    map, {brush}, "Restore terrain brush", vm::translation_matrix(vm::vec3d{-2, 0, 0})));
  CHECK(centerIs(true));

  // Face display modes must control the terrain just like ordinary brush faces.
  const auto savedFaceMode = pref(Preferences::FaceRenderMode);
  const auto restoreFaceMode =
    qScopeGuard([&] { setPref(Preferences::FaceRenderMode, savedFaceMode); });
  setPref(Preferences::FaceRenderMode, Preferences::FaceRenderModeSkip);
  CHECK_FALSE(centerIs(true));
  setPref(Preferences::FaceRenderMode, savedFaceMode);
  REQUIRE(QTest::qWaitFor([&] { return centerIs(true); }, 15000));

  // Converting painted ground and sculpting it must keep the ready paint bake,
  // while the visible geometry and the pickable brush proxies change together.
  mdl::selectNodes(map, {group});
  window->switchToInspectorPage(InspectorPage::Terrain);
  navigation->click();
  auto* editMode = panel->findChild<QComboBox*>("TerrainInspector_EditMode");
  auto* convert = panel->findChild<QPushButton*>("TerrainInspector_Convert");
  REQUIRE(editMode);
  REQUIRE(convert);
  REQUIRE(activate);
  editMode->setCurrentIndex(1);
  REQUIRE(convert->isEnabled());
  convert->click();
  const auto convertedStorage = terrainRegionSnapshot(*group);
  const auto& converted = *convertedStorage;
  REQUIRE(converted.heightField);
  const auto convertedHeights = converted.heightField->heights;
  REQUIRE(QTest::qWaitFor([&] { return centerIs(true); }, 15000));
  testTerrainPanelInteraction(window, panel);
  activate->click();
  REQUIRE(panel->painting());
  const auto terrainPick = [&] {
    auto result = mdl::PickResult::byDistance();
    mdl::pick(
      map, vm::ray3d{camera.pickRay(float(center.x()), float(center.y()))}, result);
    return result;
  };
  auto sculptTiming = render::RenderTiming{};
  {
    const auto capture = render::RenderTiming::Capture{sculptTiming};
    panel->beginStroke(terrainPick());
    REQUIRE(panel->stroking());
    CHECK(centerIs(true));
  }
  CHECK(sculptTiming.counters["terrain.bake_starts"] == 0);
  CHECK(sculptTiming.counters["terrain.scene_builds"] > 0);
  const auto sculptedStorage = terrainRegionSnapshot(*group);
  const auto& sculpted = *sculptedStorage;
  REQUIRE(sculpted.heightField);
  CHECK(sculpted.heightField->heights != convertedHeights);
  CHECK(sculpted.weights == converted.weights);
  CHECK(terrainSurfaces(*group, sculpted).is_success());
  panel->endStroke(true);
  map.undoCommand();
  CHECK(
    require(TerrainRegion::decode(group->group().terrainPaint())).heightField->heights
    == convertedHeights);
  map.redoCommand();
  CHECK(
    require(TerrainRegion::decode(group->group().terrainPaint())).heightField->heights
    == sculpted.heightField->heights);
  panel->beginStroke(terrainPick());
  REQUIRE(panel->stroking());
  editMode->setCurrentIndex(0);
  CHECK_FALSE(panel->stroking());
  CHECK(window->toolBox().terrainTool().mode() == TerrainToolMode::Paint);
  CHECK(
    require(TerrainRegion::decode(group->group().terrainPaint())).heightField->heights
    == sculpted.heightField->heights);
  editMode->setCurrentIndex(1);
  CHECK(window->toolBox().terrainTool().mode() == TerrainToolMode::Sculpt);
  activate->click();

  // Paint after sculpting, then sculpt again without losing the new paint.
  editMode->setCurrentIndex(0);
  material->setCurrentIndex(0);
  activate->click();
  panel->beginStroke(terrainPick());
  REQUIRE(panel->stroking());
  panel->endStroke(true);
  const auto repaintedStorage = terrainRegionSnapshot(*group);
  const auto& repaintedRegion = *repaintedStorage;
  CHECK(repaintedRegion.weights != sculpted.weights);
  CHECK(repaintedRegion.heightField->heights == sculpted.heightField->heights);
  activate->click();
  editMode->setCurrentIndex(1);
  activate->click();
  terrainMouseEvent(
    view, QEvent::MouseButtonPress, Qt::LeftButton, Qt::NoModifier, center);
  view->grabFramebuffer();
  REQUIRE(panel->stroking());
  QTest::qWait(110); // Pass the editor's accidental-drag threshold.
  terrainMouseEvent(
    view, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoModifier, center);
  view->grabFramebuffer();
  const auto resculpted = require(TerrainRegion::decode(group->group().terrainPaint()));
  CHECK(resculpted.weights == repaintedRegion.weights);
  CHECK(resculpted.heightField->heights != repaintedRegion.heightField->heights);
  CHECK(terrainSurfaces(*group, resculpted).is_success());
  panel->beginStroke(terrainPick());
  REQUIRE(panel->stroking());
  QTest::keyClick(view, Qt::Key_Escape);
  view->grabFramebuffer();
  CHECK_FALSE(panel->stroking());
  CHECK(
    require(TerrainRegion::decode(group->group().terrainPaint())).heightField->heights
    == resculpted.heightField->heights);
  activate->click();
  map.undoCommand();
  map.undoCommand();
  CHECK(
    require(TerrainRegion::decode(group->group().terrainPaint())).heightField->heights
    == sculpted.heightField->heights);
  REQUIRE(QTest::qWaitFor([&] { return centerIs(true); }, 15000));

  testTerrainVertexEditing(
    app, window, view, panel, group, editMode, activate, camera, sculpted);

  // Converting the terrain brushes inside the open group must retain the live
  // preview and keep both paint and sculpt tools working on entity-owned brushes.
  mdl::openGroup(map, *group);
  mdl::selectNodes(map, group->children());
  const auto detailDefinition = mdl::EntityDefinition{
    "func_detail", RgbB{255, 255, 255}, "Detail brushes", {}, std::nullopt};
  auto* detail = mdl::createBrushEntity(map, detailDefinition);
  REQUIRE(detail);
  REQUIRE(detail->hasChildren());
  CHECK(terrainSurfaces(*group, sculpted).is_success());
  mdl::deselectAll(map);
  REQUIRE(QTest::qWaitFor([&] { return centerIs(true); }, 15000));
  mdl::closeGroup(map);
  mdl::selectNodes(map, {group});
  REQUIRE(QTest::qWaitFor([&] { return centerIs(true); }, 15000));

  editMode->setCurrentIndex(0);
  material->setCurrentIndex(0);
  activate->click();
  panel->beginStroke(terrainPick());
  REQUIRE(panel->stroking());
  panel->endStroke(true);
  activate->click();
  CHECK(testTerrainRegion(*group)->weights != sculpted.weights);
  map.undoCommand();
  REQUIRE(QTest::qWaitFor([&] { return centerIs(true); }, 15000));

  editMode->setCurrentIndex(1);
  activate->click();
  panel->beginStroke(terrainPick());
  REQUIRE(panel->stroking());
  panel->endStroke(true);
  activate->click();
  CHECK(testTerrainRegion(*group)->heightField->heights != sculpted.heightField->heights);
  map.undoCommand();
  CHECK(testTerrainRegion(*group)->heightField->heights == sculpted.heightField->heights);

  mdl::lockNodes(map, {group});
  CHECK_FALSE(activate->isEnabled());
  panel->beginStroke(terrainPick());
  CHECK_FALSE(panel->stroking());
  mdl::unlockNodes(map, {group});
  mdl::selectNodes(map, {group});

  const auto mapId = project->session()->assetFor(window->document());
  REQUIRE(mapId);
  const auto copy = pathFromQString(directory.filePath("terrain-reopened.tbproj"));
  require(project->session()->saveCopy(copy));
  REQUIRE(app.openProject(copy));
  auto* reopenedProject = findProjectWindow(copy);
  REQUIRE(reopenedProject);
  require(app.mapWindowManager().loadProjectMap(reopenedProject->session(), *mapId));
  auto* reopenedWindow = app.mapWindowManager().topMapWindow();
  REQUIRE(reopenedWindow);
  const mdl::GroupNode* reopenedTerrain = nullptr;
  const auto findTerrain = [&](const auto& self, const mdl::Node& node) -> void {
    if (const auto* candidate = dynamic_cast<const mdl::GroupNode*>(&node);
        candidate && !candidate->group().terrainPaint().empty())
      reopenedTerrain = candidate;
    for (const auto* child : node.children())
      self(self, *child);
  };
  findTerrain(findTerrain, reopenedWindow->document().map().worldNode());
  REQUIRE(reopenedTerrain);
  const auto reopenedRegion =
    require(TerrainRegion::decode(reopenedTerrain->group().terrainPaint()));
  REQUIRE(reopenedRegion.heightField);
  CHECK(reopenedRegion.heightField->heights == sculpted.heightField->heights);
  CHECK(reopenedRegion.weights == sculpted.weights);
  CHECK(terrainSurfaces(*reopenedTerrain, reopenedRegion).is_success());
  reopenedProject->resize(1200, 820);
  reopenedProject->show();
  QApplication::processEvents();
  auto* reopenedView = reopenedWindow->currentOrFirstVisible3DMapView();
  REQUIRE(reopenedView);
  auto& reopenedCamera = static_cast<MapViewBase&>(*reopenedView).camera();
  reopenedCamera.setViewport({0, 0, reopenedView->width(), reopenedView->height()});
  reopenedCamera.moveTo(vm::vec3f{0, 0, 256});
  reopenedCamera.lookAt(vm::vec3f{0, 0, 0}, vm::vec3f{0, 1, 0});
  REQUIRE(QTest::qWaitFor(
    [&] {
      const auto image = reopenedView->grabFramebuffer();
      if (image.isNull())
        return false;
      const auto color =
        image.pixelColor(image.width() / 2 + 10, image.height() / 2 + 10);
      return color.red() > color.green() * 2;
    },
    15000));
  reopenedWindow->closeDocument(true);

  // Updating a captured dependency must invalidate the preview in the open map.
  project->show();
  auto bluePixels = std::vector<unsigned char>(64 * 64 * 4, 255);
  for (auto pixel = size_t{0}; pixel < 64 * 64; ++pixel)
  {
    bluePixels[pixel * 4] = 30;
    bluePixels[pixel * 4 + 1] = 40;
    bluePixels[pixel * 4 + 2] = 210;
  }
  const auto blue =
    require(img::convertGoldSrcTexture({64, 64, std::move(bluePixels)}, "terrain_slot1"));
  auto replacement = project::AssetImport{};
  replacement.type = project::AssetType::Texture;
  replacement.virtualPath = "textures/terrain_slot1.wad";
  replacement.displayName = "Terrain texture";
  replacement.original = replacement.compatible = blue.wad;
  require(project->session()->replaceAsset(textures[1], replacement));
  REQUIRE(QTest::qWaitFor(
    [&] {
      const auto image = view->grabFramebuffer();
      if (image.isNull())
        return false;
      const auto color =
        image.pixelColor(image.width() / 2 + 10, image.height() / 2 + 10);
      return color.blue() > color.red() * 2;
    },
    15000));

  // Draw a new flat terrain in an XY view, then undo it without changing the
  // existing sculpted terrain. The rectangle follows the selected grid spacing.
  window->switchToInspectorPage(InspectorPage::Terrain);
  navigation->click();
  editMode->setCurrentIndex(1);
  auto* drawNew = panel->findChild<QPushButton*>("TerrainInspector_DrawNew");
  auto* newHeight = panel->findChild<QSpinBox*>("TerrainInspector_NewHeight");
  auto* newThickness = panel->findChild<QSpinBox*>("TerrainInspector_NewThickness");
  REQUIRE(drawNew);
  REQUIRE(newHeight);
  REQUIRE(newThickness);
  auto* brushEntity = panel->findChild<QCheckBox*>("TerrainInspector_BrushEntity");
  auto* brushEntityClass =
    panel->findChild<QComboBox*>("TerrainInspector_BrushEntityClass");
  REQUIRE(brushEntity);
  REQUIRE(brushEntityClass);
  CHECK_FALSE(brushEntity->isChecked());
  const auto fgdPath = directory.filePath("terrain.fgd");
  auto fgd = QFile{fgdPath};
  REQUIRE(fgd.open(QIODevice::WriteOnly));
  REQUIRE(
    fgd.write("@SolidClass = func_wall : \"Wall\" []\n"
              "@SolidClass = func_detail : \"Detail\" []\n"
              "@SolidClass = worldspawn : \"World\" []\n"
              "@PointClass = light : \"Light\" []\n")
    > 0);
  fgd.close();
  mdl::setEntityDefinitionFile(
    map, mdl::EntityDefinitionFileSpec::makeExternal(pathFromQString(fgdPath)));
  CHECK(brushEntityClass->count() == 2);
  CHECK(brushEntityClass->currentText() == QStringLiteral("func_detail"));
  CHECK_FALSE(brushEntityClass->isEnabled());
  auto* ancestor = view->parentWidget();
  while (ancestor && !qobject_cast<MapEditorView*>(ancestor))
    ancestor = ancestor->parentWidget();
  REQUIRE(ancestor);
  {
    auto top =
      MapView2D{app, window->document(), window->toolBox(), MapView2D::ViewPlane::XY};
    top.setParent(ancestor);
    top.resize(400, 400);
    top.show();
    QApplication::processEvents();
    auto& topCamera = top.camera();
    topCamera.setViewport({0, 0, top.width(), top.height()});
    const auto toScreen = [&](const vm::vec3f& world) {
      const auto projected = topCamera.project(world);
      return QPoint{
        int(std::lround(projected.x())),
        int(std::lround(float(top.height()) - projected.y()))};
    };
    const auto start = toScreen({-128, -128, 0});
    const auto finish = toScreen({128, 128, 0});
    REQUIRE(top.rect().contains(start));
    REQUIRE(top.rect().contains(finish));
    newHeight->setValue(32);
    newThickness->setValue(96);
    drawNew->click();
    REQUIRE(panel->drawingTerrain());
    brushEntity->setChecked(true);
    REQUIRE(brushEntityClass->isEnabled());
    const auto oldGridSize = map.grid().size();
    map.grid().setSize(7);
    const auto snapped = panel->drawnTerrainBounds({139, 119}, {-119, -139});
    REQUIRE(snapped);
    CHECK(snapped->min == vm::vec3d{-128, -128, -64});
    CHECK(snapped->max == vm::vec3d{128, 128, 32});
    CHECK_FALSE(panel->drawnTerrainBounds({1, 1}, {2, 2}));
    map.grid().setSize(oldGridSize);
    terrainMouseEvent(
      &top, QEvent::MouseButtonPress, Qt::LeftButton, Qt::NoModifier, start);
    top.grabFramebuffer();
    terrainMouseEvent(&top, QEvent::MouseMove, Qt::NoButton, Qt::NoModifier, finish);
    top.grabFramebuffer();
    const auto preview = window->toolBox().terrainTool().drawPreviewBounds();
    REQUIRE(preview);
    CHECK(preview->min == vm::vec3d{-128, -128, -64});
    CHECK(preview->max == vm::vec3d{128, 128, 32});
    CHECK(map.selection().groups.front() == group);
    const auto capturePath = qEnvironmentVariable("TB_TERRAIN_DRAW_CAPTURE_PATH");
    if (!capturePath.isEmpty())
    {
      REQUIRE(top.grabFramebuffer().save(capturePath));
      REQUIRE(panel->grab().save(capturePath + ".panel.png"));
      auto& previewCamera = static_cast<MapViewBase*>(view)->camera();
      const auto oldPosition = previewCamera.position();
      const auto oldDirection = previewCamera.direction();
      const auto oldUp = previewCamera.up();
      previewCamera.moveTo(vm::vec3f{384, -384, 320});
      previewCamera.lookAt(vm::vec3f{0, 0, 0}, vm::vec3f{0, 0, 1});
      REQUIRE(view->grabFramebuffer().save(capturePath + ".3d.png"));
      previewCamera.moveTo(oldPosition);
      previewCamera.setDirection(oldDirection, oldUp);
    }
    QTest::qWait(110); // Pass the editor's accidental-drag threshold.
    terrainMouseEvent(
      &top, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoModifier, finish);
    top.grabFramebuffer();
    CHECK_FALSE(panel->drawingTerrain());
    CHECK_FALSE(window->toolBox().terrainTool().drawPreviewBounds());
    REQUIRE(map.selection().groups.size() == 1);
    auto* created = map.selection().groups.front();
    const auto newRegion =
      require(TerrainRegion::decode(created->group().terrainPaint()));
    REQUIRE(newRegion.heightField);
    CHECK(newRegion.brushEntityClass == "func_detail");
    CHECK(newRegion.heightField->columns == 4);
    CHECK(newRegion.heightField->rows == 4);
    CHECK(newRegion.heightField->bottom == -64);
    CHECK(newRegion.heightField->heights.front() == 32);
    CHECK(created->logicalBounds() == *preview);
    CHECK(terrainSurfaces(*created, newRegion).is_success());
    map.undoCommand();
    CHECK(map.selection().groups.front() == group);
    CHECK(
      require(TerrainRegion::decode(group->group().terrainPaint())).heightField->heights
      == sculpted.heightField->heights);
    drawNew->click();
    terrainMouseEvent(
      &top, QEvent::MouseButtonPress, Qt::LeftButton, Qt::NoModifier, start);
    top.grabFramebuffer();
    QTest::keyClick(&top, Qt::Key_Escape);
    top.grabFramebuffer();
    CHECK(panel->drawingTerrain());
    CHECK_FALSE(window->toolBox().terrainTool().interacting());
    CHECK_FALSE(window->toolBox().terrainTool().drawPreviewBounds());
    QTest::keyClick(&top, Qt::Key_Escape);
    top.grabFramebuffer();
    CHECK_FALSE(panel->drawingTerrain());
    QTest::qWait(110); // Pass the editor's accidental-drag threshold.
    terrainMouseEvent(
      &top, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoModifier, finish);
    top.grabFramebuffer();
    CHECK(map.selection().groups.front() == group);
    top.hide();
  }
  window->closeDocument(true);
}

TEST_CASE("Project terrain edits across chunk boundaries")
{
  if (!qEnvironmentVariableIsSet("TB_TERRAIN_CHUNK_TEST_CHILD"))
  {
    auto process = QProcess{};
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("TB_TEST_SHARED_OPENGL", "1");
    environment.insert("TB_TERRAIN_CHUNK_TEST_CHILD", "1");
    process.setProcessEnvironment(environment);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(
      QCoreApplication::applicationFilePath(),
      {"Project terrain edits across chunk boundaries", "--reporter", "compact"});
    REQUIRE(process.waitForStarted(10000));
    const auto finished = process.waitForFinished(55000);
    INFO(process.readAllStandardOutput().toStdString());
    REQUIRE(finished);
    REQUIRE(process.exitStatus() == QProcess::NormalExit);
    REQUIRE(process.exitCode() == 0);
    return;
  }
  auto directory = QTemporaryDir{};
  REQUIRE(directory.isValid());
  const auto path = pathFromQString(directory.filePath("chunked-terrain.tbproj"));
  require(ProjectSession::create(path, "Chunked Terrain", "Half-Life"));
  auto fixture = AppControllerFixture{[](const auto& writeGameConfig) {
    auto config = std::string{GameConfig};
    config.replace(config.find(GameName), GameName.size(), "Half-Life");
    writeGameConfig("Half-Life", config, std::nullopt, std::nullopt);
  }};
  auto& app = fixture.appController();
  REQUIRE(app.openProject(path));
  auto* project = findProjectWindow(path);
  REQUIRE(project);
  auto textures = std::array<std::string, 3>{};
  for (auto slot = size_t{0}; slot < textures.size(); ++slot)
  {
    auto pixels = std::vector<unsigned char>(64 * 64 * 4, 255);
    for (auto pixel = size_t{0}; pixel < 64 * 64; ++pixel)
      pixels[pixel * 4 + slot] = 40;
    const auto name = "chunk_slot" + std::to_string(slot);
    const auto converted =
      require(img::convertGoldSrcTexture({64, 64, std::move(pixels)}, name));
    auto input = project::AssetImport{};
    input.type = project::AssetType::Texture;
    input.virtualPath = "textures/" + name + ".wad";
    input.displayName = name;
    input.original = input.compatible = converted.wad;
    textures[slot] = require(project->session()->importAsset(input)).id;
  }
  const auto json = std::string{
    R"({"format":"goldsrc-terrain-material","version":1,"slots":[{"name":"Moss"},{"name":"Sand"},{"name":"Stone"}]})"};
  auto input = project::AssetImport{};
  input.type = project::AssetType::Material;
  input.virtualPath = "materials/chunks.tbmat";
  input.displayName = "Chunk blend";
  input.original.assign(json.begin(), json.end());
  input.compatible = input.original;
  const auto material = require(project->session()->importAsset(input));
  {
    auto store = require(project::ProjectStore::open(project->session()->store().path()));
    for (auto slot = 0; slot < 3; ++slot)
      require(store->addDependency(
        material.id, textures[size_t(slot)], "terrain-slot-" + std::to_string(slot)));
  }
  require(app.mapWindowManager().createProjectMap(
    project->session(), "Terrain", mdl::MapFormat::Valve));
  auto* window = app.mapWindowManager().topMapWindow();
  REQUIRE(window);
  auto& map = window->document().map();
  mdl::selectAllNodes(map);
  mdl::removeSelectedNodes(map);
  project->resize(1200, 820);
  project->show();
  window->switchToInspectorPage(InspectorPage::Terrain);
  auto* navigation = window->findChild<QToolButton*>("Inspector_NavigationTerrain");
  REQUIRE(navigation);
  navigation->click();
  QApplication::processEvents();
  auto* panel = window->findChild<TerrainInspector*>();
  auto* view = window->currentOrFirstVisible3DMapView();
  REQUIRE(panel);
  REQUIRE(view);
  testChunkedTerrainEditing(app, window, view, panel, material.id);
  window->closeDocument(true);
}

TEST_CASE("ProjectCompilation")
{
  const auto profileName = GENERATE("Probe", "Mixed", "DisabledMarker");
  const auto restoreProject = GENERATE(false, true);
  CAPTURE(profileName, restoreProject);
  // The project window belongs to the app controller, so the folder holding the project
  // file has to outlive the fixture; otherwise the store still has the file open when the
  // temporary folder is removed.
  auto directory = QTemporaryDir{};
  REQUIRE(directory.isValid());
  const auto projectPath = pathFromQString(directory.filePath("compile.tbproj"));
  require(ProjectSession::create(projectPath, "Compile", GameName));

  auto fixture = AppControllerFixture{[](const auto& writeGameConfig) {
    writeGameConfig(GameName, GameConfig, CompilationProfiles, std::nullopt);
  }};
  auto& appController = fixture.appController();

  REQUIRE(appController.openProject(projectPath));

  auto* window = findProjectWindow(projectPath);
  REQUIRE(window != nullptr);
  auto session = window->session();
  require(appController.mapWindowManager().createProjectMap(
    session, "alpha", mdl::MapFormat::Valve));

  const auto assets = require(session->store().assets());
  const auto mapAsset = std::ranges::find_if(
    assets, [](const auto& asset) { return asset.type == project::AssetType::Map; });
  REQUIRE(mapAsset != assets.end());
  auto* document = session->documentFor(mapAsset->id);
  REQUIRE(document != nullptr);
  // The default fixture brush references the editor's placeholder material, which a
  // compile snapshot correctly refuses to export; an empty map keeps this test about the
  // input paths a tool sees.
  mdl::selectAllNodes(document->map());
  mdl::removeSelectedNodes(document->map());

  REQUIRE(mdl::setEntityProperty(document->map(), "message", "Frozen content"));
  REQUIRE(mdl::setEntityProperty(document->map(), "_tb_compile_test", "Strip me"));
  const auto checkpoint = require(session->checkpoint("Frozen input"));
  const auto mapIdentity = document->map().identity();

  auto camera = gl::PerspectiveCamera{};
  auto dialog = CompilationDialog{appController, *document, camera};

  auto started = QSignalSpy{&dialog, &CompilationDialog::compilationProfileStarted};
  REQUIRE(dialog.runProfileByName(profileName));
  REQUIRE(dialog.isRunning());
  // The first task is asynchronous; edit while it is running, before Export Map starts.
  REQUIRE(mdl::setEntityProperty(document->map(), "message", "Later live edit"));
  if (restoreProject)
  {
    require(session->restore(checkpoint.id));
    CHECK(document->map().identity() != mapIdentity);
  }
  REQUIRE(QTest::qWaitFor([&]() { return !dialog.isRunning(); }, 15000));

  const auto output = dialog.outputText();
  INFO(output.toStdString());
  REQUIRE(started.count() == 1);
  CHECK_FALSE(output.contains("Compilation failed"));
  REQUIRE(output.contains("snapshot="));

  // The tool saw the frozen snapshot, under the map's own name, and never the working
  // copy the document is editing.
  const auto liveFolder = pathAsQString(document->map().path().parent_path());
  CHECK(output.contains("map=alpha.map"));
  CHECK(output.contains("base=alpha"));
  CHECK_FALSE(output.contains(liveFolder));

  const auto snapshotFolder = [&]() {
    const auto marker = output.indexOf("snapshot=");
    REQUIRE(marker >= 0);
    const auto start = marker + int(std::string{"snapshot="}.size());
    const auto end = output.indexOf(" map=", start);
    REQUIRE(end > start);
    return output.mid(start, end - start);
  }();
  CHECK_FALSE(snapshotFolder.isEmpty());
  CHECK(
    std::filesystem::path{snapshotFolder.toStdString()}
    != document->map().path().parent_path());

  // Relative paths in a profile resolve inside the snapshot, not in the live map folder.
  const auto workDir = [&]() {
    const auto marker = output.indexOf("Using working directory '");
    REQUIRE(marker >= 0);
    const auto start = marker + int(std::string{"Using working directory '"}.size());
    const auto end = output.indexOf('\'', start);
    REQUIRE(end > start);
    return output.mid(start, end - start);
  }();
  CHECK(workDir == snapshotFolder);

  auto exported = QFile{snapshotFolder + "/exported.map"};
  REQUIRE(exported.open(QIODevice::ReadOnly));
  const auto bytes = exported.readAll();
  CHECK(bytes.contains("Frozen content"));
  CHECK_FALSE(bytes.contains("Later live edit"));
  CHECK_FALSE(bytes.contains("_tb_compile_test"));
  CHECK(bytes.contains("info_player_start"));
  CHECK(document->map().modified() == !restoreProject);

  if (!restoreProject)
  {
    SECTION("a later run captures the newer edits")
    {
      REQUIRE(dialog.runProfileByName(profileName));
      REQUIRE(QTest::qWaitFor([&]() { return !dialog.isRunning(); }, 15000));
      const auto laterOutput = dialog.outputText();
      const auto start =
        laterOutput.indexOf("snapshot=") + int(std::string{"snapshot="}.size());
      const auto end = laterOutput.indexOf(" map=", start);
      REQUIRE(end > start);
      const auto laterFolder = laterOutput.mid(start, end - start);
      CHECK(laterFolder != snapshotFolder);
      auto later = QFile{laterFolder + "/exported.map"};
      REQUIRE(later.open(QIODevice::ReadOnly));
      const auto laterBytes = later.readAll();
      CHECK(laterBytes.contains("Later live edit"));
      CHECK_FALSE(laterBytes.contains("Frozen content"));
    }
  }
}

TEST_CASE("ProjectCompilation.projectAwareProfilesSkipTheSnapshot")
{
  const auto missingResource = GENERATE(false, true);
  const auto restoreProject = GENERATE(false, true);
  // Empty editor textures are allowed by the shared resource gate.
  // A project-only tool must still run, even if restoring replaces the open map.
  auto directory = QTemporaryDir{};
  REQUIRE(directory.isValid());
  const auto projectPath = pathFromQString(directory.filePath("compile.tbproj"));
  require(ProjectSession::create(projectPath, "Compile", GameName));

  auto fixture = AppControllerFixture{[](const auto& writeGameConfig) {
    writeGameConfig(GameName, GameConfig, CompilationProfiles, std::nullopt);
  }};
  auto& appController = fixture.appController();

  REQUIRE(appController.openProject(projectPath));
  auto* window = findProjectWindow(projectPath);
  REQUIRE(window != nullptr);
  auto session = window->session();
  require(appController.mapWindowManager().createProjectMap(
    session, "alpha", mdl::MapFormat::Valve));

  const auto assets = require(session->store().assets());
  const auto mapAsset = std::ranges::find_if(
    assets, [](const auto& asset) { return asset.type == project::AssetType::Map; });
  REQUIRE(mapAsset != assets.end());
  auto* document = session->documentFor(mapAsset->id);
  REQUIRE(document != nullptr);
  const auto checkpoint = require(session->checkpoint("Compile input"));
  const auto mapIdentity = document->map().identity();
  const auto liveFolder = pathAsQString(document->map().path().parent_path());

  auto camera = gl::PerspectiveCamera{};
  auto dialog = CompilationDialog{appController, *document, camera};

  if (missingResource)
  {
    auto* entity = new mdl::EntityNode{
      mdl::Entity{{{"classname", "cycler"}, {"model", "models/missing.mdl"}}}};
    mdl::addNodes(
      document->map(), {{document->map().editorContext().currentLayer(), {entity}}});
    REQUIRE(dialog.runProfileByName("ProjectAware"));
    CHECK_FALSE(dialog.isRunning());
    CHECK(dialog.outputText().contains("models/missing.mdl"));
    CHECK_FALSE(dialog.outputText().contains("project="));
    CHECK(session
            ->exportCompileSnapshot(
              mapAsset->id, pathFromQString(directory.filePath("rejected")), "alpha.map")
            .is_error());
    return;
  }

  REQUIRE(dialog.runProfileByName("ProjectAware"));
  REQUIRE(dialog.isRunning());
  if (restoreProject)
  {
    require(session->restore(checkpoint.id));
    CHECK(document->map().identity() != mapIdentity);
  }
  REQUIRE(QTest::qWaitFor([&]() { return !dialog.isRunning(); }, 15000));

  const auto output = dialog.outputText();
  INFO(output.toStdString());
  CHECK_FALSE(output.contains("Compilation failed"));
  // Project variables are available even though no snapshot was exported.
  REQUIRE(output.contains("project="));
  CHECK(output.contains("compile.tbproj"));
  CHECK_FALSE(output.contains("${PROJECT_PATH}"));
  CHECK(output.contains("name=alpha"));

  // No snapshot: the tool's working directory is the live project map folder, not a
  // temporary snapshot folder.
  const auto workDir = [&]() {
    const auto marker = output.indexOf("Using working directory '");
    REQUIRE(marker >= 0);
    const auto start = marker + int(std::string{"Using working directory '"}.size());
    const auto end = output.indexOf('\'', start);
    REQUIRE(end > start);
    return output.mid(start, end - start);
  }();
  CHECK(workDir == liveFolder);
}

TEST_CASE("ProjectCompilation.directProfileSelection")
{
  auto profile = mdl::CompilationProfile{
    "Project",
    "${MAP_DIR_PATH}",
    {mdl::CompilationRunTool{
      true, "compiler", "--project ${PROJECT_PATH} --map ${MAP_NAME}", true}}};
  REQUIRE(isDirectProjectCompilation(profile));

  SECTION("disabled tasks are ignored")
  {
    profile.tasks.push_back(mdl::CompilationExportMap{false, false, {}, {}, "unused"});
    CHECK(isDirectProjectCompilation(profile));
    std::get<mdl::CompilationRunTool>(profile.tasks.front()).enabled = false;
    profile.tasks.push_back(mdl::CompilationRunTool{true, "compiler", "ordinary", true});
    CHECK_FALSE(isDirectProjectCompilation(profile));
  }
  SECTION("an enabled map task needs a snapshot")
  {
    profile.tasks.push_back(mdl::CompilationExportMap{true, false, {}, {}, "output.map"});
    CHECK_FALSE(isDirectProjectCompilation(profile));
  }
  SECTION("map path variables need a snapshot, including expressions")
  {
    auto& tool = std::get<mdl::CompilationRunTool>(profile.tasks.front());
    tool.parameterSpec += " ${MAP_DIR_PATH + '/input.map'}";
    CHECK_FALSE(isDirectProjectCompilation(profile));
  }
  SECTION("a project path in an output task does not select direct compilation")
  {
    profile.tasks = {mdl::CompilationExportMap{true, false, {}, {}, "${PROJECT_PATH}"}};
    CHECK_FALSE(isDirectProjectCompilation(profile));
  }
}
TEST_CASE("Project library attachment recovers after an initial import failure")
{
  auto directory = QTemporaryDir{};
  REQUIRE(directory.isValid());
  const auto root = pathFromQString(directory.path());
  auto session =
    require(ProjectSession::create(root / "retry.tbproj", "Retry", "Half-Life"));
  const auto library = root / "library";
  REQUIRE(std::filesystem::create_directory(library));
  const auto write = [&](const char* name, const QByteArray& bytes) {
    auto file = QFile{pathAsQString(library / name)};
    REQUIRE(file.open(QIODevice::WriteOnly));
    REQUIRE(file.write(bytes) == bytes.size());
  };
  write("bad.wad", "not a WAD");
  write("good.wav", "RIFF");
  CHECK(session->attachLibrary(library, true).is_error());
  REQUIRE(std::filesystem::remove(library / "bad.wad"));
  const auto retry = require(session->attachLibrary(library, true));
  CHECK(retry.created);
  CHECK(retry.assets == 1);
}

} // namespace tb::ui
