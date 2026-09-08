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
#include <QDeadlineTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include "McpBridgeServerTools.h"
#include "fs/DiskIO.h"
#include "mcp/McpError.h"
#include "mcp/McpToolCatalog.h"
#include "mdl/Brush.h"
#include "mdl/BrushNode.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/GameInfo.h"
#include "mdl/GameManager.h"
#include "mdl/Grid.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "mdl/MapHeader.h"
#include "mdl/Node.h"
#include "mdl/PatchNode.h"
#include "mdl/WorldNode.h"
#include "ui/AppController.h"
#include "ui/GetVersion.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/QPathUtils.h"
#include "ui/automation/AutomationDocuments.h"
#include "ui/automation/AutomationMapSnapshot.h"

#include "vm/bbox.h"

#include <filesystem>
#include <functional>

namespace tb::ui
{
namespace mcp = tb::mcp;

namespace
{

QJsonObject mcpSkillHintsJson()
{
  return QJsonObject{
    {"associatedSkills", QJsonArray{"trenchbroom-mcp-scene-workflow"}},
    {"skillWorkflowHint",
     "Use trenchbroom-mcp-scene-workflow when building or editing TrenchBroom "
     "scenes with MCP."},
    {"recipeWorkflowHint",
     "For prefab-like scenes, routes, or architecture, use skill recipes to emit "
     "IR, then apply via ir_compile_preview_from_file and ir_apply_from_file."},
  };
}

void addMcpSkillHints(QJsonObject& object)
{
  const auto hints = mcpSkillHintsJson();
  for (auto it = hints.begin(); it != hints.end(); ++it)
  {
    object.insert(it.key(), it.value());
  }
}

QJsonArray vecToJson(const vm::vec3d& value)
{
  return QJsonArray{
    value.x(),
    value.y(),
    value.z(),
  };
}

QJsonObject boundsToJson(const vm::bbox3d& bounds)
{
  return QJsonObject{
    {"min", vecToJson(bounds.min)},
    {"max", vecToJson(bounds.max)},
  };
}

QString pathToQString(const std::filesystem::path& path)
{
  return path.empty() ? QString{} : pathAsQString(path);
}

QString documentPathString(const mdl::Map& map)
{
  return pathToQString(map.path());
}

QJsonObject documentOpenDiagnostic(
  AppController& appController,
  const std::filesystem::path& path,
  const QString& stage,
  const QString& message,
  const QString& bridgeInstanceId = {})
{
  return QJsonObject{
    {"opened", false},
    {"verified", false},
    {"stage", stage},
    {"path", pathToQString(path)},
    {"toolName", "documents_open_verified"},
    {"activeDocumentPath", tb::ui::activeDocumentPath(appController)},
    {"bridgeInstanceId", bridgeInstanceId},
    {"message", message},
  };
}

McpBridgeToolResult openDocumentForMcp(
  AppController& appController,
  const std::filesystem::path& path,
  const QString& stage,
  const QString& bridgeInstanceId = {})
{
  const auto result = openAutomationDocument(appController, path);
  if (!result)
  {
    return McpBridgeToolResult::success(documentOpenDiagnostic(
      appController,
      path,
      "openFailed",
      QString{"%1 %2"}.arg(stage, resultErrorMessage(result)).trimmed(),
      bridgeInstanceId));
  }

  return McpBridgeToolResult::success(QJsonObject{{"opened", true}});
}

int legacyDocumentEpoch(const mdl::Map& map)
{
  return static_cast<int>(
    qHash(QString::number(reinterpret_cast<quintptr>(&map), 16)) & 0x7fffffff);
}

int documentEpoch(const mdl::Map& map, const McpObjectRegistry* objectRegistry)
{
  return documentEpochForMap(map, objectRegistry);
}

QString legacyDocumentFingerprint(const mdl::Map& map)
{
  auto hash = qHash(QString::fromStdString(map.filename()));
  hash ^= qHash(documentPathString(map)) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
  hash ^= qHash(QString::fromStdString(map.gameInfo().gameConfig.name)) + 0x9e3779b9u
          + (hash << 6) + (hash >> 2);
  hash ^= qHash(QString::number(reinterpret_cast<quintptr>(&map), 16)) + 0x9e3779b9u
          + (hash << 6) + (hash >> 2);
  return QString{"doc:%1"}.arg(static_cast<quint64>(hash), 16, 16, QLatin1Char{'0'});
}

QString documentFingerprint(const mdl::Map& map, const McpObjectRegistry* objectRegistry)
{
  return documentFingerprintForMap(map, objectRegistry);
}

QString nodePathId(const mdl::Node& node, const mdl::WorldNode& worldNode)
{
  if (&node == &worldNode)
  {
    return "node:world";
  }

  auto parts = QStringList{};
  for (const auto index : node.pathFrom(worldNode).indices)
  {
    parts.push_back(QString::number(index));
  }
  return QString{"node:%1"}.arg(parts.join('/'));
}

QString nodeTypeName(const mdl::Node& node)
{
  if (dynamic_cast<const mdl::WorldNode*>(&node) != nullptr)
  {
    return "world";
  }
  if (dynamic_cast<const mdl::LayerNode*>(&node) != nullptr)
  {
    return "layer";
  }
  if (dynamic_cast<const mdl::GroupNode*>(&node) != nullptr)
  {
    return "group";
  }
  if (dynamic_cast<const mdl::EntityNode*>(&node) != nullptr)
  {
    return "entity";
  }
  if (dynamic_cast<const mdl::BrushNode*>(&node) != nullptr)
  {
    return "brush";
  }
  if (dynamic_cast<const mdl::PatchNode*>(&node) != nullptr)
  {
    return "patch";
  }
  return "node";
}

QJsonObject mcpNodeSummaryJson(const mdl::Node& node, const mdl::WorldNode& worldNode)
{
  auto result = QJsonObject{
    {"id", nodePathId(node, worldNode)},
    {"type", nodeTypeName(node)},
    {"name", QString::fromStdString(node.name())},
    {"selected", node.selected()},
    {"childCount", static_cast<int>(node.childCount())},
    {"descendantCount", static_cast<int>(node.descendantCount())},
    {"logicalBounds", boundsToJson(node.logicalBounds())},
  };

  if (const auto* nodeAsWorld = dynamic_cast<const mdl::WorldNode*>(&node))
  {
    result.insert("classname", QString::fromStdString(nodeAsWorld->entity().classname()));
  }
  else if (const auto* entityNode = dynamic_cast<const mdl::EntityNode*>(&node))
  {
    result.insert("classname", QString::fromStdString(entityNode->entity().classname()));
  }
  else if (const auto* brushNode = dynamic_cast<const mdl::BrushNode*>(&node))
  {
    result.insert("faceCount", static_cast<int>(brushNode->brush().faceCount()));
  }

  return result;
}

QJsonObject documentJson(
  const MapWindow& mapWindow,
  const int index,
  const McpObjectRegistry* objectRegistry = nullptr)
{
  const auto& map = mapWindow.document().map();
  const auto epoch = documentEpoch(map, objectRegistry);
  const auto epochKey =
    objectRegistry != nullptr ? QString{"documentEpoch"} : QString{"documentPathEpoch"};
  return QJsonObject{
    {"index", index},
    {"fileName", QString::fromStdString(map.filename())},
    {"path", documentPathString(map)},
    {"persistent", map.persistent()},
    {"modified", map.modified()},
    {"game", QString::fromStdString(map.gameInfo().gameConfig.name)},
    {"mapFormat", QString::fromStdString(mdl::formatName(map.worldNode().mapFormat()))},
    {"windowTitle", mapWindow.windowTitle()},
    {epochKey, epoch},
    {"documentFingerprint", documentFingerprint(map, objectRegistry)},
  };
}

std::filesystem::path absolutePathFromParams(
  const QJsonObject& params, const QString& key, QString& error)
{
  const auto pathString = params.value(key).toString().trimmed();
  if (pathString.isEmpty())
  {
    error = QString{"%1 is required"}.arg(key);
    return {};
  }

  auto path = pathFromQString(pathString);
  if (path.empty() || !path.is_absolute())
  {
    error = QString{"%1 must be an absolute path"}.arg(key);
    return {};
  }

  return path.lexically_normal();
}

MapWindow* documentWindowByParams(
  AppController& appController, const QJsonObject& params, QString& error)
{
  const auto windows = appController.mapWindowManager().mapWindows();
  if (windows.empty())
  {
    error = "No active document";
    return nullptr;
  }

  const auto indexValue = params.value("index");
  if (indexValue.isDouble())
  {
    const auto index = indexValue.toInt(-1);
    if (index < 0 || index >= static_cast<int>(windows.size()))
    {
      error = QString{"Unknown document index: %1"}.arg(index);
      return nullptr;
    }
    return windows[static_cast<size_t>(index)];
  }

  const auto pathValue = params.value("path");
  if (pathValue.isString())
  {
    const auto path = pathFromQString(pathValue.toString()).lexically_normal();
    for (auto* window : windows)
    {
      if (window->document().map().path().lexically_normal() == path)
      {
        return window;
      }
    }
    error = QString{"No open document for path: %1"}.arg(pathValue.toString());
    return nullptr;
  }

  return appController.mapWindowManager().topMapWindow();
}

bool mcpOptionalBool(
  const QJsonObject& params, const QString& key, const bool defaultValue)
{
  const auto value = params.value(key);
  return value.isBool() ? value.toBool() : defaultValue;
}

} // namespace

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

QJsonObject activeDocumentJson(
  AppController& appController,
  const QString& bridgeInstanceId,
  const QString& bridgeStartedAt,
  const quint16 httpPort,
  const McpObjectRegistry* objectRegistry)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    auto result = bridgeIdentityJson(bridgeInstanceId, bridgeStartedAt, httpPort);
    result.insert("activeDocument", false);
    return result;
  }

  auto result = documentJson(*mapWindow, 0, objectRegistry);
  result.insert("activeDocument", true);
  auto identity = bridgeIdentityJson(bridgeInstanceId, bridgeStartedAt, httpPort);
  for (auto it = identity.begin(); it != identity.end(); ++it)
  {
    result.insert(it.key(), it.value());
  }
  return result;
}

