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

#include "mcp/McpBridgeMessages.h"

#include <QJsonValue>

namespace tb::mcp
{
McpBridgeResponse McpBridgeResponse::success(QString id, QJsonObject result)
{
  return McpBridgeResponse{std::move(id), true, std::move(result), std::nullopt};
}

McpBridgeResponse McpBridgeResponse::failure(QString id, McpError error)
{
  return McpBridgeResponse{std::move(id), false, {}, std::move(error)};
}

QJsonObject toJson(const McpBridgeRequest& request)
{
  auto json = QJsonObject{
    {"id", request.id},
    {"tool", request.tool},
    {"params", request.params},
  };

  if (request.requestedMode)
  {
    json.insert("mode", modeName(*request.requestedMode));
  }

  return json;
}

std::optional<McpBridgeRequest> bridgeRequestFromJson(
  const QJsonObject& json, QString* error)
{
  const auto id = json.value("id");
  if (!id.isString() || id.toString().trimmed().isEmpty())
  {
    if (error)
    {
      *error = "MCP request id is missing or empty";
    }
    return std::nullopt;
  }

  const auto tool = json.value("tool");
  if (!tool.isString() || tool.toString().trimmed().isEmpty())
  {
    if (error)
    {
      *error = "MCP request tool is missing or empty";
    }
    return std::nullopt;
  }

  auto params = QJsonObject{};
  const auto paramsValue = json.value("params");
  if (!paramsValue.isUndefined())
  {
    if (!paramsValue.isObject())
    {
      if (error)
      {
        *error = "MCP request params must be an object";
      }
      return std::nullopt;
    }
    params = paramsValue.toObject();
  }

  auto requestedMode = std::optional<McpMode>{};
  const auto modeValue = json.value("mode");
  if (!modeValue.isUndefined())
  {
    if (!modeValue.isString())
    {
      if (error)
      {
        *error = "MCP request mode must be a string";
      }
      return std::nullopt;
    }
    requestedMode = parseMode(modeValue.toString());
    if (!requestedMode)
    {
      if (error)
      {
        *error = "MCP request mode is unknown";
      }
      return std::nullopt;
    }
  }

  return McpBridgeRequest{
    id.toString(),
    tool.toString(),
    params,
    requestedMode,
  };
}

QJsonObject toJson(const McpBridgeResponse& response)
{
  auto json = QJsonObject{
    {"id", response.id},
    {"ok", response.ok},
  };

  if (response.ok)
  {
    json.insert("result", response.result);
  }
  else if (response.error)
  {
    json.insert("error", toJson(*response.error));
  }

  return json;
}

std::optional<McpBridgeResponse> bridgeResponseFromJson(
  const QJsonObject& json, QString* error)
{
  const auto id = json.value("id");
  if (!id.isString())
  {
    if (error)
    {
      *error = "MCP response id is missing or not a string";
    }
    return std::nullopt;
  }

  const auto ok = json.value("ok");
  if (!ok.isBool())
  {
    if (error)
    {
      *error = "MCP response ok is missing or not a bool";
    }
    return std::nullopt;
  }

  if (ok.toBool())
  {
    const auto result = json.value("result");
    if (!result.isUndefined() && !result.isObject())
    {
      if (error)
      {
        *error = "MCP response result must be an object";
      }
      return std::nullopt;
    }
    return McpBridgeResponse::success(id.toString(), result.toObject());
  }

  const auto errorValue = json.value("error");
  if (!errorValue.isObject())
  {
    if (error)
    {
      *error = "MCP response error is missing or not an object";
    }
    return std::nullopt;
  }

  auto errorMessage = QString{};
  const auto parsedError = errorFromJson(errorValue.toObject(), &errorMessage);
  if (!parsedError)
  {
    if (error)
    {
      *error = errorMessage;
    }
    return std::nullopt;
  }

  return McpBridgeResponse::failure(id.toString(), *parsedError);
}

} // namespace tb::mcp
