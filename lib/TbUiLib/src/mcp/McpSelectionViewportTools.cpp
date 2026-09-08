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
#include "McpSelectionQuery.h"
#include "mcp/McpError.h"
#include "mdl/Brush.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushFaceHandle.h"
#include "mdl/BrushNode.h"
#include "mdl/EditorContext.h"
#include "mdl/Entity.h"
#include "mdl/EntityNodeBase.h"
#include "mdl/Map.h"
#include "mdl/Map_Selection.h"
#include "mdl/Node.h"
#include "mdl/Selection.h"
#include "mdl/WorldNode.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/automation/AutomationEntities.h"
#include "ui/automation/AutomationNodes.h"

#include <map>
#include <set>
#include <string>
#include <vector>

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

McpBridgeToolResult selectionPreconditionFailure(
  const QString& message, const QString& recoveryAction, QJsonObject details = {})
{
  details.insert("mutatedDocument", false);
  details.insert("retrySafe", true);
  details.insert("recoveryAction", recoveryAction);
  return McpBridgeToolResult::failure(
    mcp::McpErrorCode::InvalidParams, message, std::move(details));
}

QJsonObject mcpNodeSummaryJson(const mdl::Node& node, const mdl::WorldNode& worldNode)
{
  auto result = QJsonObject{
    {"id", mcpNodePathId(node, worldNode)},
    {"type", mcpNodeTypeName(node)},
    {"name", QString::fromStdString(node.name())},
    {"selected", node.selected()},
    {"childCount", static_cast<int>(node.childCount())},
    {"descendantCount", static_cast<int>(node.descendantCount())},
    {"logicalBounds", boundsToJson(node.logicalBounds())},
  };

  if (const auto* entityNode = dynamic_cast<const mdl::EntityNodeBase*>(&node))
  {
    result.insert("classname", QString::fromStdString(entityNode->entity().classname()));
  }
  if (const auto* brushNode = dynamic_cast<const mdl::BrushNode*>(&node))
  {
    result.insert("faceCount", static_cast<int>(brushNode->brush().faceCount()));
  }
  return result;
}

QJsonObject entityPropertiesJson(const mdl::Entity& entity)
{
  auto result = QJsonObject{};
  for (const auto& property : entity.properties())
  {
    result.insert(
      QString::fromStdString(property.key()), QString::fromStdString(property.value()));
  }
  return result;
}

QString entityPropertyString(const mdl::Entity& entity, const QString& key)
{
  if (const auto* value = entity.property(key.toStdString()))
  {
    return QString::fromStdString(*value);
  }
  return {};
}

QJsonObject brushMaterialsJson(const mdl::Brush& brush)
{
  auto counts = std::map<std::string, int>{};
  for (const auto& face : brush.faces())
  {
    ++counts[face.materialName()];
  }

  auto materials = QJsonArray{};
  for (const auto& [material, count] : counts)
  {
    materials.push_back(QJsonObject{
      {"material", QString::fromStdString(material)},
      {"faceCount", count},
    });
  }
  return QJsonObject{
    {"uniqueMaterialCount", static_cast<int>(counts.size())},
    {"materials", materials},
  };
}

QJsonArray brushFacesJson(const mdl::Brush& brush)
{
  auto result = QJsonArray{};
  auto index = 0;
  for (const auto& face : brush.faces())
  {
    result.push_back(QJsonObject{
      {"index", index++},
      {"material", QString::fromStdString(face.materialName())},
      {"normal", vecToJson(face.normal())},
    });
  }
  return result;
}

bool entityMatchesClassname(
  const mdl::EntityNodeBase& entityNode, const QString& classname)
{
  return classname.isEmpty()
         || QString::fromStdString(entityNode.entity().classname())
                .compare(classname, Qt::CaseInsensitive)
              == 0;
}

void collectEntityNodes(
  const mdl::Node& node,
  const mdl::WorldNode& worldNode,
  const QString& classname,
  std::vector<const mdl::EntityNodeBase*>& entities)
{
  if (&node != &worldNode)
  {
    if (const auto* entityNode = dynamic_cast<const mdl::EntityNodeBase*>(&node))
    {
      if (entityMatchesClassname(*entityNode, classname))
      {
        entities.push_back(entityNode);
      }
    }
  }

  for (const auto* child : node.children())
  {
    collectEntityNodes(*child, worldNode, classname, entities);
  }
}

