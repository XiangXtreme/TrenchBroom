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
#include <QStringList>

#include "base/Color.h"
#include "base/Macros.h"
#include "McpBridgeServerTools.h"
#include "McpResponseUtils.h"
#include "McpToolSupport.h"
#include "mcp/McpError.h"
#include "mdl/AddRemoveNodesCommand.h"
#include "mdl/Brush.h"
#include "mdl/BrushNode.h"
#include "mdl/Entity.h"
#include "mdl/EntityDefinition.h"
#include "mdl/EntityDefinitionManager.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityNodeBase.h"
#include "mdl/EntityProperties.h"
#include "mdl/Map.h"
#include "mdl/Map_Entities.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/Node.h"
#include "mdl/NodeContents.h"
#include "mdl/PatchNode.h"
#include "mdl/PropertyDefinition.h"
#include "mdl/Selection.h"
#include "mdl/SwapNodeContentsCommand.h"
#include "mdl/Transaction.h"
#include "mdl/WorldNode.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/QPathUtils.h"
#include "ui/automation/AutomationNodes.h"
#include "ui/mcp/McpObjectRegistry.h"

#include "kd/vector_utils.h"

#include "vm/bbox.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <optional>
#include <ranges>
#include <type_traits>
#include <vector>

namespace tb::ui
{
namespace mcp = tb::mcp;

namespace
{

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

std::optional<mdl::NodePath> parseNodePathId(const QString& id)
{
  if (id == "node:world")
  {
    return mdl::NodePath{};
  }

  static const auto Prefix = QString{"node:"};
  if (!id.startsWith(Prefix))
  {
    return std::nullopt;
  }

  auto path = mdl::NodePath{};
  for (const auto& part : id.mid(Prefix.size()).split('/', Qt::SkipEmptyParts))
  {
    auto ok = false;
    const auto index = part.toULongLong(&ok);
    if (!ok)
    {
      return std::nullopt;
    }
    path.indices.push_back(static_cast<std::size_t>(index));
  }
  return path;
}

mdl::Node* resolveNodeId(mdl::WorldNode& worldNode, const QString& id)
{
  const auto path = parseNodePathId(id);
  if (!path)
  {
    return nullptr;
  }
  return worldNode.resolvePath(*path);
}

QString nodeTypeName(const mdl::Node& node)
{
  if (dynamic_cast<const mdl::WorldNode*>(&node) != nullptr)
  {
    return "world";
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

QJsonObject nodeSummaryJson(const mdl::Node& node, const mdl::WorldNode& worldNode)
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

int removeEmptyEntityProperties(mdl::Entity& entity)
{
  auto keysToRemove = std::vector<std::string>{};
  for (const auto& property : entity.properties())
  {
    if (
      property.value().empty() && property.key() != mdl::EntityPropertyKeys::Classname
      && property.key() != mdl::EntityPropertyKeys::Origin)
    {
      keysToRemove.push_back(property.key());
    }
  }
  for (const auto& key : keysToRemove)
  {
    entity.removeProperty(key);
  }
  return static_cast<int>(keysToRemove.size());
}

bool textMatches(const QString& text, const QString& query)
{
  return text.contains(query, Qt::CaseInsensitive);
}

QString makeOperationId(int& nextOperationIndex)
{
  return QString{"mcp-op-%1"}.arg(nextOperationIndex++);
}

QJsonObject mutationResultJson(
  const McpOperationRecord& operation, const QString& idsMode)
{
  auto result = QJsonObject{};
  result.insert("operationId", operation.operationId);
  result.insert("transactionName", operation.transactionName);
  result.insert("mutatedDocument", true);
  result.insert("activeDocumentPath", operation.documentPath);
  result.insert("documentFingerprint", operation.documentFingerprint);
  mcpApplyChangedObjectIdsMode(result, operation.changedObjectIdsJson(), idsMode);
  return result;
}

QJsonObject targetSourceDetails(const QString& targetSource)
{
  return QJsonObject{{"targetSource", targetSource}};
}

void mcpRecordOperation(
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  mdl::Map& map,
  const QString& toolName,
  const QString& transactionName,
  const QJsonArray& changedObjectIds,
  QJsonObject& result,
  const QString& idsMode = "count")
{
  auto operation = McpOperationRecord{};
  operation.operationId = makeOperationId(nextOperationIndex);
  operation.toolName = toolName;
  operation.transactionName = transactionName;
  operation.documentPath = map.path().empty() ? QString{} : pathAsQString(map.path());
  operation.documentFingerprint = documentFingerprintForMap(map);
  operation.setChangedObjectIds(changedObjectIds);
  result = mutationResultJson(operation, idsMode);
  appendMcpOperationRecord(history, std::move(operation));
}

std::optional<vm::vec3d> mcpVec3FromJson(
  const QJsonObject& params, const QString& key, QString& error)
{
  const auto value = params.value(key);
  if (!value.isArray())
  {
    error = QString{"%1 must be an array of three numbers"}.arg(key);
    return std::nullopt;
  }

  const auto array = value.toArray();
  if (array.size() != 3)
  {
    error = QString{"%1 must contain exactly three numbers"}.arg(key);
    return std::nullopt;
  }

  auto components = std::array<double, 3>{};
  for (auto i = 0; i < 3; ++i)
  {
    if (!array[i].isDouble())
    {
      error = QString{"%1[%2] must be a number"}.arg(key).arg(i);
      return std::nullopt;
    }
    components[static_cast<size_t>(i)] = array[i].toDouble();
  }

  return vm::vec3d{components[0], components[1], components[2]};
}

bool mcpOptionalBool(
  const QJsonObject& params, const QString& key, const bool defaultValue)
{
  const auto value = params.value(key);
  return value.isBool() ? value.toBool() : defaultValue;
}

size_t optionalSize(
  const QJsonObject& params, const QString& key, const size_t defaultValue)
{
  const auto value = params.value(key);
  if (!value.isDouble())
  {
    return defaultValue;
  }
  return static_cast<size_t>(std::max(0, value.toInt()));
}

std::optional<std::map<std::string, std::string>> stringMapFromJson(
  const QJsonObject& params, const QString& key, QString& error)
{
  const auto value = params.value(key);
  if (value.isUndefined())
  {
    return std::map<std::string, std::string>{};
  }
  if (!value.isObject())
  {
    error = QString{"%1 must be an object"}.arg(key);
    return std::nullopt;
  }

  auto result = std::map<std::string, std::string>{};
  const auto object = value.toObject();
  for (auto it = object.begin(); it != object.end(); ++it)
  {
    if (!it.value().isString())
    {
      error = QString{"%1.%2 must be a string"}.arg(key, it.key());
      return std::nullopt;
    }
    result.emplace(it.key().toStdString(), it.value().toString().toStdString());
  }
  return result;
}

std::optional<std::vector<std::string>> stringArrayFromJson(
  const QJsonObject& params, const QString& key, QString& error)
{
  const auto value = params.value(key);
  if (value.isUndefined())
  {
    return std::vector<std::string>{};
  }
  if (!value.isArray())
  {
    error = QString{"%1 must be an array"}.arg(key);
    return std::nullopt;
  }

  auto result = std::vector<std::string>{};
  for (const auto& item : value.toArray())
  {
    if (!item.isString())
    {
      error = QString{"%1 must contain only strings"}.arg(key);
      return std::nullopt;
    }
    result.push_back(item.toString().toStdString());
  }
  return result;
}

QStringList operationIdsFromParams(const QJsonObject& params, QString& error)
{
  auto result = QStringList{};
  const auto operationId = params.value("operationId").toString().trimmed();
  if (!operationId.isEmpty())
  {
    result.push_back(operationId);
  }
  const auto operationIds = params.value("operationIds");
  if (!operationIds.isUndefined())
  {
    if (!operationIds.isArray())
    {
      error = "operationIds must be an array";
      return {};
    }
    for (const auto& value : operationIds.toArray())
    {
      if (!value.isString())
      {
        error = "operationIds must contain only strings";
        return {};
      }
      const auto id = value.toString().trimmed();
      if (!id.isEmpty())
      {
        result.push_back(id);
      }
    }
  }
  result.removeDuplicates();
  return result;
}

const McpOperationRecord* findOperation(
  const std::vector<McpOperationRecord>& history, const QString& operationId)
{
  const auto it = std::ranges::find_if(
    history, [&](const auto& operation) { return operation.operationId == operationId; });
  return it == history.end() ? nullptr : &*it;
}

mdl::Node* resolveObjectId(
  mdl::Map& map,
  const QString& objectId,
  const McpObjectRegistry& objectRegistry,
  QJsonArray& warnings)
{
  const auto resolved = objectRegistry.resolveExternalId(map, objectId);
  if (!resolved.ok)
  {
    if (auto* legacyNode = resolveNodeId(map.worldNode(), objectId))
    {
      return legacyNode;
    }
    warnings.push_back(resolved.diagnostic);
    return nullptr;
  }
  return resolveNodeId(map.worldNode(), resolved.legacyPathId);
}

std::vector<mdl::EntityNodeBase*> entityTargetsFromParams(
  mdl::Map& map,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& operationHistory,
  const McpObjectRegistry& objectRegistry,
  QJsonArray& warnings,
  QString& error)
{
  auto nodes = std::vector<mdl::EntityNodeBase*>{};
  if (params.value("selector").isObject())
  {
    error =
      "selector targets are not implemented for entity property tools yet; use objectIds "
      "or operationIds";
    return {};
  }

  if (const auto objectIds = params.value("objectIds"); objectIds.isArray())
  {
    for (const auto& value : objectIds.toArray())
    {
      if (!value.isString())
      {
        error = "objectIds must contain only strings";
        return {};
      }
      if (auto* node = resolveObjectId(map, value.toString(), objectRegistry, warnings))
      {
        if (auto* entityNode = dynamic_cast<mdl::EntityNodeBase*>(node))
        {
          nodes.push_back(entityNode);
        }
      }
    }
  }

  for (const auto& operationId : operationIdsFromParams(params, error))
  {
    if (!error.isEmpty())
    {
      return {};
    }
    const auto* operation = findOperation(operationHistory, operationId);
    if (operation == nullptr)
    {
      warnings.push_back(QString{"Unknown MCP operation id: %1"}.arg(operationId));
      continue;
    }
    for (const auto& objectId : operation->changedObjectIds)
    {
      if (auto* node = resolveObjectId(map, objectId, objectRegistry, warnings))
      {
        if (auto* entityNode = dynamic_cast<mdl::EntityNodeBase*>(node))
        {
          nodes.push_back(entityNode);
        }
      }
    }
  }

  nodes.erase(std::remove(nodes.begin(), nodes.end(), nullptr), nodes.end());
  nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
  if (nodes.empty())
  {
    error = "No live entity targets matched objectIds or operationIds";
  }
  return nodes;
}

std::optional<QJsonArray> addNodesWithTransaction(
  mdl::Map& map,
  const QString& transactionName,
  const std::vector<mdl::Node*>& nodes,
  const bool selectCreated)
{
  auto addedNodes = std::vector<mdl::Node*>{};
  auto ok = executeTransaction(map, transactionName, [&]() {
    if (!automation::addNodes(map, nodes, selectCreated))
    {
      return false;
    }

    addedNodes = nodes;
    return true;
  });

  if (!ok)
  {
    return std::nullopt;
  }

  auto result = QJsonArray{};
  for (const auto* node : addedNodes)
  {
    result.push_back(nodePathId(*node, map.worldNode()));
  }
  return result;
}

std::optional<QJsonArray> updateNodeContentsWithTransaction(
  mdl::Map& map,
  const QString& transactionName,
  std::vector<std::pair<mdl::Node*, mdl::NodeContents>> nodesToSwap)
{
  auto changedNodes = QJsonArray{};
  for (const auto& [node, contents] : nodesToSwap)
  {
    unused(contents);
    changedNodes.push_back(nodePathId(*node, map.worldNode()));
  }

  const auto ok = executeTransaction(map, transactionName, [&]() {
    return map.executeAndStore(std::make_unique<mdl::SwapNodeContentsCommand>(
      transactionName.toStdString(), std::move(nodesToSwap)));
  });

  return ok ? std::optional{changedNodes} : std::nullopt;
}

std::optional<QJsonArray> removeNodeWithTransaction(
  mdl::Map& map, const QString& transactionName, mdl::Node& node)
{
  if (&node == &map.worldNode())
  {
    return std::nullopt;
  }
  auto* parent = node.parent();
  if (!parent || !parent->canRemoveChild(node))
  {
    return std::nullopt;
  }

  const auto removedId = nodePathId(node, map.worldNode());
  const auto ok = executeTransaction(map, transactionName, [&]() {
    mdl::deselectNodes(map, {&node});
    return map.executeAndStore(mdl::AddRemoveNodesCommand::remove({{parent, {&node}}}));
  });

  return ok ? std::optional{QJsonArray{removedId}} : std::nullopt;
}

QString entityDefinitionTypeName(const mdl::EntityDefinition& definition)
{
  return mdl::getType(definition) == mdl::EntityDefinitionType::Point ? "point" : "brush";
}

QString propertyValueTypeName(const mdl::PropertyValueType& type)
{
  return std::visit(
    [](const auto& valueType) -> QString {
      using T = std::decay_t<decltype(valueType)>;
      if constexpr (std::is_same_v<T, mdl::PropertyValueTypes::LinkTarget>)
      {
        return "target";
      }
      else if constexpr (std::is_same_v<T, mdl::PropertyValueTypes::LinkSource>)
      {
        return "target_source";
      }
      else if constexpr (std::is_same_v<T, mdl::PropertyValueTypes::String>)
      {
        return "string";
      }
      else if constexpr (std::is_same_v<T, mdl::PropertyValueTypes::Boolean>)
      {
        return "boolean";
      }
      else if constexpr (std::is_same_v<T, mdl::PropertyValueTypes::Integer>)
      {
        return "integer";
      }
      else if constexpr (std::is_same_v<T, mdl::PropertyValueTypes::Float>)
      {
        return "float";
      }
      else if constexpr (std::is_same_v<T, mdl::PropertyValueTypes::Choice>)
      {
        return "choice";
      }
      else if constexpr (std::is_same_v<T, mdl::PropertyValueTypes::Flags>)
      {
        return "flags";
      }
      else if constexpr (std::is_same_v<T, mdl::PropertyValueTypes::Origin>)
      {
        return "origin";
      }
      else if constexpr (std::is_same_v<T, mdl::PropertyValueTypes::Input>)
      {
        return "input";
      }
      else if constexpr (std::is_same_v<T, mdl::PropertyValueTypes::Output>)
      {
        return "output";
      }
      else if constexpr (
        std::is_same_v<T, mdl::PropertyValueTypes::Color<RgbF>>
        || std::is_same_v<T, mdl::PropertyValueTypes::Color<RgbB>>
        || std::is_same_v<T, mdl::PropertyValueTypes::Color<Rgb>>)
      {
        return "color";
      }
      else
      {
        return "unknown";
      }
    },
    type);
}

QJsonObject propertyDefinitionJson(const mdl::PropertyDefinition& property)
{
  auto result = QJsonObject{
    {"key", QString::fromStdString(property.key)},
    {"type", propertyValueTypeName(property.valueType)},
    {"shortDescription", QString::fromStdString(property.shortDescription)},
    {"longDescription", QString::fromStdString(property.longDescription)},
    {"readOnly", property.readOnly},
  };

  if (const auto defaultValue = mdl::PropertyDefinition::defaultValue(property))
  {
    result.insert("defaultValue", QString::fromStdString(*defaultValue));
  }

  return result;
}

QJsonObject entityDefinitionJson(const mdl::EntityDefinition& definition)
{
  auto properties = QJsonArray{};
  for (const auto& property : definition.propertyDefinitions)
  {
    properties.push_back(propertyDefinitionJson(property));
  }

  auto result = QJsonObject{
    {"classname", QString::fromStdString(definition.name)},
    {"type", entityDefinitionTypeName(definition)},
    {"description", QString::fromStdString(definition.description)},
    {"propertyCount", properties.size()},
    {"properties", properties},
  };

  if (const auto* pointDefinition = mdl::getPointEntityDefinition(&definition))
  {
    result.insert("bounds", boundsToJson(pointDefinition->bounds));
  }

  return result;
}

std::optional<std::vector<mdl::BrushNode*>> brushNodesFromParamsOrSelection(
  mdl::Map& map, const QJsonObject& params, QString& error)
{
  auto brushes = std::vector<mdl::BrushNode*>{};
  const auto objectIdsValue = params.value("objectIds");
  if (objectIdsValue.isUndefined())
  {
    brushes = map.selection().brushes;
  }
  else
  {
    if (!objectIdsValue.isArray())
    {
      error = "objectIds must be an array";
      return std::nullopt;
    }
    for (const auto& objectIdValue : objectIdsValue.toArray())
    {
      if (!objectIdValue.isString())
      {
        error = "objectIds must contain only strings";
        return std::nullopt;
      }
      auto* node = resolveNodeId(map.worldNode(), objectIdValue.toString());
      auto* brushNode = dynamic_cast<mdl::BrushNode*>(node);
      if (!brushNode)
      {
        error = QString{"Object is not a brush: %1"}.arg(objectIdValue.toString());
        return std::nullopt;
      }
      brushes.push_back(brushNode);
    }
  }

  brushes = kdl::vec_sort_and_remove_duplicates(std::move(brushes));
  if (brushes.empty())
  {
    error = "No brushes selected or specified";
    return std::nullopt;
  }
  return brushes;
}

} // namespace

McpBridgeToolResult createEntityForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  const auto classname = params.value("classname").toString().trimmed();
  if (classname.isEmpty())
  {
    return preMutationInvalidParamsFailure(
      "entity_create requires classname", "provide_entity_classname_then_retry");
  }

  auto error = QString{};
  const auto properties = stringMapFromJson(params, "properties", error);
  if (!properties)
  {
    return preMutationInvalidParamsFailure(
      error, "fix_entity_properties_then_retry", targetSourceDetails("properties"));
  }

  auto entity =
    mdl::Entity{{{mdl::EntityPropertyKeys::Classname, classname.toStdString()}}};
  if (const auto origin = params.value("origin"); !origin.isUndefined())
  {
    const auto originVec = mcpVec3FromJson(params, "origin", error);
    if (!originVec)
    {
      return preMutationInvalidParamsFailure(
        error, "fix_entity_origin_then_retry", targetSourceDetails("origin"));
    }
    entity.setOrigin(*originVec);
  }

  for (const auto& [key, value] : *properties)
  {
    if (key != mdl::EntityPropertyKeys::Classname)
    {
      entity.addOrUpdateProperty(key, value);
    }
  }

  auto* entityNode = new mdl::EntityNode{std::move(entity)};
  const auto transactionName = QString{"MCP: Create %1"}.arg(classname);
  const auto changedObjectIds = addNodesWithTransaction(
    map, transactionName, {entityNode}, mcpOptionalBool(params, "select", true));
  if (!changedObjectIds)
  {
    delete entityNode;
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not create entity");
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history,
    nextOperationIndex,
    map,
    toolName,
    transactionName,
    *changedObjectIds,
    result,
    mcpIdsModeFromParams(params));
  result.insert("entity", nodeSummaryJson(*entityNode, map.worldNode()));
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult createEntityResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return createEntityForMapResult(
    mapWindow->document().map(), toolName, params, history, nextOperationIndex);
}

McpBridgeToolResult updateEntityForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  const auto objectId = params.value("objectId").toString().trimmed();
  if (objectId.isEmpty())
  {
    return preMutationInvalidParamsFailure(
      "entity_update requires objectId", "provide_entity_object_id_then_retry");
  }

