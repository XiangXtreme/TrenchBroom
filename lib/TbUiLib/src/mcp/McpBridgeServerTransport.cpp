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

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>
#include <QUuid>

#include "mcp/McpToolCatalog.h"
#include "ui/mcp/McpBridgeServer.h"

namespace tb::ui
{
namespace
{

mcp::McpBridgeResponse makeFailure(
  const mcp::McpBridgeRequest& request,
  const mcp::McpErrorCode code,
  const QString& message)
{
  return mcp::McpBridgeResponse::failure(request.id, mcp::McpError{code, message});
}

mcp::McpBridgeResponse makeFailure(
  const mcp::McpBridgeRequest& request,
  const mcp::McpErrorCode code,
  const QString& message,
  QJsonObject details)
{
  return mcp::McpBridgeResponse::failure(
    request.id, mcp::McpError{code, message, std::move(details)});
}

} // namespace

bool McpBridgeServer::start(const mcp::McpBridgeConfig& config, QString* error)
{
  stop();
  m_config = config;
  m_bridgeInstanceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  m_bridgeStartedAtUtc = QDateTime::currentDateTimeUtc();

  if (m_config.mode == mcp::McpMode::Off)
  {
    return true;
  }

  m_server = std::make_unique<QLocalServer>();
  m_server->setMaxPendingConnections(m_transportLimits.maxConnections);
  connect(
    m_server.get(),
    &QLocalServer::newConnection,
    this,
    &McpBridgeServer::handleNewConnection);

  auto probe = QLocalSocket{};
  probe.connectToServer(m_config.pipeName);
  if (probe.waitForConnected(250))
  {
    if (error)
    {
      *error =
        QString{"Another TrenchBroom MCP instance is already listening on pipe '%1'"}.arg(
          m_config.pipeName);
    }
    m_server.reset();
    return false;
  }

  if (
    probe.error() != QLocalSocket::ServerNotFoundError
    && probe.error() != QLocalSocket::ConnectionRefusedError)
  {
    if (error)
    {
      *error = QString{"Could not verify whether MCP pipe '%1' is active: %2"}.arg(
        m_config.pipeName, probe.errorString());
    }
    m_server.reset();
    return false;
  }

  if (m_server->listen(m_config.pipeName))
  {
    return true;
  }

  if (
    !QLocalServer::removeServer(m_config.pipeName)
    || !m_server->listen(m_config.pipeName))
  {
    if (error)
    {
      *error = QString{"Could not claim inactive MCP pipe '%1': %2"}.arg(
        m_config.pipeName, m_server->errorString());
    }
    m_server.reset();
    return false;
  }

  return true;
}

void McpBridgeServer::stop()
{
  const auto connections = m_connections.values();
  for (auto* socket : connections)
  {
    socket->disconnectFromServer();
    socket->deleteLater();
  }
  m_requestDeadlines.clear();
  m_connections.clear();

  if (m_server)
  {
    m_server->close();
    QLocalServer::removeServer(m_config.pipeName);
    m_server.reset();
  }
  m_pythonExecutionReplays.clear();
  m_pythonExecutionReplayOrder.clear();
}

bool McpBridgeServer::isListening() const
{
  return m_server != nullptr && m_server->isListening();
}

QString McpBridgeServer::pipeName() const
{
  return m_config.pipeName;
}

mcp::McpMode McpBridgeServer::mode() const
{
  return m_config.mode;
}

const QJsonObject& McpBridgeServer::overlayState() const
{
  return m_emptyOverlayState;
}

mcp::McpBridgeResponse McpBridgeServer::dispatchToolCall(
  const mcp::McpBridgeRequest& request) const
{
  if (m_dispatchInProgress)
  {
    return makeFailure(
      request,
      mcp::McpErrorCode::Forbidden,
      "MCP bridge is already handling another request; retry after it finishes");
  }

  const auto tool = mcp::findToolDefinition(request.tool);
  if (!tool)
  {
    return makeFailure(
      request,
      mcp::McpErrorCode::ToolNotFound,
      QString{"Unknown MCP tool: %1"}.arg(request.tool));
  }

  const auto effectiveMode =
    request.requestedMode && mcp::allowsMode(m_config.mode, *request.requestedMode)
      ? *request.requestedMode
      : m_config.mode;
  if (!mcp::canCallTool(*tool, effectiveMode))
  {
    return makeFailure(
      request,
      mcp::McpErrorCode::Forbidden,
      QString{"MCP tool is not available in mode %1"}.arg(mcp::modeName(effectiveMode)));
  }
  struct DispatchGuard
  {
    bool& dispatchInProgress;

    explicit DispatchGuard(bool& i_dispatchInProgress)
      : dispatchInProgress{i_dispatchInProgress}
    {
      dispatchInProgress = true;
    }

    ~DispatchGuard() { dispatchInProgress = false; }
  };

  const auto dispatchGuard = DispatchGuard{m_dispatchInProgress};

  const auto result = m_toolHandler(request.tool, request.params);
  return result.ok ? mcp::McpBridgeResponse::success(request.id, result.result)
                   : mcp::McpBridgeResponse::failure(request.id, result.error);
}

void McpBridgeServer::startRequestDeadline(QLocalSocket& socket)
{
  auto* timer = new QTimer{&socket};
  timer->setSingleShot(true);
  connect(timer, &QTimer::timeout, this, [this, socketPtr = &socket]() {
    if (!m_connections.contains(socketPtr))
    {
      return;
    }
    rejectAndDisconnect(*socketPtr, "MCP bridge request timed out");
  });
  m_requestDeadlines.insert(&socket, timer);
  timer->start(m_transportLimits.incompleteRequestTimeoutMs);
}

void McpBridgeServer::restartRequestDeadline(QLocalSocket& socket)
{
  if (auto* timer = m_requestDeadlines.value(&socket))
  {
    timer->start(m_transportLimits.incompleteRequestTimeoutMs);
  }
}

void McpBridgeServer::rejectAndDisconnect(
  QLocalSocket& socket, const QString& message) const
{
  writeResponse(
    socket,
    mcp::McpBridgeResponse::failure(
      {}, mcp::McpError{mcp::McpErrorCode::InvalidRequest, message}));
  socket.disconnectFromServer();
}

void McpBridgeServer::removeConnection(QLocalSocket& socket)
{
  m_requestDeadlines.remove(&socket);
  m_connections.remove(&socket);
  socket.deleteLater();
}

mcp::McpBridgeResponse McpBridgeServer::dispatchRequest(
  const mcp::McpBridgeRequest& request) const
{
  if (request.type == mcp::McpBridgeRequestType::ToolCall)
  {
    return dispatchToolCall(request);
  }

  return makeFailure(
    request,
    mcp::McpErrorCode::InvalidRequest,
    "MCP resources are not supported by the thin Python bridge");
}

void McpBridgeServer::handleNewConnection()
{
  while (auto* socket = m_server->nextPendingConnection())
  {
    socket->setParent(this);
    connect(socket, &QLocalSocket::disconnected, this, [this, socket]() {
      if (m_connections.contains(socket))
      {
        removeConnection(*socket);
      }
      else
      {
        socket->deleteLater();
      }
    });

    if (m_connections.size() >= m_transportLimits.maxConnections)
    {
      rejectAndDisconnect(*socket, "MCP bridge connection limit reached");
      continue;
    }

    m_connections.insert(socket);
    socket->setReadBufferSize(m_transportLimits.maxRequestBytes + 1);
    startRequestDeadline(*socket);
    connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
      handleSocketReadyRead(*socket);
    });
  }
}

