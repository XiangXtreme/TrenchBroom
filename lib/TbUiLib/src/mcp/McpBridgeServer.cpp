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

#include "ui/mcp/McpBridgeServer.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QStringList>
#include <QUuid>

#include "McpPythonExecutor.h"
#include "McpThinBridgeTools.h"
#include "McpToolRegistry.h"
#include "mcp/McpError.h"
#include "mcp/McpToolCatalog.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"

#include <utility>

namespace tb::ui
{
namespace mcp = tb::mcp;

McpBridgeToolResult noActiveDocumentFailure()
{
  return McpBridgeToolResult::failure(
    mcp::McpErrorCode::NoActiveDocument, "No active document");
}

McpBridgeToolResult invalidParamsFailure(const QString& message)
{
  return McpBridgeToolResult::failure(mcp::McpErrorCode::InvalidParams, message);
}

McpBridgeToolResult McpBridgeToolResult::success(QJsonObject result)
{
  return McpBridgeToolResult{true, std::move(result), {}};
}

McpBridgeToolResult McpBridgeToolResult::failure(
  const mcp::McpErrorCode code, QString message)
{
  return McpBridgeToolResult{false, {}, mcp::McpError{code, std::move(message)}};
}

McpBridgeToolResult McpBridgeToolResult::failure(
  const mcp::McpErrorCode code, QString message, QJsonObject details)
{
  return McpBridgeToolResult{
    false, {}, mcp::McpError{code, std::move(message), std::move(details)}};
}

McpBridgeServer::McpBridgeServer(AppController& appController, QObject* parent)
  : McpBridgeServer{appController, McpBridgeTransportLimits{}, parent}
{
}

McpBridgeServer::McpBridgeServer(
  AppController& appController, McpBridgeTransportLimits transportLimits, QObject* parent)
  : McpBridgeServer{
      [&appController, this](const auto& toolName, const auto& params) {
        if (toolName == "tb_inspect")
        {
          const auto view = params.value("view").toString("status").trimmed().toLower();
          if (
            view != "status" && view != "document" && view != "map" && view != "selection"
            && view != "problems")
          {
            return invalidParamsFailure(
              "tb_inspect view must be status, document, map, selection, or problems");
          }
          if (view == "status")
          {
            auto status = makeStatus(
              appController,
              m_config,
              m_bridgeInstanceId,
              m_bridgeStartedAtUtc.toString(Qt::ISODateWithMs),
              &m_objectRegistry);
            return McpBridgeToolResult::success(std::move(status));
          }
          if (view == "document")
          {
            auto document = activeDocumentJson(
              appController,
              m_bridgeInstanceId,
              m_bridgeStartedAtUtc.toString(Qt::ISODateWithMs),
              m_config.httpPort,
              &m_objectRegistry);
            document.insert("fingerprint", document.value("documentFingerprint"));
            return McpBridgeToolResult::success(std::move(document));
          }
          if (view == "map")
          {
            return McpBridgeToolResult::success(mapSnapshotJson(
              appController,
              m_bridgeInstanceId,
              m_bridgeStartedAtUtc.toString(Qt::ISODateWithMs),
              m_config.httpPort,
              &m_objectRegistry));
          }
          if (view == "selection")
          {
            return McpBridgeToolResult::success(selectionJson(appController));
          }
          if (view == "problems")
          {
            return problemsCheckResult(appController, params);
          }
          return invalidParamsFailure("Unsupported tb_inspect view");
        }
        if (toolName == "tb_api")
        {
          return pythonApiResult(params);
        }
        if (toolName == "tb_execute_python")
        {
          return m_pythonExecutor->execute(
            params,
            activeDocumentJson(
              appController,
              m_bridgeInstanceId,
              m_bridgeStartedAtUtc.toString(Qt::ISODateWithMs),
              m_config.httpPort,
              &m_objectRegistry));
        }
        if (toolName == "tb_capture")
        {
          return viewportCaptureCurrentResult(appController, params);
        }
        return McpBridgeToolResult::failure(
          mcp::McpErrorCode::ToolNotFound,
          QString{"MCP tool is registered but not wired yet: %1"}.arg(toolName));
      },
      std::move(transportLimits),
      parent}
{
  m_pythonExecutor = std::make_unique<McpPythonExecutor>(appController, m_objectRegistry);
  auto registeredDispatcher = std::make_shared<ToolHandler>(std::move(m_toolHandler));
  m_toolRegistry = std::make_unique<McpToolRegistry>();
  for (const auto& tool : mcp::defaultToolCatalog())
  {
    m_toolRegistry->registerHandler(
      tool.name, [registeredDispatcher](const auto& toolName, const auto& params) {
        return (*registeredDispatcher)(toolName, params);
      });
  }
  m_toolHandler = [this](const auto& toolName, const auto& params) {
    return m_toolRegistry->dispatch(toolName, params);
  };
  m_activeMapProvider = [&appController]() -> mdl::Map* {
    auto* mapWindow = appController.mapWindowManager().topMapWindow();
    return mapWindow != nullptr ? &mapWindow->document().map() : nullptr;
  };
}

QStringList McpBridgeServer::registeredToolNames() const
{
  return m_toolRegistry != nullptr ? m_toolRegistry->toolNames() : QStringList{};
}

int McpBridgeServer::duplicateToolRegistrationCount() const
{
  return m_toolRegistry != nullptr ? m_toolRegistry->duplicateRegistrationCount() : 0;
}

McpBridgeServer::McpBridgeServer(ToolHandler toolHandler, QObject* parent)
  : McpBridgeServer{std::move(toolHandler), McpBridgeTransportLimits{}, parent}
{
}

McpBridgeServer::McpBridgeServer(
  ToolHandler toolHandler, McpBridgeTransportLimits transportLimits, QObject* parent)
  : QObject{parent}
  , m_transportLimits{std::move(transportLimits)}
  , m_toolHandler{std::move(toolHandler)}
{
}


McpBridgeServer::McpBridgeServer(
  ToolHandler toolHandler, ActiveMapProvider activeMapProvider, QObject* parent)
  : McpBridgeServer{
      std::move(toolHandler),
      std::move(activeMapProvider),
      McpBridgeTransportLimits{},
      parent}
{
}

McpBridgeServer::McpBridgeServer(
  ToolHandler toolHandler,
  ActiveMapProvider activeMapProvider,
  McpBridgeTransportLimits transportLimits,
  QObject* parent)
  : QObject{parent}
  , m_transportLimits{std::move(transportLimits)}
  , m_toolHandler{std::move(toolHandler)}
  , m_activeMapProvider{std::move(activeMapProvider)}
{
}

McpBridgeServer::~McpBridgeServer()
{
  stop();
}

} // namespace tb::ui