  auto* node = resolveNodeId(map.worldNode(), objectId);
  auto* entityNode = dynamic_cast<mdl::EntityNodeBase*>(node);
  if (!entityNode)
  {
    return preMutationInvalidParamsFailure(
      QString{"Object is not an entity: %1"}.arg(objectId),
      "refresh_status_or_select_entity_then_retry",
      QJsonObject{{"objectId", objectId}, {"targetSource", "objectId"}});
  }

  auto error = QString{};
  const auto properties = stringMapFromJson(params, "properties", error);
  if (!properties)
  {
    return preMutationInvalidParamsFailure(
      error, "fix_entity_properties_then_retry", targetSourceDetails("properties"));
  }
  const auto removeKeys = stringArrayFromJson(params, "removeKeys", error);
  if (!removeKeys)
  {
    return preMutationInvalidParamsFailure(
      error, "fix_remove_keys_then_retry", targetSourceDetails("removeKeys"));
  }

  auto entity = entityNode->entity();
  for (const auto& [key, value] : *properties)
  {
    entity.addOrUpdateProperty(key, value);
  }
  for (const auto& key : *removeKeys)
  {
    if (key != mdl::EntityPropertyKeys::Classname)
    {
      entity.removeProperty(key);
    }
  }

  const auto transactionName = QString{"MCP: Update entity"};
  auto nodesToSwap = std::vector<std::pair<mdl::Node*, mdl::NodeContents>>{};
  nodesToSwap.emplace_back(entityNode, mdl::NodeContents{std::move(entity)});
  const auto changedObjectIds =
    updateNodeContentsWithTransaction(map, transactionName, std::move(nodesToSwap));
  if (!changedObjectIds)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not update entity");
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history,
    nextOperationIndex,
    map,
    toolName,
    transactionName,
    *changedObjectIds,
    result,
    mcpIdsModeFromParams(params));
  result.insert("entity", nodeSummaryJson(*entityNode, map.worldNode()));
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult updateEntityResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return updateEntityForMapResult(
    mapWindow->document().map(), toolName, params, history, nextOperationIndex);
}

