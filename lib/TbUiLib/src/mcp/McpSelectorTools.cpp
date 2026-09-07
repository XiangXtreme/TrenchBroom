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

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include "McpBridgeServerTools.h"
#include "McpSelectionQuery.h"
#include "McpToolSupport.h"
#include "mcp/McpError.h"
#include "mdl/AddRemoveNodesCommand.h"
#include "mdl/BrushNode.h"
#include "mdl/EditorContext.h"
#include "mdl/Entity.h"
#include "mdl/EntityNodeBase.h"
#include "mdl/Map.h"
#include "mdl/Map_Selection.h"
#include "mdl/Node.h"
#include "mdl/NodeWriter.h"
#include "mdl/Transaction.h"
#include "mdl/WorldNode.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/QPathUtils.h"
#include "ui/automation/AutomationIr.h"
#include "ui/automation/AutomationTransaction.h"
#include "ui/mcp/McpObjectRegistry.h"

#include "vm/bbox.h"

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <vector>

namespace tb::ui
{
namespace mcp = tb::mcp;

namespace
{

constexpr auto DefaultSampleLimit = 12;
constexpr auto CurrentIrSchemaVersion = 1;

struct SelectorDiagnosticsInternal
{
  int matchedBeforeLimit = 0;
  bool limitApplied = false;
  int staleExcluded = 0;
  int moduleObjectIdCount = 0;
  int operationObjectIdCount = 0;
  int metadataRecordCount = 0;
};

QJsonArray stringsToJson(const QStringList& values)
{
  auto result = QJsonArray{};
  for (const auto& value : values)
  {
    result.push_back(value);
  }
  return result;
}

QJsonArray vecToJson(const vm::vec3d& value)
{
  return QJsonArray{value.x(), value.y(), value.z()};
}

QJsonObject boundsToJson(const vm::bbox3d& bounds)
{
  return QJsonObject{
    {"min", vecToJson(bounds.min)},
    {"max", vecToJson(bounds.max)},
  };
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

vm::bbox3d boundsForNodes(const std::vector<mdl::Node*>& nodes)
{
  auto first = true;
  auto result = vm::bbox3d{};
  for (const auto* node : nodes)
  {
    if (node == nullptr)
    {
      continue;
    }
    if (first)
    {
      result = node->logicalBounds();
      first = false;
    }
    else
    {
      result = vm::merge(result, node->logicalBounds());
    }
  }
  return result;
}

bool metadataValueMatches(const QJsonValue& actual, const QJsonValue& expected)
{
  if (expected.isString())
  {
    return actual.toString().compare(expected.toString(), Qt::CaseInsensitive) == 0;
  }
  return actual == expected;
}

bool metadataObjectMatches(const QJsonObject& actual, const QJsonObject& expected)
{
  for (auto it = expected.begin(); it != expected.end(); ++it)
  {
    if (!metadataValueMatches(actual.value(it.key()), it.value()))
    {
      return false;
    }
  }
  return true;
}

QJsonObject selectorFromParamsInternal(const QJsonObject& params)
{
  auto selector = params.value("selector").isObject()
                    ? params.value("selector").toObject()
                    : QJsonObject{};
  static const auto Keys = QStringList{
    "metadata", "moduleId",  "part",        "role",        "order",
    "routeId",  "temporary", "generatedBy", "operationId", "operationIds",
    "type",     "classname", "targetname",  "material",    "query",
    "min",      "max",       "boundsMode",  "limit",
  };
  for (const auto& key : Keys)
  {
    if (params.contains(key) && !selector.contains(key))
    {
      selector.insert(key, params.value(key));
    }
  }
  return selector;
}

QJsonObject selectorMetadata(const QJsonObject& selector)
{
  auto metadata = selector.value("metadata").isObject()
                    ? selector.value("metadata").toObject()
                    : QJsonObject{};
  static const auto MetadataKeys = QStringList{
    "moduleId", "part", "role", "order", "routeId", "temporary", "generatedBy"};
  for (const auto& key : MetadataKeys)
  {
    if (selector.contains(key) && !metadata.contains(key))
    {
      metadata.insert(key, selector.value(key));
    }
  }
  return metadata;
}

std::optional<QStringList> stringListFromValue(
  const QJsonValue& value, const QString& key, QString& error)
{
  auto result = QStringList{};
  if (value.isUndefined() || value.isNull())
  {
    return result;
  }
  if (value.isString())
  {
    const auto string = value.toString().trimmed();
    if (!string.isEmpty())
    {
      result.push_back(string);
    }
    return result;
  }
  if (!value.isArray())
  {
    error = QString{"%1 must be a string or array of strings"}.arg(key);
    return std::nullopt;
  }
  for (const auto& entry : value.toArray())
  {
    if (!entry.isString())
    {
      error = QString{"%1 must contain only strings"}.arg(key);
      return std::nullopt;
    }
    const auto string = entry.toString().trimmed();
    if (!string.isEmpty())
    {
      result.push_back(string);
    }
  }
  result.removeDuplicates();
  return result;
}

QJsonObject flatFilterFromSelector(const QJsonObject& selector)
{
  auto result = QJsonObject{};
  for (const auto& key : QStringList{
         "type",
         "classname",
         "targetname",
         "material",
         "query",
         "min",
         "max",
         "boundsMode",
         "limit"})
  {
    if (selector.contains(key))
    {
      result.insert(key, selector.value(key));
    }
  }
  return result;
}

const McpOperationRecord* findOperation(
  const std::vector<McpOperationRecord>& history, const QString& operationId)
{
  const auto it = std::ranges::find_if(
    history, [&](const auto& operation) { return operation.operationId == operationId; });
  return it == history.end() ? nullptr : &*it;
}

QStringList operationIdsFromSelector(const QJsonObject& selector, QString& error)
{
  auto result = QStringList{};
  if (auto one = selector.value("operationId").toString().trimmed(); !one.isEmpty())
  {
    result.push_back(one);
  }
  auto many = stringListFromValue(selector.value("operationIds"), "operationIds", error);
  if (!many)
  {
    return {};
  }
  result.append(*many);
  result.removeDuplicates();
  return result;
}

mdl::Node* resolveObjectId(
  mdl::Map& map,
  const QString& objectId,
  const McpObjectRegistry& objectRegistry,
  QJsonArray& warnings)
{
  const auto resolved = objectRegistry.resolveExternalId(map, objectId);
  if (!resolved.ok || !resolved.diagnostic.value("live").toBool(true))
  {
    warnings.push_back(
      resolved.diagnostic.isEmpty()
        ? QJsonValue{QString{"Object id does not resolve: %1"}.arg(objectId)}
        : QJsonValue{resolved.diagnostic});
    return nullptr;
  }
  const auto path = McpObjectRegistry::parseLegacyObjectId(resolved.legacyPathId);
  if (!path)
  {
    warnings.push_back(
      QString{"Invalid legacy object id: %1"}.arg(resolved.legacyPathId));
    return nullptr;
  }
  auto* node = map.worldNode().resolvePath(*path);
  if (node == nullptr)
  {
    warnings.push_back(QString{"Object id does not resolve: %1"}.arg(objectId));
  }
  return node;
}

bool isStaleWarning(const QJsonValue& warning)
{
  if (warning.isObject())
  {
    const auto object = warning.toObject();
    return object.value("stale").toBool(false) || object.contains("staleReason");
  }
  return warning.toString().contains("does not resolve", Qt::CaseInsensitive)
         || warning.toString().contains("stale", Qt::CaseInsensitive);
}

QJsonArray compactWarnings(const QJsonArray& warnings, const bool fullDetail)
{
  if (fullDetail)
  {
    return warnings;
  }

  auto result = QJsonArray{};
  auto staleCount = 0;
  auto staleSample = QJsonArray{};
  for (const auto& warning : warnings)
  {
    if (!isStaleWarning(warning))
    {
      result.push_back(warning);
      continue;
    }
    ++staleCount;
    if (staleSample.size() < 3)
    {
      staleSample.push_back(warning);
    }
  }
  if (staleCount > 0)
  {
    result.push_back(QJsonObject{
      {"type", "staleTargetSummary"},
      {"count", staleCount},
      {"sample", staleSample},
      {"detailHint", "Pass detail:\"full\" to expand stale target diagnostics."},
      {"recoveryAction", "module_compact_or_refresh_status"},
    });
  }
  return result;
}

bool objectIdResolvesLive(
  mdl::Map& map, const QString& objectId, const McpObjectRegistry& objectRegistry)
{
  const auto resolved = objectRegistry.resolveExternalId(map, objectId);
  if (!resolved.ok || !resolved.diagnostic.value("live").toBool(true))
  {
    return false;
  }
  const auto path = McpObjectRegistry::parseLegacyObjectId(resolved.legacyPathId);
  return path && map.worldNode().resolvePath(*path) != nullptr;
}

QString externalObjectIdForNode(
  mdl::Map& map, const mdl::Node& node, const McpObjectRegistry& objectRegistry)
{
  return objectRegistry.externalIdForLegacy(map, mcpNodePathId(node, map.worldNode()));
}

bool nodeInVector(const std::vector<mdl::Node*>& nodes, const mdl::Node& node)
{
  return std::ranges::find(nodes, &node) != nodes.end();
}

std::vector<mdl::Node*> dedupeNodes(std::vector<mdl::Node*> nodes)
{
  std::ranges::sort(nodes);
  nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
  return nodes;
}

QString metadataStoreKey(const QString& documentFingerprint, const QString& objectId)
{
  return documentFingerprint.isEmpty()
           ? objectId
           : QString{"%1|%2"}.arg(documentFingerprint, objectId);
}

const McpBrushMetadataRecord* findMetadataRecord(
  const QString& objectId,
  const QString& documentFingerprint,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore)
{
  const auto scopedIt =
    metadataStore.find(metadataStoreKey(documentFingerprint, objectId));
  if (scopedIt != metadataStore.end())
  {
    return &scopedIt->second;
  }

  const auto legacyIt = metadataStore.find(objectId);
  if (
    legacyIt == metadataStore.end()
    || (!legacyIt->second.documentFingerprint.isEmpty() && legacyIt->second.documentFingerprint != documentFingerprint))
  {
    return nullptr;
  }
  return &legacyIt->second;
}

QJsonObject metadataForObject(
  const QString& objectId,
  const QString& documentFingerprint,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore)
{
  const auto* record = findMetadataRecord(objectId, documentFingerprint, metadataStore);
  if (record == nullptr || record->stale)
  {
    return {};
  }
  return record->metadata;
}

bool isModuleLevelMetadataKey(const QString& key)
{
  static const auto Keys = QStringList{
    "moduleId",
    "routeId",
    "temporary",
    "generatedBy",
    "intent",
    "name",
    "description",
  };
  return Keys.contains(key);
}

QJsonObject moduleLevelMetadata(const QJsonObject& metadata)
{
  auto result = QJsonObject{};
  for (auto it = metadata.begin(); it != metadata.end(); ++it)
  {
    if (isModuleLevelMetadataKey(it.key()))
    {
      result.insert(it.key(), it.value());
    }
  }
  return result;
}

QJsonObject metadataForNode(
  mdl::Map& map,
  const mdl::Node& node,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const McpObjectRegistry& objectRegistry)
{
  const auto documentFingerprint = documentFingerprintForMap(map);
  const auto stableId = externalObjectIdForNode(map, node, objectRegistry);
  if (const auto metadata =
        metadataForObject(stableId, documentFingerprint, metadataStore);
      !metadata.isEmpty())
  {
    return metadata;
  }
  return metadataForObject(
    mcpNodePathId(node, map.worldNode()), documentFingerprint, metadataStore);
}

bool recordMatchesDocument(
  const McpBrushMetadataRecord& record, const QString& documentFingerprint)
{
  return record.documentFingerprint.isEmpty()
         || record.documentFingerprint == documentFingerprint;
}

bool recordMatchesDocument(
  const McpModuleRecord& record, const QString& documentFingerprint)
{
  return record.documentFingerprint.isEmpty()
         || record.documentFingerprint == documentFingerprint;
}

const McpModuleRecord* findModuleRecord(
  const QString& moduleId,
  const QString& documentFingerprint,
  const std::map<QString, McpModuleRecord>& moduleStore)
{
  for (const auto& [key, module] : moduleStore)
  {
    Q_UNUSED(key);
    if (module.moduleId == moduleId && recordMatchesDocument(module, documentFingerprint))
    {
      return &module;
    }
  }
  return nullptr;
}

QString moduleStoreKey(const QString& documentFingerprint, const QString& moduleId)
{
  return documentFingerprint.isEmpty()
           ? moduleId
           : QString{"%1|%2"}.arg(documentFingerprint, moduleId);
}

QJsonObject metadataRecordJson(const McpBrushMetadataRecord& record)
{
  return QJsonObject{
    {"objectId", record.objectId},
    {"documentFingerprint", record.documentFingerprint},
    {"metadata", record.metadata},
    {"stale", record.stale},
  };
}

McpBrushMetadataRecord metadataRecordFromJson(const QJsonObject& object)
{
  auto record = McpBrushMetadataRecord{};
  record.objectId = object.value("objectId").toString();
  record.documentFingerprint = object.value("documentFingerprint").toString();
  record.metadata = object.value("metadata").toObject();
  record.stale = object.value("stale").toBool(false);
  return record;
}

QJsonObject moduleRecordJson(const McpModuleRecord& record)
{
  return QJsonObject{
    {"moduleId", record.moduleId},
    {"documentFingerprint", record.documentFingerprint},
    {"objectIds", stringsToJson(record.objectIds)},
    {"operationIds", stringsToJson(record.operationIds)},
    {"metadata", record.metadata},
    {"revision", record.revision},
    {"activeOperationId", record.activeOperationId},
    {"contentHash", record.contentHash},
    {"qualityPolicy", record.qualityPolicy},
  };
}

QStringList stringListFromArray(const QJsonArray& values)
{
  auto result = QStringList{};
  for (const auto& value : values)
  {
    if (value.isString())
    {
      result.push_back(value.toString());
    }
  }
  return result;
}

McpModuleRecord moduleRecordFromJson(const QJsonObject& object)
{
  auto record = McpModuleRecord{};
  record.moduleId = object.value("moduleId").toString();
  record.documentFingerprint = object.value("documentFingerprint").toString();
  record.objectIds = stringListFromArray(object.value("objectIds").toArray());
  record.operationIds = stringListFromArray(object.value("operationIds").toArray());
  record.metadata = object.value("metadata").toObject();
  record.revision = object.value("revision").toInt();
  record.activeOperationId = object.value("activeOperationId").toString();
  record.contentHash = object.value("contentHash").toString();
  record.qualityPolicy = object.value("qualityPolicy").toObject();
  return record;
}

template <typename Record>
QJsonObject changedRecordSnapshot(
  const std::map<QString, Record>& before,
  const std::map<QString, Record>& after,
  const bool useBefore,
  const std::function<QJsonObject(const Record&)>& toJson)
{
  auto keys = std::set<QString>{};
  for (const auto& [key, record] : before)
  {
    Q_UNUSED(record);
    keys.insert(key);
  }
  for (const auto& [key, record] : after)
  {
    Q_UNUSED(record);
    keys.insert(key);
  }

  auto result = QJsonObject{};
  for (const auto& key : keys)
  {
    const auto beforeIt = before.find(key);
    const auto afterIt = after.find(key);
    const auto beforeJson =
      beforeIt != before.end() ? toJson(beforeIt->second) : QJsonObject{};
    const auto afterJson =
      afterIt != after.end() ? toJson(afterIt->second) : QJsonObject{};
    if ((beforeIt != before.end()) == (afterIt != after.end()) && beforeJson == afterJson)
    {
      continue;
    }

    const auto selected = useBefore ? beforeIt : afterIt;
    const auto& source = useBefore ? before : after;
    if (selected == source.end())
    {
      result.insert(key, QJsonValue{QJsonValue::Null});
    }
    else
    {
      result.insert(key, toJson(selected->second));
    }
  }
  return result;
}

QByteArray sessionDeltaJson(
  const std::map<QString, McpBrushMetadataRecord>& metadataBefore,
  const std::map<QString, McpModuleRecord>& modulesBefore,
  const std::map<QString, McpBrushMetadataRecord>& metadataAfter,
  const std::map<QString, McpModuleRecord>& modulesAfter,
  const bool useBefore)
{
  const auto metadata = changedRecordSnapshot<McpBrushMetadataRecord>(
    metadataBefore, metadataAfter, useBefore, metadataRecordJson);
  const auto modules = changedRecordSnapshot<McpModuleRecord>(
    modulesBefore, modulesAfter, useBefore, moduleRecordJson);
  if (metadata.isEmpty() && modules.isEmpty())
  {
    return {};
  }
  return QJsonDocument{QJsonObject{{"metadata", metadata}, {"modules", modules}}}.toJson(
    QJsonDocument::Compact);
}

QString moduleContentHash(
  mdl::Map& map,
  const std::vector<mdl::Node*>& nodes,
  const McpObjectRegistry& objectRegistry)
{
  auto serializedNodes = std::vector<std::pair<QString, QByteArray>>{};
  serializedNodes.reserve(nodes.size());
  for (auto* node : nodes)
  {
    if (node == nullptr)
    {
      continue;
    }
    auto stream = std::ostringstream{};
    auto writer = mdl::NodeWriter{map.worldNode(), stream};
    writer.setStripTbProperties(true);
    writer.writeNodes({node}, map.taskManager());
    serializedNodes.emplace_back(
      externalObjectIdForNode(map, *node, objectRegistry),
      QByteArray::fromStdString(stream.str()));
  }
  std::ranges::sort(serializedNodes, [](const auto& lhs, const auto& rhs) {
    return lhs.second != rhs.second ? lhs.second < rhs.second : lhs.first < rhs.first;
  });
  auto bytes = QByteArray{};
  for (const auto& [objectId, serialized] : serializedNodes)
  {
    Q_UNUSED(objectId);
    bytes.append(serialized);
    bytes.append('\0');
  }
  return QString{"sha256:%1"}.arg(QString::fromLatin1(
    QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()));
}

QStringList liveModuleObjectIdsFromMetadata(
  mdl::Map& map,
  const QString& moduleId,
  const QString& documentFingerprint,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const McpObjectRegistry& objectRegistry)
{
  auto result = QStringList{};
  for (const auto& [objectId, record] : metadataStore)
  {
    Q_UNUSED(objectId);
    if (
      record.stale || !recordMatchesDocument(record, documentFingerprint)
      || record.metadata.value("moduleId").toString() != moduleId)
    {
      continue;
    }
    if (objectIdResolvesLive(map, record.objectId, objectRegistry))
    {
      result.push_back(record.objectId);
    }
  }
  result.removeDuplicates();
  return result;
}

bool metadataSelectorNeedsObjectFilter(
  const QJsonObject& selector, const QJsonObject& metadata, const bool seededByModuleId)
{
  if (metadata.isEmpty())
  {
    return false;
  }

  if (
    selector.value("metadata").isObject()
    && !selector.value("metadata").toObject().isEmpty())
  {
    return true;
  }

  if (!seededByModuleId)
  {
    return true;
  }

  for (auto it = metadata.begin(); it != metadata.end(); ++it)
  {
    if (it.key() != "moduleId")
    {
      return true;
    }
  }
  return false;
}

bool nodeMatchesMetadata(
  mdl::Map& map,
  const mdl::Node& node,
  const QJsonObject& metadata,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const McpObjectRegistry& objectRegistry)
{
  return metadataObjectMatches(
    metadataForNode(map, node, metadataStore, objectRegistry), metadata);
}

std::vector<mdl::Node*> resolveSelectorNodesInternal(
  mdl::Map& map,
  const QJsonObject& selector,
  const std::vector<McpOperationRecord>& history,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry,
  QJsonArray& warnings,
  QString& error,
  SelectorDiagnosticsInternal* diagnostics = nullptr)
{
  auto candidates = std::vector<mdl::Node*>{};
  auto seeded = false;
  auto seededByModuleId = false;
  const auto documentFingerprint = documentFingerprintForMap(map);
  auto staleExcluded = 0;
  auto moduleObjectIdCount = 0;
  auto operationObjectIdCount = 0;
  auto metadataRecordCount = 0;

  const auto moduleId = selector.value("moduleId").toString().trimmed();
  if (!moduleId.isEmpty())
  {
    if (const auto* module = findModuleRecord(moduleId, documentFingerprint, moduleStore))
    {
      for (const auto& objectId : module->objectIds)
      {
        ++moduleObjectIdCount;
        if (auto* node = resolveObjectId(map, objectId, objectRegistry, warnings))
        {
          candidates.push_back(node);
        }
        else
        {
          ++staleExcluded;
        }
      }
      seeded = true;
      seededByModuleId = true;
    }
    for (const auto& objectId : liveModuleObjectIdsFromMetadata(
           map, moduleId, documentFingerprint, metadataStore, objectRegistry))
    {
      ++moduleObjectIdCount;
      if (auto* node = resolveObjectId(map, objectId, objectRegistry, warnings))
      {
        candidates.push_back(node);
      }
      else
      {
        ++staleExcluded;
      }
    }
    if (moduleObjectIdCount > 0)
    {
      seeded = true;
      seededByModuleId = true;
    }
  }

  for (const auto& operationId : operationIdsFromSelector(selector, error))
  {
    if (!error.isEmpty())
    {
      return {};
    }
    const auto* operation = findOperation(history, operationId);
    if (operation == nullptr)
    {
      warnings.push_back(QString{"Unknown MCP operation id: %1"}.arg(operationId));
      continue;
    }
    if (operation->undone)
    {
      warnings.push_back(QString{"MCP operation is undone: %1"}.arg(operationId));
    }
    for (const auto& objectId : operation->changedObjectIds)
    {
      ++operationObjectIdCount;
      if (auto* node = resolveObjectId(map, objectId, objectRegistry, warnings))
      {
        candidates.push_back(node);
      }
      else
      {
        ++staleExcluded;
      }
    }
    seeded = true;
  }

  const auto metadata = selectorMetadata(selector);
  if (!metadata.isEmpty())
  {
    if (seeded && metadataSelectorNeedsObjectFilter(selector, metadata, seededByModuleId))
    {
      candidates.erase(
        std::remove_if(
          candidates.begin(),
          candidates.end(),
          [&](const auto* node) {
            return node == nullptr
                   || !nodeMatchesMetadata(
                     map, *node, metadata, metadataStore, objectRegistry);
          }),
        candidates.end());
    }
    else if (!seeded)
    {
      for (const auto& [storedObjectId, record] : metadataStore)
      {
        Q_UNUSED(storedObjectId);
        if (
          record.stale || !recordMatchesDocument(record, documentFingerprint)
          || !metadataObjectMatches(record.metadata, metadata))
        {
          continue;
        }
        ++metadataRecordCount;
        if (auto* node = resolveObjectId(map, record.objectId, objectRegistry, warnings))
        {
          candidates.push_back(node);
        }
        else
        {
          ++staleExcluded;
        }
      }
      for (const auto& [storedModuleId, module] : moduleStore)
      {
        Q_UNUSED(storedModuleId);
        if (
          !recordMatchesDocument(module, documentFingerprint)
          || !metadataObjectMatches(module.metadata, metadata))
        {
          continue;
        }
        for (const auto& objectId : module.objectIds)
        {
          if (auto* node = resolveObjectId(map, objectId, objectRegistry, warnings))
          {
            candidates.push_back(node);
          }
          else
          {
            ++staleExcluded;
          }
        }
      }
      seeded = true;
    }
  }

  auto filter = flatFilterFromSelector(selector);
  if (!filter.isEmpty() || !seeded)
  {
    if (!filter.contains("limit"))
    {
      filter.insert("limit", selector.value("limit").toInt(1000));
    }
    auto options = McpSelectionQueryOptions{};
    options.excludeWorld = true;
    options.selectableOnly = false;
    options.leafOnly = selector.value("leafOnly").toBool(false);
    options.exactTypeOnly = true;
    options.removeDescendantMatches = false;
    auto filtered = mcpFilteredNodes(map, filter, options, error);
    if (!error.isEmpty())
    {
      return {};
    }
    if (seeded)
    {
      auto intersection = std::vector<mdl::Node*>{};
      for (auto* node : candidates)
      {
        if (node != nullptr && nodeInVector(filtered, *node))
        {
          intersection.push_back(node);
        }
      }
      candidates = std::move(intersection);
    }
    else
    {
      candidates = std::move(filtered);
    }
  }

  candidates = dedupeNodes(std::move(candidates));
  const auto matchedBeforeLimit = static_cast<int>(candidates.size());
  const auto limit = std::clamp(selector.value("limit").toInt(100), 1, 10000);
  auto limitApplied = false;
  if (static_cast<int>(candidates.size()) > limit)
  {
    candidates.resize(static_cast<size_t>(limit));
    limitApplied = true;
    warnings.push_back(QString{"selectorResultTruncated: limit=%1"}.arg(limit));
  }
  if (diagnostics != nullptr)
  {
    diagnostics->matchedBeforeLimit = matchedBeforeLimit;
    diagnostics->limitApplied = limitApplied;
    diagnostics->staleExcluded = staleExcluded;
    diagnostics->moduleObjectIdCount = moduleObjectIdCount;
    diagnostics->operationObjectIdCount = operationObjectIdCount;
    diagnostics->metadataRecordCount = metadataRecordCount;
  }
  return candidates;
}

QJsonObject nodeSummary(
  mdl::Map& map,
  const mdl::Node& node,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const McpObjectRegistry& objectRegistry,
  const bool fullDetail)
{
  const auto objectId = externalObjectIdForNode(map, node, objectRegistry);
  auto result = QJsonObject{
    {"objectId", objectId},
    {"type", mcpNodeTypeName(node)},
    {"name", QString::fromStdString(node.name())},
    {"bounds", boundsToJson(node.logicalBounds())},
    {"metadata", metadataForNode(map, node, metadataStore, objectRegistry)},
  };
  if (fullDetail)
  {
    if (const auto* entityNode = dynamic_cast<const mdl::EntityNodeBase*>(&node))
    {
      result.insert(
        "classname", QString::fromStdString(entityNode->entity().classname()));
      result.insert("origin", vecToJson(entityNode->entity().origin()));
      result.insert("properties", entityPropertiesJson(entityNode->entity()));
    }
  }
  return result;
}

QJsonObject compactTargetResult(
  mdl::Map& map,
  const std::vector<mdl::Node*>& nodes,
  const QJsonObject& selector,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const McpObjectRegistry& objectRegistry,
  const QJsonArray& warnings,
  const QString& idsMode,
  const bool fullWarnings = false)
{
  auto objectIds = QStringList{};
  auto samples = QJsonArray{};
  const auto mode = idsMode.trimmed().toLower();
  const auto sampleLimit =
    std::clamp(selector.value("sampleLimit").toInt(DefaultSampleLimit), 0, 100);
  const auto includeSamples = mode.isEmpty() || mode == "sample" || mode == "full";
  for (auto i = 0; i < static_cast<int>(nodes.size()); ++i)
  {
    const auto* node = nodes[static_cast<size_t>(i)];
    if (node == nullptr)
    {
      continue;
    }
    objectIds.push_back(externalObjectIdForNode(map, *node, objectRegistry));
    if (includeSamples && i < sampleLimit)
    {
      samples.push_back(
        nodeSummary(map, *node, metadataStore, objectRegistry, fullWarnings));
    }
  }

  auto result = QJsonObject{
    {"selector", selector},
    {"matchedCount", static_cast<int>(nodes.size())},
    {"warnings", compactWarnings(warnings, fullWarnings)},
  };
  if (includeSamples)
  {
    result.insert("sampleCount", samples.size());
    result.insert("sample", samples);
  }
  if (!nodes.empty())
  {
    result.insert("bounds", boundsToJson(boundsForNodes(nodes)));
  }

  if (mode == "full")
  {
    result.insert("objectIds", stringsToJson(objectIds));
  }
  else if (mode == "sample")
  {
    auto sampleIds = QStringList{};
    for (auto i = 0; i < std::min(sampleLimit, static_cast<int>(objectIds.size())); ++i)
    {
      sampleIds.push_back(objectIds[i]);
    }
    result.insert("objectIdSample", stringsToJson(sampleIds));
    result.insert("objectIdCount", objectIds.size());
  }
  else if (mode == "count" || mode.isEmpty())
  {
    result.insert("objectIdCount", objectIds.size());
  }
  else if (mode != "none")
  {
    result.insert("objectIdCount", objectIds.size());
    auto updatedWarnings = result.value("warnings").toArray();
    updatedWarnings.push_back(
      QString{"unknownIdsMode: %1; returned count only"}.arg(idsMode));
    result.insert("warnings", updatedWarnings);
  }
  return result;
}

void applyChangedObjectIdsMode(
  QJsonObject& result, const QStringList& objectIds, const QString& idsMode)
{
  const auto mode = idsMode.trimmed().toLower();
  result.remove("changedObjectIds");
  result.remove("changedObjectIdSample");
  result.insert("changedObjectCount", objectIds.size());
  if (mode == "full")
  {
    result.insert("changedObjectIds", stringsToJson(objectIds));
  }
  else if (mode == "sample")
  {
    result.insert(
      "changedObjectIdSample",
      stringsToJson(objectIds.mid(
        0, std::min(DefaultSampleLimit, static_cast<int>(objectIds.size())))));
  }
  else if (mode == "none" || mode == "count" || mode.isEmpty())
  {
    return;
  }
  else
  {
    auto warnings = result.value("warnings").toArray();
    warnings.push_back(
      QString{"unknownIdsMode: %1; returned changedObjectCount only"}.arg(idsMode));
    result.insert("warnings", warnings);
  }
}

void compactAppliedOperationResults(QJsonObject& result, const QString& idsMode)
{
  const auto mode = idsMode.trimmed().toLower();
  if (mode == "full")
  {
    return;
  }

  auto applied = QJsonArray{};
  for (const auto& value : result.value("applied").toArray())
  {
    auto operation = value.toObject();
    const auto changedObjectIds = operation.value("changedObjectIds").toArray();
    operation.insert("changedObjectCount", changedObjectIds.size());
    operation.remove("changedObjectIds");
    if (mode == "sample")
    {
      auto sample = QJsonArray{};
      for (auto i = 0;
           i < std::min(DefaultSampleLimit, static_cast<int>(changedObjectIds.size()));
           ++i)
      {
        sample.push_back(changedObjectIds[i]);
      }
      operation.insert("changedObjectIdSample", sample);
    }
    applied.push_back(operation);
  }
  result.insert("applied", applied);
}

QJsonArray nodeIdsJson(
  mdl::Map& map,
  const std::vector<mdl::Node*>& nodes,
  const McpObjectRegistry& objectRegistry)
{
  auto result = QJsonArray{};
  for (const auto* node : nodes)
  {
    if (node != nullptr)
    {
      result.push_back(externalObjectIdForNode(map, *node, objectRegistry));
    }
  }
  return result;
}

std::optional<QJsonArray> removeNodesWithTransaction(
  mdl::Map& map, const QString& transactionName, std::vector<mdl::Node*> nodes)
{
  nodes = dedupeNodes(std::move(nodes));
  nodes.erase(
    std::remove_if(
      std::begin(nodes),
      std::end(nodes),
      [&](const auto* node) {
        return node == &map.worldNode()
               || std::ranges::any_of(nodes, [&](const auto* other) {
                    return node != other && node->isDescendantOf(*other);
                  });
      }),
    std::end(nodes));

  if (nodes.empty())
  {
    return std::nullopt;
  }

  auto nodesByParent = std::map<mdl::Node*, std::vector<mdl::Node*>>{};
  auto removedIds = QJsonArray{};
  for (auto* node : nodes)
  {
    auto* parent = node->parent();
    if (!parent || !parent->canRemoveChild(*node))
    {
      return std::nullopt;
    }
    nodesByParent[parent].push_back(node);
    removedIds.push_back(mcpNodePathId(*node, map.worldNode()));
  }

  const auto ok = executeTransaction(map, transactionName, [&]() {
    mdl::deselectNodes(map, nodes);
    return map.executeAndStore(mdl::AddRemoveNodesCommand::remove(nodesByParent));
  });

  return ok ? std::optional{removedIds} : std::nullopt;
}

QString makeOperationId(int& nextOperationIndex)
{
  return QString{"mcp-op-%1"}.arg(nextOperationIndex++);
}

void recordDeleteOperation(
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  mdl::Map& map,
  const QString& toolName,
  const QString& transactionName,
  const QJsonArray& deletedObjectIds,
  QJsonObject& result)
{
  auto operation = McpOperationRecord{};
  operation.operationId = makeOperationId(nextOperationIndex);
  operation.toolName = toolName;
  operation.transactionName = transactionName;
  operation.operationKind = "delete";
  operation.documentPath = map.path().empty() ? QString{} : pathAsQString(map.path());
  operation.documentFingerprint = documentFingerprintForMap(map);
  operation.setChangedObjectIds(QJsonArray{});
  operation.setDeletedObjectIds(deletedObjectIds);

  result = QJsonObject{
    {"operationId", operation.operationId},
    {"transactionName", operation.transactionName},
    {"mutatedDocument", true},
    {"activeDocumentPath", operation.documentPath},
    {"documentFingerprint", operation.documentFingerprint},
    {"operationKind", operation.operationKind},
    {"changedObjectCount", 0},
    {"changedObjectIds", QJsonArray{}},
    {"deletedObjectIds", deletedObjectIds},
    {"deletedObjectCount", deletedObjectIds.size()},
    {"resourceUri", QString{"tbmcp://operation/%1"}.arg(operation.operationId)},
  };
  operation.setSummary(result);
  appendMcpOperationRecord(history, std::move(operation));
}

void markDeletedMetadata(
  const QJsonArray& deletedObjectIds,
  mdl::Map& map,
  const McpObjectRegistry& objectRegistry,
  std::map<QString, McpBrushMetadataRecord>& metadataStore,
  std::map<QString, McpModuleRecord>& moduleStore)
{
  auto deleted = std::set<QString>{};
  for (const auto& value : deletedObjectIds)
  {
    if (value.isString())
    {
      deleted.insert(value.toString());
    }
  }
  auto deletedLegacyIds = std::set<QString>{};
  for (const auto& objectId : deleted)
  {
    const auto resolved = objectRegistry.resolveExternalId(map, objectId);
    if (!resolved.legacyPathId.isEmpty())
    {
      deletedLegacyIds.insert(resolved.legacyPathId);
    }
    if (McpObjectRegistry::isLegacyObjectId(objectId))
    {
      deletedLegacyIds.insert(objectId);
    }
  }
  const auto matchesDeleted = [&](const QString& objectId) {
    if (deleted.contains(objectId) || deletedLegacyIds.contains(objectId))
    {
      return true;
    }
    const auto resolved = objectRegistry.resolveExternalId(map, objectId);
    return !resolved.legacyPathId.isEmpty()
           && deletedLegacyIds.contains(resolved.legacyPathId);
  };
  for (auto& [objectId, record] : metadataStore)
  {
    if (matchesDeleted(objectId) || matchesDeleted(record.objectId))
    {
      record.stale = true;
    }
  }
  for (auto& [moduleId, module] : moduleStore)
  {
    module.objectIds.erase(
      std::remove_if(
        module.objectIds.begin(),
        module.objectIds.end(),
        [&](const auto& objectId) { return matchesDeleted(objectId); }),
      module.objectIds.end());
  }
}

QJsonObject moduleMetadataFromObjects(
  const QString& moduleId,
  const QString& documentFingerprint,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore)
{
  auto result = QJsonObject{};
  for (const auto& [objectId, record] : metadataStore)
  {
    Q_UNUSED(objectId);
    if (
      !record.stale && recordMatchesDocument(record, documentFingerprint)
      && record.metadata.value("moduleId").toString() == moduleId)
    {
      const auto moduleMetadata = moduleLevelMetadata(record.metadata);
      for (auto it = moduleMetadata.begin(); it != moduleMetadata.end(); ++it)
      {
        if (!result.contains(it.key()))
        {
          result.insert(it.key(), it.value());
        }
      }
    }
  }
  return result;
}

QJsonArray modulePartSummary(
  mdl::Map& map,
  const QString& moduleId,
  const QString& documentFingerprint,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const McpObjectRegistry& objectRegistry,
  const bool staleOnly = false)
{
  auto counts = std::map<QString, int>{};
  for (const auto& [objectId, record] : metadataStore)
  {
    Q_UNUSED(objectId);
    if (
      !recordMatchesDocument(record, documentFingerprint)
      || record.metadata.value("moduleId").toString() != moduleId)
    {
      continue;
    }
    const auto live =
      !record.stale && objectIdResolvesLive(map, record.objectId, objectRegistry);
    if (live == staleOnly)
    {
      continue;
    }
    const auto part = record.metadata.value("part").toString().trimmed();
    if (!part.isEmpty())
    {
      ++counts[part];
    }
  }

  auto result = QJsonArray{};
  for (const auto& [part, count] : counts)
  {
    result.push_back(QJsonObject{{"part", part}, {"count", count}});
  }
  return result;
}

QStringList moduleObjectIdsFromMetadata(
  const QString& moduleId,
  const QString& documentFingerprint,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore)
{
  auto result = QStringList{};
  for (const auto& [objectId, record] : metadataStore)
  {
    if (
      !record.stale && recordMatchesDocument(record, documentFingerprint)
      && record.metadata.value("moduleId").toString() == moduleId)
    {
      result.push_back(record.objectId);
    }
  }
  result.removeDuplicates();
  return result;
}

McpModuleRecord mergedModuleRecord(
  const QString& moduleId,
  const QString& documentFingerprint,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore)
{
  auto result = McpModuleRecord{};
  result.moduleId = moduleId;
  result.documentFingerprint = documentFingerprint;
  result.metadata =
    moduleMetadataFromObjects(moduleId, documentFingerprint, metadataStore);
  if (const auto* module = findModuleRecord(moduleId, documentFingerprint, moduleStore))
  {
    result = *module;
    result.moduleId = moduleId;
    result.metadata = moduleLevelMetadata(result.metadata);
    if (result.metadata.isEmpty())
    {
      result.metadata =
        moduleMetadataFromObjects(moduleId, documentFingerprint, metadataStore);
    }
  }
  result.objectIds.append(
    moduleObjectIdsFromMetadata(moduleId, documentFingerprint, metadataStore));
  result.objectIds.removeDuplicates();
  return result;
}

QJsonObject moduleSummary(
  mdl::Map& map,
  const McpModuleRecord& module,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const McpObjectRegistry& objectRegistry)
{
  auto liveCount = 0;
  auto liveReferenceCount = 0;
  auto staleCount = 0;
  auto warnings = QJsonArray{};
  auto nodes = std::vector<mdl::Node*>{};
  auto objectIds = module.objectIds;
  objectIds.removeDuplicates();
  for (const auto& objectId : objectIds)
  {
    if (auto* node = resolveObjectId(map, objectId, objectRegistry, warnings))
    {
      ++liveReferenceCount;
      if (nodeInVector(nodes, *node))
      {
        continue;
      }
      ++liveCount;
      nodes.push_back(node);
    }
    else
    {
      ++staleCount;
    }
  }
  auto rootOperationIds = QStringList{};
  if (!module.activeOperationId.isEmpty())
  {
    rootOperationIds.push_back(module.activeOperationId);
  }
  else if (!module.operationIds.isEmpty())
  {
    rootOperationIds.push_back(module.operationIds.back());
  }
  const auto contentHash = module.contentHash.isEmpty()
                             ? moduleContentHash(map, nodes, objectRegistry)
                             : module.contentHash;
  return QJsonObject{
    {"moduleId", module.moduleId},
    {"objectCount", module.objectIds.size()},
    {"storedReferenceCount", module.objectIds.size()},
    {"canonicalObjectCount", liveCount},
    {"duplicateAliasCount", std::max(0, liveReferenceCount - liveCount)},
    {"staleReferenceCount", staleCount},
    {"liveObjectCount", liveCount},
    {"staleObjectCount", staleCount},
    {"operationIds", stringsToJson(module.operationIds)},
    {"rootOperationIds", stringsToJson(rootOperationIds)},
    {"auditOperationIds", stringsToJson(module.operationIds)},
    {"operationCount", module.operationIds.size()},
    {"moduleRevision", module.revision <= 0 && !nodes.empty() ? 1 : module.revision},
    {"moduleContentHash", contentHash},
    {"activeOperationId", module.activeOperationId},
    {"qualityPolicy", module.qualityPolicy},
    {"metadata", module.metadata},
    {"parts",
     modulePartSummary(
       map, module.moduleId, module.documentFingerprint, metadataStore, objectRegistry)},
    {"liveParts",
     modulePartSummary(
       map, module.moduleId, module.documentFingerprint, metadataStore, objectRegistry)},
    {"staleParts",
     modulePartSummary(
       map,
       module.moduleId,
       module.documentFingerprint,
       metadataStore,
       objectRegistry,
       true)},
    {"bounds", nodes.empty() ? QJsonObject{} : boundsToJson(boundsForNodes(nodes))},
  };
}

QString canonicalIrHash(const QJsonObject& ir)
{
  return automation::canonicalAutomationIrHash(ir);
}

std::vector<mdl::Node*> canonicalModuleNodes(
  mdl::Map& map, const McpModuleRecord& module, const McpObjectRegistry& objectRegistry)
{
  auto nodes = std::vector<mdl::Node*>{};
  for (const auto& objectId : module.objectIds)
  {
    auto warnings = QJsonArray{};
    if (auto* node = resolveObjectId(map, objectId, objectRegistry, warnings);
        node != nullptr && !nodeInVector(nodes, *node))
    {
      nodes.push_back(node);
    }
  }
  return nodes;
}

QJsonObject replacementTargetState(
  mdl::Map& map,
  const QJsonObject& ir,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry)
{
  const auto defaultMetadata = ir.value("defaultMetadata").toObject();
  const auto moduleId =
    ir.value("moduleId").toString(defaultMetadata.value("moduleId").toString());
  const auto documentFingerprint = documentFingerprintForMap(map, &objectRegistry);
  const auto module =
    mergedModuleRecord(moduleId, documentFingerprint, metadataStore, moduleStore);
  const auto nodes = canonicalModuleNodes(map, module, objectRegistry);
  auto canonicalObjectIds = QStringList{};
  for (auto* node : nodes)
  {
    canonicalObjectIds.push_back(externalObjectIdForNode(map, *node, objectRegistry));
  }
  canonicalObjectIds.removeDuplicates();
  canonicalObjectIds.sort(Qt::CaseInsensitive);
  const auto currentHash = moduleContentHash(map, nodes, objectRegistry);
  auto effectiveRevision = module.revision;
  if (effectiveRevision <= 0 && !nodes.empty())
  {
    effectiveRevision = 1;
  }
  else if (!module.contentHash.isEmpty() && module.contentHash != currentHash)
  {
    ++effectiveRevision;
  }
  const auto exists =
    findModuleRecord(moduleId, documentFingerprint, moduleStore) != nullptr
    || !module.objectIds.isEmpty();
  return QJsonObject{
    {"targetModuleExists", exists},
    {"targetModuleRevision", effectiveRevision},
    {"targetModuleContentHash", currentHash},
    {"targetCanonicalObjectCount", canonicalObjectIds.size()},
    {"targetCanonicalObjectIds", stringsToJson(canonicalObjectIds)},
  };
}

void enrichIrPreviewForApply(
  QJsonObject& preview,
  mdl::Map& map,
  const QJsonObject& ir,
  const std::map<QString, McpBrushMetadataRecord>* metadataStore,
  const std::map<QString, McpModuleRecord>* moduleStore,
  const McpObjectRegistry* objectRegistry)
{
  preview.insert("applyMode", ir.value("applyMode").toString("create"));
  preview.insert("irHash", canonicalIrHash(ir));
  if (
    ir.value("applyMode").toString("create") != "replace_module"
    || metadataStore == nullptr || moduleStore == nullptr || objectRegistry == nullptr)
  {
    return;
  }
  const auto target =
    replacementTargetState(map, ir, *metadataStore, *moduleStore, *objectRegistry);
  for (auto it = target.begin(); it != target.end(); ++it)
  {
    preview.insert(it.key(), it.value());
  }
  if (!target.value("targetModuleExists").toBool(false))
  {
    preview.insert("valid", false);
    auto warnings = preview.value("warnings").toArray();
    warnings.push_back("replaceModuleTargetNotFound");
    preview.insert("warnings", warnings);
  }
}

bool metadataMarksContinuityTarget(const QJsonObject& metadata)
{
  const auto role = metadata.value("role").toString().trimmed().toLower();
  if (role == "walkable" || role == "playable" || role == "route")
  {
    return true;
  }
  const auto part = metadata.value("part").toString().trimmed().toLower();
  static const auto Parts = QStringList{
    "road",
    "floor",
    "surface",
    "ribbon",
    "path",
    "platform",
    "bhop-platform",
    "bhop_platform",
    "slide-ramp",
    "slide_ramp",
    "ramp",
    "steps",
    "stair",
    "stairs",
  };
  return Parts.contains(part);
}

std::vector<mdl::Node*> defaultContinuityNodes(
  mdl::Map& map,
  const std::vector<mdl::Node*>& nodes,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const McpObjectRegistry& objectRegistry)
{
  auto result = std::vector<mdl::Node*>{};
  for (auto* node : nodes)
  {
    if (
      node != nullptr
      && metadataMarksContinuityTarget(
        metadataForNode(map, *node, metadataStore, objectRegistry)))
    {
      result.push_back(node);
    }
  }
  return result;
}

QStringList allModuleIds(
  const QString& documentFingerprint,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore)
{
  auto ids = QStringList{};
  for (const auto& [storedModuleId, module] : moduleStore)
  {
    Q_UNUSED(storedModuleId);
    const auto moduleId = module.moduleId.trimmed();
    if (!moduleId.isEmpty())
    {
      if (recordMatchesDocument(module, documentFingerprint))
      {
        ids.push_back(moduleId);
      }
    }
  }
  for (const auto& [objectId, record] : metadataStore)
  {
    Q_UNUSED(objectId);
    if (!recordMatchesDocument(record, documentFingerprint))
    {
      continue;
    }
    const auto moduleId = record.metadata.value("moduleId").toString().trimmed();
    if (!moduleId.isEmpty())
    {
      ids.push_back(moduleId);
    }
  }
  ids.removeDuplicates();
  ids.sort(Qt::CaseInsensitive);
  return ids;
}

bool validateIrShape(QJsonObject& ir, QString& error, QJsonArray* warnings = nullptr)
{
  const auto parsed = automation::parseAutomationIr(QJsonObject{{"ir", ir}});
  if (!parsed.ir)
  {
    error = parsed.error;
    return false;
  }
  ir = *parsed.ir;
  if (warnings != nullptr)
  {
    for (const auto& warning : parsed.warnings)
    {
      warnings->push_back(warning);
    }
  }
  return true;
}

std::optional<QJsonObject> irFromParams(
  const QJsonObject& params, QString& error, QJsonArray* warnings = nullptr)
{
  const auto irValue = params.value("ir");
  if (irValue.isObject())
  {
    auto ir = irValue.toObject();
    if (params.value("qualityPolicy").isObject())
    {
      ir.insert("qualityPolicy", params.value("qualityPolicy"));
    }
    if (params.contains("applyMode"))
    {
      ir.insert("applyMode", params.value("applyMode"));
    }
    if (params.contains("requireMaterialAvailable"))
    {
      ir.insert("requireMaterialAvailable", params.value("requireMaterialAvailable"));
    }
    if (!validateIrShape(ir, error, warnings))
    {
      return std::nullopt;
    }
    return ir;
  }
  if (!irValue.isUndefined() && !irValue.isNull())
  {
    error = "IR field must be an object";
    return std::nullopt;
  }
  if (params.contains("operations") && !params.value("operations").isArray())
  {
    error = "IR operations must be an array";
    return std::nullopt;
  }
  if (params.contains("entities") && !params.value("entities").isArray())
  {
    error = "IR entities must be an array";
    return std::nullopt;
  }
  if (params.value("operations").isArray() || params.value("entities").isArray())
  {
    auto ir = QJsonObject{};
    if (params.value("operations").isArray())
    {
      ir.insert("operations", params.value("operations"));
    }
    if (params.value("entities").isArray())
    {
      ir.insert("entities", params.value("entities"));
    }
    for (const auto& key :
         {"schemaVersion",
          "name",
          "moduleId",
          "defaultMetadata",
          "material",
          "grid",
          "qualityPolicy",
          "applyMode",
          "requireMaterialAvailable"})
    {
      if (params.contains(key))
      {
        ir.insert(key, params.value(key));
      }
    }
    if (!validateIrShape(ir, error, warnings))
    {
      return std::nullopt;
    }
    return ir;
  }
  error = "IR requires ir object or operations/entities arrays";
  return std::nullopt;
}

std::optional<QJsonObject> irFromFileParams(
  const QJsonObject& params, QString& error, QJsonArray* warnings = nullptr)
{
  const auto path = params.value("path").toString().trimmed();
  if (path.isEmpty())
  {
    error = "file-based IR requires path";
    return std::nullopt;
  }
  const auto parsed = automation::parseAutomationIrFile(path);
  if (!parsed.ir)
  {
    error = parsed.error;
    return std::nullopt;
  }
  auto ir = *parsed.ir;
  auto parsedWarnings = parsed.warnings;
  if (params.value("qualityPolicy").isObject())
  {
    ir.insert("qualityPolicy", params.value("qualityPolicy"));
  }
  if (params.contains("applyMode"))
  {
    ir.insert("applyMode", params.value("applyMode"));
  }
  if (params.contains("requireMaterialAvailable"))
  {
    ir.insert("requireMaterialAvailable", params.value("requireMaterialAvailable"));
  }
  if (!validateIrShape(ir, error, warnings))
  {
    return std::nullopt;
  }
  if (warnings != nullptr)
  {
    for (const auto& warning : parsedWarnings)
    {
      warnings->push_back(warning);
    }
  }
  return ir;
}

std::optional<QByteArray> irFileBytesFromPath(const QString& path, QString& error)
{
  const auto info = QFileInfo{path};
  if (!info.isAbsolute())
  {
    error = "file-based IR path must be absolute";
    return std::nullopt;
  }
  if (!info.isFile() || !info.isReadable())
  {
    error = QString{"IR file is not readable: %1"}.arg(path);
    return std::nullopt;
  }
  if (info.size() > 10 * 1024 * 1024)
  {
    error = "IR file is too large; maximum size is 10 MiB";
    return std::nullopt;
  }

  auto file = QFile{path};
  if (!file.open(QIODevice::ReadOnly))
  {
    error = QString{"Could not open IR file: %1"}.arg(path);
    return std::nullopt;
  }
  return file.readAll();
}

QString hashIrFileBytes(const QByteArray& bytes)
{
  return QString{"sha256:%1"}.arg(QString::fromLatin1(
    QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()));
}

QString canonicalIrFilePath(const QString& path)
{
  const auto info = QFileInfo{path};
  return info.exists() ? info.canonicalFilePath() : info.absoluteFilePath();
}

void pruneExpiredIrPreviewCache(
  std::map<QString, McpIrPreviewCacheRecord>& previewCache, const qint64 nowMs)
{
  for (auto it = previewCache.begin(); it != previewCache.end();)
  {
    if (it->second.expiresAtMs <= nowMs)
    {
      it = previewCache.erase(it);
    }
    else
    {
      ++it;
    }
  }
}

void attachIrPreviewCacheRecord(
  QJsonObject& preview,
  mdl::Map& map,
  const QString& path,
  std::map<QString, McpIrPreviewCacheRecord>* previewCache,
  int* nextPreviewIndex)
{
  if (
    !preview.value("valid").toBool(false) || previewCache == nullptr
    || nextPreviewIndex == nullptr)
  {
    preview.insert("cacheable", false);
    return;
  }

  auto error = QString{};
  const auto bytes = irFileBytesFromPath(path, error);
  if (!bytes)
  {
    preview.insert("cacheable", false);
    auto warnings = preview.value("warnings").toArray();
    warnings.push_back(QString{"irPreviewCacheSkipped: %1"}.arg(error));
    preview.insert("warnings", warnings);
    return;
  }

  const auto nowMs = QDateTime::currentMSecsSinceEpoch();
  pruneExpiredIrPreviewCache(*previewCache, nowMs);

  const auto previewId = QString{"ir-preview-%1"}.arg((*nextPreviewIndex)++);
  auto record = McpIrPreviewCacheRecord{};
  record.previewId = previewId;
  record.sourcePath = canonicalIrFilePath(path);
  record.irHash = hashIrFileBytes(*bytes);
  record.documentFingerprint = documentFingerprintForMap(map);
  record.activeDocumentPath = QString::fromStdString(map.path().string());
  record.createdAtMs = nowMs;
  record.expiresAtMs = nowMs + McpSessionState::IrPreviewTtlMs;

  preview.insert("cacheable", true);
  preview.insert("previewId", previewId);
  preview.insert("irHash", record.irHash);
  preview.insert("documentFingerprint", record.documentFingerprint);
  preview.insert("activeDocumentPath", record.activeDocumentPath);
  preview.insert("createdAtMs", QString::number(record.createdAtMs));
  preview.insert("expiresAtMs", QString::number(record.expiresAtMs));
  preview.insert(
    "expiresAfterSeconds", static_cast<int>(McpSessionState::IrPreviewTtlMs / 1000));
  record.preview = preview;
  previewCache->insert_or_assign(previewId, record);
}

McpBridgeToolResult cachedIrApplyParams(
  mdl::Map& map,
  const QJsonObject& params,
  std::map<QString, McpIrPreviewCacheRecord>& previewCache)
{
  const auto previewId = params.value("previewId").toString().trimmed();
  if (previewId.isEmpty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      "ir_apply_from_file requires path or previewId",
      QJsonObject{
        {"mutatedDocument", false},
        {"retrySafe", true},
        {"recoveryAction", "provide_path_or_preview_id_then_retry"},
      });
  }

  const auto nowMs = QDateTime::currentMSecsSinceEpoch();
  pruneExpiredIrPreviewCache(previewCache, nowMs);
  const auto it = previewCache.find(previewId);
  if (it == previewCache.end())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      QString{"Unknown or expired IR previewId: %1"}.arg(previewId),
      QJsonObject{
        {"previewId", previewId},
        {"mutatedDocument", false},
        {"retrySafe", true},
        {"recoveryAction", "run_ir_compile_preview_from_file_again"},
      });
  }

