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

#include "mcp/McpBridgeClient.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLocalSocket>
#include <QUuid>

#include <algorithm>
#include <utility>

namespace tb::mcp
{
namespace
{

class LocalSocketBridgeConnection : public McpBridgeConnection
{
private:
  QLocalSocket m_socket;

public:
  void connectToServer(const QString& pipeName) override
  {
    m_socket.connectToServer(pipeName);
  }

  bool waitForConnected(const int timeoutMs) override
  {
    return m_socket.waitForConnected(timeoutMs);
  }

  qint64 write(const QByteArray& data) override { return m_socket.write(data); }

  bool waitForBytesWritten(const int timeoutMs) override
  {
    return m_socket.waitForBytesWritten(timeoutMs);
  }

  bool canReadLine() const override { return m_socket.canReadLine(); }

  bool waitForReadyRead(const int timeoutMs) override
  {
    return m_socket.waitForReadyRead(timeoutMs);
  }

  QByteArray readLine() override { return m_socket.readLine(); }

  QString errorString() const override { return m_socket.errorString(); }
};

McpBridgeResponse connectionFailure(
  const QString& requestId, const QString& message, QJsonObject details = {})
{
  return McpBridgeResponse::failure(
    requestId, McpError{McpErrorCode::InternalError, message, std::move(details)});
}

QJsonObject timeoutDetails(
  const QString& toolName, const QString& requestId, const int timeoutMs)
{
  return QJsonObject{
    {"tool", toolName},
    {"requestId", requestId},
    {"timeoutMs", timeoutMs},
    {"mutatedDocument", "unknown"},
    {"retrySafe", false},
    {"recoveryActions",
     QJsonArray{
       "Use tb_inspect to confirm the current document and map facts.",
       "Do not replay the executionId until its original receipt is known.",
       "Retry only after confirming whether the original request mutated the document.",
     }},
  };
}

} // namespace

int McpBridgeClientTimeouts::responseTimeoutMs(const McpToolCostClass costClass) const
{
  switch (costClass)
  {
  case McpToolCostClass::Fast:
    return fastResponseMs;
  case McpToolCostClass::Normal:
    return normalResponseMs;
  case McpToolCostClass::Long:
    return longResponseMs;
  }
  return normalResponseMs;
}

McpBridgeConnection::~McpBridgeConnection() = default;

McpBridgeClient::McpBridgeClient()
  : McpBridgeClient{
      [] { return std::make_unique<LocalSocketBridgeConnection>(); },
      McpBridgeClientTimeouts{}}
{
}

McpBridgeClient::McpBridgeClient(
  McpBridgeConnectionFactory connectionFactory, McpBridgeClientTimeouts timeouts)
  : m_connectionFactory{std::move(connectionFactory)}
  , m_timeouts{timeouts}
{
}

McpBridgeResponse McpBridgeClient::request(
  const McpBridgeConfig& config,
  const QString& toolName,
  QJsonObject params,
  QString requestId) const
{
  if (requestId.isEmpty())
  {
    requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  }

  const auto costClass = toolCostClassForName(toolName);
  return sendRequest(
    config,
    McpBridgeRequest{
      std::move(requestId),
      toolName,
      std::move(params),
      config.mode,
    },
    toolName,
    costClass);
}

McpBridgeResponse McpBridgeClient::sendRequest(
  const McpBridgeConfig& config,
  McpBridgeRequest request,
  const QString& requestName,
  const McpToolCostClass costClass) const
{
  const auto requestId = request.id;

  auto connection = m_connectionFactory();
  connection->connectToServer(config.pipeName);
  if (!connection->waitForConnected(m_timeouts.connectMs))
  {
    return connectionFailure(
      requestId,
      QString{"Could not connect to TrenchBroom MCP bridge '%1': %2"}.arg(
        config.pipeName, connection->errorString()));
  }

  auto requestBytes = QJsonDocument{toJson(request)}.toJson(QJsonDocument::Compact);
  requestBytes += '\n';
  if (
    connection->write(requestBytes) < 0
    || !connection->waitForBytesWritten(m_timeouts.writeMs))
  {
    return connectionFailure(
      requestId,
      QString{"Could not write MCP bridge request: %1"}.arg(connection->errorString()));
  }

  const auto responseTimeoutMs = m_timeouts.responseTimeoutMs(costClass);
  auto timer = QElapsedTimer{};
  timer.start();
  while (!connection->canReadLine())
  {
    const auto remainingMs =
      std::max(0, responseTimeoutMs - static_cast<int>(timer.elapsed()));
    if (remainingMs == 0 || !connection->waitForReadyRead(remainingMs))
    {
      return connectionFailure(
        requestId,
        QString{"Timed out waiting %1 ms for MCP bridge response from '%2'"}
          .arg(responseTimeoutMs)
          .arg(requestName),
        timeoutDetails(requestName, requestId, responseTimeoutMs));
    }
  }

  auto parseError = QJsonParseError{};
  const auto document =
    QJsonDocument::fromJson(connection->readLine().trimmed(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject())
  {
    return McpBridgeResponse::failure(
      requestId,
      McpError{McpErrorCode::InvalidRequest, "Invalid JSON response from MCP bridge"});
  }

  auto parseMessage = QString{};
  const auto response = bridgeResponseFromJson(document.object(), &parseMessage);
  if (!response)
  {
    return McpBridgeResponse::failure(
      requestId,
      McpError{
        McpErrorCode::InvalidRequest,
        QString{"Invalid MCP bridge response: %1"}.arg(parseMessage)});
  }

  if (response->id != requestId)
  {
    return McpBridgeResponse::failure(
      requestId,
      McpError{
        McpErrorCode::InvalidRequest,
        QString{"MCP bridge response id '%1' does not match request id '%2'"}.arg(
          response->id, requestId)});
  }

  return *response;
}

} // namespace tb::mcp