QJsonObject mapSnapshotJson(
  AppController& appController,
  const QString& bridgeInstanceId,
  const QString& bridgeStartedAt,
  const quint16 httpPort,
  const McpObjectRegistry* objectRegistry)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    auto result = bridgeIdentityJson(bridgeInstanceId, bridgeStartedAt, httpPort);
    result.insert("activeDocument", false);
    return result;
  }

  auto snapshot = mapSnapshotJsonForMap(
    mapWindow->document().map(), documentJson(*mapWindow, 0, objectRegistry));
  auto identity = bridgeIdentityJson(bridgeInstanceId, bridgeStartedAt, httpPort);
  for (auto it = identity.begin(); it != identity.end(); ++it)
  {
    snapshot.insert(it.key(), it.value());
  }
  snapshot.insert(
    "documentEpoch", documentEpoch(mapWindow->document().map(), objectRegistry));
  snapshot.insert(
    "documentFingerprint",
    documentFingerprint(mapWindow->document().map(), objectRegistry));
  return snapshot;
}

QJsonObject mapSnapshotJsonForMap(const mdl::Map& map, const QJsonObject& document)
{
  const auto& worldNode = map.worldNode();
  const auto summary = collectAutomationMapSnapshot(map);
  const auto mapContentBounds = summary.contentBounds.value_or(vm::bbox3d{});

  auto worldspawn = QJsonObject{};
  for (const auto& property : worldNode.entity().properties())
  {
    worldspawn.insert(
      QString::fromStdString(property.key()), QString::fromStdString(property.value()));
  }

  auto world = mcpNodeSummaryJson(worldNode, worldNode);
  world.insert("selectable", false);
  world.insert("operationSafe", false);
  world.insert("nodeLogicalBounds", world.value("logicalBounds"));
  world.insert("logicalBounds", boundsToJson(mapContentBounds));
  world.insert("contentBounds", boundsToJson(mapContentBounds));

  return QJsonObject{
    {"document", document},
    {"world", world},
    {"worldspawn", worldspawn},
    {"entityCount", summary.pointEntityCount},
    {"brushCount", summary.brushCount},
    {"patchCount", summary.patchCount},
    {"nodeCount", summary.nodeCount},
    {"bounds", boundsToJson(mapContentBounds)},
    {"contentBounds", boundsToJson(mapContentBounds)},
    {"grid",
     QJsonObject{
       {"size", summary.gridSize},
       {"actualSize", summary.gridActualSize},
       {"snap", summary.gridSnap},
       {"visible", summary.gridVisible},
     }},
  };
}

