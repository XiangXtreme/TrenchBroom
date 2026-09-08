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

#include <QCryptographicHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QStringList>
#include <QUuid>

#include "McpThinBridgeTools.h"
#include "McpToolRegistry.h"
#include "mcp/McpError.h"
#include "mcp/McpToolCatalog.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/python/PythonApiCatalog.h"
#include "ui/python/PythonRuntime.h"

#include <utility>

namespace tb::ui
{
namespace mcp = tb::mcp;

namespace
{

QString pythonApiEffect(const PythonApiSymbol& symbol)
{
  if (symbol.kind == PythonApiSymbolKind::Property)
  {
    return "read";
  }
  if (symbol.kind == PythonApiSymbolKind::Class)
  {
    return "construct";
  }

  const auto name = QString::fromUtf8(symbol.name).toLower();
  static const auto editPrefixes = QStringList{
    "add",
    "cancel",
    "clear",
    "commit",
    "create",
    "delete",
    "duplicate",
    "execute",
    "register",
    "reload",
    "remove",
    "rotate",
    "save",
    "scale",
    "set",
    "translate",
    "unregister",
  };
  for (const auto& prefix : editPrefixes)
  {
    if (name.startsWith(prefix))
    {
      return "edit";
    }
  }
  return "read";
}

QString pythonApiParameters(const PythonApiSymbol& symbol)
{
  const auto detail = QString::fromUtf8(symbol.detail);
  const auto arrow = detail.indexOf("->");
  if (detail.startsWith('('))
  {
    return arrow >= 0 ? detail.left(arrow).trimmed() : detail;
  }
  return {};
}

QString pythonApiReturnType(const PythonApiSymbol& symbol)
{
  const auto detail = QString::fromUtf8(symbol.detail);
  const auto arrow = detail.indexOf("->");
  if (arrow >= 0)
  {
    return detail.mid(arrow + 2).trimmed();
  }
  return symbol.kind == PythonApiSymbolKind::Property ? detail : "None";
}

QString pythonApiExample(const PythonApiTypeInfo& type, const PythonApiSymbol& symbol)
{
  const auto name = QString::fromUtf8(symbol.name);
  if (type.type == PythonApiType::Module)
  {
    return QString{"import trenchbroom as tb\nvalue = tb.%1"}.arg(name);
  }
  if (type.type == PythonApiType::Document)
  {
    return QString{
      "import trenchbroom as tb\ndocument = tb.current_document()\nvalue = document.%1"}
      .arg(name);
  }
  if (type.type == PythonApiType::Selection)
  {
    return QString{
      "import trenchbroom as tb\nselection = tb.selection()\nvalue = selection.%1"}
      .arg(name);
  }
  if (type.type == PythonApiType::Vec3)
  {
    return QString{"import trenchbroom as tb\nvalue = tb.Vec3(0, 0, 0).%1"}.arg(name);
  }
  if (type.type == PythonApiType::Plane)
  {
    return QString{"import trenchbroom as tb\nvalue = tb.Plane(tb.Vec3(0, 0, 1), 0).%1"}
      .arg(name);
  }
  switch (type.type)
  {
  case PythonApiType::Documents:
  case PythonApiType::Objects:
  case PythonApiType::Entities:
  case PythonApiType::Brushes:
  case PythonApiType::Faces:
  case PythonApiType::Materials:
  case PythonApiType::Actions:
    return QString{"import trenchbroom as tb\nvalue = tb.%1.%2"}.arg(
      QString::fromUtf8(type.name), name);
  default:
    break;
  }
  return QString{"# Obtain a %1 handle from its documented owner.\nvalue = handle.%2"}
    .arg(QString::fromUtf8(type.name), name);
}

QString pythonApiQualifiedName(
  const PythonApiTypeInfo& type, const PythonApiSymbol& symbol)
{
  const auto typeName = QString::fromUtf8(type.name);
  const auto symbolName = QString::fromUtf8(symbol.name);
  if (type.type == PythonApiType::Module)
  {
    return QString{"%1.%2"}.arg(typeName, symbolName);
  }
  switch (type.type)
  {
  case PythonApiType::Documents:
  case PythonApiType::Objects:
  case PythonApiType::Entities:
  case PythonApiType::Brushes:
  case PythonApiType::Faces:
  case PythonApiType::Materials:
  case PythonApiType::Actions:
    return QString{"trenchbroom.%1.%2"}.arg(typeName, symbolName);
  default:
    return QString{"%1.%2"}.arg(typeName, symbolName);
  }
}

QJsonObject pythonApiSymbolJson(
  const PythonApiTypeInfo& type, const PythonApiSymbol& symbol)
{
  return QJsonObject{
    {"symbol", pythonApiQualifiedName(type, symbol)},
    {"kind", static_cast<int>(symbol.kind)},
    {"signature", QString::fromUtf8(symbol.detail)},
    {"parameters", pythonApiParameters(symbol)},
    {"returns", pythonApiReturnType(symbol)},
    {"effect", pythonApiEffect(symbol)},
    {"example", pythonApiExample(type, symbol)},
  };
}

} // namespace

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
          const auto query = params.value("query").toString().trimmed().toLower();
          const auto exact = params.value("symbol").toString().trimmed();
          auto symbols = QJsonArray{};
          for (const auto& type : pythonApiTypes())
          {
            for (const auto& symbol : pythonApiSymbols(type.type))
            {
              const auto qualified = pythonApiQualifiedName(type, symbol);
              if (!exact.isEmpty() && exact != qualified)
              {
                continue;
              }
              if (
                exact.isEmpty() && !query.isEmpty()
                && !qualified.toLower().contains(query)
                && !QString::fromUtf8(symbol.detail).toLower().contains(query))
              {
                continue;
              }
              symbols.push_back(pythonApiSymbolJson(type, symbol));
              if (exact.isEmpty() && symbols.size() == 8)
              {
                return McpBridgeToolResult::success(QJsonObject{
                  {"symbols", symbols},
                  {"truncated", true},
                  {"limit", 8},
                });
              }
            }
          }
          return McpBridgeToolResult::success(QJsonObject{
            {"symbols", symbols},
            {"truncated", false},
          });
        }
        if (toolName == "tb_execute_python")
        {
          const auto executionId = params.value("executionId").toString().trimmed();
          if (executionId.isEmpty())
          {
            return invalidParamsFailure("tb_execute_python requires executionId");
          }
          const auto hasCode = params.value("code").isString();
          const auto hasPath = params.value("path").isString();
          if (hasCode == hasPath)
          {
            return invalidParamsFailure("Provide exactly one of code or path");
          }
          auto source = QString{};
          auto filename = QString{"<mcp-python:%1>"}.arg(executionId);
          if (hasCode)
          {
            source = params.value("code").toString();
          }
          else
          {
            const auto path = params.value("path").toString();
            const auto info = QFileInfo{path};
            if (!info.isAbsolute() || !info.isFile())
            {
              return invalidParamsFailure(
                "MCP Python path must be an existing absolute file");
            }
            auto file = QFile{info.absoluteFilePath()};
            if (!file.open(QIODevice::ReadOnly))
            {
              return invalidParamsFailure("Could not read MCP Python file");
            }
            source = QString::fromUtf8(file.readAll());
            filename = info.absoluteFilePath();
          }
          if (source.isEmpty() || source.toUtf8().size() > 256 * 1024)
          {
            return invalidParamsFailure(
              "MCP Python source must be non-empty and at most 256 KiB");
          }
          if (params.value("arguments").isUndefined())
          {
            // The execution request defaults arguments to an empty JSON object.
          }
          else if (!params.value("arguments").isObject())
          {
            return invalidParamsFailure("MCP Python arguments must be an object");
          }
          const auto mode =
            params.value("mode").toString("transaction").trimmed().toLower();
          if (mode != "transaction" && mode != "action")
          {
            return invalidParamsFailure("MCP Python mode must be transaction or action");
          }
          const auto timeoutMs = params.value("timeoutMs").toInt(30'000);
          if (timeoutMs < 1 || timeoutMs > 90'000)
          {
            return invalidParamsFailure(
              "MCP Python timeoutMs must be between 1 and 90000");
          }
          const auto document = params.value("document");
          if (!document.isObject())
          {
            return invalidParamsFailure("MCP Python transaction mode requires document");
          }
          const auto requested = document.toObject();
          const auto sourceHash = QString::fromLatin1(
            QCryptographicHash::hash(source.toUtf8(), QCryptographicHash::Sha256)
              .toHex());
          const auto requestHash = QCryptographicHash::hash(
            QJsonDocument{QJsonObject{
                            {"sourceHash", sourceHash},
                            {"filename", filename},
                            {"arguments", params.value("arguments").toObject()},
                            {"document", requested},
                            {"mode", mode},
                            {"name", params.value("name").toString("MCP Python")},
                            {"timeoutMs", timeoutMs},
                          }}
              .toJson(QJsonDocument::Compact),
            QCryptographicHash::Sha256);
          if (const auto it = m_pythonExecutionReplays.find(executionId);
              it != m_pythonExecutionReplays.end())
          {
            if (it->second.requestHash != requestHash)
            {
              return McpBridgeToolResult::failure(
                mcp::McpErrorCode::InvalidParams,
                "executionId was already used with different request content",
                QJsonObject{{"executionId", executionId}, {"retrySafe", false}});
            }
            auto replay = it->second.response;
            if (replay.ok)
            {
              replay.result.insert("historicalReplay", true);
            }
            else
            {
              replay.error.details.insert("historicalReplay", true);
            }
            return replay;
          }
          auto* mapWindow = appController.mapWindowManager().topMapWindow();
          if (mapWindow == nullptr)
          {
            return noActiveDocumentFailure();
          }
          const auto active = activeDocumentJson(
            appController,
            m_bridgeInstanceId,
            m_bridgeStartedAtUtc.toString(Qt::ISODateWithMs),
            m_config.httpPort,
            &m_objectRegistry);
          const auto expectedFingerprint =
            requested.value("fingerprint").toString().trimmed();
          const auto actualFingerprint = active.value("documentFingerprint").toString();
          if (expectedFingerprint.isEmpty() || expectedFingerprint != actualFingerprint)
          {
            return McpBridgeToolResult::failure(
              mcp::McpErrorCode::Forbidden,
              "MCP Python document fingerprint does not match the active document",
              QJsonObject{
                {"mutatedDocument", false},
                {"retrySafe", true},
                {"expectedFingerprint", expectedFingerprint},
                {"actualFingerprint", actualFingerprint},
              });
          }
          const auto expectedPath = requested.value("path").toString().trimmed();
          const auto actualPath = active.value("path").toString();
          if (
            (!actualPath.isEmpty() && expectedPath.isEmpty())
            || (!expectedPath.isEmpty() && expectedPath != actualPath))
          {
            return McpBridgeToolResult::failure(
              mcp::McpErrorCode::Forbidden,
              actualPath.isEmpty()
                ? "MCP Python document path does not match the active document"
                : "MCP Python requires the saved document path",
              QJsonObject{
                {"mutatedDocument", false},
                {"retrySafe", true},
                {"expectedPath", expectedPath},
                {"actualPath", actualPath},
              });
          }
          auto context = PythonExecutionContext{};
          context.mapWindow = mapWindow;
          context.document = &mapWindow->document();
          context.appController = &appController;
          context.currentMapView = mapWindow->currentMapViewBase();
          context.logger = &mapWindow->pythonLogger();
          context.objectRegistry = &m_objectRegistry;
          context.mcpExecution = true;
          context.allowNonTransactionalActions = mode == "action";
          context.allowPersistentUi = false;
          auto elapsed = QElapsedTimer{};
          elapsed.start();
          const auto execution = PythonRuntime::instance().runMcpScript(
            context,
            PythonMcpExecutionRequest{
              source,
              filename,
              params.value("arguments").toObject(),
              params.value("name").toString("MCP Python"),
              timeoutMs,
              mode == "transaction",
            });
          auto receipt = QJsonObject{
            {"executionId", executionId},
            {"bridgeInstanceId", m_bridgeInstanceId},
            {"sourceHash", sourceHash},
            {"document", active},
            {"mode", mode},
            {"status",
             execution.ok         ? "completed"
             : execution.executed ? "failed"
                                  : "rejected"},
            {"durationMs", elapsed.elapsed()},
            {"mutatedDocument", execution.mutatedDocument},
            {"partialMutation",
             mode == "action" && !execution.ok
               && (execution.mutatedDocument || !execution.completedActions.isEmpty())},
            {"completedActions", execution.completedActions},
            {"rolledBack", execution.rolledBack},
            {"retrySafe", !execution.executed},
            {"logs",
             QJsonObject{
               {"stdoutBytes", execution.stdoutText.size()},
               {"stderrBytes", execution.stderrText.size()},
               {"discardedBytes", execution.discardedLogBytes},
               {"truncated", execution.discardedLogBytes > 0},
             }},
          };
          const auto cacheExecutionResponse = [&](const McpBridgeToolResult& response) {
            constexpr auto MaxReplays = qsizetype{1024};
            while (m_pythonExecutionReplayOrder.size() >= MaxReplays)
            {
              m_pythonExecutionReplays.erase(m_pythonExecutionReplayOrder.takeFirst());
            }
            m_pythonExecutionReplayOrder.push_back(executionId);
            m_pythonExecutionReplays.emplace(
              executionId, McpPythonExecutionReplay{requestHash, response});
          };
          if (!execution.ok)
          {
            receipt.insert("error", execution.error);
            const auto response = McpBridgeToolResult::failure(
              mcp::McpErrorCode::InternalError, "MCP Python execution failed", receipt);
            cacheExecutionResponse(response);
            return response;
          }
          receipt.insert("result", execution.value);
          const auto response = McpBridgeToolResult::success(std::move(receipt));
          cacheExecutionResponse(response);
          return response;
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
