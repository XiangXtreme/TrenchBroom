#include <QJsonDocument>
#include <QLocalSocket>
#include <QPointer>
#include <QSet>
#include <QTest>
#include <QUuid>

#include "fs/TestEnvironment.h"
#include "gl/GlManager.h"
#include "mdl/EmptyPropertyValueValidator.h"
#include "mdl/EntityDefinitionManager.h"
#include "mdl/GameConfigFixture.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/NonIntegerVerticesValidator.h"
#include "mdl/WorldNode.h"
#include "ui/AppControllerFixture.h"
#include "ui/IssueBrowserView.h"
#include "ui/MapDocument.h"
#include "ui/MapViewport.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/automation/AutomationValidation.h"
#include "ui/mcp/McpBridgeServer.h"
#include "ui/python/PythonRuntime.h"

#include "kd/invoke.h"
#include "kd/result.h"

#include "vm/mat_ext.h"

#include <chrono>
#include <future>
#include <mutex>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
TEST_CASE("McpPythonExecution", "[McpBridgeServer][PythonApi]")
{
  auto fixture = AppControllerFixture{
    [](const auto&) {},
    AppControllerOptions{
      .enableBackgroundServices = false,
      .showMapWindows = false,
      .enableGlResourceProcessing = false,
    }};
  auto& app = fixture.appController();
  REQUIRE(app.mapWindowManager()
            .createDocument(mdl::QuakeGameInfo, mdl::MapFormat::Valve, vm::bbox3d{8192.0})
            .is_success());
  auto* window = app.mapWindowManager().topMapWindow();
  REQUIRE(window);
  const auto liveWindow = QPointer<MapWindow>{window};
  const auto closeWindow = kdl::invoke_later{[&]() {
    // Managed top-level windows outlive AppController unless explicitly closed.
    // Drain deferred deletion before the fixture's editor services are destroyed.
    if (liveWindow)
    {
      liveWindow->closeDocument(true);
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }};
  auto& map = window->document().map();
  auto server = McpBridgeServer{app};
  auto config = mcp::McpBridgeConfig{};
  config.mode = mcp::McpMode::Edit;
  config.httpEnabled = false;
  config.pipeName = "mcp-python-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
  REQUIRE(server.start(config));
  const auto inspected =
    server.dispatchRequest({"document", "tb_inspect", {{"view", "document"}}, {}});
  REQUIRE(inspected.ok);
  const auto target = QJsonObject{
    {"fingerprint", inspected.result.value("fingerprint")},
    {"path", inspected.result.value("path")},
  };
  const auto request = [&](const QString& id, const QString& code, const QString& mode) {
    return mcp::McpBridgeRequest{
      id,
      "tb_execute_python",
      {{"executionId", id},
       {"code", "import trenchbroom as tb\n" + code},
       {"document", target},
       {"mode", mode}},
      {}};
  };
  const auto create = QString{"tb.brushes.create_box((-16,-16,-16), (16,16,16))\n"};

  SECTION("API discovery has complete bounded pages and actual binding metadata")
  {
    auto names = QSet<QString>{};
    auto offset = 0;
    auto total = 0;
    do
    {
      const auto page = server.dispatchRequest(
        {"api", "tb_api", {{"offset", offset}, {"limit", 50}}, {}});
      REQUIRE(page.ok);
      CHECK(
        QJsonDocument{page.result}.toJson(QJsonDocument::Compact).size() <= 16 * 1024);
      total = page.result.value("total").toInt();
      const auto symbols = page.result.value("symbols").toArray();
      REQUIRE_FALSE(symbols.isEmpty());
      for (const auto& value : symbols)
      {
        const auto symbol = value.toObject();
        const auto name = symbol.value("symbol").toString();
        CHECK_FALSE(names.contains(name));
        names.insert(name);
        CHECK_FALSE(symbol.value("signature").toString().isEmpty());
      }
      if (!page.result.value("truncated").toBool())
        break;
      const auto next = page.result.value("nextOffset").toInt();
      REQUIRE(next > offset);
      offset = next;
    } while (offset < total);
    CHECK(names.size() == total);
    const auto symbol = [&](const QString& name) {
      const auto response =
        server.dispatchRequest({"api", "tb_api", {{"symbol", name}}, {}});
      REQUIRE(response.ok);
      const auto entries = response.result.value("symbols").toArray();
      REQUIRE(entries.size() == 1);
      return entries.first().toObject();
    };
    CHECK(symbol("Face.material").value("writable").toBool());
    CHECK_FALSE(symbol("Face.vertices").value("writable").toBool());
    CHECK(symbol("tb.entities.tie_brushes").value("effect") == "edit");
    CHECK(symbol("tb.history.undo").value("effect") == "action");
    CHECK(symbol("tb.assets.search").value("effect") == "read");
    CHECK(
      symbol("tb.entities.update").value("signature").toString().contains("remove_keys"));
    CHECK(
      symbol("tb.assets.place_model").value("signature").toString().contains("property"));
    CHECK(symbol("Face.set_uv_loops")
            .value("description")
            .toString()
            .contains("texture pixel"));
    CHECK(
      symbol("Face.uv_loops").value("description").toString().contains("not normalized"));
    CHECK(symbol("brushes.create_boxes")
            .value("description")
            .toString()
            .contains("'min'"));
    CHECK(symbol("brushes.create_prisms")
            .value("description")
            .toString()
            .contains("'points2d'"));
    CHECK(symbol("viewport.set_options").value("effect") == "action");
    CHECK(symbol("viewport.set_options")
            .value("description")
            .toString()
            .contains("face_render_mode"));
    const auto descriptionSearch =
      server.dispatchRequest({"api", "tb_api", {{"query", "texture pixel"}}, {}});
    REQUIRE(descriptionSearch.ok);
    const auto descriptionMatches = descriptionSearch.result.value("symbols").toArray();
    REQUIRE_FALSE(descriptionMatches.isEmpty());
    CHECK(std::ranges::all_of(descriptionMatches, [](const auto& value) {
      const auto entry = value.toObject();
      return entry.value("description").toString().contains("texture pixel");
    }));
    const auto namedSearch =
      server.dispatchRequest({"api", "tb_api", {{"query", "create boxes"}}, {}});
    REQUIRE(namedSearch.ok);
    REQUIRE_FALSE(namedSearch.result.value("symbols").toArray().isEmpty());
    CHECK(
      namedSearch.result.value("symbols").toArray().first().toObject().value("symbol")
      == "trenchbroom.brushes.create_boxes");
    CHECK_FALSE(server.dispatchRequest({"api", "tb_api", {{"offset", -1}}, {}}).ok);
  }

  SECTION("problem summaries omit ignored issues and details page within the byte budget")
  {
    map.worldNode().unregisterAllValidators();
    map.worldNode().registerValidator(
      std::make_unique<mdl::NonIntegerVerticesValidator>());
    map.worldNode().registerValidator(
      std::make_unique<mdl::EmptyPropertyValueValidator>());
    REQUIRE(server
              .dispatchRequest(request(
                "issue-fixture",
                R"(
tb.brushes.create_boxes([
    {'min': (i*64+.25, 0, 0), 'max': (i*64+32.25, 32, 32)} for i in range(45)
], select=False)
world = tb.entities.find(classname='worldspawn')[0]
world.set('message', '')
world.set('very_long_key_' + 'x'*20000, '')
)",
                "transaction"))
              .ok);
    const auto inspect = [&](QJsonObject params) {
      params.insert("view", "problems");
      return server.dispatchRequest({"inspect", "tb_inspect", std::move(params), {}});
    };
    const auto before = map.modificationCount();
    const auto summary = inspect({});
    REQUIRE(summary.ok);
    CHECK_FALSE(summary.result.contains("issues"));
    CHECK(summary.result.value("count").toInt() >= 47);
    CHECK(summary.result.value("types").toArray().size() == 2);
    auto ids = QSet<QString>{};
    auto offset = 0;
    while (true)
    {
      const auto page = inspect(
        {{"detail", "issues"},
         {"limit", 13},
         {"offset", offset},
         {"types", QJsonArray{"Non-integer vertices"}}});
      REQUIRE(page.ok);
      CHECK(
        QJsonDocument{page.result}.toJson(QJsonDocument::Compact).size() <= 16 * 1024);
      CHECK(page.result.value("count").toInt() == 45);
      const auto items = page.result.value("issues").toArray();
      REQUIRE_FALSE(items.empty());
      CHECK(items.size() <= 13);
      for (const auto& item : items)
      {
        const auto id = item.toObject().value("id").toString();
        CHECK_FALSE(ids.contains(id));
        ids.insert(id);
      }
      if (!page.result.value("truncated").toBool())
      {
        CHECK(page.result.value("nextOffset").isNull());
        break;
      }
      const auto next = page.result.value("nextOffset").toInt();
      REQUIRE(next > offset);
      offset = next;
    }
    CHECK(ids.size() == 45);
    const auto ignored = inspect(
      {{"detail", "issues"}, {"ignoreTypes", QJsonArray{"Non-integer vertices"}}});
    REQUIRE(ignored.ok);
    CHECK(ignored.result.value("ignoredCount").toInt() == 45);
    CHECK(
      ignored.result.value("count").toInt()
      == summary.result.value("count").toInt() - 45);
    CHECK(
      QJsonDocument{ignored.result}.toJson(QJsonDocument::Compact).size() <= 16 * 1024);
    auto longMessageTruncated = false;
    for (const auto& item : ignored.result.value("issues").toArray())
      longMessageTruncated |= item.toObject().value("messageTruncated").toBool();
    CHECK(longMessageTruncated);
    CHECK(map.modificationCount() == before);

    const auto issues = collectAutomationValidationIssues(map, true);
    REQUIRE_FALSE(issues.empty());
    map.setIssueHidden(*issues.front().source, true);
    // Native hiding applies to this node's issue type: both empty world properties.
    CHECK(inspect({}).result.value("hiddenCount").toInt() == 2);
    CHECK(
      inspect({{"includeHidden", true}}).result.value("count")
      == summary.result.value("count"));

    auto* browser = window->findChild<IssueBrowserView*>();
    REQUIRE(browser);
    auto mask = 0;
    for (const auto& issue : issues)
      mask |= issue.type;
    browser->setHiddenIssueTypes(mask);
    CHECK(inspect({}).result.value("count").toInt() == 0);
    CHECK(
      inspect({{"includeHidden", true}}).result.value("count")
      == summary.result.value("count"));
    const auto stillIgnored = inspect(
      {{"includeHidden", true}, {"ignoreTypes", QJsonArray{"Non-integer vertices"}}});
    CHECK(stillIgnored.result.value("ignoredCount").toInt() == 45);
    CHECK_FALSE(inspect({{"ignoreTypes", QJsonArray{"unknown validator"}}}).ok);
    CHECK_FALSE(inspect({{"limit", 0}}).ok);
    CHECK_FALSE(inspect({{"limit", 101}}).ok);
    CHECK_FALSE(inspect({{"limit", "20"}}).ok);
    CHECK_FALSE(inspect({{"offset", -1}}).ok);
    CHECK_FALSE(inspect({{"detail", "all"}}).ok);
  }

  SECTION("viewport options are idempotent and invalid patches have no side effects")
  {
    const auto before = mapViewportOptions(*window);
    const auto restore =
      kdl::invoke_later{[&] { setMapViewportOptions(*window, before); }};
    const auto modificationCount = map.modificationCount();
    const auto response = server.dispatchRequest(request(
      "view-options",
      R"(
options = dict(tb.viewport.state()['options'])
changed = tb.viewport.set_options({'show_edges': False, 'show_grid': False,
    'face_render_mode': 'textured', 'entity_link_mode': 'none'})
assert changed['options']['show_edges'] is False
assert changed['options']['show_grid'] is False
assert changed['options']['face_render_mode'] == 'textured'
assert changed['options']['entity_link_mode'] == 'none'
assert tb.viewport.set_options({'show_edges': False, 'show_grid': False}) == changed
for invalid in [{'show_grid': True, 'unknown': False},
                {'show_grid': True, 'face_render_mode': 'invalid'},
                {'show_grid': True, 'show_edges': 1}]:
    try:
        tb.viewport.set_options(invalid)
    except (ValueError, TypeError):
        pass
    else:
        raise AssertionError('Accepted invalid options')
    assert tb.viewport.state() == changed
for mode, label in [('all', 'Show all entity links'),
                    ('transitive', 'Show transitively selected entity links'),
                    ('direct', 'Show directly selected entity links'),
                    ('none', 'Hide entity links')]:
    tb.actions.execute('Controls/Map view/View Filter > ' + label)
    state = tb.viewport.state()['options']
    assert state['entity_link_mode'] == mode
    assert state['face_render_mode'] == 'textured'
tb.viewport.set_options(options)
)",
      "action"));
    INFO(QJsonDocument{response.error ? response.error->details : response.result}
           .toJson()
           .toStdString());
    REQUIRE(response.ok);
    CHECK(map.modificationCount() == modificationCount);
    CHECK(mapViewportOptions(*window) == before);
    CHECK_FALSE(server
                  .dispatchRequest(request(
                    "view-options-transaction",
                    "tb.viewport.set_options({'show_edges': False})",
                    "transaction"))
                  .ok);
    CHECK(mapViewportOptions(*window) == before);
    const auto failed = server.dispatchRequest(request(
      "view-options-failure",
      "tb.viewport.set_options({'show_edges': False})\nraise RuntimeError('after "
      "options')",
      "action"));
    REQUIRE(failed.error);
    CHECK(failed.error->details.value("completedActions")
            .toObject()
            .contains("viewport.set_options"));
  }

  SECTION("property edits preserve handles and selection through delete and history")
  {
    map.entityDefinitionManager().setDefinitions(
      {{"test_trigger", {}, "", {}, std::nullopt}});
    const auto response = server.dispatchRequest(request(
      "composition",
      R"(
world = tb.entities.find(classname="worldspawn")[0]
other_world = tb.entities.find(classname="worldspawn")[0]
world.set("message", "route")
assert other_world.get("message") == "route"
brush = tb.brushes.create_box((-32,-32,-32), (32,32,32))
keep = tb.brushes.create_box((128,0,0), (160,32,32), select=False)
before = [b.id for b in tb.brushes.selected()]
brush.faces()[0].material = "updated"
assert [b.id for b in tb.brushes.selected()] == before
entity = tb.entities.tie_brushes("test_trigger", [brush])
alias = tb.entities.find(classname="test_trigger")[0]
entity.set("targetname", "checkpoint")
tb.entities.update_many([entity], {"speed": "100"})
assert alias.get("speed") == "100"
tb.documents.current().selection.set([entity.brushes[0], keep])
tb.entities.delete(entity)
assert [b.id for b in tb.brushes.selected()] == [keep.id]
assert tb.documents.snapshot()["selected_node_count"] == 1
try:
    alias.classname
    raise AssertionError("Deleted entity remained live")
except RuntimeError:
    pass
tb.documents.current().selection.clear()
)",
      "transaction"));
    INFO(QJsonDocument{response.error ? response.error->details : response.result}
           .toJson()
           .toStdString());
    REQUIRE(response.ok);
    REQUIRE(
      server.dispatchRequest(request("undo-composition", "tb.history.undo()", "action"))
        .ok);
    REQUIRE(
      server.dispatchRequest(request("redo-composition", "tb.history.redo()", "action"))
        .ok);
    CHECK_FALSE(map.selection().hasAny());
  }

  SECTION("cached editor handles reject worker thread access")
  {
    const auto response = server.dispatchRequest(request(
      "thread-handles",
      R"(
import threading
entity = tb.entities.find(classname="worldspawn")[0]
errors = []
def worker():
    try:
        entity.set("message", "wrong thread")
    except RuntimeError as error:
        errors.append(str(error))
thread = threading.Thread(target=worker)
thread.start()
thread.join()
assert len(errors) == 1 and "execution context" in errors[0]
assert entity.get("message") is None
)",
      "transaction"));
    INFO(QJsonDocument{response.error ? response.error->details : response.result}
           .toJson()
           .toStdString());
    REQUIRE(response.ok);
  }

  SECTION("foreign document handles cannot escape the guarded transaction")
  {
    auto otherDocument = MapDocument::createDocument(
                           app.environmentConfig(),
                           mdl::QuakeGameInfo,
                           mdl::MapFormat::Valve,
                           vm::bbox3d{8192.0},
                           app.taskManager(),
                           app.glManager().resourceManager())
                         | kdl::value();
    auto otherWindow = MapWindow{app, std::move(otherDocument)};
    auto context = PythonExecutionContext{};
    context.mapWindow = &otherWindow;
    context.document = &otherWindow.document();
    context.appController = &app;
    context.logger = &otherWindow.pythonLogger();
    REQUIRE(PythonRuntime::instance().runConsoleCommand(
      context, "import trenchbroom as tb; tb._foreign_document = tb.documents.current()"));
    const auto otherBefore = otherWindow.document().map().modificationCount();
    const auto response = server.dispatchRequest(request(
      "foreign-handle",
      R"(
try:
    assert tb._foreign_document.id != tb.documents.current().id
    tb._foreign_document.entities[0].set("message", "wrong document")
finally:
    del tb._foreign_document
)",
      "transaction"));
    REQUIRE_FALSE(response.ok);
    REQUIRE(response.error);
    CHECK(
      response.error->details.value("error").toString().contains("another MCP document"));
    CHECK(response.error->details.value("rolledBack").toBool());
    CHECK(otherWindow.document().map().modificationCount() == otherBefore);
  }

  SECTION("viewport changes are synchronous actions with truthful receipts")
  {
    const auto before = map.modificationCount();
    const auto camera = server.dispatchRequest(request(
      "camera",
      R"(
state = tb.viewport.set_camera((256, -256, 192), (0, 0, 0))
assert state["projection"] == "perspective"
assert state["position"] == [256, -256, 192]
assert tb.viewport.state() == state
result = state
)",
      "action"));
    INFO(QJsonDocument{camera.error ? camera.error->details : camera.result}
           .toJson()
           .toStdString());
    REQUIRE(camera.ok);
    CHECK(map.modificationCount() == before);
    CHECK_FALSE(camera.result.value("mutatedDocument").toBool());
    const auto state = mapViewportState(*window);
    const auto invalid = server.dispatchRequest(
      request("bad-camera", "tb.viewport.set_camera((0,0,0), (0,0,0))", "action"));
    CHECK_FALSE(invalid.ok);
    CHECK(mapViewportState(*window) == state);
    const auto transactional = server.dispatchRequest(request(
      "camera-transaction", "tb.viewport.set_camera((1,2,3), (0,0,0))", "transaction"));
    CHECK_FALSE(transactional.ok);
    CHECK(mapViewportState(*window) == state);
    const auto failed = server.dispatchRequest(request(
      "camera-failure",
      "tb.viewport.set_camera((512,-256,192),(0,0,0))\nraise RuntimeError('after "
      "camera')",
      "action"));
    REQUIRE(failed.error);
    CHECK(failed.error->details.value("completedActions")
            .toObject()
            .contains("viewport.set_camera"));
  }

  SECTION("large replay payloads expire without repeating edits")
  {
    const auto before = map.modificationCount();
    const auto oversizedId =
      server.dispatchRequest(request(QString(257, 'x'), create, "transaction"));
    REQUIRE_FALSE(oversizedId.ok);
    CHECK(map.modificationCount() == before);
    const auto code = create + "print('created')\nresult = 'x' * 900000";
    for (int index = 0; index < 20; ++index)
    {
      const auto response = server.dispatchRequest(
        request(QString{"large-%1"}.arg(index), code, "transaction"));
      REQUIRE(response.ok);
      CHECK(response.result.value("logs").toObject().value("stdout") == "created\n");
    }
    const auto after = map.modificationCount();
    const auto expired = server.dispatchRequest(request("large-0", code, "transaction"));
    REQUIRE(expired.error);
    CHECK(expired.error->details.value("historicalReplay").toBool());
    CHECK(expired.error->details.value("status") == "receipt_expired");
    CHECK_FALSE(expired.error->details.value("retrySafe").toBool());
    const auto retained =
      server.dispatchRequest(request("large-19", code, "transaction"));
    REQUIRE(retained.ok);
    CHECK(retained.result.value("historicalReplay").toBool());
    CHECK(map.modificationCount() == after);
  }

  SECTION("worldspawn property edits preserve selection before subsequent brush edits")
  {
    map.entityDefinitionManager().setDefinitions({
      {"test_trigger", {}, "", {}, std::nullopt},
    });
    const auto properties = server.dispatchRequest(request(
      "world-properties",
      R"(
world = tb.entities.find(classname="worldspawn")[0]
for key in ["message", "skyname", "MaxRange"]:
    world.set(key, "test")
    assert tb.documents.snapshot()["selected_node_count"] == 0
    world.remove(key)
    assert tb.documents.snapshot()["selected_node_count"] == 0
try:
    tb.entities.delete(world)
    raise AssertionError("Deleted worldspawn")
except ValueError:
    pass
brush = tb.brushes.create_box((-16,-16,-16), (16,16,16), select=True)
tb.documents.current().selection.set_property("message", "course")
assert tb.entities.find(classname="worldspawn")[0].get("message") == "course"
assert tb.documents.snapshot()["selected_node_count"] == 1
assert len(tb.brushes.selected()) == 1
tb.documents.current().selection.clear()
assert tb.documents.snapshot()["selected_node_count"] == 0
)",
      "transaction"));
    if (properties.error)
    {
      INFO(QJsonDocument{properties.error->details}.toJson().toStdString());
      REQUIRE(properties.ok);
    }
    REQUIRE(properties.ok);
    CHECK_FALSE(map.selection().hasAny());

    const auto tied = server.dispatchRequest(request(
      "tie-after-world-properties",
      R"(
brush = tb.brushes.list()[0]
entity = tb.entities.tie_brushes("test_trigger", [brush])
assert len(entity.brushes) == 1
assert entity.brushes[0].entity.classname == "test_trigger"
tb.documents.current().selection.clear()
)",
      "transaction"));
    INFO(QJsonDocument{tied.error ? tied.error->details : tied.result}
           .toJson()
           .toStdString());
    REQUIRE(tied.ok);
    CHECK_FALSE(map.selection().hasAny());
    REQUIRE(
      server.dispatchRequest(request("undo-tie", "tb.history.undo()", "action")).ok);
    REQUIRE(
      server.dispatchRequest(request("redo-tie", "tb.history.redo()", "action")).ok);
    CHECK_FALSE(map.selection().hasAny());
  }

  SECTION("action failure before editing has no mutation or completed actions")
  {
    const auto response =
      server.dispatchRequest(request("before", "raise RuntimeError('before')", "action"));
    REQUIRE(response.error);
    const auto receipt = response.error->details;
    CHECK_FALSE(receipt.value("mutatedDocument").toBool());
    CHECK_FALSE(receipt.value("partialMutation").toBool());
    CHECK(receipt.value("completedActions").toObject().isEmpty());
    CHECK_FALSE(receipt.value("retrySafe").toBool());
  }

  SECTION("action failures preserve edits for every failure exit")
  {
    auto code = QString{};
    SECTION("exception")
    {
      code = "raise RuntimeError('after')";
    }
    SECTION("invalid result")
    {
      code = "result = object()";
    }
    SECTION("oversized result")
    {
      code = "result = 'x' * (1024 * 1024)";
    }
    SECTION("timeout")
    {
      code = "while True: pass";
    }
    const auto before = map.modificationCount();
    auto call = request("after", create + code, "action");
    call.params.insert("timeoutMs", code.startsWith("while") ? 100 : 30000);
    const auto response = server.dispatchRequest(call);
    REQUIRE(response.error);
    CHECK(map.modificationCount() != before);
    CHECK(response.error->details.value("mutatedDocument").toBool());
    CHECK(response.error->details.value("partialMutation").toBool());
    CHECK_FALSE(response.error->details.value("rolledBack").toBool());
  }

  SECTION("undo followed by failure records the completed native action")
  {
    REQUIRE(server.dispatchRequest(request("create", create, "transaction")).ok);
    const auto before = map.modificationCount();
    const auto response = server.dispatchRequest(
      request("undo", "tb.history.undo()\nraise RuntimeError('after undo')", "action"));
    REQUIRE(response.error);
    CHECK(map.modificationCount() != before);
    CHECK(response.error->details.value("mutatedDocument").toBool());
    CHECK(response.error->details.value("partialMutation").toBool());
    CHECK(
      response.error->details.value("completedActions")
        .toObject()
        .value("history.undo")
        .toInt()
      == 1);
    REQUIRE(server.dispatchRequest(request("redo", "tb.history.redo()", "action")).ok);
    CHECK(map.modificationCount() == before);
  }

  SECTION("undo and a replacement edit cannot hide mutation behind the same count")
  {
    REQUIRE(server.dispatchRequest(request("create", create, "transaction")).ok);
    const auto before = map.modificationCount();
    const auto response = server.dispatchRequest(request(
      "replace",
      "tb.history.undo()\ntb.brushes.create_box((64,64,64), (96,96,96))\nraise "
      "RuntimeError('replacement')",
      "action"));
    REQUIRE(response.error);
    CHECK(map.modificationCount() == before);
    CHECK(response.error->details.value("mutatedDocument").toBool());
    CHECK(response.error->details.value("partialMutation").toBool());
  }

  SECTION("save followed by failure retains persistence evidence without a map edit")
  {
    auto env = fs::TestEnvironment{};
    const auto path = env.dir() / "saved.map";
    auto call = request(
      "save",
      "tb.documents.current().save_as(arguments['path'])\nraise RuntimeError('after "
      "save')",
      "action");
    call.params.insert(
      "arguments", QJsonObject{{"path", QString::fromStdWString(path.wstring())}});
    const auto response = server.dispatchRequest(call);
    REQUIRE(response.error);
    CHECK(std::filesystem::is_regular_file(path));
    CHECK_FALSE(response.error->details.value("mutatedDocument").toBool());
    CHECK(response.error->details.value("partialMutation").toBool());
    CHECK(
      response.error->details.value("completedActions")
        .toObject()
        .value("save_as")
        .toInt()
      == 1);
  }

  SECTION("closing the target followed by failure retains lifecycle evidence")
  {
    const auto response = server.dispatchRequest(request(
      "close",
      "tb.documents.current().close(discard_changes=True)\nraise RuntimeError('after "
      "close')",
      "action"));
    REQUIRE(response.error);
    CHECK(app.mapWindowManager().allMapWindowsClosed());
    CHECK(response.error->details.value("mutatedDocument").toBool());
    CHECK(response.error->details.value("partialMutation").toBool());
    CHECK(
      response.error->details.value("completedActions").toObject().value("close").toInt()
      == 1);
  }

  SECTION("transaction failure restores preexisting selection and dirty state")
  {
    REQUIRE(server.dispatchRequest(request("create", create, "transaction")).ok);
    const auto before = map.modificationCount();
    const auto selection = map.selection().nodes;
    const auto response = server.dispatchRequest(
      request("rollback", create + "raise RuntimeError('rollback')", "transaction"));
    REQUIRE(response.error);
    CHECK(map.modificationCount() == before);
    CHECK(map.selection().nodes == selection);
    CHECK_FALSE(response.error->details.value("mutatedDocument").toBool());
    CHECK_FALSE(response.error->details.value("partialMutation").toBool());
    CHECK(response.error->details.value("rolledBack").toBool());
  }

  SECTION("native linked group commit failure rolls back the script")
  {
    auto* group = new mdl::GroupNode{mdl::Group{"commit failure"}};
    mdl::addNodes(map, {{map.worldNode().defaultLayer(), {group}}});
    mdl::deselectAll(map);
    mdl::selectNodes(map, {group});
    // Inject a real native commit failure: pending linked-group synchronization
    // cannot invert this source transformation. No production test hook is used.
    auto invalid = group->group();
    invalid.setTransformation(vm::scaling_matrix(vm::vec3d{0, 1, 1}));
    group->setGroup(std::move(invalid));
    group->setHasPendingChanges(true);
    const auto before = map.modificationCount();
    const auto selection = map.selection().nodes;
    const auto response =
      server.dispatchRequest(request("commit", create, "transaction"));
    REQUIRE(response.error);
    CHECK(response.error->details.value("error").toString().contains("Could not commit"));
    CHECK(response.error->details.value("rolledBack").toBool());
    CHECK_FALSE(response.error->details.value("mutatedDocument").toBool());
    CHECK(map.modificationCount() == before);
    CHECK(map.selection().nodes == selection);
    CHECK(map.selection().brushes.empty());
  }

  SECTION("disconnect after editing then reconnect and replay does not repeat the edit")
  {
    using namespace std::chrono_literals;
    auto editCompleted = std::promise<void>{};
    auto editCompletedFuture = editCompleted.get_future();
    auto editCompletedOnce = std::once_flag{};
    const auto connection = map.nodesWereAddedNotifier.connect([&](const auto&) {
      std::call_once(editCompletedOnce, [&]() { editCompleted.set_value(); });
    });
    const auto call =
      request("disconnect", create + "result = len(tb.brushes.list())", "transaction");
    const auto line =
      QJsonDocument{mcp::toJson(call)}.toJson(QJsonDocument::Compact) + '\n';
    auto client = std::async(std::launch::async, [&, line, pipe = config.pipeName]() {
      constexpr auto clientTimeout = 15s;
      const auto deadline = std::chrono::steady_clock::now() + clientTimeout;
      const auto remainingTimeoutMs = [&]() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                 deadline - std::chrono::steady_clock::now())
          .count();
      };
      auto socket = QLocalSocket{};
      socket.connectToServer(pipe);
      if (const auto timeoutMs = remainingTimeoutMs();
          timeoutMs <= 0 || !socket.waitForConnected(static_cast<int>(timeoutMs)))
      {
        return QByteArray{};
      }
      socket.write(line);
      socket.flush();
      if (const auto timeoutMs = remainingTimeoutMs();
          timeoutMs <= 0 || !socket.waitForBytesWritten(static_cast<int>(timeoutMs)))
      {
        return QByteArray{};
      }
      socket.abort();
      if (const auto timeoutMs = remainingTimeoutMs();
          timeoutMs <= 0
          || editCompletedFuture.wait_for(std::chrono::milliseconds{timeoutMs})
               != std::future_status::ready)
      {
        return QByteArray{};
      }
      socket.connectToServer(pipe);
      if (const auto timeoutMs = remainingTimeoutMs();
          timeoutMs <= 0 || !socket.waitForConnected(static_cast<int>(timeoutMs)))
      {
        return QByteArray{};
      }
      socket.write(line);
      socket.flush();
      while (!socket.canReadLine())
      {
        const auto timeoutMs = remainingTimeoutMs();
        if (timeoutMs <= 0 || !socket.waitForReadyRead(static_cast<int>(timeoutMs)))
        {
          return QByteArray{};
        }
      }
      return socket.readLine();
    });
    // Only the test harness pumps Qt. The worker owns sockets, never editor objects.
    while (client.wait_for(0s) != std::future_status::ready)
    {
      QTest::qWait(10);
    }
    const auto replay =
      mcp::bridgeResponseFromJson(QJsonDocument::fromJson(client.get()).object());
    REQUIRE(replay);
    REQUIRE(replay->ok);
    CHECK(replay->result.value("historicalReplay").toBool());
    const auto count = server.dispatchRequest(
      request("count", "result = len(tb.brushes.list())", "transaction"));
    REQUIRE(count.ok);
    CHECK(count.result.value("result") == replay->result.value("result"));
    auto conflict = call;
    conflict.params.insert("code", "raise RuntimeError('conflict')");
    const auto rejected = server.dispatchRequest(conflict);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code == mcp::McpErrorCode::InvalidParams);
  }
}
} // namespace tb::ui