QJsonObject linkedEntityNodeJson(
  const mdl::EntityNodeBase& entityNode,
  const mdl::WorldNode& worldNode,
  const QString& nameKey,
  const QString& nextKey,
  const QString& detail)
{
  const auto& entity = entityNode.entity();
  auto result = QJsonObject{
    {"objectId", mcpNodePathId(entityNode, worldNode)},
    {"type", "entity"},
    {"classname", QString::fromStdString(entity.classname())},
    {"origin", vecToJson(entity.origin())},
    {nameKey, entityPropertyString(entity, nameKey)},
    {nextKey, entityPropertyString(entity, nextKey)},
  };
  if (detail == "full")
  {
    result.insert("properties", entityPropertiesJson(entity));
  }
  return result;
}

McpBridgeToolResult entityLinkPreconditionFailure(
  const QString& message, const QString& recoveryAction, QJsonObject details = {})
{
  details.insert("mutatedDocument", false);
  details.insert("retrySafe", true);
  details.insert("recoveryAction", recoveryAction);
  return McpBridgeToolResult::failure(
    mcp::McpErrorCode::InvalidParams, message, std::move(details));
}

QJsonObject selectedFaceJson(
  const mdl::BrushFaceHandle& handle, const mdl::WorldNode& worldNode)
{
  const auto& face = handle.face();
  return QJsonObject{
    {"type", "face"},
    {"brushId", mcpNodePathId(*handle.node(), worldNode)},
    {"faceIndex", static_cast<int>(handle.faceIndex())},
    {"material", QString::fromStdString(face.materialName())},
    {"normal", vecToJson(face.normal())},
  };
}

QJsonArray childSummariesJson(const mdl::Node& node, const mdl::WorldNode& worldNode)
{
  auto result = QJsonArray{};
  for (const auto* child : node.children())
  {
    result.push_back(mcpNodeSummaryJson(*child, worldNode));
  }
  return result;
}

QJsonObject mcpNodeInspectJson(
  const mdl::Node& node,
  const mdl::WorldNode& worldNode,
  const QString& detail,
  const bool includeProperties,
  const bool includeChildren)
{
  auto result = mcpNodeSummaryJson(node, worldNode);

  if (const auto* entityNode = dynamic_cast<const mdl::EntityNodeBase*>(&node))
  {
    result.insert("origin", vecToJson(entityNode->entity().origin()));
    if (includeProperties || detail == "full")
    {
      result.insert("properties", entityPropertiesJson(entityNode->entity()));
    }
  }

  if (const auto* brushNode = dynamic_cast<const mdl::BrushNode*>(&node))
  {
    result.insert("materials", brushMaterialsJson(brushNode->brush()));
    if (detail == "full")
    {
      result.insert("faces", brushFacesJson(brushNode->brush()));
    }
  }

  if (includeChildren || detail == "full")
  {
    result.insert("children", childSummariesJson(node, worldNode));
  }

  return result;
}

bool textMatches(const QString& text, const QString& query)
{
  return text.contains(query, Qt::CaseInsensitive);
}

bool nodeMatchesQuery(
  const mdl::Node& node, const mdl::WorldNode& worldNode, const QString& query)
{
  if (
    textMatches(mcpNodePathId(node, worldNode), query)
    || textMatches(mcpNodeTypeName(node), query)
    || textMatches(QString::fromStdString(node.name()), query))
  {
    return true;
  }

  if (const auto* entityNode = dynamic_cast<const mdl::EntityNodeBase*>(&node))
  {
    if (textMatches(QString::fromStdString(entityNode->entity().classname()), query))
    {
      return true;
    }
    for (const auto& property : entityNode->entity().properties())
    {
      if (
        textMatches(QString::fromStdString(property.key()), query)
        || textMatches(QString::fromStdString(property.value()), query))
      {
        return true;
      }
    }
  }
  return false;
}

