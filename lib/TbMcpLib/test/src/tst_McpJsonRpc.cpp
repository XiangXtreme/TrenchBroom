#include <QStringList>

#include "mcp/McpJsonRpc.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::mcp
{

TEST_CASE("McpJsonRpc")
{
  SECTION("lists only the entry points permitted by the active mode")
  {
    const auto readOnly = mcpToolsListResult(McpMode::ReadOnly);
    const auto tools = readOnly.value("tools").toArray();
    REQUIRE(tools.size() == 3);
    CHECK(readOnly.value("trenchBroomMode").toString() == "ReadOnly");
  }

  SECTION("rejects retired tool names through the normal dispatcher")
  {
    const auto response = handleMcpJsonRpcRequest(
      QJsonObject{
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "tools/call"},
        {"params", QJsonObject{{"name", "tb_history"}}},
      },
      McpMode::Edit,
      [](const McpBridgeRequestType, const QString&, const QJsonObject&) {
        return McpBridgeResponse::failure(
          {}, McpError{McpErrorCode::ToolNotFound, "MCP tool is not registered"});
      });
    REQUIRE(response);
    const auto result = response->value("result").toObject();
    CHECK(result.value("isError").toBool());
    CHECK(
      result.value("structuredContent").toObject().value("code").toString()
      == "ToolNotFound");
  }
}

} // namespace tb::mcp