QString documentFingerprintForMap(
  const mdl::Map& map, const McpObjectRegistry* objectRegistry)
{
  return objectRegistry != nullptr
           ? objectRegistry->documentFingerprint(const_cast<mdl::Map&>(map))
           : legacyDocumentFingerprint(map);
}

int documentEpochForMap(const mdl::Map& map, const McpObjectRegistry* objectRegistry)
{
  return objectRegistry != nullptr
           ? objectRegistry->documentEpoch(const_cast<mdl::Map&>(map))
           : legacyDocumentEpoch(map);
}

QJsonObject documentsListJson(
  AppController& appController, const McpObjectRegistry* objectRegistry)
{
  auto documents = QJsonArray{};
  auto index = 0;
  for (const auto* mapWindow : appController.mapWindowManager().mapWindows())
  {
    documents.push_back(documentJson(*mapWindow, index, objectRegistry));
    ++index;
  }

  return QJsonObject{
    {"documents", documents},
    {"count", documents.size()},
  };
}

McpBridgeToolResult documentOpenResult(
  AppController& appController, const QJsonObject& params)
{
  auto error = QString{};
  const auto path = absolutePathFromParams(params, "path", error);
  if (!error.isEmpty())
  {
    return invalidParamsFailure(error);
  }
  if (!std::filesystem::is_regular_file(path))
  {
    return invalidParamsFailure(
      QString{"Document does not exist: %1"}.arg(pathToQString(path)));
  }

  const auto openResult = openDocumentForMcp(
    appController,
    path,
    QString{"Failed to open document without interactive game/format prompt."});
  if (!openResult.ok || !openResult.result.value("opened").toBool(false))
  {
    if (openResult.ok)
    {
      return McpBridgeToolResult::failure(
        mcp::McpErrorCode::InvalidParams,
        openResult.result.value("message").toString(
          QString{"Failed to open document: %1"}.arg(pathToQString(path))),
        openResult.result);
    }
    return openResult;
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"opened", true},
    {"document", activeDocumentJson(appController)},
  });
}