McpBridgeToolResult deleteEntityForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  const auto objectId = params.value("objectId").toString().trimmed();
  if (objectId.isEmpty())
  {
    return preMutationInvalidParamsFailure(
      "entity_delete requires objectId", "provide_entity_object_id_then_retry");
  }

  auto* node = resolveNodeId(map.worldNode(), objectId);
  if (!node)
  {
    return preMutationInvalidParamsFailure(
      QString{"Unknown MCP object id: %1"}.arg(objectId),
      "refresh_status_or_fix_object_id_then_retry",
      QJsonObject{{"objectId", objectId}, {"targetSource", "objectId"}});
  }

  const auto transactionName = QString{"MCP: Delete object"};
  const auto changedObjectIds = removeNodeWithTransaction(map, transactionName, *node);
  if (!changedObjectIds)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::Forbidden,
      QString{"Object cannot be deleted: %1"}.arg(objectId),
      preMutationFailureDetails(
        QJsonObject{{"objectId", objectId}, {"targetSource", "objectId"}},
        "choose_deletable_object_then_retry"));
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history,
    nextOperationIndex,
    map,
    toolName,
    transactionName,
    *changedObjectIds,
    result,
    mcpIdsModeFromParams(params));
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult deleteEntityResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return deleteEntityForMapResult(
    mapWindow->document().map(), toolName, params, history, nextOperationIndex);
}