  const auto& record = it->second;
  const auto currentFingerprint = documentFingerprintForMap(map);
  if (record.documentFingerprint != currentFingerprint)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      "IR preview belongs to a different active document",
      QJsonObject{
        {"previewId", previewId},
        {"cachedDocumentFingerprint", record.documentFingerprint},
        {"currentDocumentFingerprint", currentFingerprint},
        {"cachedActiveDocumentPath", record.activeDocumentPath},
        {"mutatedDocument", false},
        {"retrySafe", true},
        {"recoveryAction", "activate_original_document_or_preview_again"},
      });
  }

  auto error = QString{};
  const auto bytes = irFileBytesFromPath(record.sourcePath, error);
  if (!bytes)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      QString{"Cached IR file is no longer readable: %1"}.arg(error),
      QJsonObject{
        {"previewId", previewId},
        {"sourcePath", record.sourcePath},
        {"mutatedDocument", false},
        {"retrySafe", true},
        {"recoveryAction", "restore_ir_file_or_preview_again"},
      });
  }

  const auto currentHash = hashIrFileBytes(*bytes);
  if (currentHash != record.irHash)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      "Cached IR file changed after preview",
      QJsonObject{
        {"previewId", previewId},
        {"sourcePath", record.sourcePath},
        {"cachedIrHash", record.irHash},
        {"currentIrHash", currentHash},
        {"mutatedDocument", false},
        {"retrySafe", true},
        {"recoveryAction", "preview_changed_ir_file_again"},
      });
  }

  auto applyParams = params;
  applyParams.insert("path", record.sourcePath);
  applyParams.insert("previewId", previewId);
  applyParams.insert("irHash", record.irHash);
  applyParams.insert("expectedIrHash", record.irHash);
  if (record.preview.value("applyMode").toString() == "replace_module")
  {
    applyParams.insert("applyMode", "replace_module");
    applyParams.insert(
      "expectedTargetModuleRevision", record.preview.value("targetModuleRevision"));
    applyParams.insert(
      "expectedTargetModuleContentHash", record.preview.value("targetModuleContentHash"));
    applyParams.insert(
      "expectedTargetCanonicalObjectIds",
      record.preview.value("targetCanonicalObjectIds"));
  }
  if (
    !applyParams.contains("qualityPolicy")
    && record.preview.value("qualityPolicy").isObject())
  {
    applyParams.insert("qualityPolicy", record.preview.value("qualityPolicy"));
  }
  return McpBridgeToolResult::success(applyParams);
}

