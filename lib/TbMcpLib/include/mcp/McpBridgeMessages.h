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
#include <QString>

#include "mcp/McpError.h"
#include "mcp/McpMode.h"

#include <optional>

namespace tb::mcp
{

struct McpBridgeRequest
{
  QString id;
  QString tool;
  QJsonObject params;
  std::optional<McpMode> requestedMode;
};

struct McpBridgeResponse
{
  QString id;
  bool ok = false;
  QJsonObject result;
  std::optional<McpError> error;

  static McpBridgeResponse success(QString id, QJsonObject result = {});
  static McpBridgeResponse failure(QString id, McpError error);
};

QJsonObject toJson(const McpBridgeRequest& request);
std::optional<McpBridgeRequest> bridgeRequestFromJson(
  const QJsonObject& json, QString* error = nullptr);

QJsonObject toJson(const McpBridgeResponse& response);
std::optional<McpBridgeResponse> bridgeResponseFromJson(
  const QJsonObject& json, QString* error = nullptr);

} // namespace tb::mcp