void collectSearchResults(
  const mdl::Node& node,
  const mdl::WorldNode& worldNode,
  const QString& query,
  QJsonArray& results)
{
  if (nodeMatchesQuery(node, worldNode, query))
  {
    results.push_back(mcpNodeSummaryJson(node, worldNode));
  }
  for (const auto* child : node.children())
  {
    collectSearchResults(*child, worldNode, query, results);
  }
}

bool mcpOptionalBool(
  const QJsonObject& params, const QString& key, const bool defaultValue)
{
  const auto value = params.value(key);
  return value.isBool() ? value.toBool() : defaultValue;
}

McpBridgeToolResult retiredViewportToolResult(
  const QString& toolName, const QString& replacement)
{
  return McpBridgeToolResult::failure(
    mcp::McpErrorCode::InvalidParams,
    QString{"%1 is retired from the lightweight MCP runtime"}.arg(toolName),
    QJsonObject{
      {"retired", true},
      {"toolName", toolName},
      {"replacement", replacement},
      {"mutatedDocument", false},
      {"note",
       "Use geometry review tools for normal Agent visual validation. Exact-name tool "
       "search still returns this legacy viewport tool for migration guidance."},
    });
}

} // namespace

QJsonObject mapSearchJson(AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  const auto query = params.value("query").toString().trimmed();
  if (!mapWindow || query.isEmpty())
  {
    return QJsonObject{{"query", query}, {"results", QJsonArray{}}, {"count", 0}};
  }

  const auto& worldNode = mapWindow->document().map().worldNode();
  auto results = QJsonArray{};
  collectSearchResults(worldNode, worldNode, query, results);
  return QJsonObject{{"query", query}, {"results", results}, {"count", results.size()}};
}

QJsonObject selectionJson(AppController& appController)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return {};
  }
  return selectionJsonForMap(mapWindow->document().map());
}

QJsonObject selectionJsonForMap(const mdl::Map& map)
{
  const auto& worldNode = map.worldNode();
  const auto& selection = map.selection();

  auto nodes = QJsonArray{};
  auto selectedBrushTotalFaceCount = 0;
  for (const auto* node : selection.nodes)
  {
    nodes.push_back(mcpNodeSummaryJson(*node, worldNode));
    if (const auto* brushNode = dynamic_cast<const mdl::BrushNode*>(node))
    {
      selectedBrushTotalFaceCount += static_cast<int>(brushNode->brush().faceCount());
    }
  }

  auto faceOwnerBrushIds = QJsonArray{};
  auto faceOwnerBrushes = QJsonArray{};
  auto faceCountsByBrush = std::map<const mdl::BrushNode*, int>{};
  for (const auto& handle : selection.brushFaces)
  {
    ++faceCountsByBrush[handle.node()];
  }
  for (const auto& [brushNode, faceCount] : faceCountsByBrush)
  {
    if (brushNode == nullptr)
    {
      continue;
    }
    const auto objectId = mcpNodePathId(*brushNode, worldNode);
    faceOwnerBrushIds.push_back(objectId);
    faceOwnerBrushes.push_back(QJsonObject{
      {"id", objectId},
      {"type", "brush"},
      {"selectedFaceCount", faceCount},
      {"faceCount", static_cast<int>(brushNode->brush().faceCount())},
      {"logicalBounds", boundsToJson(brushNode->logicalBounds())},
    });
  }

  return QJsonObject{
    {"hasSelection", selection.hasAny()},
    {"nodes", nodes},
    {"nodeCount", static_cast<int>(selection.nodes.size())},
    {"groupCount", static_cast<int>(selection.groups.size())},
    {"entityCount", static_cast<int>(selection.entities.size())},
    {"brushCount", static_cast<int>(selection.brushes.size())},
    {"patchCount", static_cast<int>(selection.patches.size())},
    {"brushFaceCount", static_cast<int>(selection.brushFaces.size())},
    {"selectedBrushFaceCount", selectedBrushTotalFaceCount},
    {"faceOwnerBrushCount", faceOwnerBrushIds.size()},
    {"faceOwnerBrushIds", faceOwnerBrushIds},
    {"faceOwnerBrushes", faceOwnerBrushes},
    {"selectedBrushTotalFaceCount", selectedBrushTotalFaceCount},
  };
}

