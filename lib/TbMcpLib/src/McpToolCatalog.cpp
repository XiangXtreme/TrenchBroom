/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.
 */

#include "mcp/McpToolCatalog.h"

#include <algorithm>

namespace tb::mcp
{
namespace
{

QJsonObject objectSchema(QJsonObject properties, QJsonArray required = {})
{
  return QJsonObject{
    {"type", "object"},
    {"properties", std::move(properties)},
    {"required", std::move(required)},
    {"additionalProperties", false},
  };
}

QJsonObject stringProperty(const QString& description)
{
  return QJsonObject{{"type", "string"}, {"description", description}};
}

QJsonObject integerProperty(const QString& description)
{
  return QJsonObject{{"type", "integer"}, {"description", description}};
}

QJsonObject documentSchema()
{
  return objectSchema(
    {
      {"fingerprint", stringProperty("Document fingerprint returned by tb_inspect.")},
      {"path", stringProperty("Saved document path returned by tb_inspect.")},
    },
    {"fingerprint"});
}

} // namespace

const std::vector<McpToolDefinition>& defaultToolCatalog()
{
  static const auto Catalog = std::vector<McpToolDefinition>{
    {
      "tb_inspect",
      "Read bounded document, map, selection, and native problem facts. This never runs "
      "Python.",
      McpMode::ReadOnly,
      false,
      objectSchema({
        {"view", stringProperty("status, document, map, selection, or problems.")},
        {"limit", integerProperty("Optional bounded result limit.")},
      }),
      McpToolCostClass::Fast,
    },
    {
      "tb_api",
      "Discover public symbols in the trenchbroom Python module.",
      McpMode::ReadOnly,
      false,
      objectSchema({
        {"query", stringProperty("Case-insensitive symbol search.")},
        {"symbol", stringProperty("Exact qualified symbol name.")},
      }),
      McpToolCostClass::Fast,
    },
    {
      "tb_execute_python",
      "Execute trusted Python through the public trenchbroom API in a guarded document "
      "transaction.",
      McpMode::Edit,
      true,
      objectSchema(
        {
          {"executionId", stringProperty("Caller-generated idempotency key.")},
          {"code",
           stringProperty(
             "Inline Python source. Exactly one of code or path is required.")},
          {"path",
           stringProperty(
             "Absolute Python script path. Exactly one of code or path is required.")},
          {"arguments",
           QJsonObject{
             {"type", "object"},
             {"description", "JSON arguments exposed to the script."}}},
          {"name", stringProperty("Transaction label.")},
          {"timeoutMs",
           integerProperty("Cooperative timeout from 1 to 90000 milliseconds.")},
          {"mode",
           stringProperty(
             "transaction (default) or action for lifecycle/history actions.")},
          {"document", documentSchema()},
        },
        {"executionId", "document"}),
      McpToolCostClass::Long,
    },
    {
      "tb_capture",
      "Capture the current viewport and return its bounded evidence metadata and output "
      "path.",
      McpMode::ReadOnly,
      false,
      objectSchema({{"path", stringProperty("Optional absolute PNG output path.")}}),
      McpToolCostClass::Normal,
    },
  };
  return Catalog;
}

QString toolCostClassName(const McpToolCostClass costClass)
{
  switch (costClass)
  {
  case McpToolCostClass::Fast:
    return "fast";
  case McpToolCostClass::Normal:
    return "normal";
  case McpToolCostClass::Long:
    return "long";
  }
  return "normal";
}

int toolResponseTimeoutMs(const McpToolCostClass costClass)
{
  switch (costClass)
  {
  case McpToolCostClass::Fast:
    return 5'000;
  case McpToolCostClass::Normal:
    return 30'000;
  case McpToolCostClass::Long:
    return 90'000;
  }
  return 30'000;
}

McpToolCostClass toolCostClassForName(const QString& name)
{
  const auto tool = findToolDefinition(name);
  return tool ? tool->costClass : McpToolCostClass::Normal;
}

std::optional<McpToolDefinition> findToolDefinition(const QString& name)
{
  const auto& catalog = defaultToolCatalog();
  const auto it = std::find_if(
    catalog.begin(), catalog.end(), [&](const auto& tool) { return tool.name == name; });
  return it == catalog.end() ? std::nullopt : std::optional<McpToolDefinition>{*it};
}

bool canCallTool(const McpToolDefinition& tool, const McpMode mode)
{
  return allowsMode(mode, tool.requiredMode);
}

QJsonArray toolsSearchJson(
  const QString& query, const QString& detail, const McpMode mode)
{
  auto result = QJsonArray{};
  const auto needle = query.trimmed().toLower();
  for (const auto& tool : defaultToolCatalog())
  {
    if (!allowsMode(mode, tool.requiredMode))
    {
      continue;
    }
    if (
      !needle.isEmpty() && !tool.name.contains(needle, Qt::CaseInsensitive)
      && !tool.description.contains(needle, Qt::CaseInsensitive))
    {
      continue;
    }
    auto item = toMcpToolDiagnosticJson(tool, mode);
    if (detail == "schema")
    {
      item.insert("inputSchema", tool.inputSchema);
    }
    result.push_back(std::move(item));
  }
  return result;
}

} // namespace tb::mcp
