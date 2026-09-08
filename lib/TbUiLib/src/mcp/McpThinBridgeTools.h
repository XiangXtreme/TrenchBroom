#pragma once

#include <QJsonObject>
#include <QString>

#include "ui/mcp/McpBridgeServer.h"

namespace tb::ui
{
class AppController;
namespace automation
{
class AutomationObjectRegistry;
}

McpBridgeToolResult pythonApiResult(const QJsonObject& params);

McpBridgeToolResult noActiveDocumentFailure();
McpBridgeToolResult invalidParamsFailure(const QString& message);
QJsonObject makeStatus(
  AppController& appController,
  const mcp::McpBridgeConfig& config,
  const QString& bridgeInstanceId = {},
  const QString& bridgeStartedAt = {},
  const automation::AutomationObjectRegistry* objectRegistry = nullptr);
QJsonObject activeDocumentJson(
  AppController& appController,
  const QString& bridgeInstanceId = {},
  const QString& bridgeStartedAt = {},
  quint16 httpPort = 37666,
  const automation::AutomationObjectRegistry* objectRegistry = nullptr);
QJsonObject mapSnapshotJson(
  AppController& appController,
  const QString& bridgeInstanceId = {},
  const QString& bridgeStartedAt = {},
  quint16 httpPort = 37666,
  const automation::AutomationObjectRegistry* objectRegistry = nullptr);
QJsonObject selectionJson(AppController& appController);
McpBridgeToolResult problemsCheckResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult viewportCaptureCurrentResult(
  AppController& appController, const QJsonObject& params);
} // namespace tb::ui