McpBridgeToolResult entityPropertiesUpdateResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& operationHistory,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return entityPropertiesUpdateForMapResult(
    mapWindow->document().map(),
    toolName,
    params,
    operationHistory,
    history,
    nextOperationIndex,
    objectRegistry);
}

McpBridgeToolResult entityPropertiesUpdateForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& operationHistory,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry)
{
  auto error = QString{};
  const auto properties = stringMapFromJson(params, "properties", error);
  if (!properties)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      error,
      preMutationFailureDetails(
        QJsonObject{{"targetSource", "entityProperties"}}, "fix_properties_then_retry"));
  }
  if (properties->empty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      "entity_properties_update requires non-empty properties",
      preMutationFailureDetails(
        QJsonObject{{"targetSource", "entityProperties"}}, "add_properties_then_retry"));
  }

  auto warnings = QJsonArray{};
  const auto entityNodes = entityTargetsFromParams(
    map, params, operationHistory, objectRegistry, warnings, error);
  if (!error.isEmpty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      error,
      preMutationFailureDetails(
        QJsonObject{{"targetSource", "entityTargets"}},
        "refresh_status_or_fix_entity_targets"));
  }

  auto nodesToSwap = std::vector<std::pair<mdl::Node*, mdl::NodeContents>>{};
  for (auto* entityNode : entityNodes)
  {
    auto entity = entityNode->entity();
    for (const auto& [key, value] : *properties)
    {
      if (key != mdl::EntityPropertyKeys::Classname)
      {
        entity.addOrUpdateProperty(key, value);
      }
    }
    removeEmptyEntityProperties(entity);
    nodesToSwap.emplace_back(entityNode, mdl::NodeContents{std::move(entity)});
  }

  const auto transactionName =
    params.value("transactionName").toString("MCP: Update entity properties");
  const auto changedObjectIds =
    updateNodeContentsWithTransaction(map, transactionName, std::move(nodesToSwap));
  if (!changedObjectIds)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError,
      "Could not update entity properties",
      preMutationFailureDetails(
        QJsonObject{{"targetSource", "entityTargets"}}, "refresh_status_or_retry"));
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history,
    nextOperationIndex,
    map,
    toolName,
    transactionName,
    *changedObjectIds,
    result,
    mcpIdsModeFromParams(params));
  result.insert("entityCount", static_cast<int>(entityNodes.size()));
  auto resultWarnings = result.value("warnings").toArray();
  for (const auto& warning : warnings)
  {
    resultWarnings.push_back(warning);
  }
  result.insert("warnings", resultWarnings);
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult entityPropertiesDeleteResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& operationHistory,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return entityPropertiesDeleteForMapResult(
    mapWindow->document().map(),
    toolName,
    params,
    operationHistory,
    history,
    nextOperationIndex,
    objectRegistry);
}

