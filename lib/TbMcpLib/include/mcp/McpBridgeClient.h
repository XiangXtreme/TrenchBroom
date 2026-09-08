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
#include <QJsonObject>
#include <QString>
#include <QtGlobal>

#include "mcp/McpBridgeConfig.h"
#include "mcp/McpBridgeMessages.h"
#include "mcp/McpToolCatalog.h"

#include <functional>
#include <memory>

namespace tb::mcp
{

struct McpBridgeClientTimeouts
{
  int connectMs = 5'000;
  int writeMs = 5'000;
  int fastResponseMs = 10'000;
  int normalResponseMs = 30'000;
  int longResponseMs = 90'000;

  int responseTimeoutMs(McpToolCostClass costClass) const;
};

class McpBridgeConnection
{
public:
  virtual ~McpBridgeConnection();

  virtual void connectToServer(const QString& pipeName) = 0;
  virtual bool waitForConnected(int timeoutMs) = 0;
  virtual qint64 write(const QByteArray& data) = 0;
  virtual bool waitForBytesWritten(int timeoutMs) = 0;
  virtual bool canReadLine() const = 0;
  virtual bool waitForReadyRead(int timeoutMs) = 0;
  virtual QByteArray readLine() = 0;
  virtual QString errorString() const = 0;
};

using McpBridgeConnectionFactory = std::function<std::unique_ptr<McpBridgeConnection>()>;

class McpBridgeClient
{
private:
  McpBridgeConnectionFactory m_connectionFactory;
  McpBridgeClientTimeouts m_timeouts;

  McpBridgeResponse sendRequest(
    const McpBridgeConfig& config,
    McpBridgeRequest request,
    const QString& requestName,
    McpToolCostClass costClass) const;

public:
  McpBridgeClient();
  McpBridgeClient(
    McpBridgeConnectionFactory connectionFactory, McpBridgeClientTimeouts timeouts);

  McpBridgeResponse request(
    const McpBridgeConfig& config,
    const QString& toolName,
    QJsonObject params,
    QString requestId = {}) const;
};

} // namespace tb::mcp
