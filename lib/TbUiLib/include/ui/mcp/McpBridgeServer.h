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

#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocalServer>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QtGlobal>

#include "mcp/McpBridgeConfig.h"
#include "mcp/McpBridgeMessages.h"
#include "mcp/McpError.h"
#include "ui/automation/AutomationObjectRegistry.h"

#include <functional>
#include <map>
#include <memory>

class QLocalSocket;
class QTimer;

namespace tb::mdl
{
class Map;
}

namespace tb::ui
{
namespace mcp = tb::mcp;

class AppController;

struct McpBridgeTransportLimits
{
  qsizetype maxRequestBytes = 4 * 1024 * 1024;
  int incompleteRequestTimeoutMs = 10'000;
  int maxConnections = 32;
};

struct McpBridgeToolResult
{
  bool ok = true;
  QJsonObject result;
  mcp::McpError error;

  static McpBridgeToolResult success(QJsonObject result = {});
  static McpBridgeToolResult failure(mcp::McpErrorCode code, QString message);
  static McpBridgeToolResult failure(
    mcp::McpErrorCode code, QString message, QJsonObject details);
};

struct McpPythonExecutionReplay
{
  QByteArray requestHash;
  McpBridgeToolResult response;
};

class McpToolRegistry;

class McpBridgeServer : public QObject
{
  Q_OBJECT
public:
  using ToolHandler =
    std::function<McpBridgeToolResult(const QString&, const QJsonObject&)>;
  using ActiveMapProvider = std::function<mdl::Map*()>;

private:
  mcp::McpBridgeConfig m_config;
  McpBridgeTransportLimits m_transportLimits;
  ToolHandler m_toolHandler;
  ActiveMapProvider m_activeMapProvider;
  automation::AutomationObjectRegistry m_objectRegistry;
  mutable std::map<QString, McpPythonExecutionReplay> m_pythonExecutionReplays;
  mutable QStringList m_pythonExecutionReplayOrder;
  std::unique_ptr<McpToolRegistry> m_toolRegistry;
  mutable bool m_dispatchInProgress = false;
  QString m_bridgeInstanceId;
  QDateTime m_bridgeStartedAtUtc;
  std::unique_ptr<QLocalServer> m_server;
  QSet<QLocalSocket*> m_connections;
  QHash<QLocalSocket*, QTimer*> m_requestDeadlines;

public:
  explicit McpBridgeServer(AppController& appController, QObject* parent = nullptr);
  McpBridgeServer(
    AppController& appController,
    McpBridgeTransportLimits transportLimits,
    QObject* parent = nullptr);
  explicit McpBridgeServer(ToolHandler toolHandler, QObject* parent = nullptr);
  McpBridgeServer(
    ToolHandler toolHandler,
    McpBridgeTransportLimits transportLimits,
    QObject* parent = nullptr);
  McpBridgeServer(
    ToolHandler toolHandler,
    ActiveMapProvider activeMapProvider,
    QObject* parent = nullptr);
  McpBridgeServer(
    ToolHandler toolHandler,
    ActiveMapProvider activeMapProvider,
    McpBridgeTransportLimits transportLimits,
    QObject* parent = nullptr);
  ~McpBridgeServer() override;

  bool start(const mcp::McpBridgeConfig& config, QString* error = nullptr);
  void stop();

  bool isListening() const;
  QString pipeName() const;
  mcp::McpMode mode() const;
  QStringList registeredToolNames() const;
  int duplicateToolRegistrationCount() const;

  mcp::McpBridgeResponse dispatchRequest(const mcp::McpBridgeRequest& request) const;

private:
  mcp::McpBridgeResponse dispatchToolCall(const mcp::McpBridgeRequest& request) const;
  void startRequestDeadline(QLocalSocket& socket);
  void restartRequestDeadline(QLocalSocket& socket);
  void rejectAndDisconnect(QLocalSocket& socket, const QString& message) const;
  void removeConnection(QLocalSocket& socket);
  void handleNewConnection();
  void handleSocketReadyRead(QLocalSocket& socket);
  void writeResponse(QLocalSocket& socket, const mcp::McpBridgeResponse& response) const;
};

} // namespace tb::ui