McpBridgeToolResult entityPropertiesDeleteForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& operationHistory,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry)
{
  auto error = QString{};
  auto removeKeys = stringArrayFromJson(params, "keys", error);
  if (!removeKeys)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      error,
      preMutationFailureDetails(
        QJsonObject{{"targetSource", "entityProperties"}}, "fix_keys_then_retry"));
  }
  if (removeKeys->empty())
  {
    removeKeys = stringArrayFromJson(params, "removeKeys", error);
  }
  if (!removeKeys || removeKeys->empty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      "entity_properties_delete requires keys",
      preMutationFailureDetails(
        QJsonObject{{"targetSource", "entityProperties"}}, "add_keys_then_retry"));
  }

  auto warnings = QJsonArray{};
  const auto entityNodes = entityTargetsFromParams(
    map, params, operationHistory, objectRegistry, warnings, error);
  if (!error.isEmpty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      error,
      preMutationFailureDetails(
        QJsonObject{{"targetSource", "entityTargets"}},
        "refresh_status_or_fix_entity_targets"));
  }

  auto nodesToSwap = std::vector<std::pair<mdl::Node*, mdl::NodeContents>>{};
  for (auto* entityNode : entityNodes)
  {
    auto entity = entityNode->entity();
    for (const auto& key : *removeKeys)
    {
      if (
        key != mdl::EntityPropertyKeys::Classname
        && key != mdl::EntityPropertyKeys::Origin)
      {
        entity.removeProperty(key);
      }
    }
    nodesToSwap.emplace_back(entityNode, mdl::NodeContents{std::move(entity)});
  }

  const auto transactionName =
    params.value("transactionName").toString("MCP: Delete entity properties");
  const auto changedObjectIds =
    updateNodeContentsWithTransaction(map, transactionName, std::move(nodesToSwap));
  if (!changedObjectIds)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError,
      "Could not delete entity properties",
      preMutationFailureDetails(
        QJsonObject{{"targetSource", "entityTargets"}}, "refresh_status_or_retry"));
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history,
    nextOperationIndex,
    map,
    toolName,
    transactionName,
    *changedObjectIds,
    result,
    mcpIdsModeFromParams(params));
  result.insert("entityCount", static_cast<int>(entityNodes.size()));
  auto resultWarnings = result.value("warnings").toArray();
  for (const auto& warning : warnings)
  {
    resultWarnings.push_back(warning);
  }
  result.insert("warnings", resultWarnings);
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult fgdEntitiesListResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  const auto type = params.value("type").toString().trimmed().toLower();
  const auto query = params.value("query").toString().trimmed();
  const auto limit = optionalSize(params, "limit", 200);

  auto definitions = QJsonArray{};
  for (const auto& definition :
       mapWindow->document().map().entityDefinitionManager().definitions())
  {
    if (!type.isEmpty() && entityDefinitionTypeName(definition) != type)
    {
      continue;
    }
    if (
      !query.isEmpty() && !textMatches(QString::fromStdString(definition.name), query)
      && !textMatches(QString::fromStdString(definition.description), query))
    {
      continue;
    }

    definitions.push_back(QJsonObject{
      {"classname", QString::fromStdString(definition.name)},
      {"type", entityDefinitionTypeName(definition)},
      {"description", QString::fromStdString(definition.description)},
      {"propertyCount", static_cast<int>(definition.propertyDefinitions.size())},
    });

    if (definitions.size() >= static_cast<int>(limit))
    {
      break;
    }
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"definitions", definitions},
    {"count", definitions.size()},
  });
}