McpBridgeToolResult selectionInspectForMapResult(mdl::Map& map, const QJsonObject& params)
{
  const auto detailValue = params.value("detail").toString("summary").trimmed().toLower();
  if (!detailValue.isEmpty() && detailValue != "summary" && detailValue != "full")
  {
    return selectionPreconditionFailure(
      "selection_inspect detail must be summary or full",
      "fix_selection_inspect_detail_then_retry",
      QJsonObject{{"detail", detailValue}});
  }

  const auto detail = detailValue == "full" ? QString{"full"} : QString{"summary"};
  const auto includeProperties =
    mcpOptionalBool(params, "includeProperties", detail == "full");
  const auto includeChildren = mcpOptionalBool(params, "includeChildren", false);

  const auto& worldNode = map.worldNode();
  const auto& selection = map.selection();
  auto objects = QJsonArray{};
  auto selectedBrushTotalFaceCount = 0;
  for (const auto* node : selection.nodes)
  {
    objects.push_back(
      mcpNodeInspectJson(*node, worldNode, detail, includeProperties, includeChildren));
    if (const auto* brushNode = dynamic_cast<const mdl::BrushNode*>(node))
    {
      selectedBrushTotalFaceCount += static_cast<int>(brushNode->brush().faceCount());
    }
  }

  auto faceOwnerBrushes = QJsonArray{};
  auto selectedFaces = QJsonArray{};
  auto faceCountsByBrush = std::map<const mdl::BrushNode*, int>{};
  for (const auto& handle : selection.brushFaces)
  {
    if (handle.node() == nullptr)
    {
      continue;
    }
    ++faceCountsByBrush[handle.node()];
    selectedFaces.push_back(selectedFaceJson(handle, worldNode));
  }
  for (const auto& [brushNode, faceCount] : faceCountsByBrush)
  {
    if (brushNode == nullptr)
    {
      continue;
    }
    auto faceOwner = mcpNodeInspectJson(
      *brushNode, worldNode, detail, includeProperties, includeChildren);
    faceOwner.insert("selectedFaceCount", faceCount);
    faceOwnerBrushes.push_back(std::move(faceOwner));
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"hasSelection", selection.hasAny()},
    {"selectedCount", static_cast<int>(selection.nodes.size())},
    {"selectedFaceCount", static_cast<int>(selection.brushFaces.size())},
    {"detail", detail},
    {"includeProperties", includeProperties},
    {"includeChildren", includeChildren},
    {"objects", objects},
    {"selectedFaces", selectedFaces},
    {"faceOwnerBrushes", faceOwnerBrushes},
    {"groupCount", static_cast<int>(selection.groups.size())},
    {"entityCount", static_cast<int>(selection.entities.size())},
    {"brushCount", static_cast<int>(selection.brushes.size())},
    {"patchCount", static_cast<int>(selection.patches.size())},
    {"selectedBrushFaceCount", selectedBrushTotalFaceCount},
    {"mutatedDocument", false},
  });
}

McpBridgeToolResult selectionInspectResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }
  return selectionInspectForMapResult(mapWindow->document().map(), params);
}

