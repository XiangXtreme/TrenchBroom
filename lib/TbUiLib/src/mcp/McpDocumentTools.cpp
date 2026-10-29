/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.
 */

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "McpThinBridgeTools.h"
#include "mdl/GameInfo.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "mdl/WorldNode.h"
#include "ui/AppController.h"
#include "ui/GetVersion.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/ProjectSession.h"
#include "ui/QPathUtils.h"
#include "ui/automation/AutomationMapSnapshot.h"
#include "ui/automation/AutomationObjectRegistry.h"
#include "ui/python/PythonHandleRegistry.h"

#include "vm/bbox.h"

namespace tb::ui
{
namespace mcp = tb::mcp;

namespace
{

QJsonArray vecToJson(const vm::vec3d& value)
{
  return QJsonArray{value.x(), value.y(), value.z()};
}

QJsonObject boundsToJson(const vm::bbox3d& bounds)
{
  return QJsonObject{{"min", vecToJson(bounds.min)}, {"max", vecToJson(bounds.max)}};
}

QString documentPathString(const mdl::Map& map)
{
  return map.path().empty() ? QString{} : pathAsQString(map.path());
}

int documentEpoch(const mdl::Map& map, const automation::AutomationObjectRegistry*)
{
  return map.identity();
}

QString documentFingerprint(
  const mdl::Map& map, const automation::AutomationObjectRegistry* objectRegistry)
{
  return objectRegistry != nullptr
           ? objectRegistry->documentFingerprint(const_cast<mdl::Map&>(map))
           : automation::AutomationObjectRegistry{}.documentFingerprint(
               const_cast<mdl::Map&>(map));
}

QJsonObject documentJson(
  const MapEditingContext& context,
  const automation::AutomationObjectRegistry* objectRegistry)
{
  const auto& map = context.document->map();
  return QJsonObject{
    {"fileName", QString::fromStdString(map.filename())},
    {"path", documentPathString(map)},
    {"persistent", map.persistent()},
    {"modified", map.modified()},
    {"game", QString::fromStdString(map.gameInfo().gameConfig.name)},
    {"mapFormat", QString::fromStdString(mdl::formatName(map.worldNode().mapFormat()))},
    {"windowTitle",
     context.view ? context.view->windowTitle() : QString::fromStdString(map.filename())},
    {"projectId",
     context.project ? QString::fromStdString(context.project->store().info().id)
                     : QString{}},
    {"mapId",
     context.project
       ? QString::fromStdString(context.project->assetFor(*context.document).value_or(""))
       : QString{}},
    {"hasView", bool(context.view)},
    {"viewVisible", context.view && context.view->isVisible()},
    {"documentGeneration",
     qint64(PythonHandleRegistry::instance().documentGeneration(context.document.get()))},
    {"documentEpoch", documentEpoch(map, objectRegistry)},
    {"documentFingerprint", documentFingerprint(map, objectRegistry)},
  };
}

QJsonObject bridgeIdentityJson(
  const QString& bridgeInstanceId, const QString& bridgeStartedAt, const quint16 httpPort)
{
  return QJsonObject{
    {"processId", static_cast<int>(QCoreApplication::applicationPid())},
    {"bridgeInstanceId", bridgeInstanceId},
    {"bridgeStartedAt", bridgeStartedAt},
    {"httpPort", static_cast<int>(httpPort)},
  };
}

void addBridgeIdentity(
  QJsonObject& result,
  const QString& bridgeInstanceId,
  const QString& bridgeStartedAt,
  const quint16 httpPort)
{
  const auto identity = bridgeIdentityJson(bridgeInstanceId, bridgeStartedAt, httpPort);
  for (auto it = identity.begin(); it != identity.end(); ++it)
  {
    result.insert(it.key(), it.value());
  }
}

QJsonObject mapSnapshotJsonForMap(const mdl::Map& map, const QJsonObject& document)
{
  const auto& worldNode = map.worldNode();
  const auto summary = collectAutomationMapSnapshot(map);
  const auto contentBounds = summary.contentBounds.value_or(vm::bbox3d{});

  auto worldspawn = QJsonObject{};
  for (const auto& property : worldNode.entity().properties())
  {
    worldspawn.insert(
      QString::fromStdString(property.key()), QString::fromStdString(property.value()));
  }

  const auto world = QJsonObject{
    {"id", "world"},
    {"type", "world"},
    {"name", QString::fromStdString(worldNode.name())},
    {"classname", QString::fromStdString(worldNode.entity().classname())},
    {"selected", worldNode.selected()},
    {"childCount", static_cast<int>(worldNode.childCount())},
    {"descendantCount", static_cast<int>(worldNode.descendantCount())},
    {"logicalBounds", boundsToJson(contentBounds)},
    {"contentBounds", boundsToJson(contentBounds)},
    {"selectable", false},
    {"operationSafe", false},
  };

  return QJsonObject{
    {"document", document},
    {"world", world},
    {"worldspawn", worldspawn},
    {"entityCount", summary.pointEntityCount},
    {"brushCount", summary.brushCount},
    {"patchCount", summary.patchCount},
    {"nodeCount", summary.nodeCount},
    {"bounds", boundsToJson(contentBounds)},
    {"contentBounds", boundsToJson(contentBounds)},
    {"grid",
     QJsonObject{
       {"size", summary.gridSize},
       {"actualSize", summary.gridActualSize},
       {"snap", summary.gridSnap},
       {"visible", summary.gridVisible},
     }},
  };
}

} // namespace

QJsonObject editingDocumentJson(
  const MapEditingContext& context, const automation::AutomationObjectRegistry* registry)
{
  return context.document ? documentJson(context, registry) : QJsonObject{};
}

Result<MapEditingContext> resolveMcpTarget(AppController& app, const QJsonObject& params)
{
  if (!params.contains("target"))
    return app.activeMapEditingContext();
  if (!params.value("target").isObject())
    return Error{"target must be an object."};
  const auto target = params.value("target").toObject();
  const auto fingerprint = target.value("fingerprint").toString();
  const auto projectId = target.value("projectId").toString();
  const auto mapId = target.value("mapId").toString();
  for (auto it = target.begin(); it != target.end(); ++it)
    if (
      (it.key() != "fingerprint" && it.key() != "projectId" && it.key() != "mapId")
      || !it.value().isString() || it.value().toString().isEmpty())
      return Error{"target accepts non-empty fingerprint or projectId/mapId strings."};
  if (
    (projectId.isEmpty() != mapId.isEmpty())
    || (fingerprint.isEmpty() && projectId.isEmpty()))
    return Error{"target requires fingerprint or both projectId and mapId."};
  for (const auto& context : app.mapEditingContexts())
  {
    const auto info = documentJson(context, nullptr);
    if ((!fingerprint.isEmpty() && info.value("documentFingerprint").toString() != fingerprint)
        || (!projectId.isEmpty() && (info.value("projectId").toString() != projectId || info.value("mapId").toString() != mapId)))
      continue;
    return context;
  }
  return Error{
    "Requested document is not loaded in this workspace; no fallback target was "
    "selected."};
}

McpBridgeToolResult workspaceInspectResult(AppController& app, const QJsonObject& params)
{
  const auto limitValue = params.value("limit");
  const auto offsetValue = params.value("offset");
  const auto limit = limitValue.toInt(8);
  const auto offset = offsetValue.toInt(0);
  if (
    limit < 1 || limit > 50 || offset < 0
    || (!limitValue.isUndefined() && (!limitValue.isDouble() || limitValue.toDouble() != limit))
    || (!offsetValue.isUndefined() && (!offsetValue.isDouble() || offsetValue.toDouble() != offset)))
    return invalidParamsFailure("workspace requires integer limit 1..50 and offset >= 0");
  auto rows = QJsonArray{};
  for (const auto& project : app.projects())
    rows.append(QJsonObject{
      {"kind", "project"},
      {"projectId", QString::fromStdString(project->store().info().id)},
      {"path", pathAsQString(project->path())},
      {"modified", project->modified()}});
  for (const auto& context : app.mapEditingContexts())
  {
    auto row = documentJson(context, nullptr);
    row.insert("kind", "document");
    row.insert("fingerprint", row.value("documentFingerprint"));
    rows.append(row);
  }
  auto items = QJsonArray{};
  auto next = offset;
  const auto active = editingDocumentJson(app.activeMapEditingContext());
  auto result = QJsonObject{
    {"total", rows.size()},
    {"offset", offset},
    {"limit", limit},
    {"activeDocument", active.value("documentFingerprint")},
    {"items", items}};
  while (next < rows.size() && items.size() < limit)
  {
    auto row = rows[next].toObject();
    for (const auto& key : row.keys())
      if (row.value(key).isString() && row.value(key).toString().size() > 1024)
      {
        row.insert(key, row.value(key).toString().left(1024));
        row.insert("stringsTruncated", true);
      }
    auto candidate = items;
    candidate.append(row);
    auto measured = result;
    measured.insert("items", candidate);
    if (QJsonDocument{measured}.toJson(QJsonDocument::Compact).size() > 15 * 1024)
      break;
    items = candidate;
    ++next;
  }
  result.insert("items", items);
  result.insert("truncated", next < rows.size());
  if (next < rows.size())
    result.insert("nextOffset", next);
  return McpBridgeToolResult::success(result);
}

QJsonObject activeDocumentJson(
  AppController& appController,
  const QString& bridgeInstanceId,
  const QString& bridgeStartedAt,
  const quint16 httpPort,
  const automation::AutomationObjectRegistry* objectRegistry,
  const MapEditingContext* target)
{
  const auto context = target ? *target : appController.activeMapEditingContext();
  if (!context.document)
  {
    auto result = bridgeIdentityJson(bridgeInstanceId, bridgeStartedAt, httpPort);
    result.insert("activeDocument", false);
    return result;
  }

  auto result = documentJson(context, objectRegistry);
  result.insert("activeDocument", true);
  addBridgeIdentity(result, bridgeInstanceId, bridgeStartedAt, httpPort);
  return result;
}

QJsonObject mapSnapshotJson(
  AppController& appController,
  const QString& bridgeInstanceId,
  const QString& bridgeStartedAt,
  const quint16 httpPort,
  const automation::AutomationObjectRegistry* objectRegistry,
  const MapEditingContext* target)
{
  const auto context = target ? *target : appController.activeMapEditingContext();
  if (!context.document)
  {
    auto result = bridgeIdentityJson(bridgeInstanceId, bridgeStartedAt, httpPort);
    result.insert("activeDocument", false);
    return result;
  }

  auto result =
    mapSnapshotJsonForMap(context.document->map(), documentJson(context, objectRegistry));
  addBridgeIdentity(result, bridgeInstanceId, bridgeStartedAt, httpPort);
  return result;
}

QJsonObject makeStatus(
  AppController& appController,
  const mcp::McpBridgeConfig& config,
  const QString& bridgeInstanceId,
  const QString& bridgeStartedAt,
  const automation::AutomationObjectRegistry* objectRegistry,
  const MapEditingContext* target)
{
  const auto context = target ? *target : appController.activeMapEditingContext();
  auto result = QJsonObject{
    {"application", "TrenchBroom"},
    {"version", getBuildVersion()},
    {"mode", mcp::modeName(config.mode)},
    {"pipeName", config.pipeName},
    {"documentCount", static_cast<int>(appController.mapEditingContexts().size())},
  };
  result.insert("projectCount", int(appController.projects().size()));
  addBridgeIdentity(result, bridgeInstanceId, bridgeStartedAt, config.httpPort);

  if (context.document)
  {
    const auto document = documentJson(context, objectRegistry);
    result.insert("activeDocument", true);
    result.insert("document", document);
    result.insert("documentFingerprint", document.value("documentFingerprint"));
  }
  else
  {
    result.insert("activeDocument", false);
  }
  return result;
}

} // namespace tb::ui
