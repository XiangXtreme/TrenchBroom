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

#include "ui/automation/AutomationObjectRegistry.h"

#include <QJsonArray>

#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityNodeBase.h"
#include "mdl/GameInfo.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/Node.h"
#include "mdl/PatchNode.h"
#include "mdl/WorldNode.h"
#include "ui/QPathUtils.h"

#include <algorithm>
#include <functional>

namespace tb::ui::automation
{
namespace
{

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

QStringList brushMaterials(const mdl::BrushNode& brushNode)
{
  auto materials = QStringList{};
  const auto& brush = brushNode.brush();
  for (size_t i = 0; i < brush.faceCount(); ++i)
  {
    const auto material = QString::fromStdString(brush.face(i).materialName());
    if (!materials.contains(material))
    {
      materials.push_back(material);
    }
  }
  materials.sort(Qt::CaseInsensitive);
  return materials;
}

QJsonArray stringsToJson(const QStringList& values)
{
  auto result = QJsonArray{};
  for (const auto& value : values)
  {
    result.push_back(value);
  }
  return result;
}

QJsonObject nodeSummaryJson(const mdl::Node& node, const QString& legacyPathId)
{
  auto result = QJsonObject{
    {"legacyPathId", legacyPathId},
    {"type", nodeTypeName(node)},
    {"bounds", boundsToJson(node.logicalBounds())},
    {"childCount", static_cast<int>(node.childCount())},
  };

  if (const auto* entityNode = dynamic_cast<const mdl::EntityNodeBase*>(&node))
  {
    result.insert("classname", QString::fromStdString(entityNode->entity().classname()));
    if (const auto* targetname = entityNode->entity().property("targetname"))
    {
      result.insert("targetname", QString::fromStdString(*targetname));
    }
  }

  if (const auto* brushNode = dynamic_cast<const mdl::BrushNode*>(&node))
  {
    result.insert("faceCount", static_cast<int>(brushNode->brush().faceCount()));
    result.insert("vertexCount", static_cast<int>(brushNode->brush().vertexCount()));
    result.insert("materials", stringsToJson(brushMaterials(*brushNode)));
  }

  return result;
}

QString nodeFingerprint(const mdl::Node& node, const QString& legacyPathId)
{
  const auto summary = nodeSummaryJson(node, legacyPathId);
  auto parts = QStringList{
    summary.value("type").toString(),
    legacyPathId,
    QString::number(summary.value("childCount").toInt()),
  };

  const auto bounds = summary.value("bounds").toObject();
  for (const auto& key : {"min", "max"})
  {
    for (const auto& value : bounds.value(key).toArray())
    {
      parts.push_back(QString::number(value.toDouble(), 'f', 3));
    }
  }

  if (summary.contains("classname"))
  {
    parts.push_back(summary.value("classname").toString());
  }
  if (summary.contains("targetname"))
  {
    parts.push_back(summary.value("targetname").toString());
  }
  if (summary.contains("faceCount"))
  {
    parts.push_back(QString::number(summary.value("faceCount").toInt()));
    parts.push_back(QString::number(summary.value("vertexCount").toInt()));
  }
  for (const auto& material : summary.value("materials").toArray())
  {
    parts.push_back(material.toString());
  }

  return parts.join('|');
}

mdl::Node* resolveLegacyObjectId(mdl::Map& map, const QString& objectId)
{
  const auto path = AutomationObjectRegistry::parseLegacyObjectId(objectId);
  if (!path)
  {
    return nullptr;
  }
  return map.worldNode().resolvePath(*path);
}

mdl::Node* findNodeByAddress(mdl::Node& root, const quintptr nodeAddress)
{
  if (reinterpret_cast<quintptr>(&root) == nodeAddress)
  {
    return &root;
  }
  for (auto* child : root.children())
  {
    if (child == nullptr)
    {
      continue;
    }
    if (auto* result = findNodeByAddress(*child, nodeAddress))
    {
      return result;
    }
  }
  return nullptr;
}

bool isObjectIdKey(const QString& key)
{
  if (
    key.endsWith("ObjectId") || key.endsWith("ObjectIds") || key.endsWith("objectId")
    || key.endsWith("objectIds"))
  {
    return true;
  }

  static const auto Keys = QStringList{
    "objectId",
    "objectIds",
    "changedObjectIds",
    "childIdSample",
    "childIds",
    "deletedObjectIds",
    "faceOwnerBrushIds",
    "groupId",
    "groupIds",
    "nonGridAlignedObjectIds",
    "objectIdSample",
    "selectedObjectIdSample",
    "selectedObjectIds",
    "targetObjectIds",
    "ungroupedGroupIds",
  };
  return Keys.contains(key);
}

QJsonValue walkObjectIds(
  const QJsonValue& value,
  const QString& key,
  const std::function<QJsonValue(const QString&)>& stringFn)
{
  if (value.isString())
  {
    return isObjectIdKey(key) ? stringFn(value.toString()) : value;
  }
  if (value.isArray())
  {
    auto result = QJsonArray{};
    for (const auto& entry : value.toArray())
    {
      result.push_back(walkObjectIds(entry, key, stringFn));
    }
    return result;
  }
  if (value.isObject())
  {
    auto result = QJsonObject{};
    const auto object = value.toObject();
    for (auto it = object.begin(); it != object.end(); ++it)
    {
      result.insert(it.key(), walkObjectIds(it.value(), it.key(), stringFn));
    }
    return result;
  }
  return value;
}

} // namespace

void AutomationObjectRegistry::clear()
{
  ++m_documentEpoch;
  m_nextSequence = 1;
  m_currentMapAddress = 0;
  m_currentWorldAddress = 0;
  m_records.clear();
  m_legacyToStable.clear();
}

size_t AutomationObjectRegistry::retainDocumentFingerprints(
  const QStringList& documentFingerprints)
{
  const auto oldSize = m_records.size();
  std::erase_if(m_records, [&](const auto& entry) {
    return !documentFingerprints.contains(entry.second.documentFingerprint);
  });
  std::erase_if(m_legacyToStable, [&](const auto& entry) {
    return !m_records.contains(entry.second);
  });
  return oldSize - m_records.size();
}

size_t AutomationObjectRegistry::recordCount() const
{
  return m_records.size();
}

int AutomationObjectRegistry::documentEpoch(mdl::Map& map) const
{
  const auto mapAddress = reinterpret_cast<quintptr>(&map);
  const auto worldAddress = reinterpret_cast<quintptr>(&map.worldNode());
  if (m_currentMapAddress == 0)
  {
    m_currentMapAddress = mapAddress;
    m_currentWorldAddress = worldAddress;
  }
  else if (m_currentMapAddress != mapAddress || m_currentWorldAddress != worldAddress)
  {
    ++m_documentEpoch;
    m_nextSequence = 1;
    m_legacyToStable.clear();
    m_currentMapAddress = mapAddress;
    m_currentWorldAddress = worldAddress;
  }
  return m_documentEpoch;
}

QString AutomationObjectRegistry::documentFingerprint(mdl::Map& map) const
{
  auto hash = qHash(QString::fromStdString(map.filename()));
  const auto documentPath = map.path().empty() ? QString{} : pathAsQString(map.path());
  hash ^= qHash(documentPath) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
  hash ^= qHash(QString::fromStdString(map.gameInfo().gameConfig.name)) + 0x9e3779b9u
          + (hash << 6) + (hash >> 2);
  hash ^= qHash(QString::number(reinterpret_cast<quintptr>(&map), 16)) + 0x9e3779b9u
          + (hash << 6) + (hash >> 2);
  return QString{"doc:%1"}.arg(static_cast<quint64>(hash), 16, 16, QLatin1Char{'0'});
}

QString AutomationObjectRegistry::registerNode(mdl::Map& map, mdl::Node& node) const
{
  const auto epoch = documentEpoch(map);
  const auto legacyPathId = nodePathId(node, map.worldNode());
  const auto fingerprint = nodeFingerprint(node, legacyPathId);
  const auto legacyIt = m_legacyToStable.find(legacyPathId);
  if (legacyIt != m_legacyToStable.end())
  {
    const auto recordIt = m_records.find(legacyIt->second);
    if (
      recordIt != m_records.end() && recordIt->second.documentEpoch == epoch
      && recordIt->second.nodeAddress == reinterpret_cast<quintptr>(&node))
    {
      return recordIt->second.stableId;
    }
  }

  const auto stableId = QString{"mcp:%1:%2"}.arg(epoch).arg(m_nextSequence++);
  auto record = Record{};
  record.stableId = stableId;
  record.legacyPathId = legacyPathId;
  record.type = nodeTypeName(node);
  record.documentEpoch = epoch;
  record.documentFingerprint = documentFingerprint(map);
  record.creationFingerprint = fingerprint;
  record.nodeAddress = reinterpret_cast<quintptr>(&node);
  record.summary = nodeSummaryJson(node, legacyPathId);
  m_records[stableId] = record;
  m_legacyToStable[legacyPathId] = stableId;
  return stableId;
}

QString AutomationObjectRegistry::externalIdForLegacy(
  mdl::Map& map, const QString& legacyPathId) const
{
  if (!isLegacyObjectId(legacyPathId))
  {
    return legacyPathId;
  }
  if (const auto it = m_legacyToStable.find(legacyPathId); it != m_legacyToStable.end())
  {
    const auto stableId = it->second;
    const auto resolved = resolveExternalId(map, stableId);
    if (resolved.ok && resolved.legacyPathId == legacyPathId)
    {
      return stableId;
    }
    if (resolveLegacyObjectId(map, legacyPathId) == nullptr)
    {
      return stableId;
    }
    m_legacyToStable.erase(legacyPathId);
  }
  auto* node = resolveLegacyObjectId(map, legacyPathId);
  if (node == nullptr)
  {
    return legacyPathId;
  }
  return registerNode(map, *node);
}

AutomationObjectRegistry::ResolveResult AutomationObjectRegistry::resolveExternalId(
  mdl::Map& map, const QString& objectId) const
{
  if (isLegacyObjectId(objectId))
  {
    const auto stableIt = m_legacyToStable.find(objectId);
    if (stableIt != m_legacyToStable.end())
    {
      auto stableResult = resolveExternalId(map, stableIt->second);
      if (stableResult.ok)
      {
        stableResult.diagnostic.insert("objectId", stableIt->second);
        stableResult.diagnostic.insert("legacyInputId", objectId);
        stableResult.diagnostic.insert("stableObjectId", stableIt->second);
        stableResult.diagnostic.insert("legacy", false);
        stableResult.diagnostic.insert("legacyInput", true);
        return stableResult;
      }
      m_legacyToStable.erase(stableIt);
    }
    if (auto* node = resolveLegacyObjectId(map, objectId))
    {
      const auto stableId = registerNode(map, *node);
      return ResolveResult{
        true,
        objectId,
        objectId,
        {},
        QJsonObject{
          {"objectId", objectId},
          {"stableObjectId", stableId},
          {"legacy", true},
          {"live", true},
          {"stale", false},
          {"mismatch", false},
        },
      };
    }
    return ResolveResult{
      true,
      objectId,
      objectId,
      {},
      QJsonObject{
        {"objectId", objectId},
        {"legacy", true},
        {"live", false},
        {"stale", true},
        {"mismatch", false},
      },
    };
  }

  if (!isStableObjectId(objectId))
  {
    return ResolveResult{
      true,
      objectId,
      objectId,
      {},
      QJsonObject{{"objectId", objectId}, {"legacy", false}},
    };
  }

  const auto recordIt = m_records.find(objectId);
  if (recordIt == m_records.end())
  {
    const auto diagnostic = QJsonObject{
      {"objectId", objectId},
      {"live", false},
      {"stale", true},
      {"mismatch", false},
      {"staleReason", "stable object id is unknown in this MCP session"},
    };
    return ResolveResult{
      false,
      objectId,
      {},
      QString{"Unknown or stale MCP object id: %1"}.arg(objectId),
      diagnostic,
    };
  }

  const auto& record = recordIt->second;
  const auto currentEpoch = documentEpoch(map);
  if (record.documentEpoch != currentEpoch)
  {
    const auto diagnostic = QJsonObject{
      {"objectId", objectId},
      {"legacyPathId", record.legacyPathId},
      {"live", false},
      {"stale", true},
      {"mismatch", false},
      {"staleReason", "document epoch changed after this id was created"},
    };
    return ResolveResult{
      false,
      objectId,
      record.legacyPathId,
      QString{"MCP object id is stale after document reload/switch: %1"}.arg(objectId),
      diagnostic,
    };
  }

  if (auto* sameNode = findNodeByAddress(map.worldNode(), record.nodeAddress))
  {
    const auto currentLegacyPathId = nodePathId(*sameNode, map.worldNode());
    const auto previousLegacyPathId = record.legacyPathId;
    const auto currentFingerprint = nodeFingerprint(*sameNode, currentLegacyPathId);
    const auto pathChanged = currentLegacyPathId != previousLegacyPathId;
    if (pathChanged)
    {
      m_legacyToStable.erase(previousLegacyPathId);
      recordIt->second.legacyPathId = currentLegacyPathId;
      recordIt->second.summary = nodeSummaryJson(*sameNode, currentLegacyPathId);
      m_legacyToStable[currentLegacyPathId] = objectId;
    }
    return ResolveResult{
      true,
      objectId,
      currentLegacyPathId,
      {},
      QJsonObject{
        {"objectId", objectId},
        {"legacyPathId", currentLegacyPathId},
        {"previousLegacyPathId", pathChanged ? previousLegacyPathId : QString{}},
        {"type", record.type},
        {"live", true},
        {"stale", false},
        {"mismatch", false},
        {"pathChanged", pathChanged},
        {"documentEpoch", record.documentEpoch},
        {"changedSinceRegistered", currentFingerprint != record.creationFingerprint},
      },
    };
  }

  auto* node = resolveLegacyObjectId(map, record.legacyPathId);
  if (node == nullptr)
  {
    const auto diagnostic = QJsonObject{
      {"objectId", objectId},
      {"legacyPathId", record.legacyPathId},
      {"live", false},
      {"stale", true},
      {"mismatch", false},
      {"staleReason", "object path no longer resolves in the active document"},
    };
    return ResolveResult{
      false,
      objectId,
      record.legacyPathId,
      QString{"MCP object id no longer resolves: %1"}.arg(objectId),
      diagnostic,
    };
  }

  const auto currentNodeAddress = reinterpret_cast<quintptr>(node);
  const auto currentFingerprint = nodeFingerprint(*node, record.legacyPathId);
  if (currentNodeAddress != record.nodeAddress)
  {
    const auto diagnostic = QJsonObject{
      {"objectId", objectId},
      {"legacyPathId", record.legacyPathId},
      {"live", false},
      {"stale", true},
      {"mismatch", false},
      {"pathReused", true},
      {"staleReason", "original object is no longer live and its path is reused"},
    };
    return ResolveResult{
      false,
      objectId,
      record.legacyPathId,
      QString{"MCP object id is stale; its path is now reused: %1"}.arg(objectId),
      diagnostic,
    };
  }

  return ResolveResult{
    true,
    objectId,
    record.legacyPathId,
    {},
    QJsonObject{
      {"objectId", objectId},
      {"legacyPathId", record.legacyPathId},
      {"type", record.type},
      {"live", true},
      {"stale", false},
      {"mismatch", false},
      {"documentEpoch", record.documentEpoch},
      {"changedSinceRegistered", currentFingerprint != record.creationFingerprint},
    },
  };
}

QJsonObject AutomationObjectRegistry::liveStateJson(
  mdl::Map& map,
  const QStringList& objectIds,
  const bool undone,
  const bool includeDiagnostics) const
{
  auto liveObjectCount = 0;
  auto staleObjectCount = 0;
  auto mismatchCount = 0;
  auto legacyObjectCount = 0;
  auto diagnostics = QJsonArray{};

  for (const auto& objectId : objectIds)
  {
    const auto resolved = resolveExternalId(map, objectId);
    auto diagnostic = resolved.diagnostic;
    if (isLegacyObjectId(objectId))
    {
      ++legacyObjectCount;
    }
    if (diagnostic.value("mismatch").toBool(false))
    {
      ++mismatchCount;
    }
    else if (diagnostic.value("live").toBool(false))
    {
      ++liveObjectCount;
    }
    else
    {
      ++staleObjectCount;
    }
    if (includeDiagnostics)
    {
      diagnostics.push_back(diagnostic);
    }
  }

  auto result = QJsonObject{
    {"liveObjectCount", liveObjectCount},
    {"staleObjectCount", staleObjectCount},
    {"mismatchCount", mismatchCount},
    {"valid", !undone && staleObjectCount == 0 && mismatchCount == 0},
  };
  if (legacyObjectCount > 0 && liveObjectCount == 0 && staleObjectCount == 0)
  {
    result.insert("legacyObjectCount", legacyObjectCount);
  }
  if (undone)
  {
    result.insert("staleReason", "operation was undone");
  }
  else if (mismatchCount > 0)
  {
    result.insert(
      "staleReason", "one or more stable ids now resolve to different objects");
  }
  else if (staleObjectCount > 0)
  {
    result.insert("staleReason", "one or more operation objects are no longer live");
  }
  if (!diagnostics.isEmpty())
  {
    result.insert("objectDiagnostics", diagnostics);
  }
  return result;
}

std::optional<QJsonObject> AutomationObjectRegistry::internalizeParams(
  mdl::Map& map, const QJsonObject& params, QString& error) const
{
  const auto translate = [&](const QString& value) -> QJsonValue {
    if (!isStableObjectId(value))
    {
      return value;
    }
    const auto resolved = resolveExternalId(map, value);
    if (!resolved.ok)
    {
      error = resolved.error;
      return value;
    }
    return resolved.legacyPathId;
  };

  const auto converted = walkObjectIds(params, {}, translate).toObject();
  if (!error.isEmpty())
  {
    return std::nullopt;
  }
  return converted;
}

QJsonObject AutomationObjectRegistry::externalizeResult(
  mdl::Map& map, const QJsonObject& result) const
{
  auto diagnostics = QJsonArray{};
  const auto translate = [&](const QString& value) -> QJsonValue {
    if (!isLegacyObjectId(value))
    {
      return value;
    }
    if (resolveLegacyObjectId(map, value) == nullptr)
    {
      const auto legacyIt = m_legacyToStable.find(value);
      if (legacyIt != m_legacyToStable.end() && m_records.contains(legacyIt->second))
      {
        return legacyIt->second;
      }
      diagnostics.push_back(QJsonObject{
        {"objectId", value},
        {"legacy", true},
        {"live", false},
        {"stale", true},
        {"mismatch", false},
        {"staleReason", "legacy object id no longer resolves in active document"},
      });
      return value;
    }
    return externalIdForLegacy(map, value);
  };
  auto converted = walkObjectIds(result, {}, translate).toObject();
  if (!diagnostics.isEmpty())
  {
    auto existing = converted.value("objectIdDiagnostics").toArray();
    for (const auto& diagnostic : diagnostics)
    {
      existing.push_back(diagnostic);
    }
    converted.insert("objectIdDiagnostics", existing);
  }
  return converted;
}

bool AutomationObjectRegistry::isStableObjectId(const QString& id)
{
  return id.startsWith("mcp:");
}

bool AutomationObjectRegistry::isLegacyObjectId(const QString& id)
{
  return id == "node:world" || id.startsWith("node:");
}

std::optional<mdl::NodePath> AutomationObjectRegistry::parseLegacyObjectId(const QString& id)
{
  static const auto Prefix = QString{"node:"};
  if (id == "node:world")
  {
    return mdl::NodePath{};
  }
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

} // namespace tb::ui::automation