McpBridgeToolResult documentOpenVerifiedResult(
  AppController& appController,
  const QJsonObject& params,
  const QString& bridgeInstanceId,
  const QString& bridgeStartedAt,
  const quint16 httpPort,
  const McpObjectRegistry* objectRegistry)
{
  auto error = QString{};
  const auto path = absolutePathFromParams(params, "path", error);
  if (!error.isEmpty())
  {
    return invalidParamsFailure(error);
  }
  if (!std::filesystem::is_regular_file(path))
  {
    return invalidParamsFailure(
      QString{"Document does not exist: %1"}.arg(pathToQString(path)));
  }

  const auto pathString = pathToQString(path);
  const auto waitMs = std::clamp(params.value("waitMs").toInt(5000), 0, 30000);
  const auto activate = mcpOptionalBool(params, "activate", true);
  if (auto* activeWindow = appController.mapWindowManager().topMapWindow())
  {
    if (activeWindow->document().map().path().lexically_normal() == path)
    {
      if (activate)
      {
        activeWindow->show();
        activeWindow->raise();
        activeWindow->activateWindow();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
      }

      auto snapshot = mapSnapshotJson(
        appController, bridgeInstanceId, bridgeStartedAt, httpPort, objectRegistry);
      return McpBridgeToolResult::success(QJsonObject{
        {"opened", false},
        {"alreadyActive", true},
        {"verified", snapshot.value("document").isObject()},
        {"stage",
         snapshot.value("document").isObject() ? "alreadyActiveVerified"
                                               : "bridgeUnavailableAfterOpen"},
        {"path", pathString},
        {"activeDocumentPath", activeDocumentPath(appController)},
        {"document", documentJson(*activeWindow, 0, objectRegistry)},
        {"documentFingerprint",
         documentFingerprint(activeWindow->document().map(), objectRegistry)},
        {"documentEpoch", documentEpoch(activeWindow->document().map(), objectRegistry)},
        {"snapshot", snapshot},
      });
    }
  }

  auto localError = QString{};
  auto* alreadyOpenWindow =
    documentWindowByParams(appController, QJsonObject{{"path", pathString}}, localError);
  if (alreadyOpenWindow != nullptr)
  {
    if (activate)
    {
      alreadyOpenWindow->show();
      alreadyOpenWindow->raise();
      alreadyOpenWindow->activateWindow();
      QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }

    auto snapshot = mapSnapshotJson(
      appController, bridgeInstanceId, bridgeStartedAt, httpPort, objectRegistry);
    const auto activePath = activeDocumentPath(appController);
    return McpBridgeToolResult::success(QJsonObject{
      {"opened", false},
      {"alreadyOpen", true},
      {"verified", !activate || activePath == pathString},
      {"stage",
       !activate || activePath == pathString ? "alreadyOpenVerified"
                                             : "activationFailed"},
      {"path", pathString},
      {"activeDocumentPath", activePath},
      {"document", documentJson(*alreadyOpenWindow, 0, objectRegistry)},
      {"documentFingerprint",
       documentFingerprint(alreadyOpenWindow->document().map(), objectRegistry)},
      {"documentEpoch",
       documentEpoch(alreadyOpenWindow->document().map(), objectRegistry)},
      {"snapshot", snapshot},
    });
  }

  const auto openResult = openDocumentForMcp(
    appController,
    path,
    QString{"Failed to open document without interactive game/format prompt."},
    bridgeInstanceId);
  if (!openResult.ok || !openResult.result.value("opened").toBool(false))
  {
    return openResult.ok ? openResult
                         : McpBridgeToolResult::success(documentOpenDiagnostic(
                             appController,
                             path,
                             "openFailed",
                             openResult.error.message,
                             bridgeInstanceId));
  }

  const auto deadline = QDeadlineTimer{waitMs};
  auto* verifiedWindow = static_cast<MapWindow*>(nullptr);
  while (!deadline.hasExpired())
  {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    auto lookupError = QString{};
    verifiedWindow = documentWindowByParams(
      appController, QJsonObject{{"path", pathString}}, lookupError);
    if (verifiedWindow != nullptr)
    {
      break;
    }
  }
  if (verifiedWindow == nullptr)
  {
    return McpBridgeToolResult::success(QJsonObject{
      {"opened", true},
      {"verified", false},
      {"stage", "verificationFailed"},
      {"path", pathString},
      {"toolName", "documents_open_verified"},
      {"activeDocumentPath", activeDocumentPath(appController)},
      {"bridgeInstanceId", bridgeInstanceId},
      {"message",
       "Document was opened but could not be found in the open document list."},
      {"documents", documentsListJson(appController).value("documents")},
    });
  }

  if (activate)
  {
    verifiedWindow->show();
    verifiedWindow->raise();
    verifiedWindow->activateWindow();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }

  const auto activePath = activeDocumentPath(appController);
  if (activate && activePath != pathString)
  {
    return McpBridgeToolResult::success(QJsonObject{
      {"opened", true},
      {"verified", false},
      {"stage", "activationFailed"},
      {"path", pathString},
      {"toolName", "documents_open_verified"},
      {"activeDocumentPath", activePath},
      {"bridgeInstanceId", bridgeInstanceId},
      {"document", documentJson(*verifiedWindow, 0)},
    });
  }

  auto snapshot = mapSnapshotJson(
    appController, bridgeInstanceId, bridgeStartedAt, httpPort, objectRegistry);
  if (snapshot.isEmpty() || !snapshot.value("document").isObject())
  {
    return McpBridgeToolResult::success(QJsonObject{
      {"opened", true},
      {"verified", false},
      {"stage", "bridgeUnavailableAfterOpen"},
      {"path", pathString},
      {"toolName", "documents_open_verified"},
      {"activeDocumentPath", activeDocumentPath(appController)},
      {"bridgeInstanceId", bridgeInstanceId},
      {"document", documentJson(*verifiedWindow, 0, objectRegistry)},
    });
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"opened", true},
    {"verified", true},
    {"stage", "verified"},
    {"path", pathString},
    {"document", documentJson(*verifiedWindow, 0, objectRegistry)},
    {"activeDocumentPath", activeDocumentPath(appController)},
    {"documentFingerprint",
     documentFingerprint(verifiedWindow->document().map(), objectRegistry)},
    {"documentEpoch", documentEpoch(verifiedWindow->document().map(), objectRegistry)},
    {"snapshot", snapshot},
  });
}