McpBridgeToolResult irApplyPreMutationFailure(
  const QString& message, QString recoveryAction, QJsonObject details = {})
{
  details.insert("mutatedDocument", false);
  details.insert("partialMutation", false);
  details.insert("retrySafe", true);
  if (!details.contains("failureStage"))
  {
    details.insert("failureStage", "validation");
  }
  details.insert("recoveryAction", std::move(recoveryAction));
  return McpBridgeToolResult::failure(
    mcp::McpErrorCode::InvalidParams, message, std::move(details));
}

McpBridgeToolResult irApplyFromFileMissingTargetFailure()
{
  return irApplyPreMutationFailure(
    "ir_apply_from_file requires path or previewId",
    "provide_path_or_preview_id_then_retry");
}

QJsonObject mergeObjects(QJsonObject base, const QJsonObject& overlay)
{
  for (auto it = overlay.begin(); it != overlay.end(); ++it)
  {
    base.insert(it.key(), it.value());
  }
  return base;
}

int repeatGridCountEstimate(const QJsonValue& counts)
{
  if (counts.isDouble())
  {
    return std::max(1, counts.toInt(1));
  }
  if (!counts.isArray())
  {
    return 1;
  }
  auto count = 1;
  for (const auto& item : counts.toArray())
  {
    count *= std::max(1, item.toInt(1));
  }
  return count;
}