McpBridgeToolResult entityLinkChainInspectForMapResult(
  mdl::Map& map, const QJsonObject& params)
{
  const auto classname = params.value("classname").toString().trimmed();
  const auto nameKey = params.value("nameKey").toString("targetname").trimmed();
  const auto nextKey = params.value("nextKey").toString("target").trimmed();
  const auto detailValue = params.value("detail").toString("summary").trimmed().toLower();
  if (nameKey.isEmpty() || nextKey.isEmpty())
  {
    return entityLinkPreconditionFailure(
      "entity_link_chain_inspect requires non-empty nameKey and nextKey",
      "provide_entity_link_keys_then_retry");
  }
  if (detailValue != "summary" && detailValue != "full")
  {
    return entityLinkPreconditionFailure(
      "entity_link_chain_inspect detail must be summary or full",
      "fix_entity_link_detail_then_retry",
      QJsonObject{{"detail", detailValue}});
  }
  const auto includeAllNodes = mcpOptionalBool(params, "includeAllNodes", false);

  const auto& worldNode = map.worldNode();
  auto candidates = std::vector<const mdl::EntityNodeBase*>{};
  collectEntityNodes(worldNode, worldNode, classname, candidates);

  auto byName = std::map<QString, std::vector<const mdl::EntityNodeBase*>>{};
  for (const auto* entityNode : candidates)
  {
    const auto name = entityPropertyString(entityNode->entity(), nameKey);
    if (!name.isEmpty())
    {
      byName[name].push_back(entityNode);
    }
  }

  auto duplicateNames = QJsonArray{};
  for (const auto& [name, nodes] : byName)
  {
    if (nodes.size() > 1u)
    {
      duplicateNames.push_back(QJsonObject{
        {"name", name},
        {"count", static_cast<int>(nodes.size())},
      });
    }
  }
  const auto candidateDetails = [&]() {
    auto details = QJsonObject{
      {"candidateNodeCount", static_cast<int>(candidates.size())},
      {"duplicateNameCount", duplicateNames.size()},
    };
    if (includeAllNodes)
    {
      auto allNodes = QJsonArray{};
      for (const auto* entityNode : candidates)
      {
        allNodes.push_back(
          linkedEntityNodeJson(*entityNode, worldNode, nameKey, nextKey, "summary"));
      }
      details.insert("allNodes", allNodes);
      details.insert("duplicateNames", duplicateNames);
    }
    return details;
  };

  const auto startObject = params.value("start").toObject();
  const auto startSource =
    startObject.value("source").toString("selection").trimmed().toLower();
  auto* startNode = static_cast<const mdl::EntityNodeBase*>(nullptr);
  if (startSource == "selection")
  {
    auto selectedCandidates = std::vector<const mdl::EntityNodeBase*>{};
    for (const auto* node : map.selection().nodes)
    {
      if (const auto* entityNode = dynamic_cast<const mdl::EntityNodeBase*>(node))
      {
        if (entityMatchesClassname(*entityNode, classname))
        {
          selectedCandidates.push_back(entityNode);
        }
      }
    }
    if (selectedCandidates.size() != 1u)
    {
      auto details = candidateDetails();
      details.insert(
        "selectedMatchingEntityCount", static_cast<int>(selectedCandidates.size()));
      details.insert("classname", classname);
      return entityLinkPreconditionFailure(
        "entity_link_chain_inspect requires exactly one selected matching entity when "
        "start.source is selection",
        "select_one_start_entity_or_use_start_targetname",
        std::move(details));
    }
    startNode = selectedCandidates.front();
  }
  else if (startSource == "targetname")
  {
    const auto startName = startObject.value("targetname").toString().trimmed();
    if (startName.isEmpty())
    {
      return entityLinkPreconditionFailure(
        "start.targetname must be non-empty when start.source is targetname",
        "provide_start_targetname_then_retry");
    }
    const auto it = byName.find(startName);
    if (it == byName.end())
    {
      return entityLinkPreconditionFailure(
        "start targetname was not found",
        "choose_existing_start_targetname_then_retry",
        QJsonObject{{"targetname", startName}});
    }
    if (it->second.size() != 1u)
    {
      return entityLinkPreconditionFailure(
        "start targetname is ambiguous",
        "choose_unique_start_targetname_then_retry",
        QJsonObject{
          {"targetname", startName},
          {"matchCount", static_cast<int>(it->second.size())}});
    }
    startNode = it->second.front();
  }
  else
  {
    return entityLinkPreconditionFailure(
      "start.source must be selection or targetname",
      "fix_entity_link_start_source_then_retry",
      QJsonObject{{"source", startSource}});
  }

  const auto chain =
    automation::inspectEntityLinkChain(map, *startNode, classname, nameKey, nextKey);
  if (!chain.error.isEmpty())
  {
    return McpBridgeToolResult::failure(mcp::McpErrorCode::InternalError, chain.error);
  }

  auto nodes = QJsonArray{};
  for (const auto* node : chain.nodes)
  {
    nodes.push_back(
      linkedEntityNodeJson(*node, worldNode, nameKey, nextKey, detailValue));
  }
  auto warnings = QJsonArray{};
  for (const auto& warning : chain.warnings)
  {
    warnings.push_back(QJsonObject{
      {"status", warning.status},
      {"objectId", mcpNodePathId(*warning.node, worldNode)},
      {"key", warning.key},
    });
  }

  auto result = QJsonObject{
    {"classname", classname},
    {"nameKey", nameKey},
    {"nextKey", nextKey},
    {"startObjectId", mcpNodePathId(*startNode, worldNode)},
    {"startSource", startSource},
    {"chainComplete", chain.chainComplete},
    {"hasCycle", chain.hasCycle},
    {"nodeCount", nodes.size()},
    {"edgeCount", chain.edges.size()},
    {"candidateNodeCount", static_cast<int>(candidates.size())},
    {"duplicateNameCount", duplicateNames.size()},
    {"nodes", nodes},
    {"edges", chain.edges},
    {"failures", chain.failures},
    {"warnings", warnings},
    {"mutatedDocument", false},
  };

  if (includeAllNodes)
  {
    auto allNodes = QJsonArray{};
    for (const auto* entityNode : candidates)
    {
      allNodes.push_back(
        linkedEntityNodeJson(*entityNode, worldNode, nameKey, nextKey, "summary"));
    }
    result.insert("allNodes", allNodes);
    result.insert("duplicateNames", duplicateNames);
  }
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult entityLinkChainInspectResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }
  return entityLinkChainInspectForMapResult(mapWindow->document().map(), params);
}

