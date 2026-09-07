#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTextEdit>
#include <QThread>
#include <QTreeWidget>

#include "base/Logger.h"
#include "base/PreferenceManager.h"
#include "base/Result.h"
#include "fs/TestEnvironment.h"
#include "gl/GlManager.h"
#include "gl/Material.h"
#include "gl/MaterialCollection.h"
#include "gl/MaterialManager.h"
#include "gl/ResourceManager.h"
#include "gl/TextureResource.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityProperties.h"
#include "mdl/GameConfigFixture.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "mdl/Map_Brushes.h"
#include "mdl/Map_Entities.h"
#include "mdl/Map_Geometry.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/NodeHandles.h"
#include "mdl/WorldNode.h"
#include "prefs/Preferences.h"
#include "ui/AppControllerFixture.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/python/PythonHandleRegistry.h"
#include "ui/python/PythonPluginManager.h"
#include "ui/python/PythonPluginSession.h"
#include "ui/python/PythonRuntime.h"
#include "ui/python/PythonScripting.h"

#include "vm/approx.h"
#include "vm/bbox.h"

#include <fstream>
#include <map>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{
class TestLogger : public Logger
{
public:
  std::vector<std::string> messages;

private:
  void doLog(const LogLevel, const std::string_view message) override
  {
    messages.emplace_back(message);
  }
};

struct CurrentPathGuard
{
  explicit CurrentPathGuard(std::filesystem::path newCurrentPath)
    : m_oldCurrentPath{std::filesystem::current_path()}
  {
    std::filesystem::current_path(std::move(newCurrentPath));
  }

  ~CurrentPathGuard() { std::filesystem::current_path(m_oldCurrentPath); }

private:
  std::filesystem::path m_oldCurrentPath;
};

vm::vec2f textureCoords(const mdl::BrushFace& face, const vm::vec3d& point)
{
  const auto uvAttributes = face.uvAttributes();
  return vm::vec2f{
    face.toUvCoordSystemMatrix(uvAttributes.offset, uvAttributes.scale) * point};
}

std::vector<QWidget*> pluginPanels(MapWindow& window)
{
  auto result = std::vector<QWidget*>{};
  for (auto* widget : window.findChildren<QWidget*>())
  {
    if (widget->objectName() == QStringLiteral("PluginInspector_PluginPanel"))
    {
      result.push_back(widget);
    }
  }
  return result;
}
} // namespace