McpBridgeToolResult entitySchemaResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  const auto classname = params.value("classname").toString().trimmed();
  if (classname.isEmpty())
  {
    return invalidParamsFailure("entity_schema requires classname");
  }

  const auto* definition =
    mapWindow->document().map().entityDefinitionManager().definition(
      classname.toStdString());
  if (!definition)
  {
    return invalidParamsFailure(QString{"Unknown entity classname: %1"}.arg(classname));
  }

  return McpBridgeToolResult::success(entityDefinitionJson(*definition));
}

McpBridgeToolResult createEntityFromSchemaForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  const auto classname = params.value("classname").toString().trimmed();
  if (classname.isEmpty())
  {
    return preMutationInvalidParamsFailure(
      "entity_create_from_schema requires classname",
      "provide_entity_classname_then_retry");
  }

  const auto* definition =
    map.entityDefinitionManager().definition(classname.toStdString());
  if (!definition || mdl::getType(*definition) != mdl::EntityDefinitionType::Point)
  {
    return preMutationInvalidParamsFailure(
      QString{"Unknown point entity classname: %1"}.arg(classname),
      "choose_defined_point_entity_classname_then_retry",
      QJsonObject{{"classname", classname}});
  }

  auto error = QString{};
  const auto properties = stringMapFromJson(params, "properties", error);
  if (!properties)
  {
    return preMutationInvalidParamsFailure(
      error, "fix_entity_properties_then_retry", targetSourceDetails("properties"));
  }

  auto origin = vm::vec3d{0, 0, 0};
  if (const auto originValue = params.value("origin"); !originValue.isUndefined())
  {
    const auto parsedOrigin = mcpVec3FromJson(params, "origin", error);
    if (!parsedOrigin)
    {
      return preMutationInvalidParamsFailure(
        error, "fix_entity_origin_then_retry", targetSourceDetails("origin"));
    }
    origin = *parsedOrigin;
  }

  auto entity =
    mdl::Entity{{{mdl::EntityPropertyKeys::Classname, classname.toStdString()}}};
  mdl::setDefaultProperties(*definition, entity, mdl::SetDefaultPropertyMode::SetAll);
  entity.setOrigin(origin);
  for (const auto& [key, value] : *properties)
  {
    entity.addOrUpdateProperty(key, value);
  }

  auto* entityNode = new mdl::EntityNode{std::move(entity)};
  const auto transactionName = QString{"MCP: Create %1"}.arg(classname);
  const auto changedObjectIds = addNodesWithTransaction(
    map, transactionName, {entityNode}, mcpOptionalBool(params, "select", true));
  if (!changedObjectIds)
  {
    delete entityNode;
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError,
      QString{"Failed to create entity from schema: %1"}.arg(classname));
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history,
    nextOperationIndex,
    map,
    toolName,
    transactionName,
    *changedObjectIds,
    result,
    mcpIdsModeFromParams(params));
  result.insert("classname", classname);
  result.insert("entity", nodeSummaryJson(*entityNode, map.worldNode()));
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult createEntityFromSchemaResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return createEntityFromSchemaForMapResult(
    mapWindow->document().map(), toolName, params, history, nextOperationIndex);
}

