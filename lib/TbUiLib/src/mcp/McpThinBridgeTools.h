#pragma once

#include <QJsonObject>
#include <QString>

#include "base/Result.h"
#include "ui/mcp/McpBridgeServer.h"

#include <filesystem>

namespace tb::mdl
{
class Map;
}

namespace tb::ui
{
class AppController;
class McpObjectRegistry;

template <typename Result>
QString resultErrorMessage(const Result& result)
{
  return QString::fromStdString(std::get<Error>(result.error()).msg);
}

McpBridgeToolResult noActiveDocumentFailure();
McpBridgeToolResult invalidParamsFailure(const QString& message);
QJsonObject makeStatus(
  AppController& appController,
  const mcp::McpBridgeConfig& config,
  const QString& bridgeInstanceId = {},
  const QString& bridgeStartedAt = {},
  const McpObjectRegistry* objectRegistry = nullptr);
QJsonObject activeDocumentJson(
  AppController& appController,
  const QString& bridgeInstanceId = {},
  const QString& bridgeStartedAt = {},
  quint16 httpPort = 37666,
  const McpObjectRegistry* objectRegistry = nullptr);
QJsonObject bridgeIdentityJson(
  const QString& bridgeInstanceId,
  const QString& bridgeStartedAt,
  quint16 httpPort = 37666);
QJsonObject documentsListJson(
  AppController& appController, const McpObjectRegistry* objectRegistry = nullptr);
QJsonObject mapSnapshotJsonForMap(const mdl::Map& map, const QJsonObject& document);
QString documentFingerprintForMap(
  const mdl::Map& map, const McpObjectRegistry* objectRegistry = nullptr);
int documentEpochForMap(
  const mdl::Map& map, const McpObjectRegistry* objectRegistry = nullptr);
QString activeDocumentPath(AppController& appController);
QJsonObject mapSnapshotJson(
  AppController& appController,
  const QString& bridgeInstanceId = {},
  const QString& bridgeStartedAt = {},
  quint16 httpPort = 37666,
  const McpObjectRegistry* objectRegistry = nullptr);
QJsonObject selectionJson(AppController& appController);
McpBridgeToolResult problemsCheckResult(AppController& appController, const QJsonObject& params);
McpBridgeToolResult viewportCaptureCurrentResult(
  AppController& appController, const QJsonObject& params);
} // namespace tb::ui
