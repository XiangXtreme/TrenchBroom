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

#include <catch2/catch_test_macros.hpp>

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

} // namespace tb::ui