QString activeDocumentPath(AppController& appController)
{
  if (auto* mapWindow = appController.mapWindowManager().topMapWindow())
  {
    return documentPathString(mapWindow->document().map());
  }
  return {};
}

McpBridgeToolResult expectedDocumentPathFailure(
  AppController& appController,
  const QString& expectedPath,
  const QString& bridgeInstanceId,
  const QString& bridgeStartedAt,
  const quint16 httpPort)
{
  auto config = mcp::McpBridgeConfig{};
  config.httpPort = httpPort;
  auto status = makeStatus(appController, config, bridgeInstanceId, bridgeStartedAt);
  status.insert("expectedDocumentPath", expectedPath);
  status.insert("actualDocumentPath", activeDocumentPath(appController));
  return McpBridgeToolResult::failure(
    mcp::McpErrorCode::Forbidden,
    QString{
      "Active document does not match expectedDocumentPath. Expected '%1', actual '%2'."}
      .arg(expectedPath, activeDocumentPath(appController)),
    status);
}

McpBridgeToolResult documentActivateResult(
  AppController& appController, const QJsonObject& params)
{
  auto error = QString{};
  auto* mapWindow = documentWindowByParams(appController, params, error);
  if (!mapWindow)
  {
    return error == "No active document" ? noActiveDocumentFailure()
                                         : invalidParamsFailure(error);
  }

  if (!activateAutomationDocument(appController, *mapWindow))
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Document window is no longer available");
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"activated", true},
    {"document", documentJson(*mapWindow, 0)},
  });
}

