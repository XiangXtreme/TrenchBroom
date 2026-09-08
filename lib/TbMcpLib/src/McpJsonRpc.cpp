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

#include "mcp/McpJsonRpc.h"

#include <QJsonArray>
#include <QJsonDocument>

#include "mcp/McpError.h"
#include "mcp/McpToolCatalog.h"

namespace tb::mcp
{
namespace
{

constexpr auto ProtocolVersion = "2025-06-18";
constexpr auto ServerName = "trenchbroom-mcp";
constexpr auto ServerVersion = "0.1.0";

QJsonObject textToolResult(
  const QString& text, const bool isError, QJsonObject structuredContent = {})
{
  auto content = QJsonArray{
    QJsonObject{
      {"type", "text"},
      {"text", text},
    },
  };
  auto result = QJsonObject{
    {"content", content},
    {"isError", isError},
  };

  if (!structuredContent.isEmpty())
  {
    result.insert("structuredContent", std::move(structuredContent));
  }

  return result;
}

QString compactJsonText(const QJsonObject& json)
{
  return QString::fromUtf8(QJsonDocument{json}.toJson(QJsonDocument::Compact));
}

QString toolResultText(const QJsonObject& json)
{
  const auto summary = json.value("summary").toString();
  if (!summary.isEmpty())
  {
    return summary;
  }

  if (json.contains("count"))
  {
    return QString{"count=%1"}.arg(json.value("count").toInt());
  }

  return compactJsonText(json);
}

McpMode discoveryMode(const McpMode currentMode)
{
  return currentMode == McpMode::Off ? McpMode::ReadOnly : currentMode;
}

} // namespace

QJsonObject jsonRpcResult(const QJsonValue& id, QJsonObject result)
{
  return QJsonObject{
    {"jsonrpc", "2.0"},
    {"id", id},
    {"result", std::move(result)},
  };
}

QJsonObject jsonRpcError(const QJsonValue& id, const int code, const QString& message)
{
  return QJsonObject{
    {"jsonrpc", "2.0"},
    {"id", id},
    {"error",
     QJsonObject{
       {"code", code},
       {"message", message},
     }},
  };
}

QJsonObject mcpInitializeResult(const QJsonObject& params)
{
  Q_UNUSED(params);

  return QJsonObject{
    {"protocolVersion", ProtocolVersion},
    {"capabilities",
     QJsonObject{
       {"tools", QJsonObject{{"listChanged", false}}},
     }},
    {"serverInfo",
     QJsonObject{
       {"name", ServerName},
       {"title", "TrenchBroom MCP"},
       {"version", ServerVersion},
     }},
    {"associatedSkills", QJsonArray{"trenchbroom-mcp-scene-workflow"}},
    {"instructions",
     "Use structured tools to inspect and edit the running TrenchBroom instance. "
     "When building or editing TrenchBroom scenes, load "
     "trenchbroom-mcp-scene-workflow for scene workflow and recipe guidance. "
     "The TrenchBroom app must be running and MCP must be enabled in Preferences > "
     "MCP."},
  };
}

QJsonObject mcpToolsListResult(const McpMode currentMode)
{
  return QJsonObject{
    {"tools", toolsListJson(discoveryMode(currentMode))},
    {"trenchBroomMode", modeName(currentMode)},
  };
}

QJsonObject mcpToolCallResult(
  const QJsonObject& params, const McpRequestDispatcher& dispatcher)
{
  const auto nameValue = params.value("name");
  if (!nameValue.isString() || nameValue.toString().trimmed().isEmpty())
  {
    return textToolResult("tools/call requires a non-empty tool name", true);
  }

  auto arguments = QJsonObject{};
  const auto argumentsValue = params.value("arguments");
  if (!argumentsValue.isUndefined())
  {
    if (!argumentsValue.isObject())
    {
      return textToolResult("tools/call arguments must be an object", true);
    }
    arguments = argumentsValue.toObject();
  }

  const auto bridgeResponse = dispatcher(nameValue.toString(), arguments);
  if (bridgeResponse.ok)
  {
    return textToolResult(
      toolResultText(bridgeResponse.result), false, bridgeResponse.result);
  }

  const auto error = bridgeResponse.error.value_or(
    McpError{McpErrorCode::InternalError, "Unknown MCP bridge error"});
  auto structuredError = QJsonObject{
    {"code", errorCodeName(error.code)},
    {"message", error.message},
  };
  if (!error.details.isEmpty())
  {
    structuredError.insert("details", error.details);
    for (auto it = error.details.constBegin(); it != error.details.constEnd(); ++it)
    {
      if (!structuredError.contains(it.key()))
      {
        structuredError.insert(it.key(), it.value());
      }
    }
  }
  return textToolResult(compactJsonText(structuredError), true, structuredError);
}

std::optional<QJsonObject> handleMcpJsonRpcRequest(
  const QJsonObject& request,
  const McpMode currentMode,
  const McpRequestDispatcher& dispatcher)
{
  const auto id = request.value("id");
  if (request.value("jsonrpc").toString() != "2.0")
  {
    return jsonRpcError(id, -32600, "JSON-RPC version must be 2.0");
  }
  const auto method = request.value("method").toString();
  const auto paramsValue = request.value("params");
  if (!paramsValue.isUndefined() && !paramsValue.isObject())
  {
    return jsonRpcError(id, -32602, "JSON-RPC params must be an object");
  }
  const auto params = paramsValue.isObject() ? paramsValue.toObject() : QJsonObject{};
  const auto isNotification = request.value("id").isUndefined();

  if (method.isEmpty())
  {
    return jsonRpcError(id, -32600, "Invalid JSON-RPC request");
  }

  if (isNotification)
  {
    return std::nullopt;
  }

  if (method == "initialize")
  {
    return jsonRpcResult(id, mcpInitializeResult(params));
  }

  if (method == "ping")
  {
    return jsonRpcResult(id, {});
  }

  if (method == "tools/list")
  {
    return jsonRpcResult(id, mcpToolsListResult(currentMode));
  }

  if (method == "tools/call")
  {
    return jsonRpcResult(id, mcpToolCallResult(params, dispatcher));
  }

  return jsonRpcError(id, -32601, QString{"Method not found: %1"}.arg(method));
}

} // namespace tb::mcp