McpBridgeToolResult createEntityCheckedForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  const auto classname = params.value("classname").toString().trimmed();
  if (classname.isEmpty())
  {
    return preMutationInvalidParamsFailure(
      "entity_create_checked requires classname", "provide_entity_classname_then_retry");
  }

  const auto* definition =
    map.entityDefinitionManager().definition(classname.toStdString());
  if (!definition)
  {
    return preMutationInvalidParamsFailure(
      QString{"FGD does not define entity classname: %1"}.arg(classname),
      "choose_defined_point_entity_classname_then_retry",
      QJsonObject{{"classname", classname}});
  }
  if (mdl::getType(*definition) != mdl::EntityDefinitionType::Point)
  {
    return preMutationInvalidParamsFailure(
      QString{"entity_create_checked only creates point entities; %1 is %2"}
        .arg(classname)
        .arg(entityDefinitionTypeName(*definition)),
      "choose_point_entity_classname_then_retry",
      QJsonObject{{"classname", classname}});
  }

  auto result = createEntityFromSchemaForMapResult(
    map, toolName, params, history, nextOperationIndex);
  if (result.ok)
  {
    result.result.insert("checked", true);
    result.result.insert("schemaClassname", classname);
  }
  return result;
}

McpBridgeToolResult createEntityCheckedResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return createEntityCheckedForMapResult(
    mapWindow->document().map(), toolName, params, history, nextOperationIndex);
}

McpBridgeToolResult createEntityCheckedBatchResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return createEntityCheckedBatchForMapResult(
    mapWindow->document().map(), toolName, params, history, nextOperationIndex);
}

McpBridgeToolResult createEntityCheckedBatchForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  const auto entitiesValue = params.value("entities");
  if (!entitiesValue.isArray())
  {
    return preMutationInvalidParamsFailure(
      "entity_create_checked_batch requires entities array",
      "provide_entities_then_retry");
  }

  const auto entitiesArray = entitiesValue.toArray();
  if (entitiesArray.isEmpty())
  {
    return preMutationInvalidParamsFailure(
      "entities must not be empty", "provide_entities_then_retry");
  }

  auto nodes = std::vector<mdl::Node*>{};
  auto createdClassnames = QJsonArray{};
  auto removedEmptyPropertyCount = 0;

  const auto cleanupNodes = [&] {
    for (auto* node : nodes)
    {
      delete node;
    }
    nodes.clear();
  };

  for (auto i = 0; i < entitiesArray.size(); ++i)
  {
    const auto entityValue = entitiesArray[i];
    if (!entityValue.isObject())
    {
      cleanupNodes();
      return preMutationInvalidParamsFailure(
        QString{"entities[%1] must be an object"}.arg(i),
        "fix_entity_payload_then_retry",
        QJsonObject{{"entityIndex", i}});
    }

    const auto entityParams = entityValue.toObject();
    const auto classname = entityParams.value("classname").toString().trimmed();
    if (classname.isEmpty())
    {
      cleanupNodes();
      return preMutationInvalidParamsFailure(
        QString{"entities[%1] requires classname"}.arg(i),
        "provide_entity_classname_then_retry",
        QJsonObject{{"entityIndex", i}});
    }

    const auto* definition =
      map.entityDefinitionManager().definition(classname.toStdString());
    if (!definition)
    {
      cleanupNodes();
      return preMutationInvalidParamsFailure(
        QString{"FGD does not define entity classname: %1"}.arg(classname),
        "choose_defined_point_entity_classname_then_retry",
        QJsonObject{{"entityIndex", i}, {"classname", classname}});
    }
    if (mdl::getType(*definition) != mdl::EntityDefinitionType::Point)
    {
      cleanupNodes();
      return preMutationInvalidParamsFailure(
        QString{"entity_create_checked_batch only creates point entities; %1 is %2"}
          .arg(classname)
          .arg(entityDefinitionTypeName(*definition)),
        "choose_point_entity_classname_then_retry",
        QJsonObject{{"entityIndex", i}, {"classname", classname}});
    }

    auto error = QString{};
    const auto properties = stringMapFromJson(entityParams, "properties", error);
    if (!properties)
    {
      cleanupNodes();
      return preMutationInvalidParamsFailure(
        QString{"entities[%1].%2"}.arg(i).arg(error),
        "fix_entity_properties_then_retry",
        QJsonObject{{"entityIndex", i}});
    }

    auto origin = vm::vec3d{0, 0, 0};
    if (const auto originValue = entityParams.value("origin"); !originValue.isUndefined())
    {
      const auto parsedOrigin = mcpVec3FromJson(entityParams, "origin", error);
      if (!parsedOrigin)
      {
        cleanupNodes();
        return preMutationInvalidParamsFailure(
          QString{"entities[%1].%2"}.arg(i).arg(error),
          "fix_entity_origin_then_retry",
          QJsonObject{{"entityIndex", i}, {"classname", classname}});
      }
      origin = *parsedOrigin;
    }

    auto entity =
      mdl::Entity{{{mdl::EntityPropertyKeys::Classname, classname.toStdString()}}};
    mdl::setDefaultProperties(*definition, entity, mdl::SetDefaultPropertyMode::SetAll);
    entity.setOrigin(origin);
    for (const auto& [key, value] : *properties)
    {
      if (!value.empty())
      {
        entity.addOrUpdateProperty(key, value);
      }
    }
    removedEmptyPropertyCount += removeEmptyEntityProperties(entity);

    nodes.push_back(new mdl::EntityNode{std::move(entity)});
    createdClassnames.push_back(classname);
  }

  const auto transactionName =
    params.value("transactionName").toString("MCP: Create checked entities").trimmed();
  const auto finalTransactionName =
    transactionName.isEmpty() ? QString{"MCP: Create checked entities"} : transactionName;
  const auto changedObjectIds = addNodesWithTransaction(
    map, finalTransactionName, nodes, mcpOptionalBool(params, "select", true));
  if (!changedObjectIds)
  {
    cleanupNodes();
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not create checked entity batch");
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history,
    nextOperationIndex,
    map,
    toolName,
    finalTransactionName,
    *changedObjectIds,
    result,
    mcpIdsModeFromParams(params));
  result.insert("checked", true);
  result.insert("entityCount", static_cast<int>(nodes.size()));
  result.insert("classNames", createdClassnames);
  result.insert("removedEmptyPropertyCount", removedEmptyPropertyCount);

  const auto detail = params.value("detail").toString("summary").toLower();
  if (detail == "full")
  {
    auto entitySummaries = QJsonArray{};
    for (const auto* node : nodes)
    {
      entitySummaries.push_back(nodeSummaryJson(*node, map.worldNode()));
    }
    result.insert("entities", entitySummaries);
  }

  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult tieBrushesForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  const auto classname = params.value("classname").toString().trimmed();
  if (classname.isEmpty())
  {
    return preMutationInvalidParamsFailure(
      "entity_tie_brushes requires classname",
      "provide_brush_entity_classname_then_retry");
  }

  const auto* definition =
    map.entityDefinitionManager().definition(classname.toStdString());
  if (!definition || mdl::getType(*definition) != mdl::EntityDefinitionType::Brush)
  {
    return preMutationInvalidParamsFailure(
      QString{"Unknown brush entity classname: %1"}.arg(classname),
      "choose_defined_brush_entity_classname_then_retry",
      QJsonObject{{"classname", classname}});
  }

  auto error = QString{};
  const auto brushes = brushNodesFromParamsOrSelection(map, params, error);
  if (!brushes)
  {
    return preMutationInvalidParamsFailure(
      error,
      "select_brushes_or_fix_object_ids_then_retry",
      targetSourceDetails("brushes"));
  }

  mdl::deselectAll(map);
  mdl::selectNodes(map, kdl::vec_static_cast<mdl::Node*>(*brushes));
  const auto* entityNode = mdl::createBrushEntity(map, *definition);
  if (!entityNode)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Failed to tie brushes to entity");
  }

  auto changedObjectIds = QJsonArray{nodePathId(*entityNode, map.worldNode())};
  for (const auto* brush : *brushes)
  {
    changedObjectIds.push_back(nodePathId(*brush, map.worldNode()));
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history,
    nextOperationIndex,
    map,
    toolName,
    QString{"MCP: Tie brushes to %1"}.arg(classname),
    changedObjectIds,
    result,
    mcpIdsModeFromParams(params));
  result.insert("classname", classname);
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult tieBrushesResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return tieBrushesForMapResult(
    mapWindow->document().map(), toolName, params, history, nextOperationIndex);
}