McpBridgeToolResult documentSaveResult(
  AppController& appController, const QJsonObject& params)
{
  auto error = QString{};
  auto* mapWindow = documentWindowByParams(appController, params, error);
  if (!mapWindow)
  {
    return error == "No active document" ? noActiveDocumentFailure()
                                         : invalidParamsFailure(error);
  }

  auto& map = mapWindow->document().map();
  const auto pathValue = params.value("path");
  const auto savePath = pathValue.isString() && !pathValue.toString().trimmed().isEmpty()
                          ? absolutePathFromParams(params, "path", error)
                          : std::filesystem::path{};
  if (!error.isEmpty())
  {
    return invalidParamsFailure(error);
  }

  if (savePath.empty() && !map.persistent())
  {
    return invalidParamsFailure("Transient documents require absolute path");
  }

  const auto result = saveAutomationDocument(
    map, savePath.empty() ? std::nullopt : std::make_optional(savePath));
  if (!result)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, resultErrorMessage(result));
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"saved", true},
    {"path", pathToQString(map.path())},
    {"document", documentJson(*mapWindow, 0)},
  });
}

McpBridgeToolResult documentCloseResult(
  AppController& appController, const QJsonObject& params)
{
  auto error = QString{};
  auto* mapWindow = documentWindowByParams(appController, params, error);
  if (!mapWindow)
  {
    return error == "No active document" ? noActiveDocumentFailure()
                                         : invalidParamsFailure(error);
  }

  if (
    mapWindow->document().map().modified()
    && !mcpOptionalBool(params, "discardChanges", false))
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::Forbidden,
      "Document has unsaved changes; pass discardChanges=true to close it");
  }

  const auto document = documentJson(*mapWindow, 0);
  closeAutomationDocument(*mapWindow, mcpOptionalBool(params, "discardChanges", false));

  return McpBridgeToolResult::success(QJsonObject{
    {"closed", true},
    {"document", document},
  });
}

