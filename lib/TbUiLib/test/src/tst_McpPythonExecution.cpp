#include <QJsonDocument>
#include <QLocalSocket>
#include <QPointer>
#include <QTest>
#include <QUuid>

#include "fs/TestEnvironment.h"
#include "mdl/GameConfigFixture.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/WorldNode.h"
#include "ui/AppControllerFixture.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/mcp/McpBridgeServer.h"

#include "kd/invoke.h"

#include "vm/mat_ext.h"

#include <chrono>
#include <future>

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
      "tb.current_document().save_as(arguments['path'])\nraise RuntimeError('after "
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
      "tb.current_document().close(discard_changes=True)\nraise RuntimeError('after "
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
    auto started = std::promise<void>{};
    auto disconnected = std::promise<void>{};
    auto startedFuture = started.get_future();
    auto disconnectedFuture = disconnected.get_future();
    auto observed = false;
    auto disconnectedDuringEdit = false;
    const auto connection = map.nodesWereAddedNotifier.connect([&](const auto&) {
      if (!observed)
      {
        observed = true;
        started.set_value();
        disconnectedDuringEdit =
          disconnectedFuture.wait_for(5s) == std::future_status::ready;
      }
    });
    const auto call =
      request("disconnect", create + "result = len(tb.brushes.list())", "transaction");
    const auto line =
      QJsonDocument{mcp::toJson(call)}.toJson(QJsonDocument::Compact) + '\n';
    auto client = std::async(std::launch::async, [&, line, pipe = config.pipeName]() {
      auto socket = QLocalSocket{};
      socket.connectToServer(pipe);
      if (!socket.waitForConnected(5000))
      {
        return QByteArray{};
      }
      socket.write(line);
      socket.flush();
      if (startedFuture.wait_for(5s) != std::future_status::ready)
      {
        return QByteArray{};
      }
      socket.abort();
      disconnected.set_value();
      socket.connectToServer(pipe);
      if (!socket.waitForConnected(5000))
      {
        return QByteArray{};
      }
      socket.write(line);
      socket.flush();
      while (!socket.canReadLine())
      {
        if (!socket.waitForReadyRead(5000))
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
    CHECK(disconnectedDuringEdit);
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