void McpBridgeServer::handleSocketReadyRead(QLocalSocket& socket)
{
  if (
    !socket.canReadLine() && socket.bytesAvailable() > m_transportLimits.maxRequestBytes)
  {
    rejectAndDisconnect(socket, "MCP bridge request too large");
    return;
  }

  while (socket.canReadLine())
  {
    auto line = socket.readLine();
    if (line.endsWith('\n'))
    {
      line.chop(1);
    }
    if (line.size() > m_transportLimits.maxRequestBytes)
    {
      rejectAndDisconnect(socket, "MCP bridge request too large");
      return;
    }

    auto parseError = QJsonParseError{};
    const auto document = QJsonDocument::fromJson(line.trimmed(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
      writeResponse(
        socket,
        mcp::McpBridgeResponse::failure(
          {},
          mcp::McpError{
            mcp::McpErrorCode::InvalidRequest, "Invalid MCP bridge JSON request"}));
      restartRequestDeadline(socket);
      continue;
    }

    auto error = QString{};
    const auto request = mcp::bridgeRequestFromJson(document.object(), &error);
    if (!request)
    {
      writeResponse(
        socket,
        mcp::McpBridgeResponse::failure(
          {}, mcp::McpError{mcp::McpErrorCode::InvalidRequest, error}));
      restartRequestDeadline(socket);
      continue;
    }

    writeResponse(socket, dispatchRequest(*request));
    restartRequestDeadline(socket);
  }
}

void McpBridgeServer::writeResponse(
  QLocalSocket& socket, const mcp::McpBridgeResponse& response) const
{
  socket.write(QJsonDocument{mcp::toJson(response)}.toJson(QJsonDocument::Compact));
  socket.write("\n");
  socket.flush();
}

} // namespace tb::ui