McpBridgeToolResult untieBrushesForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  auto brushes = std::vector<mdl::BrushNode*>{};
  const auto objectIdsValue = params.value("objectIds");
  if (objectIdsValue.isUndefined())
  {
    brushes = map.selection().brushes;
  }
  else if (objectIdsValue.isArray())
  {
    for (const auto& objectIdValue : objectIdsValue.toArray())
    {
      if (!objectIdValue.isString())
      {
        return preMutationInvalidParamsFailure(
          "objectIds must contain only strings",
          "fix_object_ids_then_retry",
          targetSourceDetails("objectIds"));
      }
      auto* node = resolveNodeId(map.worldNode(), objectIdValue.toString());
      if (auto* brushNode = dynamic_cast<mdl::BrushNode*>(node))
      {
        brushes.push_back(brushNode);
      }
      else if (auto* entityNode = dynamic_cast<mdl::EntityNode*>(node))
      {
        for (auto* child : entityNode->children())
        {
          if (auto* childBrush = dynamic_cast<mdl::BrushNode*>(child))
          {
            brushes.push_back(childBrush);
          }
        }
      }
      else
      {
        return preMutationInvalidParamsFailure(
          QString{"Object is not a brush or brush entity: %1"}.arg(
            objectIdValue.toString()),
          "select_brush_entity_brushes_or_fix_object_ids_then_retry",
          QJsonObject{
            {"objectId", objectIdValue.toString()}, {"targetSource", "objectIds"}});
      }
    }
  }
  else
  {
    return preMutationInvalidParamsFailure(
      "objectIds must be an array",
      "fix_object_ids_then_retry",
      targetSourceDetails("objectIds"));
  }

  brushes.erase(
    std::remove_if(
      brushes.begin(),
      brushes.end(),
      [&](const auto* brush) { return brush->entity() == &map.worldNode(); }),
    brushes.end());
  brushes = kdl::vec_sort_and_remove_duplicates(std::move(brushes));
  if (brushes.empty())
  {
    return preMutationInvalidParamsFailure(
      "No brush entity brushes selected or specified",
      "select_brush_entity_brushes_then_retry",
      targetSourceDetails("brushes"));
  }

  auto changedObjectIds = QJsonArray{};
  for (const auto* brush : brushes)
  {
    changedObjectIds.push_back(nodePathId(*brush, map.worldNode()));
  }

  const auto nodes = kdl::vec_static_cast<mdl::Node*>(brushes);
  auto& parent = mdl::parentForNodes(map, nodes);
  if (!mdl::reparentNodes(map, {{&parent, nodes}}))
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Failed to untie brushes");
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history,
    nextOperationIndex,
    map,
    toolName,
    "MCP: Untie brushes",
    changedObjectIds,
    result,
    mcpIdsModeFromParams(params));
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult untieBrushesResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return untieBrushesForMapResult(
    mapWindow->document().map(), toolName, params, history, nextOperationIndex);
}

} // namespace tb::ui
