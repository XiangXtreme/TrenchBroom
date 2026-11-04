/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QPushButton>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include "gl/PerspectiveCamera.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/EditorContext.h"
#include "mdl/GroupNode.h"
#include "mdl/HitAdapter.h"
#include "mdl/HitFilter.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "mdl/Map_Entities.h"
#include "mdl/Map_Groups.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Picking.h"
#include "mdl/Map_Selection.h"
#include "mdl/PickResult.h"
#include "ui/AppControllerFixture.h"
#include "ui/CompilationDialog.h"
#include "ui/Inspector.h"
#include "ui/MapDocument.h"
#include "ui/MapView3D.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/ProjectSession.h"
#include "ui/ProjectWindow.h"
#include "ui/QPathUtils.h"
#include "ui/TerrainInspector.h"

#include <algorithm>
#include <string>

#include <catch2/catch_test_macros.hpp>

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

} // namespace

TEST_CASE("Project terrain panel paints a picked brush")
{
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
  const auto controls = panel->findChildren<QPushButton*>();
  const auto button = [&](const QString& text) {
    const auto found = std::ranges::find_if(
      controls, [&](const auto* candidate) { return candidate->text() == text; });
    REQUIRE(found != controls.end());
    return *found;
  };
  REQUIRE(button("Create Terrain Region")->isEnabled());
  button("Create Terrain Region")->click();
  const auto initial = group->group().terrainPaint();
  REQUIRE_FALSE(initial.empty());
  auto* material = panel->findChild<QComboBox*>();
  REQUIRE(material);
  material->setCurrentIndex(1);
  button("Paint Terrain")->click();
  QApplication::processEvents();
  REQUIRE(panel->painting());
  auto* view = window->currentOrFirstVisible3DMapView();
  REQUIRE(view);
  auto& camera = static_cast<MapViewBase&>(*view).camera();
  camera.setViewport({0, 0, view->width(), view->height()});
  camera.moveTo(vm::vec3f{0, 0, 256});
  camera.lookAt(vm::vec3f{0, 0, 0}, vm::vec3f{0, 1, 0});
  const auto center = view->rect().center();
  auto pick = mdl::PickResult::byDistance();
  mdl::pick(map, vm::ray3d{camera.pickRay(float(center.x()), float(center.y()))}, pick);
  const auto face =
    mdl::hitToFaceHandle(pick.first(mdl::HitFilters::type(mdl::BrushNode::BrushHitType)));
  REQUIRE(face);
  REQUIRE(&face->node() == brush);
  REQUIRE(face->face().normal().z() > 0);
  // The first left click activates a viewport after working in the inspector.
  QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, center);
  QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, center);
  REQUIRE(panel->stroking());
  QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, center);
  const auto painted = group->group().terrainPaint();
  REQUIRE(painted != initial);
  map.undoCommand();
  const auto restored = group->group().terrainPaint() == initial;
  CHECK(restored);
  map.redoCommand();
  const auto repainted = group->group().terrainPaint() == painted;
  CHECK(repainted);
  window->closeDocument(true);
}

TEST_CASE("ProjectCompilation")
{
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

  auto camera = gl::PerspectiveCamera{};
  auto dialog = CompilationDialog{appController, *document, camera};

  auto started = QSignalSpy{&dialog, &CompilationDialog::compilationProfileStarted};
  REQUIRE(dialog.runProfileByName("Probe"));
  REQUIRE(dialog.isRunning());
  // The first task is asynchronous; edit while it is running, before Export Map starts.
  REQUIRE(mdl::setEntityProperty(document->map(), "message", "Later live edit"));
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
  CHECK(document->map().modified());

  SECTION("a later run captures the newer edits")
  {
    REQUIRE(dialog.runProfileByName("Probe"));
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
} // namespace tb::ui
