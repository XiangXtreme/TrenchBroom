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

} // namespace tb::mcp