QStringList projectedOperationParts(const QJsonObject& operation)
{
  auto parts = QStringList{};
  const auto append = [&](const QString& part) {
    if (!part.isEmpty())
    {
      parts.push_back(part);
    }
  };

  append(operation.value("metadata").toObject().value("part").toString());
  append(operation.value("part").toString());
  if (operation.value("parts").isArray())
  {
    for (const auto& part : operation.value("parts").toArray())
    {
      append(part.toString());
    }
  }

  const auto type = operation.value("type").toString();
  if (type == "repeat_translate" || type == "repeat_grid")
  {
    const auto child = operation.value("operation").toObject();
    const auto childParts = projectedOperationParts(child);
    const auto count = type == "repeat_translate"
                         ? std::max(1, operation.value("count").toInt(1))
                         : repeatGridCountEstimate(operation.value("counts"));
    for (auto i = 0; i < count; ++i)
    {
      parts.append(childParts);
    }
  }
  else if (type == "arc_ramp" || type == "helical_ramp")
  {
    const auto count = std::max(1, operation.value("segments").toInt(12));
    const auto part =
      operation.value("metadata").toObject().value("part").toString("ramp");
    for (auto i = 0; i < count; ++i)
    {
      append(part);
    }
  }
  return parts;
}

QJsonObject irPreviewJson(const QJsonObject& ir)
{
  auto result = automation::previewAutomationIr(ir);
  const auto operations = ir.value("operations").toArray();
  const auto entities = ir.value("entities").toArray();
  auto parts = QJsonArray{};
  auto warnings = QJsonArray{};
  auto estimatedBrushCount = 0;
  for (const auto& value : operations)
  {
    if (!value.isObject())
    {
      warnings.push_back("operations contains non-object entry");
      continue;
    }
    const auto operation = value.toObject();
    const auto type = operation.value("type").toString();
    for (const auto& part : projectedOperationParts(operation))
    {
      parts.push_back(part);
    }
    if (type == "curved_corridor")
    {
      const auto segments = std::max(1, operation.value("segments").toInt(12));
      estimatedBrushCount += segments * 4;
      const auto caps = operation.value("caps").toString("none").toLower();
      if (caps == "start" || caps == "end")
      {
        ++estimatedBrushCount;
      }
      else if (caps == "both")
      {
        estimatedBrushCount += 2;
      }
    }
    else if (type == "path_ribbon")
    {
      const auto points = operation.value("points3d").isArray()
                            ? operation.value("points3d").toArray()
                            : operation.value("points2d").toArray();
      estimatedBrushCount += std::max(0, static_cast<int>(points.size()) - 1);
    }
    else if (type == "arc_ramp" || type == "helical_ramp")
    {
      estimatedBrushCount += std::max(1, operation.value("segments").toInt(12));
    }
    else if (type == "repeat_translate")
    {
      estimatedBrushCount += std::max(1, operation.value("count").toInt(1));
    }
    else if (type == "repeat_grid")
    {
      estimatedBrushCount += repeatGridCountEstimate(operation.value("counts"));
    }
    else
    {
      ++estimatedBrushCount;
    }
  }

  auto qualityError = QString{};
  const auto qualityPolicy = qualityPolicyFromJson(ir, qualityError);
  if (!qualityPolicy)
  {
    warnings.push_back(qualityError);
  }

  result.insert("valid", warnings.isEmpty());
  result.insert("estimatedBrushCount", estimatedBrushCount);
  result.insert("estimatedObjectCount", estimatedBrushCount + entities.size());
  result.insert("parts", parts);
  result.insert("warnings", warnings);
  if (qualityPolicy)
  {
    result.insert("qualityPolicy", qualityPolicyJson(*qualityPolicy));
  }
  return result;
}

void appendIrWarnings(QJsonObject& result, const QJsonArray& compatibilityWarnings)
{
  if (compatibilityWarnings.isEmpty())
  {
    return;
  }
  auto warnings = result.value("warnings").toArray();
  for (const auto& warning : compatibilityWarnings)
  {
    warnings.push_back(warning);
  }
  result.insert("warnings", warnings);
}

