/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.
 */

#include <QCoreApplication>
#include <QJsonArray>
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
#include "ui/MapWindowManager.h"
#include "ui/QPathUtils.h"
#include "ui/automation/AutomationMapSnapshot.h"
#include "ui/automation/AutomationObjectRegistry.h"

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

int documentEpoch(
  const mdl::Map& map, const automation::AutomationObjectRegistry* objectRegistry)
{
  return objectRegistry != nullptr
           ? objectRegistry->documentEpoch(const_cast<mdl::Map&>(map))
           : 0;
}

QString documentFingerprint(
  const mdl::Map& map, const automation::AutomationObjectRegistry* objectRegistry)
{
  return objectRegistry != nullptr
           ? objectRegistry->documentFingerprint(const_cast<mdl::Map&>(map))
           : QString{};
}

QJsonObject documentJson(
  const MapWindow& mapWindow, const automation::AutomationObjectRegistry* objectRegistry)
{
  const auto& map = mapWindow.document().map();
  return QJsonObject{
    {"fileName", QString::fromStdString(map.filename())},
    {"path", documentPathString(map)},
    {"persistent", map.persistent()},
    {"modified", map.modified()},
    {"game", QString::fromStdString(map.gameInfo().gameConfig.name)},
    {"mapFormat", QString::fromStdString(mdl::formatName(map.worldNode().mapFormat()))},
    {"windowTitle", mapWindow.windowTitle()},
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

QJsonObject activeDocumentJson(
  AppController& appController,
  const QString& bridgeInstanceId,
  const QString& bridgeStartedAt,
  const quint16 httpPort,
  const automation::AutomationObjectRegistry* objectRegistry)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (mapWindow == nullptr)
  {
    auto result = bridgeIdentityJson(bridgeInstanceId, bridgeStartedAt, httpPort);
    result.insert("activeDocument", false);
    return result;
  }

  auto result = documentJson(*mapWindow, objectRegistry);
  result.insert("activeDocument", true);
  addBridgeIdentity(result, bridgeInstanceId, bridgeStartedAt, httpPort);
  return result;
}

QJsonObject mapSnapshotJson(
  AppController& appController,
  const QString& bridgeInstanceId,
  const QString& bridgeStartedAt,
  const quint16 httpPort,
  const automation::AutomationObjectRegistry* objectRegistry)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (mapWindow == nullptr)
  {
    auto result = bridgeIdentityJson(bridgeInstanceId, bridgeStartedAt, httpPort);
    result.insert("activeDocument", false);
    return result;
  }

  auto result = mapSnapshotJsonForMap(
    mapWindow->document().map(), documentJson(*mapWindow, objectRegistry));
  addBridgeIdentity(result, bridgeInstanceId, bridgeStartedAt, httpPort);
  return result;
}

QJsonObject makeStatus(
  AppController& appController,
  const mcp::McpBridgeConfig& config,
  const QString& bridgeInstanceId,
  const QString& bridgeStartedAt,
  const automation::AutomationObjectRegistry* objectRegistry)
{
  auto result = QJsonObject{
    {"application", "TrenchBroom"},
    {"version", getBuildVersion()},
    {"mode", mcp::modeName(config.mode)},
    {"pipeName", config.pipeName},
    {"documentCount",
     static_cast<int>(appController.mapWindowManager().mapWindows().size())},
  };
  addBridgeIdentity(result, bridgeInstanceId, bridgeStartedAt, config.httpPort);

  if (auto* mapWindow = appController.mapWindowManager().topMapWindow())
  {
    const auto document = documentJson(*mapWindow, objectRegistry);
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
