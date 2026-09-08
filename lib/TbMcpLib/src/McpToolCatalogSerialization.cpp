/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.
 */

#include "mcp/McpToolCatalog.h"

namespace tb::mcp
{

QJsonObject toMcpToolJson(const McpToolDefinition& tool)
{
  return QJsonObject{
    {"name", tool.name},
    {"description", tool.description},
    {"inputSchema", tool.inputSchema},
  };
}

QJsonObject toMcpToolDiagnosticJson(
  const McpToolDefinition& tool, const McpMode currentMode)
{
  return QJsonObject{
    {"name", tool.name},
    {"requiredMode", modeName(tool.requiredMode)},
    {"availableInCurrentMode", allowsMode(currentMode, tool.requiredMode)},
    {"mutatesDocument", tool.mutatesDocument},
    {"costClass", toolCostClassName(tool.costClass)},
    {"timeoutMs", toolResponseTimeoutMs(tool.costClass)},
  };
}

QJsonArray toolsListJson(const McpMode mode)
{
  auto result = QJsonArray{};
  for (const auto& tool : defaultToolCatalog())
  {
    if (allowsMode(mode, tool.requiredMode))
    {
      result.push_back(toMcpToolJson(tool));
    }
  }
  return result;
}

QJsonArray toolsSummaryJson(const McpMode mode)
{
  auto result = QJsonArray{};
  for (const auto& tool : defaultToolCatalog())
  {
    if (allowsMode(mode, tool.requiredMode))
    {
      result.push_back(toMcpToolDiagnosticJson(tool, mode));
    }
  }
  return result;
}

QJsonArray toolDiagnosticsJson(const McpMode currentMode)
{
  auto result = QJsonArray{};
  for (const auto& tool : defaultToolCatalog())
  {
    result.push_back(toMcpToolDiagnosticJson(tool, currentMode));
  }
  return result;
}

} // namespace tb::mcp