QJsonObject irPreviewJsonForMap(mdl::Map& map, const QJsonObject& ir)
{
  auto preview = irPreviewJson(ir);
  if (const auto operations = ir.value("operations"); operations.isArray())
  {
    const auto defaultMetadata = ir.value("defaultMetadata").isObject()
                                   ? ir.value("defaultMetadata").toObject()
                                   : QJsonObject{};
    const auto moduleId =
      ir.value("moduleId").toString(defaultMetadata.value("moduleId").toString());
    auto mergedDefaultMetadata = defaultMetadata;
    if (!moduleId.isEmpty() && !mergedDefaultMetadata.contains("moduleId"))
    {
      mergedDefaultMetadata.insert("moduleId", moduleId);
    }

    auto batchParams = QJsonObject{
      {"operations", operations.toArray()},
      {"detail", "summary"},
    };
    if (ir.contains("grid"))
    {
      batchParams.insert("grid", ir.value("grid"));
    }
    if (ir.contains("material"))
    {
      batchParams.insert("material", ir.value("material"));
    }
    if (ir.contains("qualityPolicy"))
    {
      batchParams.insert("qualityPolicy", ir.value("qualityPolicy"));
    }
    if (ir.contains("requireMaterialAvailable"))
    {
      batchParams.insert(
        "requireMaterialAvailable", ir.value("requireMaterialAvailable"));
    }
    if (!mergedDefaultMetadata.isEmpty())
    {
      batchParams.insert("defaultMetadata", mergedDefaultMetadata);
    }

    const auto blockoutPreview = blockoutCompilePreviewForMapResult(map, batchParams);
    if (blockoutPreview.ok)
    {
      preview.insert("blockoutPreview", blockoutPreview.result);
      preview.insert(
        "valid",
        preview.value("valid").toBool()
          && blockoutPreview.result.value("valid").toBool());
      preview.insert(
        "estimatedBrushCount",
        blockoutPreview.result.value("estimatedBrushCount")
          .toInt(preview.value("estimatedBrushCount").toInt()));
      for (const auto& key :
           {"qualityPolicy", "curveQuality", "qualityStatus", "acceptancePassed"})
      {
        if (blockoutPreview.result.contains(key))
        {
          preview.insert(key, blockoutPreview.result.value(key));
        }
      }
      if (blockoutPreview.result.contains("bounds"))
      {
        preview.insert("bounds", blockoutPreview.result.value("bounds"));
      }
      auto warnings = preview.value("warnings").toArray();
      for (const auto& error : blockoutPreview.result.value("errors").toArray())
      {
        warnings.push_back(error);
      }
      preview.insert("warnings", warnings);
    }
    else
    {
      preview.insert("valid", false);
      auto warnings = preview.value("warnings").toArray();
      warnings.push_back(blockoutPreview.error.message);
      preview.insert("warnings", warnings);
    }
  }
  preview.insert(
    "estimatedObjectCount",
    preview.value("estimatedBrushCount").toInt() + preview.value("entityCount").toInt());
  return preview;
}

void mergeModuleFromOperationResult(
  const QJsonObject& result,
  const QString& documentFingerprint,
  const QJsonObject& defaultMetadata,
  const QJsonObject& qualityPolicy,
  std::map<QString, McpModuleRecord>& moduleStore)
{
  const auto moduleId = defaultMetadata.value("moduleId").toString().trimmed();
  if (moduleId.isEmpty())
  {
    return;
  }
  auto& module = moduleStore[moduleStoreKey(documentFingerprint, moduleId)];
  module.moduleId = moduleId;
  module.documentFingerprint = documentFingerprint;
  module.metadata = mergeObjects(module.metadata, moduleLevelMetadata(defaultMetadata));
  if (!qualityPolicy.isEmpty())
  {
    module.qualityPolicy = qualityPolicy;
  }
  const auto operationId = result.value("operationId").toString().trimmed();
  if (!operationId.isEmpty())
  {
    module.operationIds.push_back(operationId);
    module.operationIds.removeDuplicates();
  }
  for (const auto& value : result.value("changedObjectIds").toArray())
  {
    if (value.isString())
    {
      module.objectIds.push_back(value.toString());
    }
  }
  module.objectIds.removeDuplicates();
}

} // namespace

void reconcileMcpSessionForMap(
  mdl::Map& map,
  std::map<QString, McpBrushMetadataRecord>& metadataStore,
  std::map<QString, McpModuleRecord>& moduleStore,
  McpObjectRegistry& objectRegistry,
  const QString& activeOperationId,
  const bool allowLegacyPathRebind)
{
  const auto documentFingerprint = documentFingerprintForMap(map, &objectRegistry);
  auto reconciledMetadata = std::map<QString, McpBrushMetadataRecord>{};
  for (const auto& [storedObjectId, storedRecord] : metadataStore)
  {
    auto record = storedRecord;
    if (!recordMatchesDocument(record, documentFingerprint))
    {
      reconciledMetadata.insert_or_assign(storedObjectId, std::move(record));
      continue;
    }

    auto* node = static_cast<mdl::Node*>(nullptr);
    const auto resolved = objectRegistry.resolveExternalId(map, record.objectId);
    if (resolved.ok && resolved.diagnostic.value("live").toBool(true))
    {
      if (const auto path = McpObjectRegistry::parseLegacyObjectId(resolved.legacyPathId))
      {
        node = map.worldNode().resolvePath(*path);
      }
    }
    else if (allowLegacyPathRebind && !resolved.legacyPathId.isEmpty())
    {
      if (const auto path = McpObjectRegistry::parseLegacyObjectId(resolved.legacyPathId))
      {
        node = map.worldNode().resolvePath(*path);
      }
    }

    if (node == nullptr)
    {
      record.stale = true;
      reconciledMetadata.insert_or_assign(storedObjectId, std::move(record));
      continue;
    }

    record.objectId = objectRegistry.registerNode(map, *node);
    record.documentFingerprint = documentFingerprint;
    record.stale = false;
    const auto key = metadataStoreKey(documentFingerprint, record.objectId);
    if (auto it = reconciledMetadata.find(key); it != reconciledMetadata.end())
    {
      for (auto metadataIt = record.metadata.begin(); metadataIt != record.metadata.end();
           ++metadataIt)
      {
        if (!it->second.metadata.contains(metadataIt.key()))
        {
          it->second.metadata.insert(metadataIt.key(), metadataIt.value());
        }
      }
    }
    else
    {
      reconciledMetadata.emplace(key, std::move(record));
    }
  }
  metadataStore = std::move(reconciledMetadata);

  for (auto& [storedModuleId, module] : moduleStore)
  {
    Q_UNUSED(storedModuleId);
    if (!recordMatchesDocument(module, documentFingerprint))
    {
      continue;
    }

    auto candidateIds = module.objectIds;
    candidateIds.append(
      moduleObjectIdsFromMetadata(module.moduleId, documentFingerprint, metadataStore));
    auto nodes = std::vector<mdl::Node*>{};
    for (const auto& objectId : candidateIds)
    {
      auto warnings = QJsonArray{};
      if (auto* node = resolveObjectId(map, objectId, objectRegistry, warnings);
          node != nullptr && !nodeInVector(nodes, *node))
      {
        nodes.push_back(node);
      }
    }

    auto canonicalObjectIds = QStringList{};
    canonicalObjectIds.reserve(static_cast<qsizetype>(nodes.size()));
    for (auto* node : nodes)
    {
      canonicalObjectIds.push_back(objectRegistry.registerNode(map, *node));
    }
    canonicalObjectIds.removeDuplicates();
    canonicalObjectIds.sort(Qt::CaseInsensitive);

    const auto newContentHash = moduleContentHash(map, nodes, objectRegistry);
    const auto contentChanged =
      module.contentHash.isEmpty() || module.contentHash != newContentHash;
    if (contentChanged)
    {
      module.revision = module.revision <= 0 ? 1 : module.revision + 1;
      if (!activeOperationId.isEmpty())
      {
        module.activeOperationId = activeOperationId;
      }
    }
    module.documentFingerprint = documentFingerprint;
    module.objectIds = std::move(canonicalObjectIds);
    module.contentHash = newContentHash;
  }
}

void attachMcpSessionDelta(
  std::vector<McpOperationRecord>& history,
  const QString& operationId,
  const std::map<QString, McpBrushMetadataRecord>& metadataBefore,
  const std::map<QString, McpModuleRecord>& modulesBefore,
  const std::map<QString, McpBrushMetadataRecord>& metadataAfter,
  const std::map<QString, McpModuleRecord>& modulesAfter)
{
  const auto it = std::ranges::find_if(
    history, [&](const auto& operation) { return operation.operationId == operationId; });
  if (it == history.end())
  {
    return;
  }
  it->sessionBeforeJson =
    sessionDeltaJson(metadataBefore, modulesBefore, metadataAfter, modulesAfter, true);
  it->sessionAfterJson =
    sessionDeltaJson(metadataBefore, modulesBefore, metadataAfter, modulesAfter, false);
}

void restoreMcpSessionDelta(
  const McpOperationRecord& operation,
  const bool before,
  std::map<QString, McpBrushMetadataRecord>& metadataStore,
  std::map<QString, McpModuleRecord>& moduleStore)
{
  const auto bytes = before ? operation.sessionBeforeJson : operation.sessionAfterJson;
  const auto document = QJsonDocument::fromJson(bytes);
  if (!document.isObject())
  {
    return;
  }
  const auto root = document.object();
  const auto metadata = root.value("metadata").toObject();
  for (auto it = metadata.begin(); it != metadata.end(); ++it)
  {
    if (it.value().isNull())
    {
      metadataStore.erase(it.key());
    }
    else if (it.value().isObject())
    {
      metadataStore.insert_or_assign(
        it.key(), metadataRecordFromJson(it.value().toObject()));
    }
  }
  const auto modules = root.value("modules").toObject();
  for (auto it = modules.begin(); it != modules.end(); ++it)
  {
    if (it.value().isNull())
    {
      moduleStore.erase(it.key());
    }
    else if (it.value().isObject())
    {
      moduleStore.insert_or_assign(it.key(), moduleRecordFromJson(it.value().toObject()));
    }
  }
}

QJsonObject selectorFromParams(const QJsonObject& params)
{
  return selectorFromParamsInternal(params);
}

std::vector<mdl::Node*> resolveSelectorNodes(
  mdl::Map& map,
  const QJsonObject& selector,
  const std::vector<McpOperationRecord>& history,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry,
  QJsonArray& warnings,
  QString& error,
  McpSelectorDiagnostics* diagnostics)
{
  auto internalDiagnostics = SelectorDiagnosticsInternal{};
  auto nodes = resolveSelectorNodesInternal(
    map,
    selector,
    history,
    metadataStore,
    moduleStore,
    objectRegistry,
    warnings,
    error,
    diagnostics != nullptr ? &internalDiagnostics : nullptr);
  if (diagnostics != nullptr)
  {
    diagnostics->matchedBeforeLimit = internalDiagnostics.matchedBeforeLimit;
    diagnostics->limitApplied = internalDiagnostics.limitApplied;
    diagnostics->staleExcluded = internalDiagnostics.staleExcluded;
    diagnostics->moduleObjectIdCount = internalDiagnostics.moduleObjectIdCount;
    diagnostics->operationObjectIdCount = internalDiagnostics.operationObjectIdCount;
    diagnostics->metadataRecordCount = internalDiagnostics.metadataRecordCount;
  }
  return nodes;
}

McpBridgeToolResult selectorPreviewResult(
  AppController& appController,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (mapWindow == nullptr)
  {
    return noActiveDocumentFailure();
  }

  return selectorPreviewForMapResult(
    mapWindow->document().map(),
    params,
    history,
    metadataStore,
    moduleStore,
    objectRegistry);
}

McpBridgeToolResult selectorPreviewForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry)
{
  auto warnings = QJsonArray{};
  auto error = QString{};
  const auto selector = selectorFromParams(params);
  auto diagnostics = McpSelectorDiagnostics{};
  const auto nodes = resolveSelectorNodes(
    map,
    selector,
    history,
    metadataStore,
    moduleStore,
    objectRegistry,
    warnings,
    error,
    &diagnostics);
  if (!error.isEmpty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      error,
      QJsonObject{
        {"mutatedDocument", false},
        {"retrySafe", true},
        {"selector", selector},
        {"recoveryAction", "fix_selector_then_retry"},
      });
  }

  auto result = compactTargetResult(
    map,
    nodes,
    selector,
    metadataStore,
    objectRegistry,
    warnings,
    params.value("idsMode").toString("sample"),
    params.value("detail").toString("summary").trimmed().toLower() == "full");
  result.insert("tool", "selector_preview");
  result.insert("mutatedDocument", false);
  result.insert("matchedBeforeLimit", diagnostics.matchedBeforeLimit);
  result.insert("limitApplied", diagnostics.limitApplied);
  result.insert("staleExcluded", diagnostics.staleExcluded);
  result.insert("moduleObjectIdCount", diagnostics.moduleObjectIdCount);
  result.insert("operationObjectIdCount", diagnostics.operationObjectIdCount);
  result.insert("metadataRecordCount", diagnostics.metadataRecordCount);
  return McpBridgeToolResult::success(result);
}

McpBridgeToolResult objectsSelectBySelectorResult(
  AppController& appController,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (mapWindow == nullptr)
  {
    return noActiveDocumentFailure();
  }

  return objectsSelectBySelectorForMapResult(
    mapWindow->document().map(),
    params,
    history,
    metadataStore,
    moduleStore,
    objectRegistry);
}

McpBridgeToolResult objectsSelectBySelectorForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry)
{
  auto warnings = QJsonArray{};
  auto error = QString{};
  const auto selector = selectorFromParams(params);
  auto nodes = resolveSelectorNodes(
    map, selector, history, metadataStore, moduleStore, objectRegistry, warnings, error);
  if (!error.isEmpty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      error,
      QJsonObject{
        {"mutatedDocument", false},
        {"retrySafe", true},
        {"selector", selector},
        {"recoveryAction", "fix_selector_then_retry"},
      });
  }
  nodes.erase(
    std::remove_if(
      nodes.begin(),
      nodes.end(),
      [&](const auto* node) {
        return node == nullptr || !map.editorContext().selectable(*node);
      }),
    nodes.end());

  mdl::deselectAll(map);
  if (!nodes.empty())
  {
    mdl::selectNodes(map, nodes);
  }

  auto result = compactTargetResult(
    map,
    nodes,
    selector,
    metadataStore,
    objectRegistry,
    warnings,
    params.value("idsMode").toString("sample"));
  result.insert("tool", "objects_select_by_selector");
  result.insert("mutatedDocument", false);
  result.insert("selectedCount", static_cast<int>(nodes.size()));
  return McpBridgeToolResult::success(result);
}

McpBridgeToolResult objectsDeleteBySelectorResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  std::map<QString, McpBrushMetadataRecord>& metadataStore,
  std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (mapWindow == nullptr)
  {
    return noActiveDocumentFailure();
  }

  return objectsDeleteBySelectorForMapResult(
    mapWindow->document().map(),
    toolName,
    params,
    history,
    nextOperationIndex,
    metadataStore,
    moduleStore,
    objectRegistry);
}

