/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include "gl/PerspectiveCamera.h"
#include "img/GoldSrcTexture.h"
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
#include "mdl/Map_NodeVisibility.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Picking.h"
#include "mdl/Map_Selection.h"
#include "mdl/PickResult.h"
#include "project/ProjectStore.h"
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
  wadBudget->setValue(0);
  CHECK_FALSE(showCuts->isEnabled());
  CHECK_FALSE(showCuts->isChecked());
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
  button("Paint Terrain")->click();
  QApplication::processEvents();
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
  const auto center = view->rect().center();
  auto pick = mdl::PickResult::byDistance();
  mdl::pick(map, vm::ray3d{camera.pickRay(float(center.x()), float(center.y()))}, pick);
  const auto face =
    mdl::hitToFaceHandle(pick.first(mdl::HitFilters::type(mdl::BrushNode::BrushHitType)));
  REQUIRE(face);
  REQUIRE(&face->node() == brush);
  REQUIRE(face->face().normal().z() > 0);
  // Exercise the uncommitted stroke separately from native focus, which the
  // background-window test policy intentionally does not establish.
  view->clearFocus();
  QApplication::processEvents();
  panel->beginStroke(pick);
  REQUIRE(panel->stroking());
  CHECK(group->group().terrainPaint() == initial);
  REQUIRE(QTest::qWaitFor([&] { return centerIs(true); }, 15000));
  REQUIRE(panel->stroking());
  panel->endStroke(false);
  REQUIRE(QTest::qWaitFor([&] { return centerIs(false); }, 15000));
  // The first left click activates a viewport after working in the inspector.
  QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, center);
  QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, center);
  REQUIRE(panel->stroking());
  QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, center);
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
  button("Paint Terrain")->click();
  mdl::deselectAll(map);
  window->switchToInspectorPage(InspectorPage::Entity);
  REQUIRE(QTest::qWaitFor([&] { return centerIs(true); }, 15000));
  mdl::hideNodes(map, {group});
  REQUIRE(QTest::qWaitFor([&] { return !centerIs(true); }, 15000));
  mdl::showNodes(map, {group});
  REQUIRE(QTest::qWaitFor([&] { return centerIs(true); }, 15000));

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
