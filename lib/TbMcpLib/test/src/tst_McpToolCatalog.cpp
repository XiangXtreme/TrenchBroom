#include <QStringList>

#include "mcp/McpToolCatalog.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::mcp
{
namespace
{

QStringList names(const QJsonArray& tools)
{
  auto result = QStringList{};
  for (const auto& tool : tools)
  {
    result.push_back(tool.toObject().value("name").toString());
  }
  return result;
}

} // namespace

TEST_CASE("McpToolCatalog")
{
  const auto expected =
    QStringList{"tb_inspect", "tb_api", "tb_execute_python", "tb_capture"};

  SECTION("defines exactly the four public entry points")
  {
    auto actual = QStringList{};
    for (const auto& tool : defaultToolCatalog())
    {
      actual.push_back(tool.name);
    }
    CHECK(actual == expected);
    CHECK_FALSE(findToolDefinition("tb_history"));
    CHECK_FALSE(findToolDefinition("tb_validate"));
    CHECK_FALSE(findToolDefinition("documents_open"));
  }

  SECTION("applies mode permissions consistently")
  {
    CHECK(names(toolsListJson(McpMode::Off)).isEmpty());
    CHECK(
      names(toolsListJson(McpMode::ReadOnly))
      == QStringList{"tb_inspect", "tb_api", "tb_capture"});
    CHECK(names(toolsListJson(McpMode::Edit)) == expected);
  }

  SECTION("exact lookup shares the registered catalog")
  {
    const auto found = findToolDefinition("tb_execute_python");
    REQUIRE(found);
    CHECK(found->inputSchema.value("properties").toObject().contains("executionId"));
    CHECK(canCallTool(*found, McpMode::Edit));
    CHECK_FALSE(canCallTool(*found, McpMode::ReadOnly));
  }
}

} // namespace tb::mcp