McpBridgeToolResult objectsDeleteBySelectorForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  std::map<QString, McpBrushMetadataRecord>& metadataStore,
  std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry)
{
  auto warnings = QJsonArray{};
  auto error = QString{};
  const auto selector = selectorFromParams(params);
  auto nodes = resolveSelectorNodes(
    map, selector, history, metadataStore, moduleStore, objectRegistry, warnings, error);
  if (!error.isEmpty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      error,
      QJsonObject{
        {"mutatedDocument", false},
        {"retrySafe", true},
        {"selector", selector},
        {"recoveryAction", "fix_selector_then_retry"},
      });
  }
  nodes.erase(
    std::remove_if(
      nodes.begin(),
      nodes.end(),
      [&](const auto* node) { return node == nullptr || node == &map.worldNode(); }),
    nodes.end());
  if (nodes.empty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      "objects_delete_by_selector matched no deletable objects",
      QJsonObject{
        {"mutatedDocument", false},
        {"retrySafe", true},
        {"selector", selector},
        {"matchedCount", 0},
        {"recoveryAction", "preview_selector_or_refresh_status"},
      });
  }

  const auto transactionName =
    params.value("transactionName").toString("MCP: Delete objects by selector");
  auto deletedIds = nodeIdsJson(map, nodes, objectRegistry);
  auto deletedIdentityIds = deletedIds;
  for (const auto* node : nodes)
  {
    if (node != nullptr)
    {
      deletedIdentityIds.push_back(mcpNodePathId(*node, map.worldNode()));
    }
  }
  const auto changedObjectIds = removeNodesWithTransaction(
    map,
    transactionName.isEmpty() ? QString{"MCP: Delete objects by selector"}
                              : transactionName,
    nodes);
  if (!changedObjectIds)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::Forbidden,
      "Matched selector objects cannot be deleted",
      QJsonObject{
        {"mutatedDocument", false},
        {"retrySafe", true},
        {"selector", selector},
        {"matchedCount", deletedIds.size()},
        {"recoveryAction", "preview_selector_or_use_user_selection"},
      });
  }

  markDeletedMetadata(
    deletedIdentityIds, map, objectRegistry, metadataStore, moduleStore);
  auto result = QJsonObject{};
  recordDeleteOperation(
    history,
    nextOperationIndex,
    map,
    toolName,
    transactionName.isEmpty() ? QString{"MCP: Delete objects by selector"}
                              : transactionName,
    deletedIds,
    result);
  result.insert("mutatedDocument", true);
  result.insert("matchedCount", deletedIds.size());
  result.insert("selector", selector);
  result.insert("warnings", warnings);
  return McpBridgeToolResult::success(result);
}

McpBridgeToolResult renderReviewSelectorResult(
  AppController& appController,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (mapWindow == nullptr)
  {
    return noActiveDocumentFailure();
  }

  return renderReviewSelectorForMapResult(
    mapWindow->document().map(),
    params,
    history,
    metadataStore,
    moduleStore,
    objectRegistry);
}

McpBridgeToolResult renderReviewSelectorForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry)
{
  auto warnings = QJsonArray{};
  auto error = QString{};
  const auto selector = selectorFromParams(params);
  auto diagnostics = McpSelectorDiagnostics{};
  const auto nodes = resolveSelectorNodes(
    map,
    selector,
    history,
    metadataStore,
    moduleStore,
    objectRegistry,
    warnings,
    error,
    &diagnostics);
  if (!error.isEmpty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      error,
      QJsonObject{
        {"mutatedDocument", false},
        {"retrySafe", true},
        {"selector", selector},
        {"recoveryAction", "fix_selector_then_retry"},
      });
  }
  auto objectIds = nodeIdsJson(map, nodes, objectRegistry);
  auto reviewParams = params;
  reviewParams.insert("objectIds", objectIds);
  reviewParams.insert("detail", params.value("detail").toString("summary"));
  auto result = renderReviewTargetsForMapResult(
    map, reviewParams, history, &objectRegistry, &metadataStore);
  if (result.ok)
  {
    result.result.insert("selector", selector);
    result.result.insert("selectorMatchedCount", objectIds.size());
    auto resultWarnings = result.result.value("warnings").toArray();
    for (const auto& warning : warnings)
    {
      resultWarnings.push_back(warning);
    }
    result.result.insert("warnings", resultWarnings);
    result.result.insert("matchedBeforeLimit", diagnostics.matchedBeforeLimit);
    result.result.insert("limitApplied", diagnostics.limitApplied);
    result.result.insert("staleExcluded", diagnostics.staleExcluded);
    result.result.insert("moduleObjectIdCount", diagnostics.moduleObjectIdCount);
    result.result.insert("operationObjectIdCount", diagnostics.operationObjectIdCount);
    result.result.insert("metadataRecordCount", diagnostics.metadataRecordCount);
  }
  return result;
}

McpBridgeToolResult moduleListResult(
  AppController& appController,
  const QJsonObject& params,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (mapWindow == nullptr)
  {
    return noActiveDocumentFailure();
  }
  return moduleListForMapResult(
    mapWindow->document().map(), params, metadataStore, moduleStore, objectRegistry);
}

McpBridgeToolResult moduleListForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry)
{
  const auto documentFingerprint = documentFingerprintForMap(map);
  const auto limit = std::clamp(params.value("limit").toInt(100), 1, 1000);
  const auto includeStale = params.value("includeStale").toBool(false);
  const auto includeEmpty = params.value("includeEmpty").toBool(includeStale);
  auto modules = QJsonArray{};
  auto ids = allModuleIds(documentFingerprint, metadataStore, moduleStore);
  auto liveModuleCount = 0;
  auto staleModuleCount = 0;
  auto emptyModuleCount = 0;
  auto filteredStaleModuleCount = 0;
  auto filteredEmptyModuleCount = 0;
  for (const auto& id : ids)
  {
    const auto module =
      mergedModuleRecord(id, documentFingerprint, metadataStore, moduleStore);
    const auto summary = moduleSummary(map, module, metadataStore, objectRegistry);
    const auto objectCount = summary.value("objectCount").toInt();
    const auto liveObjectCount = summary.value("liveObjectCount").toInt();
    if (objectCount == 0)
    {
      ++emptyModuleCount;
      if (!includeEmpty)
      {
        ++filteredEmptyModuleCount;
        continue;
      }
    }
    else if (liveObjectCount == 0)
    {
      ++staleModuleCount;
      if (!includeStale)
      {
        ++filteredStaleModuleCount;
        continue;
      }
    }
    else
    {
      ++liveModuleCount;
    }
    if (modules.size() < limit)
    {
      modules.push_back(summary);
    }
  }
  return McpBridgeToolResult::success(QJsonObject{
    {"tool", "module_list"},
    {"moduleCount", ids.size()},
    {"liveModuleCount", liveModuleCount},
    {"staleModuleCount", staleModuleCount},
    {"emptyModuleCount", emptyModuleCount},
    {"filteredStaleModuleCount", filteredStaleModuleCount},
    {"filteredEmptyModuleCount", filteredEmptyModuleCount},
    {"includeStale", includeStale},
    {"includeEmpty", includeEmpty},
    {"returnedCount", modules.size()},
    {"modules", modules},
  });
}

McpBridgeToolResult moduleInspectResult(
  AppController& appController,
  const QJsonObject& params,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (mapWindow == nullptr)
  {
    return noActiveDocumentFailure();
  }
  return moduleInspectForMapResult(
    mapWindow->document().map(), params, metadataStore, moduleStore, objectRegistry);
}

McpBridgeToolResult moduleInspectForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry)
{
  const auto moduleId = params.value("moduleId").toString().trimmed();
  if (moduleId.isEmpty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      "module_inspect requires moduleId",
      QJsonObject{
        {"mutatedDocument", false},
        {"retrySafe", true},
        {"recoveryAction", "provide_module_id_then_retry"},
      });
  }
  const auto documentFingerprint = documentFingerprintForMap(map);
  auto module =
    mergedModuleRecord(moduleId, documentFingerprint, metadataStore, moduleStore);
  auto summary = moduleSummary(map, module, metadataStore, objectRegistry);
  summary.insert("tool", "module_inspect");
  summary.insert("mutatedDocument", false);
  const auto idsMode = params.value("idsMode").toString("sample").trimmed().toLower();
  summary.insert("objectIdCount", module.objectIds.size());
  if (idsMode == "full")
  {
    summary.insert("objectIds", stringsToJson(module.objectIds));
  }
  else if (idsMode == "sample" || idsMode.isEmpty())
  {
    auto sample = QStringList{};
    for (auto i = 0;
         i < std::min(DefaultSampleLimit, static_cast<int>(module.objectIds.size()));
         ++i)
    {
      sample.push_back(module.objectIds[i]);
    }
    summary.insert("objectIdSample", stringsToJson(sample));
  }
  else if (idsMode != "none" && idsMode != "count")
  {
    auto warnings = summary.value("warnings").toArray();
    warnings.push_back(
      QString{"unknownIdsMode: %1; returned objectIdCount only"}.arg(idsMode));
    summary.insert("warnings", warnings);
  }
  return McpBridgeToolResult::success(summary);
}

McpBridgeToolResult moduleSelectResult(
  AppController& appController,
  const QJsonObject& params,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry)
{
  const auto moduleId = params.value("moduleId").toString().trimmed();
  if (moduleId.isEmpty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      "module_select requires moduleId",
      QJsonObject{
        {"mutatedDocument", false},
        {"retrySafe", true},
        {"recoveryAction", "provide_module_id_then_retry"},
      });
  }
  auto selectorParams = params;
  auto selector = selectorFromParams(params);
  selector.insert("moduleId", moduleId);
  selectorParams.insert("selector", selector);
  return objectsSelectBySelectorResult(
    appController, selectorParams, {}, metadataStore, moduleStore, objectRegistry);
}

McpBridgeToolResult moduleRenderReviewResult(
  AppController& appController,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry)
{
  const auto moduleId = params.value("moduleId").toString().trimmed();
  if (moduleId.isEmpty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      "module_render_review requires moduleId",
      QJsonObject{
        {"mutatedDocument", false},
        {"retrySafe", true},
        {"recoveryAction", "provide_module_id_then_retry"},
      });
  }
  auto selectorParams = params;
  auto selector = selectorFromParams(params);
  selector.insert("moduleId", moduleId);
  if (!selector.contains("limit"))
  {
    selector.insert("limit", params.value("limit").toInt(10000));
  }
  selectorParams.insert("selector", selector);
  return renderReviewSelectorResult(
    appController, selectorParams, history, metadataStore, moduleStore, objectRegistry);
}

McpBridgeToolResult moduleValidateResult(
  AppController& appController,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (mapWindow == nullptr)
  {
    return noActiveDocumentFailure();
  }
  return moduleValidateForMapResult(
    mapWindow->document().map(),
    params,
    history,
    metadataStore,
    moduleStore,
    objectRegistry);
}

McpBridgeToolResult moduleValidateForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry)
{
  const auto moduleId = params.value("moduleId").toString().trimmed();
  if (moduleId.isEmpty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      "module_validate requires moduleId",
      QJsonObject{
        {"mutatedDocument", false},
        {"retrySafe", true},
        {"recoveryAction", "provide_module_id_then_retry"},
      });
  }
  const auto documentFingerprint = documentFingerprintForMap(map);
  auto module =
    mergedModuleRecord(moduleId, documentFingerprint, metadataStore, moduleStore);
  auto warnings = QJsonArray{};
  auto nodes = std::vector<mdl::Node*>{};
  auto staleCount = 0;
  auto accountedObjectIds = module.objectIds;
  accountedObjectIds.removeDuplicates();
  for (const auto& objectId : module.objectIds)
  {
    if (auto* node = resolveObjectId(map, objectId, objectRegistry, warnings))
    {
      nodes.push_back(node);
    }
    else
    {
      ++staleCount;
    }
  }

  for (const auto& [storedObjectId, record] : metadataStore)
  {
    Q_UNUSED(storedObjectId);
    if (
      !recordMatchesDocument(record, documentFingerprint)
      || record.metadata.value("moduleId").toString() != moduleId
      || accountedObjectIds.contains(record.objectId))
    {
      continue;
    }
    if (record.stale || !objectIdResolvesLive(map, record.objectId, objectRegistry))
    {
      accountedObjectIds.push_back(record.objectId);
      ++staleCount;
      warnings.push_back(QJsonObject{
        {"objectId", record.objectId},
        {"stale", true},
        {"staleReason", "module metadata target no longer resolves"},
      });
    }
  }

  auto result = QJsonObject{
    {"tool", "module_validate"},
    {"moduleId", moduleId},
    {"mutatedDocument", false},
    {"valid", staleCount == 0 && !nodes.empty()},
    {"objectCount", accountedObjectIds.size()},
    {"liveObjectCount", static_cast<int>(nodes.size())},
    {"staleObjectCount", staleCount},
    {"operationIds", stringsToJson(module.operationIds)},
    {"warnings",
     compactWarnings(
       warnings,
       params.value("detail").toString("summary").trimmed().toLower() == "full")},
  };
  if (!nodes.empty())
  {
    result.insert("bounds", boundsToJson(boundsForNodes(nodes)));
  }

  if (params.value("checkRouteContinuity").toBool(false))
  {
    auto continuityWarnings = QJsonArray{};
    auto continuityError = QString{};
    auto continuityNodes = std::vector<mdl::Node*>{};
    if (params.value("continuitySelector").isObject())
    {
      auto selector = params.value("continuitySelector").toObject();
      if (!selector.contains("moduleId"))
      {
        selector.insert("moduleId", moduleId);
      }
      continuityNodes = resolveSelectorNodes(
        map,
        selector,
        history,
        metadataStore,
        moduleStore,
        objectRegistry,
        continuityWarnings,
        continuityError);
      if (!continuityError.isEmpty())
      {
        return McpBridgeToolResult::failure(
          mcp::McpErrorCode::InvalidParams,
          continuityError,
          QJsonObject{
            {"mutatedDocument", false},
            {"retrySafe", true},
            {"recoveryAction", "fix_continuity_selector_then_retry"},
            {"moduleId", moduleId},
          });
      }
    }
    else
    {
      continuityNodes = defaultContinuityNodes(map, nodes, metadataStore, objectRegistry);
      if (continuityNodes.empty() && !nodes.empty())
      {
        continuityWarnings.push_back(
          "moduleValidateRouteContinuityUsedAllObjects: no walkable route "
          "metadata found; pass continuitySelector to avoid rails/supports/markers.");
        continuityNodes = nodes;
      }
    }

    for (const auto& warning : continuityWarnings)
    {
      warnings.push_back(warning);
    }
    result.insert("routeContinuityObjectCount", static_cast<int>(continuityNodes.size()));
    result.insert("warnings", warnings);

    auto continuityParams =
      QJsonObject{{"objectIds", nodeIdsJson(map, continuityNodes, objectRegistry)}};
    if (params.value("start").isArray())
    {
      continuityParams.insert("start", params.value("start"));
    }
    if (params.value("end").isArray())
    {
      continuityParams.insert("end", params.value("end"));
    }
    if (params.value("routeDirection").isArray())
    {
      continuityParams.insert("routeDirection", params.value("routeDirection"));
    }
    for (const auto& key :
         {"continuityMode",
          "maxStepHeight",
          "maxJumpGap",
          "routeMode",
          "validationMode",
          "verticalTolerance",
          "horizontalTolerance",
          "minUpNormal",
          "orderBy",
          "closedLoop",
          "detail"})
    {
      if (params.contains(key))
      {
        continuityParams.insert(key, params.value(key));
      }
    }
    if (params.contains("qualityPolicy"))
    {
      continuityParams.insert("qualityPolicy", params.value("qualityPolicy"));
    }
    const auto continuity = geometryAnalyzeRouteContinuityForMapResult(
      map, continuityParams, history, &objectRegistry, &metadataStore, &moduleStore);
    result.insert("routeContinuity", continuity.result);
    if (
      continuity.ok
      && !continuity.result.value("acceptancePassed")
            .toBool(continuity.result.value("semanticContinuous")
                      .toBool(continuity.result.value("continuous").toBool(false))))
    {
      result.insert("valid", false);
    }
  }
  return McpBridgeToolResult::success(result);
}

