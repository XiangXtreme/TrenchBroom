/*
 Copyright (C) 2026 XiangXtreme

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

#include <QColor>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>

#include "../../src/mcp/McpBridgeServerTools.h"
#include "base/Result.h"
#include "gl/GlManager.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushFaceHandle.h"
#include "mdl/BrushNode.h"
#include "mdl/Entity.h"
#include "mdl/EntityDefinition.h"
#include "mdl/EntityDefinitionManager.h"
#include "mdl/EntityNode.h"
#include "mdl/GameConfigFixture.h"
#include "mdl/GroupNode.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "mdl/Map_Entities.h"
#include "mdl/Map_Groups.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/WorldNode.h"
#include "ui/AppControllerFixture.h"
#include "ui/CatchConfig.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/mcp/McpBridgeServer.h"

#include <algorithm>
#include <optional>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace mcp = tb::mcp;

namespace
{

mcp::McpBridgeResponse readBridgeResponse(
  QLocalSocket& socket, const int timeoutMs = 2000)
{
  auto timer = QElapsedTimer{};
  timer.start();
  while (timer.elapsed() < timeoutMs && !socket.canReadLine())
  {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    socket.waitForReadyRead(10);
  }
  REQUIRE(socket.canReadLine());
  auto parseError = QJsonParseError{};
  const auto document = QJsonDocument::fromJson(socket.readLine().trimmed(), &parseError);
  REQUIRE(parseError.error == QJsonParseError::NoError);
  REQUIRE(document.isObject());
  auto error = QString{};
  const auto response = mcp::bridgeResponseFromJson(document.object(), &error);
  INFO(error.toStdString());
  REQUIRE(response);
  return *response;
}

QString uniqueBridgePipeName()
{
  return QString{"tb-test-%1"}.arg(
    QUuid::createUuid().toString(QUuid::WithoutBraces).left(12));
}

void processEventsFor(const int durationMs)
{
  auto eventLoop = QEventLoop{};
  QTimer::singleShot(durationMs, &eventLoop, &QEventLoop::quit);
  eventLoop.exec();
}

} // namespace

TEST_CASE("McpBridgeServer applies action-level history permissions", "[McpBridgeServer]")
{
  auto handlerCalls = 0;
  auto server = McpBridgeServer{[&](const QString&, const QJsonObject&) {
    ++handlerCalls;
    return McpBridgeToolResult::success();
  }};
  REQUIRE(
    server.start(mcp::McpBridgeConfig{uniqueBridgePipeName(), mcp::McpMode::ReadOnly}));

  const auto denied = server.dispatchRequest(mcp::McpBridgeRequest{
    "undo-read-only",
    "tb_history",
    QJsonObject{{"action", "undo"}},
    mcp::McpMode::ReadOnly,
  });
  CHECK_FALSE(denied.ok);
  REQUIRE(denied.error);
  CHECK(denied.error->code == mcp::McpErrorCode::Forbidden);
  CHECK(handlerCalls == 0);

  const auto status = server.dispatchRequest(mcp::McpBridgeRequest{
    "status-read-only",
    "tb_history",
    QJsonObject{{"action", "status"}},
    mcp::McpMode::ReadOnly,
  });
  CHECK(status.ok);
  CHECK(handlerCalls == 1);

  auto editServer = McpBridgeServer{[&](const QString&, const QJsonObject&) {
    ++handlerCalls;
    return McpBridgeToolResult::success();
  }};
  REQUIRE(
    editServer.start(mcp::McpBridgeConfig{uniqueBridgePipeName(), mcp::McpMode::Edit}));
  const auto downgraded = editServer.dispatchRequest(mcp::McpBridgeRequest{
    "undo-downgraded",
    "tb_history",
    QJsonObject{{"action", "undo"}},
    mcp::McpMode::ReadOnly,
  });
  CHECK_FALSE(downgraded.ok);
  REQUIRE(downgraded.error);
  CHECK(downgraded.error->code == mcp::McpErrorCode::Forbidden);
}

TEST_CASE(
  "McpBridgeServer replays guarded Python executions", "[McpBridgeServer][PythonApi]")
{
  auto appControllerFixture = AppControllerFixture{
    [](const auto&) {},
    AppControllerOptions{
      .enableBackgroundServices = false,
      .showMapWindows = false,
      .enableGlResourceProcessing = false,
    }};
  auto& appController = appControllerFixture.appController();
  REQUIRE(appController.mapWindowManager().createDocument(
    mdl::QuakeGameInfo, mdl::MapFormat::Valve, vm::bbox3d{8192.0}));

  auto server = McpBridgeServer{appController};
  REQUIRE(server.start(mcp::McpBridgeConfig{uniqueBridgePipeName(), mcp::McpMode::Edit}));

  const auto inspect = server.dispatchRequest(mcp::McpBridgeRequest{
    "inspect", "tb_inspect", QJsonObject{{"view", "document"}}, mcp::McpMode::ReadOnly});
  REQUIRE(inspect.ok);
  const auto document = inspect.result;
  REQUIRE(document.value("fingerprint").isString());
  CHECK(document.value("fingerprint") == document.value("documentFingerprint"));

  const auto api = server.dispatchRequest(mcp::McpBridgeRequest{
    "api",
    "tb_api",
    QJsonObject{{"symbol", "Document.path"}},
    mcp::McpMode::ReadOnly,
  });
  REQUIRE(api.ok);
  const auto apiSymbols = api.result.value("symbols").toArray();
  REQUIRE(apiSymbols.size() == 1);
  const auto apiSymbol = apiSymbols.first().toObject();
  CHECK(apiSymbol.value("parameters").toString().isEmpty());
  CHECK(apiSymbol.value("returns").toString() == "str | None");
  CHECK(apiSymbol.value("effect").toString() == "read");
  CHECK(apiSymbol.value("example").toString().contains("tb.current_document()"));

  const auto namespacedApi = server.dispatchRequest(mcp::McpBridgeRequest{
    "api-namespaced",
    "tb_api",
    QJsonObject{{"symbol", "trenchbroom.documents.current"}},
    mcp::McpMode::ReadOnly,
  });
  REQUIRE(namespacedApi.ok);
  const auto namespacedSymbols = namespacedApi.result.value("symbols").toArray();
  REQUIRE(namespacedSymbols.size() == 1);
  CHECK(namespacedSymbols.first().toObject().value("returns").toString() == "Document");

  const auto request = mcp::McpBridgeRequest{
    "execute",
    "tb_execute_python",
    QJsonObject{
      {"executionId", "python-replay"},
      {"code", "result = {'answer': arguments['answer']}"},
      {"arguments", QJsonObject{{"answer", 42}}},
      {"document", document},
    },
    mcp::McpMode::Edit};
  const auto first = server.dispatchRequest(request);
  REQUIRE(first.ok);
  CHECK(first.result.value("result").toObject().value("answer").toInt() == 42);
  CHECK_FALSE(first.result.value("historicalReplay").toBool());
  CHECK(first.result.value("logs").isObject());

  auto actionParams = request.params;
  actionParams.insert("executionId", "python-action");
  actionParams.insert("mode", "action");
  const auto action = server.dispatchRequest(mcp::McpBridgeRequest{
    "action", "tb_execute_python", actionParams, mcp::McpMode::Edit});
  REQUIRE(action.ok);
  CHECK(action.result.value("mode").toString() == "action");
  CHECK(action.result.value("status").toString() == "completed");
  CHECK_FALSE(action.result.value("partialMutation").toBool());

  const auto replay = server.dispatchRequest(request);
  REQUIRE(replay.ok);
  CHECK(replay.result.value("historicalReplay").toBool());
  CHECK(replay.result.value("result") == first.result.value("result"));

  auto conflictingParams = request.params;
  conflictingParams.insert("code", "result = {'answer': 7}");
  const auto conflict = server.dispatchRequest(mcp::McpBridgeRequest{
    "conflict", "tb_execute_python", conflictingParams, mcp::McpMode::Edit});
  CHECK_FALSE(conflict.ok);
  REQUIRE(conflict.error);
  CHECK(conflict.error->code == mcp::McpErrorCode::InvalidParams);
  CHECK_FALSE(conflict.error->details.value("retrySafe").toBool(true));
}

TEST_CASE(
  "McpBridgeServer bounds local transport input", "[McpBridgeServer][McpBridgeTransport]")
{
  const auto makeServer = [](const McpBridgeTransportLimits limits) {
    return McpBridgeServer{
      [](const QString& toolName, const QJsonObject&) {
        if (toolName == "render_review_current_scene")
        {
          return McpBridgeToolResult::success(QJsonObject{
            {"tool", toolName},
            {"reviewId", "transport-review"},
            {"resourceUri", "tbmcp://review/transport-review"},
          });
        }
        return McpBridgeToolResult::success(QJsonObject{{"application", "TrenchBroom"}});
      },
      limits};
  };

  SECTION("accepts a fragmented valid request")
  {
    auto server = makeServer(McpBridgeTransportLimits{512, 10'000, 4});
    const auto pipeName = uniqueBridgePipeName();
    REQUIRE(server.start(mcp::McpBridgeConfig{pipeName, mcp::McpMode::ReadOnly}));
    auto socket = QLocalSocket{};
    socket.connectToServer(pipeName);
    REQUIRE(socket.waitForConnected(1000));
    auto line = QJsonDocument{mcp::toJson(mcp::McpBridgeRequest{
                                "1", "tb_status", {}, mcp::McpMode::ReadOnly})}
                  .toJson(QJsonDocument::Compact)
                + "\n";
    const auto split = line.size() / 2;
    REQUIRE(socket.write(line.left(split)) == split);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    CHECK_FALSE(socket.canReadLine());
    REQUIRE(socket.write(line.mid(split)) == line.size() - split);
    CHECK(readBridgeResponse(socket).ok);
  }

  SECTION("rejects a request line beyond the configured budget")
  {
    auto server = makeServer(McpBridgeTransportLimits{64, 10'000, 4});
    const auto pipeName = uniqueBridgePipeName();
    REQUIRE(server.start(mcp::McpBridgeConfig{pipeName, mcp::McpMode::ReadOnly}));
    auto socket = QLocalSocket{};
    socket.connectToServer(pipeName);
    REQUIRE(socket.waitForConnected(1000));
    REQUIRE(socket.write(QByteArray(65, 'x')) == 65);
    const auto response = readBridgeResponse(socket);
    CHECK_FALSE(response.ok);
    REQUIRE(response.error);
    CHECK(response.error->code == mcp::McpErrorCode::InvalidRequest);
    CHECK(response.error->message.contains("too large"));
  }

  SECTION("times out an incomplete request line")
  {
    auto server = makeServer(McpBridgeTransportLimits{512, 50, 4});
    const auto pipeName = uniqueBridgePipeName();
    REQUIRE(server.start(mcp::McpBridgeConfig{pipeName, mcp::McpMode::ReadOnly}));
    auto socket = QLocalSocket{};
    socket.connectToServer(pipeName);
    REQUIRE(socket.waitForConnected(1000));
    REQUIRE(socket.write("{") == 1);
    const auto response = readBridgeResponse(socket, 1000);
    CHECK_FALSE(response.ok);
    REQUIRE(response.error);
    CHECK(response.error->message.contains("timed out"));
  }

  SECTION("rejects excess local connections and recovers capacity")
  {
    auto server = makeServer(McpBridgeTransportLimits{512, 10'000, 1});
    const auto pipeName = uniqueBridgePipeName();
    REQUIRE(server.start(mcp::McpBridgeConfig{pipeName, mcp::McpMode::ReadOnly}));
    auto first = QLocalSocket{};
    first.connectToServer(pipeName);
    REQUIRE(first.waitForConnected(1000));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);

    auto second = QLocalSocket{};
    second.connectToServer(pipeName);
    REQUIRE(second.waitForConnected(1000));
    const auto rejected = readBridgeResponse(second);
    CHECK_FALSE(rejected.ok);
    REQUIRE(rejected.error);
    CHECK(rejected.error->message.contains("connection limit"));

    first.disconnectFromServer();
    if (first.state() != QLocalSocket::UnconnectedState)
    {
      REQUIRE(first.waitForDisconnected(1000));
    }
    processEventsFor(100);
    auto third = QLocalSocket{};
    third.connectToServer(pipeName);
    REQUIRE(third.waitForConnected(1000));
    const auto line = QJsonDocument{mcp::toJson(mcp::McpBridgeRequest{
                                      "3", "tb_status", {}, mcp::McpMode::ReadOnly})}
                        .toJson(QJsonDocument::Compact)
                      + "\n";
    REQUIRE(third.write(line) == line.size());
    CHECK(readBridgeResponse(third).ok);
  }

  SECTION("serves typed resource list and read requests")
  {
    auto server = makeServer(McpBridgeTransportLimits{1024, 10'000, 4});
    const auto pipeName = uniqueBridgePipeName();
    REQUIRE(server.start(
      mcp::McpBridgeConfig{pipeName, mcp::McpMode::ReadOnly, true, "127.0.0.1", 0}));
    auto socket = QLocalSocket{};
    socket.connectToServer(pipeName);
    REQUIRE(socket.waitForConnected(1000));

    const auto writeRequest = [&](const mcp::McpBridgeRequest& request) {
      const auto line =
        QJsonDocument{mcp::toJson(request)}.toJson(QJsonDocument::Compact) + '\n';
      REQUIRE(socket.write(line) == line.size());
      return readBridgeResponse(socket);
    };

    const auto create = writeRequest(mcp::McpBridgeRequest{
      "create",
      "render_review_current_scene",
      {},
      mcp::McpMode::ReadOnly,
    });
    REQUIRE(create.ok);

    const auto listed = writeRequest(mcp::McpBridgeRequest{
      "list",
      {},
      {},
      std::nullopt,
      mcp::McpBridgeRequestType::ResourcesList,
    });
    REQUIRE(listed.ok);
    const auto resources = listed.result.value("resources").toArray();
    REQUIRE(resources.size() == 1);
    CHECK(
      resources.first().toObject().value("uri").toString()
      == "tbmcp://review/transport-review");

    const auto read = writeRequest(mcp::McpBridgeRequest{
      "read",
      {},
      QJsonObject{{"uri", "tbmcp://review/transport-review"}},
      std::nullopt,
      mcp::McpBridgeRequestType::ResourceRead,
    });
    REQUIRE(read.ok);
    CHECK(read.result.value("reviewId").toString() == "transport-review");
  }
}

TEST_CASE("McpBridgeServer", "[McpBridgeServer]")
{
  SECTION("uses bounded local transport defaults")
  {
    const auto limits = McpBridgeTransportLimits{};
    CHECK(limits.maxRequestBytes == 4 * 1024 * 1024);
    CHECK(limits.incompleteRequestTimeoutMs == 10'000);
    CHECK(limits.maxConnections == 32);
  }

  auto server = McpBridgeServer{[](const QString& toolName, const QJsonObject& params) {
    if (toolName == "tb_status")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"application", "TrenchBroom"},
        {"mode", "ReadOnly"},
      });
    }
    if (toolName == "documents_list")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"count", 0},
      });
    }
    if (toolName == "documents_activate")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"activated", true},
      });
    }
    if (toolName == "documents_open")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"opened", true},
      });
    }
    if (toolName == "documents_save")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"saved", true},
      });
    }
    if (toolName == "documents_close")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"closed", true},
      });
    }
    if (toolName == "documents_export")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"exported", true},
      });
    }
    if (toolName == "map_search")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"count", 1},
        {"query", "worldspawn"},
      });
    }
    if (toolName == "selection_set")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"selectedCount", 0},
      });
    }
    if (toolName == "selection_inspect")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"selectedCount", 0},
      });
    }
    if (toolName == "selection_filter")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"count", 0},
      });
    }
    if (toolName == "selection_by_bounds")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"count", 0},
      });
    }
    if (toolName == "selection_grow")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"selectedCount", 0},
      });
    }
    if (toolName == "viewport_focus")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"focused", true},
        {"cameraControlled", true},
        {"focusedObjectCount", 1},
      });
    }
    if (toolName == "viewport_clear_marks")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"active", false},
      });
    }
    if (toolName == "viewport_layout_get")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"layout", "onePane"},
        {"hasVisible2D", false},
        {"hasVisible3D", true},
      });
    }
    if (toolName == "viewport_layout_set")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"layout", "twoPanes"},
        {"previousLayout", "onePane"},
        {"changed", true},
        {"hasVisible2D", true},
        {"hasVisible3D", true},
      });
    }
    if (toolName == "viewport_camera_frame_bounds")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"cameraControlled", true},
        {"mode", "orbitBounds"},
        {"azimuth", params.value("azimuth").toDouble(-45.0)},
        {"elevation", params.value("elevation").toDouble(32.0)},
      });
    }
    if (toolName == "viewport_camera_set")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"cameraControlled", true},
        {"mode", "lookAt"},
      });
    }
    if (
      toolName == "viewport_capture_current" || toolName == "viewport_capture_3d"
      || toolName == "viewport_capture_2d")
    {
      auto scope = QString{"window"};
      if (toolName == "viewport_capture_3d")
      {
        scope = "3d";
      }
      else if (toolName == "viewport_capture_2d")
      {
        scope = "2d";
      }
      return McpBridgeToolResult::success(QJsonObject{
        {"format", "png"},
        {"path", "C:/tmp/viewport.png"},
        {"width", 640},
        {"height", 480},
        {"scope", scope},
      });
    }
    if (toolName == "viewport_capture_scene_review")
    {
      const auto cameraControlled = params.value("objectIds").isArray();
      const auto views = params.value("views").isArray()
                           ? params.value("views").toArray()
                           : QJsonArray{"current", "3d", "2d"};
      auto captures = QJsonArray{};
      auto quality = QJsonArray{};
      for (const auto& viewValue : views)
      {
        const auto view = viewValue.toString();
        captures.push_back(QJsonObject{
          {"view", view},
          {"path", QString{"C:/tmp/%1.png"}.arg(view)},
          {"width", 640},
          {"height", 480},
        });
        quality.push_back(QJsonObject{
          {"view", view},
          {"valid", true},
          {"warnings", QJsonArray{}},
          {"width", 640},
          {"height", 480},
          {"fileSize", 4096},
        });
      }
      const auto hasTarget = params.value("objectIds").isArray();
      auto warnings = QJsonArray{};
      if (
        params.value("isolate").toBool(false)
        && params.value("isolateMode").toString() == "hide_others")
      {
        warnings.push_back(
          "isolationModeFallback: hide_others requested; this build writes a focused "
          "highlight_only review bundle. True hidden-others isolation needs a dedicated "
          "renderer target filter.");
      }
      return McpBridgeToolResult::success(QJsonObject{
        {"sceneName", "whitebox review smoke"},
        {"captureCount", captures.size()},
        {"captures", captures},
        {"quality", quality},
        {"qualityValid", true},
        {"checklist", QJsonArray{"silhouette", "connectivity"}},
        {"warnings", warnings},
        {"cameraControlled", cameraControlled},
        {"focusedObjectCount", cameraControlled ? 1 : 0},
        {"targetObjectCount", hasTarget ? 1 : 0},
        {"appliedIsolateMode",
         params.value("isolate").toBool(false) ? "highlight_only" : ""},
      });
    }
    if (toolName == "render_review_operation")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"tool", "render_review_operation"},
        {"renderer", "geometry_cpu"},
        {"reviewId", "review-test"},
        {"targetObjectCount", 1},
        {"captureCount", 3},
        {"captures", QJsonArray{}},
        {"quality", QJsonArray{}},
        {"qualityValid", true},
        {"outputDir", params.value("outputDir").toString()},
      });
    }
    if (toolName == "render_review_current_scene")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"tool", "render_review_current_scene"},
        {"renderer", "geometry_cpu"},
        {"reviewId", "review-session-test"},
        {"resourceUri", "tbmcp://review/review-session-test"},
        {"targetObjectCount", 2},
        {"captureCount", 1},
        {"absolutePreferredCapturePath", "C:/tmp/review-session-test.png"},
      });
    }
    if (toolName == "render_review_targets")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"tool", "render_review_targets"},
        {"renderer", "geometry_cpu"},
        {"reviewId", "review-targets-test"},
        {"targetObjectCount", 1},
        {"targetBrushCount", 1},
        {"captureCount", 5},
        {"captures", QJsonArray{}},
        {"quality", QJsonArray{}},
        {"qualityValid", true},
        {"outputDir", params.value("outputDir").toString()},
      });
    }
    if (toolName == "overlay_set")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"active", true},
      });
    }
    if (toolName == "entity_create")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"operationId", "mcp-op-1"},
        {"transactionName", "MCP: Create info_player_start"},
      });
    }
    if (toolName == "fgd_entities_list")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"count", 1},
      });
    }
    if (toolName == "entity_schema")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"classname", "func_wall"},
      });
    }
    if (
      toolName == "entity_create_from_schema" || toolName == "entity_create_checked"
      || toolName == "entity_create_checked_batch")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"operationId", "mcp-op-4"},
        {"transactionName", "MCP: Create light"},
      });
    }
    if (toolName == "entity_tie_brushes")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"operationId", "mcp-op-5"},
        {"transactionName", "MCP: Tie brushes to func_wall"},
      });
    }
    if (toolName == "entity_untie_brushes")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"operationId", "mcp-op-6"},
        {"transactionName", "MCP: Untie brushes"},
      });
    }
    if (toolName == "brush_types_list")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"types", QJsonArray{}},
      });
    }
    if (toolName == "shape_library_list")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"shapes", QJsonArray{QJsonObject{{"name", "diamond"}}}},
      });
    }
    if (
      toolName == "brush_create" || toolName == "brush_create_cone"
      || toolName == "brush_create_pipe" || toolName == "brush_create_sphere"
      || toolName == "brush_create_pyramid" || toolName == "brush_create_tetrahedron"
      || toolName == "brush_create_from_planes" || toolName == "brush_create_prism"
      || toolName == "brush_create_cylinder_sector"
      || toolName == "brush_create_boxes_batch"
      || toolName == "brush_create_polygon_batch")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"operationId", "mcp-op-7"},
        {"transactionName", "MCP: Create brush primitive"},
      });
    }
    if (toolName == "brush_metadata_set")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"count", 1},
      });
    }
    if (toolName == "brush_metadata_get")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"count", 1},
      });
    }
    if (toolName == "selection_by_metadata")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"count", 1},
      });
    }
    if (
      toolName == "route_geometry_analyze_chain"
      || toolName == "kz_distance_analyze_chain")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"segmentCount", 1},
      });
    }
    if (toolName == "history_list")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"count", 0},
      });
    }
    if (toolName == "history_status")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"historyCount", 0},
        {"canUndoLatestMcpOperation", false},
        {"reasonIfUnavailable", "noMcpMutationYet"},
      });
    }
    if (toolName == "asset_search")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"count", 0},
      });
    }
    if (toolName == "textures_list" || toolName == "texture_search")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"count", 0},
        {"materials", QJsonArray{}},
        {"materialNames", QJsonArray{}},
      });
    }
    if (toolName == "texture_lock_get")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"textureLock", true},
        {"uvLock", false},
      });
    }
    if (toolName == "texture_lock_set")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"textureLock", true},
        {"uvLock", true},
        {"changed", true},
      });
    }
    if (toolName == "face_list")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"count", 0},
      });
    }
    if (toolName == "face_select")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"selectedCount", 1},
      });
    }
    if (
      toolName == "texture_apply" || toolName == "texture_apply_by_filter"
      || toolName == "texture_replace" || toolName == "texture_align_face"
      || toolName == "texture_copy_from_face" || toolName == "face_texture_set")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"operationId", "mcp-op-8"},
        {"transactionName", "MCP: Texture edit"},
      });
    }
    if (
      toolName == "objects_delete" || toolName == "objects_delete_by_filter"
      || toolName == "objects_transform")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"operationId", "mcp-op-9"},
        {"transactionName", "MCP: Object edit"},
      });
    }
    if (toolName == "map_validate" || toolName == "problems_check")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"valid", true},
        {"count", 0},
      });
    }
    if (toolName == "problems_fix" || toolName == "map_fix_all_safe")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"operationId", "mcp-op-10"},
        {"transactionName", "MCP: Fix problems"},
      });
    }
    if (toolName == "compile_profiles_list")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"count", 1},
      });
    }
    if (toolName == "compile_log_tail")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"lineCount", 0},
        {"running", false},
      });
    }
    if (toolName == "compile_run")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"profile", "default"},
        {"started", true},
      });
    }
    if (toolName == "leaks_load_pointfile")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"loaded", true},
        {"pointCount", 2},
      });
    }
    if (toolName == "asset_place_model")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"operationId", "mcp-op-2"},
        {"transactionName", "MCP: Place model asset"},
      });
    }
    if (toolName == "blockout_validate")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"valid", true},
      });
    }
    if (toolName == "geometry_analyze_selection")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"brushCount", 0},
        {"invalidBrushCount", 0},
      });
    }
    if (toolName == "geometry_analyze_slopes")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"targetBrushCount", 0},
        {"slopeCount", 0},
        {"slopes", QJsonArray{}},
      });
    }
    if (toolName == "geometry_analyze_route_continuity")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"targetBrushCount", 0},
        {"surfaceCount", 0},
        {"seamCount", 0},
        {"continuous", true},
      });
    }
    if (toolName == "geometry_analyze_shell_seams")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"targetBrushCount", 0},
        {"annotatedBrushCount", 0},
        {"seamCount", 0},
        {"shellContinuous", true},
      });
    }
    if (toolName == "blockout_validate_spiral_stairs")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"valid", true},
        {"gapCount", 0},
      });
    }
    if (
      toolName == "operation_inspect" || toolName == "operation_select"
      || toolName == "operation_validate")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"operationId", "mcp-op-3"},
      });
    }
    if (
      toolName == "blockout_create_batch"
      || toolName == "blockout_create_curved_corridor")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"operationId", "mcp-op-11"},
        {"transactionName",
         toolName == "blockout_create_batch" ? "MCP: Blockout batch"
                                             : "MCP: Blockout curved corridor"},
        {"brushCount", 4},
        {"resourceUri", "tbmcp://operation/mcp-op-11"},
      });
    }
    if (toolName == "heightmap_import_grayscale")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"operationId", "mcp-op-12"},
        {"transactionName", "MCP: Import grayscale heightmap"},
        {"brushCount", 4},
        {"resourceUri", "tbmcp://operation/mcp-op-12"},
      });
    }
    if (toolName == "heightmap_preview_grayscale")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"willCommit", true},
        {"estimatedBrushCount", 4},
        {"warnings", QJsonArray{}},
      });
    }
    if (toolName == "blockout_create_spiral_stairs")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"operationId", "mcp-op-3"},
        {"transactionName", "MCP: Blockout spiral stairs"},
      });
    }
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::ToolNotFound, QString{"Unknown test tool"});
  }};

  SECTION("off mode does not listen")
  {
    auto error = QString{};
    CHECK(server.start(mcp::McpBridgeConfig{"test-pipe", mcp::McpMode::Off}, &error));
    CHECK(error.isEmpty());
    CHECK(!server.isListening());
  }

  SECTION("does not remove an active bridge pipe")
  {
    const auto pipeName = uniqueBridgePipeName();
    auto activeServer = QLocalServer{};
    REQUIRE(activeServer.listen(pipeName));

    auto error = QString{};
    CHECK_FALSE(
      server.start(mcp::McpBridgeConfig{pipeName, mcp::McpMode::ReadOnly}, &error));
    CHECK(error.contains("already listening"));
    CHECK(activeServer.isListening());
  }

  SECTION("requested mode can only reduce configured permissions")
  {
    REQUIRE(server.start(mcp::McpBridgeConfig{"test-pipe", mcp::McpMode::Edit}));

    const auto response = server.dispatchRequest(mcp::McpBridgeRequest{
      "1",
      "entity_create",
      QJsonObject{{"classname", "info_player_start"}},
      mcp::McpMode::ReadOnly});

    CHECK(!response.ok);
    REQUIRE(response.error);
    CHECK(response.error->code == mcp::McpErrorCode::Forbidden);
    CHECK(response.error->message.contains("ReadOnly"));
  }

  SECTION("serves tb_status")
  {
    REQUIRE(server.start(mcp::McpBridgeConfig{"test-pipe", mcp::McpMode::ReadOnly}));

    const auto response = server.dispatchRequest(
      mcp::McpBridgeRequest{"1", "tb_status", {}, mcp::McpMode::ReadOnly});

    CHECK(response.ok);
    CHECK(response.result.value("application").toString() == "TrenchBroom");
  }

  SECTION("caches review resources returned by tools")
  {
    REQUIRE(server.start(mcp::McpBridgeConfig{"test-pipe", mcp::McpMode::Edit}));

    const auto response = server.dispatchRequest(mcp::McpBridgeRequest{
      "1", "render_review_current_scene", QJsonObject{}, mcp::McpMode::ReadOnly});

    REQUIRE(response.ok);
    CHECK(
      response.result.value("resourceUri").toString()
      == "tbmcp://review/review-session-test");
    const auto resource =
      server.readResource(response.result.value("resourceUri").toString());
    REQUIRE(resource);
    CHECK(resource->value("reviewId").toString() == "review-session-test");
    CHECK(resource->value("tool").toString() == "render_review_current_scene");
    CHECK(resource->value("targetObjectCount").toInt() == 2);
  }

  SECTION("evicted review resources return structured recovery guidance")
  {
    auto resourceServer = McpBridgeServer{[](const QString&, const QJsonObject& params) {
      const auto index = params.value("index").toInt();
      return McpBridgeToolResult::success(QJsonObject{
        {"tool", "render_review_current_scene"},
        {"reviewId", QString{"review-%1"}.arg(index, 3, 10, QLatin1Char{'0'})},
        {"resourceUri",
         QString{"tbmcp://review/review-%1"}.arg(index, 3, 10, QLatin1Char{'0'})},
      });
    }};
    REQUIRE(resourceServer.start(mcp::McpBridgeConfig{"test-pipe", mcp::McpMode::Edit}));

    for (auto i = 0; i <= static_cast<int>(McpSessionState::MaxReviewResources); ++i)
    {
      const auto response = resourceServer.dispatchRequest(mcp::McpBridgeRequest{
        QString::number(i),
        "render_review_current_scene",
        QJsonObject{{"index", i}},
        mcp::McpMode::ReadOnly});
      REQUIRE(response.ok);
    }

    const auto evicted = resourceServer.readResource("tbmcp://review/review-000");
    REQUIRE(evicted);
    CHECK(evicted->value("evicted").toBool());
    CHECK(evicted->value("reason").toString() == "review_resource_budget");
    CHECK(evicted->value("recoveryAction").toString() == "regenerate_review");

    const auto retained = resourceServer.readResource("tbmcp://review/review-128");
    REQUIRE(retained);
    CHECK_FALSE(retained->value("evicted").toBool());
    CHECK(retained->value("reviewId").toString() == "review-128");
  }

  SECTION("serves compact tb_doctor output")
  {
    auto appControllerFixture = AppControllerFixture{};
    auto& appController = appControllerFixture.appController();
    auto appServer = McpBridgeServer{appController};
    REQUIRE(appServer.start(mcp::McpBridgeConfig{"test-pipe", mcp::McpMode::Edit}));

    const auto summaryResponse = appServer.dispatchRequest(
      mcp::McpBridgeRequest{"1", "tb_doctor", {}, mcp::McpMode::ReadOnly});
    REQUIRE(summaryResponse.ok);
    CHECK(summaryResponse.result.value("detail").toString() == "summary");
    CHECK_FALSE(summaryResponse.result.contains("implementedTools"));
    CHECK_FALSE(summaryResponse.result.contains("toolDiagnostics"));
    CHECK(summaryResponse.result.value("implementedToolCount").toInt() > 0);
    CHECK(summaryResponse.result.value("expertToolCount").isDouble());
    CHECK(summaryResponse.result.value("toolCategoryCounts").isObject());
    CHECK(summaryResponse.result.value("toolLifecycleCounts").isObject());
    CHECK(summaryResponse.result.value("schemaLookupHint")
            .toString()
            .contains("tb_tools_search"));
    CHECK(summaryResponse.result.value("associatedSkills")
            .toArray()
            .contains("trenchbroom-mcp-scene-workflow"));
    CHECK(summaryResponse.result.value("skillWorkflowHint")
            .toString()
            .contains("trenchbroom-mcp-scene-workflow"));
    CHECK(summaryResponse.result.value("recipeWorkflowHint")
            .toString()
            .contains("ir_compile_preview_from_file"));
    CHECK(summaryResponse.result.contains("overlay"));
    const auto sessionState = summaryResponse.result.value("sessionState").toObject();
    CHECK(
      sessionState.value("limits").toObject().value("operationRecords").toInt()
      == static_cast<int>(McpSessionState::MaxOperationRecords));
    CHECK(sessionState.value("counts").isObject());
    CHECK(sessionState.value("evictions").isObject());
    CHECK(
      QJsonDocument{summaryResponse.result}.toJson(QJsonDocument::Compact).size() < 4096);

    const auto fullResponse = appServer.dispatchRequest(mcp::McpBridgeRequest{
      "2", "tb_doctor", QJsonObject{{"detail", "full"}}, mcp::McpMode::ReadOnly});
    REQUIRE(fullResponse.ok);
    CHECK(fullResponse.result.value("detail").toString() == "full");
    CHECK(fullResponse.result.value("toolDiagnostics").isArray());
    const auto implementedTools = fullResponse.result.value("implementedTools").toArray();
    REQUIRE(!implementedTools.isEmpty());
    const auto firstTool = implementedTools.first().toObject();
    CHECK(firstTool.value("name").isString());
    CHECK(firstTool.value("category").isString());
    CHECK(firstTool.value("lifecycle").isString());
    CHECK(firstTool.value("requiredMode").isString());
    CHECK(firstTool.value("visibleInCurrentProfile").isBool());
    CHECK_FALSE(firstTool.contains("inputSchema"));
    CHECK(fullResponse.result.value("associatedSkills")
            .toArray()
            .contains("trenchbroom-mcp-scene-workflow"));
    CHECK(fullResponse.result.value("recipeWorkflowHint")
            .toString()
            .contains("ir_apply_from_file"));
  }

  SECTION("serves compact broad tool schema search")
  {
    auto appControllerFixture = AppControllerFixture{};
    auto& appController = appControllerFixture.appController();
    auto appServer = McpBridgeServer{appController};
    REQUIRE(appServer.start(mcp::McpBridgeConfig{"test-pipe", mcp::McpMode::Edit}));

    const auto broadResponse = appServer.dispatchRequest(mcp::McpBridgeRequest{
      "1",
      "tb_tools_search",
      QJsonObject{{"query", "selector"}, {"detail", "schema"}},
      mcp::McpMode::ReadOnly});
    REQUIRE(broadResponse.ok);
    const auto broadTools = broadResponse.result.value("tools").toArray();
    REQUIRE(broadTools.size() > 1);
    for (const auto& tool : broadTools)
    {
      const auto object = tool.toObject();
      CHECK_FALSE(object.contains("inputSchema"));
      CHECK(object.value("schemaAvailable").toBool());
      CHECK(object.value("schemaOmittedReason").toString() == "multiple_matches");
      CHECK(object.value("schemaQuery").toString() == object.value("name").toString());
    }

    const auto exactResponse = appServer.dispatchRequest(mcp::McpBridgeRequest{
      "2",
      "tb_tools_search",
      QJsonObject{{"query", "blockout_create_batch"}, {"detail", "schema"}},
      mcp::McpMode::ReadOnly});
    REQUIRE(exactResponse.ok);
    const auto exactTools = exactResponse.result.value("tools").toArray();
    REQUIRE(exactTools.size() == 1);
    CHECK(exactTools.first().toObject().value("inputSchema").isObject());
  }

  SECTION("status includes bridge and process identity")
  {
    auto appControllerFixture = AppControllerFixture{};
    auto& appController = appControllerFixture.appController();
    auto config = mcp::McpBridgeConfig{"test-pipe", mcp::McpMode::Edit};
    config.httpPort = 45678;

    const auto status =
      makeStatus(appController, config, "bridge-test-id", "2026-06-28T00:00:00Z");

    CHECK(status.value("application").toString() == "TrenchBroom");
    CHECK(status.value("processId").toInt() > 0);
    CHECK(status.value("bridgeInstanceId").toString() == "bridge-test-id");
    CHECK(status.value("bridgeStartedAt").toString() == "2026-06-28T00:00:00Z");
    CHECK(status.value("httpPort").toInt() == 45678);
    CHECK(status.contains("openDocumentCount"));
    CHECK(status.contains("openDocumentsSummary"));
    CHECK(status.value("associatedSkills")
            .toArray()
            .contains("trenchbroom-mcp-scene-workflow"));
    CHECK(status.value("skillWorkflowHint")
            .toString()
            .contains("trenchbroom-mcp-scene-workflow"));
    CHECK(status.value("recipeWorkflowHint").toString().contains("skill recipes"));
  }

  SECTION("serves wired read-only tools")
  {
    REQUIRE(server.start(mcp::McpBridgeConfig{"test-pipe", mcp::McpMode::ReadOnly}));

    const auto response = server.dispatchRequest(
      mcp::McpBridgeRequest{"1", "documents_list", {}, mcp::McpMode::ReadOnly});

    CHECK(response.ok);
    CHECK(response.result.value("count").toInt() == 0);
  }

  SECTION("serves read-only document and selection parity tools")
  {
    REQUIRE(server.start(mcp::McpBridgeConfig{"test-pipe", mcp::McpMode::ReadOnly}));

    const auto activateResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "1", "documents_activate", QJsonObject{{"index", 0}}, mcp::McpMode::ReadOnly});
    CHECK(activateResponse.ok);
    CHECK(activateResponse.result.value("activated").toBool());

    const auto boundsResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "3",
      "selection_by_bounds",
      QJsonObject{
        {"min", QJsonArray{0, 0, 0}},
        {"max", QJsonArray{128, 128, 128}},
      },
      mcp::McpMode::ReadOnly});
    CHECK(boundsResponse.ok);

    const auto growResponse = server.dispatchRequest(
      mcp::McpBridgeRequest{"4", "selection_grow", {}, mcp::McpMode::ReadOnly});
    CHECK(growResponse.ok);

    const auto geometryReviewResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "10e",
      "render_review_targets",
      QJsonObject{
        {"objectIds", QJsonArray{"node:0/0"}},
        {"views", QJsonArray{"iso_overview_ne", "top_plan"}},
      },
      mcp::McpMode::ReadOnly});
    CHECK(geometryReviewResponse.ok);
    CHECK(
      geometryReviewResponse.result.value("tool").toString() == "render_review_targets");
    CHECK(geometryReviewResponse.result.value("renderer").toString() == "geometry_cpu");
    CHECK(geometryReviewResponse.result.value("captureCount").toInt() == 5);
  }

  SECTION("serves map_search")
  {
    REQUIRE(server.start(mcp::McpBridgeConfig{"test-pipe", mcp::McpMode::ReadOnly}));

    const auto response = server.dispatchRequest(mcp::McpBridgeRequest{
      "1", "map_search", QJsonObject{{"query", "worldspawn"}}, mcp::McpMode::ReadOnly});

    CHECK(response.ok);
    CHECK(response.result.value("query").toString() == "worldspawn");
    CHECK(response.result.value("count").toInt() == 1);
  }

  SECTION("serves selection_set")
  {
    REQUIRE(server.start(mcp::McpBridgeConfig{"test-pipe", mcp::McpMode::ReadOnly}));

    const auto response = server.dispatchRequest(
      mcp::McpBridgeRequest{"1", "selection_set", {}, mcp::McpMode::ReadOnly});

    CHECK(response.ok);
    CHECK(response.result.value("selectedCount").toInt() == 0);
  }

  SECTION("serves selection_inspect")
  {
    REQUIRE(server.start(mcp::McpBridgeConfig{"test-pipe", mcp::McpMode::ReadOnly}));

    const auto response = server.dispatchRequest(mcp::McpBridgeRequest{
      "1",
      "selection_inspect",
      QJsonObject{{"detail", "summary"}},
      mcp::McpMode::ReadOnly});

    CHECK(response.ok);
    CHECK(response.result.value("selectedCount").toInt() == 0);
  }

  SECTION("serves overlay_set")
  {
    REQUIRE(server.start(mcp::McpBridgeConfig{"test-pipe", mcp::McpMode::ReadOnly}));

    const auto response = server.dispatchRequest(mcp::McpBridgeRequest{
      "1",
      "overlay_set",
      QJsonObject{{"highlightObjectIds", QJsonArray{}}},
      mcp::McpMode::ReadOnly});

    CHECK(response.ok);
    CHECK(response.result.value("active").toBool());
  }

  SECTION("mode gating rejects edit tools")
  {
    REQUIRE(server.start(mcp::McpBridgeConfig{"test-pipe", mcp::McpMode::ReadOnly}));

    const auto response = server.dispatchRequest(mcp::McpBridgeRequest{
      "1",
      "texture_apply_by_filter",
      QJsonObject{{"material", "test"}},
      mcp::McpMode::Edit});

    CHECK(!response.ok);
    REQUIRE(response.error);
    CHECK(response.error->code == mcp::McpErrorCode::Forbidden);

    const auto pythonResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "2",
      "python_generate_blockout",
      QJsonObject{{"script", "print('{}')"}},
      mcp::McpMode::Edit});
    CHECK(!pythonResponse.ok);
    REQUIRE(pythonResponse.error);
    CHECK(pythonResponse.error->code == mcp::McpErrorCode::Forbidden);

    const auto textureResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "3",
      "texture_apply_by_filter",
      QJsonObject{{"material", "test"}},
      mcp::McpMode::Edit});
    CHECK(!textureResponse.ok);
    REQUIRE(textureResponse.error);
    CHECK(textureResponse.error->code == mcp::McpErrorCode::Forbidden);
  }

  SECTION("registered unsupported tools report not implemented")
  {
    REQUIRE(server.start(mcp::McpBridgeConfig{"test-pipe", mcp::McpMode::Edit}));

    const auto response = server.dispatchRequest(
      mcp::McpBridgeRequest{"1", "brush_create_arch", {}, mcp::McpMode::Edit});

    CHECK(!response.ok);
    REQUIRE(response.error);
    CHECK(response.error->code == mcp::McpErrorCode::ToolNotFound);
    CHECK(response.error->message.contains("not implemented"));
  }

  SECTION("read-only mode rejects document mutation tools")
  {
    REQUIRE(server.start(mcp::McpBridgeConfig{"test-pipe", mcp::McpMode::ReadOnly}));

    const auto response = server.dispatchRequest(mcp::McpBridgeRequest{
      "1",
      "documents_save",
      QJsonObject{{"path", "C:/tmp/test.map"}},
      mcp::McpMode::Edit});

    CHECK(!response.ok);
    REQUIRE(response.error);
    CHECK(response.error->code == mcp::McpErrorCode::Forbidden);
  }

  SECTION("serves FGD list tools")
  {
    REQUIRE(server.start(mcp::McpBridgeConfig{"test-pipe", mcp::McpMode::ReadOnly}));

    const auto listResponse = server.dispatchRequest(
      mcp::McpBridgeRequest{"1", "fgd_entities_list", {}, mcp::McpMode::ReadOnly});
    CHECK(listResponse.ok);
    CHECK(listResponse.result.value("count").toInt() == 1);
  }

  SECTION("edit mode serves FGD entity mutation tools")
  {
    REQUIRE(server.start(mcp::McpBridgeConfig{"test-pipe", mcp::McpMode::Edit}));

    const auto createResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "1",
      "entity_create_from_schema",
      QJsonObject{{"classname", "light"}},
      mcp::McpMode::Edit});
    CHECK(createResponse.ok);
    CHECK(createResponse.result.value("operationId").toString() == "mcp-op-4");

    const auto checkedBatchResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "2",
      "entity_create_checked_batch",
      QJsonObject{{"entities", QJsonArray{}}},
      mcp::McpMode::Edit});
    CHECK(checkedBatchResponse.ok);
    CHECK(checkedBatchResponse.result.value("operationId").toString() == "mcp-op-4");
  }

  SECTION("edit mode serves document lifecycle tools")
  {
    REQUIRE(server.start(mcp::McpBridgeConfig{"test-pipe", mcp::McpMode::Edit}));

    const auto openResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "1",
      "documents_open",
      QJsonObject{{"path", "C:/tmp/test.map"}},
      mcp::McpMode::Edit});
    CHECK(openResponse.ok);

    const auto saveResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "2",
      "documents_save",
      QJsonObject{{"path", "C:/tmp/test.map"}},
      mcp::McpMode::Edit});
    CHECK(saveResponse.ok);
  }

  SECTION("read-only mode serves history_status")
  {
    REQUIRE(server.start(mcp::McpBridgeConfig{"test-pipe", mcp::McpMode::ReadOnly}));

    const auto statusResponse = server.dispatchRequest(
      mcp::McpBridgeRequest{"2", "history_status", {}, mcp::McpMode::ReadOnly});
    CHECK(statusResponse.ok);
    CHECK(statusResponse.result.value("historyCount").toInt() == 0);
    CHECK(
      statusResponse.result.value("reasonIfUnavailable").toString()
      == "noMcpMutationYet");
  }

  SECTION("edit mode serves asset placement")
  {
    REQUIRE(server.start(mcp::McpBridgeConfig{"test-pipe", mcp::McpMode::Edit}));

    const auto response = server.dispatchRequest(mcp::McpBridgeRequest{
      "1",
      "asset_place_model",
      QJsonObject{{"path", "models/player.mdl"}},
      mcp::McpMode::Edit});

    CHECK(response.ok);
    CHECK(response.result.value("operationId").toString() == "mcp-op-2");
  }

  SECTION("edit mode serves texture and face mutation tools")
  {
    REQUIRE(server.start(mcp::McpBridgeConfig{"test-pipe", mcp::McpMode::Edit}));

    for (const auto& toolName :
         {"texture_apply",
          "texture_lock_set",
          "texture_replace",
          "texture_apply_by_filter"})
    {
      const auto params = toolName == QString{"texture_lock_set"}
                            ? QJsonObject{{"textureLock", true}, {"uvLock", true}}
                            : QJsonObject{{"material", "test"}};
      const auto response = server.dispatchRequest(
        mcp::McpBridgeRequest{"1", toolName, params, mcp::McpMode::Edit});
      CHECK(response.ok);
      if (toolName == QString{"texture_lock_set"})
      {
        CHECK(response.result.value("textureLock").toBool());
        CHECK(response.result.value("uvLock").toBool());
      }
      else
      {
        CHECK(response.result.value("operationId").toString() == "mcp-op-8");
      }
    }
  }

  SECTION("read-only mode serves validation tools")
  {
    REQUIRE(server.start(mcp::McpBridgeConfig{"test-pipe", mcp::McpMode::ReadOnly}));

    const auto validateResponse = server.dispatchRequest(
      mcp::McpBridgeRequest{"1", "map_validate", {}, mcp::McpMode::ReadOnly});
    CHECK(validateResponse.ok);
    CHECK(validateResponse.result.value("valid").toBool());

    const auto problemsResponse = server.dispatchRequest(
      mcp::McpBridgeRequest{"2", "problems_check", {}, mcp::McpMode::ReadOnly});
    CHECK(problemsResponse.ok);
    CHECK(problemsResponse.result.value("count").toInt() == 0);
  }

  SECTION("read-only mode serves compile profile and log tools")
  {
    REQUIRE(server.start(mcp::McpBridgeConfig{"test-pipe", mcp::McpMode::ReadOnly}));

    const auto profilesResponse = server.dispatchRequest(
      mcp::McpBridgeRequest{"1", "compile_profiles_list", {}, mcp::McpMode::ReadOnly});
    CHECK(profilesResponse.ok);
    CHECK(profilesResponse.result.value("count").toInt() == 1);

    const auto logResponse = server.dispatchRequest(
      mcp::McpBridgeRequest{"2", "compile_log_tail", {}, mcp::McpMode::ReadOnly});
    CHECK(logResponse.ok);
    CHECK(logResponse.result.value("lineCount").toInt() == 0);
  }

  SECTION("read-only mode rejects compile run and pointfile loading")
  {
    REQUIRE(server.start(mcp::McpBridgeConfig{"test-pipe", mcp::McpMode::ReadOnly}));

    const auto compileResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "1", "compile_run", QJsonObject{{"profile", "default"}}, mcp::McpMode::Edit});
    CHECK(!compileResponse.ok);
    REQUIRE(compileResponse.error);
    CHECK(compileResponse.error->code == mcp::McpErrorCode::Forbidden);

    const auto pointfileResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "2",
      "leaks_load_pointfile",
      QJsonObject{{"path", "C:/tmp/test.pts"}},
      mcp::McpMode::Edit});
    CHECK(!pointfileResponse.ok);
    REQUIRE(pointfileResponse.error);
    CHECK(pointfileResponse.error->code == mcp::McpErrorCode::Forbidden);
  }

  SECTION("edit mode serves compile run and pointfile loading")
  {
    REQUIRE(server.start(mcp::McpBridgeConfig{"test-pipe", mcp::McpMode::Edit}));

    const auto compileResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "1", "compile_run", QJsonObject{{"profile", "default"}}, mcp::McpMode::Edit});
    CHECK(compileResponse.ok);
    CHECK(compileResponse.result.value("started").toBool());

    const auto pointfileResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "2",
      "leaks_load_pointfile",
      QJsonObject{{"path", "C:/tmp/test.pts"}},
      mcp::McpMode::Edit});
    CHECK(pointfileResponse.ok);
    CHECK(pointfileResponse.result.value("loaded").toBool());
  }

  SECTION("read-only mode serves geometry analysis and spiral validation")
  {
    REQUIRE(server.start(mcp::McpBridgeConfig{"test-pipe", mcp::McpMode::ReadOnly}));

    const auto analysisResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "1", "geometry_analyze_selection", {}, mcp::McpMode::ReadOnly});
    CHECK(analysisResponse.ok);
    CHECK(analysisResponse.result.value("invalidBrushCount").toInt() == 0);

    const auto validateResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "2", "blockout_validate_spiral_stairs", QJsonObject{}, mcp::McpMode::ReadOnly});
    CHECK(validateResponse.ok);
    CHECK(validateResponse.result.value("valid").toBool());
  }

  SECTION("read-only mode serves operation detail tools")
  {
    REQUIRE(server.start(mcp::McpBridgeConfig{"test-pipe", mcp::McpMode::ReadOnly}));

    for (const auto& toolName : {"operation_inspect", "operation_validate"})
    {
      const auto response = server.dispatchRequest(mcp::McpBridgeRequest{
        "1", toolName, QJsonObject{{"operationId", "mcp-op-3"}}, mcp::McpMode::ReadOnly});
      CHECK(response.ok);
      CHECK(response.result.value("operationId").toString() == "mcp-op-3");
    }
  }

  SECTION("edit mode serves blockout creation")
  {
    REQUIRE(server.start(mcp::McpBridgeConfig{"test-pipe", mcp::McpMode::Edit}));

    const auto removedRoomParams = QJsonObject{
      {"min", QJsonArray{0, 0, 0}},
      {"max", QJsonArray{128, 128, 128}},
    };
    const auto removedRoomResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "1", "blockout_create_room", removedRoomParams, mcp::McpMode::Edit});

    CHECK_FALSE(removedRoomResponse.ok);
    REQUIRE(removedRoomResponse.error);
    CHECK(removedRoomResponse.error->code == mcp::McpErrorCode::ToolNotFound);
    CHECK(removedRoomResponse.error->message.contains("Unknown MCP tool"));

    const auto spiralResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "2",
      "blockout_create_spiral_stairs",
      QJsonObject{{"steps", 24}},
      mcp::McpMode::Edit});

    CHECK(spiralResponse.ok);
    CHECK(
      spiralResponse.result.value("transactionName").toString()
      == "MCP: Blockout spiral stairs");

    const auto batchResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "3",
      "blockout_create_batch",
      QJsonObject{{"operations", QJsonArray{QJsonObject{{"type", "box"}}}}},
      mcp::McpMode::Edit});
    CHECK(batchResponse.ok);
    CHECK(batchResponse.result.value("operationId").toString() == "mcp-op-11");
  }

  SECTION("rejects nested dispatch")
  {
    auto* nestedServer = static_cast<McpBridgeServer*>(nullptr);
    auto nestedResponse = std::optional<mcp::McpBridgeResponse>{};
    auto guardedServer = McpBridgeServer{[&](
                                           const QString& toolName, const QJsonObject&) {
      if (toolName == "tb_status")
      {
        nestedResponse = nestedServer->dispatchRequest(
          mcp::McpBridgeRequest{"nested", "tb_doctor", {}, mcp::McpMode::ReadOnly});
        return McpBridgeToolResult::success(QJsonObject{{"application", "TrenchBroom"}});
      }
      return McpBridgeToolResult::success(QJsonObject{{"ok", true}});
    }};
    nestedServer = &guardedServer;
    REQUIRE(guardedServer.start(
      mcp::McpBridgeConfig{"test-pipe-nested", mcp::McpMode::ReadOnly}));

    const auto response = guardedServer.dispatchRequest(
      mcp::McpBridgeRequest{"outer", "tb_status", {}, mcp::McpMode::ReadOnly});

    REQUIRE(response.ok);
    REQUIRE(nestedResponse);
    CHECK(!nestedResponse->ok);
    REQUIRE(nestedResponse->error);
    CHECK(nestedResponse->error->code == mcp::McpErrorCode::Forbidden);
  }

  SECTION("mutating tools reject mismatched expectedDocumentPath before dispatch")
  {
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
    const auto mapPath = getFixtureRoot() / "test/ui/MapDocument/emptyValveMap.map";
    REQUIRE(document->load(
      appController.environmentConfig(),
      mdl::QuakeGameInfo,
      mdl::MapFormat::Unknown,
      vm::bbox3d{8192.0},
      mapPath));
    auto& map = document->map();
    const auto expectedPath = QString::fromStdString(map.path().string());
    auto handlerCalled = false;
    auto guardedServer = McpBridgeServer{
      [&](const QString&, const QJsonObject&) {
        handlerCalled = true;
        return McpBridgeToolResult::success(QJsonObject{{"unexpected", true}});
      },
      [&map]() -> mdl::Map* { return &map; }};
    REQUIRE(guardedServer.start(
      mcp::McpBridgeConfig{"test-pipe-expected-doc", mcp::McpMode::Edit}));

    const auto guardedBatchParams = QJsonObject{
      {"expectedDocumentPath", "D:/does/not/match.map"},
      {"operations", QJsonArray{QJsonObject{{"type", "box"}}}},
    };
    const auto response = guardedServer.dispatchRequest(mcp::McpBridgeRequest{
      "1", "blockout_create_batch", guardedBatchParams, mcp::McpMode::Edit});

    CHECK(!response.ok);
    REQUIRE(response.error);
    CHECK(response.error->code == mcp::McpErrorCode::Forbidden);
    CHECK(response.error->message.contains("expectedDocumentPath"));
    CHECK(
      response.error->details.value("expectedDocumentPath").toString()
      == "D:/does/not/match.map");
    CHECK(response.error->details.contains("actualDocumentPath"));
    CHECK(response.error->details.contains("processId"));
    CHECK(response.error->details.contains("bridgeInstanceId"));
    CHECK(response.error->details.contains("httpPort"));
    CHECK(!handlerCalled);

    const auto fingerprint = McpObjectRegistry{}.documentFingerprint(map);
    const auto fingerprintResponse = guardedServer.dispatchRequest(mcp::McpBridgeRequest{
      "2",
      "blockout_create_batch",
      QJsonObject{
        {"expectedDocumentPath", expectedPath},
        {"expectedDocumentFingerprint", "doc:not-the-active-document"},
        {"operations", QJsonArray{QJsonObject{{"type", "box"}}}},
      },
      mcp::McpMode::Edit});

    CHECK(!fingerprintResponse.ok);
    REQUIRE(fingerprintResponse.error);
    CHECK(fingerprintResponse.error->message.contains("expectedDocumentFingerprint"));
    CHECK(
      fingerprintResponse.error->details.value("actualDocumentFingerprint").toString()
      == fingerprint);
    CHECK_FALSE(fingerprintResponse.error->details.value("mutatedDocument").toBool(true));
    CHECK(fingerprintResponse.error->details.value("retrySafe").toBool(false));
    CHECK(!handlerCalled);

    const auto matchingResponse = guardedServer.dispatchRequest(mcp::McpBridgeRequest{
      "3",
      "blockout_create_batch",
      QJsonObject{
        {"expectedDocumentPath", expectedPath},
        {"expectedDocumentFingerprint", fingerprint},
        {"operations", QJsonArray{QJsonObject{{"type", "box"}}}},
      },
      mcp::McpMode::Edit});
    CHECK(matchingResponse.ok);
    CHECK(handlerCalled);
  }

  SECTION("status and history report the same active document fingerprint")
  {
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
    auto registry = McpObjectRegistry{};

    const auto bridgeFingerprint = documentFingerprintForMap(document->map(), &registry);
    CHECK(bridgeFingerprint.startsWith("doc:"));
    CHECK(registry.documentFingerprint(document->map()) == bridgeFingerprint);
    CHECK(
      registry.documentEpoch(document->map())
      == documentEpochForMap(document->map(), &registry));
  }

  SECTION(
    "history_status treats selection-only commands above latest MCP operation as "
    "skippable")
  {
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
    auto& map = document->map();
    auto history = std::vector<McpOperationRecord>{};
    auto nextOperationIndex = 1;
    const auto createResponse = blockoutCreateBatchForMapResult(
      map,
      "blockout_create_batch",
      QJsonObject{
        {"name", "MCP: Test operation"},
        {"operations",
         QJsonArray{QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{0, 0, 0}},
           {"max", QJsonArray{64, 64, 64}},
         }}},
      },
      history,
      nextOperationIndex);
    REQUIRE(createResponse.ok);
    REQUIRE(map.undoCommandName() != nullptr);
    CHECK(QString::fromStdString(*map.undoCommandName()) == "MCP: Test operation");

    mdl::deselectAll(map);
    REQUIRE(map.undoCommandName() != nullptr);
    CHECK(QString::fromStdString(*map.undoCommandName()) == "Select None");

    auto registry = McpObjectRegistry{};
    const auto response = historyStatusForMapResult(
      map, history, registry, "bridge-test-id", "2026-06-28T00:00:00Z", {});

    REQUIRE(response.ok);
    CHECK(response.result.value("canUndoLatestMcpOperation").toBool());
    CHECK(response.result.value("selectionCommandsAboveLatestMcpOperation").toInt() == 1);
    CHECK(response.result.value("reasonIfUnavailable").isNull());
  }

  SECTION("history_undo_to_operation undoes back to the requested operation")
  {
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
    auto& map = document->map();
    auto history = std::vector<McpOperationRecord>{};
    auto nextOperationIndex = 1;
    auto metadataStore = std::map<QString, McpBrushMetadataRecord>{};

    const auto first = blockoutCreateBatchForMapResult(
      map,
      "blockout_create_batch",
      QJsonObject{
        {"name", "MCP: Undo target first"},
        {"operations",
         QJsonArray{QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{0, 0, 0}},
           {"max", QJsonArray{64, 64, 16}},
           {"metadata", QJsonObject{{"routeId", "undo-to-selection-skip"}}},
         }}},
      },
      history,
      nextOperationIndex,
      &metadataStore);
    REQUIRE(first.ok);
    const auto second = blockoutCreateBatchForMapResult(
      map,
      "blockout_create_batch",
      QJsonObject{
        {"name", "MCP: Undo target second"},
        {"operations",
         QJsonArray{QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{128, 0, 0}},
           {"max", QJsonArray{192, 64, 16}},
           {"metadata", QJsonObject{{"routeId", "undo-to-selection-skip"}}},
         }}},
      },
      history,
      nextOperationIndex,
      &metadataStore);
    REQUIRE(second.ok);

    const auto selectResponse = selectionByMetadataForMapResult(
      map,
      QJsonObject{
        {"routeId", "undo-to-selection-skip"},
        {"select", true},
      },
      metadataStore);
    REQUIRE(selectResponse.ok);
    CHECK(map.selection().nodes.size() == 2u);
    REQUIRE(map.undoCommandName() != nullptr);
    CHECK(QString::fromStdString(*map.undoCommandName()) == "Select 2 Objects");

    const auto undo = historyUndoToOperationForMapResult(
      map,
      history,
      QJsonObject{{"operationId", first.result.value("operationId").toString()}});

    REQUIRE(undo.ok);
    CHECK(undo.result.value("mutatedDocument").toBool());
    CHECK(undo.result.value("undone").toBool());
    CHECK(undo.result.value("undoneCount").toInt() == 2);
    CHECK(undo.result.value("skippedSelectionCommandCount").toInt() == 2);
    const auto undoneIds = undo.result.value("undoneOperationIds").toArray();
    REQUIRE(undoneIds.size() == 2);
    CHECK(undoneIds[0].toString() == second.result.value("operationId").toString());
    CHECK(undoneIds[1].toString() == first.result.value("operationId").toString());
    CHECK(history[0].undone);
    CHECK(history[1].undone);
  }

  SECTION("history_undo_to_operation reports blocked native undo stack")
  {
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
    auto& map = document->map();
    auto history = std::vector<McpOperationRecord>{};
    auto nextOperationIndex = 1;

    const auto create = blockoutCreateBatchForMapResult(
      map,
      "blockout_create_batch",
      QJsonObject{
        {"name", "MCP: Blocked undo target"},
        {"operations",
         QJsonArray{QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{0, 0, 0}},
           {"max", QJsonArray{64, 64, 16}},
         }}},
      },
      history,
      nextOperationIndex);
    REQUIRE(create.ok);

    const auto unrelated = blockoutCreateBatchForMapResult(
      map,
      "blockout_create_batch",
      QJsonObject{
        {"name", "User non-MCP operation"},
        {"operations",
         QJsonArray{QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{128, 0, 0}},
           {"max", QJsonArray{192, 64, 16}},
         }}},
      },
      history,
      nextOperationIndex);
    REQUIRE(unrelated.ok);
    history.back().undone = true;

    const auto undo = historyUndoToOperationForMapResult(
      map,
      history,
      QJsonObject{{"operationId", create.result.value("operationId").toString()}});

    CHECK(!undo.ok);
    CHECK_FALSE(undo.error.details.value("mutatedDocument").toBool());
    CHECK_FALSE(undo.error.details.value("partiallyUndone").toBool());
    CHECK(
      undo.error.details.value("blockedOperationId").toString()
      == create.result.value("operationId").toString());
    CHECK(
      undo.error.details.value("nativeUndoCommandName").toString()
      == "User non-MCP operation");
    CHECK(
      undo.error.details.value("recoveryAction").toString()
      == "refresh_status_or_validate");
    CHECK_FALSE(history.front().undone);
  }

  SECTION("history_undo_to_operation reports partial mutation when blocked mid-run")
  {
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
    auto& map = document->map();
    auto history = std::vector<McpOperationRecord>{};
    auto nextOperationIndex = 1;

    const auto first = blockoutCreateBatchForMapResult(
      map,
      "blockout_create_batch",
      QJsonObject{
        {"name", "MCP: Partial undo first"},
        {"operations",
         QJsonArray{QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{0, 0, 0}},
           {"max", QJsonArray{64, 64, 16}},
         }}},
      },
      history,
      nextOperationIndex);
    REQUIRE(first.ok);
    const auto unrelated = blockoutCreateBatchForMapResult(
      map,
      "blockout_create_batch",
      QJsonObject{
        {"name", "User middle operation"},
        {"operations",
         QJsonArray{QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{128, 0, 0}},
           {"max", QJsonArray{192, 64, 16}},
         }}},
      },
      history,
      nextOperationIndex);
    REQUIRE(unrelated.ok);
    history.back().undone = true;
    const auto latest = blockoutCreateBatchForMapResult(
      map,
      "blockout_create_batch",
      QJsonObject{
        {"name", "MCP: Partial undo latest"},
        {"operations",
         QJsonArray{QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{256, 0, 0}},
           {"max", QJsonArray{320, 64, 16}},
         }}},
      },
      history,
      nextOperationIndex);
    REQUIRE(latest.ok);

    const auto undo = historyUndoToOperationForMapResult(
      map,
      history,
      QJsonObject{{"operationId", first.result.value("operationId").toString()}});

    CHECK(!undo.ok);
    CHECK(undo.error.details.value("mutatedDocument").toBool());
    CHECK(undo.error.details.value("partiallyUndone").toBool());
    CHECK(
      undo.error.details.value("blockedOperationId").toString()
      == first.result.value("operationId").toString());
    CHECK(
      undo.error.details.value("nativeUndoCommandName").toString()
      == "User middle operation");
    const auto undoneIds = undo.error.details.value("undoneOperationIds").toArray();
    REQUIRE(undoneIds.size() == 1);
    CHECK(undoneIds.first().toString() == latest.result.value("operationId").toString());
    CHECK_FALSE(history.front().undone);
    CHECK(history.back().undone);
  }

  SECTION(
    "history_undo_mcp rejects operation from a different document before native undo")
  {
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
    auto& map = document->map();
    auto history = std::vector<McpOperationRecord>{};
    auto nextOperationIndex = 1;

    const auto create = blockoutCreateBatchForMapResult(
      map,
      "blockout_create_batch",
      QJsonObject{
        {"name", "MCP: Same transaction name"},
        {"operations",
         QJsonArray{QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{0, 0, 0}},
           {"max", QJsonArray{64, 64, 16}},
         }}},
      },
      history,
      nextOperationIndex);
    REQUIRE(create.ok);
    REQUIRE(map.undoCommandName() != nullptr);
    CHECK(QString::fromStdString(*map.undoCommandName()) == "MCP: Same transaction name");

    history.back().documentPath = "D:/maps/original.map";
    history.back().documentFingerprint = "doc:original";
    const auto undo = historyUndoForMapResult(map, history);

    CHECK(!undo.ok);
    CHECK_FALSE(undo.error.details.value("mutatedDocument").toBool());
    CHECK(
      undo.error.details.value("targetOperationId").toString()
      == create.result.value("operationId").toString());
    CHECK(
      undo.error.details.value("operationDocumentPath").toString()
      == "D:/maps/original.map");
    CHECK(undo.error.details.contains("activeDocumentPath"));
    CHECK(
      undo.error.details.value("recoveryAction").toString()
      == "activate_original_document_or_refresh_status");
    CHECK_FALSE(history.back().undone);
    REQUIRE(map.undoCommandName() != nullptr);
    CHECK(QString::fromStdString(*map.undoCommandName()) == "MCP: Same transaction name");
  }

  SECTION("history_undo_mcp accepts matching registry document fingerprint")
  {
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
    auto& map = document->map();
    auto objectRegistry = McpObjectRegistry{};
    auto history = std::vector<McpOperationRecord>{};
    auto nextOperationIndex = 1;

    const auto create = blockoutCreateBatchForMapResult(
      map,
      "blockout_create_batch",
      QJsonObject{
        {"name", "MCP: Registry fingerprint undo"},
        {"operations",
         QJsonArray{QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{0, 0, 0}},
           {"max", QJsonArray{64, 64, 16}},
         }}},
      },
      history,
      nextOperationIndex);
    REQUIRE(create.ok);
    history.back().documentFingerprint = objectRegistry.documentFingerprint(map);

    const auto undo = historyUndoForMapResult(map, history, &objectRegistry);

    REQUIRE(undo.ok);
    CHECK(history.back().undone);
  }

  SECTION(
    "history_undo_to_operation stops at cross-document operation with partial diagnostic")
  {
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
    auto& map = document->map();
    auto history = std::vector<McpOperationRecord>{};
    auto nextOperationIndex = 1;

    const auto first = blockoutCreateBatchForMapResult(
      map,
      "blockout_create_batch",
      QJsonObject{
        {"name", "MCP: Cross document first"},
        {"operations",
         QJsonArray{QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{0, 0, 0}},
           {"max", QJsonArray{64, 64, 16}},
         }}},
      },
      history,
      nextOperationIndex);
    REQUIRE(first.ok);
    history.back().documentPath = "D:/maps/original.map";
    history.back().documentFingerprint = "doc:original";

    const auto latest = blockoutCreateBatchForMapResult(
      map,
      "blockout_create_batch",
      QJsonObject{
        {"name", "MCP: Cross document latest"},
        {"operations",
         QJsonArray{QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{128, 0, 0}},
           {"max", QJsonArray{192, 64, 16}},
         }}},
      },
      history,
      nextOperationIndex);
    REQUIRE(latest.ok);
    history.back().documentPath.clear();
    history.back().documentFingerprint.clear();

    const auto undo = historyUndoToOperationForMapResult(
      map,
      history,
      QJsonObject{{"operationId", first.result.value("operationId").toString()}});

    CHECK(!undo.ok);
    CHECK(undo.error.details.value("mutatedDocument").toBool());
    CHECK(undo.error.details.value("partiallyUndone").toBool());
    CHECK(
      undo.error.details.value("blockedOperationId").toString()
      == first.result.value("operationId").toString());
    CHECK(
      undo.error.details.value("operationDocumentPath").toString()
      == "D:/maps/original.map");
    CHECK(
      undo.error.details.value("recoveryAction").toString()
      == "activate_original_document_or_refresh_status");
    const auto undoneIds = undo.error.details.value("undoneOperationIds").toArray();
    REQUIRE(undoneIds.size() == 1);
    CHECK(undoneIds.first().toString() == latest.result.value("operationId").toString());
    CHECK_FALSE(history.front().undone);
    CHECK(history.back().undone);
  }

  SECTION(
    "history_redo_mcp rejects operation from a different document before native redo")
  {
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
    auto& map = document->map();
    auto history = std::vector<McpOperationRecord>{};
    auto nextOperationIndex = 1;

    const auto create = blockoutCreateBatchForMapResult(
      map,
      "blockout_create_batch",
      QJsonObject{
        {"name", "MCP: Redo cross document"},
        {"operations",
         QJsonArray{QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{0, 0, 0}},
           {"max", QJsonArray{64, 64, 16}},
         }}},
      },
      history,
      nextOperationIndex);
    REQUIRE(create.ok);
    map.undoCommand();
    history.back().undone = true;
    history.back().redoable = true;
    history.back().documentPath = "D:/maps/original.map";
    history.back().documentFingerprint = "doc:original";
    REQUIRE(map.redoCommandName() != nullptr);
    CHECK(QString::fromStdString(*map.redoCommandName()) == "MCP: Redo cross document");

    const auto redo = historyRedoForMapResult(map, history);

    CHECK(!redo.ok);
    CHECK_FALSE(redo.error.details.value("mutatedDocument").toBool());
    CHECK(
      redo.error.details.value("targetOperationId").toString()
      == create.result.value("operationId").toString());
    CHECK(
      redo.error.details.value("operationDocumentPath").toString()
      == "D:/maps/original.map");
    CHECK(
      redo.error.details.value("recoveryAction").toString()
      == "activate_original_document_or_refresh_status");
    CHECK(history.back().undone);
    REQUIRE(map.redoCommandName() != nullptr);
    CHECK(QString::fromStdString(*map.redoCommandName()) == "MCP: Redo cross document");
  }

  SECTION("geometry_csg_selection reports invalid selection before mutation")
  {
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
    auto& map = document->map();
    auto history = std::vector<McpOperationRecord>{};
    auto nextOperationIndex = 1;

    const auto noSelection = geometryCsgSelectionForMapResult(
      map,
      "geometry_csg_selection",
      QJsonObject{{"operation", "subtract"}},
      history,
      nextOperationIndex);
    CHECK(!noSelection.ok);
    CHECK_FALSE(noSelection.error.details.value("mutatedDocument").toBool());
    CHECK(noSelection.error.details.value("retrySafe").toBool(false));
    CHECK(noSelection.error.details.value("selectionSummary").isObject());

    const auto create = blockoutCreateBatchForMapResult(
      map,
      "blockout_create_batch",
      QJsonObject{
        {"name", "MCP: CSG single box"},
        {"operations",
         QJsonArray{QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{0, 0, 0}},
           {"max", QJsonArray{64, 64, 64}},
         }}},
      },
      history,
      nextOperationIndex);
    REQUIRE(create.ok);
    const auto tooFew = geometryCsgSelectionForMapResult(
      map,
      "geometry_csg_selection",
      QJsonObject{{"operation", "intersect"}},
      history,
      nextOperationIndex);
    CHECK(!tooFew.ok);
    CHECK_FALSE(tooFew.error.details.value("mutatedDocument").toBool());
    CHECK(tooFew.error.details.value("retrySafe").toBool(false));
    CHECK(tooFew.error.details.value("requiredSelection").toString().contains("two"));
  }

  SECTION("geometry_csg_selection runs hollow with compact ids and undo")
  {
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
    auto& map = document->map();
    auto history = std::vector<McpOperationRecord>{};
    auto nextOperationIndex = 1;

    const auto create = blockoutCreateBatchForMapResult(
      map,
      "blockout_create_batch",
      QJsonObject{
        {"name", "MCP: CSG hollow source"},
        {"operations",
         QJsonArray{QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{0, 0, 0}},
           {"max", QJsonArray{128, 128, 128}},
         }}},
      },
      history,
      nextOperationIndex);
    REQUIRE(create.ok);
    const auto descendantCountBefore = map.worldNode().descendantCount();

    const auto hollow = geometryCsgSelectionForMapResult(
      map,
      "geometry_csg_selection",
      QJsonObject{{"operation", "hollow"}, {"idsMode", "sample"}},
      history,
      nextOperationIndex);

    const auto error = hollow.ok ? std::string{} : hollow.error.message.toStdString();
    INFO(error);
    REQUIRE(hollow.ok);
    CHECK(hollow.result.value("mutatedDocument").toBool());
    CHECK(hollow.result.value("operation").toString() == "hollow");
    CHECK(hollow.result.value("transactionName").toString() == "MCP: CSG Hollow");
    CHECK(
      hollow.result.value("documentFingerprint").toString()
      == documentFingerprintForMap(map));
    CHECK(hollow.result.value("changedObjectCount").toInt() > 1);
    CHECK(hollow.result.value("deletedObjectCount").toInt() == 1);
    CHECK(hollow.result.value("changedObjectIdSample").toArray().size() > 1);
    CHECK(!hollow.result.contains("changedObjectIds"));
    CHECK(history.back().toolName == "geometry_csg_selection");
    CHECK(history.back().transactionName == "MCP: CSG Hollow");
    CHECK(history.back().documentFingerprint == documentFingerprintForMap(map));
    CHECK(history.back().deletedObjectIds.size() == 1);
    CHECK(map.worldNode().descendantCount() > descendantCountBefore);

    const auto undo = historyUndoForMapResult(map, history);
    REQUIRE(undo.ok);
    CHECK(history.back().undone);
    CHECK(map.worldNode().descendantCount() == descendantCountBefore);
  }

  SECTION("geometry_csg_selection supports intersect and full ids")
  {
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
    auto& map = document->map();
    auto history = std::vector<McpOperationRecord>{};
    auto nextOperationIndex = 1;

    const auto create = blockoutCreateBatchForMapResult(
      map,
      "blockout_create_batch",
      QJsonObject{
        {"name", "MCP: CSG intersect sources"},
        {"operations",
         QJsonArray{
           QJsonObject{
             {"type", "box"},
             {"min", QJsonArray{0, 0, 0}},
             {"max", QJsonArray{96, 96, 96}},
           },
           QJsonObject{
             {"type", "box"},
             {"min", QJsonArray{32, 32, 32}},
             {"max", QJsonArray{128, 128, 128}},
           },
         }},
      },
      history,
      nextOperationIndex);
    REQUIRE(create.ok);
    CHECK(map.selection().nodes.size() == 2u);

    const auto intersect = geometryCsgSelectionForMapResult(
      map,
      "geometry_csg_selection",
      QJsonObject{{"operation", "intersect"}, {"idsMode", "full"}},
      history,
      nextOperationIndex);

    REQUIRE(intersect.ok);
    CHECK(intersect.result.value("operation").toString() == "intersect");
    CHECK(intersect.result.value("changedObjectCount").toInt() == 1);
    CHECK(intersect.result.value("deletedObjectCount").toInt() == 2);
    CHECK(intersect.result.value("changedObjectIds").toArray().size() == 1);
    CHECK(intersect.result.value("deletedObjectIds").toArray().size() == 2);
    CHECK(
      intersect.result.value("selectionAfter").toObject().value("brushCount").toInt()
      == 1);
  }

  SECTION("geometry_csg_selection supports convex merge and subtract")
  {
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
    auto& map = document->map();
    auto history = std::vector<McpOperationRecord>{};
    auto nextOperationIndex = 1;

    const auto mergeSources = blockoutCreateBatchForMapResult(
      map,
      "blockout_create_batch",
      QJsonObject{
        {"name", "MCP: CSG merge sources"},
        {"operations",
         QJsonArray{
           QJsonObject{
             {"type", "box"},
             {"min", QJsonArray{0, 0, 0}},
             {"max", QJsonArray{64, 64, 64}},
           },
           QJsonObject{
             {"type", "box"},
             {"min", QJsonArray{64, 0, 0}},
             {"max", QJsonArray{128, 64, 64}},
           },
         }},
      },
      history,
      nextOperationIndex);
    REQUIRE(mergeSources.ok);

    const auto merge = geometryCsgSelectionForMapResult(
      map,
      "geometry_csg_selection",
      QJsonObject{{"operation", "convex_merge"}},
      history,
      nextOperationIndex);

    REQUIRE(merge.ok);
    CHECK(merge.result.value("changedObjectCount").toInt() == 1);
    CHECK(merge.result.value("deletedObjectCount").toInt() == 2);
    CHECK(map.selection().brushes.size() == 1u);

    const auto subtractSources = blockoutCreateBatchForMapResult(
      map,
      "blockout_create_batch",
      QJsonObject{
        {"name", "MCP: CSG subtract cutter"},
        {"operations",
         QJsonArray{QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{32, 16, 16}},
           {"max", QJsonArray{96, 48, 48}},
         }}},
      },
      history,
      nextOperationIndex);
    REQUIRE(subtractSources.ok);
    REQUIRE(map.selection().brushes.size() == 1u);

    const auto subtract = geometryCsgSelectionForMapResult(
      map,
      "geometry_csg_selection",
      QJsonObject{{"operation", "subtract"}},
      history,
      nextOperationIndex);

    REQUIRE(subtract.ok);
    CHECK(subtract.result.value("operation").toString() == "subtract");
    CHECK(subtract.result.value("deletedObjectCount").toInt() >= 1);
    CHECK(subtract.result.value("changedObjectCount").toInt() >= 1);
  }

  SECTION("geometry review renderer writes isolated nonblank review bundle")
  {
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
    auto& map = document->map();
    auto history = std::vector<McpOperationRecord>{};
    auto nextOperationIndex = 1;
    const auto createResponse = blockoutCreateBatchForMapResult(
      map,
      "blockout_create_batch",
      QJsonObject{
        {"name", "MCP: Review target"},
        {"operations",
         QJsonArray{
           QJsonObject{
             {"type", "box"},
             {"min", QJsonArray{0, 0, 0}},
             {"max", QJsonArray{128, 64, 48}},
           },
           QJsonObject{
             {"type", "box"},
             {"min", QJsonArray{160, 0, 32}},
             {"max", QJsonArray{224, 64, 96}},
           },
         }},
      },
      history,
      nextOperationIndex);
    REQUIRE(createResponse.ok);
    REQUIRE(history.size() == 1u);

    const auto wasModified = map.modified();
    auto tempDir = QTemporaryDir{};
    REQUIRE(tempDir.isValid());
    const auto relativeOutputDir =
      QDir::current().relativeFilePath(tempDir.filePath("relative-review-output"));
    auto registry = McpObjectRegistry{};
    const auto response = renderReviewTargetsForMapResult(
      map,
      QJsonObject{
        {"operationIds", QJsonArray{history.front().operationId}},
        {"views",
         QJsonArray{
           "iso_overview_ne",
           "iso_overview_sw",
           "top_plan",
           "side_elevation_long",
           "front_elevation_cross",
         }},
        {"style", "material_tint_edges"},
        {"edgeMode", "minimal"},
        {"imageSize", QJsonArray{900, 650}},
        {"contactSheetSize", QJsonArray{1200, 900}},
        {"outputDir", relativeOutputDir},
      },
      history,
      &registry);

    REQUIRE(response.ok);
    CHECK(response.result.value("tool").toString() == "render_review_targets");
    CHECK(response.result.value("renderer").toString() == "geometry_cpu");
    CHECK(response.result.value("style").toString() == "material_tint_edges");
    CHECK(response.result.value("edgeMode").toString() == "minimal");
    CHECK(response.result.value("targetObjectCount").toInt() == 2);
    CHECK(response.result.value("targetBrushCount").toInt() == 2);
    CHECK(response.result.value("captureCount").toInt() == 5);
    CHECK(response.result.value("renderReadable").toBool());
    CHECK(response.result.value("qualityValid").toBool());
    const auto semanticAcceptance =
      response.result.value("semanticAcceptance").toObject();
    CHECK(semanticAcceptance.value("required").toBool());
    CHECK(!semanticAcceptance.value("automated").toBool(true));
    CHECK(
      semanticAcceptance.value("status").toString() == "requires_human_or_skill_review");
    CHECK(map.modified() == wasModified);
    CHECK(QFileInfo::exists(response.result.value("preferredCapturePath").toString()));
    CHECK(QFileInfo{response.result.value("outputDir").toString()}.isAbsolute());
    CHECK(QFileInfo{response.result.value("absoluteOutputDir").toString()}.isAbsolute());
    CHECK(QFileInfo{response.result.value("absolutePreferredCapturePath").toString()}
            .exists());

    const auto contactSheet = response.result.value("contactSheet").toObject();
    CHECK(contactSheet.value("valid").toBool());
    CHECK(QFileInfo::exists(contactSheet.value("path").toString()));
    CHECK(QFileInfo{contactSheet.value("path").toString()}.isAbsolute());
    CHECK(QFileInfo{contactSheet.value("absolutePath").toString()}.exists());
    CHECK(contactSheet.value("sourceCaptureCount").toInt() == 5);
    CHECK(contactSheet.value("includedCaptureCount").toInt() == 2);
    CHECK(contactSheet.value("omittedCaptureCount").toInt() == 3);
    CHECK(contactSheet.value("maxCaptures").toInt() == 2);

    const auto captures = response.result.value("captures").toArray();
    REQUIRE(captures.size() == 5);
    for (const auto& captureValue : captures)
    {
      const auto capture = captureValue.toObject();
      const auto path = capture.value("path").toString();
      CHECK(QFileInfo::exists(path));
      const auto image = QImage{path};
      CHECK(!image.isNull());
      CHECK(image.width() >= 900);
      CHECK(image.height() >= 650);
      CHECK(capture.value("targetCoverage").toDouble() > 0.0);
      CHECK(capture.value("edgeDensity").toDouble() > 0.0);
      CHECK(capture.value("valid").toBool());
    }

    CHECK(QFileInfo::exists(response.result.value("manifestPath").toString()));

    const auto renderEdgeMode = [&](const QString& edgeMode, const QString& outputDir) {
      return renderReviewTargetsForMapResult(
        map,
        QJsonObject{
          {"operationIds", QJsonArray{history.front().operationId}},
          {"views", QJsonArray{"top_plan"}},
          {"style", "whitebox_edges"},
          {"edgeMode", edgeMode},
          {"combineViews", false},
          {"includeAxes", false},
          {"imageSize", QJsonArray{900, 650}},
          {"outputDir", outputDir},
          {"detail", "full"},
        },
        history,
        &registry);
    };
    const auto allEdges = renderEdgeMode("all", tempDir.filePath("review-all-edges"));
    const auto silhouette =
      renderEdgeMode("silhouette", tempDir.filePath("review-silhouette"));
    REQUIRE(allEdges.ok);
    REQUIRE(silhouette.ok);
    CHECK(silhouette.result.value("edgeMode").toString() == "silhouette");
    CHECK(silhouette.result.value("edgeInterpretation").toString() == "silhouette");
    CHECK_FALSE(silhouette.result.value("internalBrushEdgesDrawn").toBool(true));
    CHECK_FALSE(silhouette.result.value("visualReviewRequired").toBool(true));
    const auto allCapture =
      allEdges.result.value("captures").toArray().first().toObject();
    const auto silhouetteCapture =
      silhouette.result.value("captures").toArray().first().toObject();
    CHECK(
      std::abs(
        allCapture.value("targetCoverage").toDouble()
        - silhouetteCapture.value("targetCoverage").toDouble())
      < 0.02);
    CHECK(
      silhouetteCapture.value("edgeDensity").toDouble()
      < allCapture.value("edgeDensity").toDouble());
  }

  SECTION("current scene review auto-collects brushes and returns compact summary")
  {
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
    auto& map = document->map();

    auto history = std::vector<McpOperationRecord>{};
    auto nextOperationIndex = 1;
    const auto createResponse = blockoutCreateBatchForMapResult(
      map,
      "blockout_create_batch",
      QJsonObject{
        {"name", "MCP: Current scene review"},
        {"operations",
         QJsonArray{
           QJsonObject{
             {"type", "box"},
             {"min", QJsonArray{0, 0, 0}},
             {"max", QJsonArray{128, 64, 32}},
           },
           QJsonObject{
             {"type", "box"},
             {"min", QJsonArray{160, 16, 16}},
             {"max", QJsonArray{240, 80, 64}},
           },
         }},
      },
      history,
      nextOperationIndex);
    REQUIRE(createResponse.ok);

    auto tempDir = QTemporaryDir{};
    REQUIRE(tempDir.isValid());
    const auto response = renderReviewCurrentSceneForMapResult(
      map,
      QJsonObject{
        {"preset", "auto"},
        {"scope", "mcp_history"},
        {"outputDir", tempDir.path()},
        {"imageSize", QJsonArray{900, 650}},
      },
      history);

    REQUIRE(response.ok);
    CHECK(response.result.value("tool").toString() == "render_review_current_scene");
    CHECK(response.result.value("scope").toString() == "mcp_history");
    CHECK(response.result.value("renderer").toString() == "geometry_cpu");
    CHECK(response.result.value("targetBrushCount").toInt() == 2);
    CHECK(response.result.value("renderReadable").toBool());
    CHECK(response.result.value("qualityValid").toBool());
    const auto semanticAcceptance =
      response.result.value("semanticAcceptance").toObject();
    CHECK(semanticAcceptance.value("required").toBool());
    CHECK(!semanticAcceptance.value("automated").toBool(true));
    CHECK(QFileInfo::exists(response.result.value("preferredCapturePath").toString()));
    CHECK(response.result.value("captures").isUndefined());
    CHECK(response.result.value("targetObjectIds").isUndefined());

    const auto routeResponse = renderReviewCurrentSceneForMapResult(
      map,
      QJsonObject{
        {"preset", "route_platform"},
        {"scope", "mcp_history"},
        {"outputDir", tempDir.path()},
        {"imageSize", QJsonArray{900, 650}},
      },
      history);
    REQUIRE(routeResponse.ok);
    CHECK(routeResponse.result.value("preset").toString() == "route_platform");
    CHECK(routeResponse.result.value("style").toString() == "whitebox_edges");
    CHECK(routeResponse.result.value("edgeMode").toString() == "all");
    CHECK(routeResponse.result.value("verticalExaggeration").toDouble() == 1.6);
    CHECK(routeResponse.result.value("labelCount").toInt() == 2);
    CHECK(routeResponse.result.value("renderReadable").toBool());
    CHECK(routeResponse.result.value("qualityValid").toBool());
  }

  SECTION("geometry review renderer draws point entity glyph labels")
  {
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
    auto& map = document->map();

    auto entity = mdl::Entity{{{"classname", "info_player_start"}}};
    entity.setOrigin(vm::vec3d{64.0, 96.0, 32.0});
    auto* entityNode = new mdl::EntityNode{std::move(entity)};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {entityNode}}});

    auto tempDir = QTemporaryDir{};
    REQUIRE(tempDir.isValid());
    auto registry = McpObjectRegistry{};
    const auto entityId = registry.registerNode(map, *entityNode);
    const auto response = renderReviewTargetsForMapResult(
      map,
      QJsonObject{
        {"objectIds", QJsonArray{entityId}},
        {"views", QJsonArray{"top_plan"}},
        {"includeEntityLabels", true},
        {"imageSize", QJsonArray{900, 650}},
        {"outputDir", tempDir.path()},
      },
      {},
      &registry);

    REQUIRE(response.ok);
    CHECK(response.result.value("targetObjectCount").toInt() == 1);
    CHECK(response.result.value("targetBrushCount").toInt() == 0);
    CHECK(response.result.value("unsupportedObjectCount").toInt() == 1);
    CHECK(response.result.value("labelCount").toInt() == 1);
    CHECK(response.result.value("edgeCount").toInt() == 3);
    CHECK(response.result.value("renderReadable").toBool());
    CHECK(response.result.value("qualityValid").toBool());
    CHECK(QFileInfo::exists(response.result.value("preferredCapturePath").toString()));
  }

  SECTION("geometry review renderer auto-hides dense entity labels but keeps glyphs")
  {
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
    auto& map = document->map();

    auto registry = McpObjectRegistry{};
    auto objectIds = QJsonArray{};
    for (auto i = 0; i < 4; ++i)
    {
      auto entity = mdl::Entity{{{"classname", i == 0 ? "info_player_start" : "light"}}};
      entity.setOrigin(vm::vec3d{i * 32.0, 0.0, 32.0});
      auto* entityNode = new mdl::EntityNode{std::move(entity)};
      mdl::addNodes(map, {{&mdl::parentForNodes(map), {entityNode}}});
      objectIds.push_back(registry.registerNode(map, *entityNode));
    }

    auto tempDir = QTemporaryDir{};
    REQUIRE(tempDir.isValid());
    const auto response = renderReviewTargetsForMapResult(
      map,
      QJsonObject{
        {"objectIds", objectIds},
        {"views", QJsonArray{"top_plan"}},
        {"includeEntityLabels", true},
        {"autoHideLabelsThreshold", 3},
        {"imageSize", QJsonArray{900, 650}},
        {"outputDir", tempDir.path()},
      },
      {},
      &registry);

    REQUIRE(response.ok);
    CHECK(response.result.value("targetObjectCount").toInt() == 4);
    CHECK(response.result.value("unsupportedObjectCount").toInt() == 4);
    CHECK(response.result.value("entityLabelCount").toInt() == 0);
    CHECK(response.result.value("labelCount").toInt() == 0);
    CHECK(response.result.value("edgeCount").toInt() == 12);
    CHECK(response.result.value("qualityValid").toBool());
    const auto warnings = response.result.value("warnings").toArray();
    CHECK(std::ranges::any_of(warnings, [](const auto& warning) {
      return warning.toString().startsWith("entityLabelsAutoHidden");
    }));
  }

  SECTION("geometry review renderer labels requested metadata parts")
  {
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
    auto& map = document->map();

    auto history = std::vector<McpOperationRecord>{};
    auto nextOperationIndex = 1;
    auto metadataStore = std::map<QString, McpBrushMetadataRecord>{};
    auto moduleStore = std::map<QString, McpModuleRecord>{};
    const auto createResponse = blockoutCreateBatchForMapResult(
      map,
      "blockout_create_batch",
      QJsonObject{
        {"name", "MCP: Review part labels"},
        {"defaultMetadata", QJsonObject{{"moduleId", "review-label-module"}}},
        {"operations",
         QJsonArray{
           QJsonObject{
             {"type", "box"},
             {"min", QJsonArray{0, 0, 0}},
             {"max", QJsonArray{128, 64, 16}},
             {"metadata", QJsonObject{{"part", "road"}}},
           },
           QJsonObject{
             {"type", "box"},
             {"min", QJsonArray{0, -24, 16}},
             {"max", QJsonArray{128, -8, 40}},
             {"metadata", QJsonObject{{"part", "rail"}}},
           },
         }},
      },
      history,
      nextOperationIndex,
      &metadataStore,
      &moduleStore);
    REQUIRE(createResponse.ok);

    auto tempDir = QTemporaryDir{};
    REQUIRE(tempDir.isValid());
    const auto response = renderReviewTargetsForMapResult(
      map,
      QJsonObject{
        {"operationIds", QJsonArray{createResponse.result.value("operationId")}},
        {"views", QJsonArray{"top_plan"}},
        {"labelParts", QJsonArray{"rail"}},
        {"imageSize", QJsonArray{900, 650}},
        {"outputDir", tempDir.path()},
      },
      history,
      nullptr,
      &metadataStore);

    REQUIRE(response.ok);
    CHECK(response.result.value("targetObjectCount").toInt() == 2);
    CHECK(response.result.value("partLabelCount").toInt() == 1);
    CHECK(response.result.value("labelCount").toInt() == 1);
    CHECK(response.result.value("qualityValid").toBool());
  }
}

TEST_CASE(
  "McpBridgeServer history_undo_to_operation reports pre-mutation target failures",
  "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};

  const auto missingId = historyUndoToOperationForMapResult(map, history, QJsonObject{});
  CHECK(!missingId.ok);
  CHECK_FALSE(missingId.error.details.value("mutatedDocument").toBool(true));
  CHECK_FALSE(missingId.error.details.value("partiallyUndone").toBool(true));
  CHECK(missingId.error.details.value("retrySafe").toBool(false));
  CHECK(
    missingId.error.details.value("recoveryAction").toString()
    == "provide_operation_id_then_retry");

  const auto unknownId = historyUndoToOperationForMapResult(
    map, history, QJsonObject{{"operationId", "mcp-op-missing"}});
  CHECK(!unknownId.ok);
  CHECK_FALSE(unknownId.error.details.value("mutatedDocument").toBool(true));
  CHECK_FALSE(unknownId.error.details.value("partiallyUndone").toBool(true));
  CHECK(unknownId.error.details.value("retrySafe").toBool(false));
  CHECK(
    unknownId.error.details.value("targetOperationId").toString() == "mcp-op-missing");
  CHECK(
    unknownId.error.details.value("recoveryAction").toString()
    == "refresh_status_or_validate");
}

TEST_CASE(
  "McpBridgeServer single-step history reports pre-mutation failures",
  "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;

  const auto emptyUndo = historyUndoForMapResult(map, history);
  CHECK(!emptyUndo.ok);
  CHECK_FALSE(emptyUndo.error.details.value("mutatedDocument").toBool(true));
  CHECK_FALSE(emptyUndo.error.details.value("partiallyUndone").toBool(true));
  CHECK(emptyUndo.error.details.value("retrySafe").toBool(false));
  CHECK(
    emptyUndo.error.details.value("recoveryAction").toString()
    == "refresh_status_or_validate");

  const auto emptyRedo = historyRedoForMapResult(map, history);
  CHECK(!emptyRedo.ok);
  CHECK_FALSE(emptyRedo.error.details.value("mutatedDocument").toBool(true));
  CHECK_FALSE(emptyRedo.error.details.value("partiallyUndone").toBool(true));
  CHECK(emptyRedo.error.details.value("retrySafe").toBool(false));
  CHECK(
    emptyRedo.error.details.value("recoveryAction").toString()
    == "refresh_status_or_validate");

  const auto create = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"name", "MCP: Single undo target"},
      {"operations",
       QJsonArray{QJsonObject{
         {"type", "box"},
         {"min", QJsonArray{0, 0, 0}},
         {"max", QJsonArray{64, 64, 16}},
       }}},
    },
    history,
    nextOperationIndex);
  REQUIRE(create.ok);

  const auto unrelated = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"name", "User non-MCP operation"},
      {"operations",
       QJsonArray{QJsonObject{
         {"type", "box"},
         {"min", QJsonArray{128, 0, 0}},
         {"max", QJsonArray{192, 64, 16}},
       }}},
    },
    history,
    nextOperationIndex);
  REQUIRE(unrelated.ok);
  history.back().undone = true;

  const auto blockedUndo = historyUndoForMapResult(map, history);
  CHECK(!blockedUndo.ok);
  CHECK_FALSE(blockedUndo.error.details.value("mutatedDocument").toBool(true));
  CHECK(blockedUndo.error.details.value("retrySafe").toBool(false));
  CHECK(
    blockedUndo.error.details.value("targetOperationId").toString()
    == create.result.value("operationId").toString());
  CHECK(
    blockedUndo.error.details.value("nativeUndoCommandName").toString()
    == "User non-MCP operation");
  CHECK(
    blockedUndo.error.details.value("recoveryAction").toString()
    == "refresh_status_or_validate");

  history.front().undone = true;
  history.front().redoable = true;
  const auto blockedRedo = historyRedoForMapResult(map, history);
  CHECK(!blockedRedo.ok);
  CHECK_FALSE(blockedRedo.error.details.value("mutatedDocument").toBool(true));
  CHECK(blockedRedo.error.details.value("retrySafe").toBool(false));
  CHECK(
    blockedRedo.error.details.value("targetOperationId").toString()
    == create.result.value("operationId").toString());
  CHECK(blockedRedo.error.details.value("nativeRedoCommandName").toString().isEmpty());
  CHECK(
    blockedRedo.error.details.value("recoveryAction").toString()
    == "refresh_status_or_validate");
}

TEST_CASE(
  "McpBridgeServer new mutations discard abandoned MCP redo candidates",
  "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  const auto initialDescendantCount = map.worldNode().descendantCount();

  const auto first = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"name", "MCP: Abandoned redo candidate"},
      {"operations",
       QJsonArray{QJsonObject{
         {"type", "box"},
         {"min", QJsonArray{0, 0, 0}},
         {"max", QJsonArray{64, 64, 16}},
       }}},
    },
    history,
    nextOperationIndex);
  REQUIRE(first.ok);
  REQUIRE(history.size() == 1);
  REQUIRE(historyUndoForMapResult(map, history).ok);
  CHECK(history[0].undone);
  CHECK(history[0].redoable);

  const auto second = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"name", "MCP: Current redo candidate"},
      {"operations",
       QJsonArray{QJsonObject{
         {"type", "box"},
         {"min", QJsonArray{128, 0, 0}},
         {"max", QJsonArray{192, 64, 16}},
       }}},
    },
    history,
    nextOperationIndex);
  REQUIRE(second.ok);
  REQUIRE(history.size() == 2);
  CHECK_FALSE(history[0].redoable);

  REQUIRE(historyUndoForMapResult(map, history).ok);
  CHECK(history[1].undone);
  CHECK(history[1].redoable);
  const auto redo = historyRedoForMapResult(map, history);
  REQUIRE(redo.ok);
  CHECK_FALSE(history[1].undone);
  CHECK_FALSE(history[1].redoable);
  CHECK(map.worldNode().descendantCount() == initialDescendantCount + 1);
}

TEST_CASE(
  "McpBridgeServer operation_select reports non-document mutation state",
  "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  auto registry = McpObjectRegistry{};

  const auto missingId =
    operationSelectForMapResult(map, history, QJsonObject{}, &registry);
  CHECK(!missingId.ok);
  CHECK_FALSE(missingId.error.details.value("mutatedDocument").toBool(true));
  CHECK(missingId.error.details.value("retrySafe").toBool(false));
  CHECK(
    missingId.error.details.value("recoveryAction").toString()
    == "provide_operation_id_then_retry");

  const auto unknownId = operationSelectForMapResult(
    map, history, QJsonObject{{"operationId", "mcp-op-missing"}}, &registry);
  CHECK(!unknownId.ok);
  CHECK_FALSE(unknownId.error.details.value("mutatedDocument").toBool(true));
  CHECK(unknownId.error.details.value("retrySafe").toBool(false));
  CHECK(unknownId.error.details.value("operationId").toString() == "mcp-op-missing");
  CHECK(
    unknownId.error.details.value("recoveryAction").toString()
    == "refresh_status_or_validate");

  const auto create = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"operations",
       QJsonArray{QJsonObject{
         {"type", "box"},
         {"min", QJsonArray{0, 0, 0}},
         {"max", QJsonArray{64, 64, 16}},
       }}},
    },
    history,
    nextOperationIndex);
  REQUIRE(create.ok);

  const auto selected = operationSelectForMapResult(
    map,
    history,
    QJsonObject{{"operationId", create.result.value("operationId").toString()}},
    &registry);
  REQUIRE(selected.ok);
  CHECK_FALSE(selected.result.value("mutatedDocument").toBool(true));
  CHECK(selected.result.value("selectedCount").toInt() == 1);
  CHECK(map.selection().nodes.size() == 1u);
}

TEST_CASE(
  "McpBridgeServer operation_validate reports recovery target failures",
  "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  auto registry = McpObjectRegistry{};

  const auto missingId =
    operationValidateForMapResult(map, history, QJsonObject{}, registry);
  CHECK(!missingId.ok);
  CHECK_FALSE(missingId.error.details.value("mutatedDocument").toBool(true));
  CHECK(missingId.error.details.value("retrySafe").toBool(false));
  CHECK(
    missingId.error.details.value("recoveryAction").toString()
    == "provide_operation_id_then_retry");

  const auto unknownId = operationValidateForMapResult(
    map, history, QJsonObject{{"operationId", "mcp-op-missing"}}, registry);
  CHECK(!unknownId.ok);
  CHECK_FALSE(unknownId.error.details.value("mutatedDocument").toBool(true));
  CHECK(unknownId.error.details.value("retrySafe").toBool(false));
  CHECK(unknownId.error.details.value("operationId").toString() == "mcp-op-missing");
  CHECK(
    unknownId.error.details.value("recoveryAction").toString()
    == "refresh_status_or_validate");

  const auto create = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"operations",
       QJsonArray{QJsonObject{
         {"type", "box"},
         {"min", QJsonArray{0, 0, 0}},
         {"max", QJsonArray{64, 64, 16}},
       }}},
    },
    history,
    nextOperationIndex);
  REQUIRE(create.ok);

  const auto valid = operationValidateForMapResult(
    map,
    history,
    QJsonObject{{"operationId", create.result.value("operationId").toString()}},
    registry);
  REQUIRE(valid.ok);
  CHECK(valid.result.value("valid").toBool());
  CHECK_FALSE(valid.result.value("mutatedDocument").toBool(true));
}

TEST_CASE(
  "McpBridgeServer operation_inspect reports recovery target failures",
  "[McpBridgeServer]")
{
  auto history = std::vector<McpOperationRecord>{};

  const auto missingId = operationInspectResult(history, QJsonObject{});
  CHECK(!missingId.ok);
  CHECK_FALSE(missingId.error.details.value("mutatedDocument").toBool(true));
  CHECK(missingId.error.details.value("retrySafe").toBool(false));
  CHECK(
    missingId.error.details.value("recoveryAction").toString()
    == "provide_operation_id_then_retry");

  const auto unknownId =
    operationInspectResult(history, QJsonObject{{"operationId", "mcp-op-missing"}});
  CHECK(!unknownId.ok);
  CHECK_FALSE(unknownId.error.details.value("mutatedDocument").toBool(true));
  CHECK(unknownId.error.details.value("retrySafe").toBool(false));
  CHECK(unknownId.error.details.value("operationId").toString() == "mcp-op-missing");
  CHECK(
    unknownId.error.details.value("recoveryAction").toString()
    == "refresh_status_or_validate");

  auto operation = McpOperationRecord{};
  operation.operationId = "mcp-op-1";
  operation.toolName = "blockout_create_batch";
  operation.transactionName = "MCP: Inspect test";
  operation.changedObjectIds = {"node:world"};
  history.push_back(operation);

  const auto inspect =
    operationInspectResult(history, QJsonObject{{"operationId", "mcp-op-1"}});
  REQUIRE(inspect.ok);
  CHECK_FALSE(inspect.result.value("mutatedDocument").toBool(true));
  CHECK(inspect.result.value("operationId").toString() == "mcp-op-1");
}

TEST_CASE(
  "McpBridgeServer module_compact reports non-document mutation state",
  "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto metadataStore = std::map<QString, McpBrushMetadataRecord>{};
  auto moduleStore = std::map<QString, McpModuleRecord>{};
  auto objectRegistry = McpObjectRegistry{};

  const auto missingId = moduleCompactForMapResult(
    map, QJsonObject{}, metadataStore, moduleStore, objectRegistry);
  CHECK(!missingId.ok);
  CHECK_FALSE(missingId.error.details.value("mutatedDocument").toBool(true));
  CHECK(missingId.error.details.value("retrySafe").toBool(false));
  CHECK(
    missingId.error.details.value("recoveryAction").toString()
    == "provide_module_id_then_retry");

  const auto fingerprint = documentFingerprintForMap(map, &objectRegistry);
  moduleStore[QString{"%1|stale-module"}.arg(fingerprint)] = McpModuleRecord{
    "stale-module",
    fingerprint,
    {"node:999"},
    {},
    {},
    {},
    {},
    {},
    {},
  };

  const auto compact = moduleCompactForMapResult(
    map,
    QJsonObject{{"moduleId", "stale-module"}},
    metadataStore,
    moduleStore,
    objectRegistry);
  REQUIRE(compact.ok);
  CHECK_FALSE(compact.result.value("mutatedDocument").toBool(true));
  CHECK(compact.result.value("removedStaleObjectIdCount").toInt() == 1);
  CHECK(moduleStore.begin()->second.objectIds.empty());
}

TEST_CASE(
  "McpBridgeServer selection tools report non-document mutation state",
  "[McpBridgeServer]")
{
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
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;

  const auto response =
    selectionSetForMapResult(document->map(), QJsonObject{{"objectIds", QJsonArray{}}});

  REQUIRE(response.ok);
  CHECK(response.result.value("selectedCount").toInt(-1) == 0);
  CHECK(response.result.value("mutatedDocument").toBool(true) == false);

  const auto missingObjectIdsResponse =
    selectionSetForMapResult(document->map(), QJsonObject{});
  CHECK(!missingObjectIdsResponse.ok);
  CHECK(
    missingObjectIdsResponse.error.details.value("mutatedDocument").toBool(true)
    == false);
  CHECK(missingObjectIdsResponse.error.details.value("retrySafe").toBool(false));
  CHECK(
    missingObjectIdsResponse.error.details.value("recoveryAction").toString()
    == "provide_object_ids_then_retry");

  const auto unknownObjectResponse = selectionSetForMapResult(
    document->map(), QJsonObject{{"objectIds", QJsonArray{"mcp-object-missing"}}});
  CHECK(!unknownObjectResponse.ok);
  CHECK(
    unknownObjectResponse.error.details.value("mutatedDocument").toBool(true) == false);
  CHECK(unknownObjectResponse.error.details.value("retrySafe").toBool(false));
  CHECK(
    unknownObjectResponse.error.details.value("recoveryAction").toString()
    == "refresh_status_or_fix_object_ids");
  CHECK(
    unknownObjectResponse.error.details.value("objectId").toString()
    == "mcp-object-missing");

  const auto filterResponse = selectionFilterForMapResult(document->map(), QJsonObject{});
  REQUIRE(filterResponse.ok);
  CHECK(filterResponse.result.value("mutatedDocument").toBool(true) == false);

  const auto invalidFilterResponse = selectionFilterForMapResult(
    document->map(),
    QJsonObject{
      {"min", QJsonArray{16, 16, 16}},
      {"max", QJsonArray{-16, -16, -16}},
    });
  CHECK(!invalidFilterResponse.ok);
  CHECK(
    invalidFilterResponse.error.details.value("mutatedDocument").toBool(true) == false);
  CHECK(invalidFilterResponse.error.details.value("retrySafe").toBool(false));
  CHECK(
    invalidFilterResponse.error.details.value("recoveryAction").toString()
    == "fix_selection_query_then_retry");

  const auto boundsResponse = selectionByBoundsForMapResult(
    document->map(),
    QJsonObject{
      {"min", QJsonArray{-16, -16, -16}},
      {"max", QJsonArray{16, 16, 16}},
    });
  REQUIRE(boundsResponse.ok);
  CHECK(boundsResponse.result.value("mutatedDocument").toBool(true) == false);

  const auto invalidBoundsResponse = selectionByBoundsForMapResult(
    document->map(),
    QJsonObject{
      {"min", QJsonArray{16, 16, 16}},
      {"max", QJsonArray{-16, -16, -16}},
    });
  CHECK(!invalidBoundsResponse.ok);
  CHECK(
    invalidBoundsResponse.error.details.value("mutatedDocument").toBool(true) == false);
  CHECK(invalidBoundsResponse.error.details.value("retrySafe").toBool(false));
  CHECK(
    invalidBoundsResponse.error.details.value("recoveryAction").toString()
    == "fix_selection_query_then_retry");

  const auto growResponse = selectionGrowForMapResult(document->map(), QJsonObject{});
  REQUIRE(growResponse.ok);
  CHECK(growResponse.result.value("selectedCount").toInt(-1) == 0);
  CHECK(growResponse.result.value("mutatedDocument").toBool(true) == false);

  const auto createResponse = blockoutCreateBatchForMapResult(
    document->map(),
    "blockout_create_batch",
    QJsonObject{
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{0, 0, 0}},
           {"max", QJsonArray{64, 64, 64}},
         },
       }},
      {"select", true},
    },
    history,
    nextOperationIndex);
  REQUIRE(createResponse.ok);

  const auto invalidGrowResponse =
    selectionGrowForMapResult(document->map(), QJsonObject{{"mode", "cousins"}});
  CHECK(!invalidGrowResponse.ok);
  CHECK(invalidGrowResponse.error.details.value("mutatedDocument").toBool(true) == false);
  CHECK(invalidGrowResponse.error.details.value("retrySafe").toBool(false));
  CHECK(
    invalidGrowResponse.error.details.value("recoveryAction").toString()
    == "fix_selection_grow_mode_then_retry");
  CHECK(invalidGrowResponse.error.details.value("mode").toString() == "cousins");
}

TEST_CASE("McpBridgeServer spiral stair geometry tools", "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  auto objectRegistry = McpObjectRegistry{};
  const auto descendantCountBeforeInvalid = map.worldNode().descendantCount();
  const auto invalidResponse = blockoutCreateSpiralStairsForMapResult(
    map,
    QJsonObject{
      {"innerRadius", 128},
      {"outerRadius", 32},
      {"steps", 24},
    },
    history,
    nextOperationIndex);
  CHECK(!invalidResponse.ok);
  CHECK(invalidResponse.error.details.value("mutatedDocument").toBool(true) == false);
  CHECK(invalidResponse.error.details.value("retrySafe").toBool(false));
  CHECK(
    invalidResponse.error.details.value("recoveryAction").toString()
    == "fix_spiral_stairs_parameters_then_retry");
  CHECK(map.worldNode().descendantCount() == descendantCountBeforeInvalid);
  CHECK(history.empty());

  const auto createResponse = blockoutCreateSpiralStairsForMapResult(
    map,
    QJsonObject{
      {"center", QJsonArray{0, 0, 0}},
      {"innerRadius", 32},
      {"outerRadius", 128},
      {"steps", 24},
      {"stepHeight", 8},
      {"turnDegrees", 360},
      {"select", true},
    },
    history,
    nextOperationIndex);

  const auto createError =
    createResponse.ok ? std::string{} : createResponse.error.message.toStdString();
  INFO(createError);
  REQUIRE(createResponse.ok);
  CHECK(
    createResponse.result.value("transactionName").toString()
    == "MCP: Blockout spiral stairs");
  CHECK(createResponse.result.value("operationId").toString() == "mcp-op-1");
  CHECK(createResponse.result.value("brushCount").toInt() == 26);
  CHECK(map.selection().nodes.size() == 26u);

  const auto validation = createResponse.result.value("validation").toObject();
  CHECK(validation.value("valid").toBool());
  CHECK(validation.value("gapCount").toInt() == 0);
  CHECK(!validation.value("radiusMismatch").toBool());
  CHECK(validation.value("columnFits").toBool());
  CHECK(validation.value("landingConnected").toBool());
  CHECK(validation.value("invalidBrushCount").toInt() == 0);

  const auto analyzeResponse = geometryAnalyzeSelectionResult(
    map, QJsonObject{{"grid", 1}, {"includeVertices", false}});
  REQUIRE(analyzeResponse.ok);
  CHECK(analyzeResponse.result.value("brushCount").toInt() == 26);
  CHECK(analyzeResponse.result.value("invalidBrushCount").toInt() == 0);
  CHECK(analyzeResponse.result.value("nonGridAlignedCount").toInt() == 0);
  CHECK(analyzeResponse.result.value("detail").toString() == "summary");
  CHECK(analyzeResponse.result.value("brushes").isUndefined());
  CHECK(analyzeResponse.result.value("objectIds").isUndefined());

  const auto fullAnalyzeResponse = geometryAnalyzeSelectionResult(
    map,
    QJsonObject{
      {"grid", 1},
      {"detail", "full"},
      {"maxBrushes", 2},
      {"includeVertices", false},
    });
  REQUIRE(fullAnalyzeResponse.ok);
  CHECK(fullAnalyzeResponse.result.value("brushCount").toInt() == 26);
  CHECK(fullAnalyzeResponse.result.value("returnedBrushCount").toInt() == 2);
  CHECK(fullAnalyzeResponse.result.value("truncated").toBool());
  CHECK(fullAnalyzeResponse.result.value("brushes").toArray().size() == 2);

  const auto idsAnalyzeResponse = geometryAnalyzeSelectionResult(
    map, QJsonObject{{"grid", 1}, {"detail", "ids"}, {"maxBrushes", 3}});
  REQUIRE(idsAnalyzeResponse.ok);
  CHECK(idsAnalyzeResponse.result.value("brushCount").toInt() == 26);
  CHECK(idsAnalyzeResponse.result.value("returnedBrushCount").toInt() == 3);
  CHECK(idsAnalyzeResponse.result.value("truncated").toBool());
  CHECK(idsAnalyzeResponse.result.value("objectIds").toArray().size() == 3);
  CHECK(idsAnalyzeResponse.result.value("brushes").isUndefined());

  const auto offGridResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "cylinder_sector"},
           {"center", QJsonArray{512, 0, 0}},
           {"innerRadius", 96},
           {"outerRadius", 224},
           {"startAngle", 15},
           {"endAngle", 105},
           {"minZ", 0},
           {"maxZ", 16},
           {"snapMode", "radial"},
         },
       }},
      {"select", true},
    },
    history,
    nextOperationIndex);
  REQUIRE(offGridResponse.ok);
  const auto offGridAnalyzeResponse =
    geometryAnalyzeSelectionResult(map, QJsonObject{{"grid", 1}});
  REQUIRE(offGridAnalyzeResponse.ok);
  CHECK(offGridAnalyzeResponse.result.value("nonGridAlignedCount").toInt() == 1);
  CHECK(
    offGridAnalyzeResponse.result.value("nonGridAlignedObjectIds").toArray().size() == 1);
  map.undoCommand();

  const auto validateResponse = blockoutValidateSpiralStairsResult(
    map,
    QJsonObject{
      {"operationId", createResponse.result.value("operationId").toString()},
      {"center", QJsonArray{0, 0, 0}},
      {"innerRadius", 32},
      {"outerRadius", 128},
      {"steps", 24},
      {"stepHeight", 8},
      {"turnDegrees", 360},
      {"grid", 1},
    },
    history);
  REQUIRE(validateResponse.ok);
  CHECK(validateResponse.result.value("valid").toBool());
  CHECK(validateResponse.result.value("source").toString() == "operationId");
  CHECK(validateResponse.result.value("brushCountMatches").toBool());
  CHECK(validateResponse.result.value("gapCount").toInt() == 0);
  CHECK(validateResponse.result.value("columnFits").toBool());
  CHECK(validateResponse.result.value("landingConnected").toBool());

  REQUIRE(map.undoCommandName() != nullptr);
  CHECK(QString::fromStdString(*map.undoCommandName()) == "MCP: Blockout spiral stairs");
  map.undoCommand();
  CHECK(map.worldNode().childCount() == 1u);
}

TEST_CASE("McpBridgeServer MCP read semantics", "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;

  const auto createResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"name", "MCP: Snapshot read semantics"},
      {"detail", "ids"},
      {"select", true},
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{1024, 0, 0}},
           {"max", QJsonArray{1088, 64, 16}},
         },
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{1216, 0, 0}},
           {"max", QJsonArray{1280, 64, 16}},
         },
       }},
    },
    history,
    nextOperationIndex);
  const auto createError =
    createResponse.ok ? std::string{} : createResponse.error.message.toStdString();
  INFO(createError);
  REQUIRE(createResponse.ok);

  const auto snapshot = mapSnapshotJsonForMap(map, QJsonObject{});
  CHECK(snapshot.value("brushCount").toInt() == 2);
  CHECK(snapshot.value("bounds").toObject().value("min").toArray()[0].toDouble() == 1024);
  CHECK(snapshot.value("bounds").toObject().value("max").toArray()[0].toDouble() == 1280);
  CHECK(snapshot.value("world").toObject().value("logicalBounds").isObject());
  CHECK_FALSE(snapshot.value("world").toObject().value("selectable").toBool(true));
  CHECK_FALSE(snapshot.value("world").toObject().value("operationSafe").toBool(true));
  CHECK(
    snapshot.value("world")
      .toObject()
      .value("contentBounds")
      .toObject()
      .value("max")
      .toArray()[0]
      .toDouble()
    == 1280);

  const auto selection = selectionJsonForMap(map);
  CHECK(selection.value("brushCount").toInt() == 2);
  CHECK(selection.value("brushFaceCount").toInt() == 0);
  CHECK(selection.value("selectedBrushTotalFaceCount").toInt() == 12);
  CHECK(selection.value("selectedBrushFaceCount").toInt() == 12);

  const auto objectIds = createResponse.result.value("changedObjectIds").toArray();
  REQUIRE(objectIds.size() == 2);
  const auto firstBrushPath =
    McpObjectRegistry::parseLegacyObjectId(objectIds[0].toString());
  REQUIRE(firstBrushPath);
  auto* firstBrushNode =
    dynamic_cast<mdl::BrushNode*>(map.worldNode().resolvePath(*firstBrushPath));
  REQUIRE(firstBrushNode != nullptr);
  mdl::deselectAll(map);
  mdl::selectBrushFaces(
    map,
    {mdl::BrushFaceHandle{firstBrushNode, 0}, mdl::BrushFaceHandle{firstBrushNode, 1}});

  const auto faceSelection = selectionJsonForMap(map);
  CHECK(faceSelection.value("brushCount").toInt() == 0);
  CHECK(faceSelection.value("brushFaceCount").toInt() == 2);
  CHECK(faceSelection.value("selectedBrushFaceCount").toInt() == 0);
  CHECK(faceSelection.value("faceOwnerBrushCount").toInt() == 1);
  CHECK(
    faceSelection.value("faceOwnerBrushIds").toArray().first().toString()
    == objectIds[0].toString());
  const auto faceOwner =
    faceSelection.value("faceOwnerBrushes").toArray().first().toObject();
  CHECK(faceOwner.value("id").toString() == objectIds[0].toString());
  CHECK(faceOwner.value("selectedFaceCount").toInt() == 2);

  const auto boundsResponse = selectionByBoundsForMapResult(
    map,
    QJsonObject{
      {"min", QJsonArray{1000, -16, -16}},
      {"max", QJsonArray{1300, 80, 32}},
      {"detail", "full"},
    });
  REQUIRE(boundsResponse.ok);
  CHECK(boundsResponse.result.value("count").toInt() == 2);
  CHECK(
    boundsResponse.result.value("filters").toObject().value("selectableOnly").toBool());
  CHECK(boundsResponse.result.value("filters").toObject().value("leafOnly").toBool());
  for (const auto& value : boundsResponse.result.value("results").toArray())
  {
    const auto object = value.toObject();
    CHECK(object.value("type").toString() == "brush");
  }

  const auto textureResponse = textureSearchForMapResult(
    map, QJsonObject{{"query", "unlikely_missing_material_name"}});
  REQUIRE(textureResponse.ok);
  CHECK(textureResponse.result.value("count").toInt() == 0);
  CHECK(textureResponse.result.value("materials").toArray().isEmpty());
  CHECK(textureResponse.result.value("materialNames").toArray().isEmpty());
  CHECK(!textureResponse.result.value("fallbackMaterial").toString().isEmpty());

  map.undoCommand();
}

TEST_CASE("McpBridgeServer selection_inspect", "[McpBridgeServer]")
{
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
  auto& map = document->map();

  SECTION("returns selected entity properties in full detail")
  {
    auto entity = mdl::Entity{{
      {"classname", "path_corner"},
      {"targetname", "corner01"},
      {"target", "corner02"},
      {"angles", "0 90 0"},
    }};
    entity.setOrigin(vm::vec3d{-256.0, 1008.0, 0.0});
    auto* entityNode = new mdl::EntityNode{std::move(entity)};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {entityNode}}});
    mdl::selectNodes(map, {entityNode});

    const auto response = selectionInspectForMapResult(
      map, QJsonObject{{"detail", "full"}, {"includeProperties", true}});

    REQUIRE(response.ok);
    CHECK(response.result.value("selectedCount").toInt() == 1);
    CHECK(response.result.value("detail").toString() == "full");
    const auto objects = response.result.value("objects").toArray();
    REQUIRE(objects.size() == 1);
    const auto object = objects.first().toObject();
    CHECK(object.value("type").toString() == "entity");
    CHECK(object.value("classname").toString() == "path_corner");
    CHECK(object.value("origin").toArray()[0].toDouble() == -256.0);
    CHECK(object.value("origin").toArray()[1].toDouble() == 1008.0);

    const auto properties = object.value("properties").toObject();
    CHECK(properties.value("targetname").toString() == "corner01");
    CHECK(properties.value("target").toString() == "corner02");
    CHECK(properties.value("angles").toString() == "0 90 0");
  }

  SECTION("keeps brush face selection compact by default")
  {
    auto history = std::vector<McpOperationRecord>{};
    auto nextOperationIndex = 1;
    const auto createResponse = createBrushForMapResult(
      map,
      "brush_create_box",
      QJsonObject{
        {"min", QJsonArray{0, 0, 0}},
        {"max", QJsonArray{64, 64, 64}},
        {"material", "debug/selection_inspect"},
      },
      history,
      nextOperationIndex);
    REQUIRE(createResponse.ok);

    REQUIRE(map.selection().nodes.size() == 1u);
    auto* brushNode = dynamic_cast<mdl::BrushNode*>(map.selection().nodes.front());
    REQUIRE(brushNode != nullptr);

    mdl::deselectAll(map);
    mdl::selectBrushFaces(map, {mdl::BrushFaceHandle{brushNode, 0}});

    const auto response = selectionInspectForMapResult(map, QJsonObject{});

    REQUIRE(response.ok);
    CHECK(response.result.value("selectedCount").toInt() == 0);
    CHECK(response.result.value("selectedFaceCount").toInt() == 1);
    const auto selectedFaces = response.result.value("selectedFaces").toArray();
    REQUIRE(selectedFaces.size() == 1);
    const auto selectedFace = selectedFaces.first().toObject();
    CHECK(selectedFace.value("type").toString() == "face");
    CHECK(selectedFace.value("faceIndex").toInt() == 0);
    CHECK(selectedFace.value("material").toString() == "debug/selection_inspect");

    const auto faceOwners = response.result.value("faceOwnerBrushes").toArray();
    REQUIRE(faceOwners.size() == 1);
    const auto faceOwner = faceOwners.first().toObject();
    CHECK(faceOwner.value("type").toString() == "brush");
    CHECK(faceOwner.value("selectedFaceCount").toInt() == 1);
    CHECK(faceOwner.value("faceCount").toInt() == 6);
    CHECK_FALSE(faceOwner.contains("faces"));
  }
}

TEST_CASE("McpBridgeServer entity_link_chain_inspect", "[McpBridgeServer]")
{
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
  auto& map = document->map();

  auto corner01Entity = mdl::Entity{{
    {"classname", "path_corner"},
    {"targetname", "corner01"},
    {"target", "corner02"},
  }};
  corner01Entity.setOrigin(vm::vec3d{0.0, 0.0, 0.0});
  auto* corner01 = new mdl::EntityNode{std::move(corner01Entity)};
  auto corner02Entity = mdl::Entity{{
    {"classname", "path_corner"},
    {"targetname", "corner02"},
    {"target", "corner03"},
  }};
  corner02Entity.setOrigin(vm::vec3d{128.0, 0.0, 16.0});
  auto* corner02 = new mdl::EntityNode{std::move(corner02Entity)};
  auto corner03Entity = mdl::Entity{{
    {"classname", "path_corner"},
    {"targetname", "corner03"},
  }};
  corner03Entity.setOrigin(vm::vec3d{256.0, 64.0, 32.0});
  auto* corner03 = new mdl::EntityNode{std::move(corner03Entity)};
  mdl::addNodes(map, {{&mdl::parentForNodes(map), {corner01, corner02, corner03}}});
  mdl::selectNodes(map, {corner01});

  SECTION("follows selected path_corner target links")
  {
    const auto response = entityLinkChainInspectForMapResult(
      map,
      QJsonObject{
        {"classname", "path_corner"},
        {"start", QJsonObject{{"source", "selection"}}},
        {"nameKey", "targetname"},
        {"nextKey", "target"},
        {"detail", "summary"},
      });

    REQUIRE(response.ok);
    CHECK(response.result.value("chainComplete").toBool());
    CHECK(response.result.value("nodeCount").toInt() == 3);
    CHECK(response.result.value("edgeCount").toInt() == 2);
    CHECK(response.result.value("hasCycle").toBool() == false);
    const auto nodes = response.result.value("nodes").toArray();
    REQUIRE(nodes.size() == 3);
    CHECK(nodes[0].toObject().value("targetname").toString() == "corner01");
    CHECK(nodes[1].toObject().value("targetname").toString() == "corner02");
    CHECK(nodes[2].toObject().value("targetname").toString() == "corner03");
    CHECK(nodes[1].toObject().value("origin").toArray()[2].toDouble() == 16.0);
  }

  SECTION("reports duplicate targetnames without guessing")
  {
    auto duplicateEntity = mdl::Entity{{
      {"classname", "path_corner"},
      {"targetname", "corner02"},
    }};
    duplicateEntity.setOrigin(vm::vec3d{512.0, 0.0, 0.0});
    auto* duplicate = new mdl::EntityNode{std::move(duplicateEntity)};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {duplicate}}});

    const auto response = entityLinkChainInspectForMapResult(
      map,
      QJsonObject{
        {"classname", "path_corner"},
        {"start", QJsonObject{{"source", "selection"}}},
      });

    REQUIRE(response.ok);
    CHECK_FALSE(response.result.value("chainComplete").toBool());
    CHECK(response.result.value("duplicateNameCount").toInt() == 1);
    const auto failures = response.result.value("failures").toArray();
    REQUIRE(failures.size() >= 1);
    CHECK(
      failures.first().toObject().value("status").toString() == "duplicate_targetname");
  }

  SECTION("reports candidate starts when selection is not a matching entity")
  {
    auto history = std::vector<McpOperationRecord>{};
    auto nextOperationIndex = 1;
    const auto createBrush = createBrushForMapResult(
      map,
      "brush_create_box",
      QJsonObject{
        {"min", QJsonArray{0, 0, 0}},
        {"max", QJsonArray{64, 64, 16}},
        {"material", "debug/entity_link_candidate"},
      },
      history,
      nextOperationIndex);
    REQUIRE(createBrush.ok);

    const auto response = entityLinkChainInspectForMapResult(
      map,
      QJsonObject{
        {"classname", "path_corner"},
        {"start", QJsonObject{{"source", "selection"}}},
        {"includeAllNodes", true},
      });

    REQUIRE_FALSE(response.ok);
    CHECK(
      response.error.details.value("recoveryAction").toString()
      == "select_one_start_entity_or_use_start_targetname");
    CHECK(response.error.details.value("selectedMatchingEntityCount").toInt() == 0);
    CHECK(response.error.details.value("candidateNodeCount").toInt() == 3);
    const auto allNodes = response.error.details.value("allNodes").toArray();
    REQUIRE(allNodes.size() == 3);
    CHECK(allNodes.first().toObject().value("targetname").toString() == "corner01");
  }
}

TEST_CASE("McpBridgeServer stable MCP object identity", "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  auto registry = McpObjectRegistry{};

  auto server = McpBridgeServer{
    [&](const QString& toolName, const QJsonObject& params) {
      if (toolName == "blockout_create_batch")
      {
        return blockoutCreateBatchForMapResult(
          map, toolName, params, history, nextOperationIndex);
      }
      if (toolName == "selection_set")
      {
        const auto ids = params.value("objectIds").toArray();
        return McpBridgeToolResult::success(QJsonObject{
          {"selectedCount", ids.size()},
          {"receivedLegacyPath", ids.isEmpty() ? QString{} : ids.first().toString()},
        });
      }
      if (toolName == "objects_delete")
      {
        return deleteObjectsForMapResult(
          map, toolName, params, history, nextOperationIndex);
      }
      if (toolName == "history_list")
      {
        return historyListResult(history);
      }
      if (toolName == "operation_inspect")
      {
        return operationInspectResult(history, params);
      }
      if (toolName == "geometry_analyze_selection")
      {
        return geometryAnalyzeSelectionResult(map, params);
      }
      return McpBridgeToolResult::failure(
        mcp::McpErrorCode::ToolNotFound, QString{"Unexpected tool: %1"}.arg(toolName));
    },
    [&map]() -> mdl::Map* { return &map; }};
  REQUIRE(server.start(mcp::McpBridgeConfig{"test-pipe-stable-id", mcp::McpMode::Edit}));

  const auto createResponse = server.dispatchRequest(mcp::McpBridgeRequest{
    "1",
    "blockout_create_batch",
    QJsonObject{
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{0, 0, 0}},
           {"max", QJsonArray{64, 64, 16}},
         },
       }},
      {"detail", "ids"},
    },
    mcp::McpMode::Edit});
  const auto createError = createResponse.ok ? QString{} : createResponse.error->message;
  INFO(createError.toStdString());
  REQUIRE(createResponse.ok);
  const auto objectIds = createResponse.result.value("changedObjectIds").toArray();
  REQUIRE(objectIds.size() == 1);
  const auto stableId = objectIds.first().toString();
  CHECK(stableId.startsWith("mcp:"));

  const auto resource =
    server.readResource(createResponse.result.value("resourceUri").toString());
  REQUIRE(resource);
  CHECK(resource->value("changedObjectCount").toInt() == 1);
  CHECK(resource->value("changedObjectIds").isUndefined());
  CHECK(resource->value("deletedObjectIds").isUndefined());
  CHECK(resource->value("idsDetail").toString().contains("operation_inspect"));

  const auto inspectIdsResponse = server.dispatchRequest(mcp::McpBridgeRequest{
    "1b",
    "operation_inspect",
    QJsonObject{
      {"operationId", createResponse.result.value("operationId").toString()},
      {"idsMode", "full"},
    },
    mcp::McpMode::ReadOnly});
  REQUIRE(inspectIdsResponse.ok);
  CHECK(inspectIdsResponse.result.value("changedObjectIds").toArray().size() == 1);

  const auto selectResponse = server.dispatchRequest(mcp::McpBridgeRequest{
    "2",
    "selection_set",
    QJsonObject{{"objectIds", QJsonArray{stableId}}},
    mcp::McpMode::ReadOnly});
  REQUIRE(selectResponse.ok);
  CHECK(selectResponse.result.value("selectedCount").toInt() == 1);
  CHECK(selectResponse.result.value("receivedLegacyPath").toString().startsWith("node:"));

  REQUIRE(history.size() == 1u);
  CHECK(history.back().changedObjectIds.front().startsWith("node:"));
  const auto legacyId = selectResponse.result.value("receivedLegacyPath").toString();
  CHECK(history.back().changedObjectIds.front() == legacyId);

  const auto directStableId = registry.externalIdForLegacy(map, legacyId);
  CHECK(directStableId.startsWith("mcp:"));
  const auto liveState = registry.liveStateJson(map, QStringList{directStableId}, false);
  CHECK(liveState.value("valid").toBool());
  CHECK(liveState.value("liveObjectCount").toInt() == 1);
  CHECK(liveState.value("staleObjectCount").toInt() == 0);
  CHECK(liveState.value("mismatchCount").toInt() == 0);

  REQUIRE(map.undoCommandName() != nullptr);
  map.undoCommand();

  const auto staleState = registry.liveStateJson(map, QStringList{directStableId}, false);
  CHECK(!staleState.value("valid").toBool());
  CHECK(staleState.value("staleObjectCount").toInt() == 1);

  const auto replacementResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{0, 0, 0}},
           {"max", QJsonArray{64, 64, 16}},
         },
       }},
      {"detail", "ids"},
    },
    history,
    nextOperationIndex);
  REQUIRE(replacementResponse.ok);
  const auto replacementObjectIds =
    replacementResponse.result.value("changedObjectIds").toArray();
  REQUIRE(replacementObjectIds.size() == 1);
  const auto replacementLegacyId = replacementObjectIds.first().toString();
  CHECK(replacementLegacyId == legacyId);

  const auto replacementStableId = registry.externalIdForLegacy(map, replacementLegacyId);
  CHECK(replacementStableId.startsWith("mcp:"));
  CHECK(replacementStableId != directStableId);
  const auto replacementState =
    registry.liveStateJson(map, QStringList{replacementStableId}, false);
  CHECK(replacementState.value("valid").toBool());
  CHECK(replacementState.value("liveObjectCount").toInt() == 1);
  CHECK(replacementState.value("mismatchCount").toInt() == 0);

  const auto pathShiftResponse = server.dispatchRequest(mcp::McpBridgeRequest{
    "3",
    "blockout_create_batch",
    QJsonObject{
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{256, 0, 0}},
           {"max", QJsonArray{320, 64, 16}},
         },
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{336, 0, 0}},
           {"max", QJsonArray{400, 64, 16}},
         },
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{416, 0, 0}},
           {"max", QJsonArray{480, 64, 16}},
         },
       }},
      {"detail", "ids"},
    },
    mcp::McpMode::Edit});
  REQUIRE(pathShiftResponse.ok);
  const auto pathShiftObjectIds =
    pathShiftResponse.result.value("changedObjectIds").toArray();
  REQUIRE(pathShiftObjectIds.size() == 3);
  const auto deletedStableId = pathShiftObjectIds[1].toString();
  REQUIRE(history.size() >= 3u);
  REQUIRE(history.back().changedObjectIds.size() == 3);
  const auto deletedStableIdForState =
    registry.externalIdForLegacy(map, history.back().changedObjectIds[1]);
  const auto shiftedStableIdForState =
    registry.externalIdForLegacy(map, history.back().changedObjectIds[2]);

  const auto deleteShiftedSiblingResponse = server.dispatchRequest(mcp::McpBridgeRequest{
    "4",
    "objects_delete",
    QJsonObject{{"objectIds", QJsonArray{deletedStableId}}},
    mcp::McpMode::Edit});
  REQUIRE(deleteShiftedSiblingResponse.ok);

  const auto shiftedState =
    registry.liveStateJson(map, QStringList{shiftedStableIdForState}, false, true);
  CHECK(shiftedState.value("valid").toBool());
  CHECK(shiftedState.value("liveObjectCount").toInt() == 1);
  CHECK(shiftedState.value("mismatchCount").toInt() == 0);
  CHECK(shiftedState.value("staleObjectCount").toInt() == 0);
  const auto shiftedDiagnostics =
    shiftedState.value("objectDiagnostics").toArray().first().toObject();
  CHECK(shiftedDiagnostics.value("pathChanged").toBool());

  const auto deletedStableState =
    registry.liveStateJson(map, QStringList{deletedStableIdForState}, false, true);
  CHECK(!deletedStableState.value("valid").toBool());
  CHECK(deletedStableState.value("staleObjectCount").toInt() == 1);
  CHECK(deletedStableState.value("mismatchCount").toInt() == 0);
  const auto deletedDiagnostics =
    deletedStableState.value("objectDiagnostics").toArray().first().toObject();
  CHECK(deletedDiagnostics.value("pathReused").toBool());

  const auto offGridCreateResponse = server.dispatchRequest(mcp::McpBridgeRequest{
    "5",
    "blockout_create_batch",
    QJsonObject{
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "cylinder_sector"},
           {"center", QJsonArray{512, 0, 0}},
           {"innerRadius", 96},
           {"outerRadius", 224},
           {"startAngle", 15},
           {"endAngle", 105},
           {"minZ", 0},
           {"maxZ", 16},
           {"snapMode", "radial"},
         },
       }},
      {"select", true},
      {"detail", "ids"},
    },
    mcp::McpMode::Edit});
  REQUIRE(offGridCreateResponse.ok);

  const auto analyzeResponse = server.dispatchRequest(mcp::McpBridgeRequest{
    "6",
    "geometry_analyze_selection",
    QJsonObject{{"grid", 1}, {"detail", "summary"}},
    mcp::McpMode::ReadOnly});
  REQUIRE(analyzeResponse.ok);
  CHECK(analyzeResponse.result.value("nonGridAlignedCount").toInt() == 1);
  const auto nonGridAlignedObjectIds =
    analyzeResponse.result.value("nonGridAlignedObjectIds").toArray();
  REQUIRE(nonGridAlignedObjectIds.size() == 1);
  CHECK(nonGridAlignedObjectIds.first().toString().startsWith("mcp:"));

  const auto deleteCreateResponse = server.dispatchRequest(mcp::McpBridgeRequest{
    "7",
    "blockout_create_batch",
    QJsonObject{
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{1024, 0, 0}},
           {"max", QJsonArray{1088, 64, 16}},
         },
       }},
      {"detail", "ids"},
    },
    mcp::McpMode::Edit});
  REQUIRE(deleteCreateResponse.ok);
  const auto deleteCreateIds =
    deleteCreateResponse.result.value("changedObjectIds").toArray();
  REQUIRE(deleteCreateIds.size() == 1);
  const auto stableDeleteTarget = deleteCreateIds.first().toString();
  REQUIRE(stableDeleteTarget.startsWith("mcp:"));

  const auto deleteResponse = server.dispatchRequest(mcp::McpBridgeRequest{
    "8",
    "objects_delete",
    QJsonObject{{"objectIds", QJsonArray{stableDeleteTarget}}},
    mcp::McpMode::Edit});
  REQUIRE(deleteResponse.ok);
  CHECK(deleteResponse.result.value("operationKind").toString() == "delete");
  CHECK(deleteResponse.result.value("changedObjectCount").toInt() == 0);
  CHECK(deleteResponse.result.value("deletedObjectCount").toInt() == 1);
  CHECK(deleteResponse.result.value("deletedObjectIds").isUndefined());
  CHECK(deleteResponse.result.value("deletedObjectIdSample").isUndefined());
  REQUIRE(history.size() >= 4u);
  CHECK(history.back().operationKind == "delete");
  REQUIRE(history.back().deletedObjectIds.size() == 1);
  CHECK(history.back().deletedObjectIds.front().startsWith("node:"));

  const auto inspectDeleteResponse = server.dispatchRequest(mcp::McpBridgeRequest{
    "9",
    "operation_inspect",
    QJsonObject{
      {"operationId", deleteResponse.result.value("operationId").toString()},
      {"detail", "ids"},
    },
    mcp::McpMode::ReadOnly});
  REQUIRE(inspectDeleteResponse.ok);
  const auto inspectedDeletedObjectIds =
    inspectDeleteResponse.result.value("deletedObjectIds").toArray();
  REQUIRE(inspectedDeletedObjectIds.size() == 1);
  CHECK(inspectedDeletedObjectIds.first().toString() == stableDeleteTarget);

  const auto fullDeleteCreateResponse = server.dispatchRequest(mcp::McpBridgeRequest{
    "10",
    "blockout_create_batch",
    QJsonObject{
      {"operations",
       QJsonArray{QJsonObject{
         {"type", "box"},
         {"min", QJsonArray{1152, 0, 0}},
         {"max", QJsonArray{1216, 64, 16}},
       }}},
      {"detail", "ids"},
    },
    mcp::McpMode::Edit});
  REQUIRE(fullDeleteCreateResponse.ok);
  const auto fullDeleteTarget = fullDeleteCreateResponse.result.value("changedObjectIds")
                                  .toArray()
                                  .first()
                                  .toString();
  const auto fullDeleteResponse = server.dispatchRequest(mcp::McpBridgeRequest{
    "11",
    "objects_delete",
    QJsonObject{{"objectIds", QJsonArray{fullDeleteTarget}}, {"idsMode", "full"}},
    mcp::McpMode::Edit});
  REQUIRE(fullDeleteResponse.ok);
  const auto fullDeletedObjectIds =
    fullDeleteResponse.result.value("deletedObjectIds").toArray();
  REQUIRE(fullDeletedObjectIds.size() == 1);
  CHECK(fullDeletedObjectIds.first().toString() == fullDeleteTarget);
}

TEST_CASE(
  "McpBridgeServer selector delete reports pre-mutation failure state",
  "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  auto metadataStore = std::map<QString, McpBrushMetadataRecord>{};
  auto moduleStore = std::map<QString, McpModuleRecord>{};
  auto objectRegistry = McpObjectRegistry{};

  const auto response = objectsDeleteBySelectorForMapResult(
    map,
    "objects_delete_by_selector",
    QJsonObject{{"selector", QJsonObject{{"moduleId", "missing-module"}}}},
    history,
    nextOperationIndex,
    metadataStore,
    moduleStore,
    objectRegistry);

  REQUIRE_FALSE(response.ok);
  CHECK(response.error.details.value("mutatedDocument").toBool(true) == false);
  CHECK(response.error.details.value("retrySafe").toBool(false));
  CHECK(response.error.details.value("matchedCount").toInt(-1) == 0);
  CHECK(
    response.error.details.value("recoveryAction").toString()
    == "preview_selector_or_refresh_status");

  const auto invalidSelectorResponse = objectsDeleteBySelectorForMapResult(
    map,
    "objects_delete_by_selector",
    QJsonObject{{"selector", QJsonObject{{"operationIds", QJsonArray{QJsonValue{42}}}}}},
    history,
    nextOperationIndex,
    metadataStore,
    moduleStore,
    objectRegistry);

  REQUIRE_FALSE(invalidSelectorResponse.ok);
  CHECK(
    invalidSelectorResponse.error.details.value("mutatedDocument").toBool(true) == false);
  CHECK(invalidSelectorResponse.error.details.value("retrySafe").toBool(false));
  CHECK(
    invalidSelectorResponse.error.details.value("recoveryAction").toString()
    == "fix_selector_then_retry");

  const auto createResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"defaultMetadata", QJsonObject{{"moduleId", "delete-target"}}},
      {"operations",
       QJsonArray{QJsonObject{
         {"type", "box"},
         {"min", QJsonArray{0, 0, 0}},
         {"max", QJsonArray{64, 64, 16}},
       }}},
    },
    history,
    nextOperationIndex,
    &metadataStore,
    &moduleStore,
    &objectRegistry);
  REQUIRE(createResponse.ok);

  const auto deleteResponse = objectsDeleteBySelectorForMapResult(
    map,
    "objects_delete_by_selector",
    QJsonObject{{"selector", QJsonObject{{"moduleId", "delete-target"}}}},
    history,
    nextOperationIndex,
    metadataStore,
    moduleStore,
    objectRegistry);

  REQUIRE(deleteResponse.ok);
  CHECK(deleteResponse.result.value("mutatedDocument").toBool());
  CHECK(
    deleteResponse.result.value("documentFingerprint").toString()
    == documentFingerprintForMap(map));
  CHECK(deleteResponse.result.value("operationKind").toString() == "delete");
  CHECK(history.back().documentFingerprint == documentFingerprintForMap(map));
}

TEST_CASE(
  "McpBridgeServer asset placement records document identity", "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;

  const auto response = placeAssetForMapResult(
    map,
    "asset_place_model",
    QJsonObject{{"path", "models/player.mdl"}, {"idsMode", "sample"}},
    history,
    nextOperationIndex);

  const auto error = response.ok ? std::string{} : response.error.message.toStdString();
  INFO(error);
  REQUIRE(response.ok);
  CHECK(response.result.value("mutatedDocument").toBool());
  CHECK(
    response.result.value("documentFingerprint").toString()
    == documentFingerprintForMap(map));
  REQUIRE_FALSE(history.empty());
  CHECK(history.back().toolName == "asset_place_model");
  CHECK(history.back().documentFingerprint == documentFingerprintForMap(map));
}

TEST_CASE(
  "McpBridgeServer asset placement reports pre-mutation failures", "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;

  const auto missingPathResponse = placeAssetForMapResult(
    map, "asset_place_model", QJsonObject{}, history, nextOperationIndex);
  REQUIRE_FALSE(missingPathResponse.ok);
  CHECK(missingPathResponse.error.details.value("mutatedDocument").toBool(true) == false);
  CHECK(missingPathResponse.error.details.value("retrySafe").toBool(false));
  CHECK(
    missingPathResponse.error.details.value("recoveryAction").toString()
    == "provide_asset_path_then_retry");

  const auto typeMismatchResponse = placeAssetForMapResult(
    map,
    "asset_place_sprite",
    QJsonObject{{"path", "models/player.mdl"}},
    history,
    nextOperationIndex);
  REQUIRE_FALSE(typeMismatchResponse.ok);
  CHECK(
    typeMismatchResponse.error.details.value("mutatedDocument").toBool(true) == false);
  CHECK(
    typeMismatchResponse.error.details.value("recoveryAction").toString()
    == "choose_matching_asset_place_tool_or_path");

  const auto invalidOriginResponse = placeAssetForMapResult(
    map,
    "asset_place_model",
    QJsonObject{{"path", "models/player.mdl"}, {"origin", QJsonArray{0, 0}}},
    history,
    nextOperationIndex);
  REQUIRE_FALSE(invalidOriginResponse.ok);
  CHECK(
    invalidOriginResponse.error.details.value("mutatedDocument").toBool(true) == false);
  CHECK(
    invalidOriginResponse.error.details.value("recoveryAction").toString()
    == "provide_valid_origin_then_retry");
}

TEST_CASE("McpBridgeServer problem fixes record document identity", "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;

  mdl::deselectAll(map);
  mdl::selectNodes(map, {&map.worldNode()});
  REQUIRE(mdl::setEntityProperty(map, "", ""));

  const auto response = mapFixAllSafeForMapResult(
    map,
    "map_fix_all_safe",
    QJsonObject{{"idsMode", "sample"}, {"includeHidden", true}},
    history,
    nextOperationIndex);

  const auto error = response.ok ? std::string{} : response.error.message.toStdString();
  INFO(error);
  REQUIRE(response.ok);
  CHECK(response.result.value("mutatedDocument").toBool());
  CHECK(
    response.result.value("documentFingerprint").toString()
    == documentFingerprintForMap(map));
  CHECK(response.result.value("fixedCount").toInt() == 1);
  REQUIRE_FALSE(history.empty());
  CHECK(history.back().toolName == "map_fix_all_safe");
  CHECK(history.back().documentFingerprint == documentFingerprintForMap(map));
}

TEST_CASE(
  "McpBridgeServer map validation reports recovery summaries", "[McpBridgeServer]")
{
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
  auto& map = document->map();

  const auto cleanValidateResponse = mapValidateForMapResult(
    map, QJsonObject{{"groupByType", true}, {"includeProblems", true}});
  REQUIRE(cleanValidateResponse.ok);
  const auto cleanProblemCount = cleanValidateResponse.result.value("count").toInt();
  CHECK(cleanValidateResponse.result.value("valid").toBool() == (cleanProblemCount == 0));
  CHECK(
    cleanValidateResponse.result.value("passed").toBool() == (cleanProblemCount == 0));
  CHECK(
    cleanValidateResponse.result.value("recoveryAction").toString()
    == (cleanProblemCount == 0 ? "continue_validation_or_review" : "inspect_problem_summary_then_fix_or_review"));
  CHECK(cleanValidateResponse.result.value("problems").isArray());
  CHECK(cleanValidateResponse.result.value("groups").isArray());

  mdl::deselectAll(map);
  mdl::selectNodes(map, {&map.worldNode()});
  REQUIRE(mdl::setEntityProperty(map, "", ""));

  const auto invalidValidateResponse =
    mapValidateForMapResult(map, QJsonObject{{"groupByType", true}});
  REQUIRE(invalidValidateResponse.ok);
  CHECK_FALSE(invalidValidateResponse.result.value("valid").toBool());
  CHECK_FALSE(invalidValidateResponse.result.value("passed").toBool());
  CHECK(invalidValidateResponse.result.value("count").toInt() > cleanProblemCount);
  CHECK(
    invalidValidateResponse.result.value("recoveryAction").toString()
    == "inspect_problem_summary_then_fix_or_review");

  const auto problemsResponse =
    problemsCheckForMapResult(map, QJsonObject{{"includeHidden", true}});
  REQUIRE(problemsResponse.ok);
  CHECK_FALSE(problemsResponse.result.value("passed").toBool());
  CHECK(
    problemsResponse.result.value("totalCount").toInt()
    == problemsResponse.result.value("returnedCount").toInt());
  CHECK_FALSE(problemsResponse.result.value("truncated").toBool(true));
  const auto problems = problemsResponse.result.value("problems").toArray();
  CHECK(std::ranges::any_of(problems, [](const auto& value) {
    const auto problem = value.toObject();
    if (
      !problem.contains("stableKey") || !problem.contains("propertyKey")
      || !problem.value("propertyKey").toString().isEmpty())
    {
      return false;
    }
    const auto stableKey = problem.value("stableKey").toString();
    return stableKey != problem.value("id").toString()
           && !stableKey.contains(
             QString{":%1:property:"}.arg(problem.value("lineNumber").toInt()));
  }));
  CHECK(
    problemsResponse.result.value("recoveryAction").toString()
    == "inspect_problem_summary_then_fix_or_review");

  const auto truncated =
    problemsCheckForMapResult(map, QJsonObject{{"includeHidden", true}, {"limit", 1}});
  REQUIRE(truncated.ok);
  CHECK(truncated.result.value("returnedCount").toInt() == 1);
  CHECK(truncated.result.value("totalCount").toInt() >= 1);
  CHECK(
    truncated.result.value("truncated").toBool()
    == (truncated.result.value("totalCount").toInt() > 1));
}

TEST_CASE(
  "McpBridgeServer problems_fix reports pre-mutation failures", "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;

  const auto missingIdsResponse = problemsFixForMapResult(
    map, "problems_fix", QJsonObject{}, history, nextOperationIndex);
  REQUIRE_FALSE(missingIdsResponse.ok);
  CHECK(missingIdsResponse.error.details.value("mutatedDocument").toBool(true) == false);
  CHECK(missingIdsResponse.error.details.value("retrySafe").toBool(false));
  CHECK(
    missingIdsResponse.error.details.value("recoveryAction").toString()
    == "provide_problem_ids_then_retry");

  const auto missingQuickFixResponse = problemsFixForMapResult(
    map,
    "problems_fix",
    QJsonObject{{"problemIds", QJsonArray{"problem:missing"}}},
    history,
    nextOperationIndex);
  REQUIRE_FALSE(missingQuickFixResponse.ok);
  CHECK(
    missingQuickFixResponse.error.details.value("mutatedDocument").toBool(true) == false);
  CHECK(
    missingQuickFixResponse.error.details.value("recoveryAction").toString()
    == "provide_quick_fix_then_retry");

  const auto missingProblemResponse = problemsFixForMapResult(
    map,
    "problems_fix",
    QJsonObject{
      {"problemIds", QJsonArray{"problem:missing"}},
      {"quickFix", "Delete"},
    },
    history,
    nextOperationIndex);
  REQUIRE_FALSE(missingProblemResponse.ok);
  CHECK(
    missingProblemResponse.error.details.value("mutatedDocument").toBool(true) == false);
  CHECK(
    missingProblemResponse.error.details.value("recoveryAction").toString()
    == "refresh_problems_then_retry");
}

TEST_CASE("McpBridgeServer externalizes native group object ids", "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  auto registry = McpObjectRegistry{};

  auto server = McpBridgeServer{
    [&](const QString& toolName, const QJsonObject& params) {
      if (toolName == "blockout_create_batch")
      {
        return blockoutCreateBatchForMapResult(
          map, toolName, params, history, nextOperationIndex);
      }
      if (toolName == "group_create_from_selection")
      {
        return groupCreateFromSelectionForMapResult(
          map, toolName, params, history, nextOperationIndex, registry);
      }
      if (toolName == "group_inspect")
      {
        return McpBridgeToolResult::success(QJsonObject{
          {"receivedLegacyPath", params.value("objectId").toString()},
        });
      }
      return McpBridgeToolResult::failure(
        mcp::McpErrorCode::ToolNotFound, QString{"Unexpected tool: %1"}.arg(toolName));
    },
    [&map]() -> mdl::Map* { return &map; }};
  REQUIRE(
    server.start(mcp::McpBridgeConfig{"test-pipe-group-stable-id", mcp::McpMode::Edit}));

  const auto createResponse = server.dispatchRequest(mcp::McpBridgeRequest{
    "1",
    "blockout_create_batch",
    QJsonObject{
      {"select", true},
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{0, 0, 0}},
           {"max", QJsonArray{64, 64, 16}},
         },
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{80, 0, 0}},
           {"max", QJsonArray{144, 64, 16}},
         },
       }},
    },
    mcp::McpMode::Edit});
  REQUIRE(createResponse.ok);
  REQUIRE(map.selection().nodes.size() == 2);

  const auto groupResponse = server.dispatchRequest(mcp::McpBridgeRequest{
    "2",
    "group_create_from_selection",
    QJsonObject{{"name", "stable-group"}, {"idsMode", "count"}},
    mcp::McpMode::Edit});
  const auto groupError = groupResponse.ok ? QString{} : groupResponse.error->message;
  INFO(groupError.toStdString());
  REQUIRE(groupResponse.ok);
  const auto groupId = groupResponse.result.value("groupId").toString();
  CHECK(groupId.startsWith("mcp:"));
  CHECK(
    groupId
    != createResponse.result.value("changedObjectIds").toArray().first().toString());
  CHECK(
    groupResponse.result.value("group").toObject().value("groupId").toString()
    == groupId);
  CHECK(groupResponse.result.value("changedObjectCount").toInt() == 1);
  CHECK(
    groupResponse.result.value("childCounts").toObject().value("brushes").toInt() == 2);

  const auto inspectResponse = server.dispatchRequest(mcp::McpBridgeRequest{
    "3", "group_inspect", QJsonObject{{"objectId", groupId}}, mcp::McpMode::ReadOnly});
  REQUIRE(inspectResponse.ok);
  CHECK(
    inspectResponse.result.value("receivedLegacyPath").toString().startsWith("node:"));
}

TEST_CASE(
  "McpBridgeServer scopes selector metadata and modules to active document",
  "[McpBridgeServer]")
{
  auto appControllerFixture = AppControllerFixture{};
  auto& appController = appControllerFixture.appController();
  auto firstDocument = MapDocument::createDocument(
                         appController.environmentConfig(),
                         mdl::QuakeGameInfo,
                         mdl::MapFormat::Valve,
                         vm::bbox3d{8192.0},
                         appController.taskManager(),
                         appController.glManager().resourceManager())
                       | kdl::value();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  auto metadataStore = std::map<QString, McpBrushMetadataRecord>{};
  auto moduleStore = std::map<QString, McpModuleRecord>{};
  auto objectRegistry = McpObjectRegistry{};

  auto& map = firstDocument->map();
  auto firstCreate = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"detail", "ids"},
      {"select", false},
      {"defaultMetadata",
       QJsonObject{
         {"moduleId", "doc-a-module"},
         {"role", "walkable"},
         {"generatedBy", "test"},
       }},
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{0, 0, 0}},
           {"max", QJsonArray{64, 64, 16}},
           {"metadata",
            QJsonObject{{"routeId", "shared-route"}, {"part", "road"}, {"order", 1}}},
         },
       }},
    },
    history,
    nextOperationIndex,
    &metadataStore,
    &moduleStore,
    &objectRegistry);
  const auto firstCreateError =
    firstCreate.ok ? std::string{} : firstCreate.error.message.toStdString();
  INFO(firstCreateError);
  REQUIRE(firstCreate.ok);
  CHECK(firstCreate.result.value("changedObjectCount").toInt() == 1);

  const auto currentFingerprint = documentFingerprintForMap(map);
  const auto otherFingerprint = QString{"doc:other-map"};
  moduleStore[QString{"%1|doc-b-module"}.arg(otherFingerprint)] = McpModuleRecord{
    "doc-b-module",
    otherFingerprint,
    QStringList{"node:0"},
    QStringList{"mcp-op-other"},
    QJsonObject{{"moduleId", "doc-b-module"}, {"role", "walkable"}},
    0,
    {},
    {},
    {},
  };
  metadataStore[QString{"%1|node:0"}.arg(otherFingerprint)] = McpBrushMetadataRecord{
    "node:0",
    otherFingerprint,
    QJsonObject{{"moduleId", "doc-b-module"}, {"routeId", "shared-route"}},
    false,
  };

  const auto selector = selectorPreviewForMapResult(
    map,
    QJsonObject{{"selector", QJsonObject{{"routeId", "shared-route"}}}},
    history,
    metadataStore,
    moduleStore,
    objectRegistry);
  REQUIRE(selector.ok);
  CHECK(selector.result.value("matchedCount").toInt() == 1);

  const auto modules = moduleListForMapResult(
    map, QJsonObject{}, metadataStore, moduleStore, objectRegistry);
  REQUIRE(modules.ok);
  CHECK(modules.result.value("moduleCount").toInt() == 1);
  const auto moduleArray = modules.result.value("modules").toArray();
  REQUIRE(moduleArray.size() == 1);
  const auto moduleSummary = moduleArray.first().toObject();
  CHECK(moduleSummary.value("moduleId").toString() == "doc-a-module");
  const auto moduleMetadata = moduleSummary.value("metadata").toObject();
  CHECK(moduleMetadata.value("moduleId").toString() == "doc-a-module");
  CHECK(moduleMetadata.value("routeId").toString() == "shared-route");
  CHECK(moduleMetadata.value("generatedBy").toString() == "test");
  CHECK(!moduleMetadata.contains("part"));
  CHECK(!moduleMetadata.contains("order"));
  CHECK(!moduleMetadata.contains("role"));
  const auto parts = moduleSummary.value("parts").toArray();
  REQUIRE(parts.size() == 1);
  CHECK(parts.first().toObject().value("part").toString() == "road");
  CHECK(parts.first().toObject().value("count").toInt() == 1);
  CHECK(currentFingerprint != otherFingerprint);
}

TEST_CASE(
  "McpBridgeServer ir_apply reports pre-mutation payload failures", "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  auto metadataStore = std::map<QString, McpBrushMetadataRecord>{};
  auto moduleStore = std::map<QString, McpModuleRecord>{};
  auto objectRegistry = McpObjectRegistry{};

  const auto descendantCountBeforeInvalidApply = map.worldNode().descendantCount();
  const auto invalidApplyResponse = irApplyForMapResult(
    map,
    "ir_apply",
    QJsonObject{{"ir", QJsonArray{}}},
    history,
    nextOperationIndex,
    metadataStore,
    moduleStore,
    &objectRegistry);
  CHECK(!invalidApplyResponse.ok);
  CHECK(invalidApplyResponse.error.message == "IR field must be an object");
  CHECK(
    invalidApplyResponse.error.details.value("mutatedDocument").toBool(true) == false);
  CHECK(invalidApplyResponse.error.details.value("retrySafe").toBool(false));
  CHECK(
    invalidApplyResponse.error.details.value("recoveryAction").toString()
    == "fix_ir_payload_then_retry");
  CHECK(map.worldNode().descendantCount() == descendantCountBeforeInvalidApply);

  for (const auto& invalidVersion :
       QJsonArray{QJsonValue{"1"}, QJsonValue{0}, QJsonValue{2}, QJsonValue{1.5}})
  {
    const auto versionResponse = irApplyForMapResult(
      map,
      "ir_apply",
      QJsonObject{
        {"ir",
         QJsonObject{
           {"schemaVersion", invalidVersion},
           {"operations",
            QJsonArray{QJsonObject{
              {"type", "box"},
              {"min", QJsonArray{0, 0, 0}},
              {"max", QJsonArray{64, 64, 16}},
            }}},
         }},
      },
      history,
      nextOperationIndex,
      metadataStore,
      moduleStore,
      &objectRegistry);
    CHECK_FALSE(versionResponse.ok);
    CHECK(versionResponse.error.message.contains("schemaVersion"));
    CHECK_FALSE(versionResponse.error.details.value("mutatedDocument").toBool(true));
    CHECK_FALSE(versionResponse.error.details.value("partialMutation").toBool(true));
    CHECK(versionResponse.error.details.value("retrySafe").toBool(false));
    CHECK(map.worldNode().descendantCount() == descendantCountBeforeInvalidApply);
    CHECK(history.empty());
    CHECK(nextOperationIndex == 1);
  }
}

TEST_CASE(
  "McpBridgeServer ir_apply rolls back geometry when entity validation fails",
  "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  auto metadataStore = std::map<QString, McpBrushMetadataRecord>{};
  auto moduleStore = std::map<QString, McpModuleRecord>{};
  auto objectRegistry = McpObjectRegistry{};

  const auto descendantCountBefore = map.worldNode().descendantCount();
  const auto modifiedBefore = map.modified();
  const auto failed = irApplyForMapResult(
    map,
    "ir_apply",
    QJsonObject{
      {"ir",
       QJsonObject{
         {"schemaVersion", 1},
         {"moduleId", "atomic-failure"},
         {"defaultMetadata", QJsonObject{{"moduleId", "atomic-failure"}}},
         {"select", false},
         {"operations",
          QJsonArray{QJsonObject{
            {"type", "box"},
            {"min", QJsonArray{0, 0, 0}},
            {"max", QJsonArray{64, 64, 16}},
            {"metadata", QJsonObject{{"part", "floor"}}},
          }}},
         {"entities",
          QJsonArray{QJsonObject{
            {"classname", "mcp_missing_entity_class"},
            {"origin", QJsonArray{0, 0, 32}},
          }}},
       }},
    },
    history,
    nextOperationIndex,
    metadataStore,
    moduleStore,
    &objectRegistry);

  REQUIRE_FALSE(failed.ok);
  CHECK(failed.error.details.value("failureStage").toString() == "entities");
  CHECK_FALSE(failed.error.details.value("mutatedDocument").toBool(true));
  CHECK_FALSE(failed.error.details.value("partialMutation").toBool(true));
  CHECK(failed.error.details.value("retrySafe").toBool(false));
  CHECK(map.worldNode().descendantCount() == descendantCountBefore);
  CHECK(map.modified() == modifiedBefore);
  CHECK(map.undoCommandName() == nullptr);
  CHECK(history.empty());
  CHECK(nextOperationIndex == 1);
  CHECK(metadataStore.empty());
  CHECK(moduleStore.empty());

  const auto valid = irApplyForMapResult(
    map,
    "ir_apply",
    QJsonObject{
      {"ir",
       QJsonObject{
         {"schemaVersion", 1},
         {"moduleId", "atomic-success-after-failure"},
         {"defaultMetadata", QJsonObject{{"moduleId", "atomic-success-after-failure"}}},
         {"select", false},
         {"operations",
          QJsonArray{QJsonObject{
            {"type", "box"},
            {"min", QJsonArray{0, 0, 0}},
            {"max", QJsonArray{64, 64, 16}},
            {"metadata", QJsonObject{{"part", "floor"}}},
          }}},
       }},
    },
    history,
    nextOperationIndex,
    metadataStore,
    moduleStore,
    &objectRegistry);
  REQUIRE(valid.ok);
  CHECK(valid.result.value("operationIds").toArray().first().toString() == "mcp-op-1");
  CHECK(valid.result.value("parentOperationId").toString() == "mcp-op-2");
  CHECK(std::ranges::any_of(
    metadataStore, [](const auto& entry) { return entry.second.objectId == "mcp:1:1"; }));
}

TEST_CASE(
  "McpBridgeServer ir_apply commits one aggregate undo operation", "[McpBridgeServer]")
{
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
  auto& map = document->map();
  map.entityDefinitionManager().setDefinitions({
    {"test_spawn", {}, "", {}, mdl::PointEntityDefinition{vm::bbox3d{16.0}, {}, {}}},
  });
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  auto metadataStore = std::map<QString, McpBrushMetadataRecord>{};
  auto moduleStore = std::map<QString, McpModuleRecord>{};
  auto objectRegistry = McpObjectRegistry{};
  const auto metadataBefore = metadataStore;
  const auto modulesBefore = moduleStore;
  const auto descendantCountBefore = map.worldNode().descendantCount();

  const auto apply = irApplyForMapResult(
    map,
    "ir_apply",
    QJsonObject{
      {"idsMode", "full"},
      {"ir",
       QJsonObject{
         {"schemaVersion", 1},
         {"name", "MCP: Atomic IR test"},
         {"moduleId", "atomic-ir-module"},
         {"defaultMetadata", QJsonObject{{"moduleId", "atomic-ir-module"}}},
         {"qualityPolicy", QJsonObject{{"intent", "balanced"}}},
         {"select", false},
         {"selectEntities", false},
         {"operations",
          QJsonArray{QJsonObject{
            {"type", "box"},
            {"min", QJsonArray{0, 0, 0}},
            {"max", QJsonArray{64, 64, 16}},
          }}},
         {"entities",
          QJsonArray{QJsonObject{
            {"classname", "test_spawn"},
            {"origin", QJsonArray{32, 32, 32}},
          }}},
       }},
    },
    history,
    nextOperationIndex,
    metadataStore,
    moduleStore,
    &objectRegistry);

  const auto applyError = apply.ok ? std::string{} : apply.error.message.toStdString();
  INFO(applyError);
  REQUIRE(apply.ok);
  CHECK(apply.result.value("schemaVersion").toInt() == 1);
  CHECK(apply.result.value("atomic").toBool());
  CHECK_FALSE(apply.result.value("partialMutation").toBool(true));
  CHECK(apply.result.value("changedObjectCount").toInt() == 2);
  const auto parentOperationId = apply.result.value("parentOperationId").toString();
  const auto childOperationIds = apply.result.value("childOperationIds").toArray();
  REQUIRE_FALSE(parentOperationId.isEmpty());
  REQUIRE(childOperationIds.size() == 2);
  reconcileMcpSessionForMap(
    map, metadataStore, moduleStore, objectRegistry, parentOperationId);
  attachMcpSessionDelta(
    history,
    parentOperationId,
    metadataBefore,
    modulesBefore,
    metadataStore,
    moduleStore);
  const auto moduleBeforeUndo = moduleInspectForMapResult(
    map,
    QJsonObject{{"moduleId", "atomic-ir-module"}},
    metadataStore,
    moduleStore,
    objectRegistry);
  REQUIRE(moduleBeforeUndo.ok);
  CHECK(moduleBeforeUndo.result.value("canonicalObjectCount").toInt() == 2);
  CHECK(moduleBeforeUndo.result.value("moduleRevision").toInt() == 1);
  const auto moduleHash = moduleBeforeUndo.result.value("moduleContentHash").toString();
  REQUIRE(moduleHash.startsWith("sha256:"));
  CHECK(
    moduleBeforeUndo.result.value("activeOperationId").toString() == parentOperationId);
  CHECK(
    moduleBeforeUndo.result.value("qualityPolicy").toObject().value("intent").toString()
    == "balanced");
  CHECK(apply.result.value("operationIds").toArray() == childOperationIds);
  CHECK(history.size() == 3u);
  CHECK(history.back().operationId == parentOperationId);
  CHECK(history.back().undoable);
  CHECK(history.back().childOperationIds.size() == 2);
  CHECK_FALSE(history.at(0).undoable);
  CHECK_FALSE(history.at(1).undoable);
  CHECK(history.at(0).parentOperationId == parentOperationId);
  CHECK(history.at(1).parentOperationId == parentOperationId);
  CHECK(map.worldNode().descendantCount() == descendantCountBefore + 2);
  REQUIRE(map.undoCommandName() != nullptr);
  CHECK(QString::fromStdString(*map.undoCommandName()) == "MCP: Atomic IR test");

  const auto childValidation = operationValidateForMapResult(
    map,
    history,
    QJsonObject{{"operationId", childOperationIds.first().toString()}},
    objectRegistry);
  REQUIRE(childValidation.ok);
  CHECK_FALSE(childValidation.result.value("undoable").toBool(true));
  CHECK(
    childValidation.result.value("parentOperationId").toString() == parentOperationId);
  CHECK(
    childValidation.result.value("recoveryAction").toString()
    == "inspect_or_undo_parent_operation");

  const auto childUndo = historyUndoToOperationForMapResult(
    map,
    history,
    QJsonObject{{"operationId", childOperationIds.first().toString()}},
    &objectRegistry);
  REQUIRE_FALSE(childUndo.ok);
  CHECK(
    childUndo.error.details.value("parentOperationId").toString() == parentOperationId);

  const auto undo =
    historyUndoForMapResult(map, history, &objectRegistry, &metadataStore, &moduleStore);
  REQUIRE(undo.ok);
  CHECK(map.worldNode().descendantCount() == descendantCountBefore);
  CHECK(
    std::ranges::all_of(history, [](const auto& operation) { return operation.undone; }));
  const auto moduleAfterUndo = moduleInspectForMapResult(
    map,
    QJsonObject{{"moduleId", "atomic-ir-module"}},
    metadataStore,
    moduleStore,
    objectRegistry);
  REQUIRE(moduleAfterUndo.ok);
  CHECK(moduleAfterUndo.result.value("canonicalObjectCount").toInt() == 0);

  const auto redo =
    historyRedoForMapResult(map, history, &objectRegistry, &metadataStore, &moduleStore);
  REQUIRE(redo.ok);
  CHECK(map.worldNode().descendantCount() == descendantCountBefore + 2);
  CHECK(std::ranges::none_of(
    history, [](const auto& operation) { return operation.undone; }));
  const auto moduleAfterRedo = moduleInspectForMapResult(
    map,
    QJsonObject{{"moduleId", "atomic-ir-module"}},
    metadataStore,
    moduleStore,
    objectRegistry);
  REQUIRE(moduleAfterRedo.ok);
  CHECK(moduleAfterRedo.result.value("canonicalObjectCount").toInt() == 2);
  CHECK(moduleAfterRedo.result.value("moduleRevision").toInt() == 1);
  CHECK(moduleAfterRedo.result.value("moduleContentHash").toString() == moduleHash);
}

TEST_CASE(
  "McpBridgeServer replace_module uses exact preview guards and one native undo",
  "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  auto metadataStore = std::map<QString, McpBrushMetadataRecord>{};
  auto moduleStore = std::map<QString, McpModuleRecord>{};
  auto objectRegistry = McpObjectRegistry{};

  const auto create = irApplyForMapResult(
    map,
    "ir_apply",
    QJsonObject{
      {"ir",
       QJsonObject{
         {"schemaVersion", 1},
         {"moduleId", "replace-test-module"},
         {"defaultMetadata", QJsonObject{{"moduleId", "replace-test-module"}}},
         {"select", false},
         {"operations",
          QJsonArray{QJsonObject{
            {"type", "box"},
            {"min", QJsonArray{0, 0, 0}},
            {"max", QJsonArray{64, 64, 16}},
          }}},
       }},
    },
    history,
    nextOperationIndex,
    metadataStore,
    moduleStore,
    &objectRegistry);
  REQUIRE(create.ok);
  const auto initialDescendantCount = map.worldNode().descendantCount();
  const auto initialState = moduleInspectForMapResult(
    map,
    QJsonObject{{"moduleId", "replace-test-module"}},
    metadataStore,
    moduleStore,
    objectRegistry);
  REQUIRE(initialState.ok);
  CHECK(initialState.result.value("moduleRevision").toInt() == 1);

  const auto replacementIr = QJsonObject{
    {"schemaVersion", 1},
    {"applyMode", "replace_module"},
    {"moduleId", "replace-test-module"},
    {"defaultMetadata", QJsonObject{{"moduleId", "replace-test-module"}}},
    {"select", false},
    {"operations",
     QJsonArray{
       QJsonObject{
         {"type", "box"},
         {"min", QJsonArray{0, 0, 0}},
         {"max", QJsonArray{96, 64, 16}},
       },
       QJsonObject{
         {"type", "box"},
         {"min", QJsonArray{0, 80, 0}},
         {"max", QJsonArray{96, 96, 32}},
       },
     }},
  };
  const auto preview = irCompilePreviewForMapResult(
    map, QJsonObject{{"ir", replacementIr}}, metadataStore, moduleStore, objectRegistry);
  REQUIRE(preview.ok);
  CHECK(preview.result.value("valid").toBool());
  CHECK(preview.result.value("targetModuleExists").toBool());
  CHECK(preview.result.value("targetModuleRevision").toInt() == 1);
  CHECK(preview.result.value("targetCanonicalObjectCount").toInt() == 1);

  const auto metadataBeforeReplace = metadataStore;
  const auto modulesBeforeReplace = moduleStore;
  const auto replace = irApplyForMapResult(
    map,
    "ir_apply",
    QJsonObject{
      {"ir", replacementIr},
      {"expectedIrHash", preview.result.value("irHash")},
      {"expectedTargetModuleRevision", preview.result.value("targetModuleRevision")},
      {"expectedTargetModuleContentHash",
       preview.result.value("targetModuleContentHash")},
      {"expectedTargetCanonicalObjectIds",
       preview.result.value("targetCanonicalObjectIds")},
    },
    history,
    nextOperationIndex,
    metadataStore,
    moduleStore,
    &objectRegistry);
  const auto replaceError =
    replace.ok ? std::string{} : replace.error.message.toStdString();
  INFO(replaceError);
  REQUIRE(replace.ok);
  CHECK(replace.result.value("applyMode").toString() == "replace_module");
  CHECK(replace.result.value("previousModuleRevision").toInt() == 1);
  CHECK(replace.result.value("moduleRevision").toInt() == 2);
  CHECK(replace.result.value("removedCanonicalObjectCount").toInt() == 1);
  CHECK(replace.result.value("createdCanonicalObjectCount").toInt() == 2);
  CHECK(map.worldNode().descendantCount() == initialDescendantCount + 1);
  const auto replaceOperationId = replace.result.value("parentOperationId").toString();
  attachMcpSessionDelta(
    history,
    replaceOperationId,
    metadataBeforeReplace,
    modulesBeforeReplace,
    metadataStore,
    moduleStore);
  const auto replacementHash = replace.result.value("moduleContentHash").toString();

  const auto undo =
    historyUndoForMapResult(map, history, &objectRegistry, &metadataStore, &moduleStore);
  REQUIRE(undo.ok);
  CHECK(map.worldNode().descendantCount() == initialDescendantCount);
  const auto restored = moduleInspectForMapResult(
    map,
    QJsonObject{{"moduleId", "replace-test-module"}},
    metadataStore,
    moduleStore,
    objectRegistry);
  REQUIRE(restored.ok);
  CHECK(restored.result.value("moduleRevision").toInt() == 1);
  CHECK(
    restored.result.value("moduleContentHash").toString()
    == initialState.result.value("moduleContentHash").toString());
  CHECK(restored.result.value("canonicalObjectCount").toInt() == 1);

  const auto redo =
    historyRedoForMapResult(map, history, &objectRegistry, &metadataStore, &moduleStore);
  REQUIRE(redo.ok);
  CHECK(map.worldNode().descendantCount() == initialDescendantCount + 1);
  const auto redone = moduleInspectForMapResult(
    map,
    QJsonObject{{"moduleId", "replace-test-module"}},
    metadataStore,
    moduleStore,
    objectRegistry);
  REQUIRE(redone.ok);
  CHECK(redone.result.value("moduleRevision").toInt() == 2);
  CHECK(redone.result.value("moduleContentHash").toString() == replacementHash);
  CHECK(redone.result.value("canonicalObjectCount").toInt() == 2);
}

TEST_CASE(
  "McpBridgeServer replace_module rejects stale target and rolls back later stages",
  "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  auto metadataStore = std::map<QString, McpBrushMetadataRecord>{};
  auto moduleStore = std::map<QString, McpModuleRecord>{};
  auto objectRegistry = McpObjectRegistry{};
  const auto create = irApplyForMapResult(
    map,
    "ir_apply",
    QJsonObject{
      {"ir",
       QJsonObject{
         {"moduleId", "guarded-module"},
         {"defaultMetadata", QJsonObject{{"moduleId", "guarded-module"}}},
         {"select", false},
         {"operations",
          QJsonArray{QJsonObject{
            {"type", "box"},
            {"min", QJsonArray{0, 0, 0}},
            {"max", QJsonArray{64, 64, 16}},
          }}},
       }},
    },
    history,
    nextOperationIndex,
    metadataStore,
    moduleStore,
    &objectRegistry);
  REQUIRE(create.ok);

  const auto replacementIr = QJsonObject{
    {"schemaVersion", 1},
    {"applyMode", "replace_module"},
    {"moduleId", "guarded-module"},
    {"defaultMetadata", QJsonObject{{"moduleId", "guarded-module"}}},
    {"select", false},
    {"operations",
     QJsonArray{QJsonObject{
       {"type", "box"},
       {"min", QJsonArray{0, 0, 0}},
       {"max", QJsonArray{96, 96, 16}},
     }}},
  };
  const auto preview = irCompilePreviewForMapResult(
    map, QJsonObject{{"ir", replacementIr}}, metadataStore, moduleStore, objectRegistry);
  REQUIRE(preview.ok);

  const auto transform = transformObjectsForMapResult(
    map,
    "objects_transform",
    QJsonObject{
      {"selector", QJsonObject{{"moduleId", "guarded-module"}}},
      {"operation", "translate"},
      {"delta", QJsonArray{16, 0, 0}},
    },
    history,
    nextOperationIndex,
    objectRegistry,
    &metadataStore,
    &moduleStore);
  REQUIRE(transform.ok);
  const auto historyCountAfterManualEdit = history.size();
  const auto descendantCountAfterManualEdit = map.worldNode().descendantCount();
  const auto staleReplace = irApplyForMapResult(
    map,
    "ir_apply",
    QJsonObject{
      {"ir", replacementIr},
      {"expectedIrHash", preview.result.value("irHash")},
      {"expectedTargetModuleRevision", preview.result.value("targetModuleRevision")},
      {"expectedTargetModuleContentHash",
       preview.result.value("targetModuleContentHash")},
      {"expectedTargetCanonicalObjectIds",
       preview.result.value("targetCanonicalObjectIds")},
    },
    history,
    nextOperationIndex,
    metadataStore,
    moduleStore,
    &objectRegistry);
  REQUIRE_FALSE(staleReplace.ok);
  CHECK_FALSE(staleReplace.error.details.value("mutatedDocument").toBool(true));
  CHECK(staleReplace.error.details.value("retrySafe").toBool(false));
  CHECK(
    staleReplace.error.details.value("failureStage").toString() == "replacement_guard");
  CHECK(history.size() == historyCountAfterManualEdit);
  CHECK(map.worldNode().descendantCount() == descendantCountAfterManualEdit);

  const auto refreshedPreview = irCompilePreviewForMapResult(
    map, QJsonObject{{"ir", replacementIr}}, metadataStore, moduleStore, objectRegistry);
  REQUIRE(refreshedPreview.ok);
  auto invalidEntityIr = replacementIr;
  invalidEntityIr.insert(
    "entities", QJsonArray{QJsonObject{{"classname", "not_a_defined_entity"}}});
  const auto invalidPreview = irCompilePreviewForMapResult(
    map,
    QJsonObject{{"ir", invalidEntityIr}},
    metadataStore,
    moduleStore,
    objectRegistry);
  REQUIRE(invalidPreview.ok);
  const auto historyBeforeInvalidApply = history;
  const auto metadataBeforeInvalidApply = metadataStore;
  const auto modulesBeforeInvalidApply = moduleStore;
  const auto countBeforeInvalidApply = map.worldNode().descendantCount();
  const auto invalidApply = irApplyForMapResult(
    map,
    "ir_apply",
    QJsonObject{
      {"ir", invalidEntityIr},
      {"expectedIrHash", invalidPreview.result.value("irHash")},
      {"expectedTargetModuleRevision",
       invalidPreview.result.value("targetModuleRevision")},
      {"expectedTargetModuleContentHash",
       invalidPreview.result.value("targetModuleContentHash")},
      {"expectedTargetCanonicalObjectIds",
       invalidPreview.result.value("targetCanonicalObjectIds")},
    },
    history,
    nextOperationIndex,
    metadataStore,
    moduleStore,
    &objectRegistry);
  REQUIRE_FALSE(invalidApply.ok);
  CHECK_FALSE(invalidApply.error.details.value("mutatedDocument").toBool(true));
  CHECK_FALSE(invalidApply.error.details.value("partialMutation").toBool(true));
  CHECK(invalidApply.error.details.value("retrySafe").toBool(false));
  CHECK(map.worldNode().descendantCount() == countBeforeInvalidApply);
  CHECK(history.size() == historyBeforeInvalidApply.size());
  CHECK(metadataStore.size() == metadataBeforeInvalidApply.size());
  CHECK(moduleStore.size() == modulesBeforeInvalidApply.size());
}

TEST_CASE(
  "McpBridgeServer selector metadata round trips through IR and operations",
  "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  auto metadataStore = std::map<QString, McpBrushMetadataRecord>{};
  auto moduleStore = std::map<QString, McpModuleRecord>{};
  auto objectRegistry = McpObjectRegistry{};

  const auto applyResponse = irApplyForMapResult(
    map,
    "ir_apply",
    QJsonObject{
      {"idsMode", "full"},
      {"ir",
       QJsonObject{
         {"moduleId", "roundtrip-cottage"},
         {"defaultMetadata",
          QJsonObject{{"moduleId", "roundtrip-cottage"}, {"generatedBy", "test"}}},
         {"operations",
          QJsonArray{
            QJsonObject{
              {"type", "box"},
              {"min", QJsonArray{0, 0, 0}},
              {"max", QJsonArray{64, 16, 96}},
              {"metadata", QJsonObject{{"part", "front_door"}, {"role", "detail"}}},
            },
            QJsonObject{
              {"type", "box"},
              {"min", QJsonArray{80, 0, 32}},
              {"max", QJsonArray{128, 16, 80}},
              {"metadata", QJsonObject{{"part", "front_window"}, {"role", "detail"}}},
            },
            QJsonObject{
              {"type", "box"},
              {"min", QJsonArray{0, 48, 0}},
              {"max", QJsonArray{160, 96, 16}},
              {"metadata", QJsonObject{{"part", "front_walkway"}, {"role", "walkable"}}},
            },
          }},
       }},
    },
    history,
    nextOperationIndex,
    metadataStore,
    moduleStore,
    &objectRegistry);
  const auto applyError =
    applyResponse.ok ? std::string{} : applyResponse.error.message.toStdString();
  INFO(applyError);
  REQUIRE(applyResponse.ok);
  CHECK(applyResponse.result.value("schemaVersion").toInt() == 1);
  CHECK(applyResponse.result.value("warnings").toArray().contains("legacyUnversionedIr"));
  const auto operationIds = applyResponse.result.value("operationIds").toArray();
  REQUIRE(operationIds.size() == 1);
  const auto operationId = operationIds.first().toString();
  CHECK(applyResponse.result.value("changedObjectCount").toInt() == 3);

  const auto partPreview = selectorPreviewForMapResult(
    map,
    QJsonObject{
      {"selector",
       QJsonObject{{"moduleId", "roundtrip-cottage"}, {"part", "front_door"}}},
      {"idsMode", "count"},
    },
    history,
    metadataStore,
    moduleStore,
    objectRegistry);
  REQUIRE(partPreview.ok);
  CHECK(partPreview.result.value("matchedCount").toInt() == 1);
  CHECK(partPreview.result.value("moduleObjectIdCount").toInt() >= 3);

  const auto operationPreview = selectorPreviewForMapResult(
    map,
    QJsonObject{
      {"selector", QJsonObject{{"operationId", operationId}}},
      {"idsMode", "count"},
    },
    history,
    metadataStore,
    moduleStore,
    objectRegistry);
  REQUIRE(operationPreview.ok);
  CHECK(operationPreview.result.value("matchedCount").toInt() == 3);
  CHECK(operationPreview.result.value("operationObjectIdCount").toInt() == 3);

  const auto moduleSummary = moduleListForMapResult(
    map, QJsonObject{}, metadataStore, moduleStore, objectRegistry);
  REQUIRE(moduleSummary.ok);
  const auto moduleArray = moduleSummary.result.value("modules").toArray();
  REQUIRE(moduleArray.size() == 1);
  CHECK(moduleArray.first().toObject().value("liveObjectCount").toInt() == 3);
  CHECK(moduleArray.first().toObject().value("staleObjectCount").toInt() == 0);

  const auto deleteResponse = objectsDeleteBySelectorForMapResult(
    map,
    "objects_delete_by_selector",
    QJsonObject{
      {"selector",
       QJsonObject{{"moduleId", "roundtrip-cottage"}, {"part", "front_walkway"}}},
    },
    history,
    nextOperationIndex,
    metadataStore,
    moduleStore,
    objectRegistry);
  REQUIRE(deleteResponse.ok);
  CHECK(deleteResponse.result.value("mutatedDocument").toBool());
  CHECK(deleteResponse.result.value("matchedCount").toInt() == 1);

  const auto deletedPartPreview = selectorPreviewForMapResult(
    map,
    QJsonObject{
      {"selector",
       QJsonObject{{"moduleId", "roundtrip-cottage"}, {"part", "front_walkway"}}},
      {"idsMode", "count"},
    },
    history,
    metadataStore,
    moduleStore,
    objectRegistry);
  REQUIRE(deletedPartPreview.ok);
  CHECK(deletedPartPreview.result.value("matchedCount").toInt() == 0);

  const auto afterDeleteSummary = moduleListForMapResult(
    map, QJsonObject{{"includeStale", true}}, metadataStore, moduleStore, objectRegistry);
  REQUIRE(afterDeleteSummary.ok);
  const auto afterDeleteModules = afterDeleteSummary.result.value("modules").toArray();
  REQUIRE(afterDeleteModules.size() == 1);
  const auto afterDeleteModule = afterDeleteModules.first().toObject();
  CHECK(afterDeleteModule.value("liveObjectCount").toInt() == 2);
  CHECK(afterDeleteModule.value("staleParts").toArray().size() == 1);

  const auto staleModuleValidation = moduleValidateForMapResult(
    map,
    QJsonObject{{"moduleId", "roundtrip-cottage"}},
    history,
    metadataStore,
    moduleStore,
    objectRegistry);
  REQUIRE(staleModuleValidation.ok);
  CHECK_FALSE(staleModuleValidation.result.value("valid").toBool());
  CHECK(staleModuleValidation.result.value("staleObjectCount").toInt() == 1);
  const auto compactWarnings = staleModuleValidation.result.value("warnings").toArray();
  REQUIRE(compactWarnings.size() == 1);
  CHECK(
    compactWarnings.first().toObject().value("type").toString() == "staleTargetSummary");

  const auto fullStaleModuleValidation = moduleValidateForMapResult(
    map,
    QJsonObject{{"moduleId", "roundtrip-cottage"}, {"detail", "full"}},
    history,
    metadataStore,
    moduleStore,
    objectRegistry);
  REQUIRE(fullStaleModuleValidation.ok);
  const auto fullWarnings = fullStaleModuleValidation.result.value("warnings").toArray();
  REQUIRE(fullWarnings.size() == 1);
  CHECK(fullWarnings.first().toObject().value("stale").toBool());
  CHECK(!fullWarnings.first().toObject().value("objectId").toString().isEmpty());
}

TEST_CASE("McpBridgeServer module_validate reports recovery state", "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  auto metadataStore = std::map<QString, McpBrushMetadataRecord>{};
  auto moduleStore = std::map<QString, McpModuleRecord>{};
  auto objectRegistry = McpObjectRegistry{};

  const auto missingId = moduleValidateForMapResult(
    map, QJsonObject{}, history, metadataStore, moduleStore, objectRegistry);
  CHECK(!missingId.ok);
  CHECK_FALSE(missingId.error.details.value("mutatedDocument").toBool(true));
  CHECK(missingId.error.details.value("retrySafe").toBool(false));
  CHECK(
    missingId.error.details.value("recoveryAction").toString()
    == "provide_module_id_then_retry");

  const auto create = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"defaultMetadata", QJsonObject{{"moduleId", "validate-module"}}},
      {"operations",
       QJsonArray{QJsonObject{
         {"type", "box"},
         {"min", QJsonArray{0, 0, 0}},
         {"max", QJsonArray{64, 64, 16}},
       }}},
    },
    history,
    nextOperationIndex,
    &metadataStore,
    &moduleStore,
    &objectRegistry);
  REQUIRE(create.ok);

  const auto valid = moduleValidateForMapResult(
    map,
    QJsonObject{{"moduleId", "validate-module"}},
    history,
    metadataStore,
    moduleStore,
    objectRegistry);
  REQUIRE(valid.ok);
  CHECK_FALSE(valid.result.value("mutatedDocument").toBool(true));
  CHECK(valid.result.value("valid").toBool());

  const auto invalidContinuitySelector = moduleValidateForMapResult(
    map,
    QJsonObject{
      {"moduleId", "validate-module"},
      {"checkRouteContinuity", true},
      {"continuitySelector",
       QJsonObject{
         {"min", QJsonArray{0, 0, 0}},
       }},
    },
    history,
    metadataStore,
    moduleStore,
    objectRegistry);
  CHECK(!invalidContinuitySelector.ok);
  CHECK_FALSE(
    invalidContinuitySelector.error.details.value("mutatedDocument").toBool(true));
  CHECK(invalidContinuitySelector.error.details.value("retrySafe").toBool(false));
  CHECK(
    invalidContinuitySelector.error.details.value("recoveryAction").toString()
    == "fix_continuity_selector_then_retry");
  CHECK(
    invalidContinuitySelector.error.details.value("moduleId").toString()
    == "validate-module");
}

TEST_CASE("McpBridgeServer module_inspect reports recovery state", "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  auto metadataStore = std::map<QString, McpBrushMetadataRecord>{};
  auto moduleStore = std::map<QString, McpModuleRecord>{};
  auto objectRegistry = McpObjectRegistry{};

  const auto missingId = moduleInspectForMapResult(
    map, QJsonObject{}, metadataStore, moduleStore, objectRegistry);
  CHECK(!missingId.ok);
  CHECK_FALSE(missingId.error.details.value("mutatedDocument").toBool(true));
  CHECK(missingId.error.details.value("retrySafe").toBool(false));
  CHECK(
    missingId.error.details.value("recoveryAction").toString()
    == "provide_module_id_then_retry");

  const auto create = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"defaultMetadata", QJsonObject{{"moduleId", "inspect-module"}}},
      {"operations",
       QJsonArray{QJsonObject{
         {"type", "box"},
         {"min", QJsonArray{0, 0, 0}},
         {"max", QJsonArray{64, 64, 16}},
       }}},
    },
    history,
    nextOperationIndex,
    &metadataStore,
    &moduleStore,
    &objectRegistry);
  REQUIRE(create.ok);

  const auto inspect = moduleInspectForMapResult(
    map,
    QJsonObject{{"moduleId", "inspect-module"}},
    metadataStore,
    moduleStore,
    objectRegistry);
  REQUIRE(inspect.ok);
  CHECK_FALSE(inspect.result.value("mutatedDocument").toBool(true));
  CHECK(inspect.result.value("moduleId").toString() == "inspect-module");
  CHECK(inspect.result.value("liveObjectCount").toInt() == 1);
  CHECK(inspect.result.value("objectIdCount").toInt() == 1);
  CHECK(inspect.result.value("objectIdSample").toArray().size() == 1);

  const auto countInspect = moduleInspectForMapResult(
    map,
    QJsonObject{{"moduleId", "inspect-module"}, {"idsMode", "count"}},
    metadataStore,
    moduleStore,
    objectRegistry);
  REQUIRE(countInspect.ok);
  CHECK(countInspect.result.value("objectIdCount").toInt() == 1);
  CHECK(countInspect.result.value("objectIdSample").isUndefined());
  CHECK(countInspect.result.value("objectIds").isUndefined());

  const auto noneInspect = moduleInspectForMapResult(
    map,
    QJsonObject{{"moduleId", "inspect-module"}, {"idsMode", "none"}},
    metadataStore,
    moduleStore,
    objectRegistry);
  REQUIRE(noneInspect.ok);
  CHECK(noneInspect.result.value("objectIdCount").toInt() == 1);
  CHECK(noneInspect.result.value("objectIdSample").isUndefined());
  CHECK(noneInspect.result.value("objectIds").isUndefined());

  const auto fullInspect = moduleInspectForMapResult(
    map,
    QJsonObject{{"moduleId", "inspect-module"}, {"idsMode", "full"}},
    metadataStore,
    moduleStore,
    objectRegistry);
  REQUIRE(fullInspect.ok);
  CHECK(fullInspect.result.value("objectIdCount").toInt() == 1);
  CHECK(fullInspect.result.value("objectIds").toArray().size() == 1);
  CHECK(fullInspect.result.value("objectIdSample").isUndefined());

  const auto createOperationId = create.result.value("operationId").toString();
  reconcileMcpSessionForMap(
    map, metadataStore, moduleStore, objectRegistry, createOperationId);
  const auto initialState = moduleInspectForMapResult(
    map,
    QJsonObject{{"moduleId", "inspect-module"}},
    metadataStore,
    moduleStore,
    objectRegistry);
  REQUIRE(initialState.ok);
  CHECK(initialState.result.value("moduleRevision").toInt() == 1);
  const auto initialHash = initialState.result.value("moduleContentHash").toString();
  REQUIRE(initialHash.startsWith("sha256:"));

  const auto metadataBeforeTransform = metadataStore;
  const auto modulesBeforeTransform = moduleStore;
  const auto transform = transformObjectsForMapResult(
    map,
    "objects_transform",
    QJsonObject{
      {"selector", QJsonObject{{"moduleId", "inspect-module"}}},
      {"operation", "translate"},
      {"delta", QJsonArray{16, 0, 0}},
    },
    history,
    nextOperationIndex,
    objectRegistry,
    &metadataStore,
    &moduleStore);
  REQUIRE(transform.ok);
  const auto transformOperationId = transform.result.value("operationId").toString();
  reconcileMcpSessionForMap(
    map, metadataStore, moduleStore, objectRegistry, transformOperationId);
  attachMcpSessionDelta(
    history,
    transformOperationId,
    metadataBeforeTransform,
    modulesBeforeTransform,
    metadataStore,
    moduleStore);
  const auto transformedState = moduleInspectForMapResult(
    map,
    QJsonObject{{"moduleId", "inspect-module"}},
    metadataStore,
    moduleStore,
    objectRegistry);
  REQUIRE(transformedState.ok);
  CHECK(transformedState.result.value("moduleRevision").toInt() == 2);
  const auto transformedHash =
    transformedState.result.value("moduleContentHash").toString();
  CHECK(transformedHash != initialHash);
  CHECK(
    transformedState.result.value("activeOperationId").toString()
    == transformOperationId);

  const auto undo =
    historyUndoForMapResult(map, history, &objectRegistry, &metadataStore, &moduleStore);
  REQUIRE(undo.ok);
  const auto restoredState = moduleInspectForMapResult(
    map,
    QJsonObject{{"moduleId", "inspect-module"}},
    metadataStore,
    moduleStore,
    objectRegistry);
  REQUIRE(restoredState.ok);
  CHECK(restoredState.result.value("moduleRevision").toInt() == 1);
  CHECK(restoredState.result.value("moduleContentHash").toString() == initialHash);
  CHECK(restoredState.result.value("canonicalObjectCount").toInt() == 1);

  const auto redo =
    historyRedoForMapResult(map, history, &objectRegistry, &metadataStore, &moduleStore);
  REQUIRE(redo.ok);
  const auto redoneState = moduleInspectForMapResult(
    map,
    QJsonObject{{"moduleId", "inspect-module"}},
    metadataStore,
    moduleStore,
    objectRegistry);
  REQUIRE(redoneState.ok);
  CHECK(redoneState.result.value("moduleRevision").toInt() == 2);
  CHECK(redoneState.result.value("moduleContentHash").toString() == transformedHash);
  CHECK(redoneState.result.value("canonicalObjectCount").toInt() == 1);
}

TEST_CASE(
  "McpBridgeServer module statistics canonicalize stable and legacy aliases",
  "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  auto metadataStore = std::map<QString, McpBrushMetadataRecord>{};
  auto moduleStore = std::map<QString, McpModuleRecord>{};
  auto objectRegistry = McpObjectRegistry{};
  auto operations = QJsonArray{};
  for (auto i = 0; i < 54; ++i)
  {
    const auto x = (i % 9) * 32;
    const auto y = (i / 9) * 32;
    operations.push_back(QJsonObject{
      {"type", "box"},
      {"min", QJsonArray{x, y, 0}},
      {"max", QJsonArray{x + 16, y + 16, 16}},
    });
  }

  const auto create = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"select", false},
      {"detail", "ids"},
      {"defaultMetadata", QJsonObject{{"moduleId", "alias-module"}}},
      {"operations", operations},
    },
    history,
    nextOperationIndex,
    &metadataStore,
    &moduleStore,
    &objectRegistry);
  REQUIRE(create.ok);
  REQUIRE(metadataStore.size() == 54u);
  REQUIRE(moduleStore.size() == 1u);

  auto aliases = QStringList{};
  for (const auto& [key, record] : metadataStore)
  {
    Q_UNUSED(key);
    aliases.push_back(record.objectId);
    const auto resolved = objectRegistry.resolveExternalId(map, record.objectId);
    REQUIRE(resolved.ok);
    aliases.push_back(resolved.legacyPathId);
  }
  REQUIRE(aliases.size() == 108);
  moduleStore.begin()->second.objectIds = aliases;

  const auto aliased = moduleInspectForMapResult(
    map,
    QJsonObject{{"moduleId", "alias-module"}, {"idsMode", "count"}},
    metadataStore,
    moduleStore,
    objectRegistry);
  REQUIRE(aliased.ok);
  CHECK(aliased.result.value("storedReferenceCount").toInt() == 108);
  CHECK(aliased.result.value("canonicalObjectCount").toInt() == 54);
  CHECK(aliased.result.value("duplicateAliasCount").toInt() == 54);
  CHECK(aliased.result.value("staleReferenceCount").toInt() == 0);

  reconcileMcpSessionForMap(
    map,
    metadataStore,
    moduleStore,
    objectRegistry,
    create.result.value("operationId").toString());
  const auto reconciled = moduleInspectForMapResult(
    map,
    QJsonObject{{"moduleId", "alias-module"}, {"idsMode", "count"}},
    metadataStore,
    moduleStore,
    objectRegistry);
  REQUIRE(reconciled.ok);
  CHECK(reconciled.result.value("storedReferenceCount").toInt() == 54);
  CHECK(reconciled.result.value("canonicalObjectCount").toInt() == 54);
  CHECK(reconciled.result.value("duplicateAliasCount").toInt() == 0);
  CHECK(reconciled.result.value("moduleRevision").toInt() == 1);
  CHECK(reconciled.result.value("moduleContentHash").toString().startsWith("sha256:"));
}

TEST_CASE(
  "McpBridgeServer selector selection reports recovery state", "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  auto metadataStore = std::map<QString, McpBrushMetadataRecord>{};
  auto moduleStore = std::map<QString, McpModuleRecord>{};
  auto objectRegistry = McpObjectRegistry{};

  const auto missingModuleId = moduleSelectResult(
    appController, QJsonObject{}, metadataStore, moduleStore, objectRegistry);
  CHECK(!missingModuleId.ok);
  CHECK_FALSE(missingModuleId.error.details.value("mutatedDocument").toBool(true));
  CHECK(missingModuleId.error.details.value("retrySafe").toBool(false));
  CHECK(
    missingModuleId.error.details.value("recoveryAction").toString()
    == "provide_module_id_then_retry");

  const auto invalidSelector = objectsSelectBySelectorForMapResult(
    map,
    QJsonObject{{"operationIds", QJsonArray{QJsonValue{42}}}},
    history,
    metadataStore,
    moduleStore,
    objectRegistry);
  CHECK(!invalidSelector.ok);
  CHECK_FALSE(invalidSelector.error.details.value("mutatedDocument").toBool(true));
  CHECK(invalidSelector.error.details.value("retrySafe").toBool(false));
  CHECK(
    invalidSelector.error.details.value("recoveryAction").toString()
    == "fix_selector_then_retry");

  const auto create = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"defaultMetadata", QJsonObject{{"moduleId", "select-module"}}},
      {"operations",
       QJsonArray{QJsonObject{
         {"type", "box"},
         {"min", QJsonArray{0, 0, 0}},
         {"max", QJsonArray{64, 64, 16}},
       }}},
    },
    history,
    nextOperationIndex,
    &metadataStore,
    &moduleStore,
    &objectRegistry);
  REQUIRE(create.ok);

  const auto selected = objectsSelectBySelectorForMapResult(
    map,
    QJsonObject{{"moduleId", "select-module"}},
    history,
    metadataStore,
    moduleStore,
    objectRegistry);
  REQUIRE(selected.ok);
  CHECK_FALSE(selected.result.value("mutatedDocument").toBool(true));
  CHECK(selected.result.value("selectedCount").toInt() == 1);
}

TEST_CASE("McpBridgeServer selector_preview reports recovery state", "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  auto metadataStore = std::map<QString, McpBrushMetadataRecord>{};
  auto moduleStore = std::map<QString, McpModuleRecord>{};
  auto objectRegistry = McpObjectRegistry{};

  const auto invalidSelector = selectorPreviewForMapResult(
    map,
    QJsonObject{{"operationIds", QJsonArray{QJsonValue{42}}}},
    history,
    metadataStore,
    moduleStore,
    objectRegistry);
  CHECK(!invalidSelector.ok);
  CHECK_FALSE(invalidSelector.error.details.value("mutatedDocument").toBool(true));
  CHECK(invalidSelector.error.details.value("retrySafe").toBool(false));
  CHECK(
    invalidSelector.error.details.value("recoveryAction").toString()
    == "fix_selector_then_retry");

  const auto create = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"defaultMetadata", QJsonObject{{"moduleId", "preview-module"}}},
      {"operations",
       QJsonArray{QJsonObject{
         {"type", "box"},
         {"min", QJsonArray{0, 0, 0}},
         {"max", QJsonArray{64, 64, 16}},
       }}},
    },
    history,
    nextOperationIndex,
    &metadataStore,
    &moduleStore,
    &objectRegistry);
  REQUIRE(create.ok);

  const auto preview = selectorPreviewForMapResult(
    map,
    QJsonObject{{"moduleId", "preview-module"}},
    history,
    metadataStore,
    moduleStore,
    objectRegistry);
  REQUIRE(preview.ok);
  CHECK_FALSE(preview.result.value("mutatedDocument").toBool(true));
  CHECK(preview.result.value("matchedCount").toInt() == 1);

  auto cornerEntity = mdl::Entity{{
    {"classname", "path_corner"},
    {"targetname", "selector-corner-01"},
    {"target", "selector-corner-02"},
  }};
  cornerEntity.setOrigin(vm::vec3d{128.0, 0.0, 0.0});
  auto* corner = new mdl::EntityNode{std::move(cornerEntity)};
  mdl::addNodes(map, {{&mdl::parentForNodes(map), {corner}}});

  const auto entityPreview = selectorPreviewForMapResult(
    map,
    QJsonObject{
      {"classname", "path_corner"},
      {"detail", "full"},
      {"idsMode", "sample"},
    },
    history,
    metadataStore,
    moduleStore,
    objectRegistry);
  REQUIRE(entityPreview.ok);
  CHECK(entityPreview.result.value("matchedCount").toInt() == 1);
  const auto sample = entityPreview.result.value("sample").toArray();
  REQUIRE(sample.size() == 1);
  const auto entitySample = sample.first().toObject();
  CHECK(entitySample.value("classname").toString() == "path_corner");
  const auto properties = entitySample.value("properties").toObject();
  CHECK(properties.value("targetname").toString() == "selector-corner-01");
  CHECK(properties.value("target").toString() == "selector-corner-02");
}

TEST_CASE(
  "McpBridgeServer render_review_selector reports recovery state", "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  auto metadataStore = std::map<QString, McpBrushMetadataRecord>{};
  auto moduleStore = std::map<QString, McpModuleRecord>{};
  auto objectRegistry = McpObjectRegistry{};

  const auto invalidSelector = renderReviewSelectorForMapResult(
    map,
    QJsonObject{{"operationIds", QJsonArray{QJsonValue{42}}}},
    history,
    metadataStore,
    moduleStore,
    objectRegistry);
  CHECK(!invalidSelector.ok);
  CHECK_FALSE(invalidSelector.error.details.value("mutatedDocument").toBool(true));
  CHECK(invalidSelector.error.details.value("retrySafe").toBool(false));
  CHECK(
    invalidSelector.error.details.value("recoveryAction").toString()
    == "fix_selector_then_retry");

  const auto create = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"defaultMetadata", QJsonObject{{"moduleId", "review-selector-module"}}},
      {"operations",
       QJsonArray{QJsonObject{
         {"type", "box"},
         {"min", QJsonArray{0, 0, 0}},
         {"max", QJsonArray{64, 64, 16}},
       }}},
    },
    history,
    nextOperationIndex,
    &metadataStore,
    &moduleStore,
    &objectRegistry);
  REQUIRE(create.ok);

  const auto reviewed = renderReviewSelectorForMapResult(
    map,
    QJsonObject{{"moduleId", "review-selector-module"}},
    history,
    metadataStore,
    moduleStore,
    objectRegistry);
  REQUIRE(reviewed.ok);
  CHECK(reviewed.result.value("selectorMatchedCount").toInt() == 1);
  CHECK(reviewed.result.value("matchedBeforeLimit").toInt() == 1);
  CHECK_FALSE(reviewed.result.value("limitApplied").toBool(true));
  CHECK(reviewed.result.value("moduleObjectIdCount").toInt() >= 1);
  CHECK(reviewed.result.value("renderReadable").toBool());
  CHECK(reviewed.result.value("qualityValid").toBool());
}

TEST_CASE(
  "McpBridgeServer module_render_review reports recovery state", "[McpBridgeServer]")
{
  auto appControllerFixture = AppControllerFixture{};
  auto& appController = appControllerFixture.appController();
  auto history = std::vector<McpOperationRecord>{};
  auto metadataStore = std::map<QString, McpBrushMetadataRecord>{};
  auto moduleStore = std::map<QString, McpModuleRecord>{};
  auto objectRegistry = McpObjectRegistry{};

  const auto missingModuleId = moduleRenderReviewResult(
    appController, QJsonObject{}, history, metadataStore, moduleStore, objectRegistry);
  CHECK(!missingModuleId.ok);
  CHECK_FALSE(missingModuleId.error.details.value("mutatedDocument").toBool(true));
  CHECK(missingModuleId.error.details.value("retrySafe").toBool(false));
  CHECK(
    missingModuleId.error.details.value("recoveryAction").toString()
    == "provide_module_id_then_retry");
}

TEST_CASE(
  "McpBridgeServer missing materials warn by default and reject in strict mode",
  "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  const auto descendantCountBefore = map.worldNode().descendantCount();

  const auto compatible = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"material", "not_loaded/material"},
      {"select", false},
      {"operations",
       QJsonArray{QJsonObject{
         {"type", "box"},
         {"min", QJsonArray{0, 0, 0}},
         {"max", QJsonArray{64, 64, 16}},
       }}},
    },
    history,
    nextOperationIndex);
  REQUIRE(compatible.ok);
  CHECK(compatible.result.value("mutatedDocument").toBool());
  const auto compatiblePreflight =
    compatible.result.value("materialPreflight").toObject();
  CHECK(compatiblePreflight.value("missingMaterialCount").toInt() == 1);
  const auto defaultMaterial = compatiblePreflight.value("defaultMaterial").toObject();
  CHECK(defaultMaterial.value("requestedMaterial").toString() == "not_loaded/material");
  CHECK(defaultMaterial.value("effectiveMaterial").toString() == "not_loaded/material");
  CHECK(defaultMaterial.value("source").toString() == "request");
  CHECK_FALSE(defaultMaterial.value("currentlyLoaded").toBool(true));
  CHECK_FALSE(defaultMaterial.value("placeholderMaterial").toBool(true));
  CHECK(compatiblePreflight.value("missingMaterialSample")
          .toArray()
          .contains("not_loaded/material"));
  CHECK(map.worldNode().descendantCount() == descendantCountBefore + 1);

  const auto countBeforeStrict = map.worldNode().descendantCount();
  const auto historyBeforeStrict = history.size();
  const auto strict = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"material", "still_not_loaded/material"},
      {"requireMaterialAvailable", true},
      {"operations",
       QJsonArray{QJsonObject{
         {"type", "box"},
         {"min", QJsonArray{80, 0, 0}},
         {"max", QJsonArray{144, 64, 16}},
       }}},
    },
    history,
    nextOperationIndex);
  REQUIRE_FALSE(strict.ok);
  CHECK_FALSE(strict.error.details.value("mutatedDocument").toBool(true));
  CHECK(strict.error.details.value("retrySafe").toBool(false));
  CHECK(
    strict.error.details.value("recoveryAction").toString()
    == "load_materials_or_disable_strict_material_check");
  CHECK(map.worldNode().descendantCount() == countBeforeStrict);
  CHECK(history.size() == historyBeforeStrict);

  const auto strictBrush = createBrushForMapResult(
    map,
    "brush_create_box",
    QJsonObject{
      {"min", QJsonArray{160, 0, 0}},
      {"max", QJsonArray{224, 64, 16}},
      {"material", "missing/brush_material"},
      {"requireMaterialAvailable", true},
    },
    history,
    nextOperationIndex);
  REQUIRE_FALSE(strictBrush.ok);
  CHECK(map.worldNode().descendantCount() == countBeforeStrict);
  CHECK(history.size() == historyBeforeStrict);

  auto metadataStore = std::map<QString, McpBrushMetadataRecord>{};
  auto moduleStore = std::map<QString, McpModuleRecord>{};
  auto objectRegistry = McpObjectRegistry{};
  const auto strictIr = irApplyForMapResult(
    map,
    "ir_apply",
    QJsonObject{
      {"ir",
       QJsonObject{
         {"moduleId", "strict-material-ir"},
         {"defaultMetadata", QJsonObject{{"moduleId", "strict-material-ir"}}},
         {"material", "missing/ir_material"},
         {"requireMaterialAvailable", true},
         {"operations",
          QJsonArray{QJsonObject{
            {"type", "box"},
            {"min", QJsonArray{240, 0, 0}},
            {"max", QJsonArray{304, 64, 16}},
          }}},
       }},
    },
    history,
    nextOperationIndex,
    metadataStore,
    moduleStore,
    &objectRegistry);
  REQUIRE_FALSE(strictIr.ok);
  CHECK_FALSE(strictIr.error.details.value("mutatedDocument").toBool(true));
  CHECK(map.worldNode().descendantCount() == countBeforeStrict);
  CHECK(history.size() == historyBeforeStrict);
  CHECK(metadataStore.empty());
  CHECK(moduleStore.empty());
}

TEST_CASE("McpBridgeServer object transform summaries", "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  auto objectRegistry = McpObjectRegistry{};

  auto server = McpBridgeServer{
    [&](const QString& toolName, const QJsonObject& params) {
      if (toolName == "blockout_create_batch")
      {
        return blockoutCreateBatchForMapResult(
          map, toolName, params, history, nextOperationIndex);
      }
      if (toolName == "objects_transform")
      {
        return transformObjectsForMapResult(
          map, toolName, params, history, nextOperationIndex, objectRegistry);
      }
      return McpBridgeToolResult::failure(
        mcp::McpErrorCode::ToolNotFound, QString{"Unexpected tool: %1"}.arg(toolName));
    },
    [&map]() -> mdl::Map* { return &map; }};
  REQUIRE(server.start(
    mcp::McpBridgeConfig{"test-pipe-transform-summary", mcp::McpMode::Edit}));

  const auto createResponse = server.dispatchRequest(mcp::McpBridgeRequest{
    "1",
    "blockout_create_batch",
    QJsonObject{
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{0, 0, 0}},
           {"max", QJsonArray{64, 64, 16}},
         },
       }},
      {"detail", "ids"},
    },
    mcp::McpMode::Edit});
  REQUIRE(createResponse.ok);
  const auto objectIds = createResponse.result.value("changedObjectIds").toArray();
  REQUIRE(objectIds.size() == 1);

  const auto transformResponse = server.dispatchRequest(mcp::McpBridgeRequest{
    "2",
    "objects_transform",
    QJsonObject{
      {"objectIds", objectIds},
      {"operation", "translate"},
      {"delta", QJsonArray{16, 0, 0}},
    },
    mcp::McpMode::Edit});
  const auto transformError = transformResponse.ok || !transformResponse.error
                                ? QString{}
                                : transformResponse.error->message;
  INFO(transformError.toStdString());
  REQUIRE(transformResponse.ok);
  CHECK(
    transformResponse.result.value("undoOperationId").toString()
    == transformResponse.result.value("operationId").toString());
  CHECK(transformResponse.result.value("undoable").toBool());
  CHECK(transformResponse.result.value("auditOperationIds")
          .toArray()
          .contains(transformResponse.result.value("operationId")));
  const auto completionState =
    transformResponse.result.value("completionState").toObject();
  CHECK(completionState.value("documentModified").toBool());
  CHECK(completionState.value("saveRequired").toBool());
  CHECK(completionState.value("visualReview").toString() == "not_run");
  CHECK(completionState.value("bspCompile").toString() == "not_run");
  CHECK(transformResponse.result.value("changedObjectCount").toInt() == 1);
  CHECK(transformResponse.result.value("selectedCount").toInt() == 1);
  CHECK(transformResponse.result.value("validation").toObject().value("valid").toBool());
  const auto bounds = transformResponse.result.value("bounds").toObject();
  CHECK(bounds.value("min").toArray().at(0).toDouble() == 16.0);
  CHECK(bounds.value("max").toArray().at(0).toDouble() == 80.0);

  const auto transformByOperationResponse = server.dispatchRequest(mcp::McpBridgeRequest{
    "3",
    "objects_transform",
    QJsonObject{
      {"operationId", createResponse.result.value("operationId").toString()},
      {"operation", "translate"},
      {"delta", QJsonArray{0, 16, 0}},
    },
    mcp::McpMode::Edit});
  const auto transformByOperationError =
    transformByOperationResponse.ok || !transformByOperationResponse.error
      ? QString{}
      : transformByOperationResponse.error->message;
  INFO(transformByOperationError.toStdString());
  REQUIRE(transformByOperationResponse.ok);
  CHECK(transformByOperationResponse.result.value("changedObjectCount").toInt() == 1);
  CHECK(transformByOperationResponse.result.value("sourceOperationCount").toInt() == 1);

  const auto transformSelectionResponse = server.dispatchRequest(mcp::McpBridgeRequest{
    "4",
    "objects_transform",
    QJsonObject{
      {"operation", "translate"},
      {"delta", QJsonArray{0, 0, 16}},
    },
    mcp::McpMode::Edit});
  const auto transformSelectionError =
    transformSelectionResponse.ok || !transformSelectionResponse.error
      ? QString{}
      : transformSelectionResponse.error->message;
  INFO(transformSelectionError.toStdString());
  REQUIRE(transformSelectionResponse.ok);
  CHECK(
    transformSelectionResponse.result.value("targetSource").toString() == "selection");
  CHECK(transformSelectionResponse.result.value("changedObjectCount").toInt() == 1);
  CHECK(transformSelectionResponse.result.value("selectedCount").toInt() == 1);
  const auto selectionBounds =
    transformSelectionResponse.result.value("bounds").toObject();
  CHECK(selectionBounds.value("min").toArray().at(2).toDouble() == 16.0);
}

TEST_CASE(
  "McpBridgeServer transforms selector targets without long id lists",
  "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  auto objectRegistry = McpObjectRegistry{};
  auto metadataStore = std::map<QString, McpBrushMetadataRecord>{};
  auto moduleStore = std::map<QString, McpModuleRecord>{};

  const auto createResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"name", "MCP: Selector transform arc ramp"},
      {"detail", "ids"},
      {"select", false},
      {"defaultMetadata",
       QJsonObject{
         {"moduleId", "selector-transform-route"},
         {"routeId", "selector-transform-route"},
         {"part", "road"},
         {"role", "walkable"},
       }},
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "arc_ramp"},
           {"center", QJsonArray{0, 0, 0}},
           {"radius", 256},
           {"width", 96},
           {"startAngle", 0},
           {"turnDegrees", 180},
           {"rise", 128},
           {"segments", 32},
           {"thickness", 16},
           {"metadata", QJsonObject{{"order", 1}}},
         },
       }},
    },
    history,
    nextOperationIndex,
    &metadataStore,
    &moduleStore,
    &objectRegistry);
  const auto createError =
    createResponse.ok ? std::string{} : createResponse.error.message.toStdString();
  INFO(createError);
  REQUIRE(createResponse.ok);
  CHECK(createResponse.result.value("changedObjectCount").toInt() == 32);
  CHECK(createResponse.result.value("metadataCount").toInt() == 32);

  const auto previewResponse = selectorPreviewForMapResult(
    map,
    QJsonObject{
      {"selector", QJsonObject{{"moduleId", "selector-transform-route"}}},
      {"idsMode", "count"},
    },
    history,
    metadataStore,
    moduleStore,
    objectRegistry);
  REQUIRE(previewResponse.ok);
  CHECK(previewResponse.result.value("matchedCount").toInt() == 32);
  CHECK(previewResponse.result.value("objectIdCount").toInt() == 32);
  CHECK(previewResponse.result.value("objectIds").isUndefined());
  CHECK(previewResponse.result.value("objectIdSample").isUndefined());
  CHECK(previewResponse.result.value("sample").isUndefined());

  const auto transformResponse = transformObjectsForMapResult(
    map,
    "objects_transform",
    QJsonObject{
      {"selector", QJsonObject{{"moduleId", "selector-transform-route"}}},
      {"operation", "scale"},
      {"scale", QJsonArray{1.12, 1.0, 1.0}},
      {"idsMode", "count"},
    },
    history,
    nextOperationIndex,
    objectRegistry,
    &metadataStore,
    &moduleStore);
  const auto transformError =
    transformResponse.ok ? std::string{} : transformResponse.error.message.toStdString();
  INFO(transformError);
  REQUIRE(transformResponse.ok);
  CHECK(transformResponse.result.value("targetSource").toString() == "selector");
  CHECK(transformResponse.result.value("targetCount").toInt() == 32);
  CHECK(transformResponse.result.value("resolvedObjectCount").toInt() == 32);
  CHECK(transformResponse.result.value("selectorMatchedCount").toInt() == 32);
  CHECK(transformResponse.result.value("matchedBeforeLimit").toInt() == 32);
  CHECK(transformResponse.result.value("staleExcluded").toInt() == 0);
  CHECK(transformResponse.result.value("changedObjectCount").toInt() == 32);
  CHECK(transformResponse.result.value("objectIdCount").toInt() == 32);
  CHECK(transformResponse.result.value("changedObjectIds").isUndefined());
  CHECK(transformResponse.result.value("objectIds").isUndefined());
  CHECK(!transformResponse.result.value("operationId").toString().isEmpty());
  CHECK(history.back().toolName == "objects_transform");

  const auto beforeBounds = transformResponse.result.value("beforeBounds").toObject();
  const auto afterBounds = transformResponse.result.value("afterBounds").toObject();
  const auto beforeMin = beforeBounds.value("min").toArray();
  const auto beforeMax = beforeBounds.value("max").toArray();
  const auto afterMin = afterBounds.value("min").toArray();
  const auto afterMax = afterBounds.value("max").toArray();
  const auto beforeWidth = beforeMax.at(0).toDouble() - beforeMin.at(0).toDouble();
  const auto afterWidth = afterMax.at(0).toDouble() - afterMin.at(0).toDouble();
  const auto beforeDepth = beforeMax.at(1).toDouble() - beforeMin.at(1).toDouble();
  const auto afterDepth = afterMax.at(1).toDouble() - afterMin.at(1).toDouble();
  CHECK(afterWidth > beforeWidth * 1.10);
  CHECK(afterDepth == beforeDepth);

  const auto continuityResponse = geometryAnalyzeRouteContinuityForMapResult(
    map,
    QJsonObject{
      {"routeId", "selector-transform-route"},
      {"orderBy", "metadataOrder"},
      {"continuityMode", "stepped"},
      {"horizontalTolerance", 128},
      {"maxStepHeight", 256},
    },
    history,
    &objectRegistry,
    &metadataStore,
    &moduleStore);
  REQUIRE(continuityResponse.ok);
  CHECK(continuityResponse.result.value("selectorMatchedCount").toInt() == 32);
  CHECK(continuityResponse.result.value("orderBy").toString() == "metadataOrder");

  const auto missResponse = transformObjectsForMapResult(
    map,
    "objects_transform",
    QJsonObject{
      {"selector", QJsonObject{{"moduleId", "missing-transform-route"}}},
      {"operation", "scale"},
      {"scale", QJsonArray{1.1, 1.0, 1.0}},
    },
    history,
    nextOperationIndex,
    objectRegistry,
    &metadataStore,
    &moduleStore);
  CHECK_FALSE(missResponse.ok);
  CHECK(missResponse.error.message.contains("selector resolved to no live"));
  CHECK(
    missResponse.error.details.value("selector").toObject().value("moduleId").toString()
    == "missing-transform-route");
  CHECK(missResponse.error.details.value("selectorMatchedCount").toInt() == 0);
  CHECK(missResponse.error.details.value("mutatedDocument").toBool(true) == false);
  CHECK(missResponse.error.details.value("retrySafe").toBool(false));
  CHECK(
    missResponse.error.details.value("recoveryAction").toString()
    == "preview_selector_or_refresh_status");
  CHECK(history.back().toolName == "objects_transform");

  const auto invalidOperationResponse = transformObjectsForMapResult(
    map,
    "objects_transform",
    QJsonObject{
      {"selector", QJsonObject{{"moduleId", "selector-transform-route"}}},
      {"operation", "skew"},
    },
    history,
    nextOperationIndex,
    objectRegistry,
    &metadataStore,
    &moduleStore);
  CHECK_FALSE(invalidOperationResponse.ok);
  CHECK(
    invalidOperationResponse.error.details.value("mutatedDocument").toBool(true)
    == false);
  CHECK(invalidOperationResponse.error.details.value("retrySafe").toBool(false));
  CHECK(
    invalidOperationResponse.error.details.value("recoveryAction").toString()
    == "fix_transform_parameters_then_retry");
  CHECK(history.back().toolName == "objects_transform");
}

TEST_CASE("McpBridgeServer native group tools", "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  auto objectRegistry = McpObjectRegistry{};
  auto metadataStore = std::map<QString, McpBrushMetadataRecord>{};

  const auto noSelectionGroupResponse = groupCreateFromSelectionForMapResult(
    map,
    "group_create_from_selection",
    QJsonObject{{"name", "empty-group"}},
    history,
    nextOperationIndex,
    objectRegistry,
    &metadataStore);
  REQUIRE_FALSE(noSelectionGroupResponse.ok);
  CHECK(
    noSelectionGroupResponse.error.details.value("mutatedDocument").toBool(true)
    == false);
  CHECK(noSelectionGroupResponse.error.details.value("retrySafe").toBool(false));
  CHECK(
    noSelectionGroupResponse.error.details.value("recoveryAction").toString()
    == "select_objects_then_retry");

  const auto noGroupRenameResponse = groupRenameSelectedForMapResult(
    map,
    "group_rename_selected",
    QJsonObject{{"name", "renamed-group"}},
    history,
    nextOperationIndex,
    objectRegistry);
  REQUIRE_FALSE(noGroupRenameResponse.ok);
  CHECK(
    noGroupRenameResponse.error.details.value("mutatedDocument").toBool(true) == false);
  CHECK(noGroupRenameResponse.error.details.value("retrySafe").toBool(false));
  CHECK(
    noGroupRenameResponse.error.details.value("recoveryAction").toString()
    == "select_groups_then_retry");

  const auto noGroupUngroupResponse = groupUngroupSelectedForMapResult(
    map,
    "group_ungroup_selected",
    QJsonObject{},
    history,
    nextOperationIndex,
    objectRegistry,
    &metadataStore);
  REQUIRE_FALSE(noGroupUngroupResponse.ok);
  CHECK(
    noGroupUngroupResponse.error.details.value("mutatedDocument").toBool(true) == false);
  CHECK(noGroupUngroupResponse.error.details.value("retrySafe").toBool(false));
  CHECK(
    noGroupUngroupResponse.error.details.value("recoveryAction").toString()
    == "select_groups_then_retry");

  const auto createBrushes = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"detail", "ids"},
      {"select", true},
      {"defaultMetadata",
       QJsonObject{
         {"moduleId", "group-route"},
         {"part", "road"},
         {"role", "walkable"},
       }},
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{0, 0, 0}},
           {"max", QJsonArray{64, 64, 16}},
           {"metadata", QJsonObject{{"order", 1}}},
         },
         QJsonObject{
           {"type", "ramp_between"},
           {"start", QJsonArray{64, 32, 16}},
           {"end", QJsonArray{192, 32, 64}},
           {"width", 64},
           {"thickness", 16},
           {"metadata", QJsonObject{{"order", 2}}},
         },
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{192, 0, 64}},
           {"max", QJsonArray{256, 64, 80}},
           {"metadata", QJsonObject{{"order", 3}}},
         },
       }},
    },
    history,
    nextOperationIndex,
    &metadataStore,
    nullptr);
  const auto createError =
    createBrushes.ok ? std::string{} : createBrushes.error.message.toStdString();
  INFO(createError);
  REQUIRE(createBrushes.ok);
  REQUIRE(map.selection().nodes.size() == 3);

  const auto groupResponse = groupCreateFromSelectionForMapResult(
    map,
    "group_create_from_selection",
    QJsonObject{{"name", "road-group-initial"}},
    history,
    nextOperationIndex,
    objectRegistry,
    &metadataStore);
  const auto groupError =
    groupResponse.ok ? std::string{} : groupResponse.error.message.toStdString();
  INFO(groupError);
  REQUIRE(groupResponse.ok);
  REQUIRE(map.selection().hasOnlyGroups());
  auto* groupNode = map.selection().groups.front();
  REQUIRE(groupNode != nullptr);
  CHECK(groupNode->group().name() == "road-group-initial");

  const auto groupId = groupResponse.result.value("groupId").toString();
  CHECK_FALSE(groupId.isEmpty());
  CHECK(groupResponse.result.value("changedObjectCount").toInt() == 1);
  CHECK(groupResponse.result.value("sourceSelectionCount").toInt() == 3);
  CHECK(
    groupResponse.result.value("childCounts").toObject().value("brushes").toInt() == 3);
  CHECK(history.back().toolName == "group_create_from_selection");

  const auto inspectResponse = groupInspectForMapResult(
    map,
    QJsonObject{
      {"objectId", groupId},
      {"includeChildren", true},
      {"idsMode", "sample"},
    },
    objectRegistry);
  const auto inspectError =
    inspectResponse.ok ? std::string{} : inspectResponse.error.message.toStdString();
  INFO(inspectError);
  REQUIRE(inspectResponse.ok);
  CHECK(inspectResponse.result.value("groupName").toString() == "road-group-initial");
  CHECK(inspectResponse.result.value("childCount").toInt() == 3);
  CHECK(inspectResponse.result.value("childIdCount").toInt() == 3);
  CHECK(inspectResponse.result.value("childIdSample").toArray().size() == 3);

  const auto transformGroup = transformObjectsForMapResult(
    map,
    "objects_transform",
    QJsonObject{
      {"operation", "translate"},
      {"delta", QJsonArray{16, 0, 0}},
    },
    history,
    nextOperationIndex,
    objectRegistry);
  const auto transformError =
    transformGroup.ok ? std::string{} : transformGroup.error.message.toStdString();
  INFO(transformError);
  REQUIRE(transformGroup.ok);
  CHECK(transformGroup.result.value("targetSource").toString() == "selection");
  CHECK(transformGroup.result.value("targetCount").toInt() == 1);
  CHECK(map.selection().hasOnlyGroups());

  const auto selectionAnalysis = geometryAnalyzeSelectionResult(map, QJsonObject{});
  REQUIRE(selectionAnalysis.ok);
  CHECK(selectionAnalysis.result.value("brushCount").toInt() == 3);

  const auto renameResponse = groupRenameSelectedForMapResult(
    map,
    "group_rename_selected",
    QJsonObject{{"name", "road-group"}},
    history,
    nextOperationIndex,
    objectRegistry);
  const auto renameError =
    renameResponse.ok ? std::string{} : renameResponse.error.message.toStdString();
  INFO(renameError);
  REQUIRE(renameResponse.ok);
  CHECK(groupNode->group().name() == "road-group");
  CHECK(history.back().toolName == "group_rename_selected");

  REQUIRE(map.undoCommandName() != nullptr);
  CHECK(QString::fromStdString(*map.undoCommandName()) == "Rename Group");
  const auto undoRename = historyUndoForMapResult(map, history);
  const auto undoRenameError =
    undoRename.ok ? std::string{} : undoRename.error.message.toStdString();
  INFO(undoRenameError);
  REQUIRE(undoRename.ok);
  CHECK(groupNode->group().name() == "road-group-initial");

  REQUIRE(map.redoCommandName() != nullptr);
  CHECK(QString::fromStdString(*map.redoCommandName()) == "Rename Group");
  map.redoCommand();
  CHECK(groupNode->group().name() == "road-group");

  mdl::deselectAll(map);
  mdl::selectNodes(map, {groupNode});
  const auto ungroupResponse = groupUngroupSelectedForMapResult(
    map,
    "group_ungroup_selected",
    QJsonObject{{"idsMode", "sample"}},
    history,
    nextOperationIndex,
    objectRegistry);
  const auto ungroupError =
    ungroupResponse.ok ? std::string{} : ungroupResponse.error.message.toStdString();
  INFO(ungroupError);
  REQUIRE(ungroupResponse.ok);
  CHECK_FALSE(map.selection().hasOnlyGroups());
  CHECK(map.selection().nodes.size() == 3);
  CHECK(ungroupResponse.result.value("ungroupedGroupCount").toInt() == 1);
  CHECK(ungroupResponse.result.value("selectedObjectIdCount").toInt() == 3);
  CHECK(ungroupResponse.result.value("selectedObjectIdSample").toArray().size() == 3);
  CHECK(
    ungroupResponse.result.value("selectedCounts").toObject().value("brushes").toInt()
    == 3);

  const auto selectedChildren = map.selection().nodes;
  REQUIRE(!selectedChildren.empty());
  const auto selectedChildIds =
    ungroupResponse.result.value("selectedObjectIdSample").toArray();
  REQUIRE(!selectedChildIds.isEmpty());
  const auto inspectNonGroup = groupInspectForMapResult(
    map, QJsonObject{{"objectId", selectedChildIds.first()}}, objectRegistry);
  CHECK_FALSE(inspectNonGroup.ok);
  CHECK(inspectNonGroup.error.message.contains("not a group"));
  CHECK(inspectNonGroup.error.details.value("mutatedDocument").toBool(true) == false);
  CHECK(inspectNonGroup.error.details.value("retrySafe").toBool(false));
  CHECK(
    inspectNonGroup.error.details.value("recoveryAction").toString()
    == "provide_group_target_or_select_group");

  mdl::deselectAll(map);
  mdl::selectNodes(map, {selectedChildren.front()});
  const auto renameNonGroup = groupRenameSelectedForMapResult(
    map,
    "group_rename_selected",
    QJsonObject{{"name", "bad"}},
    history,
    nextOperationIndex,
    objectRegistry);
  CHECK_FALSE(renameNonGroup.ok);
  CHECK(renameNonGroup.error.message.contains("only groups"));
}

TEST_CASE(
  "McpBridgeServer keeps selector metadata live after grouping", "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  auto objectRegistry = McpObjectRegistry{};
  auto metadataStore = std::map<QString, McpBrushMetadataRecord>{};
  auto moduleStore = std::map<QString, McpModuleRecord>{};

  const auto createBrushes = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"select", false},
      {"defaultMetadata",
       QJsonObject{
         {"moduleId", "group-refresh-route"},
         {"routeId", "group-refresh-route"},
       }},
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{0, 0, 0}},
           {"max", QJsonArray{64, 64, 16}},
           {"metadata", QJsonObject{{"part", "road"}, {"role", "walkable"}}},
         },
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{80, 0, 0}},
           {"max", QJsonArray{144, 64, 16}},
           {"metadata", QJsonObject{{"part", "rails"}, {"role", "boundary"}}},
         },
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{160, 0, 0}},
           {"max", QJsonArray{224, 64, 16}},
           {"metadata", QJsonObject{{"part", "supports"}, {"role", "support"}}},
         },
       }},
    },
    history,
    nextOperationIndex,
    &metadataStore,
    &moduleStore,
    &objectRegistry);
  REQUIRE(createBrushes.ok);

  const auto previewRoad = selectorPreviewForMapResult(
    map,
    QJsonObject{
      {"selector", QJsonObject{{"moduleId", "group-refresh-route"}, {"part", "road"}}},
      {"idsMode", "sample"},
    },
    history,
    metadataStore,
    moduleStore,
    objectRegistry);
  REQUIRE(previewRoad.ok);
  CHECK(previewRoad.result.value("matchedCount").toInt() == 1);
  const auto roadId =
    previewRoad.result.value("objectIdSample").toArray().first().toString();
  REQUIRE(!roadId.isEmpty());
  const auto resolvedRoad = objectRegistry.resolveExternalId(map, roadId);
  REQUIRE(resolvedRoad.ok);
  const auto roadPath = McpObjectRegistry::parseLegacyObjectId(resolvedRoad.legacyPathId);
  REQUIRE(roadPath);
  auto* roadNode = map.worldNode().resolvePath(*roadPath);
  REQUIRE(roadNode != nullptr);
  mdl::deselectAll(map);
  mdl::selectNodes(map, {roadNode});

  const auto groupRoad = groupCreateFromSelectionForMapResult(
    map,
    "group_create_from_selection",
    QJsonObject{{"name", "group-refresh-road"}},
    history,
    nextOperationIndex,
    objectRegistry,
    &metadataStore,
    &moduleStore);
  REQUIRE(groupRoad.ok);

  const auto previewSupports = selectorPreviewForMapResult(
    map,
    QJsonObject{
      {"selector",
       QJsonObject{{"moduleId", "group-refresh-route"}, {"part", "supports"}}},
      {"idsMode", "sample"},
    },
    history,
    metadataStore,
    moduleStore,
    objectRegistry);
  REQUIRE(previewSupports.ok);
  CHECK(previewSupports.result.value("matchedCount").toInt() == 1);
  CHECK(previewSupports.result.value("warnings").toArray().isEmpty());
  const auto sample = previewSupports.result.value("sample").toArray();
  REQUIRE(sample.size() == 1);
  CHECK(
    sample.first().toObject().value("metadata").toObject().value("part") == "supports");

  const auto moduleList = moduleListForMapResult(
    map, QJsonObject{}, metadataStore, moduleStore, objectRegistry);
  REQUIRE(moduleList.ok);
  CHECK(moduleList.result.value("liveModuleCount").toInt() == 1);
  CHECK(moduleList.result.value("staleModuleCount").toInt() == 0);
}

TEST_CASE(
  "McpBridgeServer applies texture by filter to unmatched materials", "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;

  const auto createResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{0, 0, 0}},
           {"max", QJsonArray{64, 64, 16}},
           {"material", "source_mat"},
         },
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{128, 0, 0}},
           {"max", QJsonArray{192, 64, 16}},
           {"material", "source_mat"},
         },
       }},
      {"detail", "ids"},
    },
    history,
    nextOperationIndex);
  REQUIRE(createResponse.ok);

  const auto response = textureApplyByFilterForMapResult(
    map,
    "texture_apply_by_filter",
    QJsonObject{
      {"material", "target_mat"},
      {"type", "brush"},
      {"min", QJsonArray{-16, -16, -16}},
      {"max", QJsonArray{80, 80, 32}},
      {"boundsMode", "intersects"},
    },
    history,
    nextOperationIndex);

  const auto error = response.ok ? std::string{} : response.error.message.toStdString();
  INFO(error);
  REQUIRE(response.ok);
  CHECK(response.result.value("mutatedDocument").toBool());
  CHECK(
    response.result.value("documentFingerprint").toString()
    == documentFingerprintForMap(map));
  CHECK(response.result.value("material").toString() == "target_mat");
  CHECK(response.result.value("materialExists").toBool(true) == false);
  CHECK(!response.result.value("fallbackMaterial").toString().isEmpty());
  CHECK(response.result.value("brushCount").toInt() == 1);
  CHECK(response.result.value("faceCount").toInt() == 6);
  CHECK(response.result.value("changedObjectCount").toInt() == 1);
  CHECK(response.result.value("changedObjectIds").isUndefined());
  CHECK(response.result.value("changedObjectIdSample").isUndefined());
  CHECK(history.back().documentFingerprint == documentFingerprintForMap(map));

  const auto objectIds = createResponse.result.value("changedObjectIds").toArray();
  REQUIRE(objectIds.size() == 2);
  const auto firstBrushPath =
    McpObjectRegistry::parseLegacyObjectId(objectIds[0].toString());
  REQUIRE(firstBrushPath);
  auto* firstBrushNode =
    dynamic_cast<mdl::BrushNode*>(map.worldNode().resolvePath(*firstBrushPath));
  REQUIRE(firstBrushNode != nullptr);
  for (const auto& face : firstBrushNode->brush().faces())
  {
    CHECK(face.materialName() == "target_mat");
  }

  const auto sampleResponse = textureApplyByFilterForMapResult(
    map,
    "texture_apply_by_filter",
    QJsonObject{
      {"material", "sample_mat"},
      {"type", "brush"},
      {"min", QJsonArray{-16, -16, -16}},
      {"max", QJsonArray{80, 80, 32}},
      {"boundsMode", "intersects"},
      {"idsMode", "sample"},
    },
    history,
    nextOperationIndex);
  REQUIRE(sampleResponse.ok);
  CHECK(sampleResponse.result.value("materialExists").toBool(true) == false);
  CHECK(!sampleResponse.result.value("fallbackMaterial").toString().isEmpty());
  CHECK(sampleResponse.result.value("changedObjectIds").isUndefined());
  CHECK(sampleResponse.result.value("changedObjectIdSample").toArray().size() == 1);

  const auto fullResponse = textureApplyByFilterForMapResult(
    map,
    "texture_apply_by_filter",
    QJsonObject{
      {"material", "full_mat"},
      {"type", "brush"},
      {"min", QJsonArray{-16, -16, -16}},
      {"max", QJsonArray{80, 80, 32}},
      {"boundsMode", "intersects"},
      {"idsMode", "full"},
    },
    history,
    nextOperationIndex);
  REQUIRE(fullResponse.ok);
  CHECK(fullResponse.result.value("materialExists").toBool(true) == false);
  CHECK(fullResponse.result.value("changedObjectIds").toArray().size() == 1);

  const auto missingMaterialResponse = textureApplyByFilterForMapResult(
    map,
    "texture_apply_by_filter",
    QJsonObject{
      {"type", "brush"},
      {"min", QJsonArray{-16, -16, -16}},
      {"max", QJsonArray{80, 80, 32}},
      {"boundsMode", "intersects"},
    },
    history,
    nextOperationIndex);
  REQUIRE_FALSE(missingMaterialResponse.ok);
  CHECK(
    missingMaterialResponse.error.details.value("mutatedDocument").toBool(true) == false);
  CHECK(missingMaterialResponse.error.details.value("retrySafe").toBool(false));
  CHECK(
    missingMaterialResponse.error.details.value("recoveryAction").toString()
    == "add_material_then_retry");

  const auto missingTargetResponse = textureApplyByFilterForMapResult(
    map,
    "texture_apply_by_filter",
    QJsonObject{{"material", "target_mat"}, {"operationId", "mcp-op-missing"}},
    history,
    nextOperationIndex);
  REQUIRE_FALSE(missingTargetResponse.ok);
  CHECK(
    missingTargetResponse.error.details.value("mutatedDocument").toBool(true) == false);
  CHECK(
    missingTargetResponse.error.details.value("recoveryAction").toString()
    == "refresh_status_or_fix_texture_targets");
}

TEST_CASE(
  "McpBridgeServer texture_apply reports pre-mutation failures", "[McpBridgeServer]")
{
  auto appControllerFixture = AppControllerFixture{};
  auto& appController = appControllerFixture.appController();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  auto objectRegistry = McpObjectRegistry{};

  const auto missingMaterialResponse = textureApplyResult(
    appController,
    "texture_apply",
    QJsonObject{},
    history,
    nextOperationIndex,
    objectRegistry);
  REQUIRE_FALSE(missingMaterialResponse.ok);
  CHECK(
    missingMaterialResponse.error.details.value("mutatedDocument").toBool(true) == false);
  CHECK(missingMaterialResponse.error.details.value("retrySafe").toBool(false));
  CHECK(
    missingMaterialResponse.error.details.value("recoveryAction").toString()
    == "add_material_then_retry");
}

TEST_CASE(
  "McpBridgeServer texture_replace reports pre-mutation failures", "[McpBridgeServer]")
{
  auto appControllerFixture = AppControllerFixture{};
  auto& appController = appControllerFixture.appController();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;

  const auto response = textureReplaceResult(
    appController, "texture_replace", QJsonObject{}, history, nextOperationIndex);
  REQUIRE_FALSE(response.ok);
  CHECK(response.error.details.value("mutatedDocument").toBool(true) == false);
  CHECK(response.error.details.value("retrySafe").toBool(false));
  CHECK(
    response.error.details.value("recoveryAction").toString()
    == "provide_find_and_replace_then_retry");
}

TEST_CASE(
  "McpBridgeServer texture_lock_set reports non-document mutation state",
  "[McpBridgeServer]")
{
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
  auto& map = document->map();

  const auto missingParamsResponse = textureLockSetForMapResult(map, QJsonObject{});
  REQUIRE_FALSE(missingParamsResponse.ok);
  CHECK(
    missingParamsResponse.error.details.value("mutatedDocument").toBool(true) == false);
  CHECK(missingParamsResponse.error.details.value("retrySafe").toBool(false));
  CHECK(
    missingParamsResponse.error.details.value("recoveryAction").toString()
    == "provide_texture_or_uv_lock_then_retry");

  const auto response =
    textureLockSetForMapResult(map, QJsonObject{{"textureLock", true}, {"uvLock", true}});
  REQUIRE(response.ok);
  CHECK(response.result.value("changed").toBool());
  CHECK(response.result.value("textureLock").toBool());
  CHECK(response.result.value("uvLock").toBool());
  CHECK(response.result.value("mutatedDocument").toBool(true) == false);
}

TEST_CASE(
  "McpBridgeServer texture_align_face reports pre-mutation failures", "[McpBridgeServer]")
{
  auto appControllerFixture = AppControllerFixture{};
  auto& appController = appControllerFixture.appController();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  auto objectRegistry = McpObjectRegistry{};

  const auto response = textureAlignFaceResult(
    appController,
    "texture_align_face",
    QJsonObject{{"mode", "diagonal"}},
    history,
    nextOperationIndex,
    objectRegistry);
  REQUIRE_FALSE(response.ok);
  CHECK(response.error.details.value("mutatedDocument").toBool(true) == false);
  CHECK(response.error.details.value("retrySafe").toBool(false));
  CHECK(
    response.error.details.value("recoveryAction").toString()
    == "choose_supported_alignment_mode");
}

TEST_CASE(
  "McpBridgeServer texture_copy_from_face reports pre-mutation failures",
  "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  auto objectRegistry = McpObjectRegistry{};

  const auto missingSourceResponse = textureCopyFromFaceForMapResult(
    map,
    "texture_copy_from_face",
    QJsonObject{},
    history,
    nextOperationIndex,
    objectRegistry);
  REQUIRE_FALSE(missingSourceResponse.ok);
  CHECK(
    missingSourceResponse.error.details.value("mutatedDocument").toBool(true) == false);
  CHECK(missingSourceResponse.error.details.value("retrySafe").toBool(false));
  CHECK(
    missingSourceResponse.error.details.value("recoveryAction").toString()
    == "provide_valid_source_face_then_retry");

  const auto createResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"operations",
       QJsonArray{QJsonObject{
         {"type", "box"},
         {"min", QJsonArray{0, 0, 0}},
         {"max", QJsonArray{64, 64, 16}},
       }}},
      {"detail", "ids"},
    },
    history,
    nextOperationIndex);
  REQUIRE(createResponse.ok);
  const auto objectIds = createResponse.result.value("changedObjectIds").toArray();
  REQUIRE(objectIds.size() == 1);
  mdl::deselectAll(map);

  const auto missingTargetResponse = textureCopyFromFaceForMapResult(
    map,
    "texture_copy_from_face",
    QJsonObject{
      {"sourceObjectId", objectIds.first().toString()},
      {"sourceFaceIndex", 0},
    },
    history,
    nextOperationIndex,
    objectRegistry);
  REQUIRE_FALSE(missingTargetResponse.ok);
  CHECK(
    missingTargetResponse.error.details.value("mutatedDocument").toBool(true) == false);
  CHECK(missingTargetResponse.error.details.value("retrySafe").toBool(false));
  CHECK(
    missingTargetResponse.error.details.value("recoveryAction").toString()
    == "provide_target_faces_or_select_brush_faces");
}

TEST_CASE(
  "McpBridgeServer entity mutations report pre-mutation failures", "[McpBridgeServer]")
{
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
  auto& map = document->map();
  map.entityDefinitionManager().setDefinitions({
    {"test_light", {}, "", {}, mdl::PointEntityDefinition{vm::bbox3d{8.0}, {}, {}}},
  });
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  const auto descendantCountBefore = map.worldNode().descendantCount();

  const auto checkFailure = [](const auto& response, const QString& recoveryAction) {
    REQUIRE_FALSE(response.ok);
    CHECK(response.error.details.value("mutatedDocument").toBool(true) == false);
    CHECK(response.error.details.value("retrySafe").toBool(false));
    CHECK(response.error.details.value("recoveryAction").toString() == recoveryAction);
  };

  checkFailure(
    createEntityForMapResult(
      map, "entity_create", QJsonObject{}, history, nextOperationIndex),
    "provide_entity_classname_then_retry");
  checkFailure(
    updateEntityForMapResult(
      map, "entity_update", QJsonObject{}, history, nextOperationIndex),
    "provide_entity_object_id_then_retry");
  checkFailure(
    deleteEntityForMapResult(
      map, "entity_delete", QJsonObject{}, history, nextOperationIndex),
    "provide_entity_object_id_then_retry");
  checkFailure(
    createEntityFromSchemaForMapResult(
      map, "entity_create_from_schema", QJsonObject{}, history, nextOperationIndex),
    "provide_entity_classname_then_retry");
  checkFailure(
    createEntityCheckedForMapResult(
      map,
      "entity_create_checked",
      QJsonObject{{"classname", "not_a_real_entity"}},
      history,
      nextOperationIndex),
    "choose_defined_point_entity_classname_then_retry");
  checkFailure(
    tieBrushesForMapResult(
      map, "entity_tie_brushes", QJsonObject{}, history, nextOperationIndex),
    "provide_brush_entity_classname_then_retry");
  checkFailure(
    untieBrushesForMapResult(
      map, "entity_untie_brushes", QJsonObject{}, history, nextOperationIndex),
    "select_brush_entity_brushes_then_retry");

  CHECK(history.empty());
  CHECK(map.worldNode().descendantCount() == descendantCountBefore);
}

TEST_CASE(
  "McpBridgeServer face_select reports non-document mutation state", "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  auto objectRegistry = McpObjectRegistry{};

  const auto createResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"operations",
       QJsonArray{QJsonObject{
         {"type", "box"},
         {"min", QJsonArray{0, 0, 0}},
         {"max", QJsonArray{64, 64, 16}},
       }}},
      {"detail", "ids"},
    },
    history,
    nextOperationIndex);
  REQUIRE(createResponse.ok);
  const auto objectIds = createResponse.result.value("changedObjectIds").toArray();
  REQUIRE(objectIds.size() == 1);

  const auto response = faceSelectForMapResult(
    map,
    QJsonObject{{"objectId", objectIds.first().toString()}, {"faceIndex", 0}},
    history,
    objectRegistry);
  REQUIRE(response.ok);
  CHECK(response.result.value("selectedCount").toInt() == 1);
  CHECK(response.result.value("mutatedDocument").toBool(true) == false);
  CHECK(map.selection().brushFaces.size() == 1u);

  const auto listResponse = faceListForMapResult(
    map,
    QJsonObject{{"objectId", objectIds.first().toString()}, {"faceIndex", 0}},
    history,
    objectRegistry);
  REQUIRE(listResponse.ok);
  CHECK(listResponse.result.value("count").toInt() == 1);

  mdl::deselectAll(map);
  const auto missingListTargetResponse = faceListForMapResult(
    map, QJsonObject{{"objectId", "missing"}}, history, objectRegistry);
  REQUIRE_FALSE(missingListTargetResponse.ok);
  CHECK(
    missingListTargetResponse.error.details.value("mutatedDocument").toBool(true)
    == false);
  CHECK(missingListTargetResponse.error.details.value("retrySafe").toBool(false));
  CHECK(
    missingListTargetResponse.error.details.value("recoveryAction").toString()
    == "provide_face_targets_or_select_brush_faces");

  const auto missingTargetResponse =
    faceSelectForMapResult(map, QJsonObject{}, history, objectRegistry);
  REQUIRE_FALSE(missingTargetResponse.ok);
  CHECK(
    missingTargetResponse.error.details.value("mutatedDocument").toBool(true) == false);
  CHECK(missingTargetResponse.error.details.value("retrySafe").toBool(false));
  CHECK(
    missingTargetResponse.error.details.value("recoveryAction").toString()
    == "provide_faces_or_select_brush_faces");
}

TEST_CASE(
  "McpBridgeServer face_texture_set reports pre-mutation failures", "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  auto objectRegistry = McpObjectRegistry{};

  const auto missingAttributesResponse = faceTextureSetForMapResult(
    map, "face_texture_set", QJsonObject{}, history, nextOperationIndex, objectRegistry);
  REQUIRE_FALSE(missingAttributesResponse.ok);
  CHECK(
    missingAttributesResponse.error.details.value("mutatedDocument").toBool(true)
    == false);
  CHECK(missingAttributesResponse.error.details.value("retrySafe").toBool(false));
  CHECK(
    missingAttributesResponse.error.details.value("recoveryAction").toString()
    == "provide_face_texture_attributes_then_retry");

  const auto missingTargetResponse = faceTextureSetForMapResult(
    map,
    "face_texture_set",
    QJsonObject{{"material", "brick"}},
    history,
    nextOperationIndex,
    objectRegistry);
  REQUIRE_FALSE(missingTargetResponse.ok);
  CHECK(
    missingTargetResponse.error.details.value("mutatedDocument").toBool(true) == false);
  CHECK(missingTargetResponse.error.details.value("retrySafe").toBool(false));
  CHECK(
    missingTargetResponse.error.details.value("recoveryAction").toString()
    == "provide_faces_or_select_brush_faces");
}

TEST_CASE(
  "McpBridgeServer applies texture to semantic operation faces", "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;

  const auto createResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"operations",
       QJsonArray{QJsonObject{
         {"type", "box"},
         {"min", QJsonArray{0, 0, 0}},
         {"max", QJsonArray{64, 64, 16}},
         {"material", "source_mat"},
       }}},
      {"detail", "ids"},
    },
    history,
    nextOperationIndex);
  REQUIRE(createResponse.ok);
  REQUIRE(history.size() == 1u);

  const auto response = textureApplyByFilterForMapResult(
    map,
    "texture_apply_by_filter",
    QJsonObject{
      {"material", "top_mat"},
      {"operationId", history.front().operationId},
      {"faceSemantic", "top"},
    },
    history,
    nextOperationIndex);

  const auto error = response.ok ? std::string{} : response.error.message.toStdString();
  INFO(error);
  REQUIRE(response.ok);
  CHECK(response.result.value("faceCount").toInt() == 1);
  CHECK(response.result.value("brushCount").toInt() == 1);
  CHECK(response.result.value("faceSemantic").toString() == "top");

  const auto objectIds = createResponse.result.value("changedObjectIds").toArray();
  REQUIRE(objectIds.size() == 1);
  const auto brushPath = McpObjectRegistry::parseLegacyObjectId(objectIds[0].toString());
  REQUIRE(brushPath);
  auto* brushNode =
    dynamic_cast<mdl::BrushNode*>(map.worldNode().resolvePath(*brushPath));
  REQUIRE(brushNode != nullptr);

  auto topFaceCount = 0;
  auto topFaceMaterialCount = 0;
  for (const auto& face : brushNode->brush().faces())
  {
    if (face.normal().z() > 0.75)
    {
      ++topFaceCount;
      if (face.materialName() == "top_mat")
      {
        ++topFaceMaterialCount;
      }
    }
    else
    {
      CHECK(face.materialName() == "source_mat");
    }
  }
  CHECK(topFaceCount == 1);
  CHECK(topFaceMaterialCount == 1);
}

TEST_CASE(
  "McpBridgeServer deletes operations without long object id lists", "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  auto registry = McpObjectRegistry{};

  const auto createResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{0, 0, 0}},
           {"max", QJsonArray{64, 64, 16}},
         },
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{128, 0, 0}},
           {"max", QJsonArray{192, 64, 16}},
         },
       }},
      {"detail", "ids"},
    },
    history,
    nextOperationIndex);
  REQUIRE(createResponse.ok);
  REQUIRE(history.size() == 1u);
  for (const auto& id : history.front().changedObjectIds)
  {
    CHECK(registry.externalIdForLegacy(map, id).startsWith("mcp:"));
  }
  CHECK(map.worldNode().descendantCount() == 3u);

  const auto liveCompactState =
    registry.liveStateJson(map, history.front().changedObjectIds, false);
  CHECK_FALSE(liveCompactState.contains("objectDiagnostics"));
  CHECK(liveCompactState.value("valid").toBool());

  const auto liveFullState =
    registry.liveStateJson(map, history.front().changedObjectIds, false, true);
  CHECK(liveFullState.value("valid").toBool());
  CHECK(liveFullState.value("objectDiagnostics").toArray().size() == 2);

  const auto deleteResponse = deleteObjectsByOperationForMapResult(
    map,
    "objects_delete_by_operation",
    QJsonObject{{"operationId", history.front().operationId}, {"idsMode", "full"}},
    history,
    nextOperationIndex,
    registry);
  REQUIRE(deleteResponse.ok);
  CHECK(deleteResponse.result.value("sourceOperationId").toString() == "mcp-op-1");
  CHECK(deleteResponse.result.value("operationKind").toString() == "delete");
  CHECK(deleteResponse.result.value("mutatedDocument").toBool());
  CHECK(
    deleteResponse.result.value("documentFingerprint").toString()
    == documentFingerprintForMap(map));
  CHECK(deleteResponse.result.value("changedObjectCount").toInt() == 0);
  CHECK(deleteResponse.result.value("deletedCount").toInt() == 2);
  CHECK(deleteResponse.result.value("deletedObjectIds").toArray().size() == 2);
  CHECK(map.worldNode().descendantCount() == 1u);
  REQUIRE(history.size() == 2u);
  CHECK(history.back().operationKind == "delete");
  CHECK(history.back().documentFingerprint == documentFingerprintForMap(map));
  CHECK(history.back().changedObjectIds.empty());
  CHECK(history.back().deletedObjectIds.size() == 2);

  const auto staleDeleteResponse = deleteObjectsByOperationForMapResult(
    map,
    "objects_delete_by_operation",
    QJsonObject{{"operationId", history.front().operationId}},
    history,
    nextOperationIndex,
    registry);
  REQUIRE_FALSE(staleDeleteResponse.ok);
  CHECK(staleDeleteResponse.error.details.value("mutatedDocument").toBool(true) == false);
  CHECK(staleDeleteResponse.error.details.value("retrySafe").toBool(false));
  CHECK(
    staleDeleteResponse.error.details.value("recoveryAction").toString()
    == "inspect_operation_or_refresh_status");
  CHECK(
    staleDeleteResponse.error.details.value("operationId").toString()
    == history.front().operationId);

  const auto validateDelete = operationValidateForMapResult(
    map, history, QJsonObject{{"operationId", history.back().operationId}}, registry);
  REQUIRE(validateDelete.ok);
  CHECK(validateDelete.result.value("valid").toBool());
  CHECK_FALSE(validateDelete.result.value("targetsLive").toBool());
  CHECK(validateDelete.result.value("deletedObjectCount").toInt() == 2);

  const auto compactState =
    registry.liveStateJson(map, history.front().changedObjectIds, false);
  CHECK_FALSE(compactState.contains("objectDiagnostics"));
  CHECK_FALSE(compactState.value("valid").toBool());
  CHECK(compactState.value("staleObjectCount").toInt() >= 1);

  const auto fullState =
    registry.liveStateJson(map, history.front().changedObjectIds, false, true);
  CHECK(fullState.contains("objectDiagnostics"));
}

TEST_CASE(
  "McpBridgeServer brush primitives report pre-mutation failures", "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  const auto descendantCountBefore = map.worldNode().descendantCount();

  const auto response = createBrushForMapResult(
    map,
    "brush_create",
    QJsonObject{
      {"type", "torus"}, {"min", QJsonArray{0, 0, 0}}, {"max", QJsonArray{64, 64, 64}}},
    history,
    nextOperationIndex);

  REQUIRE_FALSE(response.ok);
  CHECK(response.error.details.value("mutatedDocument").toBool(true) == false);
  CHECK(response.error.details.value("retrySafe").toBool(false));
  CHECK(
    response.error.details.value("recoveryAction").toString()
    == "fix_brush_parameters_then_retry");
  CHECK(response.error.details.value("type").toString() == "torus");
  CHECK(history.empty());
  CHECK(map.worldNode().descendantCount() == descendantCountBefore);
}

TEST_CASE("McpBridgeServer batch blockout tools", "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;

  const auto missingOperationsResponse = blockoutCreateBatchForMapResult(
    map, "blockout_create_batch", QJsonObject{}, history, nextOperationIndex);
  REQUIRE_FALSE(missingOperationsResponse.ok);
  CHECK(
    missingOperationsResponse.error.details.value("mutatedDocument").toBool(true)
    == false);
  CHECK(missingOperationsResponse.error.details.value("retrySafe").toBool(false));
  CHECK(
    missingOperationsResponse.error.details.value("recoveryAction").toString()
    == "provide_batch_operations_then_retry");

  const auto missingBoxesResponse = createBoxesBatchForMapResult(
    map, "brush_create_boxes_batch", QJsonObject{}, history, nextOperationIndex);
  REQUIRE_FALSE(missingBoxesResponse.ok);
  CHECK(
    missingBoxesResponse.error.details.value("mutatedDocument").toBool(true) == false);
  CHECK(missingBoxesResponse.error.details.value("retrySafe").toBool(false));
  CHECK(
    missingBoxesResponse.error.details.value("recoveryAction").toString()
    == "provide_box_specs_then_retry");

  const auto batchResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"name", "MCP: Test batch"},
      {"grid", 16},
      {"select", true},
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{0, 0, 0}},
           {"max", QJsonArray{64, 64, 16}},
         },
         QJsonObject{
           {"type", "ramp"},
           {"min", QJsonArray{80, 0, 0}},
           {"max", QJsonArray{144, 64, 32}},
           {"axis", "x"},
         },
         QJsonObject{
           {"type", "wedge"},
           {"min", QJsonArray{160, 0, 0}},
           {"max", QJsonArray{224, 64, 32}},
           {"axis", "x"},
         },
         QJsonObject{
           {"type", "ramp_between"},
           {"start", QJsonArray{240, 32, 0}},
           {"end", QJsonArray{368, 32, 64}},
           {"width", 64},
           {"thickness", 16},
         },
         QJsonObject{
           {"type", "cylinder"},
           {"min", QJsonArray{384, 0, 0}},
           {"max", QJsonArray{448, 64, 96}},
           {"sides", 8},
           {"axis", "z"},
         },
       }},
    },
    history,
    nextOperationIndex);

  const auto batchError =
    batchResponse.ok ? std::string{} : batchResponse.error.message.toStdString();
  INFO(batchError);
  REQUIRE(batchResponse.ok);
  const auto operationId = batchResponse.result.value("operationId").toString();
  CHECK(!operationId.isEmpty());
  CHECK(batchResponse.result.value("mutatedDocument").toBool());
  CHECK(
    batchResponse.result.value("documentFingerprint").toString()
    == documentFingerprintForMap(map));
  CHECK(batchResponse.result.value("transactionName").toString() == "MCP: Test batch");
  CHECK(batchResponse.result.value("brushCount").toInt() == 5);
  CHECK(batchResponse.result.value("changedObjectCount").toInt() == 5);
  CHECK(batchResponse.result.value("grid").toDouble() == 16.0);
  CHECK(batchResponse.result.value("changedObjectIds").isUndefined());
  CHECK(batchResponse.result.value("resourceUri")
          .toString()
          .startsWith("tbmcp://operation/"));
  CHECK(batchResponse.result.value("validation").toObject().value("valid").toBool());
  CHECK(batchResponse.result.value("warnings").toArray().isEmpty());
  CHECK(map.selection().nodes.size() == 5u);
  CHECK(batchResponse.result.value("intentSummaries").toArray().size() == 3);
  REQUIRE_FALSE(history.empty());
  CHECK(history.back().documentFingerprint == documentFingerprintForMap(map));

  const auto idsModeResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"idsMode", "full"},
      {"operations",
       QJsonArray{QJsonObject{
         {"type", "box"},
         {"min", QJsonArray{512, 0, 0}},
         {"max", QJsonArray{576, 64, 32}},
       }}},
    },
    history,
    nextOperationIndex);
  REQUIRE(idsModeResponse.ok);
  CHECK(idsModeResponse.result.value("changedObjectIds").toArray().size() == 1);

  auto metadataStore = std::map<QString, McpBrushMetadataRecord>{};
  auto moduleStore = std::map<QString, McpModuleRecord>{};
  auto objectRegistry = McpObjectRegistry{};
  const auto floorRibbonResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"defaultMetadata", QJsonObject{{"moduleId", "ribbon-floor-module"}}},
      {"operations",
       QJsonArray{QJsonObject{
         {"type", "path_ribbon"},
         {"parts", QJsonArray{"floor"}},
         {"points2d", QJsonArray{QJsonArray{640, 0}, QJsonArray{768, 0}}},
         {"width", 64},
         {"minZ", 0},
         {"maxZ", 16},
       }}},
    },
    history,
    nextOperationIndex,
    &metadataStore,
    &moduleStore,
    &objectRegistry);
  REQUIRE(floorRibbonResponse.ok);
  CHECK(std::ranges::any_of(
    floorRibbonResponse.result.value("warnings").toArray(), [](const auto& warning) {
      return warning.toString().contains("pathRibbonFloorPartPreserved");
    }));
  const auto ribbonFloorPreview = selectorPreviewForMapResult(
    map,
    QJsonObject{
      {"selector", QJsonObject{{"moduleId", "ribbon-floor-module"}, {"part", "floor"}}},
      {"idsMode", "count"},
    },
    history,
    metadataStore,
    moduleStore,
    objectRegistry);
  REQUIRE(ribbonFloorPreview.ok);
  CHECK(ribbonFloorPreview.result.value("matchedCount").toInt() == 1);

  const auto diagonalRampPreviewResponse = blockoutCompilePreviewForMapResult(
    map,
    QJsonObject{
      {"grid", 16},
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "ramp_between"},
           {"start", QJsonArray{0, 256, 0}},
           {"end", QJsonArray{128, 384, 64}},
           {"width", 96},
           {"thickness", 16},
         },
       }},
    });
  REQUIRE(diagonalRampPreviewResponse.ok);
  const auto diagonalPreviewWarnings =
    diagonalRampPreviewResponse.result.value("warnings").toArray();
  REQUIRE(diagonalPreviewWarnings.size() == 1);
  CHECK(diagonalPreviewWarnings.first().toString().contains(
    "offAxisRampMayProduceNonGridVertices"));

  const auto diagonalRampResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"name", "MCP: Diagonal ramp warning sample"},
      {"grid", 16},
      {"select", false},
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "ramp_between"},
           {"start", QJsonArray{0, 384, 0}},
           {"end", QJsonArray{128, 512, 64}},
           {"width", 96},
           {"thickness", 16},
         },
       }},
    },
    history,
    nextOperationIndex);
  REQUIRE(diagonalRampResponse.ok);
  const auto diagonalWarnings = diagonalRampResponse.result.value("warnings").toArray();
  REQUIRE(diagonalWarnings.size() == 1);
  CHECK(
    diagonalWarnings.first().toString().contains("offAxisRampMayProduceNonGridVertices"));

  const auto cylinderAnalyzeResponse =
    geometryAnalyzeSelectionResult(map, QJsonObject{{"grid", 1}});
  REQUIRE(cylinderAnalyzeResponse.ok);
  CHECK(
    cylinderAnalyzeResponse.result.value("nonGridAlignedObjectIds").toArray().isEmpty());

  const auto slopeResponse = geometryAnalyzeSlopesForMapResult(
    map,
    QJsonObject{
      {"operationId", operationId},
      {"start", QJsonArray{240, 32, 0}},
      {"end", QJsonArray{368, 32, 64}},
      {"detail", "full"},
    },
    history);
  REQUIRE(slopeResponse.ok);
  CHECK(slopeResponse.result.value("passed").toBool());
  CHECK(
    slopeResponse.result.value("recoveryAction").toString()
    == "continue_validation_or_review");
  CHECK(slopeResponse.result.value("routeDirectionProvided").toBool());
  const auto slopes = slopeResponse.result.value("slopes").toArray();
  REQUIRE(!slopes.isEmpty());
  auto foundAscendingRampBetween = false;
  for (const auto& slopeValue : slopes)
  {
    const auto slope = slopeValue.toObject();
    if (
      slope.value("classification").toString() == "ascending"
      && slope.value("heightDeltaAlongRoute").toDouble() > 0.0)
    {
      foundAscendingRampBetween = true;
    }
  }
  CHECK(foundAscendingRampBetween);

  const auto reverseSlopeResponse = geometryAnalyzeSlopesForMapResult(
    map,
    QJsonObject{
      {"operationId", operationId},
      {"routeDirection", QJsonArray{-1, 0, 0}},
      {"detail", "full"},
    },
    history);
  REQUIRE(reverseSlopeResponse.ok);
  const auto reverseSlopes = reverseSlopeResponse.result.value("slopes").toArray();
  auto foundDescendingRampBetween = false;
  for (const auto& slopeValue : reverseSlopes)
  {
    const auto slope = slopeValue.toObject();
    if (
      slope.value("classification").toString() == "descending"
      && slope.value("heightDeltaAlongRoute").toDouble() < 0.0)
    {
      foundDescendingRampBetween = true;
    }
  }
  CHECK(foundDescendingRampBetween);

  const auto selectedRampResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"name", "MCP: Selection slope sample"},
      {"select", true},
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "ramp_between"},
           {"start", QJsonArray{480, 32, 0}},
           {"end", QJsonArray{608, 32, 64}},
           {"width", 64},
           {"thickness", 16},
         },
       }},
    },
    history,
    nextOperationIndex);
  REQUIRE(selectedRampResponse.ok);

  const auto selectionSlopeResponse = geometryAnalyzeSlopesForMapResult(
    map,
    QJsonObject{{"routeDirection", QJsonArray{1, 0, 0}}, {"detail", "full"}},
    history);
  REQUIRE(selectionSlopeResponse.ok);
  CHECK(selectionSlopeResponse.result.value("passed").toBool());
  CHECK(selectionSlopeResponse.result.value("source").toString() == "selection");
  CHECK(selectionSlopeResponse.result.value("targetSource").toString() == "selection");
  CHECK(
    selectionSlopeResponse.result.value("targetBrushCount").toInt()
    == static_cast<int>(map.selection().nodes.size()));
  CHECK(selectionSlopeResponse.result.value("slopeCount").toInt() >= 1);

  const auto brokenRampResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"name", "MCP: Broken ramp continuity sample"},
      {"grid", 16},
      {"select", true},
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{640, 0, 0}},
           {"max", QJsonArray{768, 96, 16}},
         },
         QJsonObject{
           {"type", "ramp_between"},
           {"start", QJsonArray{768, 48, 16}},
           {"end", QJsonArray{1024, 48, 96}},
           {"width", 96},
           {"thickness", 16},
         },
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{1024, 0, 96}},
           {"max", QJsonArray{1152, 96, 112}},
         },
       }},
    },
    history,
    nextOperationIndex);
  REQUIRE(brokenRampResponse.ok);

  const auto brokenContinuityResponse = geometryAnalyzeRouteContinuityForMapResult(
    map,
    QJsonObject{
      {"operationId", brokenRampResponse.result.value("operationId").toString()},
      {"start", QJsonArray{640, 48, 16}},
      {"end", QJsonArray{1152, 48, 112}},
      {"detail", "full"},
    },
    history);
  REQUIRE(brokenContinuityResponse.ok);
  CHECK_FALSE(brokenContinuityResponse.result.value("passed").toBool());
  CHECK(
    brokenContinuityResponse.result.value("recoveryAction").toString()
    == "inspect_failing_seam_sample_then_fix_route_geometry");
  CHECK_FALSE(brokenContinuityResponse.result.value("continuous").toBool());
  const auto brokenSeams = brokenContinuityResponse.result.value("seams").toArray();
  REQUIRE(brokenSeams.size() == 2);
  const auto brokenTopSeam = brokenSeams[1].toObject();
  CHECK(brokenTopSeam.value("classification").toString() == "step_up");
  CHECK(brokenTopSeam.value("verticalStep").toDouble() == 16.0);

  const auto continuousRampResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"name", "MCP: Continuous ramp sample"},
      {"grid", 16},
      {"select", true},
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{1280, 0, 0}},
           {"max", QJsonArray{1408, 96, 16}},
         },
         QJsonObject{
           {"type", "ramp_between"},
           {"start", QJsonArray{1408, 48, 16}},
           {"end", QJsonArray{1664, 48, 96}},
           {"width", 96},
           {"thickness", 16},
         },
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{1664, 0, 80}},
           {"max", QJsonArray{1792, 96, 96}},
         },
       }},
    },
    history,
    nextOperationIndex);
  REQUIRE(continuousRampResponse.ok);

  const auto continuousResponse = geometryAnalyzeRouteContinuityForMapResult(
    map,
    QJsonObject{
      {"operationId", continuousRampResponse.result.value("operationId").toString()},
      {"start", QJsonArray{1280, 48, 16}},
      {"end", QJsonArray{1792, 48, 96}},
      {"detail", "full"},
    },
    history);
  REQUIRE(continuousResponse.ok);
  CHECK(continuousResponse.result.value("continuous").toBool());
  const auto continuousSeams = continuousResponse.result.value("seams").toArray();
  REQUIRE(continuousSeams.size() == 2);
  for (const auto& seamValue : continuousSeams)
  {
    const auto seam = seamValue.toObject();
    CHECK(seam.value("classification").toString() == "continuous");
    CHECK(seam.value("verticalStep").toDouble() == 0.0);
  }

  const auto selectionContinuityResponse = geometryAnalyzeRouteContinuityForMapResult(
    map,
    QJsonObject{
      {"start", QJsonArray{1280, 48, 16}},
      {"end", QJsonArray{1792, 48, 96}},
      {"detail", "full"},
    },
    history);
  REQUIRE(selectionContinuityResponse.ok);
  CHECK(selectionContinuityResponse.result.value("source").toString() == "selection");
  CHECK(
    selectionContinuityResponse.result.value("targetSource").toString() == "selection");
  CHECK(selectionContinuityResponse.result.value("targetBrushCount").toInt() == 3);
  CHECK(selectionContinuityResponse.result.value("continuous").toBool());

  const auto overlapRampResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"name", "MCP: Overlap continuity sample"},
      {"grid", 16},
      {"select", true},
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{1920, 0, 0}},
           {"max", QJsonArray{2048, 96, 16}},
         },
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{2032, 0, 0}},
           {"max", QJsonArray{2160, 96, 16}},
         },
       }},
    },
    history,
    nextOperationIndex);
  REQUIRE(overlapRampResponse.ok);
  const auto overlapContinuityResponse = geometryAnalyzeRouteContinuityForMapResult(
    map,
    QJsonObject{
      {"operationId", overlapRampResponse.result.value("operationId").toString()},
      {"start", QJsonArray{1920, 48, 16}},
      {"end", QJsonArray{2160, 48, 16}},
      {"detail", "full"},
    },
    history);
  REQUIRE(overlapContinuityResponse.ok);
  CHECK(overlapContinuityResponse.result.value("continuous").toBool());
  const auto overlapSeams = overlapContinuityResponse.result.value("seams").toArray();
  REQUIRE(overlapSeams.size() == 1);
  const auto overlapSeam = overlapSeams.first().toObject();
  CHECK(overlapSeam.value("classification").toString() == "overlap_continuous_height");
  CHECK(overlapSeam.value("seamRelation").toString() == "overlap");
  CHECK(overlapSeam.value("positiveGap").toDouble() == 0.0);
  CHECK(overlapSeam.value("overlapDepth").toDouble() == 16.0);
  CHECK(
    overlapSeam.value("edgeEndpointDistanceMax").toDouble()
    == overlapSeam.value("edgeGapMax").toDouble());
  CHECK(overlapSeam.value("continuous").toBool());
  CHECK(overlapContinuityResponse.result.value("maxPositiveGap").toDouble() == 0.0);
  CHECK(overlapContinuityResponse.result.value("maxOverlapDepth").toDouble() == 16.0);
  CHECK(overlapContinuityResponse.result.value("strictSurfaceContinuous").toBool());
  CHECK(overlapContinuityResponse.result.value("walkableContinuous").toBool());
  CHECK(overlapContinuityResponse.result.value("acceptancePassed").toBool());
  CHECK(overlapContinuityResponse.result.value("passScope")
          .toArray()
          .contains("walkable_surface_continuity"));

  const auto ribbonResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"name", "MCP: Path ribbon"},
      {"grid", 16},
      {"select", true},
      {"detail", "ids"},
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "path_ribbon"},
           {"points2d",
            QJsonArray{QJsonArray{256, 0}, QJsonArray{512, 0}, QJsonArray{512, 256}}},
           {"width", 128},
           {"minZ", 0},
           {"maxZ", 16},
         },
       }},
    },
    history,
    nextOperationIndex);
  const auto ribbonError =
    ribbonResponse.ok ? std::string{} : ribbonResponse.error.message.toStdString();
  INFO(ribbonError);
  REQUIRE(ribbonResponse.ok);
  CHECK(ribbonResponse.result.value("brushCount").toInt() == 2);
  CHECK(ribbonResponse.result.value("changedObjectCount").toInt() == 2);
  CHECK(ribbonResponse.result.value("changedObjectIds").toArray().size() == 2);
  CHECK(ribbonResponse.result.value("validation").toObject().value("valid").toBool());
  CHECK(map.selection().nodes.size() == 2u);

  const auto repeatResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"name", "MCP: Repeat boxes"},
      {"grid", 16},
      {"select", true},
      {"detail", "ids"},
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "repeat_translate"},
           {"count", 4},
           {"offset", QJsonArray{128, 0, 0}},
           {"operation",
            QJsonObject{
              {"type", "box"},
              {"min", QJsonArray{640, 0, 0}},
              {"max", QJsonArray{704, 64, 16}},
            }},
         },
       }},
    },
    history,
    nextOperationIndex);
  const auto repeatError =
    repeatResponse.ok ? std::string{} : repeatResponse.error.message.toStdString();
  INFO(repeatError);
  REQUIRE(repeatResponse.ok);
  CHECK(repeatResponse.result.value("brushCount").toInt() == 4);
  CHECK(repeatResponse.result.value("changedObjectCount").toInt() == 4);
  CHECK(repeatResponse.result.value("changedObjectIds").toArray().size() == 4);
  CHECK(repeatResponse.result.value("validation").toObject().value("valid").toBool());
  CHECK(map.selection().nodes.size() == 4u);

  const auto repeatGridResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"name", "MCP: Repeat grid"},
      {"grid", 16},
      {"select", true},
      {"detail", "ids"},
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "repeat_grid"},
           {"counts", QJsonArray{4, 3}},
           {"offsets",
            QJsonArray{
              QJsonArray{128, 0, 0},
              QJsonArray{0, 0, 64},
            }},
           {"operation",
            QJsonObject{
              {"type", "box"},
              {"min", QJsonArray{1024, 0, 0}},
              {"max", QJsonArray{1088, 32, 32}},
            }},
         },
       }},
    },
    history,
    nextOperationIndex);
  const auto repeatGridError = repeatGridResponse.ok
                                 ? std::string{}
                                 : repeatGridResponse.error.message.toStdString();
  INFO(repeatGridError);
  REQUIRE(repeatGridResponse.ok);
  CHECK(repeatGridResponse.result.value("brushCount").toInt() == 12);
  CHECK(repeatGridResponse.result.value("changedObjectCount").toInt() == 12);
  CHECK(repeatGridResponse.result.value("changedObjectIds").toArray().size() == 12);
  CHECK(repeatGridResponse.result.value("validation").toObject().value("valid").toBool());
  CHECK(map.selection().nodes.size() == 12u);

  const auto repeatGridShorthandResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"name", "MCP: Repeat grid shorthand"},
      {"grid", 16},
      {"select", true},
      {"detail", "ids"},
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "repeat_grid"},
           {"counts", 6},
           {"offsets", QJsonArray{96, 0, 0}},
           {"operation",
            QJsonObject{
              {"type", "box"},
              {"min", QJsonArray{1024, 512, 0}},
              {"max", QJsonArray{1088, 544, 32}},
            }},
         },
       }},
    },
    history,
    nextOperationIndex);
  const auto repeatGridShorthandError =
    repeatGridShorthandResponse.ok
      ? std::string{}
      : repeatGridShorthandResponse.error.message.toStdString();
  INFO(repeatGridShorthandError);
  REQUIRE(repeatGridShorthandResponse.ok);
  CHECK(repeatGridShorthandResponse.result.value("brushCount").toInt() == 6);
  CHECK(repeatGridShorthandResponse.result.value("changedObjectCount").toInt() == 6);
  CHECK(
    repeatGridShorthandResponse.result.value("changedObjectIds").toArray().size() == 6);
  CHECK(repeatGridShorthandResponse.result.value("validation")
          .toObject()
          .value("valid")
          .toBool());
  CHECK(map.selection().nodes.size() == 6u);

  const auto steppedMassResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"name", "MCP: Stepped mass"},
      {"grid", 16},
      {"select", true},
      {"detail", "ids"},
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "stepped_mass"},
           {"min", QJsonArray{-512, -512, 0}},
           {"max", QJsonArray{512, 512, 64}},
           {"levels", 5},
           {"inset", 96},
           {"stepHeight", 64},
         },
       }},
    },
    history,
    nextOperationIndex);
  const auto steppedMassError = steppedMassResponse.ok
                                  ? std::string{}
                                  : steppedMassResponse.error.message.toStdString();
  INFO(steppedMassError);
  REQUIRE(steppedMassResponse.ok);
  CHECK(steppedMassResponse.result.value("brushCount").toInt() == 5);
  CHECK(steppedMassResponse.result.value("changedObjectCount").toInt() == 5);
  CHECK(steppedMassResponse.result.value("changedObjectIds").toArray().size() == 5);
  CHECK(
    steppedMassResponse.result.value("validation").toObject().value("valid").toBool());
  CHECK(map.selection().nodes.size() == 5u);

  const auto supportPostsResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"name", "MCP: Support posts"},
      {"grid", 16},
      {"select", true},
      {"detail", "ids"},
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "support_posts_between"},
           {"points2d",
            QJsonArray{QJsonArray{0, 0}, QJsonArray{128, 0}, QJsonArray{0, 128}}},
           {"bottomZ", 0},
           {"topZ", 192},
           {"postSize", 32},
         },
       }},
    },
    history,
    nextOperationIndex);
  const auto supportPostsError = supportPostsResponse.ok
                                   ? std::string{}
                                   : supportPostsResponse.error.message.toStdString();
  INFO(supportPostsError);
  REQUIRE(supportPostsResponse.ok);
  CHECK(supportPostsResponse.result.value("brushCount").toInt() == 3);
  CHECK(supportPostsResponse.result.value("changedObjectCount").toInt() == 3);
  CHECK(supportPostsResponse.result.value("changedObjectIds").toArray().size() == 3);
  CHECK(
    supportPostsResponse.result.value("validation").toObject().value("valid").toBool());
  CHECK(map.selection().nodes.size() == 3u);

  const auto historyResponse = historyListResult(history);
  REQUIRE(historyResponse.ok);
  CHECK(
    historyResponse.result.value("count").toInt() == static_cast<int>(history.size()));
  const auto historyOperation =
    historyResponse.result.value("operations").toArray().first().toObject();
  CHECK(historyOperation.value("operationId").toString() == operationId);
  CHECK(!historyOperation.value("createdAt").toString().isEmpty());
  CHECK(historyOperation.value("createdAtMs").toDouble() > 0.0);
  CHECK(
    historyOperation.value("changedObjectCount").toInt()
    == batchResponse.result.value("changedObjectCount").toInt());

  const auto descendantCountBeforeInvalid = map.worldNode().descendantCount();
  const auto invalidResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{0, 0, 0}},
           {"max", QJsonArray{0, 64, 16}},
         },
       }},
    },
    history,
    nextOperationIndex);
  REQUIRE(invalidResponse.ok);
  const auto invalidValidation = invalidResponse.result.value("validation").toObject();
  CHECK(!invalidValidation.value("valid").toBool());
  CHECK(
    invalidResponse.result.value("expansion")
      .toObject()
      .value("expandedOperationCount")
      .toInt()
    == 1);
  CHECK(invalidValidation.value("failedOperationIndex").toInt() == 0);
  CHECK(invalidValidation.value("failedOperationType").toString() == "box");
  CHECK(invalidValidation.value("compiledOperationCount").toInt() == 0);
  CHECK(invalidValidation.value("compiledBrushCount").toInt() == 0);
  CHECK(
    invalidValidation.value("failedOperationPreview").toObject().value("type").toString()
    == "box");
  CHECK(map.worldNode().descendantCount() == descendantCountBeforeInvalid);

  for (const auto* removedType :
       {"room", "corridor", "sky_shell", "doorway", "cover", "stairs"})
  {
    const auto removedTypeResponse = blockoutCreateBatchForMapResult(
      map,
      "blockout_create_batch",
      QJsonObject{
        {"operations",
         QJsonArray{
           QJsonObject{
             {"type", removedType},
             {"min", QJsonArray{0, 0, 0}},
             {"max", QJsonArray{128, 128, 128}},
           },
         }},
      },
      history,
      nextOperationIndex);
    REQUIRE(removedTypeResponse.ok);
    const auto removedTypeValidation =
      removedTypeResponse.result.value("validation").toObject();
    CHECK_FALSE(removedTypeValidation.value("valid").toBool());
    CHECK(removedTypeValidation.value("failedOperationIndex").toInt() == 0);
    CHECK(removedTypeValidation.value("failedOperationType").toString() == removedType);
    const auto removedTypeErrors = removedTypeValidation.value("errors").toArray();
    REQUIRE(removedTypeErrors.size() == 1);
    CHECK(removedTypeErrors.first().toString().contains(
      QString{"%1 moved to trenchbroom-mcp-scene-workflow recipes"}.arg(removedType)));
    CHECK(map.worldNode().descendantCount() == descendantCountBeforeInvalid);

    const auto validateRemovedType = blockoutValidateResult(QJsonObject{
      {"type", removedType},
      {"min", QJsonArray{0, 0, 0}},
      {"max", QJsonArray{128, 128, 128}},
    });
    REQUIRE(validateRemovedType.ok);
    CHECK_FALSE(validateRemovedType.result.value("valid").toBool());
    const auto validateRemovedTypeErrors =
      validateRemovedType.result.value("errors").toArray();
    REQUIRE(validateRemovedTypeErrors.size() == 1);
    CHECK(validateRemovedTypeErrors.first().toString().contains(
      QString{"%1 moved to trenchbroom-mcp-scene-workflow recipes"}.arg(removedType)));
  }

  const auto partiallyInvalidResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{2048, 0, 0}},
           {"max", QJsonArray{2112, 64, 16}},
         },
         QJsonObject{
           {"type", "cylinder"},
           {"min", QJsonArray{2200, 0, 0}},
           {"max", QJsonArray{2200, 64, 128}},
         },
       }},
    },
    history,
    nextOperationIndex);
  REQUIRE(partiallyInvalidResponse.ok);
  const auto partiallyInvalidValidation =
    partiallyInvalidResponse.result.value("validation").toObject();
  CHECK(!partiallyInvalidValidation.value("valid").toBool());
  CHECK(partiallyInvalidValidation.value("failedOperationIndex").toInt() == 1);
  CHECK(partiallyInvalidValidation.value("failedOperationType").toString() == "cylinder");
  CHECK(partiallyInvalidValidation.value("compiledOperationCount").toInt() == 1);
  CHECK(partiallyInvalidValidation.value("compiledBrushCount").toInt() == 1);
  CHECK(
    partiallyInvalidValidation.value("failedOperationPreview")
      .toObject()
      .value("type")
      .toString()
    == "cylinder");
  CHECK(map.worldNode().descendantCount() == descendantCountBeforeInvalid);

  const auto invalidRibbonResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "path_ribbon"},
           {"points2d", QJsonArray{QJsonArray{0, 0}, QJsonArray{0, 0}}},
           {"width", 128},
           {"minZ", 0},
           {"maxZ", 16},
         },
       }},
    },
    history,
    nextOperationIndex);
  REQUIRE(invalidRibbonResponse.ok);
  CHECK(
    !invalidRibbonResponse.result.value("validation").toObject().value("valid").toBool());
  CHECK(map.worldNode().descendantCount() == descendantCountBeforeInvalid);

  const auto invalidSteppedMassResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "stepped_mass"},
           {"min", QJsonArray{0, 0, 0}},
           {"max", QJsonArray{128, 128, 16}},
           {"levels", 4},
           {"inset", 64},
           {"stepHeight", 16},
         },
       }},
    },
    history,
    nextOperationIndex);
  REQUIRE(invalidSteppedMassResponse.ok);
  const auto invalidSteppedMassValidation =
    invalidSteppedMassResponse.result.value("validation").toObject();
  CHECK(!invalidSteppedMassValidation.value("valid").toBool());
  CHECK(invalidSteppedMassValidation.value("failedOperationIndex").toInt() == 0);
  CHECK(
    invalidSteppedMassValidation.value("failedOperationType").toString()
    == "stepped_mass");
  CHECK(
    invalidSteppedMassValidation.value("errors").toArray().first().toString().contains(
      "collapsed"));
  CHECK(map.worldNode().descendantCount() == descendantCountBeforeInvalid);

  const auto invalidSupportPostsResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "support_posts_between"},
           {"points2d", QJsonArray{QJsonArray{0, 0}}},
           {"bottomZ", 128},
           {"topZ", 64},
           {"postSize", 16},
         },
       }},
    },
    history,
    nextOperationIndex);
  REQUIRE(invalidSupportPostsResponse.ok);
  const auto invalidSupportPostsValidation =
    invalidSupportPostsResponse.result.value("validation").toObject();
  CHECK(!invalidSupportPostsValidation.value("valid").toBool());
  CHECK(invalidSupportPostsValidation.value("failedOperationIndex").toInt() == 0);
  CHECK(
    invalidSupportPostsValidation.value("failedOperationType").toString()
    == "support_posts_between");
  CHECK(!invalidSupportPostsValidation.value("errors").toArray().isEmpty());
  CHECK(map.worldNode().descendantCount() == descendantCountBeforeInvalid);

  const auto invalidRepeatZeroOffsetResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "repeat_translate"},
           {"count", 2},
           {"offset", QJsonArray{0, 0, 0}},
           {"operation",
            QJsonObject{
              {"type", "box"},
              {"min", QJsonArray{0, 0, 0}},
              {"max", QJsonArray{64, 64, 16}},
            }},
         },
       }},
    },
    history,
    nextOperationIndex);
  REQUIRE(invalidRepeatZeroOffsetResponse.ok);
  const auto invalidRepeatZeroOffsetValidation =
    invalidRepeatZeroOffsetResponse.result.value("validation").toObject();
  CHECK(!invalidRepeatZeroOffsetValidation.value("valid").toBool());
  CHECK(invalidRepeatZeroOffsetValidation.value("failedOperationIndex").toInt() == 0);
  CHECK(
    invalidRepeatZeroOffsetValidation.value("failedOperationType").toString()
    == "repeat_translate");
  CHECK(invalidRepeatZeroOffsetValidation.value("errors")
          .toArray()
          .first()
          .toString()
          .contains("offset"));
  CHECK(map.worldNode().descendantCount() == descendantCountBeforeInvalid);

  const auto invalidRepeatGridZeroOffsetResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "repeat_grid"},
           {"counts", QJsonArray{2, 3}},
           {"offsets",
            QJsonArray{
              QJsonArray{0, 0, 0},
              QJsonArray{0, 0, 64},
            }},
           {"operation",
            QJsonObject{
              {"type", "box"},
              {"min", QJsonArray{0, 0, 0}},
              {"max", QJsonArray{64, 64, 16}},
            }},
         },
       }},
    },
    history,
    nextOperationIndex);
  REQUIRE(invalidRepeatGridZeroOffsetResponse.ok);
  const auto invalidRepeatGridZeroOffsetValidation =
    invalidRepeatGridZeroOffsetResponse.result.value("validation").toObject();
  CHECK(!invalidRepeatGridZeroOffsetValidation.value("valid").toBool());
  CHECK(invalidRepeatGridZeroOffsetValidation.value("failedOperationIndex").toInt() == 0);
  CHECK(
    invalidRepeatGridZeroOffsetValidation.value("failedOperationType").toString()
    == "repeat_grid");
  CHECK(invalidRepeatGridZeroOffsetValidation.value("errors")
          .toArray()
          .first()
          .toString()
          .contains("offsets[0]"));
  CHECK(map.worldNode().descendantCount() == descendantCountBeforeInvalid);

  const auto invalidRepeatResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "repeat_translate"},
           {"count", 0},
           {"offset", QJsonArray{128, 0, 0}},
           {"operation",
            QJsonObject{
              {"type", "box"},
              {"min", QJsonArray{3000, 0, 0}},
              {"max", QJsonArray{3064, 64, 16}},
            }},
         },
       }},
    },
    history,
    nextOperationIndex);
  REQUIRE(invalidRepeatResponse.ok);
  const auto invalidRepeatValidation =
    invalidRepeatResponse.result.value("validation").toObject();
  CHECK(!invalidRepeatValidation.value("valid").toBool());
  CHECK(invalidRepeatValidation.value("failedOperationIndex").toInt() == 0);
  CHECK(
    invalidRepeatValidation.value("failedOperationType").toString()
    == "repeat_translate");
  CHECK(invalidRepeatValidation.value("errors").toArray().first().toString().contains(
    "count"));
  CHECK(map.worldNode().descendantCount() == descendantCountBeforeInvalid);

  const auto inspectResponse = operationInspectResult(
    history, QJsonObject{{"operationId", operationId}, {"detail", "ids"}});
  REQUIRE(inspectResponse.ok);
  CHECK(
    inspectResponse.result.value("changedObjectIds").toArray().size()
    == batchResponse.result.value("changedObjectCount").toInt());
  CHECK(!inspectResponse.result.value("createdAt").toString().isEmpty());
  CHECK(inspectResponse.result.value("createdAtMs").toDouble() > 0.0);

  mdl::deselectAll(map);

  const auto curvedResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_curved_corridor",
    QJsonObject{
      {"center", QJsonArray{256, 0, 0}},
      {"innerRadius", 64},
      {"outerRadius", 128},
      {"startAngle", 0},
      {"turnDegrees", 90},
      {"height", 96},
      {"segments", 3},
      {"caps", "both"},
      {"select", true},
    },
    history,
    nextOperationIndex);

  const auto curvedError =
    curvedResponse.ok ? std::string{} : curvedResponse.error.message.toStdString();
  INFO(curvedError);
  REQUIRE(curvedResponse.ok);
  CHECK(curvedResponse.result.value("brushCount").toInt() == 14);
  CHECK(curvedResponse.result.value("changedObjectIds").isUndefined());
  CHECK(curvedResponse.result.value("validation").toObject().value("valid").toBool());
  const auto curvedExpansion = curvedResponse.result.value("expansion").toObject();
  CHECK(curvedExpansion.value("sourceOperationCount").toInt() == 1);
  CHECK(curvedExpansion.value("expandedOperationCount").toInt() == 14);
  CHECK(curvedExpansion.value("segments").toInt() == 3);
  CHECK(
    curvedExpansion.value("brushCountByPart").toObject().value("outer_wall").toInt()
    == 3);
  CHECK(curvedExpansion.value("brushCountByPart").toObject().value("floor").toInt() == 3);
  CHECK(map.selection().nodes.size() == 14u);

  auto curvedMetadataStore = std::map<QString, McpBrushMetadataRecord>{};
  auto curvedModuleStore = std::map<QString, McpModuleRecord>{};
  auto curvedObjectRegistry = McpObjectRegistry{};
  const auto metadataCurvedResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_curved_corridor",
    QJsonObject{
      {"center", QJsonArray{384, 256, 0}},
      {"innerRadius", 64},
      {"outerRadius", 128},
      {"turnDegrees", 90},
      {"segments", 2},
      {"select", false},
      {"metadata", QJsonObject{{"moduleId", "curved-selector"}, {"routeId", "arc-a"}}},
    },
    history,
    nextOperationIndex,
    &curvedMetadataStore,
    &curvedModuleStore,
    &curvedObjectRegistry);
  REQUIRE(metadataCurvedResponse.ok);
  CHECK(metadataCurvedResponse.result.value("metadataCount").toInt() == 8);
  const auto floorPreview = selectorPreviewForMapResult(
    map,
    QJsonObject{
      {"selector", QJsonObject{{"moduleId", "curved-selector"}, {"role", "walkable"}}},
    },
    history,
    curvedMetadataStore,
    curvedModuleStore,
    curvedObjectRegistry);
  REQUIRE(floorPreview.ok);
  CHECK(floorPreview.result.value("matchedCount").toInt() == 2);
  const auto wallPreview = selectorPreviewForMapResult(
    map,
    QJsonObject{
      {"selector", QJsonObject{{"moduleId", "curved-selector"}, {"part", "outer_wall"}}},
    },
    history,
    curvedMetadataStore,
    curvedModuleStore,
    curvedObjectRegistry);
  REQUIRE(wallPreview.ok);
  CHECK(wallPreview.result.value("matchedCount").toInt() == 2);

  const auto radialCurvedResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_curved_corridor",
    QJsonObject{
      {"center", QJsonArray{512, 0, 0}},
      {"innerRadius", 64},
      {"outerRadius", 128},
      {"startAngle", 0},
      {"turnDegrees", 90},
      {"height", 96},
      {"segments", 3},
      {"caps", "none"},
      {"snapMode", "radial"},
      {"grid", 16},
      {"select", true},
      {"detail", "full"},
    },
    history,
    nextOperationIndex);

  const auto radialCurvedError = radialCurvedResponse.ok
                                   ? std::string{}
                                   : radialCurvedResponse.error.message.toStdString();
  INFO(radialCurvedError);
  REQUIRE(radialCurvedResponse.ok);
  CHECK(radialCurvedResponse.result.value("brushCount").toInt() == 12);
  CHECK(
    radialCurvedResponse.result.value("validation").toObject().value("valid").toBool());
  CHECK(radialCurvedResponse.result.value("grid").toDouble() == 16.0);
  CHECK(map.selection().nodes.size() == 12u);

  const auto radialInspectResponse = operationInspectResult(
    history,
    QJsonObject{
      {"operationId", radialCurvedResponse.result.value("operationId").toString()},
      {"detail", "full"},
    });
  REQUIRE(radialInspectResponse.ok);
  const auto radialInput = radialInspectResponse.result.value("operationDetail")
                             .toObject()
                             .value("input")
                             .toObject();
  const auto radialOperation =
    radialInput.value("operations").toArray().first().toObject();
  CHECK(radialOperation.value("snapMode").toString() == "radial");
  CHECK(radialInput.value("grid").toDouble() == 16.0);
  CHECK(
    radialInspectResponse.result.value("summary")
      .toObject()
      .value("expansion")
      .toObject()
      .value("expandedOperationCount")
      .toInt()
    == 12);
  CHECK(radialInspectResponse.result.value("expandedOperations").toArray().size() == 12);
  CHECK(!radialInspectResponse.result.value("expandedOperationsTruncated").toBool());

  const auto invalidTurnResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_curved_corridor",
    QJsonObject{
      {"center", QJsonArray{640, 0, 0}},
      {"innerRadius", 64},
      {"outerRadius", 128},
      {"turnDegrees", 361},
    },
    history,
    nextOperationIndex);
  REQUIRE(invalidTurnResponse.ok);
  CHECK(invalidTurnResponse.result.value("validation")
          .toObject()
          .value("errors")
          .toArray()
          .first()
          .toString()
          .contains("360"));

  const auto invalidWallResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_curved_corridor",
    QJsonObject{
      {"center", QJsonArray{700, 0, 0}},
      {"innerRadius", 64},
      {"outerRadius", 128},
      {"turnDegrees", 90},
      {"wallThickness", 0},
    },
    history,
    nextOperationIndex);
  REQUIRE(invalidWallResponse.ok);
  CHECK(invalidWallResponse.result.value("validation")
          .toObject()
          .value("errors")
          .toArray()
          .first()
          .toString()
          .contains("inner_wall"));

  const auto invalidSnapResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_curved_corridor",
    QJsonObject{
      {"center", QJsonArray{768, 0, 0}},
      {"innerRadius", 64},
      {"outerRadius", 128},
      {"turnDegrees", 90},
      {"snapMode", "wobbly"},
    },
    history,
    nextOperationIndex);
  REQUIRE(invalidSnapResponse.ok);
  const auto invalidSnapValidation =
    invalidSnapResponse.result.value("validation").toObject();
  CHECK(!invalidSnapValidation.value("valid").toBool());
  CHECK(invalidSnapValidation.value("errors").toArray().first().toString().contains(
    "snapMode"));

  const auto terracedCurvedPreview = blockoutCompilePreviewForMapResult(
    map,
    QJsonObject{
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "curved_corridor"},
           {"center", QJsonArray{1024, 0, 0}},
           {"innerRadius", 64},
           {"outerRadius", 128},
           {"turnDegrees", 90},
           {"height", 96},
           {"segments", 4},
           {"slopeStartZ", 0},
           {"slopeEndZ", 64},
         },
       }},
    });
  REQUIRE(terracedCurvedPreview.ok);
  CHECK(terracedCurvedPreview.result.value("warnings")
          .toArray()
          .first()
          .toString()
          .contains("terracedCurvedCorridor"));

  const auto flatRibbonPreview = blockoutCompilePreviewForMapResult(
    map,
    QJsonObject{
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "path_ribbon"},
           {"points3d", QJsonArray{QJsonArray{0, 1024, 0}, QJsonArray{256, 1024, 64}}},
           {"width", 96},
           {"thickness", 16},
         },
       }},
    });
  REQUIRE(flatRibbonPreview.ok);
  CHECK(flatRibbonPreview.result.value("warnings")
          .toArray()
          .first()
          .toString()
          .contains("flatPoints3dPathRibbon"));
}

TEST_CASE(
  "McpBridgeServer geometry analysis accepts selectors and semantic modes",
  "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  auto metadataStore = std::map<QString, McpBrushMetadataRecord>{};
  auto moduleStore = std::map<QString, McpModuleRecord>{};
  auto objectRegistry = McpObjectRegistry{};

  const auto routeResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"name", "MCP: Selector route"},
      {"grid", 16},
      {"select", false},
      {"detail", "ids"},
      {"defaultMetadata",
       QJsonObject{
         {"moduleId", "selector-route"},
         {"generatedBy", "test"},
         {"role", "walkable"},
         {"routeId", "selector-route-a"},
       }},
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{0, 0, 0}},
           {"max", QJsonArray{128, 96, 16}},
           {"metadata", QJsonObject{{"part", "platform"}, {"order", 1}}},
         },
         QJsonObject{
           {"type", "ramp_between"},
           {"start", QJsonArray{128, 48, 16}},
           {"end", QJsonArray{384, 48, 64}},
           {"width", 96},
           {"thickness", 16},
           {"metadata", QJsonObject{{"part", "ramp"}, {"order", 2}}},
         },
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{384, 0, 48}},
           {"max", QJsonArray{512, 96, 64}},
           {"metadata", QJsonObject{{"part", "platform"}, {"order", 3}}},
         },
       }},
    },
    history,
    nextOperationIndex,
    &metadataStore,
    &moduleStore);
  REQUIRE(routeResponse.ok);

  const auto selectorContinuity = geometryAnalyzeRouteContinuityForMapResult(
    map,
    QJsonObject{
      {"selector", QJsonObject{{"moduleId", "selector-route"}, {"role", "walkable"}}},
      {"start", QJsonArray{0, 48, 16}},
      {"end", QJsonArray{512, 48, 64}},
    },
    history,
    &objectRegistry,
    &metadataStore,
    &moduleStore);
  REQUIRE(selectorContinuity.ok);
  CHECK(selectorContinuity.result.value("selectorMatchedCount").toInt() == 3);
  CHECK(selectorContinuity.result.value("continuous").toBool());
  CHECK(selectorContinuity.result.value("detail").toString() == "summary");
  CHECK(selectorContinuity.result.value("seams").isUndefined());
  CHECK(selectorContinuity.result.value("surfaceSample").toArray().size() == 3);
  CHECK(selectorContinuity.result.value("seamSample").toArray().size() == 2);
  CHECK_FALSE(selectorContinuity.result.value("seamSample")
                .toArray()
                .first()
                .toObject()
                .contains("edge"));
  CHECK(selectorContinuity.result.value("mixedTargetWarning").isUndefined());

  const auto mixedContinuity = geometryAnalyzeRouteContinuityForMapResult(
    map,
    QJsonObject{
      {"operationId", routeResponse.result.value("operationId").toString()},
      {"start", QJsonArray{0, 48, 16}},
      {"end", QJsonArray{512, 48, 64}},
    },
    history,
    &objectRegistry,
    &metadataStore,
    &moduleStore);
  REQUIRE(mixedContinuity.ok);
  CHECK(mixedContinuity.result.value("mixedTargetWarning")
          .toString()
          .contains("explicit selector"));
  CHECK(
    mixedContinuity.result.value("partCounts").toObject().value("platform").toInt() == 2);
  CHECK(mixedContinuity.result.value("partCounts").toObject().value("ramp").toInt() == 1);
  CHECK(mixedContinuity.result.value("recommendedSelector").isObject());

  const auto selectorSlopes = geometryAnalyzeSlopesForMapResult(
    map,
    QJsonObject{
      {"selector", QJsonObject{{"moduleId", "selector-route"}, {"part", "ramp"}}},
      {"routeDirection", QJsonArray{1, 0, 0}},
    },
    history,
    &objectRegistry,
    &metadataStore,
    &moduleStore);
  REQUIRE(selectorSlopes.ok);
  CHECK(selectorSlopes.result.value("passed").toBool());
  CHECK(selectorSlopes.result.value("selectorMatchedCount").toInt() == 1);
  CHECK(selectorSlopes.result.value("slopeCount").toInt() >= 1);
  CHECK(selectorSlopes.result.value("detail").toString() == "summary");
  CHECK(selectorSlopes.result.value("slopes").isUndefined());
  CHECK(selectorSlopes.result.value("slopeSample").toArray().size() >= 1);
  CHECK(selectorSlopes.result.value("ascendingCount").toInt() >= 1);

  const auto objectIds = routeResponse.result.value("changedObjectIds").toArray();
  REQUIRE(objectIds.size() == 3);
  const auto steppedResponse = geometryAnalyzeRouteContinuityForMapResult(
    map,
    QJsonObject{
      {"objectIds", QJsonArray{objectIds[0], objectIds[2]}},
      {"start", QJsonArray{0, 48, 16}},
      {"end", QJsonArray{512, 48, 64}},
      {"continuityMode", "stepped"},
      {"maxStepHeight", 48},
      {"horizontalTolerance", 512},
    },
    history,
    &objectRegistry);
  REQUIRE(steppedResponse.ok);
  CHECK(steppedResponse.result.value("passed").toBool());
  CHECK_FALSE(steppedResponse.result.value("continuous").toBool());
  CHECK(steppedResponse.result.value("semanticContinuous").toBool());

  const auto jumpRouteResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"name", "MCP: Jump gap route"},
      {"grid", 16},
      {"select", false},
      {"detail", "ids"},
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{1024, 0, 0}},
           {"max", QJsonArray{1152, 96, 16}},
         },
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{1280, 0, 0}},
           {"max", QJsonArray{1408, 96, 16}},
         },
       }},
    },
    history,
    nextOperationIndex);
  REQUIRE(jumpRouteResponse.ok);
  const auto jumpGapResponse = geometryAnalyzeRouteContinuityForMapResult(
    map,
    QJsonObject{
      {"operationId", jumpRouteResponse.result.value("operationId").toString()},
      {"start", QJsonArray{1024, 48, 16}},
      {"end", QJsonArray{1408, 48, 16}},
      {"continuityMode", "jump_gaps"},
      {"maxJumpGap", 128},
    },
    history,
    &objectRegistry);
  REQUIRE(jumpGapResponse.ok);
  CHECK_FALSE(jumpGapResponse.result.value("continuous").toBool());
  CHECK(jumpGapResponse.result.value("semanticContinuous").toBool());
  CHECK(jumpGapResponse.result.value("routeMode").toString() == "jump_chain");

  const auto jumpChainResponse = geometryAnalyzeRouteContinuityForMapResult(
    map,
    QJsonObject{
      {"operationId", jumpRouteResponse.result.value("operationId").toString()},
      {"start", QJsonArray{1024, 48, 16}},
      {"end", QJsonArray{1408, 48, 16}},
      {"routeMode", "jump_chain"},
      {"maxJumpGap", 128},
    },
    history,
    &objectRegistry);
  REQUIRE(jumpChainResponse.ok);
  CHECK_FALSE(jumpChainResponse.result.value("continuous").toBool());
  CHECK(jumpChainResponse.result.value("semanticContinuous").toBool());
  CHECK(jumpChainResponse.result.value("routeMode").toString() == "jump_chain");

  const auto coarseArcOperations = QJsonArray{
    QJsonObject{
      {"type", "arc_ramp"},
      {"center", QJsonArray{1600, 0, 0}},
      {"radius", 256},
      {"width", 96},
      {"startAngle", 0},
      {"turnDegrees", 90},
      {"rise", 128},
      {"segments", 4},
      {"thickness", 16},
    },
  };
  const auto balancedCurvePreview = blockoutCompilePreviewForMapResult(
    map, QJsonObject{{"operations", coarseArcOperations}});
  REQUIRE(balancedCurvePreview.ok);
  CHECK(balancedCurvePreview.result.value("valid").toBool());
  CHECK(balancedCurvePreview.result.value("qualityStatus").toString() == "warning");
  CHECK(balancedCurvePreview.result.value("acceptancePassed").toBool());
  CHECK(
    balancedCurvePreview.result.value("curveQuality")
      .toObject()
      .value("suggestedSegments")
      .toInt()
    > 4);

  const auto smoothCurvePreview = blockoutCompilePreviewForMapResult(
    map,
    QJsonObject{
      {"operations", coarseArcOperations},
      {"qualityPolicy", QJsonObject{{"intent", "smooth"}}},
    });
  REQUIRE(smoothCurvePreview.ok);
  CHECK_FALSE(smoothCurvePreview.result.value("valid").toBool());
  CHECK(smoothCurvePreview.result.value("qualityStatus").toString() == "failed");
  CHECK_FALSE(smoothCurvePreview.result.value("acceptancePassed").toBool());

  const auto gridCurvePreview = blockoutCompilePreviewForMapResult(
    map,
    QJsonObject{
      {"grid", 16},
      {"operations",
       QJsonArray{QJsonObject{
         {"type", "curved_corridor"},
         {"center", QJsonArray{0, 0, 0}},
         {"innerRadius", 128},
         {"outerRadius", 224},
         {"turnDegrees", 180},
         {"segments", 12},
         {"snapMode", "grid"},
       }}},
    });
  REQUIRE(gridCurvePreview.ok);
  CHECK(
    gridCurvePreview.result.value("curveQuality")
      .toObject()
      .value("maxSnapDisplacement")
      .toDouble()
    > 1.0);

  const auto radialCurvePreview = blockoutCompilePreviewForMapResult(
    map,
    QJsonObject{
      {"grid", 16},
      {"operations",
       QJsonArray{QJsonObject{
         {"type", "curved_corridor"},
         {"center", QJsonArray{0, 0, 0}},
         {"innerRadius", 128},
         {"outerRadius", 224},
         {"turnDegrees", 180},
         {"segments", 12},
         {"snapMode", "radial"},
       }}},
    });
  REQUIRE(radialCurvePreview.ok);
  CHECK(
    radialCurvePreview.result.value("curveQuality")
      .toObject()
      .value("maxSnapDisplacement")
      .toDouble()
    == 0.0);

  const auto arcRampResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"name", "MCP: Arc ramp"},
      {"detail", "ids"},
      {"defaultMetadata",
       QJsonObject{
         {"moduleId", "arc-ramp-module"},
         {"routeId", "arc-loop"},
         {"role", "walkable"},
       }},
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "arc_ramp"},
           {"center", QJsonArray{1600, 0, 0}},
           {"radius", 256},
           {"width", 96},
           {"startAngle", 0},
           {"turnDegrees", 90},
           {"rise", 128},
           {"segments", 8},
           {"thickness", 16},
           {"metadata", QJsonObject{{"part", "road"}, {"order", 100}}},
         },
       }},
    },
    history,
    nextOperationIndex,
    &metadataStore,
    &moduleStore);
  REQUIRE(arcRampResponse.ok);
  CHECK(arcRampResponse.result.value("brushCount").toInt() == 8);
  CHECK(arcRampResponse.result.value("changedObjectCount").toInt() == 8);

  const auto arcSlopes = geometryAnalyzeSlopesForMapResult(
    map,
    QJsonObject{
      {"operationId", arcRampResponse.result.value("operationId").toString()},
      {"routeDirection", QJsonArray{-1, 1, 0}},
    },
    history,
    &objectRegistry,
    &metadataStore,
    &moduleStore);
  REQUIRE(arcSlopes.ok);
  CHECK(arcSlopes.result.value("slopeCount").toInt() >= 1);

  const auto arcLoopByRouteId = geometryAnalyzeRouteContinuityForMapResult(
    map,
    QJsonObject{
      {"routeId", "arc-loop"},
      {"orderBy", "metadataOrder"},
      {"closedLoop", false},
      {"continuityMode", "stepped"},
      {"maxStepHeight", 256},
      {"horizontalTolerance", 1024},
      {"detail", "full"},
    },
    history,
    &objectRegistry,
    &metadataStore,
    &moduleStore);
  REQUIRE(arcLoopByRouteId.ok);
  CHECK(arcLoopByRouteId.result.value("selectorMatchedCount").toInt() == 8);
  CHECK(arcLoopByRouteId.result.value("orderBy").toString() == "metadataOrder");
  CHECK_FALSE(arcLoopByRouteId.result.value("closedLoop").toBool());
  CHECK(arcLoopByRouteId.result.value("warnings")
          .toArray()
          .contains("implicitSelectorFromTopLevelMetadata"));
  const auto arcLoopSeams = arcLoopByRouteId.result.value("seams").toArray();
  REQUIRE(!arcLoopSeams.isEmpty());
  CHECK(arcLoopByRouteId.result.value("maxEdgeGap").toDouble() < 0.001);
  CHECK(arcLoopByRouteId.result.value("fullWidthContinuous").toBool());
  CHECK(arcLoopByRouteId.result.value("centerlineContinuous").toBool());
  CHECK(arcLoopByRouteId.result.value("walkableContinuous").toBool());
  CHECK(arcLoopByRouteId.result.value("acceptancePassed").toBool());
  CHECK(arcLoopByRouteId.result.value("qualityStatus").toString() == "passed");
  CHECK(
    arcLoopByRouteId.result.value("directionMetricMethod").toString()
    == "centerline_polyline");
  CHECK(arcLoopByRouteId.result.value("maxDirectionChangeDegrees").toDouble() > 0.0);
  for (const auto& seamValue : arcLoopSeams)
  {
    const auto seam = seamValue.toObject();
    CHECK(seam.value("fullWidthContinuous").toBool());
    CHECK(seam.value("edgeGapMax").toDouble() < 0.001);
    CHECK(seam.value("innerEdgeGap").toDouble() < 0.001);
    CHECK(seam.value("outerEdgeGap").toDouble() < 0.001);
  }

  const auto arcClosedLoopResponse = geometryAnalyzeRouteContinuityForMapResult(
    map,
    QJsonObject{
      {"routeId", "arc-loop"},
      {"orderBy", "metadataOrder"},
      {"routeMode", "closed_loop"},
      {"maxStepHeight", 256},
      {"horizontalTolerance", 1024},
      {"detail", "full"},
    },
    history,
    &objectRegistry,
    &metadataStore,
    &moduleStore);
  REQUIRE(arcClosedLoopResponse.ok);
  CHECK(arcClosedLoopResponse.result.value("routeMode").toString() == "closed_loop");
  CHECK(arcClosedLoopResponse.result.value("closedLoop").toBool());
  const auto arcClosedLoopSeams = arcClosedLoopResponse.result.value("seams").toArray();
  REQUIRE(!arcClosedLoopSeams.isEmpty());
  CHECK(arcClosedLoopSeams.last().toObject().value("loopClosure").toBool());

  const auto mismatchedArcResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"name", "MCP: Mismatched arc seam"},
      {"detail", "ids"},
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "arc_ramp_segment"},
           {"center", QJsonArray{2200, 0, 0}},
           {"innerRadius", 200},
           {"outerRadius", 300},
           {"startAngle", 0},
           {"endAngle", 10},
           {"startZ", 0},
           {"endZ", 16},
           {"thickness", 16},
         },
         QJsonObject{
           {"type", "arc_ramp_segment"},
           {"center", QJsonArray{2200, 0, 0}},
           {"innerRadius", 232},
           {"outerRadius", 332},
           {"startAngle", 10},
           {"endAngle", 20},
           {"startZ", 16},
           {"endZ", 32},
           {"thickness", 16},
         },
       }},
    },
    history,
    nextOperationIndex,
    &metadataStore,
    &moduleStore);
  REQUIRE(mismatchedArcResponse.ok);
  const auto mismatchedContinuity = geometryAnalyzeRouteContinuityForMapResult(
    map,
    QJsonObject{
      {"operationId", mismatchedArcResponse.result.value("operationId").toString()},
      {"orderBy", "metadataOrder"},
      {"routeDirection", QJsonArray{0, 1, 0}},
      {"horizontalTolerance", 1},
    },
    history,
    &objectRegistry,
    &metadataStore,
    &moduleStore);
  REQUIRE(mismatchedContinuity.ok);
  CHECK_FALSE(mismatchedContinuity.result.value("passed").toBool());
  CHECK_FALSE(mismatchedContinuity.result.value("continuous").toBool());
  CHECK(mismatchedContinuity.result.value("centerlineContinuous").toBool());
  CHECK_FALSE(mismatchedContinuity.result.value("fullWidthContinuous").toBool());
  CHECK(mismatchedContinuity.result.value("failingSeamCount").toInt() == 1);
  CHECK(mismatchedContinuity.result.value("semanticFailingSeamCount").toInt() == 1);
  CHECK(mismatchedContinuity.result.value("failingSeamSample").toArray().size() == 1);
  CHECK_FALSE(mismatchedContinuity.result.value("failingSeamSample")
                .toArray()
                .first()
                .toObject()
                .contains("edge"));
  CHECK(mismatchedContinuity.result.value("maxEdgeGap").toDouble() > 1.0);
  CHECK(mismatchedContinuity.result.value("warnings")
          .toArray()
          .contains(
            "fullWidthRouteNotContinuous: centerline seam continuity passed, but inner/"
            "outer playable edges do not meet within tolerance."));

  const auto closedLoopResponse = geometryAnalyzeRouteContinuityForMapResult(
    map,
    QJsonObject{
      {"selector", QJsonObject{{"moduleId", "selector-route"}, {"role", "walkable"}}},
      {"orderBy", "metadataOrder"},
      {"closedLoop", true},
      {"routeDirection", QJsonArray{1, 0, 0}},
      {"continuityMode", "stepped"},
      {"maxStepHeight", 128},
      {"horizontalTolerance", 1024},
      {"detail", "full"},
    },
    history,
    &objectRegistry,
    &metadataStore,
    &moduleStore);
  REQUIRE(closedLoopResponse.ok);
  CHECK(closedLoopResponse.result.value("orderBy").toString() == "metadataOrder");
  CHECK(closedLoopResponse.result.value("closedLoop").toBool());
  const auto seams = closedLoopResponse.result.value("seams").toArray();
  REQUIRE(!seams.isEmpty());
  CHECK(seams.last().toObject().value("loopClosure").toBool());
}

TEST_CASE("McpBridgeServer geometry_analyze_shell_seams", "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  auto metadataStore = std::map<QString, McpBrushMetadataRecord>{};
  auto moduleStore = std::map<QString, McpModuleRecord>{};
  auto objectRegistry = McpObjectRegistry{};

  const auto seam = [](const QString& key, const int x) {
    return QJsonObject{
      {"key", key},
      {"vertices",
       QJsonArray{
         QJsonArray{x, 0, 0},
         QJsonArray{x, 64, 0},
         QJsonArray{x, 64, 96},
         QJsonArray{x, 0, 96},
       }},
    };
  };

  const auto continuousShell = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"name", "MCP: Annotated shell seam"},
      {"detail", "ids"},
      {"defaultMetadata",
       QJsonObject{
         {"moduleId", "shell-seam-pass"},
         {"routeId", "shell-route-a"},
         {"recipe", "path_tunnel"},
       }},
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{0, 0, 0}},
           {"max", QJsonArray{128, 64, 96}},
           {"metadata",
            QJsonObject{
              {"part", "wall_left"},
              {"role", "shell"},
              {"order", 1},
              {"shellSeams", QJsonArray{seam("section:1:left", 128)}},
            }},
         },
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{128, 0, 0}},
           {"max", QJsonArray{256, 64, 96}},
           {"metadata",
            QJsonObject{
              {"part", "wall_left"},
              {"role", "shell"},
              {"order", 2},
              {"shellSeams", QJsonArray{seam("section:1:left", 128)}},
            }},
         },
       }},
    },
    history,
    nextOperationIndex,
    &metadataStore,
    &moduleStore,
    &objectRegistry);
  REQUIRE(continuousShell.ok);

  const auto continuousAnalysis = geometryAnalyzeShellSeamsForMapResult(
    map,
    QJsonObject{
      {"operationId", continuousShell.result.value("operationId").toString()},
      {"detail", "full"},
    },
    history,
    &objectRegistry,
    &metadataStore,
    &moduleStore);
  REQUIRE(continuousAnalysis.ok);
  CHECK(continuousAnalysis.result.value("shellContinuous").toBool());
  CHECK(continuousAnalysis.result.value("acceptancePassed").toBool());
  CHECK(continuousAnalysis.result.value("targetBrushCount").toInt() == 2);
  CHECK(continuousAnalysis.result.value("annotatedBrushCount").toInt() == 2);
  CHECK(continuousAnalysis.result.value("seamCount").toInt() == 1);
  CHECK(continuousAnalysis.result.value("failingSeamCount").toInt() == 0);
  CHECK(continuousAnalysis.result.value("maxVertexDelta").toDouble() == 0.0);
  CHECK(continuousAnalysis.result.value("seams").toArray().size() == 1);

  const auto gappedShell = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"name", "MCP: Gapped annotated shell seam"},
      {"detail", "ids"},
      {"defaultMetadata",
       QJsonObject{
         {"moduleId", "shell-seam-fail"},
         {"routeId", "shell-route-b"},
         {"recipe", "path_tunnel"},
       }},
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{512, 0, 0}},
           {"max", QJsonArray{640, 64, 96}},
           {"metadata",
            QJsonObject{
              {"part", "wall_right"},
              {"role", "shell"},
              {"order", 1},
              {"shellSeams", QJsonArray{seam("section:1:right", 640)}},
            }},
         },
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{641, 0, 0}},
           {"max", QJsonArray{769, 64, 96}},
           {"metadata",
            QJsonObject{
              {"part", "wall_right"},
              {"role", "shell"},
              {"order", 2},
              {"shellSeams", QJsonArray{seam("section:1:right", 641)}},
            }},
         },
       }},
    },
    history,
    nextOperationIndex,
    &metadataStore,
    &moduleStore,
    &objectRegistry);
  REQUIRE(gappedShell.ok);

  const auto gappedAnalysis = geometryAnalyzeShellSeamsForMapResult(
    map,
    QJsonObject{{"operationId", gappedShell.result.value("operationId").toString()}},
    history,
    &objectRegistry,
    &metadataStore,
    &moduleStore);
  REQUIRE(gappedAnalysis.ok);
  CHECK_FALSE(gappedAnalysis.result.value("shellContinuous").toBool());
  CHECK_FALSE(gappedAnalysis.result.value("acceptancePassed").toBool());
  CHECK(gappedAnalysis.result.value("failingSeamCount").toInt() > 0);
  CHECK(gappedAnalysis.result.value("maxVertexDelta").toDouble() > 0.0);
  CHECK(gappedAnalysis.result.value("failingSeamSample").toArray().size() == 1);
  CHECK(
    gappedAnalysis.result.value("recoveryAction").toString()
    == "inspect_failing_seam_sample_then_rebuild_annotated_shell");

  const auto unannotatedShell = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"name", "MCP: Unannotated shell"},
      {"detail", "ids"},
      {"operations",
       QJsonArray{QJsonObject{
         {"type", "box"},
         {"min", QJsonArray{1024, 0, 0}},
         {"max", QJsonArray{1152, 64, 96}},
       }}},
    },
    history,
    nextOperationIndex,
    &metadataStore,
    &moduleStore,
    &objectRegistry);
  REQUIRE(unannotatedShell.ok);

  const auto unannotatedAnalysis = geometryAnalyzeShellSeamsForMapResult(
    map,
    QJsonObject{{"operationId", unannotatedShell.result.value("operationId").toString()}},
    history,
    &objectRegistry,
    &metadataStore,
    &moduleStore);
  CHECK_FALSE(unannotatedAnalysis.ok);
  CHECK(
    (unannotatedAnalysis.error.message.contains("shellSeams")
     || unannotatedAnalysis.error.message.contains("annotated")));
  CHECK(
    unannotatedAnalysis.error.details.value("recoveryAction").toString()
    == "use_annotated_recipe_or_visual_review");
  CHECK_FALSE(unannotatedAnalysis.error.details.value("mutatedDocument").toBool());
  CHECK(unannotatedAnalysis.error.details.value("retrySafe").toBool());
}

TEST_CASE("McpBridgeServer route metadata tools", "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  auto metadataStore = std::map<QString, McpBrushMetadataRecord>{};

  SECTION("lists supported route platform shapes")
  {
    const auto response = shapeLibraryListResult();

    REQUIRE(response.ok);
    const auto shapes = response.result.value("shapes").toArray();
    auto names = QStringList{};
    for (const auto& shape : shapes)
    {
      names.push_back(shape.toObject().value("name").toString());
    }

    CHECK(names.contains("diamond"));
    CHECK(names.contains("trapezoid"));
    CHECK(names.contains("chamfered_rect"));
    CHECK(names.contains("half_hex"));
    CHECK(names.contains("arrowhead"));
    CHECK(names.contains("slanted_plank"));
  }

  SECTION("creates convex polygon platforms in one transaction and stores metadata")
  {
    const auto descendantCountBefore = map.worldNode().descendantCount();
    const auto response = brushCreatePolygonBatchForMapResult(
      map,
      "brush_create_polygon_batch",
      QJsonObject{
        {"transactionName", "MCP: Route polygon platforms"},
        {"grid", 16},
        {"select", true},
        {"detail", "summary"},
        {"brushes",
         QJsonArray{
           QJsonObject{
             {"points2d",
              QJsonArray{
                QJsonArray{0, 32},
                QJsonArray{32, 0},
                QJsonArray{64, 32},
                QJsonArray{32, 64}}},
             {"minZ", 0},
             {"maxZ", 16},
             {"material", "mcp_floor_a"},
             {"metadata",
              QJsonObject{
                {"routeId", "intro"},
                {"movementType", "bhop"},
                {"outgoingDirection", QJsonArray{1, 0, 0}},
              }},
           },
           QJsonObject{
             {"points2d",
              QJsonArray{
                QJsonArray{160, 0},
                QJsonArray{240, 0},
                QJsonArray{224, 64},
                QJsonArray{176, 64}}},
             {"minZ", 0},
             {"maxZ", 16},
             {"material", "mcp_floor_b"},
             {"metadata",
              QJsonObject{
                {"routeId", "intro"},
                {"intent", "landing_bias"},
                {"movementType", "bhop"},
              }},
           },
           QJsonObject{
             {"points2d",
              QJsonArray{
                QJsonArray{320, 0},
                QJsonArray{384, 0},
                QJsonArray{400, 16},
                QJsonArray{400, 48},
                QJsonArray{384, 64},
                QJsonArray{320, 64},
                QJsonArray{304, 48},
                QJsonArray{304, 16}}},
             {"minZ", 16},
             {"maxZ", 32},
             {"material", "mcp_floor_a"},
             {"metadata",
              QJsonObject{
                {"routeId", "intro"},
                {"difficulty", "easy"},
                {"movementType", "bhop"},
              }},
           },
         }},
      },
      history,
      nextOperationIndex,
      metadataStore);

    const auto error = response.ok ? std::string{} : response.error.message.toStdString();
    INFO(error);
    REQUIRE(response.ok);
    CHECK(
      response.result.value("transactionName").toString()
      == "MCP: Route polygon platforms");
    CHECK(response.result.value("brushCount").toInt() == 3);
    CHECK(response.result.value("changedObjectCount").toInt() == 3);
    CHECK(response.result.value("changedObjectIds").isUndefined());
    CHECK(response.result.value("metadataCount").toInt() == 3);
    CHECK(response.result.value("validation").toObject().value("valid").toBool());
    const auto materials = response.result.value("materials").toArray();
    auto materialNames = QStringList{};
    for (const auto& material : materials)
    {
      materialNames.push_back(material.toString());
    }
    CHECK(materialNames.contains("mcp_floor_a"));
    CHECK(materialNames.contains("mcp_floor_b"));
    CHECK(!materialNames.contains("__TB_empty"));
    CHECK(map.selection().nodes.size() == 3u);
    REQUIRE(map.undoCommandName() != nullptr);
    CHECK(
      QString::fromStdString(*map.undoCommandName()) == "MCP: Route polygon platforms");

    const auto inspect = operationInspectResult(
      history,
      QJsonObject{
        {"operationId", response.result.value("operationId").toString()},
        {"detail", "ids"},
      });
    REQUIRE(inspect.ok);
    CHECK(inspect.result.value("changedObjectIds").toArray().size() == 3);
    CHECK(metadataStore.size() == 3u);

    map.undoCommand();
    CHECK(map.worldNode().descendantCount() == descendantCountBefore);
  }

  SECTION("reports operation and metadata stale diagnostics")
  {
    const auto response = brushCreatePolygonBatchForMapResult(
      map,
      "brush_create_polygon_batch",
      QJsonObject{
        {"detail", "ids"},
        {"select", false},
        {"brushes",
         QJsonArray{
           QJsonObject{
             {"points2d",
              QJsonArray{
                QJsonArray{0, 0},
                QJsonArray{64, 0},
                QJsonArray{64, 64},
                QJsonArray{0, 64}}},
             {"minZ", 0},
             {"maxZ", 16},
             {"metadata",
              QJsonObject{
                {"routeId", "stale_probe"},
                {"movementType", "bhop"},
              }},
           },
         }},
      },
      history,
      nextOperationIndex,
      metadataStore);

    const auto error = response.ok ? std::string{} : response.error.message.toStdString();
    INFO(error);
    REQUIRE(response.ok);
    const auto operationId = response.result.value("operationId").toString();
    const auto objectIds = response.result.value("changedObjectIds").toArray();
    REQUIRE(objectIds.size() == 1);

    map.undoCommand();

    auto registry = McpObjectRegistry{};
    const auto staleState =
      registry.liveStateJson(map, QStringList{objectIds.first().toString()}, false);
    CHECK(!staleState.value("valid").toBool());
    CHECK(staleState.value("staleObjectCount").toInt() == 1);
    CHECK(!staleState.value("staleReason").toString().isEmpty());

    const auto getResponse = brushMetadataGetForMapResult(
      map, QJsonObject{{"objectIds", objectIds}}, metadataStore);
    REQUIRE(getResponse.ok);
    CHECK(getResponse.result.value("staleCount").toInt() == 1);
    CHECK(getResponse.result.value("diagnostic").toString().contains("metadata records"));
    const auto object = getResponse.result.value("objects").toArray().first().toObject();
    CHECK(object.value("stale").toBool());
    CHECK(!object.value("staleReason").toString().isEmpty());

    const auto byMetadataResponse = selectionByMetadataForMapResult(
      map,
      QJsonObject{
        {"routeId", "stale_probe"},
        {"select", true},
      },
      metadataStore);
    REQUIRE(byMetadataResponse.ok);
    CHECK(byMetadataResponse.result.value("count").toInt() == 0);
    CHECK(byMetadataResponse.result.value("staleCount").toInt() == 1);
    CHECK(byMetadataResponse.result.value("suggestedAction")
            .toString()
            .contains("re-identify"));
  }

  SECTION("rejects invalid polygon batch without committing")
  {
    const auto descendantCount = map.worldNode().descendantCount();
    const auto missingBrushesResponse = brushCreatePolygonBatchForMapResult(
      map,
      "brush_create_polygon_batch",
      QJsonObject{},
      history,
      nextOperationIndex,
      metadataStore);
    CHECK(!missingBrushesResponse.ok);
    CHECK(
      missingBrushesResponse.error.details.value("mutatedDocument").toBool(true)
      == false);
    CHECK(missingBrushesResponse.error.details.value("retrySafe").toBool(false));
    CHECK(
      missingBrushesResponse.error.details.value("recoveryAction").toString()
      == "provide_polygon_brushes_then_retry");

    const auto invalidMetadataResponse = brushCreatePolygonBatchForMapResult(
      map,
      "brush_create_polygon_batch",
      QJsonObject{
        {"brushes",
         QJsonArray{
           QJsonObject{
             {"points2d",
              QJsonArray{QJsonArray{0, 0}, QJsonArray{64, 0}, QJsonArray{64, 64}}},
             {"metadata", "not an object"},
           },
         }},
      },
      history,
      nextOperationIndex,
      metadataStore);
    CHECK(!invalidMetadataResponse.ok);
    CHECK(
      invalidMetadataResponse.error.details.value("mutatedDocument").toBool(true)
      == false);
    CHECK(invalidMetadataResponse.error.details.value("retrySafe").toBool(false));
    CHECK(
      invalidMetadataResponse.error.details.value("recoveryAction").toString()
      == "fix_polygon_metadata_then_retry");

    const auto response = brushCreatePolygonBatchForMapResult(
      map,
      "brush_create_polygon_batch",
      QJsonObject{
        {"brushes",
         QJsonArray{
           QJsonObject{
             {"points2d",
              QJsonArray{
                QJsonArray{0, 0},
                QJsonArray{64, 0},
                QJsonArray{16, 16},
                QJsonArray{0, 64}}},
             {"minZ", 0},
             {"maxZ", 16},
           },
           QJsonObject{
             {"points2d",
              QJsonArray{
                QJsonArray{128, 0},
                QJsonArray{192, 0},
                QJsonArray{192, 64},
                QJsonArray{128, 64}}},
             {"minZ", 32},
             {"maxZ", 16},
           },
         }},
      },
      history,
      nextOperationIndex,
      metadataStore);

    REQUIRE(response.ok);
    const auto validation = response.result.value("validation").toObject();
    CHECK(!validation.value("valid").toBool());
    CHECK(response.result.value("mutatedDocument").toBool(true) == false);
    CHECK(response.result.value("retrySafe").toBool(false));
    CHECK(response.result.value("invalidPolygonCount").toInt() == 2);
    CHECK(validation.value("invalidPolygonCount").toInt() == 2);
    CHECK(response.result.value("firstInvalidPolygonIndex").toInt() == 0);
    const auto diagnostics = response.result.value("polygonDiagnostics").toArray();
    REQUIRE(diagnostics.size() == 2);
    CHECK(diagnostics[0].toObject().value("brushIndex").toInt() == 0);
    CHECK(diagnostics[0].toObject().value("reason").toString().contains("convex"));
    CHECK(!diagnostics[0].toObject().value("failingPointIndices").toArray().isEmpty());
    CHECK(diagnostics[1].toObject().value("brushIndex").toInt() == 1);
    CHECK(diagnostics[1].toObject().value("reason").toString().contains("minZ"));
    CHECK(map.worldNode().descendantCount() == descendantCount);
    CHECK(metadataStore.empty());
  }

  SECTION("sets, gets, selects, and analyzes route metadata")
  {
    const auto response = brushCreatePolygonBatchForMapResult(
      map,
      "brush_create_polygon_batch",
      QJsonObject{
        {"detail", "ids"},
        {"select", false},
        {"brushes",
         QJsonArray{
           QJsonObject{
             {"points2d",
              QJsonArray{
                QJsonArray{0, 0},
                QJsonArray{64, 0},
                QJsonArray{64, 64},
                QJsonArray{0, 64}}},
             {"minZ", 0},
             {"maxZ", 16},
             {"metadata",
              QJsonObject{
                {"routeId", "chain_a"},
                {"movementType", "bhop"},
                {"outgoingDirection", QJsonArray{1, 0, 0}},
              }},
           },
           QJsonObject{
             {"points2d",
              QJsonArray{
                QJsonArray{160, 0},
                QJsonArray{224, 0},
                QJsonArray{224, 64},
                QJsonArray{160, 64}}},
             {"minZ", 0},
             {"maxZ", 16},
             {"metadata",
              QJsonObject{
                {"routeId", "chain_a"},
                {"movementType", "bhop"},
                {"incomingDirection", QJsonArray{1, 0, 0}},
              }},
           },
         }},
      },
      history,
      nextOperationIndex,
      metadataStore);

    const auto error = response.ok ? std::string{} : response.error.message.toStdString();
    INFO(error);
    REQUIRE(response.ok);
    const auto objectIds = response.result.value("changedObjectIds").toArray();
    REQUIRE(objectIds.size() == 2);

    const auto firstObjectId = objectIds[0].toString();
    const auto setResponse = brushMetadataSetForMapResult(
      map,
      QJsonObject{
        {"objectIds", QJsonArray{firstObjectId}},
        {"metadata",
         QJsonObject{
           {"routeId", "chain_a"},
           {"intent", "takeoff"},
           {"difficulty", "easy"},
           {"movementType", "bhop"},
           {"outgoingDirection", QJsonArray{1, 0, 0}},
         }},
      },
      metadataStore);
    REQUIRE(setResponse.ok);
    CHECK(setResponse.result.value("count").toInt() == 1);
    CHECK(setResponse.result.value("mutatedDocument").toBool(true) == false);

    const auto missingTargetSetResponse = brushMetadataSetForMapResult(
      map,
      QJsonObject{
        {"objectIds", QJsonArray{"node:999"}},
        {"metadata", QJsonObject{{"routeId", "missing"}}},
      },
      metadataStore);
    REQUIRE_FALSE(missingTargetSetResponse.ok);
    CHECK(
      missingTargetSetResponse.error.details.value("mutatedDocument").toBool(true)
      == false);
    CHECK(missingTargetSetResponse.error.details.value("retrySafe").toBool(false));
    CHECK(
      missingTargetSetResponse.error.details.value("recoveryAction").toString()
      == "refresh_status_or_select_live_brushes");
    CHECK(metadataStore.size() == 2u);

    const auto getResponse = brushMetadataGetForMapResult(
      map, QJsonObject{{"objectIds", objectIds}}, metadataStore);
    REQUIRE(getResponse.ok);
    CHECK(getResponse.result.value("count").toInt() == 2);
    CHECK(
      getResponse.result.value("objects")
        .toArray()
        .first()
        .toObject()
        .value("metadata")
        .toObject()
        .value("intent")
        .toString()
      == "takeoff");

    const auto selectResponse = selectionByMetadataForMapResult(
      map,
      QJsonObject{
        {"routeId", "chain_a"},
        {"movementType", "bhop"},
        {"select", true},
      },
      metadataStore);
    REQUIRE(selectResponse.ok);
    CHECK(selectResponse.result.value("mutatedDocument").toBool(true) == false);
    CHECK(selectResponse.result.value("count").toInt() == 2);
    CHECK(map.selection().nodes.size() == 2u);

    const auto analyzeResponse = kzDistanceAnalyzeChainForMapResult(
      map,
      QJsonObject{
        {"objectIds", objectIds},
        {"movementType", "bhop"},
        {"playerHull", QJsonArray{32, 32, 72}},
      },
      metadataStore);
    REQUIRE(analyzeResponse.ok);
    CHECK(analyzeResponse.result.value("segmentCount").toInt() == 1);
    const auto segment =
      analyzeResponse.result.value("segments").toArray().first().toObject();
    CHECK(segment.value("edgeGap").toDouble() == 96.0);
    CHECK(segment.value("heightDelta").toDouble() == 0.0);
    CHECK(segment.value("verticalGap").toDouble() == 0.0);
    CHECK(segment.value("effectiveDistanceBadLanding").toDouble() > 96.0);
    CHECK(segment.value("usedMetadataDirection").toBool());

    const auto customSetResponse = brushMetadataSetForMapResult(
      map,
      QJsonObject{
        {"objectIds", QJsonArray{firstObjectId}},
        {"metadata",
         QJsonObject{
           {"routeId", "chain_a"},
           {"probeTag", "agent_probe"},
         }},
      },
      metadataStore);
    REQUIRE(customSetResponse.ok);
    CHECK(customSetResponse.result.value("mutatedDocument").toBool(true) == false);

    const auto customSelectResponse = selectionByMetadataForMapResult(
      map,
      QJsonObject{
        {"metadata", QJsonObject{{"probeTag", "agent_probe"}}},
        {"select", false},
      },
      metadataStore);
    REQUIRE(customSelectResponse.ok);
    CHECK(customSelectResponse.result.value("mutatedDocument").toBool(true) == false);
    CHECK(customSelectResponse.result.value("count").toInt() == 1);

    const auto topLevelCustomSelectResponse = selectionByMetadataForMapResult(
      map,
      QJsonObject{
        {"probeTag", "agent_probe"},
        {"select", false},
      },
      metadataStore);
    REQUIRE(topLevelCustomSelectResponse.ok);
    CHECK(
      topLevelCustomSelectResponse.result.value("mutatedDocument").toBool(true) == false);
    CHECK(topLevelCustomSelectResponse.result.value("count").toInt() == 1);
  }

  SECTION("distance analysis uses top-to-top height delta")
  {
    const auto response = brushCreatePolygonBatchForMapResult(
      map,
      "brush_create_polygon_batch",
      QJsonObject{
        {"detail", "ids"},
        {"select", false},
        {"brushes",
         QJsonArray{
           QJsonObject{
             {"points2d",
              QJsonArray{
                QJsonArray{0, 0},
                QJsonArray{64, 0},
                QJsonArray{64, 64},
                QJsonArray{0, 64}}},
             {"minZ", 0},
             {"maxZ", 16},
             {"metadata",
              QJsonObject{
                {"routeId", "height_chain"},
                {"movementType", "bhop"},
                {"outgoingDirection", QJsonArray{1, 0, 0}},
              }},
           },
           QJsonObject{
             {"points2d",
              QJsonArray{
                QJsonArray{160, 0},
                QJsonArray{224, 0},
                QJsonArray{224, 64},
                QJsonArray{160, 64}}},
             {"minZ", 24},
             {"maxZ", 40},
             {"metadata",
              QJsonObject{
                {"routeId", "height_chain"},
                {"movementType", "bhop"},
                {"incomingDirection", QJsonArray{1, 0, 0}},
              }},
           },
         }},
      },
      history,
      nextOperationIndex,
      metadataStore);

    const auto error = response.ok ? std::string{} : response.error.message.toStdString();
    INFO(error);
    REQUIRE(response.ok);
    const auto objectIds = response.result.value("changedObjectIds").toArray();

    const auto analyzeResponse = kzDistanceAnalyzeChainForMapResult(
      map,
      QJsonObject{
        {"objectIds", objectIds},
        {"movementType", "bhop"},
      },
      metadataStore);
    REQUIRE(analyzeResponse.ok);
    const auto segment =
      analyzeResponse.result.value("segments").toArray().first().toObject();
    CHECK(segment.value("heightDelta").toDouble() == 24.0);
    CHECK(segment.value("verticalGap").toDouble() == 8.0);
    CHECK(analyzeResponse.result.value("maxAbsHeightDelta").toDouble() == 24.0);
  }

  SECTION("routeId analysis uses metadata order when available")
  {
    const auto response = brushCreatePolygonBatchForMapResult(
      map,
      "brush_create_polygon_batch",
      QJsonObject{
        {"detail", "ids"},
        {"select", false},
        {"brushes",
         QJsonArray{
           QJsonObject{
             {"points2d",
              QJsonArray{
                QJsonArray{160, 0},
                QJsonArray{224, 0},
                QJsonArray{224, 64},
                QJsonArray{160, 64}}},
             {"minZ", 0},
             {"maxZ", 16},
             {"metadata",
              QJsonObject{
                {"routeId", "ordered_chain"},
                {"order", 2},
                {"movementType", "bhop"},
                {"incomingDirection", QJsonArray{1, 0, 0}},
              }},
           },
           QJsonObject{
             {"points2d",
              QJsonArray{
                QJsonArray{0, 0},
                QJsonArray{64, 0},
                QJsonArray{64, 64},
                QJsonArray{0, 64}}},
             {"minZ", 0},
             {"maxZ", 16},
             {"metadata",
              QJsonObject{
                {"routeId", "ordered_chain"},
                {"order", 1},
                {"movementType", "bhop"},
                {"outgoingDirection", QJsonArray{1, 0, 0}},
              }},
           },
         }},
      },
      history,
      nextOperationIndex,
      metadataStore);
    REQUIRE(response.ok);
    const auto objectIds = response.result.value("changedObjectIds").toArray();
    REQUIRE(objectIds.size() == 2);

    const auto analyzeResponse = routeGeometryAnalyzeChainForMapResult(
      map,
      QJsonObject{
        {"routeId", "ordered_chain"},
        {"orderBy", "metadata"},
        {"movementType", "bhop"},
      },
      metadataStore);
    REQUIRE(analyzeResponse.ok);
    const auto orderedIds = analyzeResponse.result.value("objectIds").toArray();
    REQUIRE(orderedIds.size() == 2);
    CHECK(orderedIds[0].toString() == objectIds[1].toString());
    CHECK(orderedIds[1].toString() == objectIds[0].toString());
    CHECK(analyzeResponse.result.value("warnings").toArray().isEmpty());
    const auto segment =
      analyzeResponse.result.value("segments").toArray().first().toObject();
    CHECK(segment.value("edgeGap").toDouble() == 96.0);
  }

  SECTION("undo skips metadata selection commands before reverting the MCP operation")
  {
    const auto descendantCountBefore = map.worldNode().descendantCount();
    const auto response = brushCreatePolygonBatchForMapResult(
      map,
      "brush_create_polygon_batch",
      QJsonObject{
        {"transactionName", "MCP: Route polygon platforms"},
        {"detail", "ids"},
        {"select", false},
        {"brushes",
         QJsonArray{
           QJsonObject{
             {"points2d",
              QJsonArray{
                QJsonArray{0, 0},
                QJsonArray{64, 0},
                QJsonArray{64, 64},
                QJsonArray{0, 64}}},
             {"minZ", 0},
             {"maxZ", 16},
             {"metadata",
              QJsonObject{
                {"routeId", "undo_chain"},
                {"movementType", "bhop"},
              }},
           },
           QJsonObject{
             {"points2d",
              QJsonArray{
                QJsonArray{160, 0},
                QJsonArray{224, 0},
                QJsonArray{224, 64},
                QJsonArray{160, 64}}},
             {"minZ", 0},
             {"maxZ", 16},
             {"metadata",
              QJsonObject{
                {"routeId", "undo_chain"},
                {"movementType", "bhop"},
              }},
           },
         }},
      },
      history,
      nextOperationIndex,
      metadataStore);

    const auto error = response.ok ? std::string{} : response.error.message.toStdString();
    INFO(error);
    REQUIRE(response.ok);
    CHECK(map.worldNode().descendantCount() == descendantCountBefore + 2u);

    const auto selectResponse = selectionByMetadataForMapResult(
      map,
      QJsonObject{
        {"routeId", "undo_chain"},
        {"select", true},
      },
      metadataStore);
    REQUIRE(selectResponse.ok);
    CHECK(map.selection().nodes.size() == 2u);
    REQUIRE(map.undoCommandName() != nullptr);
    CHECK(QString::fromStdString(*map.undoCommandName()) == "Select 2 Objects");

    const auto undoResponse = historyUndoForMapResult(map, history);
    REQUIRE(undoResponse.ok);
    CHECK(undoResponse.result.value("undone").toBool());
    CHECK(undoResponse.result.value("skippedSelectionCommands").toInt() == 1);
    CHECK(map.worldNode().descendantCount() == descendantCountBefore);
  }
}

TEST_CASE("McpBridgeServer checked entity batch", "[McpBridgeServer]")
{
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
  auto& map = document->map();
  map.entityDefinitionManager().setDefinitions({
    {"test_spawn", {}, "", {}, mdl::PointEntityDefinition{vm::bbox3d{16.0}, {}, {}}},
    {"test_light", {}, "", {}, mdl::PointEntityDefinition{vm::bbox3d{8.0}, {}, {}}},
  });
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;

  SECTION("creates multiple checked point entities in one transaction")
  {
    const auto descendantCountBefore = map.worldNode().descendantCount();
    const auto response = createEntityCheckedBatchForMapResult(
      map,
      "entity_create_checked_batch",
      QJsonObject{
        {"transactionName", "MCP: Test checked entity batch"},
        {"detail", "ids"},
        {"select", true},
        {"entities",
         QJsonArray{
           QJsonObject{
             {"classname", "test_spawn"},
             {"origin", QJsonArray{0, 0, 8}},
             {"properties", QJsonObject{{"angles", "0 90 0"}}},
           },
           QJsonObject{
             {"classname", "test_light"},
             {"origin", QJsonArray{128, 0, 128}},
             {"properties", QJsonObject{{"_light", "255 240 220 160"}}},
           },
           QJsonObject{
             {"classname", "test_light"},
             {"origin", QJsonArray{256, 0, 128}},
             {"properties", QJsonObject{{"_light", "220 240 255 120"}}},
           },
         }},
      },
      history,
      nextOperationIndex);

    const auto error = response.ok ? std::string{} : response.error.message.toStdString();
    INFO(error);
    REQUIRE(response.ok);
    CHECK(
      response.result.value("transactionName").toString()
      == "MCP: Test checked entity batch");
    CHECK(response.result.value("checked").toBool());
    CHECK(response.result.value("entityCount").toInt() == 3);
    CHECK(response.result.value("changedObjectCount").toInt() == 3);
    CHECK(response.result.value("changedObjectIds").toArray().size() == 3);
    CHECK(history.size() == 1u);
    CHECK(history.front().toolName == "entity_create_checked_batch");
    CHECK(map.selection().nodes.size() == 3u);
    CHECK(map.worldNode().descendantCount() == descendantCountBefore + 3u);
    REQUIRE(map.undoCommandName() != nullptr);
    CHECK(
      QString::fromStdString(*map.undoCommandName()) == "MCP: Test checked entity batch");

    map.undoCommand();
    CHECK(map.worldNode().descendantCount() == descendantCountBefore);
  }

  SECTION("rejects unknown class without committing")
  {
    const auto descendantCountBefore = map.worldNode().descendantCount();
    const auto missingEntitiesResponse = createEntityCheckedBatchForMapResult(
      map, "entity_create_checked_batch", QJsonObject{}, history, nextOperationIndex);
    CHECK(!missingEntitiesResponse.ok);
    CHECK(
      missingEntitiesResponse.error.details.value("mutatedDocument").toBool(true)
      == false);
    CHECK(missingEntitiesResponse.error.details.value("retrySafe").toBool(false));
    CHECK(
      missingEntitiesResponse.error.details.value("recoveryAction").toString()
      == "provide_entities_then_retry");

    const auto response = createEntityCheckedBatchForMapResult(
      map,
      "entity_create_checked_batch",
      QJsonObject{
        {"entities",
         QJsonArray{
           QJsonObject{{"classname", "test_light"}, {"origin", QJsonArray{0, 0, 128}}},
           QJsonObject{{"classname", "not_a_real_entity"}},
         }},
      },
      history,
      nextOperationIndex);

    CHECK(!response.ok);
    CHECK(response.error.code == mcp::McpErrorCode::InvalidParams);
    CHECK(response.error.details.value("mutatedDocument").toBool(true) == false);
    CHECK(response.error.details.value("retrySafe").toBool(false));
    CHECK(
      response.error.details.value("recoveryAction").toString()
      == "choose_defined_point_entity_classname_then_retry");
    CHECK(response.error.details.value("entityIndex").toInt(-1) == 1);
    CHECK(response.error.details.value("classname").toString() == "not_a_real_entity");
    CHECK(map.worldNode().descendantCount() == descendantCountBefore);
    CHECK(history.empty());
  }

  SECTION("updates and deletes entity properties by operation id")
  {
    const auto createResponse = createEntityCheckedBatchForMapResult(
      map,
      "entity_create_checked_batch",
      QJsonObject{
        {"detail", "ids"},
        {"entities",
         QJsonArray{
           QJsonObject{
             {"classname", "test_light"},
             {"origin", QJsonArray{64, 0, 128}},
             {"properties", QJsonObject{{"targetname", "temp_light"}, {"_light", "100"}}},
           },
         }},
      },
      history,
      nextOperationIndex);
    REQUIRE(createResponse.ok);

    auto objectRegistry = McpObjectRegistry{};
    const auto updateResponse = entityPropertiesUpdateForMapResult(
      map,
      "entity_properties_update",
      QJsonObject{
        {"operationIds", QJsonArray{createResponse.result.value("operationId")}},
        {"properties", QJsonObject{{"_light", "255 255 255 200"}}},
      },
      history,
      history,
      nextOperationIndex,
      objectRegistry);
    REQUIRE(updateResponse.ok);
    CHECK(updateResponse.result.value("mutatedDocument").toBool());
    CHECK(
      updateResponse.result.value("documentFingerprint").toString()
      == documentFingerprintForMap(map));
    CHECK(updateResponse.result.value("entityCount").toInt() == 1);
    CHECK(updateResponse.result.value("changedObjectCount").toInt() == 1);
    CHECK(updateResponse.result.value("changedObjectIds").isUndefined());
    CHECK(updateResponse.result.value("changedObjectIdSample").isUndefined());
    CHECK(history.back().documentFingerprint == documentFingerprintForMap(map));

    const auto deleteResponse = entityPropertiesDeleteForMapResult(
      map,
      "entity_properties_delete",
      QJsonObject{
        {"operationIds", QJsonArray{createResponse.result.value("operationId")}},
        {"keys", QJsonArray{"targetname"}},
        {"idsMode", "sample"},
      },
      history,
      history,
      nextOperationIndex,
      objectRegistry);
    REQUIRE(deleteResponse.ok);
    CHECK(deleteResponse.result.value("mutatedDocument").toBool());
    CHECK(deleteResponse.result.value("entityCount").toInt() == 1);
    CHECK(deleteResponse.result.value("changedObjectCount").toInt() == 1);
    CHECK(deleteResponse.result.value("changedObjectIds").isUndefined());
    CHECK(deleteResponse.result.value("changedObjectIdSample").toArray().size() == 1);

    const auto invalidUpdateResponse = entityPropertiesUpdateForMapResult(
      map,
      "entity_properties_update",
      QJsonObject{
        {"operationIds", QJsonArray{createResponse.result.value("operationId")}},
        {"properties", QJsonObject{}},
      },
      history,
      history,
      nextOperationIndex,
      objectRegistry);
    REQUIRE_FALSE(invalidUpdateResponse.ok);
    CHECK(
      invalidUpdateResponse.error.details.value("mutatedDocument").toBool(true) == false);
    CHECK(invalidUpdateResponse.error.details.value("retrySafe").toBool(false));
    CHECK(
      invalidUpdateResponse.error.details.value("recoveryAction").toString()
      == "add_properties_then_retry");

    const auto missingTargetDeleteResponse = entityPropertiesDeleteForMapResult(
      map,
      "entity_properties_delete",
      QJsonObject{
        {"operationIds", QJsonArray{"mcp-op-missing"}},
        {"keys", QJsonArray{"targetname"}},
      },
      history,
      history,
      nextOperationIndex,
      objectRegistry);
    REQUIRE_FALSE(missingTargetDeleteResponse.ok);
    CHECK(
      missingTargetDeleteResponse.error.details.value("mutatedDocument").toBool(true)
      == false);
    CHECK(
      missingTargetDeleteResponse.error.details.value("recoveryAction").toString()
      == "refresh_status_or_fix_entity_targets");

    const auto fullUpdateResponse = entityPropertiesUpdateForMapResult(
      map,
      "entity_properties_update",
      QJsonObject{
        {"operationIds", QJsonArray{createResponse.result.value("operationId")}},
        {"properties", QJsonObject{{"_light", "128 128 128 100"}}},
        {"idsMode", "full"},
      },
      history,
      history,
      nextOperationIndex,
      objectRegistry);
    REQUIRE(fullUpdateResponse.ok);
    CHECK(fullUpdateResponse.result.value("changedObjectIds").toArray().size() == 1);
  }
}

TEST_CASE("McpBridgeServer file based IR tools", "[McpBridgeServer]")
{
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
  auto& map = document->map();

  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  auto metadataStore = std::map<QString, McpBrushMetadataRecord>{};
  auto moduleStore = std::map<QString, McpModuleRecord>{};
  auto previewCache = std::map<QString, McpIrPreviewCacheRecord>{};
  auto nextPreviewIndex = 1;
  auto tempDir = QTemporaryDir{};
  REQUIRE(tempDir.isValid());
  const auto path = tempDir.filePath("scene-ir.json");
  auto file = QFile{path};
  REQUIRE(file.open(QIODevice::WriteOnly));
  file.write(QJsonDocument{
    QJsonObject{
      {"moduleId", "file-ir-module"},
      {"defaultMetadata", QJsonObject{{"moduleId", "file-ir-module"}}},
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{0, 0, 0}},
           {"max", QJsonArray{64, 64, 16}},
           {"metadata", QJsonObject{{"part", "floor"}}},
         },
       }},
    }}.toJson());
  file.close();

  const auto previewResponse = irCompilePreviewFromFileForMapResult(
    map, QJsonObject{{"path", path}}, &previewCache, &nextPreviewIndex);
  REQUIRE(previewResponse.ok);
  CHECK(
    previewResponse.result.value("tool").toString() == "ir_compile_preview_from_file");
  CHECK(previewResponse.result.value("schemaVersion").toInt() == 1);
  CHECK(
    previewResponse.result.value("warnings").toArray().contains("legacyUnversionedIr"));
  CHECK(!previewResponse.result.value("willCommit").toBool());
  CHECK(
    previewResponse.result.value("sourcePath").toString()
    == QFileInfo{path}.canonicalFilePath());
  CHECK(previewResponse.result.value("estimatedBrushCount").toInt() == 1);
  CHECK(previewResponse.result.value("cacheable").toBool());
  CHECK(previewResponse.result.value("previewId").toString() == "ir-preview-1");
  CHECK(previewResponse.result.value("irHash").toString().startsWith("sha256:"));
  CHECK(
    previewResponse.result.value("documentFingerprint").toString()
    == documentFingerprintForMap(map));
  CHECK(
    previewResponse.result.value("activeDocumentPath").toString()
    == QString::fromStdString(map.path().string()));
  CHECK(previewResponse.result.value("expiresAfterSeconds").toInt() == 600);
  CHECK(previewCache.size() == 1u);

  const auto descendantCountBeforeRejectedApply = map.worldNode().descendantCount();
  const auto missingPathApplyResponse = irApplyFromFileForMapResult(
    map,
    "ir_apply_from_file",
    QJsonObject{},
    history,
    nextOperationIndex,
    metadataStore,
    moduleStore);
  CHECK(!missingPathApplyResponse.ok);
  CHECK(
    missingPathApplyResponse.error.message
    == "ir_apply_from_file requires path or previewId");
  CHECK(
    missingPathApplyResponse.error.details.value("mutatedDocument").toBool(true)
    == false);
  CHECK(missingPathApplyResponse.error.details.value("retrySafe").toBool(false));
  CHECK(
    missingPathApplyResponse.error.details.value("recoveryAction").toString()
    == "provide_path_or_preview_id_then_retry");
  CHECK(map.worldNode().descendantCount() == descendantCountBeforeRejectedApply);

  const auto invalidApplyPath = tempDir.filePath("invalid-apply-ir.json");
  auto invalidApplyFile = QFile{invalidApplyPath};
  REQUIRE(invalidApplyFile.open(QIODevice::WriteOnly));
  invalidApplyFile.write(QJsonDocument{
    QJsonObject{
      {"operations",
       QJsonArray{
         QJsonObject{
           {"min", QJsonArray{0, 0, 0}},
           {"max", QJsonArray{64, 64, 16}},
         },
       }},
    }}.toJson());
  invalidApplyFile.close();
  const auto invalidApplyResponse = irApplyFromFileForMapResult(
    map,
    "ir_apply_from_file",
    QJsonObject{{"path", invalidApplyPath}},
    history,
    nextOperationIndex,
    metadataStore,
    moduleStore);
  CHECK(!invalidApplyResponse.ok);
  CHECK(invalidApplyResponse.error.message.contains("requires type"));
  CHECK(
    invalidApplyResponse.error.details.value("mutatedDocument").toBool(true) == false);
  CHECK(invalidApplyResponse.error.details.value("retrySafe").toBool(false));
  CHECK(
    invalidApplyResponse.error.details.value("recoveryAction").toString()
    == "fix_ir_file_or_preview_again");
  CHECK(
    invalidApplyResponse.error.details.value("sourcePath").toString()
    == invalidApplyPath);
  CHECK(map.worldNode().descendantCount() == descendantCountBeforeRejectedApply);

  const auto unavailableCacheResponse = irApplyFromFileForMapResult(
    map,
    "ir_apply_from_file",
    QJsonObject{{"previewId", previewResponse.result.value("previewId").toString()}},
    history,
    nextOperationIndex,
    metadataStore,
    moduleStore);
  CHECK(!unavailableCacheResponse.ok);
  CHECK(unavailableCacheResponse.error.message.contains("cache is unavailable"));
  CHECK(
    unavailableCacheResponse.error.details.value("mutatedDocument").toBool(true)
    == false);
  CHECK(unavailableCacheResponse.error.details.value("retrySafe").toBool(false));
  CHECK(
    unavailableCacheResponse.error.details.value("recoveryAction").toString()
    == "run_ir_compile_preview_from_file_again");
  CHECK(map.worldNode().descendantCount() == descendantCountBeforeRejectedApply);

  const auto expiredPreviewId = previewResponse.result.value("previewId").toString();
  auto expiredPreviewCache = previewCache;
  expiredPreviewCache[expiredPreviewId].expiresAtMs = 1;
  const auto expiredPreviewResponse = irApplyFromFileForMapResult(
    map,
    "ir_apply_from_file",
    QJsonObject{{"previewId", expiredPreviewId}},
    history,
    nextOperationIndex,
    metadataStore,
    moduleStore,
    nullptr,
    &expiredPreviewCache);
  CHECK(!expiredPreviewResponse.ok);
  CHECK(expiredPreviewResponse.error.message.contains("Unknown or expired"));
  CHECK(
    expiredPreviewResponse.error.details.value("mutatedDocument").toBool(true) == false);
  CHECK(expiredPreviewResponse.error.details.value("retrySafe").toBool(false));
  CHECK(
    expiredPreviewResponse.error.details.value("recoveryAction").toString()
    == "run_ir_compile_preview_from_file_again");

  auto otherDocument = MapDocument::createDocument(
                         appController.environmentConfig(),
                         mdl::QuakeGameInfo,
                         mdl::MapFormat::Valve,
                         vm::bbox3d{8192.0},
                         appController.taskManager(),
                         appController.glManager().resourceManager())
                       | kdl::value();
  const auto wrongDocumentResponse = irApplyFromFileForMapResult(
    otherDocument->map(),
    "ir_apply_from_file",
    QJsonObject{{"previewId", expiredPreviewId}},
    history,
    nextOperationIndex,
    metadataStore,
    moduleStore,
    nullptr,
    &previewCache);
  CHECK(!wrongDocumentResponse.ok);
  CHECK(wrongDocumentResponse.error.message.contains("different active document"));
  CHECK(
    wrongDocumentResponse.error.details.value("mutatedDocument").toBool(true) == false);
  CHECK(wrongDocumentResponse.error.details.value("retrySafe").toBool(false));
  CHECK(
    wrongDocumentResponse.error.details.value("recoveryAction").toString()
    == "activate_original_document_or_preview_again");

  const auto invalidPreviewPath = tempDir.filePath("invalid-preview-ir.json");
  auto invalidPreviewFile = QFile{invalidPreviewPath};
  REQUIRE(invalidPreviewFile.open(QIODevice::WriteOnly));
  invalidPreviewFile.write(QJsonDocument{
    QJsonObject{
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{0, 0, 0}},
           {"max", QJsonArray{0, 64, 16}},
         },
       }},
    }}.toJson());
  invalidPreviewFile.close();
  const auto invalidPreviewResponse = irCompilePreviewFromFileForMapResult(
    map, QJsonObject{{"path", invalidPreviewPath}}, &previewCache, &nextPreviewIndex);
  REQUIRE(invalidPreviewResponse.ok);
  CHECK(!invalidPreviewResponse.result.value("valid").toBool(true));
  CHECK(!invalidPreviewResponse.result.value("cacheable").toBool(true));
  CHECK(invalidPreviewResponse.result.value("previewId").isUndefined());
  CHECK(previewCache.size() == 1u);

  const auto cachedApplyResponse = irApplyFromFileForMapResult(
    map,
    "ir_apply_from_file",
    QJsonObject{
      {"previewId", previewResponse.result.value("previewId").toString()},
      {"idsMode", "count"},
    },
    history,
    nextOperationIndex,
    metadataStore,
    moduleStore,
    nullptr,
    &previewCache);
  REQUIRE(cachedApplyResponse.ok);
  CHECK(cachedApplyResponse.result.value("usedPreviewCache").toBool());
  CHECK(
    cachedApplyResponse.result.value("previewId").toString()
    == previewResponse.result.value("previewId").toString());
  CHECK(
    cachedApplyResponse.result.value("irHash").toString()
    == previewResponse.result.value("irHash").toString());
  CHECK(cachedApplyResponse.result.value("operationCount").toInt() == 1);
  CHECK(cachedApplyResponse.result.value("changedObjectCount").toInt() == 1);

  const auto applyResponse = irApplyFromFileForMapResult(
    map,
    "ir_apply_from_file",
    QJsonObject{{"path", path}, {"idsMode", "count"}},
    history,
    nextOperationIndex,
    metadataStore,
    moduleStore);
  REQUIRE(applyResponse.ok);
  CHECK(applyResponse.result.value("tool").toString() == "ir_apply_from_file");
  CHECK(applyResponse.result.value("schemaVersion").toInt() == 1);
  CHECK(applyResponse.result.value("warnings").toArray().contains("legacyUnversionedIr"));
  CHECK_FALSE(applyResponse.result.value("parentOperationId").toString().isEmpty());
  CHECK(
    applyResponse.result.value("sourcePath").toString()
    == QFileInfo{path}.canonicalFilePath());
  CHECK(applyResponse.result.value("operationCount").toInt() == 1);
  CHECK(applyResponse.result.value("changedObjectCount").toInt() == 1);
  CHECK(applyResponse.result.value("changedObjectIds").isUndefined());
  const auto applied = applyResponse.result.value("applied").toArray();
  REQUIRE(applied.size() == 1);
  CHECK(applied.at(0).toObject().value("changedObjectIds").isUndefined());

  const auto moduleResponse = moduleListForMapResult(
    map, QJsonObject{}, metadataStore, moduleStore, McpObjectRegistry{});
  REQUIRE(moduleResponse.ok);
  const auto modules = moduleResponse.result.value("modules").toArray();
  REQUIRE(modules.size() == 1);
  const auto module = modules.first().toObject();
  CHECK(module.value("moduleId").toString() == "file-ir-module");
  CHECK(module.value("liveObjectCount").toInt() == 2);
  CHECK(module.value("staleObjectCount").toInt() == 0);
  const auto parts = module.value("parts").toArray();
  REQUIRE(parts.size() == 1);
  CHECK(parts.first().toObject().value("part").toString() == "floor");
  CHECK(parts.first().toObject().value("count").toInt() == 2);

  const auto selectorResponse = selectorPreviewForMapResult(
    map,
    QJsonObject{
      {"selector", QJsonObject{{"moduleId", "file-ir-module"}, {"part", "floor"}}},
      {"idsMode", "count"},
    },
    history,
    metadataStore,
    moduleStore,
    McpObjectRegistry{});
  REQUIRE(selectorResponse.ok);
  CHECK(selectorResponse.result.value("matchedCount").toInt() == 2);
  CHECK(selectorResponse.result.value("staleExcluded").toInt() == 0);

  auto changedFile = QFile{path};
  REQUIRE(changedFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
  changedFile.write(QJsonDocument{
    QJsonObject{
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{0, 0, 0}},
           {"max", QJsonArray{0, 64, 16}},
         },
       }},
    }}.toJson());
  changedFile.close();

  const auto descendantCountBeforeChangedFileApply = map.worldNode().descendantCount();
  const auto changedFileApplyResponse = irApplyFromFileForMapResult(
    map,
    "ir_apply_from_file",
    QJsonObject{{"previewId", "ir-preview-1"}},
    history,
    nextOperationIndex,
    metadataStore,
    moduleStore,
    nullptr,
    &previewCache);
  CHECK(!changedFileApplyResponse.ok);
  CHECK(changedFileApplyResponse.error.code == mcp::McpErrorCode::InvalidParams);
  CHECK(changedFileApplyResponse.error.message.contains("changed after preview"));
  CHECK(
    changedFileApplyResponse.error.details.value("mutatedDocument").toBool(true)
    == false);
  CHECK(changedFileApplyResponse.error.details.value("retrySafe").toBool(false));
  CHECK(
    changedFileApplyResponse.error.details.value("recoveryAction").toString()
    == "preview_changed_ir_file_again");
  CHECK(map.worldNode().descendantCount() == descendantCountBeforeChangedFileApply);

  const auto unknownPreviewResponse = irApplyFromFileForMapResult(
    map,
    "ir_apply_from_file",
    QJsonObject{{"previewId", "ir-preview-missing"}},
    history,
    nextOperationIndex,
    metadataStore,
    moduleStore,
    nullptr,
    &previewCache);
  CHECK(!unknownPreviewResponse.ok);
  CHECK(unknownPreviewResponse.error.message.contains("Unknown or expired"));
  CHECK(
    unknownPreviewResponse.error.details.value("mutatedDocument").toBool(true) == false);
  CHECK(unknownPreviewResponse.error.details.value("retrySafe").toBool(false));
  CHECK(
    unknownPreviewResponse.error.details.value("recoveryAction").toString()
    == "run_ir_compile_preview_from_file_again");

  REQUIRE(changedFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
  changedFile.write(QJsonDocument{
    QJsonObject{
      {"moduleId", "file-ir-module"},
      {"defaultMetadata", QJsonObject{{"moduleId", "file-ir-module"}}},
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{0, 0, 0}},
           {"max", QJsonArray{64, 64, 16}},
           {"metadata", QJsonObject{{"part", "floor"}}},
         },
       }},
    }}.toJson());
  changedFile.close();

  const auto fullApplyResponse = irApplyFromFileForMapResult(
    map,
    "ir_apply_from_file",
    QJsonObject{{"path", path}, {"idsMode", "full"}},
    history,
    nextOperationIndex,
    metadataStore,
    moduleStore);
  REQUIRE(fullApplyResponse.ok);
  CHECK(fullApplyResponse.result.value("changedObjectCount").toInt() == 1);
  CHECK(fullApplyResponse.result.value("changedObjectIds").toArray().size() == 1);
  CHECK(
    fullApplyResponse.result.value("applied")
      .toArray()
      .at(0)
      .toObject()
      .value("changedObjectIds")
      .toArray()
      .size()
    == 1);
}

TEST_CASE(
  "McpBridgeServer file replace_module requires and reuses preview guards",
  "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  auto metadataStore = std::map<QString, McpBrushMetadataRecord>{};
  auto moduleStore = std::map<QString, McpModuleRecord>{};
  auto objectRegistry = McpObjectRegistry{};
  auto previewCache = std::map<QString, McpIrPreviewCacheRecord>{};
  auto nextPreviewIndex = 1;
  const auto create = irApplyForMapResult(
    map,
    "ir_apply",
    QJsonObject{
      {"ir",
       QJsonObject{
         {"moduleId", "file-replace-module"},
         {"defaultMetadata", QJsonObject{{"moduleId", "file-replace-module"}}},
         {"select", false},
         {"operations",
          QJsonArray{QJsonObject{
            {"type", "box"},
            {"min", QJsonArray{0, 0, 0}},
            {"max", QJsonArray{64, 64, 16}},
          }}},
       }},
    },
    history,
    nextOperationIndex,
    metadataStore,
    moduleStore,
    &objectRegistry);
  REQUIRE(create.ok);

  auto tempDir = QTemporaryDir{};
  REQUIRE(tempDir.isValid());
  const auto path = tempDir.filePath("replace-module.json");
  auto file = QFile{path};
  REQUIRE(file.open(QIODevice::WriteOnly));
  file.write(QJsonDocument{
    QJsonObject{
      {"schemaVersion", 1},
      {"applyMode", "replace_module"},
      {"moduleId", "file-replace-module"},
      {"defaultMetadata", QJsonObject{{"moduleId", "file-replace-module"}}},
      {"select", false},
      {"operations",
       QJsonArray{QJsonObject{
         {"type", "box"},
         {"min", QJsonArray{0, 0, 0}},
         {"max", QJsonArray{128, 64, 16}},
       }}},
    }}.toJson());
  file.close();

  const auto pathOnly = irApplyFromFileForMapResult(
    map,
    "ir_apply_from_file",
    QJsonObject{{"path", path}},
    history,
    nextOperationIndex,
    metadataStore,
    moduleStore,
    &objectRegistry,
    &previewCache);
  REQUIRE_FALSE(pathOnly.ok);
  CHECK(pathOnly.error.message.contains("requires previewId"));
  CHECK_FALSE(pathOnly.error.details.value("mutatedDocument").toBool(true));

  const auto preview = irCompilePreviewFromFileForMapResult(
    map,
    QJsonObject{{"path", path}},
    &previewCache,
    &nextPreviewIndex,
    &metadataStore,
    &moduleStore,
    &objectRegistry);
  REQUIRE(preview.ok);
  CHECK(preview.result.value("targetModuleRevision").toInt() == 1);
  CHECK(preview.result.value("targetCanonicalObjectCount").toInt() == 1);
  REQUIRE(previewCache.size() == 1u);
  const auto previewId = preview.result.value("previewId").toString();
  REQUIRE_FALSE(previewId.isEmpty());
  CHECK(previewCache.at(previewId).preview.value("targetModuleRevision").toInt() == 1);
  CHECK(
    previewCache.at(previewId).preview.value("targetCanonicalObjectIds").toArray().size()
    == 1);

  const auto replace = irApplyFromFileForMapResult(
    map,
    "ir_apply_from_file",
    QJsonObject{{"previewId", previewId}},
    history,
    nextOperationIndex,
    metadataStore,
    moduleStore,
    &objectRegistry,
    &previewCache);
  const auto replaceError =
    replace.ok ? std::string{} : replace.error.message.toStdString();
  INFO(replaceError);
  REQUIRE(replace.ok);
  CHECK(replace.result.value("usedPreviewCache").toBool());
  CHECK(replace.result.value("applyMode").toString() == "replace_module");
  CHECK(replace.result.value("previousModuleRevision").toInt() == 1);
  CHECK(replace.result.value("moduleRevision").toInt() == 2);
  CHECK(replace.result.value("removedCanonicalObjectCount").toInt() == 1);
  CHECK(replace.result.value("createdCanonicalObjectCount").toInt() == 1);
}

TEST_CASE("McpBridgeServer Python blockout tools", "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;
  auto metadataStore = std::map<QString, McpBrushMetadataRecord>{};

  SECTION("compiles operations printed by Python")
  {
    const auto response = pythonGenerateBlockoutForMapResult(
      map,
      "python_generate_blockout",
      QJsonObject{
        {"name", "MCP: Python test blockout"},
        {"grid", 16},
        {"select", true},
        {"detail", "summary"},
        {"script",
         R"PY(
import json
print(json.dumps({
    "operations": [
        {"type": "box", "min": [0, 0, 0], "max": [64, 64, 16]}
    ]
}))
)PY"},
      },
      history,
      nextOperationIndex,
      metadataStore);

    const auto error = response.ok ? std::string{} : response.error.message.toStdString();
    INFO(error);
    REQUIRE(response.ok);
    CHECK(
      response.result.value("transactionName").toString() == "MCP: Python test blockout");
    CHECK(response.result.value("brushCount").toInt() == 1);
    CHECK(response.result.value("changedObjectCount").toInt() == 1);
    CHECK(response.result.value("changedObjectIds").isUndefined());
    CHECK(response.result.value("validation").toObject().value("valid").toBool());
    CHECK(response.result.value("python").toObject().value("stdoutBytes").toInt() > 0);
    CHECK(map.selection().nodes.size() == 1u);
  }

  SECTION("stores operation metadata printed by Python")
  {
    const auto response = pythonGenerateBlockoutForMapResult(
      map,
      "python_generate_blockout",
      QJsonObject{
        {"name", "MCP: Python metadata route"},
        {"detail", "summary"},
        {"script",
         R"PY(
import json
print(json.dumps({
    "operations": [
        {
            "type": "prism",
            "points2d": [[0, 0], [64, 0], [64, 64], [0, 64]],
            "minZ": 0,
            "maxZ": 16,
            "metadata": {"routeId": "python_route", "order": 1}
        },
        {
            "type": "prism",
            "points2d": [[128, 0], [192, 0], [192, 64], [128, 64]],
            "minZ": 0,
            "maxZ": 16,
            "metadata": {"routeId": "python_route", "order": 2}
        }
    ]
}))
)PY"},
      },
      history,
      nextOperationIndex,
      metadataStore);

    const auto error = response.ok ? std::string{} : response.error.message.toStdString();
    INFO(error);
    REQUIRE(response.ok);
    CHECK(response.result.value("metadataCount").toInt() == 2);
    CHECK(response.result.value("changedObjectIds").isUndefined());

    const auto selectResponse = selectionByMetadataForMapResult(
      map, QJsonObject{{"routeId", "python_route"}, {"select", false}}, metadataStore);
    REQUIRE(selectResponse.ok);
    CHECK(selectResponse.result.value("count").toInt() == 2);
  }

  SECTION("rejects invalid JSON without committing")
  {
    const auto descendantCount = map.worldNode().descendantCount();
    const auto response = pythonGenerateBlockoutForMapResult(
      map,
      "python_generate_blockout",
      QJsonObject{{"script", "print('not json')"}},
      history,
      nextOperationIndex,
      metadataStore);

    CHECK(!response.ok);
    CHECK(response.error.code == mcp::McpErrorCode::InvalidParams);
    CHECK(response.error.details.value("valid").toBool(true) == false);
    CHECK(response.error.details.value("mutatedDocument").toBool(true) == false);
    CHECK(response.error.details.value("retrySafe").toBool(false));
    CHECK(
      response.error.details.value("recoveryAction").toString()
      == "fix_python_blockout_script_then_retry");
    CHECK(map.worldNode().descendantCount() == descendantCount);
  }

  SECTION("requires script before mutation")
  {
    const auto descendantCount = map.worldNode().descendantCount();
    const auto response = pythonGenerateBlockoutForMapResult(
      map,
      "python_generate_blockout",
      QJsonObject{},
      history,
      nextOperationIndex,
      metadataStore);

    CHECK(!response.ok);
    CHECK(response.error.code == mcp::McpErrorCode::InvalidParams);
    CHECK(response.error.details.value("mutatedDocument").toBool(true) == false);
    CHECK(response.error.details.value("retrySafe").toBool(false));
    CHECK(
      response.error.details.value("recoveryAction").toString()
      == "provide_python_blockout_script_then_retry");
    CHECK(map.worldNode().descendantCount() == descendantCount);
  }

  SECTION("requires operations array")
  {
    const auto descendantCount = map.worldNode().descendantCount();
    const auto response = pythonGenerateBlockoutForMapResult(
      map,
      "python_generate_blockout",
      QJsonObject{{"script", "print('{}')"}},
      history,
      nextOperationIndex,
      metadataStore);

    CHECK(!response.ok);
    CHECK(response.error.code == mcp::McpErrorCode::InvalidParams);
    CHECK(response.error.details.value("message").toString().contains("operations"));
    CHECK(response.error.details.value("mutatedDocument").toBool(true) == false);
    CHECK(response.error.details.value("retrySafe").toBool(false));
    CHECK(
      response.error.details.value("recoveryAction").toString()
      == "fix_python_blockout_script_then_retry");
    CHECK(map.worldNode().descendantCount() == descendantCount);
  }

  SECTION("reports nonzero Python exit")
  {
    const auto descendantCount = map.worldNode().descendantCount();
    const auto response = pythonGenerateBlockoutForMapResult(
      map,
      "python_generate_blockout",
      QJsonObject{
        {"script",
         R"PY(
import sys
print("script failed", file=sys.stderr)
sys.exit(7)
)PY"},
      },
      history,
      nextOperationIndex,
      metadataStore);

    CHECK(!response.ok);
    CHECK(response.error.code == mcp::McpErrorCode::InvalidParams);
    CHECK(response.error.details.value("stderr").toString().contains("script failed"));
    CHECK(map.worldNode().descendantCount() == descendantCount);
  }

  SECTION("kills timed out Python")
  {
    const auto descendantCount = map.worldNode().descendantCount();
    const auto response = pythonGenerateBlockoutForMapResult(
      map,
      "python_generate_blockout",
      QJsonObject{
        {"timeoutMs", 50},
        {"script",
         R"PY(
import time
time.sleep(2)
)PY"},
      },
      history,
      nextOperationIndex,
      metadataStore);

    CHECK(!response.ok);
    CHECK(response.error.code == mcp::McpErrorCode::InternalError);
    CHECK(response.error.message.contains("timed out"));
    CHECK(map.worldNode().descendantCount() == descendantCount);
  }

  SECTION("does not commit invalid generated operations")
  {
    const auto descendantCount = map.worldNode().descendantCount();
    const auto response = pythonGenerateBlockoutForMapResult(
      map,
      "python_generate_blockout",
      QJsonObject{
        {"script",
         R"PY(
import json
print(json.dumps({
    "operations": [
        {"type": "box", "min": [0, 0, 0], "max": [0, 64, 16]}
    ]
}))
)PY"},
      },
      history,
      nextOperationIndex,
      metadataStore);

    REQUIRE(response.ok);
    CHECK(!response.result.value("validation").toObject().value("valid").toBool());
    CHECK(map.worldNode().descendantCount() == descendantCount);
  }
}

TEST_CASE("McpBridgeServer grayscale heightmap import tool", "[McpBridgeServer]")
{
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
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;

  auto tempDir = QTemporaryDir{};
  REQUIRE(tempDir.isValid());

  const auto saveImage = [&](const QString& fileName, const QImage& image) {
    const auto path = tempDir.filePath(fileName);
    REQUIRE(image.save(path));
    return path;
  };

  SECTION("imports terraced merged brushes from a grayscale image")
  {
    auto image = QImage{2, 2, QImage::Format_RGB32};
    image.setPixelColor(0, 0, QColor{0, 0, 0});
    image.setPixelColor(1, 0, QColor{128, 128, 128});
    image.setPixelColor(0, 1, QColor{128, 128, 128});
    image.setPixelColor(1, 1, QColor{255, 255, 255});
    const auto imagePath = saveImage("heightmap.png", image);

    const auto response = heightmapImportGrayscaleForMapResult(
      map,
      "heightmap_import_grayscale",
      QJsonObject{
        {"imagePath", imagePath},
        {"origin", QJsonArray{0, 0, 0}},
        {"cellSize", 32},
        {"heightScale", 64},
        {"heightSteps", 4},
        {"maxSize", 4},
        {"maxBrushes", 16},
        {"select", true},
        {"detail", "summary"},
      },
      history,
      nextOperationIndex);

    const auto error = response.ok ? std::string{} : response.error.message.toStdString();
    INFO(error);
    REQUIRE(response.ok);
    CHECK(
      response.result.value("transactionName").toString()
      == "MCP: Import grayscale heightmap");
    CHECK(response.result.value("operationId").toString() == "mcp-op-1");
    CHECK(response.result.value("brushCount").toInt() == 3);
    CHECK(response.result.value("changedObjectCount").toInt() == 3);
    CHECK(response.result.value("changedObjectIds").isUndefined());
    CHECK(response.result.value("validation").toObject().value("valid").toBool());
    CHECK(
      response.result.value("heightmap").toObject().value("sourceWidth").toInt() == 2);
    CHECK(
      response.result.value("heightmap").toObject().value("sampledHeight").toInt() == 2);
    CHECK(
      response.result.value("heightmap")
        .toObject()
        .value("skippedZeroHeightCells")
        .toInt()
      == 1);
    CHECK(
      response.result.value("heightmap")
        .toObject()
        .value("outputBounds")
        .toObject()
        .value("max")
        .toArray()[2]
        .toDouble()
      == response.result.value("bounds").toObject().value("max").toArray()[2].toDouble());
    CHECK(map.selection().nodes.size() == 3u);

    REQUIRE(map.undoCommandName() != nullptr);
    CHECK(
      QString::fromStdString(*map.undoCommandName())
      == "MCP: Import grayscale heightmap");
  }

  SECTION("previews grayscale heightmap without committing")
  {
    auto image = QImage{2, 2, QImage::Format_RGB32};
    image.setPixelColor(0, 0, QColor{0, 0, 0});
    image.setPixelColor(1, 0, QColor{128, 128, 128});
    image.setPixelColor(0, 1, QColor{128, 128, 128});
    image.setPixelColor(1, 1, QColor{255, 255, 255});
    const auto imagePath = saveImage("heightmap_preview.png", image);

    const auto descendantCount = map.worldNode().descendantCount();
    const auto response = heightmapPreviewGrayscaleForMapResult(
      map,
      QJsonObject{
        {"imagePath", imagePath},
        {"origin", QJsonArray{0, 0, 0}},
        {"cellSize", 32},
        {"heightScale", 64},
        {"heightSteps", 4},
        {"maxSize", 4},
        {"maxBrushes", 16},
      });

    const auto error = response.ok ? std::string{} : response.error.message.toStdString();
    INFO(error);
    REQUIRE(response.ok);
    CHECK(response.result.value("willCommit").toBool());
    CHECK(response.result.value("estimatedBrushCount").toInt() == 3);
    CHECK(response.result.value("sampleGrid").toObject().value("width").toInt() == 2);
    CHECK(
      response.result.value("sourceImageSize").toObject().value("height").toInt() == 2);
    const auto outputBounds = response.result.value("outputBounds").toObject();
    CHECK(outputBounds.value("max").toArray()[2].toDouble() == 64.0);
    const auto heightRange = response.result.value("heightRange").toObject();
    CHECK(heightRange.value("max").toDouble() == 64.0);
    CHECK(response.result.value("warnings").isArray());
    CHECK(response.result.value("suggestedParams").isObject());
    CHECK(map.worldNode().descendantCount() == descendantCount);
    CHECK(map.undoCommandName() == nullptr);
  }

  SECTION("rejects missing image path without committing")
  {
    const auto descendantCount = map.worldNode().descendantCount();
    const auto response = heightmapImportGrayscaleForMapResult(
      map,
      "heightmap_import_grayscale",
      QJsonObject{{"imagePath", tempDir.filePath("missing.png")}},
      history,
      nextOperationIndex);

    CHECK(!response.ok);
    CHECK(response.error.code == mcp::McpErrorCode::InvalidParams);
    CHECK(response.error.details.value("mutatedDocument").toBool(true) == false);
    CHECK(response.error.details.value("retrySafe").toBool(false));
    CHECK(
      response.error.details.value("recoveryAction").toString()
      == "fix_heightmap_parameters_then_retry");
    CHECK(map.worldNode().descendantCount() == descendantCount);
  }

  SECTION("reports empty all-black heightmap without committing")
  {
    auto image = QImage{4, 4, QImage::Format_RGB32};
    image.fill(QColor{0, 0, 0});
    const auto imagePath = saveImage("black.png", image);

    const auto descendantCount = map.worldNode().descendantCount();
    const auto response = heightmapImportGrayscaleForMapResult(
      map,
      "heightmap_import_grayscale",
      QJsonObject{{"imagePath", imagePath}},
      history,
      nextOperationIndex);

    REQUIRE(response.ok);
    CHECK(!response.result.value("validation").toObject().value("valid").toBool());
    CHECK(
      response.result.value("heightmap").toObject().value("mergedBrushCount").toInt()
      == 0);
    CHECK(map.worldNode().descendantCount() == descendantCount);
  }

  SECTION("imports adaptive surface brushes from a grayscale image")
  {
    auto image = QImage{3, 3, QImage::Format_RGB32};
    image.setPixelColor(0, 0, QColor{0, 0, 0});
    image.setPixelColor(1, 0, QColor{96, 96, 96});
    image.setPixelColor(2, 0, QColor{32, 32, 32});
    image.setPixelColor(0, 1, QColor{128, 128, 128});
    image.setPixelColor(1, 1, QColor{255, 255, 255});
    image.setPixelColor(2, 1, QColor{96, 96, 96});
    image.setPixelColor(0, 2, QColor{32, 32, 32});
    image.setPixelColor(1, 2, QColor{96, 96, 96});
    image.setPixelColor(2, 2, QColor{0, 0, 0});
    const auto imagePath = saveImage("adaptive.png", image);

    const auto response = heightmapImportGrayscaleForMapResult(
      map,
      "heightmap_import_grayscale",
      QJsonObject{
        {"imagePath", imagePath},
        {"mode", "adaptive_surface"},
        {"origin", QJsonArray{0, 0, 0}},
        {"cellSize", 32},
        {"heightScale", 64},
        {"maxSize", 2},
        {"minCellSize", 32},
        {"maxCellSize", 64},
        {"errorTolerance", 0},
        {"maxBrushes", 16},
        {"select", true},
        {"detail", "summary"},
      },
      history,
      nextOperationIndex);

    const auto error = response.ok ? std::string{} : response.error.message.toStdString();
    INFO(error);
    REQUIRE(response.ok);
    CHECK(response.result.value("validation").toObject().value("valid").toBool());
    CHECK(response.result.value("brushCount").toInt() > 0);
    CHECK(response.result.value("changedObjectIds").isUndefined());

    const auto heightmap = response.result.value("heightmap").toObject();
    CHECK(heightmap.value("mode").toString() == "adaptive_surface");
    CHECK(heightmap.value("surfaceCellCount").toInt() > 0);
    CHECK(
      heightmap.value("triangleBrushCount").toInt()
      == response.result.value("brushCount").toInt());
    CHECK(heightmap.value("minCellSize").toDouble() == 32.0);
    CHECK(heightmap.value("maxCellSize").toDouble() == 64.0);
    CHECK(
      map.selection().nodes.size()
      == static_cast<size_t>(response.result.value("brushCount").toInt()));
  }

  SECTION("rejects excessive brush count without committing")
  {
    auto image = QImage{4, 4, QImage::Format_RGB32};
    for (auto y = 0; y < image.height(); ++y)
    {
      for (auto x = 0; x < image.width(); ++x)
      {
        const auto value = ((x + y) % 2 == 0) ? 64 : 192;
        image.setPixelColor(x, y, QColor{value, value, value});
      }
    }
    const auto imagePath = saveImage("checker.png", image);

    const auto descendantCount = map.worldNode().descendantCount();
    const auto response = heightmapImportGrayscaleForMapResult(
      map,
      "heightmap_import_grayscale",
      QJsonObject{
        {"imagePath", imagePath},
        {"heightSteps", 8},
        {"maxSize", 4},
        {"maxBrushes", 4},
      },
      history,
      nextOperationIndex);

    REQUIRE(response.ok);
    CHECK(!response.result.value("validation").toObject().value("valid").toBool());
    CHECK(
      response.result.value("heightmap").toObject().value("mergedBrushCount").toInt()
      > 4);
    CHECK(map.worldNode().descendantCount() == descendantCount);
  }

  SECTION("rejects excessive adaptive surface brush count without committing")
  {
    auto image = QImage{4, 4, QImage::Format_RGB32};
    for (auto y = 0; y < image.height(); ++y)
    {
      for (auto x = 0; x < image.width(); ++x)
      {
        const auto value = ((x + y) % 2 == 0) ? 64 : 192;
        image.setPixelColor(x, y, QColor{value, value, value});
      }
    }
    const auto imagePath = saveImage("adaptive_checker.png", image);

    const auto descendantCount = map.worldNode().descendantCount();
    const auto response = heightmapImportGrayscaleForMapResult(
      map,
      "heightmap_import_grayscale",
      QJsonObject{
        {"imagePath", imagePath},
        {"mode", "adaptive_surface"},
        {"cellSize", 32},
        {"heightScale", 64},
        {"maxSize", 4},
        {"minCellSize", 32},
        {"maxCellSize", 32},
        {"errorTolerance", 0},
        {"maxBrushes", 4},
      },
      history,
      nextOperationIndex);

    REQUIRE(response.ok);
    CHECK(!response.result.value("validation").toObject().value("valid").toBool());
    CHECK(
      response.result.value("heightmap").toObject().value("triangleBrushCount").toInt()
      > 4);
    CHECK(map.worldNode().descendantCount() == descendantCount);
  }
}

} // namespace tb::ui
