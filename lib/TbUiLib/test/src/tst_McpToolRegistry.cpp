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

#include "mcp/McpToolCatalog.h"
#include "ui/AppControllerFixture.h"
#include "ui/mcp/McpBridgeServer.h"

#include <QUuid>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <optional>

namespace tb::ui
{
namespace mcp = tb::mcp;

TEST_CASE(
  "McpToolRegistry matches the implemented catalog", "[McpBridgeServer][McpToolRegistry]")
{
  auto appControllerFixture = AppControllerFixture{};
  auto& appController = appControllerFixture.appController();
  const auto server = McpBridgeServer{appController};

  auto expectedNames = QStringList{};
  for (const auto& tool : mcp::defaultToolCatalog())
  {
    expectedNames.push_back(tool.name);
  }
  expectedNames.sort();

  CHECK(server.registeredToolNames() == expectedNames);
  CHECK(server.duplicateToolRegistrationCount() == 0);

  const auto retired = server.dispatchRequest(
    mcp::McpBridgeRequest{"retired-tool", "tb_history", {}, mcp::McpMode::Edit});
  CHECK_FALSE(retired.ok);
  REQUIRE(retired.error);
  CHECK(retired.error->code == mcp::McpErrorCode::ToolNotFound);
}

TEST_CASE("McpBridgeServer dispatches only the thin Python bridge", "[McpBridgeServer]")
{
  auto appControllerFixture = AppControllerFixture{};
  auto& appController = appControllerFixture.appController();
  auto server = McpBridgeServer{appController};
  auto config = mcp::McpBridgeConfig{};
  config.mode = mcp::McpMode::ReadOnly;
  config.httpEnabled = false;
  config.pipeName = QString{"trenchbroom-mcp-test-%1"}.arg(
    QUuid::createUuid().toString(QUuid::WithoutBraces));
  auto error = QString{};
  const auto started = server.start(config, &error);
  INFO(error.toStdString());
  REQUIRE(started);

  const auto inspect = server.dispatchRequest(
    mcp::McpBridgeRequest{
      "inspect", "tb_inspect", QJsonObject{{"view", "status"}}, std::nullopt});
  REQUIRE(inspect.ok);
  CHECK(inspect.result.value("mode").toString() == "ReadOnly");

  const auto api = server.dispatchRequest(
    mcp::McpBridgeRequest{
      "api", "tb_api", QJsonObject{{"symbol", "trenchbroom.ir"}}, std::nullopt});
  REQUIRE(api.ok);
  CHECK(api.result.value("symbols").toArray().isEmpty());

  const auto capture =
    server.dispatchRequest(mcp::McpBridgeRequest{"capture", "tb_capture", {}, std::nullopt});
  CHECK_FALSE(capture.ok);
  REQUIRE(capture.error);
  CHECK(capture.error->code == mcp::McpErrorCode::NoActiveDocument);

  const auto execute = server.dispatchRequest(mcp::McpBridgeRequest{
    "execute", "tb_execute_python", QJsonObject{{"executionId", "read-only"}}, std::nullopt});
  CHECK_FALSE(execute.ok);
  REQUIRE(execute.error);
  CHECK(execute.error->code == mcp::McpErrorCode::Forbidden);

  const auto retired =
    server.dispatchRequest(mcp::McpBridgeRequest{"retired", "tb_history", {}, std::nullopt});
  CHECK_FALSE(retired.ok);
  REQUIRE(retired.error);
  CHECK(retired.error->code == mcp::McpErrorCode::ToolNotFound);
  server.stop();
}

} // namespace tb::ui