McpBridgeToolResult documentExportResult(
  AppController& appController, const QJsonObject& params)
{
  auto error = QString{};
  auto* mapWindow = documentWindowByParams(appController, params, error);
  if (!mapWindow)
  {
    return error == "No active document" ? noActiveDocumentFailure()
                                         : invalidParamsFailure(error);
  }

  const auto exportPath = absolutePathFromParams(params, "path", error);
  if (!error.isEmpty())
  {
    return invalidParamsFailure(error);
  }
  if (exportPath == mapWindow->document().map().path())
  {
    return invalidParamsFailure("Export path must not overwrite the current document");
  }

  const auto stripTbProperties = mcpOptionalBool(params, "stripTbProperties", true);
  const auto result =
    exportAutomationDocument(mapWindow->document().map(), exportPath, stripTbProperties);
  if (!result)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, resultErrorMessage(result));
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"exported", true},
    {"path", pathToQString(exportPath)},
    {"stripTbProperties", stripTbProperties},
  });
}

QJsonObject makeStatus(
  AppController& appController,
  const mcp::McpBridgeConfig& config,
  const QString& bridgeInstanceId,
  const QString& bridgeStartedAt,
  const McpObjectRegistry* objectRegistry)
{
  auto result = QJsonObject{
    {"application", "TrenchBroom"},
    {"version", getBuildVersion()},
    {"mode", mcp::modeName(config.mode)},
    {"pipeName", config.pipeName},
    {"processId", static_cast<int>(QCoreApplication::applicationPid())},
    {"bridgeInstanceId", bridgeInstanceId},
    {"bridgeStartedAt", bridgeStartedAt},
    {"httpPort", static_cast<int>(config.httpPort)},
    {"documentCount",
     static_cast<int>(appController.mapWindowManager().mapWindows().size())},
    {"openDocumentCount",
     static_cast<int>(appController.mapWindowManager().mapWindows().size())},
    {"openDocumentsSummary", documentsListJson(appController).value("documents")},
    {"activeDocument", false},
  };
  addMcpSkillHints(result);

  if (auto* mapWindow = appController.mapWindowManager().topMapWindow())
  {
    const auto& map = mapWindow->document().map();
    result.insert("activeDocument", true);
    result.insert("activeDocumentFileName", QString::fromStdString(map.filename()));
    result.insert("activeDocumentPath", documentPathString(map));
    result.insert("activeDocumentIndex", 0);
    result.insert("activeDocumentModified", map.modified());
    result.insert("activeDocumentDirty", map.modified());
    result.insert("activeWindowTitle", mapWindow->windowTitle());
    result.insert("activeDocumentWindowTitle", mapWindow->windowTitle());
    result.insert("documentEpoch", documentEpoch(map, objectRegistry));
    result.insert("documentFingerprint", documentFingerprint(map, objectRegistry));
  }

  return result;
}

QJsonObject doctorJson(
  AppController& appController, const mcp::McpBridgeConfig& config, const bool fullDetail)
{
  auto result = QJsonObject{
    {"configPath", mcp::defaultConfigPath()},
    {"pipeName", config.pipeName},
    {"mode", mcp::modeName(config.mode)},
    {"authentication", "none"},
    {"listening", config.mode != mcp::McpMode::Off},
    {"documentCount",
     static_cast<int>(appController.mapWindowManager().mapWindows().size())},
    {"activeDocument", appController.mapWindowManager().topMapWindow() != nullptr},
    {"schemaLookupHint",
     "Use tb_tools_search(detail:\"schema\", query:\"exact_tool_name\") to inspect one "
     "tool schema."},
  };
  addMcpSkillHints(result);

  result.insert("implementedToolCount", mcp::toolsSummaryJson(config.mode).size());

  if (fullDetail)
  {
    result.insert("detail", "full");
    result.insert("implementedTools", mcp::toolsSummaryJson(config.mode));
    result.insert("toolDiagnostics", mcp::toolDiagnosticsJson(config.mode));
  }
  else
  {
    result.insert("detail", "summary");
  }

  return result;
}

} // namespace tb::ui