McpBridgeToolResult selectionSetResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return selectionSetForMapResult(mapWindow->document().map(), params);
}

McpBridgeToolResult selectionSetForMapResult(mdl::Map& map, const QJsonObject& params)
{
  const auto objectIdsValue = params.value("objectIds");
  if (!objectIdsValue.isArray())
  {
    return selectionPreconditionFailure(
      "selection_set requires objectIds array", "provide_object_ids_then_retry");
  }

  auto nodes = std::vector<mdl::Node*>{};
  for (const auto& objectIdValue : objectIdsValue.toArray())
  {
    if (!objectIdValue.isString())
    {
      return selectionPreconditionFailure(
        "objectIds must contain only strings", "fix_object_ids_then_retry");
    }
    const auto objectId = objectIdValue.toString();
    const auto path = McpObjectRegistry::parseLegacyObjectId(objectId);
    auto* node = path ? map.worldNode().resolvePath(*path) : nullptr;
    if (node == nullptr)
    {
      return selectionPreconditionFailure(
        QString{"Unknown MCP object id: %1"}.arg(objectId),
        "refresh_status_or_fix_object_ids",
        QJsonObject{{"objectId", objectId}});
    }
    if (!map.editorContext().selectable(*node))
    {
      return selectionPreconditionFailure(
        QString{"MCP object id is not selectable: %1"}.arg(objectId),
        "select_supported_object_ids_then_retry",
        QJsonObject{{"objectId", objectId}});
    }
    nodes.push_back(node);
  }

  automation::replaceSelection(map, nodes);

  auto selectedIds = QJsonArray{};
  for (const auto* node : nodes)
  {
    selectedIds.push_back(mcpNodePathId(*node, map.worldNode()));
  }
  return McpBridgeToolResult::success(QJsonObject{
    {"selectedObjectIds", selectedIds},
    {"selectedCount", selectedIds.size()},
    {"mutatedDocument", false},
  });
}

McpBridgeToolResult selectionFilterResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }
  return selectionFilterForMapResult(mapWindow->document().map(), params);
}