TEST_CASE("PythonApi")
{
  setPref(Preferences::DefaultPluginPaths, "");
  setPref(Preferences::PythonPluginDirectories, "");

  auto appControllerFixture = AppControllerFixture{};
  auto& appController = appControllerFixture.appController();
  auto document = MapDocument::createDocument(
                    appController.environmentConfig(),
                    mdl::QuakeGameInfo,
                    mdl::MapFormat::Valve,
                    vm::bbox3d{8192.0},
                    appController.taskManager(),
                    appController.glManager().resourceManager())
                  | kdl::value();
  auto window = MapWindow{appController, std::move(document)};

  SECTION("runs Python API smoke script")
  {
    auto env = fs::TestEnvironment{};
    auto currentPathGuard = CurrentPathGuard{env.dir()};
    env.createFile(
      "api_smoke.py",
      R"(
import os
import trenchbroom as tb

doc = tb.current_document()
assert doc is not None
assert len(doc.entities) >= 1
assert isinstance(doc.materials, list)
assert isinstance(doc.material_collections, list)
assert tb.documents.current().path == doc.path
assert len(tb.documents.list()) == 1
assert tb.documents.list()[0].path == doc.path
snapshot = tb.documents.snapshot()
assert snapshot["entity_count"] >= 1
assert snapshot["brush_count"] == 0
assert snapshot["point_entity_count"] == 0
assert snapshot["patch_count"] == 0
assert snapshot["content_bounds"] is None
assert snapshot["node_count"] >= 1
assert isinstance(snapshot["worldspawn"], dict)
assert snapshot["selected_node_count"] == 0
assert "grid_size" in tb.objects.snapshot()
assert "has_selection" in tb.objects.inspect()
save_path = os.path.abspath("python-api-save.map")
assert tb.documents.save_as(save_path).path == save_path
assert os.path.isfile(save_path)
assert tb.documents.save_current().path == save_path
export_path = os.path.abspath("python-api-export.map")
assert tb.documents.export(export_path).path == save_path
assert os.path.isfile(export_path)
assert len(tb.entities.find(classname="worldspawn")) == 1
assert len(tb.entities.find(property="classname", value="world")) == 1
assert len(tb.brushes.list()) == 0
assert tb.objects.selection().brushes == []
tb.objects.set_selection([])
assert tb.entities.list()[0].classname == "worldspawn"
assert tb.entities.selected() == []
assert tb.brushes.selected() == []
assert tb.faces.selected() == []
assert isinstance(tb.materials.list(), list)
assert isinstance(tb.materials.collections(), list)
assert isinstance(tb.materials.current(), str)
assert tb.materials.search("definitely-not-a-loaded-material") == []
assert isinstance(tb.actions.list(), list)
history = tb.history.status()
assert {"can_undo", "can_redo", "undo_name", "redo_name"} <= set(history)
brush = tb.brushes.create([(-16,-16,-16),(16,-16,-16),(16,16,-16),(16,16,-16),
                           (-16,-16,16),(16,-16,16),(16,16,16),(-16,16,16)])
all_brushes = tb.brushes.list()
assert len(all_brushes) >= 1
assert len(all_brushes[0].faces()) > 0
assert doc.id.startswith("doc:")
assert all_brushes[0].id.startswith("mcp:")
all_faces = tb.faces.list()
assert len(all_faces) >= 1
assert all_faces[0].id.startswith("face:mcp:")
tb.faces.set_material([all_faces[0], all_faces[0]], "python-api-material")
assert tb.faces.list()[0].material == "python-api-material"
assert tb.documents.snapshot()["brush_count"] >= 1
assert tb.documents.snapshot()["content_bounds"] is not None
assert tb.objects.inspect()["brush_count"] >= 1
created_entity = tb.entities.create(
    "info_player_start", {"targetname": "python-api-entity"}, (16, 32, 48))
assert created_entity.classname == "info_player_start"
assert created_entity.id.startswith("mcp:")
assert created_entity["targetname"] == "python-api-entity"
assert len(tb.entities.find(property="targetname", value="python-api-entity")) == 1
tb.entities.update(created_entity, {"health": "100"}, ["targetname"])
assert created_entity["health"] == "100"
assert "targetname" not in created_entity
tb.entities.properties_update(
    [created_entity, created_entity], {"targetname": "python-api-entity", "armor": "50"})
updated_entity = tb.entities.find(property="targetname", value="python-api-entity")[0]
assert updated_entity["armor"] == "50"
tb.entities.properties_delete([updated_entity], ["armor"])
created_entity = tb.entities.find(property="targetname", value="python-api-entity")[0]
assert "armor" not in created_entity
tb.entities.delete(created_entity)
assert len(tb.entities.find(property="targetname", value="python-api-entity")) == 0
with doc.transaction("Python API smoke"):
    pass
with open("python-api-smoke-ok.txt", "w", encoding="utf-8") as f:
    f.write(doc.entities[0].classname)
opened = tb.documents.open(save_path)
assert opened.path == save_path
assert tb.documents.activate(opened).id == opened.id
assert len(tb.documents.list()) == 1
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "api_smoke.py";

    const auto scriptSucceeded =
      PythonRuntime::instance().runScript(context, context.scriptPath);
    CAPTURE(PythonRuntime::instance().lastError());
    CHECK(scriptSucceeded);
    CHECK(env.loadFile("python-api-smoke-ok.txt") == "worldspawn");
  }

  SECTION("runs isolated MCP Python globals and rolls back invalid results")
  {
    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();

    auto& runtime = PythonRuntime::instance();
    const auto first = runtime.runMcpScript(
      context,
      PythonMcpExecutionRequest{
        "import sys\n"
        "print('mcp stdout')\n"
        "print('mcp stderr', file=sys.stderr)\n"
        "result = {'answer': arguments['value']}\n"
        "private_value = 42",
        "<mcp-python:first>",
        QJsonObject{{"value", 42}},
      });
    REQUIRE(first.ok);
    CHECK(first.value.toObject().value("answer").toInt() == 42);
    CHECK_FALSE(first.mutatedDocument);
    CHECK(first.stdoutText.contains("mcp stdout"));
    CHECK(first.stderrText.contains("mcp stderr"));
    CHECK(first.discardedLogBytes == 0);

    const auto second = runtime.runMcpScript(
      context,
      PythonMcpExecutionRequest{
        "result = globals().get('private_value')",
        "<mcp-python:second>",
        {},
      });
    REQUIRE(second.ok);
    CHECK(second.value.isNull());

    auto transactionOnlyContext = context;
    transactionOnlyContext.mcpExecution = true;
    transactionOnlyContext.allowNonTransactionalActions = false;
    transactionOnlyContext.allowPersistentUi = false;
    const auto rejectedAction = runtime.runMcpScript(
      transactionOnlyContext,
      PythonMcpExecutionRequest{
        "import trenchbroom as tb\ntb.current_document().save()",
        "<mcp-python:transaction-action>",
        {},
      });
    CHECK_FALSE(rejectedAction.ok);
    CHECK(rejectedAction.rolledBack);
    CHECK(rejectedAction.error.contains("requires mode='action'"));

    const auto beforeInvalidResult = runtime.runMcpScript(
      context,
      PythonMcpExecutionRequest{
        "import trenchbroom as tb\nresult = "
        "len(tb.current_document().entities[0].brushes)",
        "<mcp-python:before-invalid>",
        {},
      });
    REQUIRE(beforeInvalidResult.ok);
    const auto invalid = runtime.runMcpScript(
      context,
      PythonMcpExecutionRequest{
        "import trenchbroom as tb\n"
        "tb.create_brush([(-16,-16,-16),(16,-16,-16),(16,16,-16),(-16,16,-16),"
        "(-16,-16,16),(16,-16,16),(16,16,16),(-16,16,16)])\n"
        "result = object()",
        "<mcp-python:rollback>",
        {},
      });
    CHECK_FALSE(invalid.ok);
    CHECK(invalid.rolledBack);
    const auto afterInvalidResult = runtime.runMcpScript(
      context,
      PythonMcpExecutionRequest{
        "import trenchbroom as tb\nresult = "
        "len(tb.current_document().entities[0].brushes)",
        "<mcp-python:after-invalid>",
        {},
      });
    REQUIRE(afterInvalidResult.ok);
    CHECK(afterInvalidResult.value == beforeInvalidResult.value);

    const auto timedOut = runtime.runMcpScript(
      context,
      PythonMcpExecutionRequest{
        "try:\n"
        "    while True:\n"
        "        pass\n"
        "except TimeoutError:\n"
        "    result = 'caught'",
        "<mcp-python:timeout>",
        {},
        "MCP Python timeout",
        1,
      });
    CHECK_FALSE(timedOut.ok);
    CHECK(timedOut.timedOut);
    CHECK(timedOut.rolledBack);
  }

  SECTION("keeps the public API catalog synchronized with trenchbroom bindings")
  {
    auto env = fs::TestEnvironment{};
    env.createFile(
      "python_api_catalog.py",
      R"(
import trenchbroom as tb

for type_name, expected_names in tb._api_catalog.items():
    target = tb if type_name == "trenchbroom" else getattr(tb, type_name)
    actual_names = {name for name in vars(target) if not name.startswith("_")}
    assert actual_names == set(expected_names), (
        type_name,
        sorted(actual_names - set(expected_names)),
        sorted(set(expected_names) - actual_names),
    )
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "python_api_catalog.py";

    const auto scriptSucceeded =
      PythonRuntime::instance().runScript(context, context.scriptPath);
    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(scriptSucceeded);
  }

  SECTION("loads manifest plugin and emits events")
  {
    auto env = fs::TestEnvironment{};
    auto currentPathGuard = CurrentPathGuard{env.dir()};
    env.createDirectory("plugin");
    env.createFile(
      "plugin/trenchbroom-plugin.json",
      R"({
        "id": "codex.api",
        "name": "Codex API",
        "version": "1.0.0",
        "apiVersion": 2,
        "pluginType": "ui",
        "entry": "main.py"
      })");
    env.createFile(
      "plugin/main.py",
      R"(
import trenchbroom as tb

panel = tb.create_plugin_panel("API Plugin")
panel.add_label("Loaded")

def on_selection_changed():
    with open("python-api-event-ok.txt", "w", encoding="utf-8") as f:
        f.write("selection")

tb.register_callback("selection_changed", on_selection_changed)
)");

    auto manager = PythonPluginManager{};
    manager.reload({env.dir() / "plugin"});
    REQUIRE(manager.errors().empty());
    REQUIRE(manager.plugins().size() == 1u);
    CHECK(manager.loadPlugins(window));

    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents();

    CHECK_FALSE(pluginPanels(window).empty());

    PythonRuntime::instance().emitEvent("selection_changed", window);
    CHECK(env.loadFile("python-api-event-ok.txt") == "selection");

    REQUIRE(manager.plugins()[0].session != nullptr);
    manager.unloadPlugins(window);
    CHECK(manager.plugins()[0].session == nullptr);
  }

  SECTION("redirects stdout and stderr")
  {
    auto env = fs::TestEnvironment{};
    env.createFile(
      "api_streams.py",
      R"(
import sys

print("hello stdout")
print("hello stderr", file=sys.stderr)
)");

    auto logger = TestLogger{};
    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &logger;
    context.scriptPath = env.dir() / "api_streams.py";

    CHECK(PythonRuntime::instance().runScript(context, context.scriptPath));
    CHECK(
      std::find(logger.messages.begin(), logger.messages.end(), "hello stdout")
      != logger.messages.end());
    CHECK(
      std::find(logger.messages.begin(), logger.messages.end(), "hello stderr")
      != logger.messages.end());
  }

  SECTION("runs persistent console commands")
  {
    auto logger = TestLogger{};
    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &logger;

    auto& runtime = PythonRuntime::instance();
    REQUIRE(runtime.runConsoleCommand(context, "console_value = 41"));
    REQUIRE(runtime.runConsoleCommand(context, "console_value + 1"));
    CHECK(logger.messages.back() == "=> 42");

    REQUIRE(runtime.runConsoleCommand(
      context, "trenchbroom.current_document().entities[0].classname"));
    CHECK(logger.messages.back() == "=> 'worldspawn'");

    REQUIRE(runtime.runConsoleCommand(context, "doc.entities[0].classname"));
    CHECK(logger.messages.back() == "=> 'worldspawn'");

    REQUIRE(runtime.runConsoleCommand(context, "len(selected_brushes())"));
    CHECK(logger.messages.back() == "=> 0");

    REQUIRE(runtime.runConsoleCommand(context, "len(selectedBrushes())"));
    CHECK(logger.messages.back() == "=> 0");

    REQUIRE(runtime.runConsoleCommand(
      context,
      "b = "
      "create_brush([(-32,-32,-32),(32,-32,-32),(32,32,-32),(-32,32,-32),(-32,-32,32),("
      "32,-32,32),(32,32,32),(-32,32,32)])"));
    REQUIRE(runtime.runConsoleCommand(context, "sel.set([b])"));
    REQUIRE(runtime.runConsoleCommand(context, "len(selected_brushes())"));
    CHECK(logger.messages.back() == "=> 1");

    REQUIRE(runtime.runConsoleCommand(context, "import trenchbroom as api"));
    REQUIRE(runtime.runConsoleCommand(context, "brush_list = selected_brushes()"));
    REQUIRE(runtime.runConsoleCommand(context, "e = 1"));
    const auto apiRoot = runtime.consoleCompletionRoot(window, "api");
    CHECK(apiRoot.exists);
    CHECK(apiRoot.type == PythonApiValueType{PythonApiType::Module});
    const auto brushRoot = runtime.consoleCompletionRoot(window, "b");
    CHECK(brushRoot.exists);
    CHECK(brushRoot.type == PythonApiValueType{PythonApiType::Brush});
    const auto brushListRoot = runtime.consoleCompletionRoot(window, "brush_list");
    const auto brushListType = PythonApiValueType{PythonApiType::Brush, 1u};
    CHECK(brushListRoot.exists);
    CHECK(brushListRoot.type == brushListType);
    const auto unsupportedRoot = runtime.consoleCompletionRoot(window, "e");
    CHECK(unsupportedRoot.exists);
    CHECK_FALSE(unsupportedRoot.type);
    CHECK_FALSE(runtime.consoleCompletionRoot(window, "missing").exists);

    REQUIRE(runtime.runConsoleCommand(context, "translate(0, 0, 64)"));
    REQUIRE(runtime.runConsoleCommand(context, "rotate(0, 0, 90)"));
    REQUIRE(runtime.runConsoleCommand(context, "duplicate()"));
    REQUIRE(runtime.runConsoleCommand(context, "len(selected_brushes())"));
    CHECK(logger.messages.back() == "=> 1");

    REQUIRE(runtime.runConsoleCommand(context, "delete_selection()"));
    REQUIRE(runtime.runConsoleCommand(context, "len(selected_brushes())"));
    CHECK(logger.messages.back() == "=> 0");

    REQUIRE(runtime.runConsoleCommand(
      context,
      "b2 = "
      "create_brush([(-16,-16,-16),(16,-16,-16),(16,16,-16),(-16,16,-16),(-16,-16,16),("
      "16,-16,16),(16,16,16),(-16,16,16)])"));
    REQUIRE(runtime.runConsoleCommand(context, "sel.set([b2])"));
    REQUIRE(runtime.runConsoleCommand(
      context, "initial_brush_count = len(doc.entities[0].brushes)"));

    REQUIRE(runtime.runConsoleCommand(
      context,
      "for _ in range(8):\n"
      "    duplicate()\n"
      "    translate(64, 0, 16)\n"));
    REQUIRE(runtime.runConsoleCommand(
      context, "len(doc.entities[0].brushes) == initial_brush_count + 8"));
    CHECK(logger.messages.back() == "=> True");

    // A single undo step reverts the entire 8-step procedural generation loop at once
    window.document().map().undoCommand();
    REQUIRE(runtime.runConsoleCommand(
      context, "len(doc.entities[0].brushes) == initial_brush_count"));
    CHECK(logger.messages.back() == "=> True");

    CHECK_FALSE(runtime.runConsoleCommand(context, "1 / 0"));
    CHECK(runtime.lastError().find("ZeroDivisionError") != std::string::npos);

    runtime.cleanupDocument(window);
    CHECK_FALSE(runtime.runConsoleCommand(context, "console_value"));
    CHECK(runtime.lastError().find("NameError") != std::string::npos);
  }

  SECTION("reports tracebacks and plugin load errors")
  {
    auto env = fs::TestEnvironment{};
    env.createDirectory("plugin");
    env.createFile(
      "plugin/trenchbroom-plugin.json",
      R"({
        "id": "codex.api.failure",
        "name": "Codex API Failure",
        "version": "1.0.0",
        "apiVersion": 2,
        "pluginType": "ui",
        "entry": "main.py"
      })");
    env.createFile(
      "plugin/main.py",
      R"(
def explode():
    raise RuntimeError("Python API exploded")

explode()
)");

    auto manager = PythonPluginManager{};
    manager.reload({env.dir() / "plugin"});
    REQUIRE(manager.errors().empty());
    REQUIRE(manager.plugins().size() == 1u);
    CHECK_FALSE(manager.loadPlugins(window));
    REQUIRE(manager.plugins()[0].status == PythonPluginStatus::Failed);
    CHECK(manager.plugins()[0].error.find("Traceback") != std::string::npos);
    CHECK(manager.plugins()[0].error.find("Python API exploded") != std::string::npos);
  }

  SECTION("invalidates document handles on cleanup")
  {
    auto env = fs::TestEnvironment{};
    env.createFile(
      "api_cache_document.py",
      R"(
import trenchbroom as tb

tb._cached_document = tb.current_document()
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "api_cache_document.py";

    REQUIRE(PythonRuntime::instance().runScript(context, context.scriptPath));
    PythonRuntime::instance().cleanupDocument(window);

    env.createFile(
      "api_use_cached_document.py",
      R"(
import trenchbroom as tb

tb._cached_document.entities
)");

    CHECK_FALSE(PythonRuntime::instance().runScript(
      context, env.dir() / "api_use_cached_document.py"));
    CHECK(
      PythonRuntime::instance().lastError().find("Document is no longer valid")
      != std::string::npos);
  }

  SECTION("invalidates entity handles when nodes change")
  {
    auto env = fs::TestEnvironment{};
    env.createFile(
      "api_cache_entity.py",
      R"(
import trenchbroom as tb

tb._cached_entity = tb.current_document().entities[0]
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "api_cache_entity.py";

    REQUIRE(PythonRuntime::instance().runScript(context, context.scriptPath));
    auto* worldNode = static_cast<mdl::Node*>(&window.document().map().worldNode());
    auto nodes = std::vector<mdl::Node*>{worldNode};
    PythonHandleRegistry::instance().invalidateNodes(nodes);

    env.createFile(
      "api_use_cached_entity.py",
      R"(
import trenchbroom as tb

tb._cached_entity.classname
)");

    CHECK_FALSE(PythonRuntime::instance().runScript(
      context, env.dir() / "api_use_cached_entity.py"));
    CHECK(
      PythonRuntime::instance().lastError().find("Entity is no longer valid")
      != std::string::npos);
  }

  SECTION("invalidates entity, brush, and face handles when a subtree is removed")
  {
    auto& map = window.document().map();
    auto entity = mdl::Entity{};
    entity.setClassname("func_detail");
    auto* entityNode = new mdl::EntityNode{std::move(entity)};
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNode =
      new mdl::BrushNode{builder.createCube(64.0, "cached") | kdl::value()};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {entityNode}}});
    mdl::addNodes(map, {{entityNode, {brushNode}}});

    auto env = fs::TestEnvironment{};
    env.createFile(
      "api_cache_brush.py",
      R"(
import trenchbroom as tb

tb._cached_entity = next(e for e in tb.current_document().entities if e.classname == "func_detail")
tb._cached_brush = tb._cached_entity.brushes[0]
tb._cached_face = tb._cached_brush.faces()[0]
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "api_cache_brush.py";

    REQUIRE(PythonRuntime::instance().runScript(context, context.scriptPath));
    mdl::removeNodes(map, {entityNode});

    env.createFile(
      "api_use_cached_entity.py",
      R"(
import trenchbroom as tb

tb._cached_entity.classname
)");
    CHECK_FALSE(PythonRuntime::instance().runScript(
      context, env.dir() / "api_use_cached_entity.py"));
    CHECK(
      PythonRuntime::instance().lastError().find("Entity is no longer valid")
      != std::string::npos);

    env.createFile(
      "api_use_cached_brush.py",
      R"(
import trenchbroom as tb

tb._cached_brush.faces()
)");
    CHECK_FALSE(PythonRuntime::instance().runScript(
      context, env.dir() / "api_use_cached_brush.py"));
    CHECK(
      PythonRuntime::instance().lastError().find("Brush is no longer valid")
      != std::string::npos);

    env.createFile(
      "api_use_cached_face.py",
      R"(
import trenchbroom as tb

tb._cached_face.material
)");
    CHECK_FALSE(
      PythonRuntime::instance().runScript(context, env.dir() / "api_use_cached_face.py"));
    CHECK(
      PythonRuntime::instance().lastError().find("Brush is no longer valid")
      != std::string::npos);
  }

  SECTION("invalidates face handles when brush geometry changes")
  {
    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNode =
      new mdl::BrushNode{builder.createCube(64.0, "cached") | kdl::value()};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {brushNode}}});

    auto env = fs::TestEnvironment{};
    env.createFile(
      "api_cache_face.py",
      R"(
import trenchbroom as tb

tb._cached_brush = tb.current_document().entities[0].brushes[0]
tb._cached_face = tb._cached_brush.faces()[0]
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "api_cache_face.py";

    REQUIRE(PythonRuntime::instance().runScript(context, context.scriptPath));

    mdl::selectNodes(map, {brushNode});
    REQUIRE(mdl::translateSelection(map, vm::vec3d{128.0, 0.0, 0.0}));

    env.createFile(
      "api_use_cached_face_after_geometry_change.py",
      R"(
import trenchbroom as tb

assert len(tb._cached_brush.faces()) == 6
try:
    tb._cached_face.material
except RuntimeError as error:
    assert "Face is no longer valid" in str(error)
else:
    raise AssertionError("Cached face remained valid after brush geometry changed")
)");

    const auto scriptSucceeded = PythonRuntime::instance().runScript(
      context, env.dir() / "api_use_cached_face_after_geometry_change.py");
    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(scriptSucceeded);
  }

  SECTION("runs and clears session timers")
  {
    auto env = fs::TestEnvironment{};
    auto currentPathGuard = CurrentPathGuard{env.dir()};
    env.createDirectory("plugin");
    env.createFile(
      "plugin/trenchbroom-plugin.json",
      R"({
        "id": "codex.api.timer",
        "name": "Codex API Timer",
        "version": "1.0.0",
        "apiVersion": 2,
        "pluginType": "ui",
        "entry": "main.py"
      })");
    env.createFile(
      "plugin/main.py",
      R"(
import trenchbroom as tb

def on_timer():
    with open("python-api-timer-ok.txt", "a", encoding="utf-8") as f:
        f.write("tick\n")

tb.set_interval(on_timer, 10)
)");

    auto manager = PythonPluginManager{};
    manager.reload({env.dir() / "plugin"});
    REQUIRE(manager.loadPlugins(window));

    for (int i = 0;
         i < 10 && !std::filesystem::exists(env.dir() / "python-api-timer-ok.txt");
         ++i)
    {
      QApplication::processEvents();
      QThread::msleep(10);
    }
    CHECK(std::filesystem::exists(env.dir() / "python-api-timer-ok.txt"));

    manager.unloadPlugins(window);
    env.remove("python-api-timer-ok.txt");
    for (int i = 0; i < 5; ++i)
    {
      QApplication::processEvents();
      QThread::msleep(10);
    }
    CHECK_FALSE(std::filesystem::exists(env.dir() / "python-api-timer-ok.txt"));
  }

  SECTION("logs timer callback exceptions")
  {
    auto env = fs::TestEnvironment{};
    env.createDirectory("plugin");
    env.createFile(
      "plugin/trenchbroom-plugin.json",
      R"({
        "id": "codex.api.timer_failure",
        "name": "Codex API Timer Failure",
        "version": "1.0.0",
        "apiVersion": 2,
        "pluginType": "ui",
        "entry": "main.py"
      })");
    env.createFile(
      "plugin/main.py",
      R"(
import trenchbroom as tb

def on_timer():
    raise RuntimeError("timer exploded")

tb.set_timeout(on_timer, 10)
)");

    auto logger = TestLogger{};
    auto manager = PythonPluginManager{};
    manager.reload({env.dir() / "plugin"});
    REQUIRE(manager.plugins().size() == 1u);

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.logger = &logger;
    context.pluginId = manager.plugins()[0].manifest.id;
    context.pluginDirectory = manager.plugins()[0].manifest.directory;
    context.scriptPath =
      manager.plugins()[0].manifest.directory / manager.plugins()[0].manifest.entry;

    auto session = PythonPluginSession{manager.plugins()[0].manifest, std::move(context)};
    REQUIRE(PythonRuntime::instance().runScript(session));

    for (int i = 0; i < 10 && logger.messages.empty(); ++i)
    {
      QApplication::processEvents();
      QThread::msleep(10);
    }

    REQUIRE_FALSE(logger.messages.empty());
    CHECK(logger.messages.back().find("timer exploded") != std::string::npos);
  }

  SECTION("creates common plugin panel controls")
  {
    auto env = fs::TestEnvironment{};
    auto currentPathGuard = CurrentPathGuard{env.dir()};
    env.createDirectory("plugin");
    env.createFile(
      "plugin/trenchbroom-plugin.json",
      R"({
        "id": "codex.api.controls",
        "name": "Codex API Controls",
        "version": "1.0.0",
        "apiVersion": 2,
        "pluginType": "ui",
        "entry": "main.py"
      })");
    env.createFile(
      "plugin/main.py",
      R"(
import trenchbroom as tb

panel = tb.create_plugin_panel("Controls")
panel.add_label("Loaded")
panel.add_button("Run", lambda: open("python-api-button-ok.txt", "w", encoding="utf-8").write("button"))
panel.add_checkbox("Enabled", False, lambda value: open("python-api-checkbox-ok.txt", "w", encoding="utf-8").write(str(value)))
panel.add_line_edit("", lambda value: open("python-api-line-ok.txt", "w", encoding="utf-8").write(value))
panel.add_combo_box(["a", "b"], 0, lambda value: open("python-api-combo-ok.txt", "w", encoding="utf-8").write(value))
)");

    auto manager = PythonPluginManager{};
    manager.reload({env.dir() / "plugin"});
    REQUIRE(manager.loadPlugins(window));

    QApplication::processEvents();
    const auto panels = pluginPanels(window);
    REQUIRE_FALSE(panels.empty());
    auto* panel = panels.back();

    auto* button = panel->findChild<QPushButton*>();
    REQUIRE(button != nullptr);
    const auto buttonPointer = QPointer<QPushButton>{button};
    button->click();
    CHECK(env.loadFile("python-api-button-ok.txt") == "button");

    auto* checkbox = panel->findChild<QCheckBox*>();
    REQUIRE(checkbox != nullptr);
    checkbox->setChecked(true);
    CHECK(env.loadFile("python-api-checkbox-ok.txt") == "True");

    auto* lineEdit = panel->findChild<QLineEdit*>();
    REQUIRE(lineEdit != nullptr);
    lineEdit->setText("hello");
    CHECK(env.loadFile("python-api-line-ok.txt") == "hello");

    auto* comboBox = panel->findChild<QComboBox*>();
    REQUIRE(comboBox != nullptr);
    comboBox->setCurrentIndex(1);
    CHECK(env.loadFile("python-api-combo-ok.txt") == "b");

    manager.unloadPlugins(window);
    CHECK(buttonPointer.isNull());
    CHECK(pluginPanels(window).empty());
    env.remove("python-api-button-ok.txt");
    CHECK_FALSE(std::filesystem::exists(env.dir() / "python-api-button-ok.txt"));
  }

  SECTION("creates named plugin panel fields")
  {
    auto env = fs::TestEnvironment{};
    auto currentPathGuard = CurrentPathGuard{env.dir()};
    env.createFile(
      "fields.py",
      R"(
import trenchbroom as tb

panel = tb.create_plugin_panel("Fields")
panel.add_label_named("status", "Ready")
panel.set_label_text("status", "Updated")
panel.add_text_field("name", "Name", "world", "placeholder")
panel.add_int_field("count", "Count", 7, 1, 16)
panel.add_float_field("scale", "Scale", 0.5, 0.1, 2.0, 2, 0.1)
panel.add_combo_box("texture", "Texture", ["a", "b"], current="b")
panel.add_checkbox("enabled", "Enabled", True)
assert panel.get_text_field("name") == "world"
assert panel.get_int_field("count") == 7
assert panel.get_float_field("scale") == 0.5
assert panel.get_combo_box_text("texture") == "b"
assert panel.get_checkbox("enabled") is True
panel.add_button_callback("Write", lambda: open("fields-ok.txt", "w", encoding="utf-8").write(panel.get_text_field("name")))
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "fields.py";
    REQUIRE(PythonRuntime::instance().runScript(context, context.scriptPath));

    const auto panels = pluginPanels(window);
    REQUIRE_FALSE(panels.empty());
    auto* panel = panels.back();
    auto* label =
      panel->findChild<QLabel*>(QStringLiteral("trenchbroom_panel_label_status"));
    REQUIRE(label != nullptr);
    CHECK(label->text() == QStringLiteral("Updated"));
    auto* button = panel->findChild<QPushButton*>();
    REQUIRE(button != nullptr);
    button->click();
    CHECK(env.loadFile("fields-ok.txt") == "world");
  }

  SECTION("supports Vec3 math and color fields")
  {
    auto env = fs::TestEnvironment{};
    auto currentPathGuard = CurrentPathGuard{env.dir()};
    env.createFile(
      "vec3_color.py",
      R"(
import trenchbroom as tb

a = tb.Vec3(1, 0, 0)
b = tb.Vec3(0, 1, 0)
assert a.dot(b) == 0
assert tuple(a.cross(b)) == (0.0, 0.0, 1.0)
assert (a + b).length() > 1.4
assert tuple((a * 2) / 2) == (1.0, 0.0, 0.0)
assert tuple(a.normalize()) == (1.0, 0.0, 0.0)

plane = tb.Plane.from_points(tb.Vec3(0, 0, 0), tb.Vec3(1, 0, 0), tb.Vec3(0, 1, 0))
assert tuple(plane.normal) == (0.0, 0.0, 1.0)
assert plane.dist == 0.0
assert plane.distance(tb.Vec3(0, 0, 5)) == 5.0
assert tuple(plane.project(tb.Vec3(1, 2, 5))) == (1.0, 2.0, 0.0)

panel = tb.create_plugin_panel("Vec3 Color")
panel.add_color_field("color", "Color", (1, 2, 3))
assert panel.get_color_field("color") == (1, 2, 3)
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "vec3_color.py";
    const auto scriptSucceeded =
      PythonRuntime::instance().runScript(context, context.scriptPath);
    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(scriptSucceeded);
  }

  SECTION("edits entity properties transactionally")
  {
    auto env = fs::TestEnvironment{};
    env.createFile(
      "api_edit_entity.py",
      R"(
import trenchbroom as tb

entity = tb.current_document().entities[0]
entity.set("message", "hello")
assert entity.get("message") == "hello"
entity.remove("message")
assert entity.get("message") is None
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "api_edit_entity.py";

    REQUIRE(PythonRuntime::instance().runScript(context, context.scriptPath));
    CHECK(window.document().map().worldNode().entity().property("message") == nullptr);
  }

  SECTION("supports pythonic entity dict protocol and brush entity lookup")
  {
    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNode =
      new mdl::BrushNode{builder.createCube(64.0, "selection") | kdl::value()};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {brushNode}}});
    mdl::selectNodes(map, {brushNode});

    auto env = fs::TestEnvironment{};
    env.createFile(
      "api_entity_dict_protocol.py",
      R"(
import trenchbroom as tb

doc = tb.current_document()
entity = doc.entities[0]

# Subscript access and assignment
entity["message"] = "magic_door"
assert entity["message"] == "magic_door"
assert "message" in entity
assert len(entity) > 0

# Dictionary extraction and iteration
props = entity.properties
assert isinstance(props, dict)
assert props["message"] == "magic_door"
assert ("message", "magic_door") in entity.items()
assert "magic_door" in entity.values()

# Repr / str output
r = repr(entity)
assert "Entity(classname=" in r
assert "'message': 'magic_door'" in r

# Brush entity reverse lookup
if entity.brushes:
    brush = entity.brushes[0]
    assert brush.entity.classname == entity.classname
    assert "Brush(" in repr(brush)

# Selection helpers
sel = tb.selection()
assert sel is not None
assert "Selection(" in repr(sel)
assert "Document(" in repr(doc)

# Selection direct property access reads the first selected entity. Assignment applies
# to every selected entity, matching set_property().
assert sel.entity is not None
assert sel.brush is not None
assert sel.classname == "worldspawn"
assert isinstance(sel.properties, dict)
sel["test_sel_key"] = "test_val"
assert sel["test_sel_key"] == "test_val"
assert "test_sel_key" in sel
sel.entity.remove("test_sel_key")
assert "test_sel_key" not in sel

all_ents = tb.selected_all_entities()
assert isinstance(all_ents, list)
inc_ents = tb.selected_entities(include_brushes=True)
assert isinstance(inc_ents, list)

entity = sel.entity
del entity["message"]
assert "message" not in entity
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "api_entity_dict_protocol.py";

    REQUIRE(PythonRuntime::instance().runScript(context, context.scriptPath));
    CHECK(window.document().map().worldNode().entity().property("message") == nullptr);
  }

  SECTION("uses actual selected entities for selection property access")
  {
    auto& map = window.document().map();
    auto env = fs::TestEnvironment{};
    env.createFile(
      "api_empty_selection_properties.py",
      R"(
import trenchbroom as tb

sel = tb.current_document().selection
assert sel.entity is None
assert sel.properties is None
assert sel.classname is None
assert sel.all_entities == []
assert tb.selected_all_entities() == []
assert "classname" not in sel
assert not sel.set_property("empty_selection_key", "value")
try:
    sel["classname"]
except KeyError:
    pass
else:
    raise AssertionError("Empty selection exposed worldspawn properties")
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "api_empty_selection_properties.py";

    auto scriptSucceeded =
      PythonRuntime::instance().runScript(context, context.scriptPath);
    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(scriptSucceeded);
    CHECK(map.worldNode().entity().property("empty_selection_key") == nullptr);

    auto entity = mdl::Entity{};
    entity.setClassname("func_detail");
    entity.addOrUpdateProperty("message", "face owner");
    auto* entityNode = new mdl::EntityNode{std::move(entity)};
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNode =
      new mdl::BrushNode{builder.createCube(64.0, "selection") | kdl::value()};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {entityNode}}});
    mdl::addNodes(map, {{entityNode, {brushNode}}});
    mdl::selectBrushFaces(map, {mdl::BrushFaceHandle{brushNode, 0u}});

    env.createFile(
      "api_face_selection_properties.py",
      R"(
import trenchbroom as tb

sel = tb.current_document().selection
assert sel.entity.classname == "func_detail"
assert sel.classname == "func_detail"
assert sel.properties["message"] == "face owner"
assert [entity.classname for entity in sel.all_entities] == ["func_detail"]
assert sel["message"] == "face owner"
assert "message" in sel
assert not sel.set_property("missing", "ignored", create_if_missing=False)
assert sel.set_property("face_selection_key", "value")
assert sel["face_selection_key"] == "value"
)");

    scriptSucceeded = PythonRuntime::instance().runScript(
      context, env.dir() / "api_face_selection_properties.py");
    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(scriptSucceeded);
    CHECK(entityNode->entity().property("face_selection_key") != nullptr);
    CHECK(entityNode->entity().property("missing") == nullptr);
    CHECK(map.worldNode().entity().property("face_selection_key") == nullptr);
  }

  SECTION("represents non-ASCII document names as UTF-8")
  {
    auto env = fs::TestEnvironment{};
    const auto filename = std::filesystem::path{std::u8string{u8"\u5730\u56fe.map"}};
    REQUIRE(window.document().map().saveAs(env.dir() / filename));

    env.createFile(
      "api_document_repr.py",
      R"PY(
import trenchbroom as tb

doc = tb.current_document()
assert repr(doc) == "Document(name='\u5730\u56fe.map')"
assert str(doc) == "Document(name='\u5730\u56fe.map')"
)PY");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "api_document_repr.py";

    const auto scriptSucceeded =
      PythonRuntime::instance().runScript(context, context.scriptPath);
    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(scriptSucceeded);
  }

  SECTION("rolls back entity edits on script failure")
  {
    auto env = fs::TestEnvironment{};
    env.createFile(
      "api_rollback_entity.py",
      R"(
import trenchbroom as tb

entity = tb.current_document().entities[0]
with tb.current_document().transaction("rollback entity"):
    entity.set("message", "temporary")
    raise RuntimeError("rollback me")
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "api_rollback_entity.py";

    CHECK_FALSE(PythonRuntime::instance().runScript(context, context.scriptPath));
    CHECK(window.document().map().worldNode().entity().property("message") == nullptr);
  }

  SECTION("edits document selection")
  {
    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNode =
      new mdl::BrushNode{builder.createCube(64.0, "original") | kdl::value()};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {brushNode}}});

    auto env = fs::TestEnvironment{};
    env.createFile(
      "api_select.py",
      R"(
import trenchbroom as tb

doc = tb.current_document()
brush = doc.entities[0].brushes[0]
doc.select([brush])
assert len(doc.selection.brushes) == 1
doc.clear_selection()
assert len(doc.selection.brushes) == 0
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "api_select.py";

    REQUIRE(PythonRuntime::instance().runScript(context, context.scriptPath));
    CHECK_FALSE(map.selection().hasAny());
  }

  SECTION("exposes selected triangle UVs to Python")
  {
    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNode = new mdl::BrushNode{
      builder.createBrush(
        std::vector<vm::vec3d>{
          vm::vec3d{-32, 0, 0},
          vm::vec3d{32, 0, 0},
          vm::vec3d{0, 0, 64},
          vm::vec3d{-32, 64, 0},
          vm::vec3d{32, 64, 0},
          vm::vec3d{0, 64, 64},
        },
        "original")
      | kdl::value()};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {brushNode}}});

    const auto faceIndex = brushNode->brush().findFace(vm::vec3d{0, -1, 0});
    REQUIRE(faceIndex);
    mdl::selectBrushFaces(map, {{brushNode, *faceIndex}});

    auto env = fs::TestEnvironment{};
    env.createFile(
      "api_triangle_uv.py",
      R"(
import trenchbroom as tb

doc = tb.current_document()
payload = doc.selection.triangle_uvs()
triangles = payload["triangles"]
assert len(triangles) == 1, f"triangle count: {len(triangles)}"
tri = triangles[0]
assert len(tri["vertices"]) == 3, f"vertex count: {len(tri['vertices'])}"
assert len(tri["loops"]) == 3, f"loop count: {len(tri['loops'])}"

uvs = [(10.0, 20.0), (40.0, 20.0), (10.0, 60.0)]
for loop, uv in zip(tri["loops"], uvs):
    loop["uv"] = uv

assert doc.set_triangle_uvs(payload), "set_triangle_uvs failed"
updated = doc.selection.triangle_uvs()["triangles"][0]
assert [loop["uv"] for loop in updated["loops"]] == uvs, updated
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "api_triangle_uv.py";

    const auto scriptSucceeded =
      PythonRuntime::instance().runScript(context, context.scriptPath);
    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(scriptSucceeded);
  }

  SECTION("exposes face vertices and UV loops to Python")
  {
    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNode =
      new mdl::BrushNode{builder.createCube(64.0, "original") | kdl::value()};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {brushNode}}});
    mdl::selectNodes(map, {brushNode});

    auto env = fs::TestEnvironment{};
    env.createFile(
      "api_face_uv.py",
      R"(
import trenchbroom as tb

doc = tb.current_document()
face = doc.selection.brushes[0].faces()[0]
assert len(face.vertices) == 4, face.vertices
loops = face.uv_loops
assert len(loops) == 4, loops
uvs = [(16.0, 8.0), (80.0, 8.0), (80.0, 40.0), (16.0, 40.0)]
for loop, uv in zip(loops, uvs):
    loop["uv"] = uv
assert face.set_uv_loops(loops)
assert [loop["uv"] for loop in face.uv_loops] == uvs, face.uv_loops
bad = face.uv_loops
bad[3]["uv"] = (23.0, 40.0)
assert not face.set_uv_loops(bad)
assert [loop["uv"] for loop in face.uv_loops] == uvs, face.uv_loops
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "api_face_uv.py";

    const auto scriptSucceeded =
      PythonRuntime::instance().runScript(context, context.scriptPath);
    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(scriptSucceeded);
  }

  SECTION("exposes selected brush faces and updates their UVs atomically")
  {
    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNode =
      new mdl::BrushNode{builder.createCube(64.0, "original") | kdl::value()};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {brushNode}}});
    mdl::selectBrushFaces(map, {{brushNode, 0u}, {brushNode, 1u}});

    auto env = fs::TestEnvironment{};
    env.createFile(
      "api_selected_face_uv.py",
      R"(
import trenchbroom as tb

doc = tb.current_document()
faces = doc.selection.brush_faces
assert len(faces) == 2, len(faces)

updates = []
expected = []
for face_index, face in enumerate(faces):
    loops = face.uv_loops
    base = (face_index + 1) * 32.0
    target = [
        (base, 16.0),
        (base + 64.0, 16.0),
        (base + 64.0, 80.0),
        (base, 80.0),
    ]
    for loop, uv in zip(loops, target):
        loop["uv"] = uv
    updates.append({"face": face, "material": f"updated_{face_index}", "loops": loops})
    expected.append(target)

assert doc.set_face_uvs(updates)
for face_index, (face, target) in enumerate(zip(doc.selection.brush_faces, expected)):
    assert [loop["uv"] for loop in face.uv_loops] == target, face.uv_loops
    assert face.texture_name == f"updated_{face_index}", face.texture_name

before = [[loop["uv"] for loop in face.uv_loops] for face in doc.selection.brush_faces]
bad_updates = []
for face in doc.selection.brush_faces:
    bad_updates.append({"face": face, "loops": face.uv_loops})
bad_updates[1]["loops"][3]["uv"] = (
    bad_updates[1]["loops"][3]["uv"][0] + 7.0,
    bad_updates[1]["loops"][3]["uv"][1],
)
assert not doc.set_face_uvs(bad_updates)
after = [[loop["uv"] for loop in face.uv_loops] for face in doc.selection.brush_faces]
assert after == before, (before, after)
assert doc.set_face_uvs_with_split(bad_updates)
assert len(doc.selection.brushes) == 2, len(doc.selection.brushes)
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "api_selected_face_uv.py";

    const auto scriptSucceeded =
      PythonRuntime::instance().runScript(context, context.scriptPath);
    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(scriptSucceeded);
  }

  SECTION("reads selected brush and vertex tool vertices")
  {
    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNode = new mdl::BrushNode{
      builder.createCuboid(
        vm::bbox3d{vm::vec3d{1.0, 2.0, 3.0}, vm::vec3d{65.0, 66.0, 67.0}}, "original")
      | kdl::value()};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {brushNode}}});
    mdl::selectNodes(map, {brushNode});

    map.nodeHandles().addHandles<mdl::VertexHandle>(*brushNode);
    map.nodeHandles().selectHandle(mdl::VertexHandle{vm::vec3d{1.0, 2.0, 3.0}});

    auto env = fs::TestEnvironment{};
    env.createFile(
      "api_selection_vertices.py",
      R"(
import trenchbroom as tb

doc = tb.current_document()
assert len(doc.vertex_tool_vertices()) == 1
assert tuple(doc.vertex_tool_vertices()[0]) == (1.0, 2.0, 3.0)

verts_by_brush = doc.selection.brush_vertices()
assert len(verts_by_brush) == 1
assert len(verts_by_brush[0]) == 8
assert all(isinstance(v, tb.Vec3) for v in verts_by_brush[0])
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "api_selection_vertices.py";

    REQUIRE(PythonRuntime::instance().runScript(context, context.scriptPath));
  }

  SECTION("edits selection and transforms selected objects")
  {
    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNodeA =
      new mdl::BrushNode{builder.createCube(64.0, "move_a") | kdl::value()};
    auto* brushNodeB =
      new mdl::BrushNode{builder.createCube(64.0, "move_b") | kdl::value()};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {brushNodeA, brushNodeB}}});

    auto env = fs::TestEnvironment{};
    env.createFile(
      "api_selection_transform.py",
      R"(
import trenchbroom as tb

doc = tb.current_document()
world = doc.entities[0]
brush_a = world.brushes[0]
brush_b = world.brushes[1]

doc.selection.set([brush_a])
assert len(doc.selection.brushes) == 1
doc.selection.add([brush_b])
assert len(doc.selection.brushes) == 2
doc.selection.deselect_all()
assert len(doc.selection.brushes) == 0

doc.selection.set([brush_a])
assert doc.selection.translate(128, 0, 0)
assert doc.selection.rotate(0, 0, 1, 90, 0, 0, 0)
assert doc.selection.scale(1, 1, 1, 0, 0, 0)
doc.selection.duplicate()
assert len(doc.selection.brushes) == 1
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "api_selection_transform.py";

    const auto scriptSucceeded =
      PythonRuntime::instance().runScript(context, context.scriptPath);
    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(scriptSucceeded);
    CHECK(map.selection().brushes.size() == 1u);
    CHECK(map.worldNode().defaultLayer()->childCount() == 3u);
  }

  SECTION("chamfers selected vertex and edge handles")
  {
    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* vertexBrush =
      new mdl::BrushNode{builder.createCube(64.0, "chamfer_vertex") | kdl::value()};
    auto* edgeBrush =
      new mdl::BrushNode{builder.createCube(64.0, "chamfer_edge") | kdl::value()};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {vertexBrush, edgeBrush}}});

    map.nodeHandles().addHandles<mdl::VertexHandle>(*vertexBrush);
    const auto vertexHandles = map.nodeHandles().allHandles<mdl::VertexHandle>();
    REQUIRE_FALSE(vertexHandles.empty());
    map.nodeHandles().selectHandle(vertexHandles.front());

    auto env = fs::TestEnvironment{};
    env.createFile(
      "api_chamfer_vertex.py",
      R"(
import trenchbroom as tb

doc = tb.current_document()
doc.selection.set([doc.entities[0].brushes[0]])
assert doc.selection.chamfer_vertices(4.0)
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "api_chamfer_vertex.py";

    auto scriptSucceeded =
      PythonRuntime::instance().runScript(context, context.scriptPath);
    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(scriptSucceeded);
    CHECK(vertexBrush->brush().vertexCount() > 8u);

    map.nodeHandles().clear<mdl::VertexHandle>();
    map.nodeHandles().addHandles<mdl::EdgeHandle>(*edgeBrush);
    const auto edgeHandles = map.nodeHandles().allHandles<mdl::EdgeHandle>();
    REQUIRE_FALSE(edgeHandles.empty());
    map.nodeHandles().selectHandle(edgeHandles.front());

    env.createFile(
      "api_chamfer_edge.py",
      R"(
import trenchbroom as tb

doc = tb.current_document()
doc.selection.set([doc.entities[0].brushes[1]])
assert doc.selection.chamfer_edges(4.0, 2)
)");
    context.scriptPath = env.dir() / "api_chamfer_edge.py";

    scriptSucceeded = PythonRuntime::instance().runScript(context, context.scriptPath);
    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(scriptSucceeded);
    CHECK(edgeBrush->brush().faceCount() > 6u);
  }

  SECTION("sets face material without changing the visible selection")
  {
    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNode =
      new mdl::BrushNode{builder.createCube(64.0, "original") | kdl::value()};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {brushNode}}});
    mdl::selectNodes(map, {brushNode});

    auto env = fs::TestEnvironment{};
    env.createFile(
      "api_set_face_material.py",
      R"(
import trenchbroom as tb

brush = tb.current_document().entities[0].brushes[0]
face = brush.faces()[0]
face.set_material("changed")
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "api_set_face_material.py";

    REQUIRE(PythonRuntime::instance().runScript(context, context.scriptPath));
    CHECK(brushNode->brush().face(0).materialName() == "changed");
    REQUIRE(map.selection().brushes.size() == 1u);
    CHECK(map.selection().brushes.front() == brushNode);
  }

  SECTION("creates brushes and edits face attributes")
  {
    auto env = fs::TestEnvironment{};
    env.createFile(
      "api_create_brush.py",
      R"(
import trenchbroom as tb

half = 32
brush = tb.create_brush([
    tb.Vec3(-half, -half, -half),
    tb.Vec3( half, -half, -half),
    tb.Vec3( half,  half, -half),
    tb.Vec3(-half,  half, -half),
    tb.Vec3(-half, -half,  half),
    tb.Vec3( half, -half,  half),
    tb.Vec3( half,  half,  half),
    tb.Vec3(-half,  half,  half),
], "original")
face = brush.faces()[0]
face.texture_name = "changed"
face.offset = (12.0, 24.0)
face.scale = (0.5, 0.25)
face.rotation = 45.0
face.surface_contents = 7
face.surface_flags = 11
face.surface_value = 3.5
assert face.texture_name == "changed"
assert face.material == "changed"
assert face.offset == (12.0, 24.0)
assert face.scale == (0.5, 0.25)
assert face.rotation == 45.0
assert face.surface_contents == 7
assert face.surface_flags == 11
assert face.surface_value == 3.5
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "api_create_brush.py";

    REQUIRE(PythonRuntime::instance().runScript(context, context.scriptPath));
    const auto& selectedBrushes = window.document().map().selection().brushes;
    REQUIRE(selectedBrushes.size() == 1u);
    const auto& face = selectedBrushes.front()->brush().faces().front();
    CHECK(face.materialName() == "changed");
    CHECK(face.uvAttributes().offset == vm::vec2f{12.0f, 24.0f});
    CHECK(face.uvAttributes().scale == vm::vec2f{0.5f, 0.25f});
    CHECK(face.uvAttributes().rotation == 45.0f);
    CHECK(face.surfaceAttributes().contents == std::optional<int>{7});
    CHECK(face.surfaceAttributes().flags == std::optional<int>{11});
    CHECK(face.surfaceAttributes().value == std::optional<float>{3.5f});
  }

  SECTION("runs Python API brush builder example script")
  {
    const auto pluginDir = std::filesystem::path{"python/examples/brush_builder"};
    REQUIRE(std::filesystem::exists(pluginDir / "trenchbroom-plugin.json"));

    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(PythonScripting::instance().runScript(window, pluginDir / "main.py"));

    const auto& selectedBrushes = window.document().map().selection().brushes;
    REQUIRE(selectedBrushes.size() == 1u);
    CHECK(selectedBrushes.front()->brush().faceCount() == 6u);
  }

  SECTION("loads Python API brush manager example plugin")
  {
    const auto pluginDir = std::filesystem::path{"python/examples/brush_manager"};
    REQUIRE(std::filesystem::exists(pluginDir / "trenchbroom-plugin.json"));

    auto manager = PythonPluginManager{};
    manager.reload({pluginDir});
    REQUIRE(manager.errors().empty());
    REQUIRE(manager.plugins().size() == 1u);
    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(manager.loadPlugins(window));

    const auto panels = pluginPanels(window);
    REQUIRE_FALSE(panels.empty());
    auto* panel = panels.back();

    auto* createButton = static_cast<QPushButton*>(nullptr);
    auto* applyButton = static_cast<QPushButton*>(nullptr);
    auto* analyzeButton = static_cast<QPushButton*>(nullptr);
    for (auto* button : panel->findChildren<QPushButton*>())
    {
      if (button->text() == QStringLiteral("Create Cube"))
      {
        createButton = button;
      }
      if (button->text() == QStringLiteral("Apply to Selection"))
      {
        applyButton = button;
      }
      if (button->text() == QStringLiteral("Analyze Selection"))
      {
        analyzeButton = button;
      }
    }
    REQUIRE(createButton != nullptr);
    REQUIRE(applyButton != nullptr);
    REQUIRE(analyzeButton != nullptr);

    createButton->click();
    auto& selection = window.document().map().selection();
    REQUIRE(selection.brushes.size() == 1u);
    auto* brushNode = selection.brushes.front();
    CHECK(brushNode->brush().faceCount() == 6u);

    applyButton->click();
    CHECK(brushNode->brush().face(0).materialName() == "common/caulk");
    CHECK(brushNode->brush().face(0).uvAttributes().scale == vm::vec2f{1.0f, 1.0f});

    analyzeButton->click();
    auto* status =
      panel->findChild<QLabel*>(QStringLiteral("trenchbroom_panel_label_status"));
    REQUIRE(status != nullptr);
    CAPTURE(status->text().toStdString());
    CHECK(status->text().contains(QStringLiteral("6 faces")));
    manager.unloadPlugins(window);
  }

  SECTION("loads Python API texture replacer example plugin")
  {
    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNode = new mdl::BrushNode{builder.createCube(64.0, "old") | kdl::value()};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {brushNode}}});
    mdl::selectNodes(map, {brushNode});

    const auto pluginDir = std::filesystem::path{"python/examples/texture_replacer"};
    REQUIRE(std::filesystem::exists(pluginDir / "trenchbroom-plugin.json"));

    auto manager = PythonPluginManager{};
    manager.reload({pluginDir});
    REQUIRE(manager.errors().empty());
    REQUIRE(manager.plugins().size() == 1u);
    REQUIRE(manager.loadPlugins(window));

    const auto panels = pluginPanels(window);
    REQUIRE_FALSE(panels.empty());
    auto* panel = panels.back();
    auto* find =
      panel->findChild<QLineEdit*>(QStringLiteral("trenchbroom_panel_text_find"));
    auto* replace =
      panel->findChild<QLineEdit*>(QStringLiteral("trenchbroom_panel_text_replace"));
    REQUIRE(find != nullptr);
    REQUIRE(replace != nullptr);
    find->setText(QStringLiteral("old"));
    replace->setText(QStringLiteral("new"));

    const auto buttons = panel->findChildren<QPushButton*>();
    auto* button = static_cast<QPushButton*>(nullptr);
    for (auto* candidate : buttons)
    {
      if (candidate->text() == QStringLiteral("Replace All in Selection"))
      {
        button = candidate;
      }
    }
    REQUIRE(button != nullptr);
    button->click();
    auto* status =
      panel->findChild<QLabel*>(QStringLiteral("trenchbroom_panel_label_status"));
    REQUIRE(status != nullptr);
    CAPTURE(status->text().toStdString());
    CHECK(brushNode->brush().face(0).materialName() == "new");
    manager.unloadPlugins(window);
  }

  SECTION("loads Python API texture browser example plugin")
  {
    auto material =
      gl::Material{"example/stone", gl::createTextureResource(gl::Texture{64u, 32u})};
    auto materials = std::vector<gl::Material>{};
    materials.push_back(std::move(material));
    auto collections = std::vector<gl::MaterialCollection>{};
    collections.emplace_back(
      std::filesystem::path{"textures/example"}, std::move(materials));
    window.document().map().materialManager().setMaterialCollections(
      std::move(collections));

    const auto pluginDir = std::filesystem::path{"python/examples/texture_browser"};
    REQUIRE(std::filesystem::exists(pluginDir / "trenchbroom-plugin.json"));

    auto manager = PythonPluginManager{};
    manager.reload({pluginDir});
    REQUIRE(manager.errors().empty());
    REQUIRE(manager.plugins().size() == 1u);
    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(manager.loadPlugins(window));

    const auto panels = pluginPanels(window);
    REQUIRE_FALSE(panels.empty());
    auto* panel = panels.back();
    auto* status =
      panel->findChild<QLabel*>(QStringLiteral("trenchbroom_panel_label_status"));
    REQUIRE(status != nullptr);
    const auto text = status->text();
    CAPTURE(text.toStdString());
    CHECK(text.contains(QStringLiteral("textures/example")));
    CHECK(text.contains(QStringLiteral("example/stone (64x32)")));
    manager.unloadPlugins(window);
  }

  SECTION("loads Python API blender brush sync example plugin")
  {
    auto syncEnv = fs::TestEnvironment{};
    const auto syncDir = syncEnv.dir() / "blender-sync";
    REQUIRE(std::filesystem::create_directories(syncDir));
    const auto requestPath = syncDir / "request.json";
    const auto responsePath = syncDir / "response.json";
    const auto pendingRequestPath = syncDir / "pending-request.json";

    auto& map = window.document().map();
    const auto wadPath =
      std::filesystem::absolute("lib/TbMdlLib/test/fixture/mdl/LoadMipTexture/hl.wad");
    mdl::selectNodes(map, {&map.worldNode()});
    REQUIRE(mdl::setEntityProperty(map, "wad", wadPath.generic_string()));

    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNode =
      new mdl::BrushNode{builder.createCube(64.0, "old_sync") | kdl::value()};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {brushNode}}});
    mdl::selectBrushFaces(map, {{brushNode, 0u}});

    const auto pluginDir = std::filesystem::path{"python/examples/blender_brush_sync"};
    REQUIRE(std::filesystem::exists(pluginDir / "trenchbroom-plugin.json"));

    auto manager = PythonPluginManager{};
    manager.reload({pluginDir});
    REQUIRE(manager.errors().empty());
    REQUIRE(manager.plugins().size() == 1u);
    CAPTURE(PythonRuntime::instance().lastError());
    syncEnv.createFile(
      "set_sync_env.py",
      "import os\nos.environ['TB_BLENDER_SYNC_DIR'] = r'" + syncDir.generic_string()
        + "'\n");
    REQUIRE(
      PythonScripting::instance().runScript(window, syncEnv.dir() / "set_sync_env.py"));
    REQUIRE(manager.loadPlugins(window));

    const auto panels = pluginPanels(window);
    REQUIRE_FALSE(panels.empty());
    auto* panel = panels.back();
    auto* sendButton = static_cast<QPushButton*>(nullptr);
    auto* applyButton = static_cast<QPushButton*>(nullptr);
    auto* splitButton = static_cast<QPushButton*>(nullptr);
    for (auto* button : panel->findChildren<QPushButton*>())
    {
      if (button->text() == QStringLiteral("Send Selection"))
      {
        sendButton = button;
      }
      if (button->text() == QStringLiteral("Apply Pending"))
      {
        applyButton = button;
      }
      if (button->text() == QStringLiteral("Apply + Split"))
      {
        splitButton = button;
      }
    }
    REQUIRE(sendButton != nullptr);
    REQUIRE(applyButton != nullptr);
    REQUIRE(splitButton != nullptr);
    auto* status =
      panel->findChild<QLabel*>(QStringLiteral("trenchbroom_panel_label_status"));
    REQUIRE(status != nullptr);

    sendButton->click();

    REQUIRE(std::filesystem::exists(requestPath));
    REQUIRE(std::filesystem::exists(pendingRequestPath));
    auto requestStream = std::ifstream{requestPath};
    const auto requestJson =
      std::string{std::istreambuf_iterator<char>{requestStream}, {}};
    requestStream.close();
    CHECK(requestJson.find(R"("schema": "tb.blenderBrushSync.v1")") != std::string::npos);
    CHECK(requestJson.find(R"("wadPaths")") != std::string::npos);
    CHECK(requestJson.find("hl.wad") != std::string::npos);
    CHECK(requestJson.find(R"("brushes")") != std::string::npos);
    CHECK(requestJson.find(R"("faces")") != std::string::npos);
    CHECK(requestJson.find(R"("selectionMode": "faces")") != std::string::npos);
    CHECK(requestJson.find(R"("face1")") == std::string::npos);

    const auto vertices = brushNode->brush().face(0).vertexPositions();
    REQUIRE(vertices.size() == 4u);
    const auto boundary = brushNode->brush().face(0).boundary();

    const auto sessionKey = std::string{R"("sessionId": )"};
    const auto sessionStart = requestJson.find(sessionKey);
    REQUIRE(sessionStart != std::string::npos);
    const auto sessionValueStart =
      requestJson.find('"', sessionStart + sessionKey.size());
    REQUIRE(sessionValueStart != std::string::npos);
    const auto sessionValueEnd = requestJson.find('"', sessionValueStart + 1);
    REQUIRE(sessionValueEnd != std::string::npos);
    const auto sessionValue =
      requestJson.substr(sessionValueStart, sessionValueEnd - sessionValueStart + 1);

    auto response = std::ofstream{responsePath};
    response << R"({
  "schema": "tb.blenderBrushSync.v1",
  "sessionId": )"
             << sessionValue << R"(,
  "faces": [
    {
      "brushId": "brush0",
      "faceId": "face0",
      "material": "new_sync",
      "loops": [
        {"vertex": 0, "uv": [16.0, 8.0]},
        {"vertex": 1, "uv": [80.0, 8.0]},
        {"vertex": 2, "uv": [80.0, 40.0]},
        {"vertex": 3, "uv": [23.0, 40.0]}
      ]
    }
  ],
  "warnings": []
})";
    response.close();

    applyButton->click();
    CHECK(std::filesystem::exists(responsePath));
    CHECK(brushNode->brush().face(0).materialName() == "old_sync");
    CHECK(status->text().contains(QStringLiteral("Apply + Split")));

    manager.unloadPlugins(window);
    QApplication::processEvents();
    REQUIRE(manager.loadPlugins(window));
    const auto reloadedPanels = pluginPanels(window);
    REQUIRE_FALSE(reloadedPanels.empty());
    panel = reloadedPanels.back();
    status = panel->findChild<QLabel*>(QStringLiteral("trenchbroom_panel_label_status"));
    REQUIRE(status != nullptr);
    CHECK(status->text().contains(QStringLiteral("Recovered pending Blender response")));
    splitButton = nullptr;
    for (auto* button : panel->findChildren<QPushButton*>())
    {
      if (button->text() == QStringLiteral("Apply + Split"))
      {
        splitButton = button;
      }
    }
    REQUIRE(splitButton != nullptr);
    splitButton->click();
    CAPTURE(status->text().toStdString());
    CHECK_FALSE(std::filesystem::exists(responsePath));
    CHECK_FALSE(std::filesystem::exists(requestPath));
    CHECK_FALSE(std::filesystem::exists(pendingRequestPath));
    REQUIRE(map.selection().brushes.size() == 2u);
    CHECK(status->text().contains(QStringLiteral("split 1 non-affine faces")));
    const auto expectedUVs = std::array<vm::vec2f, 4>{
      vm::vec2f{16.0f, 8.0f},
      vm::vec2f{80.0f, 8.0f},
      vm::vec2f{80.0f, 40.0f},
      vm::vec2f{23.0f, 40.0f},
    };
    for (const auto* piece : map.selection().brushes)
    {
      const auto faceIndex = piece->brush().findFace(boundary);
      REQUIRE(faceIndex);
      const auto& face = piece->brush().face(*faceIndex);
      CHECK(face.vertexCount() == 3u);
      CHECK(face.materialName() == "new_sync");
      for (const auto& vertex : face.vertexPositions())
      {
        const auto originalVertex = std::ranges::find(vertices, vertex);
        REQUIRE(originalVertex != std::end(vertices));
        const auto originalIndex =
          size_t(std::distance(std::begin(vertices), originalVertex));
        const auto uv = textureCoords(face, vertex);
        CHECK(uv.x() == vm::approx{expectedUVs[originalIndex].x()});
        CHECK(uv.y() == vm::approx{expectedUVs[originalIndex].y()});
      }
    }
    manager.unloadPlugins(window);
    QApplication::processEvents();
    syncEnv.createFile(
      "clear_sync_env.py", "import os\nos.environ.pop('TB_BLENDER_SYNC_DIR', None)\n");
    REQUIRE(
      PythonScripting::instance().runScript(window, syncEnv.dir() / "clear_sync_env.py"));
    auto ignoredError = std::error_code{};
    std::filesystem::remove(requestPath, ignoredError);
    ignoredError.clear();
    std::filesystem::remove(responsePath, ignoredError);
  }

  SECTION("runs Python API event callback example script")
  {
    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNode =
      new mdl::BrushNode{builder.createCube(64.0, "event_callback") | kdl::value()};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {brushNode}}});
    mdl::selectNodes(map, {brushNode});

    const auto pluginDir = std::filesystem::path{"python/examples/event_callback"};
    REQUIRE(std::filesystem::exists(pluginDir / "trenchbroom-plugin.json"));

    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(PythonScripting::instance().runScript(window, pluginDir / "main.py"));

    PythonRuntime::instance().emitEvent("selection_changed", window);
    CHECK(PythonRuntime::instance().lastError().empty());
  }

  SECTION("loads Python API advanced panel example plugin")
  {
    const auto pluginDir = std::filesystem::path{"python/examples/advanced_panel"};
    REQUIRE(std::filesystem::exists(pluginDir / "trenchbroom-plugin.json"));

    auto manager = PythonPluginManager{};
    manager.reload({pluginDir});
    REQUIRE(manager.errors().empty());
    REQUIRE(manager.plugins().size() == 1u);
    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(manager.loadPlugins(window));

    const auto panels = pluginPanels(window);
    REQUIRE_FALSE(panels.empty());
    auto* panel = panels.back();

    auto* updateButton = static_cast<QPushButton*>(nullptr);
    for (auto* button : panel->findChildren<QPushButton*>())
    {
      if (button->text() == QStringLiteral("Update"))
      {
        updateButton = button;
      }
    }
    REQUIRE(updateButton != nullptr);
    updateButton->click();

    auto* name =
      panel->findChild<QLineEdit*>(QStringLiteral("trenchbroom_panel_text_name"));
    auto* message =
      panel->findChild<QTextEdit*>(QStringLiteral("trenchbroom_panel_text_area_message"));
    auto* table =
      panel->findChild<QTableWidget*>(QStringLiteral("trenchbroom_panel_table_table"));
    auto* tree =
      panel->findChild<QTreeWidget*>(QStringLiteral("trenchbroom_panel_tree_tree"));
    auto* status =
      panel->findChild<QLabel*>(QStringLiteral("trenchbroom_panel_label_status"));
    REQUIRE(name != nullptr);
    REQUIRE(message != nullptr);
    REQUIRE(table != nullptr);
    REQUIRE(tree != nullptr);
    REQUIRE(status != nullptr);

    CHECK(name->text() == QStringLiteral("Run 1"));
    CHECK(message->toPlainText().contains(QStringLiteral("Counter=1")));
    REQUIRE(table->rowCount() == 5);
    REQUIRE(table->item(0, 1) != nullptr);
    CHECK(table->item(0, 1)->text() == QStringLiteral("Item 1 (run 1)"));
    CHECK(tree->topLevelItemCount() == 5);
    CHECK(tree->topLevelItem(0)->text(0) == QStringLiteral("Node 1 (run 1)"));

    table->setCurrentCell(0, 1);
    CHECK(status->text() == QStringLiteral("Table selection: row=0, column=1"));
    tree->setCurrentItem(tree->topLevelItem(0));
    CHECK(status->text() == QStringLiteral("Tree selection: row=0"));
    manager.unloadPlugins(window);
  }

  SECTION("creates Python API html views and exposes document path")
  {
    auto env = fs::TestEnvironment{};
    auto currentPathGuard = CurrentPathGuard{env.dir()};
    env.createFile(
      "api_html_view.py",
      R"(
import trenchbroom as tb

doc = tb.current_document()
assert doc.path is None or isinstance(doc.path, str)
assert callable(doc.save)
assert callable(doc.reload)

panel = tb.create_plugin_panel("HTML View")

def on_link(link):
    with open("python-api-html-link-ok.txt", "w", encoding="utf-8") as f:
        f.write(link)

panel.add_html_view("history", '<a href="tb://history/123">History</a>', 120, on_link)
panel.set_html_view("history", '<a href="tb://history/456">Updated</a>')
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "api_html_view.py";

    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(PythonRuntime::instance().runScript(context, context.scriptPath));

    const auto panels = pluginPanels(window);
    REQUIRE_FALSE(panels.empty());
    auto* htmlView = panels.back()->findChild<QTextBrowser*>(
      QStringLiteral("trenchbroom_panel_html_view_history"));
    REQUIRE(htmlView != nullptr);
    CHECK(htmlView->toPlainText().contains(QStringLiteral("Updated")));

    emit htmlView->anchorClicked(QUrl{QStringLiteral("tb://history/456")});
    CHECK(env.loadFile("python-api-html-link-ok.txt") == "tb://history/456");
  }

  SECTION("loads Python API git plugin example")
  {
    const auto pluginDir = std::filesystem::path{"python/examples/git_plugin"};
    REQUIRE(std::filesystem::exists(pluginDir / "trenchbroom-plugin.json"));

    auto manager = PythonPluginManager{};
    manager.reload({pluginDir});
    REQUIRE(manager.errors().empty());
    REQUIRE(manager.plugins().size() == 1u);
    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(manager.loadPlugins(window));

    const auto panels = pluginPanels(window);
    REQUIRE_FALSE(panels.empty());
    auto* panel = panels.back();
    auto* noDocMessage =
      panel->findChild<QLabel*>(QStringLiteral("trenchbroom_panel_label_no_doc_msg"));
    REQUIRE(noDocMessage != nullptr);
    CHECK(noDocMessage->text().contains(QStringLiteral("Please save the map")));

    manager.unloadPlugins(window);
    QApplication::processEvents();
  }

  SECTION("loads Python API vec3 color demo example plugin")
  {
    auto& map = window.document().map();
    auto* entityNode = new mdl::EntityNode{
      mdl::Entity{{{mdl::EntityPropertyKeys::Classname, "info_player_start"}}}};
    auto nodesToAdd = std::map<mdl::Node*, std::vector<mdl::Node*>>{};
    nodesToAdd.emplace(
      static_cast<mdl::Node*>(map.worldNode().defaultLayer()),
      std::vector<mdl::Node*>{static_cast<mdl::Node*>(entityNode)});
    mdl::addNodes(map, nodesToAdd);

    auto nodesToSelect = std::vector<mdl::Node*>{static_cast<mdl::Node*>(entityNode)};
    mdl::selectNodes(map, nodesToSelect);

    const auto pluginDir = std::filesystem::path{"python/examples/vec3_color_demo"};
    REQUIRE(std::filesystem::exists(pluginDir / "trenchbroom-plugin.json"));

    auto manager = PythonPluginManager{};
    manager.reload({pluginDir});
    REQUIRE(manager.errors().empty());
    REQUIRE(manager.plugins().size() == 1u);
    REQUIRE(manager.loadPlugins(window));

    const auto panels = pluginPanels(window);
    REQUIRE_FALSE(panels.empty());
    auto* panel = panels.back();

    auto* dotButton = static_cast<QPushButton*>(nullptr);
    auto* colorButton = static_cast<QPushButton*>(nullptr);
    for (auto* button : panel->findChildren<QPushButton*>())
    {
      if (button->text() == QStringLiteral("Calculate Dot (A . B)"))
      {
        dotButton = button;
      }
      if (button->text() == QStringLiteral("Apply Color to Selection (_color)"))
      {
        colorButton = button;
      }
    }
    REQUIRE(dotButton != nullptr);
    REQUIRE(colorButton != nullptr);

    dotButton->click();
    auto* result =
      panel->findChild<QLabel*>(QStringLiteral("trenchbroom_panel_label_result"));
    REQUIRE(result != nullptr);
    CHECK(result->text().contains(QStringLiteral("Dot Product")));

    colorButton->click();
    const auto* colorProperty = entityNode->entity().property("_color");
    REQUIRE(colorProperty != nullptr);
    CHECK(*colorProperty == "1.0 0.0 0.0");
    manager.unloadPlugins(window);
  }

  SECTION("loads Python API plane selection example plugin")
  {
    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNode =
      new mdl::BrushNode{builder.createCube(64.0, "plane") | kdl::value()};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {brushNode}}});
    mdl::selectNodes(map, {brushNode});

    const auto pluginDir = std::filesystem::path{"python/examples/plane_selection_demo"};
    REQUIRE(std::filesystem::exists(pluginDir / "trenchbroom-plugin.json"));

    auto manager = PythonPluginManager{};
    manager.reload({pluginDir});
    REQUIRE(manager.errors().empty());
    REQUIRE(manager.plugins().size() == 1u);
    REQUIRE(manager.loadPlugins(window));

    const auto panels = pluginPanels(window);
    REQUIRE_FALSE(panels.empty());
    auto* panel = panels.back();
    auto* label =
      panel->findChild<QLabel*>(QStringLiteral("trenchbroom_panel_label_selection_info"));
    REQUIRE(label != nullptr);

    auto* inspectButton = static_cast<QPushButton*>(nullptr);
    for (auto* button : panel->findChildren<QPushButton*>())
    {
      if (button->text() == QStringLiteral("Inspect Selected Brush Vertices"))
      {
        inspectButton = button;
      }
    }
    REQUIRE(inspectButton != nullptr);
    inspectButton->click();
    CHECK(label->text().contains(QStringLiteral("Brushes: 1")));
    manager.unloadPlugins(window);
  }

  SECTION("runs Python API print selected vertices example script")
  {
    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNode =
      new mdl::BrushNode{builder.createCube(64.0, "verts") | kdl::value()};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {brushNode}}});
    mdl::selectNodes(map, {brushNode});

    const auto pluginDir =
      std::filesystem::path{"python/examples/print_selected_vertices"};
    REQUIRE(std::filesystem::exists(pluginDir / "trenchbroom-plugin.json"));

    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(PythonScripting::instance().runScript(window, pluginDir / "main.py"));
  }

  SECTION("loads Python API plane builder example plugin")
  {
    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNode =
      new mdl::BrushNode{builder.createCube(64.0, "plane_builder") | kdl::value()};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {brushNode}}});
    mdl::selectNodes(map, {brushNode});

    const auto pluginDir = std::filesystem::path{"python/examples/plane_builder"};
    REQUIRE(std::filesystem::exists(pluginDir / "trenchbroom-plugin.json"));

    auto manager = PythonPluginManager{};
    manager.reload({pluginDir});
    REQUIRE(manager.errors().empty());
    REQUIRE(manager.plugins().size() == 1u);
    REQUIRE(manager.loadPlugins(window));
    CHECK(map.worldNode().defaultLayer()->childCount() == 10u);
    manager.unloadPlugins(window);
  }

  SECTION("runs Python API spin entity generator example script")
  {
    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* entityNode = new mdl::EntityNode{mdl::Entity{
      {{mdl::EntityPropertyKeys::Classname, "func_detail"},
       {"_angle", "90"},
       {"_count", "2"},
       {"_pivot", "0 0 0"},
       {"_axis", "0 0 1"}}}};
    auto* brushNode = new mdl::BrushNode{builder.createCube(64.0, "spin") | kdl::value()};

    auto entityNodesToAdd = std::map<mdl::Node*, std::vector<mdl::Node*>>{};
    entityNodesToAdd.emplace(
      static_cast<mdl::Node*>(map.worldNode().defaultLayer()),
      std::vector<mdl::Node*>{static_cast<mdl::Node*>(entityNode)});
    mdl::addNodes(map, entityNodesToAdd);

    auto brushNodesToAdd = std::map<mdl::Node*, std::vector<mdl::Node*>>{};
    brushNodesToAdd.emplace(
      static_cast<mdl::Node*>(entityNode),
      std::vector<mdl::Node*>{static_cast<mdl::Node*>(brushNode)});
    mdl::addNodes(map, brushNodesToAdd);
    mdl::selectNodes(map, {entityNode});

    const auto pluginDir = std::filesystem::path{"python/examples/generator_spin_entity"};
    REQUIRE(std::filesystem::exists(pluginDir / "trenchbroom-plugin.json"));

    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(PythonScripting::instance().runScript(window, pluginDir / "main.py"));
    CHECK(map.selection().nodes.size() == 1u);
  }

  SECTION("loads Python API chamfer example plugins")
  {
    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNode =
      new mdl::BrushNode{builder.createCube(64.0, "chamfer_example") | kdl::value()};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {brushNode}}});
    mdl::selectNodes(map, {brushNode});
    map.nodeHandles().addHandles<mdl::VertexHandle>(*brushNode);
    const auto vertexHandles = map.nodeHandles().allHandles<mdl::VertexHandle>();
    REQUIRE_FALSE(vertexHandles.empty());
    map.nodeHandles().selectHandle(vertexHandles.front());

    auto manager = PythonPluginManager{};
    const auto chamferToolDir = std::filesystem::path{"python/examples/chamfer_tool"};
    manager.reload({chamferToolDir});
    REQUIRE(manager.errors().empty());
    REQUIRE(manager.plugins().size() == 1u);
    REQUIRE(manager.loadPlugins(window));

    const auto panels = pluginPanels(window);
    REQUIRE_FALSE(panels.empty());
    auto* panel = panels.back();
    auto* vertexButton = static_cast<QPushButton*>(nullptr);
    for (auto* button : panel->findChildren<QPushButton*>())
    {
      if (button->text() == QStringLiteral("Chamfer Vertex Handles"))
      {
        vertexButton = button;
      }
    }
    REQUIRE(vertexButton != nullptr);
    vertexButton->click();
    CHECK(brushNode->brush().vertexCount() > 8u);
    manager.unloadPlugins(window);

    const auto simpleChamferDir =
      std::filesystem::path{"python/examples/simple_chamfer_edge"};
    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(PythonScripting::instance().runScript(window, simpleChamferDir / "main.py"));
  }

  SECTION("loads Python API transform tool example plugin")
  {
    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNode =
      new mdl::BrushNode{builder.createCube(64.0, "transform_tool") | kdl::value()};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {brushNode}}});
    mdl::selectNodes(map, {brushNode});
    map.nodeHandles().addHandles<mdl::VertexHandle>(*brushNode);
    const auto vertexHandles = map.nodeHandles().allHandles<mdl::VertexHandle>();
    REQUIRE_FALSE(vertexHandles.empty());
    map.nodeHandles().selectHandle(vertexHandles.front());

    const auto pluginDir = std::filesystem::path{"python/examples/transform_tool"};
    REQUIRE(std::filesystem::exists(pluginDir / "trenchbroom-plugin.json"));

    auto manager = PythonPluginManager{};
    manager.reload({pluginDir});
    REQUIRE(manager.errors().empty());
    REQUIRE(manager.plugins().size() == 1u);
    REQUIRE(manager.loadPlugins(window));

    const auto panels = pluginPanels(window);
    REQUIRE_FALSE(panels.empty());
    auto* panel = panels.back();

    auto* recordButton = static_cast<QPushButton*>(nullptr);
    auto* applyButton = static_cast<QPushButton*>(nullptr);
    for (auto* button : panel->findChildren<QPushButton*>())
    {
      if (button->text() == QStringLiteral("Record Pivot From Vertex Handles"))
      {
        recordButton = button;
      }
      if (button->text() == QStringLiteral("Apply Duplicate + Rotate"))
      {
        applyButton = button;
      }
    }
    REQUIRE(recordButton != nullptr);
    REQUIRE(applyButton != nullptr);

    auto* duplicateCount =
      panel->findChild<QSpinBox*>(QStringLiteral("trenchbroom_panel_int_dup_count"));
    REQUIRE(duplicateCount != nullptr);
    duplicateCount->setValue(1);

    recordButton->click();
    applyButton->click();
    CHECK(map.worldNode().defaultLayer()->childCount() == 2u);
    manager.unloadPlugins(window);
  }

  SECTION("loads Python API distribute tool example plugin")
  {
    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNode =
      new mdl::BrushNode{builder.createCube(64.0, "distribute_tool") | kdl::value()};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {brushNode}}});
    mdl::selectNodes(map, {brushNode});

    map.nodeHandles().addHandles<mdl::VertexHandle>(*brushNode);
    const auto vertexHandles = map.nodeHandles().allHandles<mdl::VertexHandle>();
    REQUIRE(vertexHandles.size() >= 2u);
    auto vertexHandle = vertexHandles.begin();
    map.nodeHandles().selectHandle(*vertexHandle++);
    map.nodeHandles().selectHandle(*vertexHandle);

    const auto pluginDir = std::filesystem::path{"python/examples/distribute_tool"};
    REQUIRE(std::filesystem::exists(pluginDir / "trenchbroom-plugin.json"));

    auto manager = PythonPluginManager{};
    manager.reload({pluginDir});
    REQUIRE(manager.errors().empty());
    REQUIRE(manager.plugins().size() == 1u);
    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(manager.loadPlugins(window));

    const auto panels = pluginPanels(window);
    REQUIRE_FALSE(panels.empty());
    auto* panel = panels.back();

    auto* recordButton = static_cast<QPushButton*>(nullptr);
    auto* distributeButton = static_cast<QPushButton*>(nullptr);
    for (auto* button : panel->findChildren<QPushButton*>())
    {
      if (button->text() == QStringLiteral("Record Path From Selection"))
      {
        recordButton = button;
      }
      if (button->text() == QStringLiteral("Distribute Selected Objects"))
      {
        distributeButton = button;
      }
    }
    REQUIRE(recordButton != nullptr);
    REQUIRE(distributeButton != nullptr);

    recordButton->click();
    distributeButton->click();
    CHECK(map.worldNode().defaultLayer()->childCount() > 1u);
    manager.unloadPlugins(window);
  }

  SECTION("loads Python API curve sweep example plugin")
  {
    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto handleSource =
      mdl::BrushNode{builder.createCube(64.0, "curve_sweep") | kdl::value()};
    map.nodeHandles().addHandles<mdl::VertexHandle>(handleSource);
    const auto vertexHandles = map.nodeHandles().allHandles<mdl::VertexHandle>();
    REQUIRE(vertexHandles.size() >= 3u);
    auto vertexHandle = vertexHandles.begin();
    map.nodeHandles().selectHandle(*vertexHandle++);
    map.nodeHandles().selectHandle(*vertexHandle++);
    map.nodeHandles().selectHandle(*vertexHandle);

    const auto pluginDir = std::filesystem::path{"python/examples/curve_sweep"};
    REQUIRE(std::filesystem::exists(pluginDir / "trenchbroom-plugin.json"));

    auto manager = PythonPluginManager{};
    manager.reload({pluginDir});
    REQUIRE(manager.errors().empty());
    REQUIRE(manager.plugins().size() == 1u);
    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(manager.loadPlugins(window));

    const auto panels = pluginPanels(window);
    REQUIRE_FALSE(panels.empty());
    auto* panel = panels.back();

    auto* recordButton = static_cast<QPushButton*>(nullptr);
    auto* applyButton = static_cast<QPushButton*>(nullptr);
    for (auto* button : panel->findChildren<QPushButton*>())
    {
      if (button->text() == QStringLiteral("Record Path From Vertex Handles"))
      {
        recordButton = button;
      }
      if (button->text() == QStringLiteral("Apply Sweep"))
      {
        applyButton = button;
      }
    }
    REQUIRE(recordButton != nullptr);
    REQUIRE(applyButton != nullptr);

    recordButton->click();
    applyButton->click();
    CHECK(map.worldNode().defaultLayer()->childCount() == 2u);
    manager.unloadPlugins(window);
  }

  SECTION("runs Python API entity brush modifier example script")
  {
    auto& map = window.document().map();
    auto entity = mdl::Entity{};
    entity.setClassname("func_detail");
    auto* entityNode = new mdl::EntityNode{std::move(entity)};

    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNode =
      new mdl::BrushNode{builder.createCube(64.0, "entity_brush") | kdl::value()};

    mdl::addNodes(
      map,
      {{static_cast<mdl::Node*>(map.worldNode().defaultLayer()),
        {static_cast<mdl::Node*>(entityNode)}}});
    mdl::addNodes(
      map, {{static_cast<mdl::Node*>(entityNode), {static_cast<mdl::Node*>(brushNode)}}});
    mdl::selectNodes(map, {entityNode});

    const auto oldOffset = brushNode->brush().face(0).uvAttributes().offset;
    const auto pluginDir = std::filesystem::path{"python/examples/entity_brush_modifier"};
    REQUIRE(std::filesystem::exists(pluginDir / "trenchbroom-plugin.json"));

    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(PythonScripting::instance().runScript(window, pluginDir / "main.py"));

    const auto newOffset = brushNode->brush().face(0).uvAttributes().offset;
    CHECK(newOffset.x() == oldOffset.x() + 16.0f);
    CHECK(newOffset.y() == oldOffset.y());
  }

  SECTION("runs Python API hello panel through the script entry")
  {
    const auto helloPanelScript =
      std::filesystem::path{"python/examples/hello_panel/main.py"};
    REQUIRE(std::filesystem::exists(helloPanelScript));
    REQUIRE(pluginPanels(window).empty());
    CAPTURE(PythonRuntime::instance().lastError());
    CHECK(PythonScripting::instance().runScript(window, helloPanelScript));
    CHECK_FALSE(pluginPanels(window).empty());
  }

  SECTION("runs generated Python API panels through direct script entry")
  {
    auto env = fs::TestEnvironment{};
    env.createFile(
      "direct_api_import.py",
      R"(
import trenchbroom as tb
)");
    env.createFile(
      "direct_api_panel.py",
      R"(
import trenchbroom as tb

panel = tb.create_plugin_panel("API Hello")
)");
    env.createFile(
      "direct_api_label.py",
      R"(
import trenchbroom as tb

panel = tb.create_plugin_panel("API Hello")
panel.add_label("Hello from a direct script.")
)");
    env.createFile(
      "direct_api_button.py",
      R"(
import trenchbroom as tb

panel = tb.create_plugin_panel("API Hello")
panel.add_label("Hello from a direct script.")
panel.add_button("Print", lambda: print("clicked"))
)");

    CHECK(
      PythonScripting::instance().runScript(window, env.dir() / "direct_api_import.py"));
    CHECK(
      PythonScripting::instance().runScript(window, env.dir() / "direct_api_panel.py"));
    CHECK(
      PythonScripting::instance().runScript(window, env.dir() / "direct_api_label.py"));
    CAPTURE(PythonRuntime::instance().lastError());
    CHECK(
      PythonScripting::instance().runScript(window, env.dir() / "direct_api_button.py"));
    CHECK_FALSE(pluginPanels(window).empty());
  }
}

} // namespace tb::ui