McpBridgeToolResult moduleCompactResult(
  AppController& appController,
  const QJsonObject& params,
  std::map<QString, McpBrushMetadataRecord>& metadataStore,
  std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (mapWindow == nullptr)
  {
    return noActiveDocumentFailure();
  }

  return moduleCompactForMapResult(
    mapWindow->document().map(), params, metadataStore, moduleStore, objectRegistry);
}

McpBridgeToolResult moduleCompactForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  std::map<QString, McpBrushMetadataRecord>& metadataStore,
  std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry)
{
  const auto moduleId = params.value("moduleId").toString().trimmed();
  if (moduleId.isEmpty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      "module_compact requires moduleId",
      QJsonObject{
        {"mutatedDocument", false},
        {"retrySafe", true},
        {"recoveryAction", "provide_module_id_then_retry"},
      });
  }
  const auto documentFingerprint = documentFingerprintForMap(map);
  auto warnings = QJsonArray{};
  auto removedMetadataCount = 0;
  for (auto it = metadataStore.begin(); it != metadataStore.end();)
  {
    const auto& record = it->second;
    if (
      recordMatchesDocument(record, documentFingerprint)
      && record.metadata.value("moduleId").toString() == moduleId
      && (record.stale || !objectIdResolvesLive(map, record.objectId, objectRegistry)))
    {
      it = metadataStore.erase(it);
      ++removedMetadataCount;
    }
    else
    {
      ++it;
    }
  }

  auto removedObjectIdCount = qsizetype{0};
  const auto key = moduleStoreKey(documentFingerprint, moduleId);
  auto moduleIt = moduleStore.find(key);
  if (moduleIt == moduleStore.end())
  {
    for (auto it = moduleStore.begin(); it != moduleStore.end(); ++it)
    {
      if (
        it->second.moduleId == moduleId
        && recordMatchesDocument(it->second, documentFingerprint))
      {
        moduleIt = it;
        break;
      }
    }
  }
  if (moduleIt != moduleStore.end())
  {
    auto& objectIds = moduleIt->second.objectIds;
    const auto before = objectIds.size();
    objectIds.erase(
      std::remove_if(
        objectIds.begin(),
        objectIds.end(),
        [&](const auto& objectId) {
          return resolveObjectId(map, objectId, objectRegistry, warnings) == nullptr;
        }),
      objectIds.end());
    removedObjectIdCount = before - objectIds.size();
  }

  const auto module =
    mergedModuleRecord(moduleId, documentFingerprint, metadataStore, moduleStore);
  auto summary = moduleSummary(map, module, metadataStore, objectRegistry);
  summary.insert("tool", "module_compact");
  summary.insert("removedStaleMetadataCount", removedMetadataCount);
  summary.insert("removedStaleObjectIdCount", removedObjectIdCount);
  summary.insert("mutatedDocument", false);
  summary.insert("warnings", warnings);
  return McpBridgeToolResult::success(summary);
}

McpBridgeToolResult irValidateResult(
  AppController& appController, const QJsonObject& params)
{
  auto error = QString{};
  auto compatibilityWarnings = QJsonArray{};
  const auto ir = irFromParams(params, error, &compatibilityWarnings);
  if (!ir)
  {
    return invalidParamsFailure(error);
  }
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  auto preview = mapWindow != nullptr
                   ? irPreviewJsonForMap(mapWindow->document().map(), *ir)
                   : irPreviewJson(*ir);
  if (mapWindow != nullptr)
  {
    enrichIrPreviewForApply(
      preview, mapWindow->document().map(), *ir, nullptr, nullptr, nullptr);
  }
  appendIrWarnings(preview, compatibilityWarnings);
  preview.insert("tool", "ir_validate");
  return McpBridgeToolResult::success(preview);
}

McpBridgeToolResult irCompilePreviewResult(
  AppController& appController,
  const QJsonObject& params,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry)
{
  auto error = QString{};
  auto compatibilityWarnings = QJsonArray{};
  const auto ir = irFromParams(params, error, &compatibilityWarnings);
  if (!ir)
  {
    return invalidParamsFailure(error);
  }
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  auto preview = mapWindow != nullptr
                   ? irPreviewJsonForMap(mapWindow->document().map(), *ir)
                   : irPreviewJson(*ir);
  if (mapWindow != nullptr)
  {
    enrichIrPreviewForApply(
      preview,
      mapWindow->document().map(),
      *ir,
      &metadataStore,
      &moduleStore,
      &objectRegistry);
  }
  appendIrWarnings(preview, compatibilityWarnings);
  preview.insert("tool", "ir_compile_preview");
  preview.insert("willCommit", false);
  return McpBridgeToolResult::success(preview);
}

McpBridgeToolResult irCompilePreviewResult(
  AppController& appController, const QJsonObject& params)
{
  auto error = QString{};
  auto compatibilityWarnings = QJsonArray{};
  const auto ir = irFromParams(params, error, &compatibilityWarnings);
  if (!ir)
  {
    return invalidParamsFailure(error);
  }
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  auto preview = mapWindow != nullptr
                   ? irPreviewJsonForMap(mapWindow->document().map(), *ir)
                   : irPreviewJson(*ir);
  if (mapWindow != nullptr)
  {
    enrichIrPreviewForApply(
      preview, mapWindow->document().map(), *ir, nullptr, nullptr, nullptr);
  }
  appendIrWarnings(preview, compatibilityWarnings);
  preview.insert("tool", "ir_compile_preview");
  preview.insert("willCommit", false);
  return McpBridgeToolResult::success(preview);
}

McpBridgeToolResult irCompilePreviewForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry)
{
  auto error = QString{};
  auto compatibilityWarnings = QJsonArray{};
  const auto ir = irFromParams(params, error, &compatibilityWarnings);
  if (!ir)
  {
    return invalidParamsFailure(error);
  }
  auto preview = irPreviewJsonForMap(map, *ir);
  enrichIrPreviewForApply(
    preview, map, *ir, &metadataStore, &moduleStore, &objectRegistry);
  appendIrWarnings(preview, compatibilityWarnings);
  preview.insert("tool", "ir_compile_preview");
  preview.insert("willCommit", false);
  return McpBridgeToolResult::success(preview);
}

McpBridgeToolResult irCompilePreviewFromFileResult(
  AppController& appController,
  const QJsonObject& params,
  std::map<QString, McpIrPreviewCacheRecord>* previewCache,
  int* nextPreviewIndex,
  const std::map<QString, McpBrushMetadataRecord>* metadataStore,
  const std::map<QString, McpModuleRecord>* moduleStore,
  const McpObjectRegistry* objectRegistry)
{
  auto error = QString{};
  auto compatibilityWarnings = QJsonArray{};
  const auto ir = irFromFileParams(params, error, &compatibilityWarnings);
  if (!ir)
  {
    return invalidParamsFailure(error);
  }
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  auto preview = mapWindow != nullptr
                   ? irPreviewJsonForMap(mapWindow->document().map(), *ir)
                   : irPreviewJson(*ir);
  appendIrWarnings(preview, compatibilityWarnings);
  preview.insert("tool", "ir_compile_preview_from_file");
  preview.insert("willCommit", false);
  preview.insert("sourcePath", canonicalIrFilePath(params.value("path").toString()));
  if (mapWindow != nullptr)
  {
    enrichIrPreviewForApply(
      preview,
      mapWindow->document().map(),
      *ir,
      metadataStore,
      moduleStore,
      objectRegistry);
    attachIrPreviewCacheRecord(
      preview,
      mapWindow->document().map(),
      params.value("path").toString(),
      previewCache,
      nextPreviewIndex);
  }
  else
  {
    preview.insert("cacheable", false);
  }
  return McpBridgeToolResult::success(preview);
}

McpBridgeToolResult irCompilePreviewFromFileForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  std::map<QString, McpIrPreviewCacheRecord>* previewCache,
  int* nextPreviewIndex,
  const std::map<QString, McpBrushMetadataRecord>* metadataStore,
  const std::map<QString, McpModuleRecord>* moduleStore,
  const McpObjectRegistry* objectRegistry)
{
  auto error = QString{};
  auto compatibilityWarnings = QJsonArray{};
  const auto ir = irFromFileParams(params, error, &compatibilityWarnings);
  if (!ir)
  {
    return invalidParamsFailure(error);
  }
  auto preview = irPreviewJsonForMap(map, *ir);
  enrichIrPreviewForApply(preview, map, *ir, metadataStore, moduleStore, objectRegistry);
  appendIrWarnings(preview, compatibilityWarnings);
  preview.insert("tool", "ir_compile_preview_from_file");
  preview.insert("willCommit", false);
  preview.insert("sourcePath", canonicalIrFilePath(params.value("path").toString()));
  attachIrPreviewCacheRecord(
    preview, map, params.value("path").toString(), previewCache, nextPreviewIndex);
  return McpBridgeToolResult::success(preview);
}

McpBridgeToolResult irApplyResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  std::map<QString, McpBrushMetadataRecord>& metadataStore,
  std::map<QString, McpModuleRecord>& moduleStore,
  McpObjectRegistry* objectRegistry)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (mapWindow == nullptr)
  {
    return noActiveDocumentFailure();
  }
  return irApplyForMapResult(
    mapWindow->document().map(),
    toolName,
    params,
    history,
    nextOperationIndex,
    metadataStore,
    moduleStore,
    objectRegistry);
}

