/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.
 */

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "mcp/McpMode.h"

#include <optional>
#include <vector>

namespace tb::mcp
{

enum class McpToolCostClass
{
  Fast,
  Normal,
  Long,
};

struct McpToolDefinition
{
  QString name;
  QString description;
  McpMode requiredMode = McpMode::ReadOnly;
  bool mutatesDocument = false;
  QJsonObject inputSchema;
  McpToolCostClass costClass = McpToolCostClass::Normal;
};

const std::vector<McpToolDefinition>& defaultToolCatalog();

QString toolCostClassName(McpToolCostClass costClass);
int toolResponseTimeoutMs(McpToolCostClass costClass);
McpToolCostClass toolCostClassForName(const QString& name);

std::optional<McpToolDefinition> findToolDefinition(const QString& name);
bool canCallTool(const McpToolDefinition& tool, McpMode mode);

QJsonObject toMcpToolJson(const McpToolDefinition& tool);
QJsonObject toMcpToolDiagnosticJson(const McpToolDefinition& tool, McpMode currentMode);
QJsonArray toolsListJson(McpMode mode);
QJsonArray toolsSummaryJson(McpMode mode);
QJsonArray toolsSearchJson(const QString& query, const QString& detail, McpMode mode);
QJsonArray toolDiagnosticsJson(McpMode currentMode);

} // namespace tb::mcp
