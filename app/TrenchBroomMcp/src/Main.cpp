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

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include "mcp/McpBridgeClient.h"
#include "mcp/McpBridgeConfig.h"
#include "mcp/McpJsonRpc.h"

namespace tb::mcp
{
namespace
{

std::optional<McpBridgeConfig> loadConfig(QString* error)
{
  return readOrCreateBridgeConfig(defaultConfigPath(), error);
}

std::optional<QJsonObject> handleRequest(const QJsonObject& request)
{
  auto error = QString{};
  const auto config = loadConfig(&error);
  if (!config)
  {
    return jsonRpcError(
      request.value("id"), -32603, QString{"Could not load MCP config: %1"}.arg(error));
  }

  static const auto Client = McpBridgeClient{};
  return handleMcpJsonRpcRequest(
    request, config->mode, [&](const QString& toolName, const QJsonObject& params) {
      if (config->mode == McpMode::Off)
      {
        return McpBridgeResponse::failure(
          {}, McpError{McpErrorCode::Forbidden, "TrenchBroom MCP bridge is disabled"});
      }
      return Client.request(*config, toolName, params);
    });
}

void writeJsonLine(QTextStream& out, const QJsonObject& json)
{
  out << QString::fromUtf8(QJsonDocument{json}.toJson(QJsonDocument::Compact))
      << Qt::endl;
  out.flush();
}

int runServer()
{
  auto in = QTextStream{stdin};
  auto out = QTextStream{stdout};

  while (!in.atEnd())
  {
    const auto line = in.readLine();
    if (line.trimmed().isEmpty())
    {
      continue;
    }

    auto parseError = QJsonParseError{};
    const auto request = QJsonDocument::fromJson(line.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !request.isObject())
    {
      writeJsonLine(out, jsonRpcError({}, -32700, "Parse error"));
      continue;
    }

    const auto response = handleRequest(request.object());
    if (response)
    {
      writeJsonLine(out, *response);
    }
  }

  return 0;
}

} // namespace
} // namespace tb::mcp

int main(int argc, char* argv[])
{
  auto app = QCoreApplication{argc, argv};
  QCoreApplication::setApplicationName("TrenchBroom");
  QCoreApplication::setOrganizationName("");

  return tb::mcp::runServer();
}
