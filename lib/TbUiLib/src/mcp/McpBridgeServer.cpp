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
#include <QUuid>

#include "McpBridgeServerTools.h"
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
        if (toolName == "tb_status")
        {
          auto status = makeStatus(
            appController,
            m_config,
            m_bridgeInstanceId,
            m_bridgeStartedAtUtc.toString(Qt::ISODateWithMs),
            &m_objectRegistry);
          status.insert("sessionState", m_session.diagnosticsJson());
          return McpBridgeToolResult::success(std::move(status));
        }
        if (toolName == "tb_doctor")
        {
          const auto fullDetail =
            params.value("detail").toString("summary").trimmed().toLower() == "full";
          auto doctor = doctorJson(appController, m_config, fullDetail);
          doctor.insert("overlay", m_overlayState);
          doctor.insert("sessionState", m_session.diagnosticsJson());
          return McpBridgeToolResult::success(std::move(doctor));
        }
        if (toolName == "tb_tools_search")
        {
          return McpBridgeToolResult::success(QJsonObject{
            {"tools",
             mcp::toolsSearchJson(
               params.value("query").toString(),
               params.value("category").toString(),
               params.value("detail").toString("summary"),
               m_config.mode,
               m_config.toolProfile)},
            {"toolProfile", mcp::toolProfileName(m_config.toolProfile)},
          });
        }
        if (toolName == "tb_inspect")
        {
          const auto view = params.value("view").toString("status").trimmed().toLower();
          if (view == "status")
          {
            auto status = makeStatus(
              appController,
              m_config,
              m_bridgeInstanceId,
              m_bridgeStartedAtUtc.toString(Qt::ISODateWithMs),
              &m_objectRegistry);
            status.insert("sessionState", m_session.diagnosticsJson());
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
          if (view == "actions")
          {
            return McpBridgeToolResult::success(actionsListJson(appController));
          }
          return invalidParamsFailure(
            "tb_inspect view must be status, document, map, selection, or actions");
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
              const auto qualified = QString{"%1.%2"}.arg(
                QString::fromUtf8(type.name), QString::fromUtf8(symbol.name));
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
              symbols.push_back(QJsonObject{
                {"symbol", qualified},
                {"kind", static_cast<int>(symbol.kind)},
                {"signature", QString::fromUtf8(symbol.detail)},
              });
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
          if (!expectedPath.isEmpty() && expectedPath != actualPath)
          {
            return McpBridgeToolResult::failure(
              mcp::McpErrorCode::Forbidden,
              "MCP Python document path does not match the active document",
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
            {"partialMutation", mode == "action" && execution.executed && !execution.ok},
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
        if (toolName == "tb_history")
        {
          const auto action =
            params.value("action").toString("status").trimmed().toLower();
          if (action == "status")
          {
            return historyStatusResult(
              appController,
              m_operationHistory,
              m_objectRegistry,
              m_bridgeInstanceId,
              m_bridgeStartedAtUtc.toString(Qt::ISODateWithMs));
          }
          if (action == "list")
          {
            return historyListResult(appController, m_operationHistory, m_objectRegistry);
          }
          if (action == "inspect")
          {
            return operationInspectResult(
              appController, m_operationHistory, params, m_objectRegistry);
          }
          if (action == "undo")
          {
            return historyUndoResult(
              appController,
              m_operationHistory,
              m_objectRegistry,
              &m_brushMetadata,
              &m_modules);
          }
          if (action == "redo")
          {
            return historyRedoResult(
              appController,
              m_operationHistory,
              m_objectRegistry,
              &m_brushMetadata,
              &m_modules);
          }
          return invalidParamsFailure(
            "tb_history action must be status, list, inspect, undo, or redo");
        }
        if (toolName == "tb_validate")
        {
          const auto action = params.value("action").toString("map").trimmed().toLower();
          if (action == "map")
          {
            return mapValidateResult(appController, params);
          }
          if (action == "problems")
          {
            return problemsCheckResult(appController, params);
          }
          if (action == "slopes")
          {
            return geometryAnalyzeSlopesResult(
              appController,
              params,
              m_operationHistory,
              &m_objectRegistry,
              &m_brushMetadata,
              &m_modules);
          }
          if (action == "route")
          {
            return geometryAnalyzeRouteContinuityResult(
              appController,
              params,
              m_operationHistory,
              &m_objectRegistry,
              &m_brushMetadata,
              &m_modules);
          }
          if (action == "shell_seams")
          {
            return geometryAnalyzeShellSeamsResult(
              appController,
              params,
              m_operationHistory,
              &m_objectRegistry,
              &m_brushMetadata,
              &m_modules);
          }
          return invalidParamsFailure(
            "tb_validate action must be map, problems, slopes, route, or shell_seams");
        }
        if (toolName == "tb_capture")
        {
          const auto action =
            params.value("action").toString("current").trimmed().toLower();
          if (action == "current")
          {
            return viewportCaptureCurrentResult(appController, params);
          }
          if (action == "3d")
          {
            return viewportCapture3DResult(appController, params);
          }
          if (action == "2d")
          {
            return viewportCapture2DResult(appController, params);
          }
          if (action == "scene")
          {
            return renderReviewCurrentSceneResult(
              appController,
              params,
              m_operationHistory,
              &m_objectRegistry,
              &m_brushMetadata);
          }
          return invalidParamsFailure(
            "tb_capture action must be current, 2d, 3d, or scene");
        }
        if (toolName == "documents_list")
        {
          return McpBridgeToolResult::success(documentsListJson(appController));
        }
        if (toolName == "documents_open")
        {
          return documentOpenResult(appController, params);
        }
        if (toolName == "documents_open_verified")
        {
          return documentOpenVerifiedResult(
            appController,
            params,
            m_bridgeInstanceId,
            m_bridgeStartedAtUtc.toString(Qt::ISODateWithMs),
            m_config.httpPort,
            &m_objectRegistry);
        }
        if (toolName == "documents_activate")
        {
          return documentActivateResult(appController, params);
        }
        if (
          toolName == "documents_save" || toolName == "documents_save_current"
          || toolName == "documents_save_as")
        {
          return documentSaveResult(appController, params);
        }
        if (toolName == "documents_close")
        {
          return documentCloseResult(appController, params);
        }
        if (toolName == "documents_export")
        {
          return documentExportResult(appController, params);
        }
        if (toolName == "document_snapshot")
        {
          return McpBridgeToolResult::success(activeDocumentJson(
            appController,
            m_bridgeInstanceId,
            m_bridgeStartedAtUtc.toString(Qt::ISODateWithMs),
            m_config.httpPort,
            &m_objectRegistry));
        }
        if (toolName == "map_snapshot")
        {
          return McpBridgeToolResult::success(mapSnapshotJson(
            appController,
            m_bridgeInstanceId,
            m_bridgeStartedAtUtc.toString(Qt::ISODateWithMs),
            m_config.httpPort,
            &m_objectRegistry));
        }
        if (toolName == "map_search")
        {
          return McpBridgeToolResult::success(mapSearchJson(appController, params));
        }
        if (toolName == "selection_get")
        {
          return McpBridgeToolResult::success(selectionJson(appController));
        }
        if (toolName == "selection_inspect")
        {
          return selectionInspectResult(appController, params);
        }
        if (toolName == "entity_link_chain_inspect")
        {
          return entityLinkChainInspectResult(appController, params);
        }
        if (toolName == "selection_set")
        {
          return selectionSetResult(appController, params);
        }
        if (toolName == "selection_filter")
        {
          return selectionFilterResult(appController, params);
        }
        if (toolName == "selection_by_bounds")
        {
          return selectionByBoundsResult(appController, params);
        }
        if (toolName == "selection_grow")
        {
          return selectionGrowResult(appController, params);
        }
        if (toolName == "viewport_focus")
        {
          return viewportFocusResult(appController, params);
        }
        if (toolName == "viewport_clear_marks")
        {
          return viewportClearMarksResult(appController, params, m_overlayState);
        }
        if (toolName == "viewport_layout_get")
        {
          return viewportLayoutGetResult(appController);
        }
        if (toolName == "viewport_layout_set")
        {
          return viewportLayoutSetResult(appController, params);
        }
        if (toolName == "viewport_camera_frame_bounds")
        {
          return viewportCameraFrameBoundsResult(appController, params);
        }
        if (toolName == "viewport_camera_set")
        {
          return viewportCameraSetResult(appController, params);
        }
        if (toolName == "viewport_capture_current")
        {
          return viewportCaptureCurrentResult(appController, params);
        }
        if (toolName == "viewport_capture_3d")
        {
          return viewportCapture3DResult(appController, params);
        }
        if (toolName == "viewport_capture_2d")
        {
          return viewportCapture2DResult(appController, params);
        }
        if (toolName == "viewport_capture_scene_review")
        {
          return viewportCaptureSceneReviewResult(
            appController, params, m_overlayState, m_operationHistory, &m_objectRegistry);
        }
        if (toolName == "render_review_targets")
        {
          return renderReviewTargetsResult(
            appController,
            params,
            m_operationHistory,
            &m_objectRegistry,
            &m_brushMetadata);
        }
        if (toolName == "render_review_current_scene")
        {
          return renderReviewCurrentSceneResult(
            appController,
            params,
            m_operationHistory,
            &m_objectRegistry,
            &m_brushMetadata);
        }
        if (toolName == "render_review_operation")
        {
          return renderReviewOperationResult(
            appController,
            params,
            m_operationHistory,
            &m_objectRegistry,
            &m_brushMetadata);
        }
        if (toolName == "selector_preview")
        {
          return selectorPreviewResult(
            appController,
            params,
            m_operationHistory,
            m_brushMetadata,
            m_modules,
            m_objectRegistry);
        }
        if (toolName == "objects_select_by_selector")
        {
          return objectsSelectBySelectorResult(
            appController,
            params,
            m_operationHistory,
            m_brushMetadata,
            m_modules,
            m_objectRegistry);
        }
        if (toolName == "objects_delete_by_selector")
        {
          return objectsDeleteBySelectorResult(
            appController,
            toolName,
            params,
            m_operationHistory,
            m_nextOperationIndex,
            m_brushMetadata,
            m_modules,
            m_objectRegistry);
        }
        if (toolName == "render_review_selector")
        {
          return renderReviewSelectorResult(
            appController,
            params,
            m_operationHistory,
            m_brushMetadata,
            m_modules,
            m_objectRegistry);
        }
        if (toolName == "module_list")
        {
          return moduleListResult(
            appController, params, m_brushMetadata, m_modules, m_objectRegistry);
        }
        if (toolName == "module_inspect")
        {
          return moduleInspectResult(
            appController, params, m_brushMetadata, m_modules, m_objectRegistry);
        }
        if (toolName == "module_select")
        {
          return moduleSelectResult(
            appController, params, m_brushMetadata, m_modules, m_objectRegistry);
        }
        if (toolName == "module_render_review")
        {
          return moduleRenderReviewResult(
            appController,
            params,
            m_operationHistory,
            m_brushMetadata,
            m_modules,
            m_objectRegistry);
        }
        if (toolName == "module_validate")
        {
          return moduleValidateResult(
            appController,
            params,
            m_operationHistory,
            m_brushMetadata,
            m_modules,
            m_objectRegistry);
        }
        if (toolName == "module_compact")
        {
          return moduleCompactResult(
            appController, params, m_brushMetadata, m_modules, m_objectRegistry);
        }
        if (toolName == "actions_list")
        {
          return McpBridgeToolResult::success(actionsListJson(appController));
        }
        if (toolName == "action_execute")
        {
          return actionExecuteResult(appController, params);
        }
        if (toolName == "overlay_set")
        {
          m_overlayState = params;
          appController.refreshMcpOverlayViews();
          return McpBridgeToolResult::success(QJsonObject{
            {"overlay", m_overlayState},
            {"active", true},
          });
        }
        if (toolName == "overlay_clear")
        {
          m_overlayState = QJsonObject{};
          appController.refreshMcpOverlayViews();
          return McpBridgeToolResult::success(QJsonObject{
            {"overlay", m_overlayState},
            {"active", false},
          });
        }
        if (toolName == "entity_create")
        {
          return createEntityResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "entity_update")
        {
          return updateEntityResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "entity_delete")
        {
          return deleteEntityResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "entity_properties_update")
        {
          return entityPropertiesUpdateResult(
            appController,
            toolName,
            params,
            m_operationHistory,
            m_operationHistory,
            m_nextOperationIndex,
            m_objectRegistry);
        }
        if (toolName == "entity_properties_delete")
        {
          return entityPropertiesDeleteResult(
            appController,
            toolName,
            params,
            m_operationHistory,
            m_operationHistory,
            m_nextOperationIndex,
            m_objectRegistry);
        }
        if (toolName == "fgd_entities_list")
        {
          return fgdEntitiesListResult(appController, params);
        }
        if (toolName == "entity_schema")
        {
          return entitySchemaResult(appController, params);
        }
        if (toolName == "entity_create_from_schema")
        {
          return createEntityFromSchemaResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "entity_create_checked")
        {
          return createEntityCheckedResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "entity_create_checked_batch")
        {
          return createEntityCheckedBatchResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "entity_tie_brushes")
        {
          return tieBrushesResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "entity_untie_brushes")
        {
          return untieBrushesResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "brush_types_list")
        {
          return brushTypesListResult();
        }
        if (toolName == "shape_library_list")
        {
          return shapeLibraryListResult();
        }
        if (
          toolName == "brush_create" || toolName == "brush_create_box"
          || toolName == "brush_create_wedge" || toolName == "brush_create_cylinder"
          || toolName == "brush_create_cone" || toolName == "brush_create_pipe"
          || toolName == "brush_create_sphere" || toolName == "brush_create_pyramid"
          || toolName == "brush_create_tetrahedron"
          || toolName == "brush_create_from_planes" || toolName == "brush_create_prism"
          || toolName == "brush_create_cylinder_sector")
        {
          return createBrushResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "brush_create_boxes_batch")
        {
          return createBoxesBatchResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "brush_create_polygon_batch")
        {
          return brushCreatePolygonBatchResult(
            appController,
            toolName,
            params,
            m_operationHistory,
            m_nextOperationIndex,
            m_brushMetadata);
        }
        if (toolName == "brush_metadata_set")
        {
          return brushMetadataSetResult(appController, params, m_brushMetadata);
        }
        if (toolName == "brush_metadata_get")
        {
          return brushMetadataGetResult(appController, params, m_brushMetadata);
        }
        if (toolName == "selection_by_metadata")
        {
          return selectionByMetadataResult(appController, params, m_brushMetadata);
        }
        if (
          toolName == "route_geometry_analyze_chain"
          || toolName == "kz_distance_analyze_chain")
        {
          return routeGeometryAnalyzeChainResult(appController, params, m_brushMetadata);
        }
        if (toolName == "history_list")
        {
          return historyListResult(appController, m_operationHistory, m_objectRegistry);
        }
        if (toolName == "history_status")
        {
          return historyStatusResult(
            appController,
            m_operationHistory,
            m_objectRegistry,
            m_bridgeInstanceId,
            m_bridgeStartedAtUtc.toString(Qt::ISODateWithMs));
        }
        if (toolName == "operation_inspect")
        {
          return operationInspectResult(
            appController, m_operationHistory, params, m_objectRegistry);
        }
        if (toolName == "operation_select")
        {
          return operationSelectResult(
            appController, m_operationHistory, params, m_objectRegistry);
        }
        if (toolName == "operation_validate")
        {
          return operationValidateResult(
            appController, m_operationHistory, params, m_objectRegistry);
        }
        if (toolName == "history_undo_mcp")
        {
          return historyUndoResult(
            appController,
            m_operationHistory,
            m_objectRegistry,
            &m_brushMetadata,
            &m_modules);
        }
        if (toolName == "history_undo_to_operation")
        {
          return historyUndoToOperationResult(
            appController,
            m_operationHistory,
            params,
            m_objectRegistry,
            &m_brushMetadata,
            &m_modules);
        }
        if (toolName == "history_redo_mcp")
        {
          return historyRedoResult(
            appController,
            m_operationHistory,
            m_objectRegistry,
            &m_brushMetadata,
            &m_modules);
        }
        if (toolName == "objects_delete_by_operation")
        {
          return deleteObjectsByOperationResult(
            appController,
            toolName,
            params,
            m_operationHistory,
            m_nextOperationIndex,
            m_objectRegistry);
        }
        if (toolName == "group_create_from_selection")
        {
          return groupCreateFromSelectionResult(
            appController,
            toolName,
            params,
            m_operationHistory,
            m_nextOperationIndex,
            m_objectRegistry,
            &m_brushMetadata,
            &m_modules);
        }
        if (toolName == "group_inspect")
        {
          return groupInspectResult(appController, params, m_objectRegistry);
        }
        if (toolName == "group_rename_selected")
        {
          return groupRenameSelectedResult(
            appController,
            toolName,
            params,
            m_operationHistory,
            m_nextOperationIndex,
            m_objectRegistry);
        }
        if (toolName == "group_ungroup_selected")
        {
          return groupUngroupSelectedResult(
            appController,
            toolName,
            params,
            m_operationHistory,
            m_nextOperationIndex,
            m_objectRegistry,
            &m_brushMetadata,
            &m_modules);
        }
        if (toolName == "asset_search")
        {
          return assetSearchResult(appController, params);
        }
        if (
          toolName == "asset_place_model" || toolName == "asset_place_sprite"
          || toolName == "asset_place_sound")
        {
          return placeAssetResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "textures_list" || toolName == "texture_search")
        {
          return textureSearchResult(appController, params);
        }
        if (toolName == "texture_lock_get")
        {
          return textureLockGetResult(appController);
        }
        if (toolName == "texture_lock_set")
        {
          return textureLockSetResult(appController, params);
        }
        if (toolName == "texture_apply")
        {
          return textureApplyResult(
            appController,
            toolName,
            params,
            m_operationHistory,
            m_nextOperationIndex,
            m_objectRegistry);
        }
        if (toolName == "texture_apply_by_filter")
        {
          return textureApplyByFilterResult(
            appController,
            toolName,
            params,
            m_operationHistory,
            m_nextOperationIndex,
            m_objectRegistry);
        }
        if (toolName == "texture_replace")
        {
          return textureReplaceResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "texture_align_face")
        {
          return textureAlignFaceResult(
            appController,
            toolName,
            params,
            m_operationHistory,
            m_nextOperationIndex,
            m_objectRegistry);
        }
        if (toolName == "texture_copy_from_face")
        {
          return textureCopyFromFaceResult(
            appController,
            toolName,
            params,
            m_operationHistory,
            m_nextOperationIndex,
            m_objectRegistry);
        }
        if (toolName == "face_list")
        {
          return faceListResult(
            appController, params, m_operationHistory, m_objectRegistry);
        }
        if (toolName == "face_select")
        {
          return faceSelectResult(
            appController, params, m_operationHistory, m_objectRegistry);
        }
        if (toolName == "face_texture_set")
        {
          return faceTextureSetResult(
            appController,
            toolName,
            params,
            m_operationHistory,
            m_nextOperationIndex,
            m_objectRegistry);
        }
        if (toolName == "objects_delete")
        {
          return deleteObjectsResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "objects_delete_by_filter")
        {
          return deleteObjectsByFilterResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "objects_transform")
        {
          return transformObjectsResult(
            appController,
            toolName,
            params,
            m_operationHistory,
            m_nextOperationIndex,
            m_objectRegistry,
            &m_brushMetadata,
            &m_modules);
        }
        if (toolName == "map_validate")
        {
          return mapValidateResult(appController, params);
        }
        if (toolName == "problems_check")
        {
          return problemsCheckResult(appController, params);
        }
        if (toolName == "problems_fix")
        {
          return problemsFixResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "map_fix_all_safe")
        {
          return mapFixAllSafeResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "compile_profiles_list")
        {
          return compileProfilesListResult(appController);
        }
        if (toolName == "compile_run")
        {
          return compileRunResult(appController, params);
        }
        if (toolName == "compile_log_tail")
        {
          return compileLogTailResult(appController, params);
        }
        if (toolName == "leaks_load_pointfile")
        {
          return leaksLoadPointfileResult(appController, params);
        }
        if (toolName == "python_generate_blockout")
        {
          return pythonGenerateBlockoutResult(
            appController,
            toolName,
            params,
            m_operationHistory,
            m_nextOperationIndex,
            m_brushMetadata);
        }
        if (toolName == "heightmap_import_grayscale")
        {
          return heightmapImportGrayscaleResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "heightmap_preview_grayscale")
        {
          return heightmapPreviewGrayscaleResult(appController, params);
        }
        if (toolName == "geometry_csg_selection")
        {
          return geometryCsgSelectionResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "ir_validate")
        {
          return irValidateResult(appController, params);
        }
        if (toolName == "ir_compile_preview")
        {
          return irCompilePreviewResult(
            appController, params, m_brushMetadata, m_modules, m_objectRegistry);
        }
        if (toolName == "ir_compile_preview_from_file")
        {
          return irCompilePreviewFromFileResult(
            appController,
            params,
            &m_irPreviewCache,
            &m_nextIrPreviewIndex,
            &m_brushMetadata,
            &m_modules,
            &m_objectRegistry);
        }
        if (toolName == "ir_apply")
        {
          return irApplyResult(
            appController,
            toolName,
            params,
            m_operationHistory,
            m_nextOperationIndex,
            m_brushMetadata,
            m_modules,
            &m_objectRegistry);
        }
        if (toolName == "ir_apply_from_file")
        {
          return irApplyFromFileResult(
            appController,
            toolName,
            params,
            m_operationHistory,
            m_nextOperationIndex,
            m_brushMetadata,
            m_modules,
            &m_objectRegistry,
            &m_irPreviewCache);
        }
        if (
          toolName == "blockout_create_batch"
          || toolName == "blockout_create_curved_corridor")
        {
          return blockoutCreateBatchResult(
            appController,
            toolName,
            params,
            m_operationHistory,
            m_nextOperationIndex,
            &m_brushMetadata,
            &m_modules,
            &m_objectRegistry);
        }
        if (toolName == "blockout_create_spiral_stairs")
        {
          return blockoutCreateResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "blockout_validate")
        {
          return blockoutValidateResult(params);
        }
        if (toolName == "geometry_analyze_selection")
        {
          return geometryAnalyzeSelectionResult(appController, params);
        }
        if (toolName == "geometry_analyze_slopes")
        {
          return geometryAnalyzeSlopesResult(
            appController,
            params,
            m_operationHistory,
            &m_objectRegistry,
            &m_brushMetadata,
            &m_modules);
        }
        if (toolName == "geometry_analyze_route_continuity")
        {
          return geometryAnalyzeRouteContinuityResult(
            appController,
            params,
            m_operationHistory,
            &m_objectRegistry,
            &m_brushMetadata,
            &m_modules);
        }
        if (toolName == "geometry_analyze_shell_seams")
        {
          return geometryAnalyzeShellSeamsResult(
            appController,
            params,
            m_operationHistory,
            &m_objectRegistry,
            &m_brushMetadata,
            &m_modules);
        }
        if (toolName == "blockout_validate_spiral_stairs")
        {
          return blockoutValidateSpiralStairsResult(
            appController, params, m_operationHistory);
        }
        return McpBridgeToolResult::failure(
          mcp::McpErrorCode::ToolNotFound,
          QString{"MCP tool is registered but not wired yet: %1"}.arg(toolName));
      },
      std::move(transportLimits),
      parent}
{
  auto legacyDispatcher = std::make_shared<ToolHandler>(std::move(m_toolHandler));
  m_toolRegistry = std::make_unique<McpToolRegistry>();
  for (const auto& tool : mcp::defaultToolCatalog())
  {
    if (tool.implemented)
    {
      m_toolRegistry->registerHandler(
        tool.name, [legacyDispatcher](const auto& toolName, const auto& params) {
          return (*legacyDispatcher)(toolName, params);
        });
    }
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
