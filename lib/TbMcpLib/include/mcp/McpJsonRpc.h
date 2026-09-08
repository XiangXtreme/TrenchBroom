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

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "mcp/McpBridgeMessages.h"
#include "mcp/McpMode.h"
#include "mcp/McpToolCatalog.h"

#include <functional>
#include <optional>

namespace tb::mcp
{

using McpRequestDispatcher =
  std::function<McpBridgeResponse(const QString& toolName, const QJsonObject& params)>;

QJsonObject jsonRpcResult(const QJsonValue& id, QJsonObject result);
QJsonObject jsonRpcError(const QJsonValue& id, int code, const QString& message);
QJsonObject mcpInitializeResult(const QJsonObject& params);
QJsonObject mcpToolsListResult(McpMode currentMode);
QJsonObject mcpToolCallResult(
  const QJsonObject& params, const McpRequestDispatcher& dispatcher);
std::optional<QJsonObject> handleMcpJsonRpcRequest(
  const QJsonObject& request,
  McpMode currentMode,
  const McpRequestDispatcher& dispatcher);

} // namespace tb::mcp
