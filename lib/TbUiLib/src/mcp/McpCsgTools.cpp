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

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "McpBridgeServerTools.h"
#include "McpResponseUtils.h"
#include "mcp/McpError.h"
#include "mdl/BrushNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Selection.h"
#include "mdl/WorldNode.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/QPathUtils.h"
#include "ui/automation/AutomationGeometry.h"

namespace tb::ui
{
namespace
{

QString makeOperationId(int& nextOperationIndex)
{
  return QString{"mcp-op-%1"}.arg(nextOperationIndex++);
}

QString nodePathId(const mdl::Node& node, const mdl::WorldNode& worldNode)
{
  if (&node == &worldNode)
  {
    return "node:world";
  }

  const auto path = node.pathFrom(worldNode);
  auto parts = QStringList{};
  for (const auto index : path.indices)
  {
    parts.push_back(QString::number(static_cast<qulonglong>(index)));
  }
  return QString{"node:%1"}.arg(parts.join("/"));
}

QJsonArray brushIdsJson(mdl::Map& map, const std::vector<mdl::BrushNode*>& brushes)
{
  auto result = QJsonArray{};
  for (const auto* brush : brushes)
  {
    if (brush != nullptr)
    {
      result.push_back(nodePathId(*brush, map.worldNode()));
    }
  }
  return result;
}

QJsonObject selectionSummaryJson(const mdl::Selection& selection)
{
  return QJsonObject{
    {"hasSelection", selection.hasAny()},
    {"nodeCount", static_cast<int>(selection.nodes.size())},
    {"groupCount", static_cast<int>(selection.groups.size())},
    {"entityCount", static_cast<int>(selection.entities.size())},
    {"brushCount", static_cast<int>(selection.brushes.size())},
    {"patchCount", static_cast<int>(selection.patches.size())},
    {"brushFaceCount", static_cast<int>(selection.brushFaces.size())},
  };
}

McpBridgeToolResult csgSelectionFailure(
  const QString& reason,
  const QString& requiredSelection,
  const mdl::Selection& selection)
{
  return McpBridgeToolResult::failure(
    mcp::McpErrorCode::InvalidParams,
    reason,
    QJsonObject{
      {"mutatedDocument", false},
      {"retrySafe", true},
      {"reason", reason},
      {"selectionSummary", selectionSummaryJson(selection)},
      {"requiredSelection", requiredSelection},
      {"recoveryAction", "select_brushes_then_retry"},
    });
}

QString csgIdsModeFromParams(const QJsonObject& params)
{
  const auto idsMode = mcpIdsModeFromParams(params);
  return idsMode == "count" && !params.contains("idsMode") && !params.contains("detail")
           ? QString{"sample"}
           : idsMode;
}

} // namespace

McpBridgeToolResult geometryCsgSelectionResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (mapWindow == nullptr)
  {
    return noActiveDocumentFailure();
  }

  const auto expectedDocumentPath = params.value("expectedDocumentPath").toString();
  if (
    !expectedDocumentPath.isEmpty()
    && activeDocumentPath(appController) != expectedDocumentPath)
  {
    return expectedDocumentPathFailure(appController, expectedDocumentPath, {}, {});
  }

  return geometryCsgSelectionForMapResult(
    mapWindow->document().map(), toolName, params, history, nextOperationIndex);
}

McpBridgeToolResult geometryCsgSelectionForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  const auto operation = params.value("operation").toString().trimmed().toLower();
  const auto csgOperation =
    automation::automationCsgOperationFromString(operation.toStdString());
  if (!csgOperation)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      "geometry_csg_selection requires operation: convex_merge, subtract, intersect, or "
      "hollow",
      QJsonObject{
        {"mutatedDocument", false},
        {"retrySafe", true},
        {"reason", "invalidOperation"},
        {"recoveryAction", "inspect_schema_and_retry"},
      });
  }

  const auto transactionName = QString::fromStdString(
    "MCP: " + automation::automationCsgTransactionName(*csgOperation));
  const auto deletedObjectIds = brushIdsJson(map, map.selection().brushes);
  const auto csgResult =
    automation::applySelectionCsg(map, *csgOperation, transactionName.toStdString());
  if (!csgResult.ok())
  {
    if (csgResult.selectionFailure)
    {
      return csgSelectionFailure(
        QString::fromStdString(csgResult.error),
        QString::fromStdString(csgResult.requiredSelection),
        map.selection());
    }
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      QString::fromStdString(csgResult.error),
      QJsonObject{
        {"mutatedDocument", false},
        {"retrySafe", true},
        {"reason", "nativeCsgReturnedFalse"},
        {"selectionSummary", selectionSummaryJson(map.selection())},
        {"requiredSelection", QString::fromStdString(csgResult.requiredSelection)},
        {"recoveryAction", "select_brushes_then_retry"},
      });
  }

  const auto changedObjectIds = brushIdsJson(map, csgResult.selectedBrushes);
  auto result = QJsonObject{
    {"mutatedDocument", true},
    {"operation", operation},
    {"transactionName", transactionName},
    {"selectedBrushCountBefore", static_cast<int>(csgResult.selectedBrushCountBefore)},
    {"selectedBrushFaceCountBefore",
     static_cast<int>(csgResult.selectedBrushFaceCountBefore)},
    {"selectionAfter", selectionSummaryJson(map.selection())},
    {"deletedObjectCount", deletedObjectIds.size()},
  };

  auto record = McpOperationRecord{};
  record.operationId = makeOperationId(nextOperationIndex);
  record.toolName = toolName;
  record.transactionName = transactionName;
  record.documentPath = map.path().empty() ? QString{} : pathAsQString(map.path());
  record.documentFingerprint = documentFingerprintForMap(map);
  record.setChangedObjectIds(changedObjectIds);
  record.setDeletedObjectIds(deletedObjectIds);

  result.insert("operationId", record.operationId);
  result.insert("activeDocumentPath", record.documentPath);
  result.insert("documentFingerprint", record.documentFingerprint);
  result.insert("resourceUri", QString{"tbmcp://operation/%1"}.arg(record.operationId));
  const auto idsMode = csgIdsModeFromParams(params);
  mcpApplyChangedObjectIdsMode(result, changedObjectIds, idsMode);
  mcpApplyDeletedObjectIdsMode(result, deletedObjectIds, idsMode);

  record.setSummary(result);
  appendMcpOperationRecord(history, std::move(record));
  return McpBridgeToolResult::success(result);
}

} // namespace tb::ui