McpBridgeToolResult irApplyForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  std::map<QString, McpBrushMetadataRecord>& metadataStore,
  std::map<QString, McpModuleRecord>& moduleStore,
  McpObjectRegistry* objectRegistry)
{
  auto error = QString{};
  auto compatibilityWarnings = params.value("_irCompatibilityWarnings").toArray();
  auto parsedWarnings = QJsonArray{};
  const auto ir = irFromParams(params, error, &parsedWarnings);
  if (!ir)
  {
    return irApplyPreMutationFailure(error, "fix_ir_payload_then_retry");
  }
  for (const auto& warning : parsedWarnings)
  {
    compatibilityWarnings.push_back(warning);
  }

  const auto rollbackFailure = [&](
                                 const mcp::McpErrorCode code,
                                 const QString& stage,
                                 const QString& message,
                                 const QString& recoveryAction,
                                 QJsonObject details = {}) {
    details.insert("failureStage", stage);
    details.insert("mutatedDocument", false);
    details.insert("partialMutation", false);
    details.insert("retrySafe", true);
    details.insert("recoveryAction", recoveryAction);
    return McpBridgeToolResult::failure(code, message, std::move(details));
  };

  auto stagedHistory = history;
  auto stagedNextOperationIndex = nextOperationIndex;
  auto stagedMetadataStore = metadataStore;
  auto stagedModuleStore = moduleStore;
  auto stagedObjectRegistry =
    objectRegistry != nullptr ? *objectRegistry : McpObjectRegistry{};
  auto* stagedObjectRegistryPtr =
    objectRegistry != nullptr ? &stagedObjectRegistry : nullptr;

  const auto documentFingerprint =
    documentFingerprintForMap(map, stagedObjectRegistryPtr);
  const auto defaultMetadata = ir->value("defaultMetadata").isObject()
                                 ? ir->value("defaultMetadata").toObject()
                                 : QJsonObject{};
  const auto moduleId =
    ir->value("moduleId").toString(defaultMetadata.value("moduleId").toString());
  const auto applyMode = ir->value("applyMode").toString("create");
  auto mergedDefaultMetadata = defaultMetadata;
  if (!moduleId.isEmpty() && !mergedDefaultMetadata.contains("moduleId"))
  {
    mergedDefaultMetadata.insert("moduleId", moduleId);
  }

  auto previousModuleRevision = 0;
  auto previousModuleContentHash = QString{};
  auto replacementNodes = std::vector<mdl::Node*>{};
  auto removedObjectIds = QStringList{};
  if (applyMode == "replace_module")
  {
    if (objectRegistry == nullptr)
    {
      return rollbackFailure(
        mcp::McpErrorCode::InternalError,
        "replacement_guard",
        "replace_module requires the MCP object registry",
        "refresh_status_then_preview_again");
    }
    if (moduleId.trimmed().isEmpty())
    {
      return rollbackFailure(
        mcp::McpErrorCode::InvalidParams,
        "replacement_guard",
        "replace_module requires IR moduleId",
        "add_module_id_then_preview_again");
    }
    reconcileMcpSessionForMap(
      map, stagedMetadataStore, stagedModuleStore, stagedObjectRegistry);
    const auto targetState = replacementTargetState(
      map, *ir, stagedMetadataStore, stagedModuleStore, stagedObjectRegistry);
    if (!targetState.value("targetModuleExists").toBool(false))
    {
      return rollbackFailure(
        mcp::McpErrorCode::InvalidParams,
        "replacement_guard",
        QString{"replace_module target does not exist: %1"}.arg(moduleId),
        "refresh_module_and_preview_again",
        QJsonObject{{"moduleId", moduleId}});
    }

    const auto actualIrHash = params.value("irHash").toString().trimmed().isEmpty()
                                ? canonicalIrHash(*ir)
                                : params.value("irHash").toString().trimmed();
    const auto expectedIrHash = params.value("expectedIrHash").toString().trimmed();
    const auto expectedContentHash =
      params.value("expectedTargetModuleContentHash").toString().trimmed();
    const auto expectedRevisionValue = params.value("expectedTargetModuleRevision");
    const auto expectedIdsValue = params.value("expectedTargetCanonicalObjectIds");
    if (
      expectedIrHash.isEmpty() || expectedContentHash.isEmpty()
      || !expectedRevisionValue.isDouble() || !expectedIdsValue.isArray())
    {
      return rollbackFailure(
        mcp::McpErrorCode::InvalidParams,
        "replacement_guard",
        "replace_module requires expectedIrHash, expectedTargetModuleRevision, "
        "expectedTargetModuleContentHash, and expectedTargetCanonicalObjectIds",
        "run_ir_compile_preview_again");
    }

    auto expectedIds = stringListFromArray(expectedIdsValue.toArray());
    for (auto& expectedId : expectedIds)
    {
      if (McpObjectRegistry::isLegacyObjectId(expectedId))
      {
        expectedId = stagedObjectRegistry.externalIdForLegacy(map, expectedId);
      }
    }
    auto actualIds =
      stringListFromArray(targetState.value("targetCanonicalObjectIds").toArray());
    expectedIds.removeDuplicates();
    expectedIds.sort(Qt::CaseInsensitive);
    actualIds.removeDuplicates();
    actualIds.sort(Qt::CaseInsensitive);
    const auto actualRevision = targetState.value("targetModuleRevision").toInt();
    const auto actualContentHash =
      targetState.value("targetModuleContentHash").toString();
    if (
      expectedIrHash != actualIrHash || expectedRevisionValue.toInt() != actualRevision
      || expectedContentHash != actualContentHash || expectedIds != actualIds)
    {
      return rollbackFailure(
        mcp::McpErrorCode::InvalidParams,
        "replacement_guard",
        "replace_module preview guard no longer matches the IR or target module",
        "run_ir_compile_preview_again",
        QJsonObject{
          {"expectedIrHash", expectedIrHash},
          {"actualIrHash", actualIrHash},
          {"expectedTargetModuleRevision", expectedRevisionValue},
          {"actualTargetModuleRevision", actualRevision},
          {"expectedTargetModuleContentHash", expectedContentHash},
          {"actualTargetModuleContentHash", actualContentHash},
          {"expectedTargetCanonicalObjectIds", stringsToJson(expectedIds)},
          {"actualTargetCanonicalObjectIds", stringsToJson(actualIds)},
        });
    }

    previousModuleRevision = actualRevision;
    previousModuleContentHash = actualContentHash;
    const auto targetModule = mergedModuleRecord(
      moduleId, documentFingerprint, stagedMetadataStore, stagedModuleStore);
    replacementNodes = canonicalModuleNodes(map, targetModule, stagedObjectRegistry);
    for (auto* node : replacementNodes)
    {
      removedObjectIds.push_back(
        externalObjectIdForNode(map, *node, stagedObjectRegistry));
    }
    removedObjectIds.removeDuplicates();
  }

  const auto transactionName = [&] {
    const auto requested = ir->value("name").toString().trimmed();
    return requested.isEmpty() ? QString{"MCP: Apply IR"} : requested;
  }();
  auto transaction = automation::AutomationTransaction{map, transactionName.toStdString()};
  const auto cancelAndFail = [&](
                               const mcp::McpErrorCode code,
                               const QString& stage,
                               const QString& message,
                               const QString& recoveryAction,
                               QJsonObject details = {}) {
    if (transaction.active())
    {
      transaction.cancel();
    }
    return rollbackFailure(code, stage, message, recoveryAction, std::move(details));
  };

  const auto historyStart = stagedHistory.size();
  auto appliedOperations = QJsonArray{};
  const auto idsMode = params.value("idsMode").toString("count");

  if (applyMode == "replace_module")
  {
    if (!replacementNodes.empty())
    {
      const auto removed = removeNodesWithTransaction(
        map,
        QString{"%1: remove previous module"}.arg(transactionName),
        replacementNodes);
      if (!removed)
      {
        return cancelAndFail(
          mcp::McpErrorCode::InternalError,
          "remove_previous_module",
          "Could not remove the previous module inside the aggregate transaction",
          "refresh_module_and_preview_again");
      }
    }
    std::erase_if(stagedMetadataStore, [&](const auto& entry) {
      return recordMatchesDocument(entry.second, documentFingerprint)
             && entry.second.metadata.value("moduleId").toString() == moduleId;
    });
    std::erase_if(stagedModuleStore, [&](const auto& entry) {
      return recordMatchesDocument(entry.second, documentFingerprint)
             && entry.second.moduleId == moduleId;
    });
  }

  if (const auto operations = ir->value("operations");
      operations.isArray() && !operations.toArray().isEmpty())
  {
    auto batchParams = QJsonObject{
      {"operations", operations.toArray()},
      {"name", transactionName},
      {"select", ir->value("select").toBool(true)},
      {"detail", "ids"},
    };
    if (ir->contains("grid"))
    {
      batchParams.insert("grid", ir->value("grid"));
    }
    if (ir->contains("material"))
    {
      batchParams.insert("material", ir->value("material"));
    }
    if (ir->contains("qualityPolicy"))
    {
      batchParams.insert("qualityPolicy", ir->value("qualityPolicy"));
    }
    if (ir->contains("requireMaterialAvailable"))
    {
      batchParams.insert(
        "requireMaterialAvailable", ir->value("requireMaterialAvailable"));
    }
    if (!mergedDefaultMetadata.isEmpty())
    {
      batchParams.insert("defaultMetadata", mergedDefaultMetadata);
    }
    auto blockoutResult = blockoutCreateBatchForMapResult(
      map,
      "blockout_create_batch",
      batchParams,
      stagedHistory,
      stagedNextOperationIndex,
      &stagedMetadataStore,
      &stagedModuleStore,
      stagedObjectRegistryPtr);
    appliedOperations.push_back(blockoutResult.result);
    const auto geometryValid =
      blockoutResult.ok
      && blockoutResult.result.value("validation").toObject().value("valid").toBool(true);
    if (!geometryValid)
    {
      auto details = QJsonObject{{"stageResult", blockoutResult.result}};
      if (!blockoutResult.ok)
      {
        details.insert("stageError", blockoutResult.error.message);
        details.insert("stageErrorDetails", blockoutResult.error.details);
      }
      return cancelAndFail(
        blockoutResult.ok ? mcp::McpErrorCode::InvalidParams : blockoutResult.error.code,
        "geometry",
        blockoutResult.ok ? "IR geometry validation failed"
                          : blockoutResult.error.message,
        "fix_ir_geometry_then_preview_and_retry",
        std::move(details));
    }
    mergeModuleFromOperationResult(
      blockoutResult.result,
      documentFingerprint,
      mergedDefaultMetadata,
      ir->value("qualityPolicy").toObject(),
      stagedModuleStore);
  }

  if (const auto entities = ir->value("entities");
      entities.isArray() && !entities.toArray().isEmpty())
  {
    auto entityParams = QJsonObject{
      {"entities", entities.toArray()},
      {"transactionName",
       ir->value("entityTransactionName").toString("MCP: Apply IR entities")},
      {"select", ir->value("selectEntities").toBool(false)},
      {"detail", "ids"},
    };
    auto result = createEntityCheckedBatchForMapResult(
      map,
      "entity_create_checked_batch",
      entityParams,
      stagedHistory,
      stagedNextOperationIndex);
    appliedOperations.push_back(result.result);
    if (!result.ok)
    {
      return cancelAndFail(
        result.error.code,
        "entities",
        result.error.message,
        "fix_ir_entities_then_preview_and_retry",
        QJsonObject{
          {"stageErrorDetails", result.error.details},
          {"stageResult", result.result},
        });
    }
    mergeModuleFromOperationResult(
      result.result,
      documentFingerprint,
      mergedDefaultMetadata,
      ir->value("qualityPolicy").toObject(),
      stagedModuleStore);
  }

  auto childOperationIds = QStringList{};
  auto objectIds = QStringList{};
  for (auto i = historyStart; i < stagedHistory.size(); ++i)
  {
    auto& child = stagedHistory[i];
    childOperationIds.push_back(child.operationId);
    objectIds.append(child.changedObjectIds);
  }
  childOperationIds.removeDuplicates();
  objectIds.removeDuplicates();

  const auto parentOperationId = QString{"mcp-op-%1"}.arg(stagedNextOperationIndex++);
  for (auto i = historyStart; i < stagedHistory.size(); ++i)
  {
    stagedHistory[i].undoable = false;
    stagedHistory[i].parentOperationId = parentOperationId;
    stagedHistory[i].documentPath =
      map.path().empty() ? QString{} : pathAsQString(map.path());
    stagedHistory[i].documentFingerprint = documentFingerprint;
  }

  if (!transaction.commit())
  {
    return rollbackFailure(
      mcp::McpErrorCode::InternalError,
      "commit",
      "Could not commit the aggregate IR transaction",
      "refresh_status_then_retry_ir_apply");
  }

  const auto normalizedPreview = irPreviewJsonForMap(map, *ir);
  auto result = QJsonObject{
    {"tool", toolName},
    {"valid", true},
    {"schemaVersion", ir->value("schemaVersion").toInt(CurrentIrSchemaVersion)},
    {"applyMode", applyMode},
    {"irHash",
     params.value("irHash").toString().trimmed().isEmpty()
       ? canonicalIrHash(*ir)
       : params.value("irHash").toString().trimmed()},
    {"moduleId", moduleId},
    {"operationId", parentOperationId},
    {"parentOperationId", parentOperationId},
    {"childOperationIds", stringsToJson(childOperationIds)},
    {"operationIds", stringsToJson(childOperationIds)},
    {"operationCount", childOperationIds.size()},
    {"transactionName", transactionName},
    {"changedObjectCount", objectIds.size()},
    {"applied", appliedOperations},
    {"preview", normalizedPreview},
    {"warnings", compatibilityWarnings},
    {"mutatedDocument", true},
    {"partialMutation", false},
    {"atomic", true},
  };
  if (applyMode == "replace_module")
  {
    result.insert("previousModuleRevision", previousModuleRevision);
    result.insert("previousModuleContentHash", previousModuleContentHash);
    result.insert("removedCanonicalObjectCount", removedObjectIds.size());
  }
  for (const auto& key :
       {"qualityPolicy", "curveQuality", "qualityStatus", "acceptancePassed"})
  {
    if (normalizedPreview.contains(key))
    {
      result.insert(key, normalizedPreview.value(key));
    }
  }
  applyChangedObjectIdsMode(result, objectIds, idsMode);
  compactAppliedOperationResults(result, idsMode);
  if (!moduleId.isEmpty())
  {
    result.insert("resourceUri", QString{"tbmcp://module/%1"}.arg(moduleId));
  }

  auto parent = McpOperationRecord{};
  parent.operationId = parentOperationId;
  parent.toolName = toolName;
  parent.transactionName = transactionName;
  parent.operationKind = "aggregate";
  parent.documentPath = map.path().empty() ? QString{} : pathAsQString(map.path());
  parent.documentFingerprint = documentFingerprint;
  parent.changedObjectIds = objectIds;
  parent.deletedObjectIds = removedObjectIds;
  parent.childOperationIds = childOperationIds;
  parent.setSummary(result);
  parent.setDetail(QJsonObject{{"ir", *ir}, {"applied", appliedOperations}});
  stagedHistory.push_back(std::move(parent));

  if (!moduleId.isEmpty())
  {
    for (auto& [key, module] : stagedModuleStore)
    {
      Q_UNUSED(key);
      if (
        module.moduleId == moduleId && module.documentFingerprint == documentFingerprint
        && !module.operationIds.contains(parentOperationId))
      {
        module.operationIds.push_back(parentOperationId);
      }
    }
    if (applyMode == "replace_module")
    {
      for (auto& [key, module] : stagedModuleStore)
      {
        Q_UNUSED(key);
        if (
          module.moduleId == moduleId
          && recordMatchesDocument(module, documentFingerprint))
        {
          module.revision = previousModuleRevision;
          module.contentHash.clear();
          break;
        }
      }
    }
  }

  if (objectRegistry != nullptr)
  {
    reconcileMcpSessionForMap(
      map,
      stagedMetadataStore,
      stagedModuleStore,
      stagedObjectRegistry,
      parentOperationId);
    if (!moduleId.isEmpty())
    {
      const auto module = mergedModuleRecord(
        moduleId, documentFingerprint, stagedMetadataStore, stagedModuleStore);
      result.insert("moduleRevision", module.revision);
      result.insert("moduleContentHash", module.contentHash);
      result.insert("createdCanonicalObjectCount", module.objectIds.size());
    }
  }
  stagedHistory.back().setSummary(result);

  history = std::move(stagedHistory);
  nextOperationIndex = stagedNextOperationIndex;
  metadataStore = std::move(stagedMetadataStore);
  moduleStore = std::move(stagedModuleStore);
  if (objectRegistry != nullptr)
  {
    *objectRegistry = std::move(stagedObjectRegistry);
  }
  return McpBridgeToolResult::success(result);
}

McpBridgeToolResult irApplyFromFileResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  std::map<QString, McpBrushMetadataRecord>& metadataStore,
  std::map<QString, McpModuleRecord>& moduleStore,
  McpObjectRegistry* objectRegistry,
  std::map<QString, McpIrPreviewCacheRecord>* previewCache)
{
  auto paramsWithPath = params;
  if (
    paramsWithPath.value("path").toString().trimmed().isEmpty()
    && paramsWithPath.value("previewId").toString().trimmed().isEmpty())
  {
    return irApplyFromFileMissingTargetFailure();
  }
  if (!paramsWithPath.value("previewId").toString().trimmed().isEmpty())
  {
    auto* mapWindow = appController.mapWindowManager().topMapWindow();
    if (mapWindow == nullptr)
    {
      return noActiveDocumentFailure();
    }
    if (previewCache == nullptr)
    {
      return irApplyPreMutationFailure(
        "ir_apply_from_file previewId cache is unavailable",
        "run_ir_compile_preview_from_file_again");
    }
    const auto cached =
      cachedIrApplyParams(mapWindow->document().map(), paramsWithPath, *previewCache);
    if (!cached.ok)
    {
      return cached;
    }
    paramsWithPath = cached.result;
  }

  auto error = QString{};
  auto compatibilityWarnings = QJsonArray{};
  const auto ir = irFromFileParams(paramsWithPath, error, &compatibilityWarnings);
  if (!ir)
  {
    return irApplyPreMutationFailure(
      error,
      "fix_ir_file_or_preview_again",
      QJsonObject{{"sourcePath", paramsWithPath.value("path").toString()}});
  }
  if (
    ir->value("applyMode").toString("create") == "replace_module"
    && paramsWithPath.value("previewId").toString().trimmed().isEmpty())
  {
    return irApplyPreMutationFailure(
      "replace_module file apply requires previewId",
      "run_ir_compile_preview_from_file_again");
  }
  auto applyParams = paramsWithPath;
  applyParams.insert("ir", *ir);
  applyParams.insert("_irCompatibilityWarnings", compatibilityWarnings);
  auto result = irApplyResult(
    appController,
    toolName,
    applyParams,
    history,
    nextOperationIndex,
    metadataStore,
    moduleStore,
    objectRegistry);
  if (result.ok)
  {
    result.result.insert(
      "sourcePath", canonicalIrFilePath(paramsWithPath.value("path").toString()));
    if (!paramsWithPath.value("previewId").toString().trimmed().isEmpty())
    {
      result.result.insert("previewId", paramsWithPath.value("previewId").toString());
      result.result.insert("irHash", paramsWithPath.value("irHash").toString());
      result.result.insert("usedPreviewCache", true);
    }
  }
  return result;
}

McpBridgeToolResult irApplyFromFileForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  std::map<QString, McpBrushMetadataRecord>& metadataStore,
  std::map<QString, McpModuleRecord>& moduleStore,
  McpObjectRegistry* objectRegistry,
  std::map<QString, McpIrPreviewCacheRecord>* previewCache)
{
  auto paramsWithPath = params;
  if (
    paramsWithPath.value("path").toString().trimmed().isEmpty()
    && paramsWithPath.value("previewId").toString().trimmed().isEmpty())
  {
    return irApplyFromFileMissingTargetFailure();
  }
  if (!paramsWithPath.value("previewId").toString().trimmed().isEmpty())
  {
    if (previewCache == nullptr)
    {
      return irApplyPreMutationFailure(
        "ir_apply_from_file previewId cache is unavailable",
        "run_ir_compile_preview_from_file_again");
    }
    const auto cached = cachedIrApplyParams(map, paramsWithPath, *previewCache);
    if (!cached.ok)
    {
      return cached;
    }
    paramsWithPath = cached.result;
  }

  auto error = QString{};
  auto compatibilityWarnings = QJsonArray{};
  const auto ir = irFromFileParams(paramsWithPath, error, &compatibilityWarnings);
  if (!ir)
  {
    return irApplyPreMutationFailure(
      error,
      "fix_ir_file_or_preview_again",
      QJsonObject{{"sourcePath", paramsWithPath.value("path").toString()}});
  }
  if (
    ir->value("applyMode").toString("create") == "replace_module"
    && paramsWithPath.value("previewId").toString().trimmed().isEmpty())
  {
    return irApplyPreMutationFailure(
      "replace_module file apply requires previewId",
      "run_ir_compile_preview_from_file_again");
  }
  auto applyParams = paramsWithPath;
  applyParams.insert("ir", *ir);
  applyParams.insert("_irCompatibilityWarnings", compatibilityWarnings);
  auto result = irApplyForMapResult(
    map,
    toolName,
    applyParams,
    history,
    nextOperationIndex,
    metadataStore,
    moduleStore,
    objectRegistry);
  if (result.ok)
  {
    result.result.insert(
      "sourcePath", canonicalIrFilePath(paramsWithPath.value("path").toString()));
    if (!paramsWithPath.value("previewId").toString().trimmed().isEmpty())
    {
      result.result.insert("previewId", paramsWithPath.value("previewId").toString());
      result.result.insert("irHash", paramsWithPath.value("irHash").toString());
      result.result.insert("usedPreviewCache", true);
    }
  }
  return result;
}

} // namespace tb::ui