McpBridgeToolResult selectionFilterForMapResult(mdl::Map& map, const QJsonObject& params)
{
  auto error = QString{};
  auto options = McpSelectionQueryOptions{};
  options.excludeWorld = mcpOptionalBool(params, "excludeWorld", true);
  options.selectableOnly = mcpOptionalBool(params, "selectableOnly", false);
  options.leafOnly = mcpOptionalBool(params, "leafOnly", false);
  options.exactTypeOnly = mcpOptionalBool(params, "exactTypeOnly", true);
  options.removeDescendantMatches =
    mcpOptionalBool(params, "removeDescendantMatches", false);
  const auto matches = mcpFilteredNodes(map, params, options, error);
  if (!error.isEmpty())
  {
    return selectionPreconditionFailure(error, "fix_selection_query_then_retry");
  }

  if (mcpOptionalBool(params, "select", false))
  {
    auto selectableNodes = std::vector<mdl::Node*>{};
    for (auto* node : matches)
    {
      if (node != &map.worldNode() && map.editorContext().selectable(*node))
      {
        selectableNodes.push_back(node);
      }
    }
    mdl::deselectAll(map);
    if (!selectableNodes.empty())
    {
      mdl::selectNodes(map, selectableNodes);
    }
  }

  const auto detail = params.value("detail").toString("summary").toLower();
  auto results = QJsonArray{};
  auto objectIds = QJsonArray{};
  for (const auto* node : matches)
  {
    objectIds.push_back(mcpNodePathId(*node, map.worldNode()));
    if (detail == "full")
    {
      results.push_back(mcpNodeSummaryJson(*node, map.worldNode()));
    }
  }

  auto result = QJsonObject{
    {"objectIds", objectIds},
    {"count", objectIds.size()},
    {"detail", detail == "full" ? "full" : "summary"},
    {"mutatedDocument", false},
    {"filters",
     QJsonObject{
       {"excludeWorld", options.excludeWorld},
       {"selectableOnly", options.selectableOnly},
       {"leafOnly", options.leafOnly},
       {"exactTypeOnly", options.exactTypeOnly},
       {"removeDescendantMatches", options.removeDescendantMatches},
     }},
  };
  if (detail == "full")
  {
    result.insert("results", results);
  }
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult selectionByBoundsResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }
  return selectionByBoundsForMapResult(mapWindow->document().map(), params);
}

McpBridgeToolResult selectionByBoundsForMapResult(
  mdl::Map& map, const QJsonObject& params)
{
  auto paramsWithSelect = params;
  paramsWithSelect.insert("select", true);
  paramsWithSelect.insert("boundsMode", params.value("mode").toString("intersects"));
  if (!paramsWithSelect.contains("excludeWorld"))
  {
    paramsWithSelect.insert("excludeWorld", true);
  }
  if (!paramsWithSelect.contains("selectableOnly"))
  {
    paramsWithSelect.insert("selectableOnly", true);
  }
  if (!paramsWithSelect.contains("leafOnly"))
  {
    paramsWithSelect.insert("leafOnly", true);
  }
  return selectionFilterForMapResult(map, paramsWithSelect);
}

McpBridgeToolResult selectionGrowResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return selectionGrowForMapResult(mapWindow->document().map(), params);
}

McpBridgeToolResult selectionGrowForMapResult(mdl::Map& map, const QJsonObject& params)
{
  const auto selectedNodes = map.selection().nodes;
  if (selectedNodes.empty())
  {
    return McpBridgeToolResult::success(QJsonObject{
      {"selectedObjectIds", QJsonArray{}},
      {"selectedCount", 0},
      {"mutatedDocument", false},
    });
  }

  const auto mode = params.value("mode").toString("parents").trimmed().toLower();
  auto grown = std::vector<mdl::Node*>{};
  auto seen = std::set<mdl::Node*>{};
  const auto addNode = [&](mdl::Node* node) {
    if (node && node != &map.worldNode() && map.editorContext().selectable(*node))
    {
      if (seen.insert(node).second)
      {
        grown.push_back(node);
      }
    }
  };

  if (mode == "parents")
  {
    for (auto* node : selectedNodes)
    {
      addNode(node->parent());
    }
  }
  else if (mode == "children")
  {
    for (auto* node : selectedNodes)
    {
      for (auto* child : node->children())
      {
        addNode(child);
      }
    }
  }
  else if (mode == "siblings")
  {
    for (auto* node : selectedNodes)
    {
      if (auto* parent = node->parent())
      {
        for (auto* sibling : parent->children())
        {
          addNode(sibling);
        }
      }
    }
  }
  else
  {
    return selectionPreconditionFailure(
      "selection_grow mode must be parents, children, or siblings",
      "fix_selection_grow_mode_then_retry",
      QJsonObject{{"mode", mode}});
  }

  mdl::deselectAll(map);
  if (!grown.empty())
  {
    mdl::selectNodes(map, grown);
  }

  auto selectedIds = QJsonArray{};
  for (const auto* node : grown)
  {
    selectedIds.push_back(mcpNodePathId(*node, map.worldNode()));
  }
  return McpBridgeToolResult::success(QJsonObject{
    {"mode", mode},
    {"selectedObjectIds", selectedIds},
    {"selectedCount", selectedIds.size()},
    {"mutatedDocument", false},
  });
}

