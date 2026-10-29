#pragma once

#include <QJsonObject>
#include <QString>

#include "base/Result.h"
#include "ui/MapEditingContext.h"
#include "ui/mcp/McpBridgeServer.h"

namespace tb::ui
{
class AppController;
namespace automation
{
class AutomationObjectRegistry;
}

Result<MapEditingContext> resolveMcpTarget(AppController& app, const QJsonObject& params);
QJsonObject editingDocumentJson(
  const MapEditingContext& context,
  const automation::AutomationObjectRegistry* registry = nullptr);
McpBridgeToolResult workspaceInspectResult(AppController& app, const QJsonObject& params);

McpBridgeToolResult pythonApiResult(const QJsonObject& params);

McpBridgeToolResult noActiveDocumentFailure();
McpBridgeToolResult invalidParamsFailure(const QString& message);
QJsonObject makeStatus(
  AppController& appController,
  const mcp::McpBridgeConfig& config,
  const QString& bridgeInstanceId = {},
  const QString& bridgeStartedAt = {},
  const automation::AutomationObjectRegistry* objectRegistry = nullptr,
  const MapEditingContext* target = nullptr);
QJsonObject activeDocumentJson(
  AppController& appController,
  const QString& bridgeInstanceId = {},
  const QString& bridgeStartedAt = {},
  quint16 httpPort = 37666,
  const automation::AutomationObjectRegistry* objectRegistry = nullptr,
  const MapEditingContext* target = nullptr);
QJsonObject mapSnapshotJson(
  AppController& appController,
  const QString& bridgeInstanceId = {},
  const QString& bridgeStartedAt = {},
  quint16 httpPort = 37666,
  const automation::AutomationObjectRegistry* objectRegistry = nullptr,
  const MapEditingContext* target = nullptr);
QJsonObject selectionJson(
  AppController& appController, const MapEditingContext* target = nullptr);
McpBridgeToolResult problemsCheckResult(
  AppController& appController,
  const QJsonObject& params,
  const MapEditingContext* target = nullptr);
McpBridgeToolResult viewportCaptureCurrentResult(
  AppController& appController,
  const QJsonObject& params,
  const MapEditingContext* target = nullptr);
} // namespace tb::ui