McpBridgeToolResult viewportFocusResult(AppController&, const QJsonObject&)
{
  return retiredViewportToolResult(
    "viewport_focus",
    "Use selection-aware geometry/review tools, or user selection followed by "
    "render_review_current_scene(scope:\"selection\").");
}

McpBridgeToolResult viewportClearMarksResult(
  AppController& appController, const QJsonObject& params, QJsonObject& overlayState)
{
  overlayState = QJsonObject{};
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (mcpOptionalBool(params, "clearSelection", false))
  {
    if (!mapWindow)
    {
      return noActiveDocumentFailure();
    }
    mdl::deselectAll(mapWindow->document().map());
  }
  return McpBridgeToolResult::success(QJsonObject{
    {"overlay", overlayState},
    {"active", false},
    {"selectionCleared", mcpOptionalBool(params, "clearSelection", false)},
  });
}

McpBridgeToolResult viewportLayoutGetResult(AppController&)
{
  return retiredViewportToolResult(
    "viewport_layout_get", "Use render_review_current_scene or render_review_selector.");
}

McpBridgeToolResult viewportLayoutSetResult(AppController&, const QJsonObject&)
{
  return retiredViewportToolResult(
    "viewport_layout_set", "Use render_review_current_scene or render_review_selector.");
}

McpBridgeToolResult viewportCameraFrameBoundsResult(AppController&, const QJsonObject&)
{
  return retiredViewportToolResult(
    "viewport_camera_frame_bounds",
    "Use render_review_selector/module_render_review view presets.");
}

McpBridgeToolResult viewportCameraSetResult(AppController&, const QJsonObject&)
{
  return retiredViewportToolResult(
    "viewport_camera_set",
    "Use render_review_selector/module_render_review view presets.");
}

McpBridgeToolResult viewportCaptureCurrentResult(AppController&, const QJsonObject&)
{
  return retiredViewportToolResult(
    "viewport_capture_current",
    "Use render_review_current_scene for scene review, or render_review_selector for "
    "targeted review.");
}

McpBridgeToolResult viewportCapture3DResult(AppController&, const QJsonObject&)
{
  return retiredViewportToolResult(
    "viewport_capture_3d", "Use render_review_selector or module_render_review.");
}

McpBridgeToolResult viewportCapture2DResult(AppController&, const QJsonObject&)
{
  return retiredViewportToolResult(
    "viewport_capture_2d", "Use render_review_selector or module_render_review.");
}

McpBridgeToolResult viewportCaptureSceneReviewResult(
  AppController&,
  const QJsonObject&,
  QJsonObject&,
  const std::vector<McpOperationRecord>&,
  const McpObjectRegistry*)
{
  return retiredViewportToolResult(
    "viewport_capture_scene_review",
    "Use render_review_current_scene, render_review_operation, render_review_selector, "
    "or module_render_review.");
}

McpBridgeToolResult renderReviewOperationResult(
  AppController& appController,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry* objectRegistry,
  const std::map<QString, McpBrushMetadataRecord>* metadataStore)
{
  auto reviewParams = params;
  if (!reviewParams.contains("style"))
  {
    reviewParams.insert("style", "whitebox_edges");
  }
  if (!reviewParams.contains("views"))
  {
    reviewParams.insert(
      "views",
      QJsonArray{
        "iso_overview_ne",
        "iso_overview_sw",
        "top_plan",
        "side_elevation_long",
        "front_elevation_cross",
      });
  }

  auto review = renderReviewTargetsResult(
    appController, reviewParams, history, objectRegistry, metadataStore);
  if (!review.ok)
  {
    return review;
  }
  review.result.insert("tool", "render_review_operation");
  return review;
}

} // namespace tb::ui
