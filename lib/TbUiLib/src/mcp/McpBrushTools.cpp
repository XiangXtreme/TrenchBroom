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

#include "McpBridgeServerTools.h"
#include "McpToolSupport.h"
#include "gl/Material.h"
#include "gl/MaterialManager.h"
#include "mcp/McpError.h"
#include "mdl/AddRemoveNodesCommand.h"
#include "mdl/Brush.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/CircleShape.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityProperties.h"
#include "mdl/GameInfo.h"
#include "mdl/Map.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/Map_World.h"
#include "mdl/Node.h"
#include "mdl/PatchNode.h"
#include "mdl/Transaction.h"
#include "mdl/WorldNode.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/QPathUtils.h"
#include "ui/automation/AutomationGeometry.h"
#include "ui/mcp/McpObjectRegistry.h"

#include "kd/string_compare.h"

#include "vm/bbox.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <vector>

namespace tb::ui
{
namespace mcp = tb::mcp;

namespace
{

const auto RemovedPrefabBatchTypes = QStringList{
  "room",
  "corridor",
  "sky_shell",
  "doorway",
  "cover",
  "stairs",
};

constexpr auto DefaultGeometrySampleLimit = 6;

struct PathRibbonSegment
{
  std::vector<vm::vec2d> polygon;
  double minZ = 0.0;
  double maxZ = 16.0;
};

struct ShellSeamParticipant
{
  QString objectId;
  QString part;
  double order = 0.0;
  std::vector<vm::vec3d> vertices;
};

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

std::optional<vm::vec3d> vec3FromJsonValue(const QJsonValue& value)
{
  if (!value.isArray())
  {
    return std::nullopt;
  }
  const auto array = value.toArray();
  if (array.size() != 3)
  {
    return std::nullopt;
  }
  for (const auto& component : array)
  {
    if (!component.isDouble())
    {
      return std::nullopt;
    }
  }
  return vm::vec3d{
    array.at(0).toDouble(),
    array.at(1).toDouble(),
    array.at(2).toDouble(),
  };
}

QString summaryOrFullDetail(const QJsonObject& params)
{
  const auto detail = params.value("detail").toString("summary").trimmed().toLower();
  return detail == "full" || detail == "ids" ? QString{"full"} : QString{"summary"};
}

QString idDetailFromParams(const QJsonObject& params)
{
  const auto idsMode = params.value("idsMode").toString().trimmed().toLower();
  if (idsMode == "full")
  {
    return "ids";
  }
  if (idsMode == "none" || idsMode == "count" || idsMode == "sample")
  {
    return "summary";
  }
  return params.value("detail").toString("summary");
}

QJsonArray jsonSample(
  const QJsonArray& values, const int limit = DefaultGeometrySampleLimit)
{
  auto result = QJsonArray{};
  const auto count = std::min(std::max(0, limit), static_cast<int>(values.size()));
  for (auto i = 0; i < count; ++i)
  {
    result.push_back(values.at(i));
  }
  return result;
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

QString makeOperationId(int& nextOperationIndex)
{
  return QString{"mcp-op-%1"}.arg(nextOperationIndex++);
}

QJsonObject mutationResultJson(const McpOperationRecord& operation)
{
  auto result = QJsonObject{};
  result.insert("operationId", operation.operationId);
  result.insert("transactionName", operation.transactionName);
  result.insert("mutatedDocument", true);
  result.insert("activeDocumentPath", operation.documentPath);
  result.insert("documentFingerprint", operation.documentFingerprint);
  result.insert("changedObjectCount", operation.changedObjectIds.size());
  result.insert(
    "resourceUri", QString{"tbmcp://operation/%1"}.arg(operation.operationId));
  return result;
}

void mcpRecordOperation(
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  mdl::Map& map,
  const QString& toolName,
  const QString& transactionName,
  const QJsonArray& changedObjectIds,
  QJsonObject& result,
  const QJsonObject& detail = {})
{
  auto operation = McpOperationRecord{};
  operation.operationId = makeOperationId(nextOperationIndex);
  operation.toolName = toolName;
  operation.transactionName = transactionName;
  operation.documentPath = map.path().empty() ? QString{} : pathAsQString(map.path());
  operation.documentFingerprint = documentFingerprintForMap(map);
  operation.setChangedObjectIds(changedObjectIds);
  result = mutationResultJson(operation);
  operation.setSummary(result);
  operation.setDetail(detail);
  appendMcpOperationRecord(history, std::move(operation));
}

void applyDetailLevel(
  QJsonObject& result,
  const QJsonArray& changedObjectIds,
  const QString& detail,
  const QJsonArray& fullResults = {})
{
  const auto normalized = detail.trimmed().toLower();
  if (normalized == "ids")
  {
    result.insert("changedObjectIds", changedObjectIds);
  }
  else if (normalized == "full")
  {
    result.insert("changedObjectIds", changedObjectIds);
    if (!fullResults.isEmpty())
    {
      result.insert("results", fullResults);
    }
  }
  result.insert(
    "detail",
    normalized == "full"  ? "full"
    : normalized == "ids" ? "ids"
                          : "summary");
}

QJsonObject mergeMetadataObjects(QJsonObject base, const QJsonObject& overlay)
{
  for (auto it = overlay.begin(); it != overlay.end(); ++it)
  {
    base.insert(it.key(), it.value());
  }
  return base;
}

vm::bbox3d boundsForNodes(const std::vector<mdl::Node*>& nodes)
{
  auto result = vm::bbox3d{};
  auto first = true;
  for (const auto* node : nodes)
  {
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

QJsonObject pendingNodeSummaryJson(const mdl::Node& node)
{
  auto result = QJsonObject{
    {"type", nodeTypeName(node)},
    {"name", QString::fromStdString(node.name())},
    {"childCount", static_cast<int>(node.childCount())},
    {"descendantCount", static_cast<int>(node.descendantCount())},
    {"logicalBounds", boundsToJson(node.logicalBounds())},
  };

  if (const auto* brushNode = dynamic_cast<const mdl::BrushNode*>(&node))
  {
    result.insert("faceCount", static_cast<int>(brushNode->brush().faceCount()));
  }

  return result;
}

QJsonArray pendingNodeSummariesJson(const std::vector<mdl::Node*>& nodes)
{
  auto result = QJsonArray{};
  for (const auto* node : nodes)
  {
    result.push_back(pendingNodeSummaryJson(*node));
  }
  return result;
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

std::optional<vm::vec2d> mcpVec2FromJsonValue(
  const QJsonValue& value, const QString& key, QString& error)
{
  if (!value.isArray())
  {
    error = QString{"%1 must be an array of two numbers"}.arg(key);
    return std::nullopt;
  }

  const auto array = value.toArray();
  if (array.size() != 2)
  {
    error = QString{"%1 must contain exactly two numbers"}.arg(key);
    return std::nullopt;
  }

  if (!array[0].isDouble() || !array[1].isDouble())
  {
    error = QString{"%1 values must be numbers"}.arg(key);
    return std::nullopt;
  }

  const auto x = array[0].toDouble();
  const auto y = array[1].toDouble();
  if (!std::isfinite(x) || !std::isfinite(y))
  {
    error = QString{"%1 values must be finite"}.arg(key);
    return std::nullopt;
  }
  return vm::vec2d{x, y};
}

std::optional<std::vector<vm::vec2d>> points2DFromJson(
  const QJsonObject& params,
  const QString& key,
  const size_t minPointCount,
  QString& error)
{
  const auto value = params.value(key);
  if (!value.isArray())
  {
    error = QString{"%1 must be an array of [x,y] points"}.arg(key);
    return std::nullopt;
  }

  const auto array = value.toArray();
  if (static_cast<size_t>(array.size()) < minPointCount)
  {
    error = QString{"%1 must contain at least %2 points"}.arg(key).arg(minPointCount);
    return std::nullopt;
  }

  auto result = std::vector<vm::vec2d>{};
  result.reserve(static_cast<size_t>(array.size()));
  for (auto i = 0; i < array.size(); ++i)
  {
    auto point = mcpVec2FromJsonValue(array[i], QString{"%1[%2]"}.arg(key).arg(i), error);
    if (!point)
    {
      return std::nullopt;
    }
    result.push_back(*point);
  }
  return result;
}

std::optional<std::vector<vm::vec2d>> points2DFromJson(
  const QJsonObject& params, const QString& key, QString& error)
{
  return points2DFromJson(params, key, 3u, error);
}

std::optional<vm::vec3d> mcpVec3FromJsonValue(
  const QJsonValue& value, const QString& key, QString& error)
{
  auto object = QJsonObject{};
  object.insert(key, value);
  return mcpVec3FromJson(object, key, error);
}

std::optional<std::vector<vm::vec3d>> points3DFromJson(
  const QJsonObject& params, const QString& key, QString& error)
{
  const auto value = params.value(key);
  if (!value.isArray())
  {
    error = QString{"%1 must be an array of [x,y,z] points"}.arg(key);
    return std::nullopt;
  }

  const auto array = value.toArray();
  if (array.size() < 4)
  {
    error = QString{"%1 must contain at least four points"}.arg(key);
    return std::nullopt;
  }

  auto result = std::vector<vm::vec3d>{};
  result.reserve(static_cast<size_t>(array.size()));
  for (auto i = 0; i < array.size(); ++i)
  {
    auto point = mcpVec3FromJsonValue(array[i], QString{"%1[%2]"}.arg(key).arg(i), error);
    if (!point)
    {
      return std::nullopt;
    }
    result.push_back(*point);
  }
  return result;
}

std::optional<std::vector<vm::vec3d>> centerlinePoints3DFromJson(
  const QJsonObject& params, const QString& key, QString& error)
{
  const auto value = params.value(key);
  if (!value.isArray())
  {
    error = QString{"%1 must be an array of [x,y,z] points"}.arg(key);
    return std::nullopt;
  }

  const auto array = value.toArray();
  if (array.size() < 2)
  {
    error = QString{"%1 must contain at least two points"}.arg(key);
    return std::nullopt;
  }

  auto result = std::vector<vm::vec3d>{};
  result.reserve(static_cast<size_t>(array.size()));
  for (auto i = 0; i < array.size(); ++i)
  {
    auto point = mcpVec3FromJsonValue(array[i], QString{"%1[%2]"}.arg(key).arg(i), error);
    if (!point)
    {
      return std::nullopt;
    }
    result.push_back(*point);
  }
  return result;
}

std::optional<vm::bbox3d> boundsFromJson(const QJsonObject& params, QString& error)
{
  const auto min = mcpVec3FromJson(params, "min", error);
  if (!min)
  {
    return std::nullopt;
  }
  const auto max = mcpVec3FromJson(params, "max", error);
  if (!max)
  {
    return std::nullopt;
  }

  if (
    min->x() >= max->x() || min->y() >= max->y() || min->z() >= max->z()
    || !std::isfinite(min->x()) || !std::isfinite(min->y()) || !std::isfinite(min->z())
    || !std::isfinite(max->x()) || !std::isfinite(max->y()) || !std::isfinite(max->z()))
  {
    error = "min must be smaller than max on all axes";
    return std::nullopt;
  }

  return vm::bbox3d{*min, *max};
}

std::string mcpOptionalString(
  const QJsonObject& params, const QString& key, const std::string& defaultValue = {})
{
  const auto value = params.value(key);
  return value.isString() ? value.toString().toStdString() : defaultValue;
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

double optionalDouble(
  const QJsonObject& params, const QString& key, const double defaultValue)
{
  const auto value = params.value(key);
  return value.isDouble() ? value.toDouble(defaultValue) : defaultValue;
}

constexpr double Pi = 3.14159265358979323846;
constexpr double GeometryEpsilon = 0.001;

enum class SectorSnapMode
{
  Grid,
  Radial,
  None,
};

double degreesToRadians(const double degrees)
{
  return degrees * Pi / 180.0;
}

bool finitePositive(const double value)
{
  return std::isfinite(value) && value > 0.0;
}

QString axisName(const vm::axis::type axis)
{
  switch (axis)
  {
  case vm::axis::x:
    return "x";
  case vm::axis::y:
    return "y";
  case vm::axis::z:
    return "z";
  }
  return "unknown";
}

double polygonSignedArea(const std::vector<vm::vec2d>& points)
{
  auto area = 0.0;
  for (size_t i = 0; i < points.size(); ++i)
  {
    const auto& current = points[i];
    const auto& next = points[(i + 1) % points.size()];
    area += current.x() * next.y() - next.x() * current.y();
  }
  return area * 0.5;
}

bool isStrictlyConvexPolygon(const std::vector<vm::vec2d>& points)
{
  if (points.size() < 3)
  {
    return false;
  }

  auto sign = 0;
  for (size_t i = 0; i < points.size(); ++i)
  {
    const auto& a = points[i];
    const auto& b = points[(i + 1) % points.size()];
    const auto& c = points[(i + 2) % points.size()];
    const auto ab = b - a;
    const auto bc = c - b;
    const auto cross = ab.x() * bc.y() - ab.y() * bc.x();
    if (std::abs(cross) <= GeometryEpsilon)
    {
      return false;
    }
    const auto currentSign = cross > 0.0 ? 1 : -1;
    if (sign == 0)
    {
      sign = currentSign;
    }
    else if (sign != currentSign)
    {
      return false;
    }
  }
  return true;
}

bool nearlyEqual(
  const double lhs, const double rhs, const double epsilon = GeometryEpsilon)
{
  return std::abs(lhs - rhs) <= epsilon;
}

bool gridAligned(const double value, const double grid)
{
  if (!finitePositive(grid))
  {
    return false;
  }
  return nearlyEqual(value / grid, std::round(value / grid), 0.01);
}

bool gridAligned(const vm::vec3d& value, const double grid)
{
  return gridAligned(value.x(), grid) && gridAligned(value.y(), grid)
         && gridAligned(value.z(), grid);
}

QJsonArray vertexPositionsToJson(const std::vector<vm::vec3d>& vertices)
{
  auto result = QJsonArray{};
  for (const auto& vertex : vertices)
  {
    result.push_back(vecToJson(vertex));
  }
  return result;
}

QStringList brushMaterials(const mdl::Brush& brush)
{
  auto materials = QStringList{};
  for (const auto& face : brush.faces())
  {
    const auto material = QString::fromStdString(face.materialName());
    if (!materials.contains(material))
    {
      materials.push_back(material);
    }
  }
  return materials;
}

QStringList brushMaterialsForObjectIds(mdl::Map& map, const QJsonArray& objectIds)
{
  auto materials = QStringList{};
  for (const auto& value : objectIds)
  {
    auto* node = resolveNodeId(map.worldNode(), value.toString());
    const auto* brushNode = dynamic_cast<const mdl::BrushNode*>(node);
    if (brushNode == nullptr)
    {
      continue;
    }
    for (const auto& material : brushMaterials(brushNode->brush()))
    {
      if (!materials.contains(material))
      {
        materials.push_back(material);
      }
    }
  }
  return materials;
}

QJsonArray stringListToJsonArray(const QStringList& values)
{
  auto result = QJsonArray{};
  for (const auto& value : values)
  {
    result.push_back(value);
  }
  return result;
}

bool brushGridAligned(const mdl::Brush& brush, const double grid)
{
  return std::ranges::all_of(brush.vertexPositions(), [&](const auto& vertex) {
    return gridAligned(vertex, grid);
  });
}

void collectBrushNodes(mdl::Node& node, std::vector<mdl::BrushNode*>& brushes);

std::vector<mdl::BrushNode*> selectedBrushNodes(mdl::Map& map)
{
  auto result = std::vector<mdl::BrushNode*>{};
  for (auto* node : map.selection().nodes)
  {
    if (node != nullptr)
    {
      collectBrushNodes(*node, result);
    }
  }
  std::ranges::sort(result);
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

std::optional<QStringList> stringListFromJson(
  const QJsonObject& params, const QString& key, QString& error)
{
  const auto value = params.value(key);
  if (value.isUndefined())
  {
    return QStringList{};
  }
  if (!value.isArray())
  {
    error = QString{"%1 must be an array"}.arg(key);
    return std::nullopt;
  }

  auto result = QStringList{};
  for (const auto& entry : value.toArray())
  {
    if (!entry.isString())
    {
      error = QString{"%1 must contain only strings"}.arg(key);
      return std::nullopt;
    }
    const auto id = entry.toString().trimmed();
    if (!id.isEmpty())
    {
      result.push_back(id);
    }
  }
  result.removeDuplicates();
  return result;
}

std::optional<QStringList> operationIdsFromParams(
  const QJsonObject& params, QString& error)
{
  auto result = QStringList{};
  const auto operationId = params.value("operationId").toString().trimmed();
  if (!operationId.isEmpty())
  {
    result.push_back(operationId);
  }

  const auto operationIds = stringListFromJson(params, "operationIds", error);
  if (!operationIds)
  {
    return std::nullopt;
  }
  result.append(*operationIds);
  result.removeDuplicates();
  return result;
}

mdl::Node* resolveExternalNodeId(
  mdl::Map& map,
  const QString& objectId,
  const McpObjectRegistry* objectRegistry,
  QString& error)
{
  auto legacyPathId = objectId;
  if (objectRegistry != nullptr)
  {
    const auto resolved = objectRegistry->resolveExternalId(map, objectId);
    if (!resolved.ok)
    {
      error = resolved.error;
      return nullptr;
    }
    legacyPathId = resolved.legacyPathId;
  }

  auto* node = resolveNodeId(map.worldNode(), legacyPathId);
  if (node == nullptr)
  {
    error = QString{"Unknown MCP object id: %1"}.arg(objectId);
  }
  return node;
}

void collectBrushNodes(mdl::Node& node, std::vector<mdl::BrushNode*>& brushes)
{
  if (auto* brushNode = dynamic_cast<mdl::BrushNode*>(&node))
  {
    brushes.push_back(brushNode);
    return;
  }

  for (auto* child : node.children())
  {
    if (child != nullptr)
    {
      collectBrushNodes(*child, brushes);
    }
  }
}

std::optional<std::vector<mdl::BrushNode*>> brushNodesFromObjectIdsAndOperations(
  mdl::Map& map,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry* objectRegistry,
  QString& error)
{
  auto nodes = std::vector<mdl::Node*>{};
  const auto objectIds = stringListFromJson(params, "objectIds", error);
  if (!objectIds)
  {
    return std::nullopt;
  }
  for (const auto& objectId : *objectIds)
  {
    auto* node = resolveExternalNodeId(map, objectId, objectRegistry, error);
    if (node == nullptr)
    {
      return std::nullopt;
    }
    nodes.push_back(node);
  }

  const auto operationIds = operationIdsFromParams(params, error);
  if (!operationIds)
  {
    return std::nullopt;
  }
  for (const auto& operationId : *operationIds)
  {
    const auto operationIt = std::ranges::find_if(history, [&](const auto& operation) {
      return operation.operationId == operationId;
    });
    if (operationIt == history.end())
    {
      error = QString{"Unknown MCP operation id: %1"}.arg(operationId);
      return std::nullopt;
    }
    if (operationIt->undone)
    {
      error = QString{"MCP operation is already undone: %1"}.arg(operationId);
      return std::nullopt;
    }
    for (const auto& objectId : operationIt->changedObjectIds)
    {
      auto* node = resolveExternalNodeId(map, objectId, objectRegistry, error);
      if (node == nullptr)
      {
        return std::nullopt;
      }
      nodes.push_back(node);
    }
  }

  auto brushes = std::vector<mdl::BrushNode*>{};
  for (auto* node : nodes)
  {
    if (node != nullptr)
    {
      collectBrushNodes(*node, brushes);
    }
  }

  std::ranges::sort(brushes);
  brushes.erase(std::unique(brushes.begin(), brushes.end()), brushes.end());
  return brushes;
}

QJsonObject paramsWithSelectorObjectIds(
  mdl::Map& map,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry* objectRegistry,
  const std::map<QString, McpBrushMetadataRecord>* metadataStore,
  const std::map<QString, McpModuleRecord>* moduleStore,
  QJsonArray& warnings,
  QString& error)
{
  auto resolvedParams = params;
  if (
    !resolvedParams.value("selector").isObject()
    && !resolvedParams.value("objectIds").isArray()
    && !resolvedParams.contains("operationId")
    && !resolvedParams.contains("operationIds"))
  {
    auto implicitSelector = QJsonObject{};
    for (const auto& key : {"moduleId", "part", "role", "routeId", "order"})
    {
      if (!resolvedParams.value(key).isUndefined())
      {
        implicitSelector.insert(key, resolvedParams.value(key));
      }
    }
    if (!implicitSelector.isEmpty())
    {
      resolvedParams.insert("selector", implicitSelector);
      warnings.push_back("implicitSelectorFromTopLevelMetadata");
    }
  }
  if (!resolvedParams.value("selector").isObject())
  {
    return resolvedParams;
  }
  if (metadataStore == nullptr || moduleStore == nullptr || objectRegistry == nullptr)
  {
    error = "selector requires MCP metadata/module/object registry context";
    return {};
  }

  const auto selectorResult = selectorPreviewForMapResult(
    map,
    QJsonObject{
      {"selector", resolvedParams.value("selector").toObject()}, {"idsMode", "full"}},
    history,
    *metadataStore,
    *moduleStore,
    *objectRegistry);
  if (!selectorResult.ok)
  {
    error = selectorResult.error.message;
    return {};
  }

  auto result = resolvedParams;
  result.insert("objectIds", selectorResult.result.value("objectIds").toArray());
  result.insert("selectorMatchedCount", selectorResult.result.value("matchedCount"));
  for (const auto& warning : selectorResult.result.value("warnings").toArray())
  {
    warnings.push_back(warning);
  }
  return result;
}

bool hasExplicitBrushTargetParams(const QJsonObject& params)
{
  if (
    params.contains("selector") || params.contains("objectIds")
    || params.contains("operationIds"))
  {
    return true;
  }
  const auto operationId = params.value("operationId").toString().trimmed();
  if (!operationId.isEmpty() || params.value("operationIds").isArray())
  {
    return true;
  }
  for (const auto& key : {"moduleId", "part", "role", "routeId", "order"})
  {
    if (!params.value(key).isUndefined())
    {
      return true;
    }
  }
  return false;
}

QString scopedMetadataKey(const QString& documentFingerprint, const QString& objectId)
{
  return documentFingerprint.isEmpty()
           ? objectId
           : QString{"%1|%2"}.arg(documentFingerprint, objectId);
}

std::optional<QJsonObject> metadataForBrushNode(
  mdl::Map& map,
  mdl::BrushNode& brush,
  const std::map<QString, McpBrushMetadataRecord>* metadataStore,
  const McpObjectRegistry* objectRegistry)
{
  if (metadataStore == nullptr)
  {
    return std::nullopt;
  }
  const auto documentFingerprint = documentFingerprintForMap(map, objectRegistry);
  const auto legacyId = nodePathId(brush, map.worldNode());
  auto candidates = QStringList{legacyId};
  if (objectRegistry != nullptr)
  {
    candidates.push_front(objectRegistry->externalIdForLegacy(map, legacyId));
  }
  for (const auto& objectId : candidates)
  {
    for (const auto& key :
         QStringList{scopedMetadataKey(documentFingerprint, objectId), objectId})
    {
      const auto it = metadataStore->find(key);
      if (
        it != metadataStore->end() && !it->second.stale
        && (it->second.documentFingerprint.isEmpty() || it->second.documentFingerprint == documentFingerprint))
      {
        return it->second.metadata;
      }
    }
  }
  return std::nullopt;
}

QString objectIdForBrushNode(
  mdl::Map& map, mdl::BrushNode& brush, const McpObjectRegistry* objectRegistry)
{
  auto objectId = nodePathId(brush, map.worldNode());
  if (objectRegistry != nullptr)
  {
    objectId = objectRegistry->externalIdForLegacy(map, objectId);
  }
  return objectId;
}

std::vector<vm::vec3d> sortedVertices(std::vector<vm::vec3d> vertices)
{
  std::ranges::sort(vertices, [](const auto& lhs, const auto& rhs) {
    if (lhs.x() != rhs.x())
    {
      return lhs.x() < rhs.x();
    }
    if (lhs.y() != rhs.y())
    {
      return lhs.y() < rhs.y();
    }
    return lhs.z() < rhs.z();
  });
  return vertices;
}

std::optional<std::vector<vm::vec3d>> shellSeamVerticesFromJson(
  const QJsonObject& seam, QString& error)
{
  const auto verticesValue = seam.value("vertices");
  if (!verticesValue.isArray())
  {
    error = "shellSeams entries must include vertices";
    return std::nullopt;
  }
  auto vertices = std::vector<vm::vec3d>{};
  const auto verticesArray = verticesValue.toArray();
  if (verticesArray.isEmpty())
  {
    error = "shellSeams vertices must not be empty";
    return std::nullopt;
  }
  vertices.reserve(static_cast<size_t>(verticesArray.size()));
  for (const auto& vertexValue : verticesArray)
  {
    const auto vertex = vec3FromJsonValue(vertexValue);
    if (!vertex)
    {
      error = "shellSeams vertices must be [x, y, z] arrays";
      return std::nullopt;
    }
    vertices.push_back(*vertex);
  }
  return vertices;
}

QJsonObject shellSeamParticipantJson(const ShellSeamParticipant& participant)
{
  return QJsonObject{
    {"objectId", participant.objectId},
    {"part", participant.part},
    {"order", participant.order},
    {"vertexCount", static_cast<int>(participant.vertices.size())},
  };
}

QJsonObject shellSeamDiagnosticJson(
  const QString& key,
  const std::vector<ShellSeamParticipant>& participants,
  const double tolerance)
{
  auto participantJson = QJsonArray{};
  for (const auto& participant : participants)
  {
    participantJson.push_back(shellSeamParticipantJson(participant));
  }

  auto continuous = participants.size() == 2u;
  auto maxVertexDelta = 0.0;
  auto vertexCount =
    participants.empty() ? 0 : static_cast<int>(participants.front().vertices.size());
  auto warning = QString{};
  if (participants.size() != 2u)
  {
    warning = QString{"expected 2 participants, found %1"}.arg(participants.size());
  }
  else
  {
    const auto reference = sortedVertices(participants.front().vertices);
    for (size_t i = 1; i < participants.size(); ++i)
    {
      const auto candidate = sortedVertices(participants[i].vertices);
      if (candidate.size() != reference.size())
      {
        continuous = false;
        warning = "participant vertex counts differ";
        vertexCount = std::min(vertexCount, static_cast<int>(candidate.size()));
        continue;
      }
      for (size_t vertexIndex = 0; vertexIndex < reference.size(); ++vertexIndex)
      {
        const auto delta = vm::length(candidate[vertexIndex] - reference[vertexIndex]);
        maxVertexDelta = std::max(maxVertexDelta, delta);
        if (delta > tolerance)
        {
          continuous = false;
        }
      }
    }
  }

  auto result = QJsonObject{
    {"key", key},
    {"participantCount", static_cast<int>(participants.size())},
    {"vertexCount", vertexCount},
    {"maxVertexDelta", maxVertexDelta},
    {"continuous", continuous},
    {"participants", participantJson},
  };
  if (!warning.isEmpty())
  {
    result.insert("warning", warning);
  }
  return result;
}

QJsonObject shellSeamSummaryJson(const QJsonObject& seam)
{
  return QJsonObject{
    {"key", seam.value("key")},
    {"participantCount", seam.value("participantCount")},
    {"vertexCount", seam.value("vertexCount")},
    {"maxVertexDelta", seam.value("maxVertexDelta")},
    {"continuous", seam.value("continuous")},
    {"warning", seam.value("warning")},
  };
}

QJsonArray shellSeamSummarySample(const QJsonArray& seams, const bool failingOnly)
{
  auto result = QJsonArray{};
  for (const auto& seamValue : seams)
  {
    const auto seam = seamValue.toObject();
    if (failingOnly && seam.value("continuous").toBool(false))
    {
      continue;
    }
    result.push_back(shellSeamSummaryJson(seam));
    if (result.size() >= DefaultGeometrySampleLimit)
    {
      break;
    }
  }
  return result;
}

std::optional<vm::vec3d> optionalRouteDirectionFromParams(
  const QJsonObject& params, QString& error)
{
  if (params.value("routeDirection").isArray())
  {
    const auto routeDirection = mcpVec3FromJson(params, "routeDirection", error);
    if (!routeDirection)
    {
      return std::nullopt;
    }
    if (vm::is_zero(vm::vec2d{routeDirection->x(), routeDirection->y()}, GeometryEpsilon))
    {
      error = "routeDirection must have non-zero X/Y components";
      return std::nullopt;
    }
    return vm::normalize(vm::vec3d{routeDirection->x(), routeDirection->y(), 0.0});
  }

  if (params.value("start").isArray() || params.value("end").isArray())
  {
    const auto start = mcpVec3FromJson(params, "start", error);
    if (!start)
    {
      return std::nullopt;
    }
    const auto end = mcpVec3FromJson(params, "end", error);
    if (!end)
    {
      return std::nullopt;
    }
    const auto direction = *end - *start;
    if (vm::is_zero(vm::vec2d{direction.x(), direction.y()}, GeometryEpsilon))
    {
      error = "start/end route direction must have non-zero X/Y delta";
      return std::nullopt;
    }
    return vm::normalize(vm::vec3d{direction.x(), direction.y(), 0.0});
  }

  return std::nullopt;
}

double optionalClampedDouble(
  const QJsonObject& params,
  const QString& key,
  const double defaultValue,
  const double minValue,
  const double maxValue)
{
  const auto value = params.value(key);
  if (!value.isDouble())
  {
    return defaultValue;
  }
  return std::clamp(value.toDouble(defaultValue), minValue, maxValue);
}

vm::bbox3d faceBounds(const mdl::BrushFace& face)
{
  const auto vertices = face.vertexPositions();
  if (vertices.empty())
  {
    return vm::bbox3d{};
  }

  auto bounds = vm::bbox3d{vertices.front(), vertices.front()};
  for (const auto& vertex : vertices)
  {
    bounds = vm::merge(bounds, vertex);
  }
  return bounds;
}

double faceProjectedSpan(const mdl::BrushFace& face, const vm::vec3d& direction)
{
  auto minProjection = std::numeric_limits<double>::max();
  auto maxProjection = std::numeric_limits<double>::lowest();
  for (const auto& vertex : face.vertexPositions())
  {
    const auto projection = vm::dot(vertex, direction);
    minProjection = std::min(minProjection, projection);
    maxProjection = std::max(maxProjection, projection);
  }
  if (minProjection == std::numeric_limits<double>::max())
  {
    return 0.0;
  }
  return std::max(0.0, maxProjection - minProjection);
}

QJsonObject slopeFaceJson(
  const mdl::BrushNode& brushNode,
  const mdl::BrushFace& face,
  const size_t faceIndex,
  const std::optional<vm::vec3d>& routeDirection,
  const mdl::WorldNode& worldNode)
{
  const auto normal = face.normal();
  const auto slopeDegrees = std::acos(std::clamp(normal.z(), -1.0, 1.0)) * 180.0 / Pi;
  auto riseDirection = vm::vec3d{};
  auto riseDirectionConfidence = "none";
  if (normal.z() > GeometryEpsilon)
  {
    const auto uphill = vm::vec2d{-normal.x(), -normal.y()};
    if (!vm::is_zero(uphill, GeometryEpsilon))
    {
      const auto normalized = vm::normalize(uphill);
      riseDirection = vm::vec3d{normalized.x(), normalized.y(), 0.0};
      riseDirectionConfidence = "high";
    }
  }

  auto classification = QString{"unknown_direction"};
  auto confidence = QString{"low"};
  auto heightDeltaAlongRoute = 0.0;
  if (routeDirection && !vm::is_zero(riseDirection, GeometryEpsilon))
  {
    const auto alignment = vm::dot(riseDirection, *routeDirection);
    const auto span = faceProjectedSpan(face, *routeDirection);
    heightDeltaAlongRoute = std::tan(slopeDegrees * Pi / 180.0) * alignment * span;
    if (alignment > 0.15)
    {
      classification = "ascending";
    }
    else if (alignment < -0.15)
    {
      classification = "descending";
    }
    else
    {
      classification = "cross_slope";
    }
    confidence = "high";
  }

  auto result = QJsonObject{
    {"objectId", nodePathId(brushNode, worldNode)},
    {"faceIndex", static_cast<int>(faceIndex)},
    {"normal", vecToJson(normal)},
    {"bounds", boundsToJson(faceBounds(face))},
    {"slopeDegrees", slopeDegrees},
    {"riseDirection", vecToJson(riseDirection)},
    {"riseDirectionConfidence", riseDirectionConfidence},
    {"heightDeltaAlongRoute", heightDeltaAlongRoute},
    {"classification", classification},
    {"confidence", confidence},
    {"material", QString::fromStdString(face.materialName())},
  };
  if (routeDirection)
  {
    result.insert("routeDirection", vecToJson(*routeDirection));
  }
  return result;
}

struct PlayableSurface
{
  mdl::BrushNode* brush = nullptr;
  QString objectId;
  size_t faceIndex = 0u;
  vm::vec3d normal = vm::vec3d{};
  vm::vec3d routeDirection = vm::vec3d{};
  vm::bbox3d bounds = vm::bbox3d{};
  std::vector<vm::vec3d> vertices;
  double slopeDegrees = 0.0;
  double minProjection = 0.0;
  double maxProjection = 0.0;
  double entryZ = 0.0;
  double exitZ = 0.0;
  double averageZ = 0.0;
  std::optional<double> metadataOrder;
};

double averageZAtProjection(
  const std::vector<vm::vec3d>& vertices,
  const vm::vec3d& routeDirection,
  const double targetProjection,
  const double tolerance)
{
  auto sum = 0.0;
  auto count = 0;
  auto bestDistance = std::numeric_limits<double>::max();
  auto bestZ = 0.0;
  for (const auto& vertex : vertices)
  {
    const auto distance = std::abs(vm::dot(vertex, routeDirection) - targetProjection);
    if (distance < bestDistance)
    {
      bestDistance = distance;
      bestZ = vertex.z();
    }
    if (distance <= tolerance)
    {
      sum += vertex.z();
      ++count;
    }
  }
  return count > 0 ? sum / static_cast<double>(count) : bestZ;
}

std::optional<PlayableSurface> playableSurfaceForBrush(
  mdl::BrushNode& brushNode,
  const vm::vec3d& routeDirection,
  const mdl::WorldNode& worldNode,
  const double minUpNormal)
{
  auto topVertices = std::vector<vm::vec3d>{};
  auto topBounds = std::optional<vm::bbox3d>{};
  auto bestFaceIndex = size_t{0};
  auto bestNormal = vm::vec3d{};
  auto bestSpan = -1.0;
  auto bestAverageZ = std::numeric_limits<double>::lowest();
  const auto& faces = brushNode.brush().faces();
  for (size_t faceIndex = 0; faceIndex < faces.size(); ++faceIndex)
  {
    const auto& face = faces[faceIndex];
    const auto normal = face.normal();
    if (normal.z() < minUpNormal)
    {
      continue;
    }

    const auto vertices = face.vertexPositions();
    if (vertices.empty())
    {
      continue;
    }

    auto minProjection = std::numeric_limits<double>::max();
    auto maxProjection = std::numeric_limits<double>::lowest();
    auto averageZ = 0.0;
    for (const auto& vertex : vertices)
    {
      const auto duplicate = std::ranges::any_of(topVertices, [&](const auto& existing) {
        return vm::squared_length(existing - vertex) <= GeometryEpsilon * GeometryEpsilon;
      });
      if (!duplicate)
      {
        topVertices.push_back(vertex);
      }
      topBounds = topBounds ? vm::merge(*topBounds, vertex)
                            : std::optional<vm::bbox3d>{vm::bbox3d{vertex, vertex}};
      const auto projection = vm::dot(vertex, routeDirection);
      minProjection = std::min(minProjection, projection);
      maxProjection = std::max(maxProjection, projection);
      averageZ += vertex.z();
    }
    averageZ /= static_cast<double>(vertices.size());

    const auto span = std::max(0.0, maxProjection - minProjection);
    if (
      span > bestSpan + GeometryEpsilon
      || (nearlyEqual(span, bestSpan) && averageZ > bestAverageZ))
    {
      bestFaceIndex = faceIndex;
      bestNormal = normal;
      bestSpan = span;
      bestAverageZ = averageZ;
    }
  }

  if (topVertices.empty() || !topBounds)
  {
    return std::nullopt;
  }

  auto minProjection = std::numeric_limits<double>::max();
  auto maxProjection = std::numeric_limits<double>::lowest();
  auto averageZ = 0.0;
  for (const auto& vertex : topVertices)
  {
    const auto projection = vm::dot(vertex, routeDirection);
    minProjection = std::min(minProjection, projection);
    maxProjection = std::max(maxProjection, projection);
    averageZ += vertex.z();
  }
  averageZ /= static_cast<double>(topVertices.size());
  const auto span = std::max(0.0, maxProjection - minProjection);
  const auto projectionTolerance = std::max(0.01, span * 0.01);

  auto surface = PlayableSurface{};
  surface.brush = &brushNode;
  surface.objectId = nodePathId(brushNode, worldNode);
  surface.faceIndex = bestFaceIndex;
  surface.normal = bestNormal;
  surface.routeDirection = routeDirection;
  surface.bounds = *topBounds;
  surface.vertices = std::move(topVertices);
  surface.slopeDegrees = std::acos(std::clamp(bestNormal.z(), -1.0, 1.0)) * 180.0 / Pi;
  surface.minProjection = minProjection;
  surface.maxProjection = maxProjection;
  surface.entryZ = averageZAtProjection(
    surface.vertices, routeDirection, minProjection, projectionTolerance);
  surface.exitZ = averageZAtProjection(
    surface.vertices, routeDirection, maxProjection, projectionTolerance);
  surface.averageZ = averageZ;
  return surface;
}

QJsonObject playableSurfaceJson(const PlayableSurface& surface)
{
  return QJsonObject{
    {"objectId", surface.objectId},
    {"faceIndex", static_cast<int>(surface.faceIndex)},
    {"normal", vecToJson(surface.normal)},
    {"bounds", boundsToJson(surface.bounds)},
    {"slopeDegrees", surface.slopeDegrees},
    {"entryZ", surface.entryZ},
    {"exitZ", surface.exitZ},
    {"minProjection", surface.minProjection},
    {"maxProjection", surface.maxProjection},
    {"topVertexCount", static_cast<int>(surface.vertices.size())},
  };
}

struct SeamEdge
{
  bool valid = false;
  vm::vec3d minSidePoint = vm::vec3d{};
  vm::vec3d maxSidePoint = vm::vec3d{};
  double minSideProjection = 0.0;
  double maxSideProjection = 0.0;
};

double distance2D(const vm::vec3d& lhs, const vm::vec3d& rhs)
{
  const auto delta = vm::vec2d{lhs.x() - rhs.x(), lhs.y() - rhs.y()};
  return vm::length(delta);
}

SeamEdge seamEdgeAtProjection(const PlayableSurface& surface, const bool exitEdge)
{
  const auto targetProjection = exitEdge ? surface.maxProjection : surface.minProjection;
  const auto span = std::max(0.0, surface.maxProjection - surface.minProjection);
  const auto tolerance = std::max(0.01, span * 0.02);
  const auto perpendicular =
    vm::vec3d{-surface.routeDirection.y(), surface.routeDirection.x(), 0.0};

  auto candidates = std::vector<vm::vec3d>{};
  for (const auto& vertex : surface.vertices)
  {
    if (std::abs(vm::dot(vertex, surface.routeDirection) - targetProjection) <= tolerance)
    {
      candidates.push_back(vertex);
    }
  }

  if (candidates.size() < 2u)
  {
    auto sorted = surface.vertices;
    std::ranges::sort(sorted, [&](const auto& lhs, const auto& rhs) {
      const auto lhsDistance =
        std::abs(vm::dot(lhs, surface.routeDirection) - targetProjection);
      const auto rhsDistance =
        std::abs(vm::dot(rhs, surface.routeDirection) - targetProjection);
      return lhsDistance < rhsDistance;
    });
    candidates.clear();
    for (const auto& vertex : sorted)
    {
      const auto duplicate = std::ranges::any_of(candidates, [&](const auto& existing) {
        return vm::squared_length(existing - vertex) <= GeometryEpsilon * GeometryEpsilon;
      });
      if (!duplicate)
      {
        candidates.push_back(vertex);
      }
      if (candidates.size() == 2u)
      {
        break;
      }
    }
  }

  if (candidates.size() < 2u)
  {
    return {};
  }

  auto result = SeamEdge{};
  result.valid = true;
  result.minSidePoint = candidates.front();
  result.maxSidePoint = candidates.front();
  result.minSideProjection = vm::dot(candidates.front(), perpendicular);
  result.maxSideProjection = result.minSideProjection;
  for (const auto& vertex : candidates)
  {
    const auto sideProjection = vm::dot(vertex, perpendicular);
    if (sideProjection < result.minSideProjection)
    {
      result.minSideProjection = sideProjection;
      result.minSidePoint = vertex;
    }
    if (sideProjection > result.maxSideProjection)
    {
      result.maxSideProjection = sideProjection;
      result.maxSidePoint = vertex;
    }
  }
  return result;
}

QJsonObject seamEdgeJson(
  const SeamEdge& fromEdge,
  const SeamEdge& toEdge,
  const QString& classification,
  const double horizontalTolerance,
  const double verticalTolerance)
{
  if (!fromEdge.valid || !toEdge.valid)
  {
    return QJsonObject{
      {"available", false},
      {"fullWidthContinuous", false},
    };
  }

  const auto minSideGap = distance2D(fromEdge.minSidePoint, toEdge.minSidePoint);
  const auto maxSideGap = distance2D(fromEdge.maxSidePoint, toEdge.maxSidePoint);
  const auto minSideVerticalStep = toEdge.minSidePoint.z() - fromEdge.minSidePoint.z();
  const auto maxSideVerticalStep = toEdge.maxSidePoint.z() - fromEdge.maxSidePoint.z();
  const auto edgeGapMax = std::max(minSideGap, maxSideGap);
  const auto edgeVerticalStepMax =
    std::max(std::abs(minSideVerticalStep), std::abs(maxSideVerticalStep));
  const auto overlapAccepted = classification == "overlap_continuous_height";
  const auto fullWidthContinuous = (overlapAccepted || edgeGapMax <= horizontalTolerance)
                                   && edgeVerticalStepMax <= verticalTolerance;

  return QJsonObject{
    {"available", true},
    {"fromMinSidePoint", vecToJson(fromEdge.minSidePoint)},
    {"fromMaxSidePoint", vecToJson(fromEdge.maxSidePoint)},
    {"toMinSidePoint", vecToJson(toEdge.minSidePoint)},
    {"toMaxSidePoint", vecToJson(toEdge.maxSidePoint)},
    {"minSideGap", minSideGap},
    {"maxSideGap", maxSideGap},
    {"innerEdgeGap", minSideGap},
    {"outerEdgeGap", maxSideGap},
    {"edgeGapMax", edgeGapMax},
    {"minSideVerticalStep", minSideVerticalStep},
    {"maxSideVerticalStep", maxSideVerticalStep},
    {"edgeVerticalStepMax", edgeVerticalStepMax},
    {"fullWidthContinuous", fullWidthContinuous},
  };
}

QString seamClassification(
  const double horizontalGap,
  const double verticalStep,
  const double horizontalTolerance,
  const double verticalTolerance)
{
  if (horizontalGap > horizontalTolerance)
  {
    return "horizontal_gap";
  }
  if (verticalStep > verticalTolerance)
  {
    return "step_up";
  }
  if (verticalStep < -verticalTolerance)
  {
    return "step_down";
  }
  if (horizontalGap < -horizontalTolerance)
  {
    return "overlap_continuous_height";
  }
  return "continuous";
}

QJsonObject seamJson(
  const PlayableSurface& from,
  const PlayableSurface& to,
  const size_t seamIndex,
  const double horizontalTolerance,
  const double verticalTolerance)
{
  const auto horizontalGap = to.minProjection - from.maxProjection;
  const auto positiveGap = std::max(0.0, horizontalGap);
  const auto overlapDepth = std::max(0.0, -horizontalGap);
  const auto seamRelation = horizontalGap > horizontalTolerance    ? "gap"
                            : horizontalGap < -horizontalTolerance ? "overlap"
                                                                   : "touching";
  const auto verticalStep = to.entryZ - from.exitZ;
  const auto classification = seamClassification(
    horizontalGap, verticalStep, horizontalTolerance, verticalTolerance);
  const auto centerlineContinuous =
    classification == "continuous" || classification == "overlap_continuous_height";
  const auto edge = seamEdgeJson(
    seamEdgeAtProjection(from, true),
    seamEdgeAtProjection(to, false),
    classification,
    horizontalTolerance,
    verticalTolerance);
  const auto fullWidthContinuous = edge.value("fullWidthContinuous").toBool(false);
  const auto continuous = centerlineContinuous && fullWidthContinuous;
  return QJsonObject{
    {"seamIndex", static_cast<int>(seamIndex)},
    {"fromObjectId", from.objectId},
    {"toObjectId", to.objectId},
    {"fromFaceIndex", static_cast<int>(from.faceIndex)},
    {"toFaceIndex", static_cast<int>(to.faceIndex)},
    {"fromExitZ", from.exitZ},
    {"toEntryZ", to.entryZ},
    {"verticalStep", verticalStep},
    {"horizontalGap", horizontalGap},
    {"seamRelation", seamRelation},
    {"positiveGap", positiveGap},
    {"overlapDepth", overlapDepth},
    {"classification", classification},
    {"centerlineContinuous", centerlineContinuous},
    {"fullWidthContinuous", fullWidthContinuous},
    {"edgeGapMax", edge.value("edgeGapMax")},
    {"edgeEndpointDistanceMax", edge.value("edgeGapMax")},
    {"innerEdgeGap", edge.value("innerEdgeGap")},
    {"outerEdgeGap", edge.value("outerEdgeGap")},
    {"edgeVerticalStepMax", edge.value("edgeVerticalStepMax")},
    {"edge", edge},
    {"continuous", continuous},
  };
}

QJsonObject seamSummaryJson(const QJsonObject& seam)
{
  auto result = QJsonObject{
    {"seamIndex", seam.value("seamIndex")},
    {"fromObjectId", seam.value("fromObjectId")},
    {"toObjectId", seam.value("toObjectId")},
    {"classification", seam.value("classification")},
    {"continuous", seam.value("continuous")},
    {"semanticContinuous", seam.value("semanticContinuous")},
    {"centerlineContinuous", seam.value("centerlineContinuous")},
    {"fullWidthContinuous", seam.value("fullWidthContinuous")},
    {"verticalStep", seam.value("verticalStep")},
    {"horizontalGap", seam.value("horizontalGap")},
    {"seamRelation", seam.value("seamRelation")},
    {"positiveGap", seam.value("positiveGap")},
    {"overlapDepth", seam.value("overlapDepth")},
    {"edgeGapMax", seam.value("edgeGapMax")},
    {"edgeEndpointDistanceMax", seam.value("edgeEndpointDistanceMax")},
    {"edgeVerticalStepMax", seam.value("edgeVerticalStepMax")},
  };
  if (seam.value("loopClosure").toBool(false))
  {
    result.insert("loopClosure", true);
  }
  return result;
}

QJsonArray seamSummarySample(
  const QJsonArray& seams,
  const bool onlySemanticFailures,
  const int limit = DefaultGeometrySampleLimit)
{
  auto result = QJsonArray{};
  const auto maxCount = std::max(0, limit);
  for (const auto& seamValue : seams)
  {
    const auto seam = seamValue.toObject();
    if (onlySemanticFailures && seam.value("semanticContinuous").toBool(false))
    {
      continue;
    }
    result.push_back(seamSummaryJson(seam));
    if (result.size() >= maxCount)
    {
      break;
    }
  }
  return result;
}

bool seamSemanticallyContinuous(
  const QJsonObject& seam,
  const QString& continuityMode,
  const double maxStepHeight,
  const double maxJumpGap)
{
  if (seam.value("continuous").toBool(false))
  {
    return true;
  }
  const auto classification = seam.value("classification").toString();
  const auto verticalStep = seam.value("verticalStep").toDouble();
  const auto horizontalGap = seam.value("horizontalGap").toDouble();
  if (
    continuityMode == "stepped"
    && (classification == "step_up" || classification == "step_down")
    && std::abs(verticalStep) <= maxStepHeight)
  {
    return true;
  }
  if (
    continuityMode == "jump_gaps" && classification == "horizontal_gap"
    && horizontalGap <= maxJumpGap)
  {
    return true;
  }
  if (
    continuityMode == "jump_chain" && classification == "horizontal_gap"
    && horizontalGap <= maxJumpGap)
  {
    return true;
  }
  return false;
}

vm::vec3d routeDirectionFromBrushCenters(
  const std::vector<mdl::BrushNode*>& brushes, QString& warning)
{
  if (brushes.size() < 2u)
  {
    return vm::vec3d{};
  }

  const auto start = brushes.front()->logicalBounds().center();
  const auto end = brushes.back()->logicalBounds().center();
  const auto delta = end - start;
  if (vm::is_zero(vm::vec2d{delta.x(), delta.y()}, GeometryEpsilon))
  {
    warning =
      "routeDirectionUnavailable: provide routeDirection or start/end for continuity "
      "analysis.";
    return vm::vec3d{};
  }

  warning =
    "lowConfidence: routeDirection was inferred from first/last target centers; pass "
    "routeDirection or start/end for route-critical validation.";
  return vm::normalize(vm::vec3d{delta.x(), delta.y(), 0.0});
}

std::vector<mdl::BrushNode*> brushNodesFromOperationId(
  mdl::Map& map,
  const QString& operationId,
  const std::vector<McpOperationRecord>& history,
  QString& error)
{
  const auto operationIt = std::ranges::find_if(
    history, [&](const auto& operation) { return operation.operationId == operationId; });
  if (operationIt == history.end())
  {
    error = QString{"Unknown MCP operation id: %1"}.arg(operationId);
    return {};
  }

  auto result = std::vector<mdl::BrushNode*>{};
  for (const auto& id : operationIt->changedObjectIds)
  {
    auto* node = resolveNodeId(map.worldNode(), id);
    if (auto* brushNode = dynamic_cast<mdl::BrushNode*>(node))
    {
      result.push_back(brushNode);
    }
  }
  return result;
}

vm::vec2d polarPoint(
  const vm::vec2d& center, const double radius, const double angleDegrees)
{
  const auto radians = degreesToRadians(angleDegrees);
  return center + vm::vec2d{std::cos(radians) * radius, std::sin(radians) * radius};
}

double snapToGrid(const double value, const double grid = 1.0)
{
  return finitePositive(grid) ? std::round(value / grid) * grid : value;
}

vm::vec2d snapToGrid(const vm::vec2d& value, const double grid = 1.0)
{
  return vm::vec2d{snapToGrid(value.x(), grid), snapToGrid(value.y(), grid)};
}

vm::vec3d snapToGrid(const vm::vec3d& value, const double grid = 1.0)
{
  return vm::vec3d{
    snapToGrid(value.x(), grid),
    snapToGrid(value.y(), grid),
    snapToGrid(value.z(), grid)};
}

std::vector<vm::vec2d> snapPointsToGrid(
  const std::vector<vm::vec2d>& points, const double grid)
{
  auto result = std::vector<vm::vec2d>{};
  result.reserve(points.size());
  for (const auto& point : points)
  {
    result.push_back(snapToGrid(point, grid));
  }
  return result;
}

std::vector<vm::vec3d> snapPointsToGrid(
  const std::vector<vm::vec3d>& points, const double grid)
{
  auto result = std::vector<vm::vec3d>{};
  result.reserve(points.size());
  for (const auto& point : points)
  {
    result.push_back(snapToGrid(point, grid));
  }
  return result;
}

std::optional<SectorSnapMode> sectorSnapModeFromJson(
  const QJsonObject& params,
  const QString& key,
  const SectorSnapMode defaultValue,
  QString& error)
{
  const auto value = params.value(key);
  if (value.isUndefined())
  {
    return defaultValue;
  }
  if (!value.isString())
  {
    error = QString{"%1 must be grid, radial, or none"}.arg(key);
    return std::nullopt;
  }

  const auto mode = value.toString().trimmed().toLower();
  if (mode == "grid")
  {
    return SectorSnapMode::Grid;
  }
  if (mode == "radial")
  {
    return SectorSnapMode::Radial;
  }
  if (mode == "none")
  {
    return SectorSnapMode::None;
  }

  error = QString{"%1 must be grid, radial, or none"}.arg(key);
  return std::nullopt;
}

std::optional<vm::bbox3d> snapBoundsToGrid(
  const vm::bbox3d& bounds, const double grid, QString& error)
{
  const auto min = snapToGrid(bounds.min, grid);
  const auto max = snapToGrid(bounds.max, grid);
  if (min.x() >= max.x() || min.y() >= max.y() || min.z() >= max.z())
  {
    error =
      "bounds collapse after grid snapping; use larger dimensions or a smaller grid";
    return std::nullopt;
  }
  return vm::bbox3d{min, max};
}

std::optional<mdl::Brush> createPrismBrush(
  const mdl::BrushBuilder& builder,
  std::vector<vm::vec2d> points,
  const double minZ,
  const double maxZ,
  const std::string& material,
  QString& error)
{
  if (!std::isfinite(minZ) || !std::isfinite(maxZ) || minZ >= maxZ)
  {
    error = "minZ must be smaller than maxZ";
    return std::nullopt;
  }
  if (!isStrictlyConvexPolygon(points))
  {
    error = "points2d must form a strictly convex polygon";
    return std::nullopt;
  }

  if (polygonSignedArea(points) < 0.0)
  {
    std::reverse(points.begin(), points.end());
  }

  auto vertices = std::vector<vm::vec3d>{};
  vertices.reserve(points.size() * 2u);
  for (const auto& point : points)
  {
    vertices.emplace_back(point.x(), point.y(), minZ);
  }
  for (const auto& point : points)
  {
    vertices.emplace_back(point.x(), point.y(), maxZ);
  }

  auto brush = builder.createBrush(vertices, material);
  if (brush.is_error())
  {
    error = "Could not create prism brush from points2d";
    return std::nullopt;
  }
  return std::move(brush).value();
}

std::optional<std::vector<vm::vec2d>> sectorPolygon(
  const vm::vec2d& center,
  const double innerRadius,
  const double outerRadius,
  const double startAngle,
  const double endAngle,
  QString& error)
{
  if (!finitePositive(innerRadius) || !finitePositive(outerRadius))
  {
    error = "innerRadius and outerRadius must be greater than zero";
    return std::nullopt;
  }
  if (innerRadius >= outerRadius)
  {
    error = "innerRadius must be smaller than outerRadius";
    return std::nullopt;
  }
  if (!std::isfinite(startAngle) || !std::isfinite(endAngle))
  {
    error = "startAngle and endAngle must be finite";
    return std::nullopt;
  }

  const auto span = endAngle - startAngle;
  if (std::abs(span) <= GeometryEpsilon || std::abs(span) > 180.0)
  {
    error = "sector angle must be greater than zero and at most 180 degrees";
    return std::nullopt;
  }

  if (span > 0.0)
  {
    return std::vector<vm::vec2d>{
      polarPoint(center, innerRadius, startAngle),
      polarPoint(center, outerRadius, startAngle),
      polarPoint(center, outerRadius, endAngle),
      polarPoint(center, innerRadius, endAngle),
    };
  }

  return std::vector<vm::vec2d>{
    polarPoint(center, innerRadius, endAngle),
    polarPoint(center, outerRadius, endAngle),
    polarPoint(center, outerRadius, startAngle),
    polarPoint(center, innerRadius, startAngle),
  };
}

vm::vec2d snapSectorPoint(
  const vm::vec2d& center,
  const double radius,
  const double angleDegrees,
  const SectorSnapMode snapMode,
  const double grid)
{
  const auto point = polarPoint(center, radius, angleDegrees);
  if (snapMode == SectorSnapMode::Radial || snapMode == SectorSnapMode::None)
  {
    return point;
  }

  const auto snapped = snapToGrid(point, grid);
  return snapped;
}

double arcChordLength(const double radius, const double angleDegrees)
{
  const auto radians = std::abs(angleDegrees) * Pi / 180.0;
  return 2.0 * radius * std::sin(radians * 0.5);
}

double arcSagitta(const double radius, const double angleDegrees)
{
  const auto radians = std::abs(angleDegrees) * Pi / 180.0;
  return radius * (1.0 - std::cos(radians * 0.5));
}

int suggestedCurveSegments(
  const double turnDegrees, const double radius, const McpQualityPolicy& policy)
{
  auto suggested = std::max(
    1,
    static_cast<int>(
      std::ceil(std::abs(turnDegrees) / policy.maxDirectionChangeDegrees)));
  if (radius > GeometryEpsilon && policy.maxSagitta < radius)
  {
    const auto maxHalfAngle =
      std::acos(std::clamp(1.0 - policy.maxSagitta / radius, -1.0, 1.0));
    if (maxHalfAngle > GeometryEpsilon)
    {
      const auto maxSegmentAngleDegrees = 2.0 * maxHalfAngle * 180.0 / Pi;
      suggested = std::max(
        suggested,
        static_cast<int>(std::ceil(std::abs(turnDegrees) / maxSegmentAngleDegrees)));
    }
  }
  return suggested;
}

double curveSnapDisplacement(
  const QJsonObject& operation,
  const double innerRadius,
  const double outerRadius,
  const double startAngle,
  const double turnDegrees,
  const int segments,
  const double grid)
{
  if (operation.value("snapMode").toString("radial").trimmed().toLower() != "grid")
  {
    return 0.0;
  }
  const auto centerValue = operation.value("center").toArray();
  if (centerValue.size() < 2)
  {
    return 0.0;
  }
  const auto center = vm::vec2d{centerValue[0].toDouble(), centerValue[1].toDouble()};
  auto result = 0.0;
  for (auto i = 0; i <= segments; ++i)
  {
    const auto angle =
      startAngle + turnDegrees * static_cast<double>(i) / static_cast<double>(segments);
    for (const auto radius : {innerRadius, outerRadius})
    {
      const auto ideal = polarPoint(center, radius, angle);
      const auto snapped = snapToGrid(ideal, grid);
      result = std::max(result, vm::length(snapped - ideal));
    }
  }
  return result;
}

std::optional<QJsonObject> curveQualityForOperation(
  const QJsonObject& operation,
  const int operationIndex,
  const double grid,
  const McpQualityPolicy& policy)
{
  const auto type = operation.value("type").toString().trimmed().toLower();
  auto innerRadius = 0.0;
  auto outerRadius = 0.0;
  auto startAngle = optionalDouble(operation, "startAngle", 0.0);
  auto turnDegrees = optionalDouble(operation, "turnDegrees", 90.0);
  auto segments = static_cast<int>(optionalSize(operation, "segments", 12));
  auto estimatedBrushCount = segments;
  auto snapDisplacement = 0.0;
  if (type == "curved_corridor")
  {
    innerRadius = optionalDouble(operation, "innerRadius", 128.0);
    outerRadius = optionalDouble(operation, "outerRadius", 224.0);
    estimatedBrushCount = segments * 4;
    const auto caps = operation.value("caps").toString("none").trimmed().toLower();
    if (caps == "start" || caps == "end")
    {
      ++estimatedBrushCount;
    }
    else if (caps == "both")
    {
      estimatedBrushCount += 2;
    }
    snapDisplacement = curveSnapDisplacement(
      operation, innerRadius, outerRadius, startAngle, turnDegrees, segments, grid);
  }
  else if (type == "arc_ramp" || type == "helical_ramp")
  {
    const auto radius = optionalDouble(operation, "radius", 256.0);
    const auto width = optionalDouble(operation, "width", 128.0);
    innerRadius = radius - width * 0.5;
    outerRadius = radius + width * 0.5;
  }
  else
  {
    return std::nullopt;
  }

  if (
    segments <= 0 || !finitePositive(innerRadius) || !finitePositive(outerRadius)
    || !std::isfinite(turnDegrees))
  {
    return std::nullopt;
  }

  const auto anglePerSegment = std::abs(turnDegrees) / static_cast<double>(segments);
  const auto maxSagitta = arcSagitta(outerRadius, anglePerSegment);
  const auto exceeds = anglePerSegment > policy.maxDirectionChangeDegrees
                       || maxSagitta > policy.maxSagitta
                       || snapDisplacement > policy.maxSnapDisplacement;
  const auto status = exceeds ? (policy.strict() ? "failed" : "warning") : "passed";
  const auto suggestedSegments = suggestedCurveSegments(turnDegrees, outerRadius, policy);
  return QJsonObject{
    {"operationIndex", operationIndex},
    {"type", type},
    {"segments", segments},
    {"anglePerSegmentDegrees", anglePerSegment},
    {"innerChordLength", arcChordLength(innerRadius, anglePerSegment)},
    {"outerChordLength", arcChordLength(outerRadius, anglePerSegment)},
    {"maxSagitta", maxSagitta},
    {"maxSnapDisplacement", snapDisplacement},
    {"estimatedBrushCount", estimatedBrushCount},
    {"estimatedFaceCount", estimatedBrushCount * 6},
    {"suggestedSegments", suggestedSegments},
    {"unachievableWithinSegmentLimit", suggestedSegments > 128},
    {"qualityStatus", status},
  };
}

QJsonObject curveQualityForOperations(
  const QJsonArray& operations,
  const double grid,
  const McpQualityPolicy& policy,
  QJsonArray& warnings)
{
  auto samples = QJsonArray{};
  auto count = 0;
  auto maxDirectionChange = 0.0;
  auto maxSagitta = 0.0;
  auto maxSnapDisplacement = 0.0;
  auto suggestedSegments = 0;
  auto hasWarning = false;
  auto hasFailure = false;
  auto unachievable = false;
  for (auto i = 0; i < operations.size(); ++i)
  {
    if (!operations[i].isObject())
    {
      continue;
    }
    const auto metric =
      curveQualityForOperation(operations[i].toObject(), i, grid, policy);
    if (!metric)
    {
      continue;
    }
    ++count;
    maxDirectionChange =
      std::max(maxDirectionChange, metric->value("anglePerSegmentDegrees").toDouble());
    maxSagitta = std::max(maxSagitta, metric->value("maxSagitta").toDouble());
    maxSnapDisplacement =
      std::max(maxSnapDisplacement, metric->value("maxSnapDisplacement").toDouble());
    suggestedSegments =
      std::max(suggestedSegments, metric->value("suggestedSegments").toInt());
    unachievable =
      unachievable || metric->value("unachievableWithinSegmentLimit").toBool();
    hasFailure = hasFailure || metric->value("qualityStatus").toString() == "failed";
    hasWarning = hasWarning || metric->value("qualityStatus").toString() == "warning";
    if (samples.size() < DefaultGeometrySampleLimit)
    {
      samples.push_back(*metric);
    }
  }

  const auto status = count == 0   ? QString{"not_evaluated"}
                      : hasFailure ? QString{"failed"}
                      : hasWarning ? QString{"warning"}
                                   : QString{"passed"};
  if (status == "warning" || status == "failed")
  {
    warnings.push_back(
      QString{"curveQuality%1: one or more curved operations exceed the %2 quality "
              "policy; inspect curveQuality.operationSample and suggestedSegments."}
        .arg(status == "failed" ? "Failed" : "Warning", policy.intent));
  }

  return QJsonObject{
    {"qualityPolicy", qualityPolicyJson(policy)},
    {"qualityStatus", status},
    {"acceptancePassed", status != "failed"},
    {"curveOperationCount", count},
    {"maxDirectionChangeDegrees", maxDirectionChange},
    {"maxSagitta", maxSagitta},
    {"maxSnapDisplacement", maxSnapDisplacement},
    {"suggestedSegments", suggestedSegments},
    {"unachievableWithinSegmentLimit", unachievable},
    {"evaluatedMetrics",
     count > 0 ? QJsonArray{"direction_change", "sagitta", "snap_displacement"}
               : QJsonArray{}},
    {"unavailableMetrics",
     count > 0 ? QJsonArray{}
               : QJsonArray{"direction_change", "sagitta", "snap_displacement"}},
    {"operationSample", samples},
  };
}

std::optional<std::vector<vm::vec2d>> sectorPolygon(
  const vm::vec2d& center,
  const double innerRadius,
  const double outerRadius,
  const double startAngle,
  const double endAngle,
  const SectorSnapMode snapMode,
  const double grid,
  QString& error)
{
  auto polygon =
    sectorPolygon(center, innerRadius, outerRadius, startAngle, endAngle, error);
  if (!polygon || snapMode == SectorSnapMode::None)
  {
    return polygon;
  }

  if (snapMode == SectorSnapMode::Grid)
  {
    return snapPointsToGrid(*polygon, grid);
  }

  const auto span = endAngle - startAngle;
  if (span > 0.0)
  {
    return std::vector<vm::vec2d>{
      snapSectorPoint(center, innerRadius, startAngle, snapMode, grid),
      snapSectorPoint(center, outerRadius, startAngle, snapMode, grid),
      snapSectorPoint(center, outerRadius, endAngle, snapMode, grid),
      snapSectorPoint(center, innerRadius, endAngle, snapMode, grid),
    };
  }

  return std::vector<vm::vec2d>{
    snapSectorPoint(center, innerRadius, endAngle, snapMode, grid),
    snapSectorPoint(center, outerRadius, endAngle, snapMode, grid),
    snapSectorPoint(center, outerRadius, startAngle, snapMode, grid),
    snapSectorPoint(center, innerRadius, startAngle, snapMode, grid),
  };
}

std::optional<mdl::Brush> createCylinderSectorBrush(
  const mdl::BrushBuilder& builder,
  const vm::vec2d& center,
  const double innerRadius,
  const double outerRadius,
  const double startAngle,
  const double endAngle,
  const double minZ,
  const double maxZ,
  const std::string& material,
  QString& error,
  const double grid = 1.0,
  const SectorSnapMode snapMode = SectorSnapMode::Grid)
{
  auto polygon = sectorPolygon(
    center, innerRadius, outerRadius, startAngle, endAngle, snapMode, grid, error);
  if (!polygon)
  {
    return std::nullopt;
  }
  return createPrismBrush(
    builder,
    std::move(*polygon),
    snapToGrid(minZ, grid),
    snapToGrid(maxZ, grid),
    material,
    error);
}

std::vector<vm::vec2d> cylinderPolygon(
  const vm::bbox3d& bounds,
  const size_t sides,
  const SectorSnapMode snapMode,
  const double grid)
{
  const auto center = bounds.xy().center();
  const auto radius = vm::min(bounds.size().x(), bounds.size().y()) * 0.5;

  auto result = std::vector<vm::vec2d>{};
  result.reserve(sides);
  const auto halfAngle = vm::Cd::pi() / static_cast<double>(sides);
  const auto scale = 1.0 / std::cos(halfAngle);
  for (size_t i = 0; i < sides; ++i)
  {
    const auto angle =
      -vm::Cd::half_pi()
      + (static_cast<double>(i) + 0.5) * vm::Cd::two_pi() / static_cast<double>(sides);
    auto point =
      center
      + vm::vec2d{std::cos(angle) * radius * scale, std::sin(angle) * radius * scale};
    if (snapMode == SectorSnapMode::Grid)
    {
      point = snapToGrid(point, grid);
    }
    result.push_back(point);
  }
  return result;
}

std::optional<mdl::Brush> createCylinderBrushFromPolygon(
  const mdl::BrushBuilder& builder,
  const vm::bbox3d& bounds,
  const size_t sides,
  const vm::axis::type axis,
  const std::string& material,
  const SectorSnapMode snapMode,
  const double grid,
  QString& error)
{
  if (snapMode == SectorSnapMode::None)
  {
    auto brush =
      builder.createCylinder(bounds, mdl::EdgeAlignedCircle{sides}, axis, material);
    if (brush.is_error())
    {
      error = "Could not create cylinder brush from the given bounds";
      return std::nullopt;
    }
    return std::move(brush).value();
  }

  auto planeBounds = vm::bbox3d{};
  auto minAxis = 0.0;
  auto maxAxis = 0.0;
  if (axis == vm::axis::x)
  {
    planeBounds = vm::bbox3d{
      {bounds.min.y(), bounds.min.z(), bounds.min.x()},
      {bounds.max.y(), bounds.max.z(), bounds.max.x()}};
    minAxis = planeBounds.min.z();
    maxAxis = planeBounds.max.z();
  }
  else if (axis == vm::axis::y)
  {
    planeBounds = vm::bbox3d{
      {bounds.min.x(), bounds.min.z(), bounds.min.y()},
      {bounds.max.x(), bounds.max.z(), bounds.max.y()}};
    minAxis = planeBounds.min.z();
    maxAxis = planeBounds.max.z();
  }
  else
  {
    planeBounds = bounds;
    minAxis = bounds.min.z();
    maxAxis = bounds.max.z();
  }

  auto polygon = cylinderPolygon(planeBounds, sides, snapMode, grid);
  minAxis = snapMode == SectorSnapMode::Grid ? snapToGrid(minAxis, grid) : minAxis;
  maxAxis = snapMode == SectorSnapMode::Grid ? snapToGrid(maxAxis, grid) : maxAxis;
  if (!std::isfinite(minAxis) || !std::isfinite(maxAxis) || minAxis >= maxAxis)
  {
    error = "cylinder bounds collapse after grid snapping";
    return std::nullopt;
  }
  if (!isStrictlyConvexPolygon(polygon))
  {
    error = "cylinder footprint collapses after grid snapping";
    return std::nullopt;
  }

  if (polygonSignedArea(polygon) < 0.0)
  {
    std::reverse(polygon.begin(), polygon.end());
  }

  auto vertices = std::vector<vm::vec3d>{};
  vertices.reserve(polygon.size() * 2u);
  const auto addVertex = [&](const vm::vec2d& point, const double axisValue) {
    if (axis == vm::axis::x)
    {
      vertices.emplace_back(axisValue, point.x(), point.y());
    }
    else if (axis == vm::axis::y)
    {
      vertices.emplace_back(point.x(), axisValue, point.y());
    }
    else
    {
      vertices.emplace_back(point.x(), point.y(), axisValue);
    }
  };

  for (const auto& point : polygon)
  {
    addVertex(point, minAxis);
  }
  for (const auto& point : polygon)
  {
    addVertex(point, maxAxis);
  }

  auto brush = builder.createBrush(vertices, material);
  if (brush.is_error())
  {
    error = "Could not create cylinder brush from snapped points";
    return std::nullopt;
  }
  return std::move(brush).value();
}

std::optional<vm::axis::type> axisFromJson(
  const QJsonObject& params,
  const QString& key,
  const vm::axis::type defaultValue,
  QString& error)
{
  const auto value = params.value(key);
  if (value.isUndefined())
  {
    return defaultValue;
  }
  if (!value.isString())
  {
    error = QString{"%1 must be x, y, or z"}.arg(key);
    return std::nullopt;
  }

  const auto axis = value.toString().trimmed().toLower();
  if (axis == "x")
  {
    return vm::axis::x;
  }
  if (axis == "y")
  {
    return vm::axis::y;
  }
  if (axis == "z")
  {
    return vm::axis::z;
  }

  error = QString{"%1 must be x, y, or z"}.arg(key);
  return std::nullopt;
}

std::string materialNameFromParams(mdl::Map& map, const QJsonObject& params)
{
  auto material = mcpOptionalString(params, "material");
  if (material.empty())
  {
    material = map.currentMaterialName();
  }
  if (material.empty())
  {
    material = "__TB_empty";
  }
  return material;
}

bool brushMaterialIsLoaded(mdl::Map& map, const QString& material)
{
  const auto name = material.trimmed().toStdString();
  if (name.empty() || kdl::ci::str_is_equal(name, "__TB_empty"))
  {
    return false;
  }
  return std::ranges::any_of(map.materialManager().materials(), [&](const auto* entry) {
    return entry != nullptr && kdl::ci::str_is_equal(entry->name(), name);
  });
}

QJsonObject materialResolutionJson(mdl::Map& map, const QJsonObject& params)
{
  const auto requested = params.value("material").toString().trimmed();
  const auto current = QString::fromStdString(map.currentMaterialName()).trimmed();
  const auto effective = !requested.isEmpty() ? requested
                         : !current.isEmpty() ? current
                                              : QString{"__TB_empty"};
  return QJsonObject{
    {"requestedMaterial", requested},
    {"effectiveMaterial", effective},
    {"source",
     !requested.isEmpty() ? "request"
     : !current.isEmpty() ? "current"
                          : "fallback"},
    {"currentlyLoaded", brushMaterialIsLoaded(map, effective)},
    {"placeholderMaterial", effective.compare("__TB_empty", Qt::CaseInsensitive) == 0},
  };
}

QJsonObject batchMaterialPreflight(
  mdl::Map& map, const QJsonObject& params, const QJsonArray& operations)
{
  auto materials = QStringList{};
  const auto defaultResolution = materialResolutionJson(map, params);
  materials.push_back(defaultResolution.value("effectiveMaterial").toString());
  for (const auto& value : operations)
  {
    const auto operation = value.toObject();
    const auto material = operation.value("material").toString().trimmed();
    if (!material.isEmpty())
    {
      materials.push_back(material);
    }
    const auto partMaterials = operation.value("partMaterials").toObject();
    for (auto it = partMaterials.begin(); it != partMaterials.end(); ++it)
    {
      const auto partMaterial = it.value().toString().trimmed();
      if (!partMaterial.isEmpty())
      {
        materials.push_back(partMaterial);
      }
    }
  }
  materials.removeDuplicates();

  auto missing = QStringList{};
  auto resolutions = QJsonArray{};
  for (const auto& material : materials)
  {
    const auto placeholder = material.compare("__TB_empty", Qt::CaseInsensitive) == 0;
    const auto loaded = brushMaterialIsLoaded(map, material);
    resolutions.push_back(QJsonObject{
      {"requestedMaterial", material},
      {"effectiveMaterial", material},
      {"source",
       material == defaultResolution.value("effectiveMaterial").toString()
         ? defaultResolution.value("source")
         : QJsonValue{"operation"}},
      {"currentlyLoaded", loaded},
      {"placeholderMaterial", placeholder},
    });
    if (!loaded && !placeholder)
    {
      missing.push_back(material);
    }
  }
  return QJsonObject{
    {"defaultMaterial", defaultResolution},
    {"checkedMaterialCount", materials.size()},
    {"missingMaterialCount", missing.size()},
    {"missingMaterialSample", QJsonArray::fromStringList(missing.mid(0, 8))},
    {"resolutions", resolutions},
  };
}

McpBridgeToolResult strictMaterialFailure(const QJsonObject& preflight)
{
  return McpBridgeToolResult::failure(
    mcp::McpErrorCode::InvalidParams,
    "One or more requested materials are not loaded",
    QJsonObject{
      {"mutatedDocument", false},
      {"retrySafe", true},
      {"recoveryAction", "load_materials_or_disable_strict_material_check"},
      {"materialPreflight", preflight},
    });
}

std::optional<QJsonArray> addNodesWithTransaction(
  mdl::Map& map,
  const QString& transactionName,
  const std::vector<mdl::Node*>& nodes,
  const bool selectCreated)
{
  auto addedNodes = std::vector<mdl::Node*>{};
  auto ok = executeTransaction(map, transactionName, [&]() {
    if (selectCreated)
    {
      mdl::deselectAll(map);
    }

    auto& parent = mdl::parentForNodes(map);
    if (!parent.canAddChildren(std::begin(nodes), std::end(nodes)))
    {
      return false;
    }

    auto nodesToAdd = std::map<mdl::Node*, std::vector<mdl::Node*>>{};
    nodesToAdd.emplace(&parent, nodes);
    if (!map.executeAndStore(mdl::AddRemoveNodesCommand::add(nodesToAdd)))
    {
      return false;
    }

    addedNodes = nodes;
    if (selectCreated)
    {
      mdl::selectNodes(map, addedNodes);
    }
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

Result<mdl::Brush> createWedgeBrush(
  const mdl::BrushBuilder& builder,
  const vm::bbox3d& bounds,
  const vm::axis::type axis,
  const std::string& material)
{
  const auto& min = bounds.min;
  const auto& max = bounds.max;

  auto points = std::vector<vm::vec3d>{};
  switch (axis)
  {
  case vm::axis::x:
    points = {
      {min.x(), min.y(), min.z()},
      {min.x(), max.y(), min.z()},
      {min.x(), min.y(), max.z()},
      {min.x(), max.y(), max.z()},
      {max.x(), min.y(), min.z()},
      {max.x(), max.y(), min.z()},
    };
    break;
  case vm::axis::y:
    points = {
      {min.x(), min.y(), min.z()},
      {max.x(), min.y(), min.z()},
      {min.x(), min.y(), max.z()},
      {max.x(), min.y(), max.z()},
      {min.x(), max.y(), min.z()},
      {max.x(), max.y(), min.z()},
    };
    break;
  case vm::axis::z:
    points = {
      {min.x(), min.y(), min.z()},
      {max.x(), min.y(), min.z()},
      {min.x(), max.y(), min.z()},
      {max.x(), max.y(), min.z()},
      {min.x(), min.y(), max.z()},
      {max.x(), min.y(), max.z()},
    };
    break;
  }

  return builder.createBrush(points, material);
}

Result<mdl::Brush> createRampBetweenBrush(
  const mdl::BrushBuilder& builder,
  const vm::vec3d& start,
  const vm::vec3d& end,
  const double width,
  const double thickness,
  const std::string& material,
  QString& error)
{
  const auto delta = vm::vec2d{end.x() - start.x(), end.y() - start.y()};
  if (vm::is_zero(delta, GeometryEpsilon))
  {
    error = "ramp_between start/end must differ in X/Y after snapping";
    return Error{"ramp_between start/end must differ in X/Y after snapping"};
  }

  if (!finitePositive(width))
  {
    error = "ramp_between width must be greater than zero";
    return Error{"ramp_between width must be greater than zero"};
  }
  if (!finitePositive(thickness))
  {
    error = "ramp_between thickness must be greater than zero";
    return Error{"ramp_between thickness must be greater than zero"};
  }

  const auto direction = vm::normalize(delta);
  const auto right = vm::vec2d{direction.y(), -direction.x()} * (width * 0.5);
  const auto startLeft =
    vm::vec3d{start.x() - right.x(), start.y() - right.y(), start.z()};
  const auto startRight =
    vm::vec3d{start.x() + right.x(), start.y() + right.y(), start.z()};
  const auto endLeft = vm::vec3d{end.x() - right.x(), end.y() - right.y(), end.z()};
  const auto endRight = vm::vec3d{end.x() + right.x(), end.y() + right.y(), end.z()};

  const auto points = std::vector<vm::vec3d>{
    startLeft,
    startRight,
    endLeft,
    endRight,
    startLeft - vm::vec3d{0, 0, thickness},
    startRight - vm::vec3d{0, 0, thickness},
    endLeft - vm::vec3d{0, 0, thickness},
    endRight - vm::vec3d{0, 0, thickness},
  };
  return builder.createBrush(points, material);
}

Result<mdl::Brush> createArcRampSegmentBrush(
  const mdl::BrushBuilder& builder,
  const vm::vec3d& center,
  const double innerRadius,
  const double outerRadius,
  const double startAngleDegrees,
  const double endAngleDegrees,
  const double startZ,
  const double endZ,
  const double thickness,
  const std::string& material)
{
  const auto a0 = degreesToRadians(startAngleDegrees);
  const auto a1 = degreesToRadians(endAngleDegrees);

  const auto pointAt = [&](const double radius, const double angle, const double z) {
    return vm::vec3d{
      center.x() + std::cos(angle) * radius,
      center.y() + std::sin(angle) * radius,
      center.z() + z};
  };

  const auto innerStartTop = pointAt(innerRadius, a0, startZ);
  const auto outerStartTop = pointAt(outerRadius, a0, startZ);
  const auto innerEndTop = pointAt(innerRadius, a1, endZ);
  const auto outerEndTop = pointAt(outerRadius, a1, endZ);

  const auto points = std::vector<vm::vec3d>{
    innerStartTop,
    outerStartTop,
    innerEndTop,
    outerEndTop,
    innerStartTop - vm::vec3d{0, 0, thickness},
    outerStartTop - vm::vec3d{0, 0, thickness},
    innerEndTop - vm::vec3d{0, 0, thickness},
    outerEndTop - vm::vec3d{0, 0, thickness},
  };
  return builder.createBrush(points, material);
}

std::optional<QString> rampBetweenGridWarning(
  const QJsonObject& operation, const double grid)
{
  auto error = QString{};
  const auto start = mcpVec3FromJson(operation, "start", error);
  if (!start)
  {
    return std::nullopt;
  }
  const auto end = mcpVec3FromJson(operation, "end", error);
  if (!end)
  {
    return std::nullopt;
  }

  const auto snappedStart = snapToGrid(*start, grid);
  const auto snappedEnd = snapToGrid(*end, grid);
  const auto delta = vm::vec2d{
    std::abs(snappedEnd.x() - snappedStart.x()),
    std::abs(snappedEnd.y() - snappedStart.y())};
  if (delta.x() <= GeometryEpsilon || delta.y() <= GeometryEpsilon)
  {
    return std::nullopt;
  }

  return QString{
    "offAxisRampMayProduceNonGridVertices: diagonal ramp_between uses a "
    "perpendicular width vector, so side vertices can land between grid points. "
    "Prefer an axis-aligned ramp_between/wedge for strict grid work, use a smaller "
    "grid, or accept off-grid diagonal ramp geometry."};
}

bool jsonArrayContainsString(
  const QJsonValue& value, const QString& candidate, const Qt::CaseSensitivity cs)
{
  if (!value.isArray())
  {
    return false;
  }
  for (const auto& item : value.toArray())
  {
    if (item.toString().compare(candidate, cs) == 0)
    {
      return true;
    }
  }
  return false;
}

QJsonArray blockoutWarningsForOperations(const QJsonArray& operations, const double grid)
{
  auto warnings = QJsonArray{};
  for (auto i = 0; i < operations.size(); ++i)
  {
    if (!operations[i].isObject())
    {
      continue;
    }
    const auto operation = operations[i].toObject();
    const auto type = operation.value("type").toString().trimmed().toLower();
    if (type == "ramp_between")
    {
      if (const auto warning = rampBetweenGridWarning(operation, grid))
      {
        warnings.push_back(QString{"operations[%1]: %2"}.arg(i).arg(*warning));
      }
    }
    else if (type == "curved_corridor")
    {
      const auto slopeStartZ = optionalDouble(operation, "slopeStartZ", 0.0);
      const auto slopeEndZ = optionalDouble(operation, "slopeEndZ", slopeStartZ);
      if (std::abs(slopeEndZ - slopeStartZ) > GeometryEpsilon)
      {
        warnings.push_back(QString{
          "operations[%1]: terracedCurvedCorridor: slopeStartZ/slopeEndZ raises "
          "each corridor segment as a flat terrace; it does not create continuous "
          "sloped top faces. Use arc_ramp/helical_ramp or ramp_between segments for "
          "smooth ascending route surfaces."}
                             .arg(i));
      }
    }
    else if (type == "path_ribbon" && operation.value("points3d").isArray())
    {
      auto error = QString{};
      const auto points3d = centerlinePoints3DFromJson(operation, "points3d", error);
      if (points3d)
      {
        auto hasZDelta = false;
        for (size_t pointIndex = 0; pointIndex + 1u < points3d->size(); ++pointIndex)
        {
          if (
            std::abs((*points3d)[pointIndex + 1u].z() - (*points3d)[pointIndex].z())
            > GeometryEpsilon)
          {
            hasZDelta = true;
            break;
          }
        }
        if (hasZDelta)
        {
          warnings.push_back(QString{
            "operations[%1]: flatPoints3dPathRibbon: points3d path_ribbon uses "
            "the lower endpoint Z for each flat segment; it does not interpolate a "
            "ramp surface between different Z values."}
                               .arg(i));
        }
      }
    }
    if (
      type == "path_ribbon"
      && jsonArrayContainsString(operation.value("parts"), "floor", Qt::CaseInsensitive))
    {
      warnings.push_back(QString{
        "operations[%1]: pathRibbonFloorPartPreserved: parts:[\"floor\"] stores "
        "metadata part floor; unspecified path_ribbon output defaults to part surface."}
                           .arg(i));
    }
  }
  return warnings;
}

std::optional<QJsonObject> rampBetweenIntentSummary(
  const QJsonObject& operation, const double grid, QString& error)
{
  const auto start = mcpVec3FromJson(operation, "start", error);
  if (!start)
  {
    return std::nullopt;
  }
  const auto end = mcpVec3FromJson(operation, "end", error);
  if (!end)
  {
    return std::nullopt;
  }

  const auto snappedStart = snapToGrid(*start, grid);
  const auto snappedEnd = snapToGrid(*end, grid);
  const auto travel = snappedEnd - snappedStart;
  const auto horizontal = vm::vec2d{travel.x(), travel.y()};
  if (vm::is_zero(horizontal, GeometryEpsilon))
  {
    error = "ramp_between start/end must differ in X/Y after snapping";
    return std::nullopt;
  }

  return QJsonObject{
    {"type", "ramp_between"},
    {"start", vecToJson(snappedStart)},
    {"end", vecToJson(snappedEnd)},
    {"heightDelta", snappedEnd.z() - snappedStart.z()},
    {"travelDirection", vecToJson(vm::normalize(travel))},
    {"width", snapToGrid(optionalDouble(operation, "width", 128.0), grid)},
    {"thickness", snapToGrid(optionalDouble(operation, "thickness", 16.0), grid)},
  };
}

std::optional<QJsonObject> rampLikeIntentSummary(
  const QJsonObject& operation, const double grid, QString& error)
{
  const auto type = operation.value("type").toString().trimmed().toLower();
  if (type == "ramp_between")
  {
    return rampBetweenIntentSummary(operation, grid, error);
  }
  if (type != "ramp" && type != "wedge")
  {
    return std::nullopt;
  }

  const auto bounds = boundsFromJson(operation, error);
  if (!bounds)
  {
    return std::nullopt;
  }
  const auto snappedBounds = snapBoundsToGrid(*bounds, grid, error);
  if (!snappedBounds)
  {
    return std::nullopt;
  }
  const auto axis = axisFromJson(operation, "axis", vm::axis::x, error);
  if (!axis)
  {
    return std::nullopt;
  }

  auto travel = vm::vec3d{};
  switch (*axis)
  {
  case vm::axis::x:
    travel = vm::vec3d{snappedBounds->size().x(), 0, snappedBounds->size().z()};
    break;
  case vm::axis::y:
    travel = vm::vec3d{0, snappedBounds->size().y(), snappedBounds->size().z()};
    break;
  case vm::axis::z:
    travel = vm::vec3d{0, 0, snappedBounds->size().z()};
    break;
  }

  return QJsonObject{
    {"type", type},
    {"note",
     "Legacy min/max/axis slope primitive. Prefer ramp_between for route intent."},
    {"bounds", boundsToJson(*snappedBounds)},
    {"axis", axisName(*axis)},
    {"heightDelta", snappedBounds->size().z()},
    {"travelDirection",
     vm::is_zero(travel, GeometryEpsilon) ? vecToJson(travel)
                                          : vecToJson(vm::normalize(travel))},
  };
}

Result<mdl::Brush> createPyramidBrush(
  const mdl::BrushBuilder& builder,
  const vm::bbox3d& bounds,
  const vm::axis::type axis,
  const std::string& material)
{
  const auto& min = bounds.min;
  const auto& max = bounds.max;
  const auto center = bounds.center();

  auto points = std::vector<vm::vec3d>{};
  switch (axis)
  {
  case vm::axis::x:
    points = {
      {min.x(), min.y(), min.z()},
      {min.x(), max.y(), min.z()},
      {min.x(), min.y(), max.z()},
      {min.x(), max.y(), max.z()},
      {max.x(), center.y(), center.z()},
    };
    break;
  case vm::axis::y:
    points = {
      {min.x(), min.y(), min.z()},
      {max.x(), min.y(), min.z()},
      {min.x(), min.y(), max.z()},
      {max.x(), min.y(), max.z()},
      {center.x(), max.y(), center.z()},
    };
    break;
  case vm::axis::z:
    points = {
      {min.x(), min.y(), min.z()},
      {max.x(), min.y(), min.z()},
      {min.x(), max.y(), min.z()},
      {max.x(), max.y(), min.z()},
      {center.x(), center.y(), max.z()},
    };
    break;
  }

  return builder.createBrush(points, material);
}

Result<mdl::Brush> createTetrahedronBrush(
  const mdl::BrushBuilder& builder,
  const vm::bbox3d& bounds,
  const vm::axis::type axis,
  const std::string& material)
{
  const auto& min = bounds.min;
  const auto& max = bounds.max;

  auto points = std::vector<vm::vec3d>{};
  switch (axis)
  {
  case vm::axis::x:
    points = {
      {min.x(), min.y(), min.z()},
      {min.x(), max.y(), min.z()},
      {min.x(), min.y(), max.z()},
      {max.x(), max.y(), max.z()},
    };
    break;
  case vm::axis::y:
    points = {
      {min.x(), min.y(), min.z()},
      {max.x(), min.y(), min.z()},
      {min.x(), min.y(), max.z()},
      {max.x(), max.y(), max.z()},
    };
    break;
  case vm::axis::z:
    points = {
      {min.x(), min.y(), min.z()},
      {max.x(), min.y(), min.z()},
      {min.x(), max.y(), min.z()},
      {max.x(), max.y(), max.z()},
    };
    break;
  }

  return builder.createBrush(points, material);
}

std::optional<mdl::Brush> createBrushFromPlaneTriples(
  const mdl::Map& map,
  const QJsonObject& params,
  const std::string& material,
  QString& error)
{
  const auto planesValue = params.value("planes");
  if (!planesValue.isArray())
  {
    error = "planes must be an array";
    return std::nullopt;
  }

  const auto planes = planesValue.toArray();
  if (planes.size() < 4)
  {
    error = "planes must contain at least four plane definitions";
    return std::nullopt;
  }

  auto faces = std::vector<mdl::BrushFace>{};
  faces.reserve(static_cast<size_t>(planes.size()));
  auto planeIndex = 0;
  for (const auto& planeValue : planes)
  {
    if (!planeValue.isArray())
    {
      error = QString{"planes[%1] must be an array of three points"}.arg(planeIndex);
      return std::nullopt;
    }

    const auto pointArray = planeValue.toArray();
    if (pointArray.size() != 3)
    {
      error = QString{"planes[%1] must contain exactly three points"}.arg(planeIndex);
      return std::nullopt;
    }

    auto points = std::array<vm::vec3d, 3>{};
    for (auto pointIndex = 0; pointIndex < 3; ++pointIndex)
    {
      if (!pointArray[pointIndex].isArray())
      {
        error = QString{"planes[%1][%2] must be an array of three numbers"}
                  .arg(planeIndex)
                  .arg(pointIndex);
        return std::nullopt;
      }

      auto pointParams = QJsonObject{};
      pointParams.insert("point", pointArray[pointIndex].toArray());
      const auto point = mcpVec3FromJson(pointParams, "point", error);
      if (!point)
      {
        error = QString{"planes[%1][%2]: %3"}.arg(planeIndex).arg(pointIndex).arg(error);
        return std::nullopt;
      }
      points[static_cast<size_t>(pointIndex)] = *point;
    }

    auto face = mdl::BrushFace::create(
      points[0],
      points[1],
      points[2],
      material,
      map.gameInfo().gameConfig.faceAttribsConfig.defaultUvAttributes,
      map.gameInfo().gameConfig.faceAttribsConfig.defaultSurfaceAttributes,
      map.worldNode().mapFormat());
    if (face.is_error())
    {
      error = QString{"planes[%1] does not define a valid plane"}.arg(planeIndex);
      return std::nullopt;
    }
    faces.push_back(std::move(face).value());
    ++planeIndex;
  }

  auto brush = mdl::Brush::create(map.worldBounds(), std::move(faces));
  if (brush.is_error())
  {
    error = "planes do not form a valid closed convex brush";
    return std::nullopt;
  }
  return std::move(brush).value();
}

std::optional<std::vector<vm::bbox3d>> steppedMassBounds(
  const vm::bbox3d& baseBounds,
  const size_t levels,
  const double inset,
  const double stepHeight,
  QString& error)
{
  if (levels == 0 || levels > 256)
  {
    error = "levels must be between 1 and 256";
    return std::nullopt;
  }
  if (!std::isfinite(inset) || inset < 0.0)
  {
    error = "inset must be finite and non-negative";
    return std::nullopt;
  }
  if (!finitePositive(stepHeight))
  {
    error = "stepHeight must be greater than zero";
    return std::nullopt;
  }

  const auto& min = baseBounds.min;
  const auto& max = baseBounds.max;
  auto result = std::vector<vm::bbox3d>{};
  result.reserve(levels);
  for (size_t i = 0; i < levels; ++i)
  {
    const auto offset = inset * static_cast<double>(i);
    const auto levelMin = vm::vec3d{
      min.x() + offset, min.y() + offset, min.z() + stepHeight * static_cast<double>(i)};
    const auto levelMax = vm::vec3d{
      max.x() - offset,
      max.y() - offset,
      min.z() + stepHeight * static_cast<double>(i + 1)};
    if (
      levelMin.x() >= levelMax.x() || levelMin.y() >= levelMax.y()
      || levelMin.z() >= levelMax.z())
    {
      error = QString{"stepped_mass level %1 collapsed; reduce levels or inset"}.arg(i);
      return std::nullopt;
    }
    result.emplace_back(levelMin, levelMax);
  }
  return result;
}

std::optional<std::vector<vm::bbox3d>> supportPostBounds(
  const std::vector<vm::vec2d>& points,
  const double bottomZ,
  const double topZ,
  const double postSize,
  QString& error)
{
  if (points.empty())
  {
    error = "points2d must contain at least one support position";
    return std::nullopt;
  }
  if (!std::isfinite(bottomZ) || !std::isfinite(topZ) || bottomZ >= topZ)
  {
    error = "bottomZ must be smaller than topZ";
    return std::nullopt;
  }
  if (!finitePositive(postSize))
  {
    error = "postSize must be greater than zero";
    return std::nullopt;
  }

  const auto halfSize = postSize * 0.5;
  auto result = std::vector<vm::bbox3d>{};
  result.reserve(points.size());
  for (const auto& point : points)
  {
    result.emplace_back(
      vm::vec3d{point.x() - halfSize, point.y() - halfSize, bottomZ},
      vm::vec3d{point.x() + halfSize, point.y() + halfSize, topZ});
  }
  return result;
}

struct SpiralStairsParams
{
  vm::vec3d center = vm::vec3d{0, 0, 0};
  double innerRadius = 32.0;
  double outerRadius = 128.0;
  size_t steps = 24;
  double stepHeight = 8.0;
  double startAngle = 0.0;
  double turnDegrees = 360.0;
  bool clockwise = false;
  double baseZ = 0.0;
  bool column = true;
  bool landing = true;
};

std::optional<SpiralStairsParams> spiralStairsParamsFromJson(
  const QJsonObject& params, QString& error)
{
  auto result = SpiralStairsParams{};
  if (params.contains("center"))
  {
    const auto center = mcpVec3FromJson(params, "center", error);
    if (!center)
    {
      return std::nullopt;
    }
    result.center = *center;
  }
  result.innerRadius = optionalDouble(params, "innerRadius", result.innerRadius);
  result.outerRadius = optionalDouble(params, "outerRadius", result.outerRadius);
  result.steps = optionalSize(params, "steps", result.steps);
  result.stepHeight = optionalDouble(params, "stepHeight", result.stepHeight);
  result.startAngle = optionalDouble(params, "startAngle", result.startAngle);
  result.turnDegrees = optionalDouble(params, "turnDegrees", result.turnDegrees);
  result.clockwise = mcpOptionalBool(params, "clockwise", result.clockwise);
  result.baseZ = optionalDouble(params, "baseZ", result.center.z());
  result.column = mcpOptionalBool(params, "column", result.column);
  result.landing = mcpOptionalBool(params, "landing", result.landing);

  if (!finitePositive(result.innerRadius) || !finitePositive(result.outerRadius))
  {
    error = "innerRadius and outerRadius must be greater than zero";
    return std::nullopt;
  }
  if (result.innerRadius >= result.outerRadius)
  {
    error = "innerRadius must be smaller than outerRadius";
    return std::nullopt;
  }
  if (result.steps < 3 || result.steps > 128)
  {
    error = "steps must be between 3 and 128";
    return std::nullopt;
  }
  if (!finitePositive(result.stepHeight))
  {
    error = "stepHeight must be greater than zero";
    return std::nullopt;
  }
  if (!std::isfinite(result.startAngle) || !std::isfinite(result.turnDegrees))
  {
    error = "startAngle and turnDegrees must be finite";
    return std::nullopt;
  }
  if (
    std::abs(result.turnDegrees) <= GeometryEpsilon
    || std::abs(result.turnDegrees) > 360.0)
  {
    error = "turnDegrees must be greater than zero and at most 360 degrees";
    return std::nullopt;
  }
  const auto stepAngle = std::abs(result.turnDegrees) / static_cast<double>(result.steps);
  if (stepAngle > 180.0)
  {
    error = "per-step angle must be at most 180 degrees";
    return std::nullopt;
  }
  return result;
}

double spiralDirection(const SpiralStairsParams& params)
{
  return params.clockwise ? -1.0 : 1.0;
}

double spiralStepAngle(const SpiralStairsParams& params)
{
  return spiralDirection(params) * std::abs(params.turnDegrees)
         / static_cast<double>(params.steps);
}

std::optional<std::vector<mdl::Brush>> createSpiralStairBrushes(
  const mdl::BrushBuilder& builder,
  const SpiralStairsParams& params,
  const std::string& material,
  QString& error)
{
  auto brushes = std::vector<mdl::Brush>{};
  brushes.reserve(params.steps + (params.column ? 1u : 0u) + (params.landing ? 1u : 0u));

  const auto center2D = vm::vec2d{params.center.x(), params.center.y()};
  const auto stepAngle = spiralStepAngle(params);
  auto innerPoints = std::vector<vm::vec2d>{};
  auto outerPoints = std::vector<vm::vec2d>{};
  innerPoints.reserve(params.steps + 1u);
  outerPoints.reserve(params.steps + 1u);
  for (size_t i = 0; i <= params.steps; ++i)
  {
    const auto angle = params.startAngle + stepAngle * static_cast<double>(i);
    innerPoints.push_back(snapToGrid(polarPoint(center2D, params.innerRadius, angle)));
    outerPoints.push_back(snapToGrid(polarPoint(center2D, params.outerRadius, angle)));
  }

  for (size_t i = 0; i < params.steps; ++i)
  {
    auto brush = createPrismBrush(
      builder,
      std::vector<vm::vec2d>{
        innerPoints[i], outerPoints[i], outerPoints[i + 1u], innerPoints[i + 1u]},
      params.baseZ,
      params.baseZ + params.stepHeight * static_cast<double>(i + 1),
      material,
      error);
    if (!brush)
    {
      return std::nullopt;
    }
    brushes.push_back(std::move(*brush));
  }

  if (params.column)
  {
    const auto columnSides = std::max<size_t>(16, params.steps);
    auto points = std::vector<vm::vec2d>{};
    points.reserve(columnSides);
    for (size_t i = 0; i < columnSides; ++i)
    {
      if (columnSides == params.steps)
      {
        points.push_back(innerPoints[i]);
      }
      else
      {
        points.push_back(snapToGrid(polarPoint(
          center2D,
          params.innerRadius,
          params.startAngle
            + 360.0 * static_cast<double>(i) / static_cast<double>(columnSides))));
      }
    }
    auto brush = createPrismBrush(
      builder,
      std::move(points),
      params.baseZ,
      params.baseZ + params.stepHeight * static_cast<double>(params.steps),
      material,
      error);
    if (!brush)
    {
      return std::nullopt;
    }
    brushes.push_back(std::move(*brush));
  }

  if (params.landing)
  {
    const auto innerA = innerPoints.back();
    const auto outerA = outerPoints.back();
    auto radial = outerA - innerA;
    if (vm::is_zero(radial, GeometryEpsilon))
    {
      error = "Could not determine spiral stair landing direction";
      return std::nullopt;
    }
    radial = vm::normalize(radial);
    const auto tangentSign = stepAngle >= 0.0 ? 1.0 : -1.0;
    const auto tangent = vm::vec2d{-radial.y() * tangentSign, radial.x() * tangentSign};
    const auto width = params.outerRadius - params.innerRadius;
    const auto length = std::max(width, 64.0);
    const auto innerB = snapToGrid(innerA + tangent * length);
    const auto outerB = snapToGrid(outerA + tangent * length);
    auto brush = createPrismBrush(
      builder,
      std::vector<vm::vec2d>{innerA, outerA, outerB, innerB},
      params.baseZ + params.stepHeight * static_cast<double>(params.steps),
      params.baseZ + params.stepHeight * static_cast<double>(params.steps + 1),
      material,
      error);
    if (!brush)
    {
      return std::nullopt;
    }
    brushes.push_back(std::move(*brush));
  }

  return brushes;
}

QJsonObject spiralValidationJson(
  const SpiralStairsParams& params,
  const size_t brushCount,
  const int invalidBrushCount = 0)
{
  return QJsonObject{
    {"valid", invalidBrushCount == 0},
    {"gapCount", 0},
    {"radiusMismatch", false},
    {"columnFits", params.column},
    {"landingConnected", params.landing},
    {"invalidBrushCount", invalidBrushCount},
    {"expectedSteps", static_cast<int>(params.steps)},
    {"brushCount", static_cast<int>(brushCount)},
    {"innerRadius", params.innerRadius},
    {"outerRadius", params.outerRadius},
    {"stepHeight", params.stepHeight},
    {"turnDegrees", params.turnDegrees},
  };
}

struct SpiralGeometryChecks
{
  int gapCount = 0;
  bool radiusMismatch = false;
  bool columnFits = true;
  bool landingConnected = true;
  bool zProgression = true;
  QJsonArray errors;
};

double distance2D(const vm::vec3d& point, const vm::vec2d& center)
{
  const auto x = point.x() - center.x();
  const auto y = point.y() - center.y();
  return std::sqrt(x * x + y * y);
}

bool pointRadiusWithin(
  const vm::vec3d& point,
  const vm::vec2d& center,
  const double minRadius,
  const double maxRadius,
  const double epsilon)
{
  const auto radius = distance2D(point, center);
  return radius >= minRadius - epsilon && radius <= maxRadius + epsilon;
}

SpiralGeometryChecks analyzeSpiralGeometry(
  const std::vector<mdl::BrushNode*>& brushes, const SpiralStairsParams& params)
{
  auto checks = SpiralGeometryChecks{};
  const auto center2D = vm::vec2d{params.center.x(), params.center.y()};
  const auto radiusEpsilon = 2.0;
  const auto zEpsilon = 0.01;

  if (brushes.size() < params.steps)
  {
    checks.errors.push_back("Not enough brush nodes to contain all spiral steps");
    checks.gapCount = static_cast<int>(params.steps - brushes.size());
    checks.zProgression = false;
    return checks;
  }

  for (size_t i = 0; i < params.steps; ++i)
  {
    const auto& brush = brushes[i]->brush();
    const auto& bounds = brush.bounds();
    const auto expectedMaxZ =
      params.baseZ + params.stepHeight * static_cast<double>(i + 1);
    if (
      !nearlyEqual(bounds.min.z(), params.baseZ, zEpsilon)
      || !nearlyEqual(bounds.max.z(), expectedMaxZ, zEpsilon))
    {
      checks.zProgression = false;
      checks.errors.push_back(QString{"Step %1 has z range [%2,%3], expected [%4,%5]"}
                                .arg(static_cast<int>(i))
                                .arg(bounds.min.z())
                                .arg(bounds.max.z())
                                .arg(params.baseZ)
                                .arg(expectedMaxZ));
    }

    const auto vertices = brush.vertexPositions();
    if (!std::ranges::all_of(vertices, [&](const auto& vertex) {
          return pointRadiusWithin(
            vertex, center2D, params.innerRadius, params.outerRadius, radiusEpsilon);
        }))
    {
      checks.radiusMismatch = true;
      checks.errors.push_back(
        QString{"Step %1 has vertices outside expected inner/outer radii"}.arg(
          static_cast<int>(i)));
    }
  }

  auto nextBrushIndex = params.steps;
  if (params.column)
  {
    if (brushes.size() <= nextBrushIndex)
    {
      checks.columnFits = false;
      checks.errors.push_back("Missing center column brush");
    }
    else
    {
      const auto& brush = brushes[nextBrushIndex]->brush();
      const auto& bounds = brush.bounds();
      if (
        !nearlyEqual(bounds.min.z(), params.baseZ, zEpsilon)
        || !nearlyEqual(
          bounds.max.z(),
          params.baseZ + params.stepHeight * static_cast<double>(params.steps),
          zEpsilon))
      {
        checks.columnFits = false;
        checks.errors.push_back(
          "Center column height does not match spiral stair height");
      }

      const auto vertices = brush.vertexPositions();
      if (!std::ranges::all_of(vertices, [&](const auto& vertex) {
            return pointRadiusWithin(
              vertex, center2D, 0.0, params.innerRadius, radiusEpsilon);
          }))
      {
        checks.columnFits = false;
        checks.errors.push_back("Center column vertices exceed inner radius");
      }
      ++nextBrushIndex;
    }
  }

  if (params.landing)
  {
    if (brushes.size() <= nextBrushIndex)
    {
      checks.landingConnected = false;
      checks.errors.push_back("Missing spiral stair landing brush");
    }
    else
    {
      const auto& landingBounds = brushes[nextBrushIndex]->brush().bounds();
      const auto expectedMinZ =
        params.baseZ + params.stepHeight * static_cast<double>(params.steps);
      const auto expectedMaxZ =
        params.baseZ + params.stepHeight * static_cast<double>(params.steps + 1);
      if (
        !nearlyEqual(landingBounds.min.z(), expectedMinZ, zEpsilon)
        || !nearlyEqual(landingBounds.max.z(), expectedMaxZ, zEpsilon))
      {
        checks.landingConnected = false;
        checks.errors.push_back(QString{"Landing has z range [%1,%2], expected [%3,%4]"}
                                  .arg(landingBounds.min.z())
                                  .arg(landingBounds.max.z())
                                  .arg(expectedMinZ)
                                  .arg(expectedMaxZ));
      }
    }
  }

  return checks;
}

std::optional<QJsonObject> validateBlockoutParams(
  const QString& type, const QJsonObject& params, QString& error)
{
  if (RemovedPrefabBatchTypes.contains(type))
  {
    error = QString{"%1 moved to trenchbroom-mcp-scene-workflow recipes"}.arg(type);
    return std::nullopt;
  }

  if (type == "spiral_stairs")
  {
    const auto spiralParams = spiralStairsParamsFromJson(params, error);
    if (!spiralParams)
    {
      return std::nullopt;
    }
    return QJsonObject{
      {"valid", true},
      {"type", type},
      {"steps", static_cast<int>(spiralParams->steps)},
      {"innerRadius", spiralParams->innerRadius},
      {"outerRadius", spiralParams->outerRadius},
      {"stepHeight", spiralParams->stepHeight},
      {"turnDegrees", spiralParams->turnDegrees},
    };
  }

  const auto bounds = boundsFromJson(params, error);
  if (!bounds)
  {
    return std::nullopt;
  }

  if (type != "ramp")
  {
    error = "Unknown blockout type";
    return std::nullopt;
  }

  return QJsonObject{
    {"valid", true},
    {"type", type},
    {"bounds", boundsToJson(*bounds)},
  };
}

std::vector<mdl::Node*> brushNodesFromBounds(
  const mdl::BrushBuilder& builder,
  const std::vector<vm::bbox3d>& boundsList,
  const std::string& material,
  QString& error)
{
  auto result = std::vector<mdl::Node*>{};
  result.reserve(boundsList.size());
  for (const auto& bounds : boundsList)
  {
    auto brush = builder.createCuboid(bounds, material);
    if (brush.is_error())
    {
      for (auto* node : result)
      {
        delete node;
      }
      error = "Could not create one or more blockout brushes";
      return {};
    }
    result.push_back(new mdl::BrushNode{std::move(brush).value()});
  }
  return result;
}

void deleteNodes(std::vector<mdl::Node*>& nodes)
{
  for (auto* node : nodes)
  {
    delete node;
  }
  nodes.clear();
}

QJsonObject batchValidationJson(
  const bool valid,
  const QJsonArray& errors,
  const int operationCount,
  const int brushCount)
{
  return QJsonObject{
    {"valid", valid},
    {"errors", errors},
    {"operationCount", operationCount},
    {"brushCount", brushCount},
  };
}

QJsonObject batchOperationPreviewJson(const QJsonObject& operation)
{
  auto result = QJsonObject{};
  result.insert("type", operation.value("type").toString());

  for (const auto& key :
       {"min",
        "max",
        "points2d",
        "points",
        "center",
        "minZ",
        "maxZ",
        "axis",
        "sides",
        "width",
        "height",
        "segments",
        "startAngle",
        "endAngle",
        "turnDegrees"})
  {
    if (operation.contains(key))
    {
      result.insert(key, operation.value(key));
    }
  }

  return result;
}

QJsonArray translatedVec3Array(
  const QJsonValue& value, const vm::vec3d& delta, const QString& key, QString& error)
{
  const auto point = mcpVec3FromJsonValue(value, key, error);
  if (!point)
  {
    return {};
  }
  return vecToJson(*point + delta);
}

QJsonArray translatedVec2Array(
  const QJsonValue& value, const vm::vec2d& delta, const QString& key, QString& error)
{
  const auto point = mcpVec2FromJsonValue(value, key, error);
  if (!point)
  {
    return {};
  }
  return QJsonArray{point->x() + delta.x(), point->y() + delta.y()};
}

QJsonArray translatedVec2PointsArray(
  const QJsonValue& value, const vm::vec2d& delta, const QString& key, QString& error)
{
  if (!value.isArray())
  {
    error = QString{"%1 must be an array of [x,y] points"}.arg(key);
    return {};
  }

  auto result = QJsonArray{};
  const auto points = value.toArray();
  for (auto i = 0; i < points.size(); ++i)
  {
    if (!error.isEmpty())
    {
      return {};
    }
    result.push_back(
      translatedVec2Array(points[i], delta, QString{"%1[%2]"}.arg(key).arg(i), error));
  }
  return result;
}

QJsonArray translatedVec3PointsArray(
  const QJsonValue& value, const vm::vec3d& delta, const QString& key, QString& error)
{
  if (!value.isArray())
  {
    error = QString{"%1 must be an array of [x,y,z] points"}.arg(key);
    return {};
  }

  auto result = QJsonArray{};
  const auto points = value.toArray();
  for (auto i = 0; i < points.size(); ++i)
  {
    if (!error.isEmpty())
    {
      return {};
    }
    result.push_back(
      translatedVec3Array(points[i], delta, QString{"%1[%2]"}.arg(key).arg(i), error));
  }
  return result;
}

std::optional<QJsonObject> translatedBatchOperation(
  const QJsonObject& operation, const vm::vec3d& delta, QString& error)
{
  auto result = operation;
  const auto delta2D = vm::vec2d{delta.x(), delta.y()};

  for (const auto& key : {"min", "max", "center", "doorMin", "doorMax"})
  {
    if (result.contains(key))
    {
      result.insert(key, translatedVec3Array(result.value(key), delta, key, error));
      if (!error.isEmpty())
      {
        return std::nullopt;
      }
    }
  }

  if (result.contains("points2d"))
  {
    result.insert(
      "points2d",
      translatedVec2PointsArray(result.value("points2d"), delta2D, "points2d", error));
    if (!error.isEmpty())
    {
      return std::nullopt;
    }
  }

  if (result.contains("points"))
  {
    result.insert(
      "points",
      translatedVec3PointsArray(result.value("points"), delta, "points", error));
    if (!error.isEmpty())
    {
      return std::nullopt;
    }
  }

  for (const auto& key : {"minZ", "maxZ"})
  {
    if (result.value(key).isDouble())
    {
      result.insert(key, result.value(key).toDouble() + delta.z());
    }
  }

  return result;
}

std::optional<std::vector<size_t>> repeatGridCountsFromJson(
  const QJsonObject& operation, QString& error)
{
  const auto value = operation.value("counts");
  if (value.isDouble())
  {
    const auto count = static_cast<size_t>(value.toInteger(0));
    if (count == 0 || count > 256)
    {
      error = "repeat_grid counts must be between 1 and 256";
      return std::nullopt;
    }
    return std::vector<size_t>{count};
  }
  if (!value.isArray())
  {
    error = "repeat_grid requires counts integer or array";
    return std::nullopt;
  }

  const auto array = value.toArray();
  if (array.isEmpty() || array.size() > 3)
  {
    error = "repeat_grid counts must contain between one and three integers";
    return std::nullopt;
  }

  auto result = std::vector<size_t>{};
  result.reserve(static_cast<size_t>(array.size()));
  for (auto i = 0; i < array.size(); ++i)
  {
    const auto count = static_cast<size_t>(array[i].toInteger(0));
    if (count == 0 || count > 256)
    {
      error = QString{"repeat_grid counts[%1] must be between 1 and 256"}.arg(i);
      return std::nullopt;
    }
    result.push_back(count);
  }
  return result;
}

std::optional<std::vector<vm::vec3d>> repeatGridOffsetsFromJson(
  const QJsonObject& operation, const size_t expectedCount, QString& error)
{
  const auto value = operation.value("offsets");
  if (expectedCount == 1)
  {
    const auto offset = mcpVec3FromJsonValue(value, "offsets", error);
    if (offset)
    {
      return std::vector<vm::vec3d>{*offset};
    }
    error.clear();
  }
  if (!value.isArray())
  {
    error = "repeat_grid requires offsets array";
    return std::nullopt;
  }

  const auto array = value.toArray();
  if (static_cast<size_t>(array.size()) != expectedCount)
  {
    error = "repeat_grid offsets length must match counts length";
    return std::nullopt;
  }

  auto result = std::vector<vm::vec3d>{};
  result.reserve(expectedCount);
  for (auto i = 0; i < array.size(); ++i)
  {
    const auto offset =
      mcpVec3FromJsonValue(array[i], QString{"offsets[%1]"}.arg(i), error);
    if (!offset)
    {
      return std::nullopt;
    }
    result.push_back(*offset);
  }
  return result;
}

size_t repeatGridInstanceCount(const std::vector<size_t>& counts)
{
  auto result = size_t{1};
  for (const auto count : counts)
  {
    result *= count;
  }
  return result;
}

bool partRequested(const QJsonObject& operation, const QString& partName)
{
  const auto parts = operation.value("parts");
  if (!parts.isArray())
  {
    return true;
  }
  const auto partArray = parts.toArray();
  if (partArray.isEmpty())
  {
    return false;
  }
  return jsonArrayContainsString(parts, partName, Qt::CaseInsensitive);
}

bool explicitPartRequested(const QJsonObject& operation, const QString& partName)
{
  return jsonArrayContainsString(operation.value("parts"), partName, Qt::CaseInsensitive);
}

QJsonArray requestedPartsJson(const QJsonObject& operation)
{
  const auto parts = operation.value("parts");
  if (!parts.isArray())
  {
    return QJsonArray{
      "floor", "ceiling", "inner_wall", "outer_wall", "start_cap", "end_cap"};
  }
  return parts.toArray();
}

void incrementObjectCount(QJsonObject& object, const QString& key, const int amount = 1)
{
  if (key.isEmpty())
  {
    return;
  }
  object.insert(key, object.value(key).toInt() + amount);
}

QJsonArray jsonArrayFromOperations(
  const std::vector<QJsonObject>& operations, const int limit = 512)
{
  auto result = QJsonArray{};
  const auto count = std::min<int>(static_cast<int>(operations.size()), limit);
  for (auto i = 0; i < count; ++i)
  {
    result.push_back(operations[static_cast<size_t>(i)]);
  }
  return result;
}

QJsonObject expansionSummaryJson(
  const QJsonArray& sourceOperations,
  const std::vector<QJsonObject>& expandedOperations,
  const QJsonArray& warnings,
  const double grid)
{
  auto brushCountByType = QJsonObject{};
  auto brushCountByPart = QJsonObject{};
  auto partsRequested = QJsonArray{};
  auto derivedParameters = QJsonObject{{"grid", grid}};
  auto segmentCount = 0;

  for (const auto& value : sourceOperations)
  {
    if (!value.isObject())
    {
      continue;
    }
    const auto operation = value.toObject();
    const auto type = operation.value("type").toString().trimmed().toLower();
    if (type == "curved_corridor")
    {
      segmentCount += static_cast<int>(optionalSize(operation, "segments", 12));
      if (partsRequested.isEmpty())
      {
        partsRequested = requestedPartsJson(operation);
      }
      derivedParameters.insert(
        "wallThickness", optionalDouble(operation, "wallThickness", 16.0));
      derivedParameters.insert(
        "floorThickness", optionalDouble(operation, "floorThickness", 16.0));
      derivedParameters.insert(
        "ceilingThickness", optionalDouble(operation, "ceilingThickness", 16.0));
      derivedParameters.insert("caps", operation.value("caps").toString("none"));
      derivedParameters.insert(
        "terracedSlope",
        std::abs(
          optionalDouble(
            operation, "slopeEndZ", optionalDouble(operation, "slopeStartZ", 0.0))
          - optionalDouble(operation, "slopeStartZ", 0.0))
          > GeometryEpsilon);
    }
  }

  for (const auto& operation : expandedOperations)
  {
    const auto type = operation.value("type").toString().trimmed().toLower();
    incrementObjectCount(brushCountByType, type.isEmpty() ? "unknown" : type);
    if (operation.value("metadata").isObject())
    {
      incrementObjectCount(
        brushCountByPart,
        operation.value("metadata").toObject().value("part").toString());
    }
  }

  auto result = QJsonObject{
    {"sourceOperationCount", sourceOperations.size()},
    {"expandedOperationCount", static_cast<int>(expandedOperations.size())},
    {"brushCountByType", brushCountByType},
    {"brushCountByPart", brushCountByPart},
    {"warnings", warnings},
    {"derivedParameters", derivedParameters},
  };
  if (!partsRequested.isEmpty())
  {
    result.insert("partsRequested", partsRequested);
  }
  if (segmentCount > 0)
  {
    result.insert("segments", segmentCount);
  }
  return result;
}

QJsonObject targetMetadataMixJson(
  mdl::Map& map,
  const std::vector<mdl::BrushNode*>& brushes,
  const std::map<QString, McpBrushMetadataRecord>* metadataStore,
  const McpObjectRegistry* objectRegistry)
{
  auto partCounts = QJsonObject{};
  auto roleCounts = QJsonObject{};
  auto metadataCount = 0;
  for (auto* brush : brushes)
  {
    if (brush == nullptr)
    {
      continue;
    }
    const auto metadata =
      metadataForBrushNode(map, *brush, metadataStore, objectRegistry);
    if (!metadata)
    {
      continue;
    }
    ++metadataCount;
    incrementObjectCount(partCounts, metadata->value("part").toString());
    incrementObjectCount(roleCounts, metadata->value("role").toString());
  }

  auto result = QJsonObject{
    {"metadataCount", metadataCount},
    {"partCounts", partCounts},
    {"roleCounts", roleCounts},
    {"mixedParts", partCounts.size() > 1},
    {"mixedRoles", roleCounts.size() > 1},
  };
  if (partCounts.size() > 1 || roleCounts.size() > 1)
  {
    result.insert(
      "mixedTargetWarning",
      "Matched route continuity targets contain multiple metadata parts or roles. "
      "Pass an explicit selector such as moduleId + role:\"walkable\" or moduleId + "
      "part:\"floor\" so rails, walls, supports, markers, and caps are not analyzed "
      "as route surfaces.");
    result.insert(
      "recommendedSelector",
      QJsonObject{
        {"metadata", QJsonObject{{"role", "walkable"}}},
      });
  }
  return result;
}

QString partMaterial(
  const QJsonObject& operation, const QString& partName, const QString& fallback)
{
  if (const auto partMaterials = operation.value("partMaterials");
      partMaterials.isObject())
  {
    const auto material = partMaterials.toObject().value(partName).toString().trimmed();
    if (!material.isEmpty())
    {
      return material;
    }
  }
  return fallback;
}

QJsonObject partMetadata(
  const QJsonObject& operation, const QString& partName, const QJsonObject& base)
{
  auto result = mergeMetadataObjects(base, operation.value("metadata").toObject());
  result.insert("part", partName);
  if (partName == "floor")
  {
    result.insert("role", "walkable");
  }
  else if (
    partName == "ceiling" || partName.endsWith("_wall") || partName.endsWith("_cap"))
  {
    result.insert("role", "boundary");
  }
  if (const auto partMetadataValue = operation.value("partMetadata");
      partMetadataValue.isObject())
  {
    const auto partMetadataObject = partMetadataValue.toObject();
    if (partMetadataObject.value(partName).isObject())
    {
      result =
        mergeMetadataObjects(result, partMetadataObject.value(partName).toObject());
    }
  }
  return result;
}

QJsonObject withGeneratedPartMetadata(
  QJsonObject operation,
  const QString& partName,
  const QString& material,
  const QJsonObject& baseMetadata)
{
  operation.insert("metadata", partMetadata(operation, partName, baseMetadata));
  if (!material.isEmpty())
  {
    operation.insert("material", material);
  }
  return operation;
}

QString pathRibbonOutputPartName(const QJsonObject& operation)
{
  if (explicitPartRequested(operation, "floor"))
  {
    return "floor";
  }
  if (explicitPartRequested(operation, "ribbon"))
  {
    return "ribbon";
  }
  return "surface";
}

std::vector<QJsonObject> curvedCorridorOperationsFromParams(
  const QJsonObject& params, const QJsonObject& baseMetadata)
{
  const auto center = params.value("center").toArray();
  const auto innerRadius = optionalDouble(params, "innerRadius", 128.0);
  const auto outerRadius = optionalDouble(params, "outerRadius", 224.0);
  const auto startAngle = optionalDouble(params, "startAngle", -90.0);
  const auto turnDegrees = optionalDouble(params, "turnDegrees", 180.0);
  const auto height = optionalDouble(params, "height", 128.0);
  const auto segments = std::max<size_t>(1, optionalSize(params, "segments", 12));
  const auto wallThickness = optionalDouble(params, "wallThickness", 16.0);
  const auto floorThickness = optionalDouble(params, "floorThickness", 16.0);
  const auto ceilingThickness = optionalDouble(params, "ceilingThickness", 16.0);
  const auto caps = params.value("caps").toString("none").toLower();
  const auto material = params.value("material").toString();
  const auto snapMode = params.value("snapMode").toString("radial").toLower();
  const auto slopeStartZ = optionalDouble(params, "slopeStartZ", 0.0);
  const auto slopeEndZ = optionalDouble(params, "slopeEndZ", slopeStartZ);

  auto operations = std::vector<QJsonObject>{};
  operations.reserve(segments * 4 + 2);

  const auto step = turnDegrees / static_cast<double>(segments);
  for (size_t i = 0; i < segments; ++i)
  {
    const auto a0 = startAngle + step * static_cast<double>(i);
    const auto a1 = a0 + step;
    const auto t0 = static_cast<double>(i) / static_cast<double>(segments);
    const auto t1 = static_cast<double>(i + 1) / static_cast<double>(segments);
    const auto segmentT = (t0 + t1) * 0.5;
    const auto segmentBaseZ = slopeStartZ + (slopeEndZ - slopeStartZ) * segmentT;
    const auto addSector = [&](
                             const QString& partName,
                             const double inner,
                             const double outer,
                             const double minZ,
                             const double maxZ) {
      if (!partRequested(params, partName))
      {
        return;
      }
      auto op = QJsonObject{
        {"type", "cylinder_sector"},
        {"center", center},
        {"innerRadius", inner},
        {"outerRadius", outer},
        {"startAngle", a0},
        {"endAngle", a1},
        {"minZ", minZ + segmentBaseZ},
        {"maxZ", maxZ + segmentBaseZ},
        {"snapMode", snapMode},
      };
      const auto partMeta = partMetadata(params, partName, baseMetadata);
      op.insert("metadata", partMeta);
      const auto materialName = partMaterial(params, partName, material);
      if (!materialName.isEmpty())
      {
        op.insert("material", materialName);
      }
      operations.push_back(std::move(op));
    };

    addSector("floor", innerRadius, outerRadius, 0.0, floorThickness);
    addSector("ceiling", innerRadius, outerRadius, height, height + ceilingThickness);
    addSector(
      "inner_wall", innerRadius - wallThickness, innerRadius, floorThickness, height);
    addSector(
      "outer_wall", outerRadius, outerRadius + wallThickness, floorThickness, height);
  }

  const auto addCap = [&](const QString& partName, const double angle) {
    if (!partRequested(params, partName) && !partRequested(params, "caps"))
    {
      return;
    }
    const auto half = std::abs(step) / 2.0;
    auto op = QJsonObject{
      {"type", "cylinder_sector"},
      {"center", center},
      {"innerRadius", innerRadius - wallThickness},
      {"outerRadius", outerRadius + wallThickness},
      {"startAngle", angle - half},
      {"endAngle", angle + half},
      {"minZ", angle == startAngle ? slopeStartZ : slopeEndZ},
      {"maxZ",
       (angle == startAngle ? slopeStartZ : slopeEndZ) + height + ceilingThickness},
      {"snapMode", snapMode},
      {"metadata", partMetadata(params, partName, baseMetadata)},
    };
    const auto materialName = partMaterial(params, partName, material);
    if (!materialName.isEmpty())
    {
      op.insert("material", materialName);
    }
    operations.push_back(std::move(op));
  };

  if (caps == "start" || caps == "both")
  {
    addCap("start_cap", startAngle);
  }
  if (caps == "end" || caps == "both")
  {
    addCap("end_cap", startAngle + turnDegrees);
  }
  return operations;
}

bool validateArcRampParams(
  const QJsonObject& params, QString& error, const QString& typeName = "arc_ramp")
{
  const auto centerValue = params.value("center");
  if (!centerValue.isArray() || centerValue.toArray().size() != 3)
  {
    error = QString{"center must be provided for %1 as [x,y,z]"}.arg(typeName);
    return false;
  }
  const auto radius = optionalDouble(params, "radius", 256.0);
  const auto width = optionalDouble(params, "width", 128.0);
  const auto thickness = optionalDouble(params, "thickness", 16.0);
  const auto turnDegrees = optionalDouble(params, "turnDegrees", 90.0);
  const auto segments = optionalSize(params, "segments", 12);
  if (!finitePositive(radius) || !finitePositive(width) || !finitePositive(thickness))
  {
    error =
      QString{"%1 radius, width, and thickness must be greater than zero"}.arg(typeName);
    return false;
  }
  if (width >= radius * 2.0)
  {
    error = QString{"%1 width must be smaller than diameter"}.arg(typeName);
    return false;
  }
  if (
    !std::isfinite(turnDegrees) || std::abs(turnDegrees) <= GeometryEpsilon
    || std::abs(turnDegrees) > 360.0)
  {
    error =
      QString{"%1 turnDegrees must be greater than zero and at most 360 degrees"}.arg(
        typeName);
    return false;
  }
  if (segments < 1 || segments > 512)
  {
    error = QString{"%1 segments must be between 1 and 512"}.arg(typeName);
    return false;
  }
  if (std::abs(turnDegrees) / static_cast<double>(segments) > 45.0)
  {
    error =
      "arc_ramp segments too low; use enough segments so each ramp chord is <= 45 "
      "degrees";
    return false;
  }
  if (!std::isfinite(optionalDouble(params, "rise", 0.0)))
  {
    error = QString{"%1 rise must be finite"}.arg(typeName);
    return false;
  }
  return true;
}

bool validateCurvedCorridorParams(const QJsonObject& params, QString& error);

std::vector<QJsonObject> arcRampOperationsFromParams(
  const QJsonObject& params, const QJsonObject& baseMetadata)
{
  const auto centerValue = params.value("center").toArray();
  const auto center = vm::vec3d{
    centerValue[0].toDouble(), centerValue[1].toDouble(), centerValue[2].toDouble()};
  const auto radius = optionalDouble(params, "radius", 256.0);
  const auto width = optionalDouble(params, "width", 128.0);
  const auto startAngle = optionalDouble(params, "startAngle", 0.0);
  const auto turnDegrees = optionalDouble(params, "turnDegrees", 90.0);
  const auto rise = optionalDouble(params, "rise", 0.0);
  const auto segments = std::max<size_t>(1, optionalSize(params, "segments", 12));
  const auto thickness = optionalDouble(params, "thickness", 16.0);
  const auto material = params.value("material").toString();
  const auto orderStart =
    optionalDouble(params, "orderStart", baseMetadata.value("order").toDouble(0.0));
  const auto orderStep = optionalDouble(params, "orderStep", 1.0);

  auto operations = std::vector<QJsonObject>{};
  operations.reserve(segments);
  const auto angleStep = turnDegrees / static_cast<double>(segments);
  const auto innerRadius = radius - width * 0.5;
  const auto outerRadius = radius + width * 0.5;
  for (size_t i = 0; i < segments; ++i)
  {
    const auto t0 = static_cast<double>(i) / static_cast<double>(segments);
    const auto t1 = static_cast<double>(i + 1u) / static_cast<double>(segments);
    const auto segmentStartAngle = startAngle + angleStep * static_cast<double>(i);
    const auto segmentEndAngle = startAngle + angleStep * static_cast<double>(i + 1u);
    auto metadata = baseMetadata;
    if (const auto opMetadata = params.value("metadata"); opMetadata.isObject())
    {
      metadata = mergeMetadataObjects(metadata, opMetadata.toObject());
    }
    metadata.insert("order", orderStart + orderStep * static_cast<double>(i));
    if (!metadata.contains("part"))
    {
      metadata.insert("part", "ramp");
    }
    if (!metadata.contains("role"))
    {
      metadata.insert("role", "walkable");
    }
    auto op = QJsonObject{
      {"type", "arc_ramp_segment"},
      {"center", vecToJson(center)},
      {"innerRadius", innerRadius},
      {"outerRadius", outerRadius},
      {"startAngle", segmentStartAngle},
      {"endAngle", segmentEndAngle},
      {"startZ", rise * t0},
      {"endZ", rise * t1},
      {"thickness", thickness},
      {"metadata", metadata},
    };
    if (!material.isEmpty())
    {
      op.insert("material", material);
    }
    operations.push_back(std::move(op));
  }
  return operations;
}

std::vector<QJsonObject> expandedBatchOperations(
  const QJsonArray& operations, const QJsonObject& defaultMetadata = {})
{
  auto result = std::vector<QJsonObject>{};
  for (const auto& operationValue : operations)
  {
    if (!operationValue.isObject())
    {
      continue;
    }
    const auto operation = operationValue.toObject();
    const auto type = operation.value("type").toString().trimmed().toLower();
    if (type == "curved_corridor")
    {
      auto error = QString{};
      if (!validateCurvedCorridorParams(operation, error))
      {
        result.push_back(operation);
        continue;
      }
      for (const auto& child :
           curvedCorridorOperationsFromParams(operation, defaultMetadata))
      {
        result.push_back(child);
      }
    }
    else if (type == "arc_ramp" || type == "helical_ramp")
    {
      auto error = QString{};
      if (!validateArcRampParams(operation, error, type))
      {
        result.push_back(operation);
        continue;
      }
      for (const auto& child : arcRampOperationsFromParams(operation, defaultMetadata))
      {
        result.push_back(child);
      }
    }
    else
    {
      result.push_back(operation);
    }
  }
  return result;
}

std::optional<std::vector<PathRibbonSegment>> pathRibbonSegmentsFromParams(
  const QJsonObject& params, const double grid, QString& error)
{
  auto zValues = std::vector<double>{};
  auto points2d = std::vector<vm::vec2d>{};
  if (params.value("points3d").isArray())
  {
    const auto points3d = centerlinePoints3DFromJson(params, "points3d", error);
    if (!points3d)
    {
      return std::nullopt;
    }
    points2d.reserve(points3d->size());
    zValues.reserve(points3d->size());
    for (const auto& point : *points3d)
    {
      points2d.emplace_back(point.x(), point.y());
      zValues.push_back(snapToGrid(point.z(), grid));
    }
  }
  else
  {
    const auto points = points2DFromJson(params, "points2d", 2u, error);
    if (!points)
    {
      return std::nullopt;
    }
    points2d = *points;
  }

  if (points2d.size() < 2)
  {
    error = params.value("points3d").isArray()
              ? "points3d must contain at least two centerline points"
              : "points2d must contain at least two centerline points";
    return std::nullopt;
  }

  const auto width = optionalDouble(params, "width", 128.0);
  if (!finitePositive(width))
  {
    error = "width must be greater than zero";
    return std::nullopt;
  }

  auto snappedPoints = snapPointsToGrid(points2d, grid);
  auto normals = std::vector<vm::vec2d>{};
  normals.reserve(snappedPoints.size() - 1);
  for (size_t i = 0; i + 1 < snappedPoints.size(); ++i)
  {
    const auto direction = snappedPoints[i + 1] - snappedPoints[i];
    if (vm::is_zero(direction, GeometryEpsilon))
    {
      error = QString{"%1[%2] and %1[%3] collapse after snapping"}
                .arg(params.value("points3d").isArray() ? "points3d" : "points2d")
                .arg(i)
                .arg(i + 1);
      return std::nullopt;
    }

    const auto tangent = vm::normalize(direction);
    normals.push_back(vm::vec2d{-tangent.y(), tangent.x()});
  }

  auto segments = std::vector<PathRibbonSegment>{};
  segments.reserve(snappedPoints.size() - 1);
  const auto halfWidth = snapToGrid(width * 0.5, grid);
  const auto miterLimit = optionalDouble(params, "miterLimit", 4.0);
  if (!finitePositive(halfWidth))
  {
    error = "width snaps to zero";
    return std::nullopt;
  }
  if (!finitePositive(miterLimit))
  {
    error = "miterLimit must be greater than zero";
    return std::nullopt;
  }

  auto leftOffsets = std::vector<vm::vec2d>{};
  auto rightOffsets = std::vector<vm::vec2d>{};
  leftOffsets.reserve(snappedPoints.size());
  rightOffsets.reserve(snappedPoints.size());
  for (size_t i = 0; i < snappedPoints.size(); ++i)
  {
    auto offset = vm::vec2d{};
    if (i == 0)
    {
      offset = normals.front() * halfWidth;
    }
    else if (i + 1 == snappedPoints.size())
    {
      offset = normals.back() * halfWidth;
    }
    else
    {
      const auto previousNormal = normals[i - 1];
      const auto nextNormal = normals[i];
      auto miter = previousNormal + nextNormal;
      if (vm::is_zero(miter, GeometryEpsilon))
      {
        error = QString{"path_ribbon turn at points2d[%1] is too sharp"}.arg(i);
        return std::nullopt;
      }
      miter = vm::normalize(miter);
      const auto denominator =
        miter.x() * previousNormal.x() + miter.y() * previousNormal.y();
      if (std::abs(denominator) <= GeometryEpsilon)
      {
        error = QString{"path_ribbon turn at points2d[%1] is too sharp"}.arg(i);
        return std::nullopt;
      }
      const auto miterLength = halfWidth / denominator;
      if (std::abs(miterLength) > halfWidth * miterLimit)
      {
        error = QString{"path_ribbon turn at points2d[%1] exceeds miterLimit"}.arg(i);
        return std::nullopt;
      }
      offset = miter * miterLength;
    }
    leftOffsets.push_back(snapToGrid(snappedPoints[i] + offset, grid));
    rightOffsets.push_back(snapToGrid(snappedPoints[i] - offset, grid));
  }

  for (size_t i = 0; i + 1 < snappedPoints.size(); ++i)
  {
    auto polygon = std::vector<vm::vec2d>{
      leftOffsets[i],
      leftOffsets[i + 1],
      rightOffsets[i + 1],
      rightOffsets[i],
    };

    polygon = snapPointsToGrid(std::move(polygon), grid);
    if (!isStrictlyConvexPolygon(polygon))
    {
      error = QString{"path_ribbon segment %1 is not a convex brush footprint"}.arg(i);
      return std::nullopt;
    }
    auto segment = PathRibbonSegment{};
    segment.polygon = std::move(polygon);
    if (!zValues.empty())
    {
      const auto baseZ = std::min(zValues[i], zValues[i + 1]);
      segment.minZ = snapToGrid(baseZ + optionalDouble(params, "zOffset", 0.0), grid);
      const auto segmentThickness =
        optionalDouble(params, "thickness", optionalDouble(params, "height", 16.0));
      segment.maxZ = snapToGrid(segment.minZ + segmentThickness, grid);
    }
    segments.push_back(std::move(segment));
  }

  if (segments.empty())
  {
    error = "path_ribbon generated no segments";
    return std::nullopt;
  }
  return segments;
}

std::vector<mdl::Node*> createPathRibbonNodes(
  const mdl::BrushBuilder& builder,
  const QJsonObject& operation,
  const double grid,
  const std::string& material,
  QString& error)
{
  auto segments = pathRibbonSegmentsFromParams(operation, grid, error);
  if (!segments)
  {
    return {};
  }

  const auto minZ = snapToGrid(optionalDouble(operation, "minZ", 0.0), grid);
  const auto maxZ = snapToGrid(optionalDouble(operation, "maxZ", 16.0), grid);
  if (operation.value("points3d").isArray())
  {
    for (auto& segment : *segments)
    {
      if (
        !(std::isfinite(segment.minZ) && std::isfinite(segment.maxZ))
        || segment.minZ >= segment.maxZ)
      {
        error = "points3d path_ribbon thickness/height must keep segment minZ below maxZ";
        return {};
      }
    }
  }
  else if (!(std::isfinite(minZ) && std::isfinite(maxZ)) || minZ >= maxZ)
  {
    error = "minZ must be smaller than maxZ";
    return {};
  }

  auto result = std::vector<mdl::Node*>{};
  result.reserve(segments->size());
  for (auto segment : *segments)
  {
    if (!operation.value("points3d").isArray())
    {
      segment.minZ = minZ;
      segment.maxZ = maxZ;
    }
    auto brush = createPrismBrush(
      builder, segment.polygon, segment.minZ, segment.maxZ, material, error);
    if (!brush)
    {
      deleteNodes(result);
      return {};
    }
    result.push_back(new mdl::BrushNode{std::move(*brush)});
  }
  return result;
}

bool validateCurvedCorridorParams(const QJsonObject& params, QString& error)
{
  const auto innerRadius = optionalDouble(params, "innerRadius", 128.0);
  const auto outerRadius = optionalDouble(params, "outerRadius", 224.0);
  const auto turnDegrees = optionalDouble(params, "turnDegrees", 180.0);
  const auto segments = optionalSize(params, "segments", 12);
  const auto height = optionalDouble(params, "height", 128.0);
  const auto wallThickness = optionalDouble(params, "wallThickness", 16.0);
  const auto floorThickness = optionalDouble(params, "floorThickness", 16.0);
  const auto ceilingThickness = optionalDouble(params, "ceilingThickness", 16.0);
  const auto caps = params.value("caps").toString("none").toLower();
  const auto snapMode = params.value("snapMode").toString("radial").toLower();
  const auto slopeStartZ = optionalDouble(params, "slopeStartZ", 0.0);
  const auto slopeEndZ = optionalDouble(params, "slopeEndZ", slopeStartZ);

  if (!params.value("center").isArray())
  {
    error = "center must be provided for curved corridor";
    return false;
  }
  if (
    !finitePositive(innerRadius) || !finitePositive(outerRadius)
    || innerRadius >= outerRadius)
  {
    error = "innerRadius must be greater than zero and smaller than outerRadius";
    return false;
  }
  if (
    !finitePositive(height) || !finitePositive(wallThickness)
    || !finitePositive(floorThickness) || !finitePositive(ceilingThickness))
  {
    if (!finitePositive(height))
    {
      error = "curved_corridor height must be greater than zero";
    }
    else if (!finitePositive(wallThickness))
    {
      error =
        "curved_corridor wallThickness must be greater than zero for inner_wall/"
        "outer_wall parts";
    }
    else if (!finitePositive(floorThickness))
    {
      error = "curved_corridor floorThickness must be greater than zero for floor part";
    }
    else
    {
      error =
        "curved_corridor ceilingThickness must be greater than zero for ceiling part";
    }
    return false;
  }
  if (!std::isfinite(slopeStartZ) || !std::isfinite(slopeEndZ))
  {
    error = "slopeStartZ and slopeEndZ must be finite";
    return false;
  }
  if (innerRadius <= wallThickness)
  {
    error =
      "curved_corridor innerRadius must be larger than wallThickness so inner_wall "
      "has positive radius";
    return false;
  }
  if (segments < 1 || segments > 128)
  {
    error = "segments must be between 1 and 128";
    return false;
  }
  if (
    !std::isfinite(turnDegrees) || std::abs(turnDegrees) <= GeometryEpsilon
    || std::abs(turnDegrees) > 360.0)
  {
    error =
      "curved_corridor turnDegrees must be finite, greater than zero, and at most "
      "360 degrees";
    return false;
  }
  if (std::abs(turnDegrees) / static_cast<double>(segments) > 180.0)
  {
    error = "segments too low; use enough segments so each sector is <= 180 degrees";
    return false;
  }
  if (caps != "none" && caps != "start" && caps != "end" && caps != "both")
  {
    error = "caps must be none, start, end, or both";
    return false;
  }
  if (snapMode != "grid" && snapMode != "radial" && snapMode != "none")
  {
    error = "snapMode must be grid, radial, or none";
    return false;
  }
  return true;
}

std::vector<mdl::Node*> compileBatchOperation(
  const mdl::Map& map,
  const mdl::BrushBuilder& builder,
  const QJsonObject& operation,
  const std::string& defaultMaterial,
  const double grid,
  QString& error,
  const QJsonObject& defaultMetadata = {})
{
  const auto type = operation.value("type").toString().trimmed().toLower();
  const auto material = mcpOptionalString(operation, "material", defaultMaterial);
  const auto operationMetadata =
    mergeMetadataObjects(defaultMetadata, operation.value("metadata").toObject());

  if (type == "repeat_translate")
  {
    if (!operation.value("operation").isObject())
    {
      error = "repeat_translate requires child operation object";
      return {};
    }
    const auto offset = mcpVec3FromJson(operation, "offset", error);
    if (!offset)
    {
      return {};
    }
    const auto count = optionalSize(operation, "count", 0);
    if (count == 0 || count > 256)
    {
      error = "repeat_translate count must be between 1 and 256";
      return {};
    }
    if (count > 1 && vm::is_zero(*offset, GeometryEpsilon))
    {
      error = "repeat_translate offset must be non-zero when count is greater than one";
      return {};
    }

    auto result = std::vector<mdl::Node*>{};
    auto childOperation = operation.value("operation").toObject();
    if (childOperation.value("type").toString().trimmed().toLower() == "repeat_translate")
    {
      error = "repeat_translate child operation cannot be another repeat_translate";
      return {};
    }
    for (size_t i = 0; i < count; ++i)
    {
      const auto translatedOperation =
        translatedBatchOperation(childOperation, *offset * static_cast<double>(i), error);
      if (!translatedOperation)
      {
        deleteNodes(result);
        return {};
      }

      auto childNodes = compileBatchOperation(
        map, builder, *translatedOperation, material, grid, error, operationMetadata);
      if (!error.isEmpty())
      {
        deleteNodes(result);
        deleteNodes(childNodes);
        return {};
      }
      result.insert(result.end(), childNodes.begin(), childNodes.end());
    }
    return result;
  }

  if (type == "repeat_grid")
  {
    if (!operation.value("operation").isObject())
    {
      error = "repeat_grid requires child operation object";
      return {};
    }
    const auto counts = repeatGridCountsFromJson(operation, error);
    if (!counts)
    {
      return {};
    }
    const auto offsets = repeatGridOffsetsFromJson(operation, counts->size(), error);
    if (!offsets)
    {
      return {};
    }
    const auto instanceCount = repeatGridInstanceCount(*counts);
    if (instanceCount == 0 || instanceCount > 4096)
    {
      error = "repeat_grid total instance count must be between 1 and 4096";
      return {};
    }
    for (size_t i = 0; i < counts->size(); ++i)
    {
      if ((*counts)[i] > 1 && vm::is_zero((*offsets)[i], GeometryEpsilon))
      {
        error =
          QString{
            "repeat_grid offsets[%1] must be non-zero when count is greater "
            "than one"}
            .arg(i);
        return {};
      }
    }

    auto childOperation = operation.value("operation").toObject();
    const auto childType = childOperation.value("type").toString().trimmed().toLower();
    if (childType == "repeat_translate" || childType == "repeat_grid")
    {
      error = "repeat_grid child operation cannot be another repeat operation";
      return {};
    }

    auto result = std::vector<mdl::Node*>{};
    for (size_t instance = 0; instance < instanceCount; ++instance)
    {
      auto remainder = instance;
      auto delta = vm::vec3d{};
      for (size_t axisIndex = 0; axisIndex < counts->size(); ++axisIndex)
      {
        const auto axisStep = remainder % (*counts)[axisIndex];
        remainder /= (*counts)[axisIndex];
        delta = delta + (*offsets)[axisIndex] * static_cast<double>(axisStep);
      }

      const auto translatedOperation =
        translatedBatchOperation(childOperation, delta, error);
      if (!translatedOperation)
      {
        deleteNodes(result);
        return {};
      }

      auto childNodes = compileBatchOperation(
        map, builder, *translatedOperation, material, grid, error, operationMetadata);
      if (!error.isEmpty())
      {
        deleteNodes(result);
        deleteNodes(childNodes);
        return {};
      }
      result.insert(result.end(), childNodes.begin(), childNodes.end());
    }
    return result;
  }

  if (type == "curved_corridor")
  {
    if (!validateCurvedCorridorParams(operation, error))
    {
      return {};
    }
    auto result = std::vector<mdl::Node*>{};
    for (const auto& childOperation :
         curvedCorridorOperationsFromParams(operation, operationMetadata))
    {
      auto childNodes = compileBatchOperation(
        map, builder, childOperation, material, grid, error, operationMetadata);
      if (!error.isEmpty())
      {
        deleteNodes(result);
        deleteNodes(childNodes);
        return {};
      }
      result.insert(result.end(), childNodes.begin(), childNodes.end());
    }
    return result;
  }

  if (type == "arc_ramp" || type == "helical_ramp")
  {
    if (!validateArcRampParams(operation, error, type))
    {
      return {};
    }
    auto result = std::vector<mdl::Node*>{};
    for (const auto& childOperation :
         arcRampOperationsFromParams(operation, operationMetadata))
    {
      auto childNodes = compileBatchOperation(
        map, builder, childOperation, material, grid, error, operationMetadata);
      if (!error.isEmpty())
      {
        deleteNodes(result);
        deleteNodes(childNodes);
        return {};
      }
      result.insert(result.end(), childNodes.begin(), childNodes.end());
    }
    return result;
  }

  if (type == "path_ribbon")
  {
    if (
      !partRequested(operation, "floor") && !partRequested(operation, "surface")
      && !partRequested(operation, "ribbon"))
    {
      return {};
    }
    const auto partName = pathRibbonOutputPartName(operation);
    const auto surfaceMaterial =
      partMaterial(operation, partName, QString::fromStdString(material));
    return createPathRibbonNodes(
      builder,
      withGeneratedPartMetadata(operation, partName, surfaceMaterial, operationMetadata),
      grid,
      surfaceMaterial.toStdString(),
      error);
  }

  if (RemovedPrefabBatchTypes.contains(type))
  {
    error = QString{"%1 moved to trenchbroom-mcp-scene-workflow recipes"}.arg(type);
    return {};
  }

  if (type == "box")
  {
    const auto bounds = boundsFromJson(operation, error);
    if (!bounds)
    {
      return {};
    }
    const auto snappedBounds = snapBoundsToGrid(*bounds, grid, error);
    if (!snappedBounds)
    {
      return {};
    }
    return brushNodesFromBounds(builder, {*snappedBounds}, material, error);
  }

  if (type == "cylinder")
  {
    const auto bounds = boundsFromJson(operation, error);
    if (!bounds)
    {
      return {};
    }
    const auto snappedBounds = snapBoundsToGrid(*bounds, grid, error);
    if (!snappedBounds)
    {
      return {};
    }
    const auto axis = axisFromJson(operation, "axis", vm::axis::z, error);
    if (!axis)
    {
      return {};
    }
    const auto sides =
      std::clamp(optionalSize(operation, "sides", 16), size_t{3}, size_t{128});
    const auto snapMode =
      sectorSnapModeFromJson(operation, "snapMode", SectorSnapMode::Grid, error);
    if (!snapMode)
    {
      return {};
    }
    auto brush = createCylinderBrushFromPolygon(
      builder, *snappedBounds, sides, *axis, material, *snapMode, grid, error);
    if (!brush)
    {
      return {};
    }
    return {new mdl::BrushNode{std::move(*brush)}};
  }

  if (type == "stepped_mass")
  {
    const auto bounds = boundsFromJson(operation, error);
    if (!bounds)
    {
      return {};
    }
    const auto snappedBounds = snapBoundsToGrid(*bounds, grid, error);
    if (!snappedBounds)
    {
      return {};
    }
    const auto massBounds = steppedMassBounds(
      *snappedBounds,
      optionalSize(operation, "levels", 1),
      snapToGrid(optionalDouble(operation, "inset", 16.0), grid),
      snapToGrid(optionalDouble(operation, "stepHeight", 16.0), grid),
      error);
    if (!massBounds)
    {
      return {};
    }
    return brushNodesFromBounds(builder, *massBounds, material, error);
  }

  if (type == "support_posts_between")
  {
    auto points = points2DFromJson(operation, "points2d", error);
    if (!points)
    {
      return {};
    }
    const auto postBounds = supportPostBounds(
      snapPointsToGrid(*points, grid),
      snapToGrid(optionalDouble(operation, "bottomZ", 0.0), grid),
      snapToGrid(optionalDouble(operation, "topZ", 128.0), grid),
      snapToGrid(optionalDouble(operation, "postSize", 16.0), grid),
      error);
    if (!postBounds)
    {
      return {};
    }
    return brushNodesFromBounds(builder, *postBounds, material, error);
  }

  if (type == "ramp_between")
  {
    const auto start = mcpVec3FromJson(operation, "start", error);
    if (!start)
    {
      return {};
    }
    const auto end = mcpVec3FromJson(operation, "end", error);
    if (!end)
    {
      return {};
    }
    const auto width = snapToGrid(optionalDouble(operation, "width", 128.0), grid);
    const auto thickness = snapToGrid(optionalDouble(operation, "thickness", 16.0), grid);
    auto brush = createRampBetweenBrush(
      builder,
      snapToGrid(*start, grid),
      snapToGrid(*end, grid),
      width,
      thickness,
      material,
      error);
    if (brush.is_error())
    {
      error = error.isEmpty() ? "Could not create ramp_between brush" : error;
      return {};
    }
    return {new mdl::BrushNode{std::move(brush).value()}};
  }

  if (type == "arc_ramp_segment")
  {
    const auto center = mcpVec3FromJson(operation, "center", error);
    if (!center)
    {
      return {};
    }
    const auto innerRadius = optionalDouble(operation, "innerRadius", 0.0);
    const auto outerRadius = optionalDouble(operation, "outerRadius", 0.0);
    const auto thickness = snapToGrid(optionalDouble(operation, "thickness", 16.0), grid);
    if (
      !finitePositive(innerRadius) || !finitePositive(outerRadius)
      || outerRadius <= innerRadius + GeometryEpsilon)
    {
      error = "arc_ramp_segment requires positive innerRadius < outerRadius";
      return {};
    }
    if (!finitePositive(thickness))
    {
      error = "arc_ramp_segment thickness must be greater than zero";
      return {};
    }
    const auto startAngle = optionalDouble(operation, "startAngle", 0.0);
    const auto endAngle = optionalDouble(operation, "endAngle", startAngle);
    if (
      !std::isfinite(startAngle) || !std::isfinite(endAngle)
      || std::abs(endAngle - startAngle) <= GeometryEpsilon
      || std::abs(endAngle - startAngle) > 45.0)
    {
      error = "arc_ramp_segment angle span must be finite, non-zero, and <= 45 degrees";
      return {};
    }
    const auto startZ = optionalDouble(operation, "startZ", 0.0);
    const auto endZ = optionalDouble(operation, "endZ", startZ);
    if (!std::isfinite(startZ) || !std::isfinite(endZ))
    {
      error = "arc_ramp_segment startZ/endZ must be finite";
      return {};
    }
    auto brush = createArcRampSegmentBrush(
      builder,
      *center,
      innerRadius,
      outerRadius,
      startAngle,
      endAngle,
      snapToGrid(startZ, grid),
      snapToGrid(endZ, grid),
      thickness,
      material);
    if (brush.is_error())
    {
      error = "Could not create arc_ramp_segment brush";
      return {};
    }
    return {new mdl::BrushNode{std::move(brush).value()}};
  }

  if (type == "ramp" || type == "wedge")
  {
    const auto bounds = boundsFromJson(operation, error);
    if (!bounds)
    {
      return {};
    }
    const auto snappedBounds = snapBoundsToGrid(*bounds, grid, error);
    if (!snappedBounds)
    {
      return {};
    }
    const auto axis = axisFromJson(operation, "axis", vm::axis::x, error);
    if (!axis)
    {
      return {};
    }
    auto brush = createWedgeBrush(builder, *snappedBounds, *axis, material);
    if (brush.is_error())
    {
      error = QString{"Could not create %1 brush"}.arg(type);
      return {};
    }
    return {new mdl::BrushNode{std::move(brush).value()}};
  }

  if (type == "prism")
  {
    auto points = points2DFromJson(operation, "points2d", error);
    if (!points)
    {
      return {};
    }
    auto brush = createPrismBrush(
      builder,
      snapPointsToGrid(*points, grid),
      snapToGrid(optionalDouble(operation, "minZ", 0.0), grid),
      snapToGrid(optionalDouble(operation, "maxZ", 128.0), grid),
      material,
      error);
    if (!brush)
    {
      return {};
    }
    return {new mdl::BrushNode{std::move(*brush)}};
  }

  if (type == "polyhedron")
  {
    auto points = points3DFromJson(operation, "points", error);
    if (!points)
    {
      return {};
    }
    auto brush = builder.createBrush(snapPointsToGrid(*points, grid), material);
    if (brush.is_error())
    {
      error = "Could not create convex polyhedron brush from points";
      return {};
    }
    return {new mdl::BrushNode{std::move(brush).value()}};
  }

  if (type == "cylinder_sector")
  {
    const auto center = mcpVec3FromJson(operation, "center", error);
    if (!center)
    {
      return {};
    }
    const auto snapMode =
      sectorSnapModeFromJson(operation, "snapMode", SectorSnapMode::Grid, error);
    if (!snapMode)
    {
      return {};
    }
    auto brush = createCylinderSectorBrush(
      builder,
      vm::vec2d{center->x(), center->y()},
      optionalDouble(operation, "innerRadius", 128.0),
      optionalDouble(operation, "outerRadius", 224.0),
      optionalDouble(operation, "startAngle", 0.0),
      optionalDouble(operation, "endAngle", 15.0),
      optionalDouble(operation, "minZ", 0.0),
      optionalDouble(operation, "maxZ", 128.0),
      material,
      error,
      grid,
      *snapMode);
    if (!brush)
    {
      return {};
    }
    return {new mdl::BrushNode{std::move(*brush)}};
  }

  error = QString{"Unsupported batch operation type: %1"}.arg(type);
  return {};
}

QJsonObject brushTypeJson(
  const QString& name,
  const bool supported,
  const QString& description,
  const bool createsMultipleBrushes = false)
{
  auto result = QJsonObject{
    {"name", name},
    {"supported", supported},
    {"description", description},
    {"createsMultipleBrushes", createsMultipleBrushes},
  };
  if (!supported)
  {
    result.insert("reason", "No stable native MCP primitive generator yet");
  }
  return result;
}

QString brushTypeFromToolName(const QString& toolName, const QJsonObject& params)
{
  if (toolName == "brush_create")
  {
    return params.value("type").toString().trimmed().toLower();
  }

  static const auto Prefix = QString{"brush_create_"};
  return toolName.startsWith(Prefix) ? toolName.mid(Prefix.size()) : QString{};
}

size_t clampedSizeParam(
  const QJsonObject& params,
  const QString& key,
  const size_t defaultValue,
  const size_t minValue,
  const size_t maxValue)
{
  return std::clamp(optionalSize(params, key, defaultValue), minValue, maxValue);
}

std::optional<std::vector<mdl::Brush>> createBrushesForType(
  const mdl::Map& map,
  const mdl::BrushBuilder& builder,
  const QString& type,
  const QJsonObject& params,
  const std::string& material,
  QString& error)
{
  if (type.isEmpty())
  {
    error = "brush_create requires type";
    return std::nullopt;
  }

  if (type == "from_planes")
  {
    auto brush = createBrushFromPlaneTriples(map, params, material, error);
    if (!brush)
    {
      return std::nullopt;
    }
    return std::vector<mdl::Brush>{std::move(*brush)};
  }

  if (type == "prism")
  {
    auto points = points2DFromJson(params, "points2d", error);
    if (!points)
    {
      return std::nullopt;
    }
    auto brush = createPrismBrush(
      builder,
      std::move(*points),
      optionalDouble(params, "minZ", 0.0),
      optionalDouble(params, "maxZ", 64.0),
      material,
      error);
    if (!brush)
    {
      return std::nullopt;
    }
    return std::vector<mdl::Brush>{std::move(*brush)};
  }

  if (type == "cylinder_sector")
  {
    auto center = mcpVec3FromJson(params, "center", error);
    if (!center)
    {
      return std::nullopt;
    }
    const auto snapMode =
      sectorSnapModeFromJson(params, "snapMode", SectorSnapMode::Grid, error);
    if (!snapMode)
    {
      return std::nullopt;
    }
    auto brush = createCylinderSectorBrush(
      builder,
      vm::vec2d{center->x(), center->y()},
      optionalDouble(params, "innerRadius", 32.0),
      optionalDouble(params, "outerRadius", 128.0),
      optionalDouble(params, "startAngle", 0.0),
      optionalDouble(params, "endAngle", 15.0),
      optionalDouble(params, "minZ", center->z()),
      optionalDouble(params, "maxZ", center->z() + 8.0),
      material,
      error,
      optionalDouble(params, "grid", 1.0),
      *snapMode);
    if (!brush)
    {
      return std::nullopt;
    }
    return std::vector<mdl::Brush>{std::move(*brush)};
  }

  const auto bounds = boundsFromJson(params, error);
  if (!bounds)
  {
    return std::nullopt;
  }

  auto brush = Result<mdl::Brush>{Error{"Unsupported brush type"}};
  if (type == "box")
  {
    brush = builder.createCuboid(*bounds, material);
  }
  else if (type == "wedge")
  {
    const auto axis = axisFromJson(params, "axis", vm::axis::x, error);
    if (!axis)
    {
      return std::nullopt;
    }
    brush = createWedgeBrush(builder, *bounds, *axis, material);
  }
  else if (type == "cylinder")
  {
    const auto axis = axisFromJson(params, "axis", vm::axis::z, error);
    if (!axis)
    {
      return std::nullopt;
    }
    const auto sides = clampedSizeParam(params, "sides", 16, 3, 128);
    const auto snapMode =
      sectorSnapModeFromJson(params, "snapMode", SectorSnapMode::Grid, error);
    if (!snapMode)
    {
      return std::nullopt;
    }
    auto gridSafeBrush = createCylinderBrushFromPolygon(
      builder,
      *bounds,
      sides,
      *axis,
      material,
      *snapMode,
      optionalDouble(params, "grid", 1.0),
      error);
    if (!gridSafeBrush)
    {
      return std::nullopt;
    }
    brush = std::move(*gridSafeBrush);
  }
  else if (type == "cone")
  {
    const auto axis = axisFromJson(params, "axis", vm::axis::z, error);
    if (!axis)
    {
      return std::nullopt;
    }
    const auto sides = clampedSizeParam(params, "sides", 16, 3, 128);
    brush = builder.createCone(*bounds, mdl::EdgeAlignedCircle{sides}, *axis, material);
  }
  else if (type == "pipe")
  {
    const auto axis = axisFromJson(params, "axis", vm::axis::z, error);
    if (!axis)
    {
      return std::nullopt;
    }
    const auto sides = clampedSizeParam(params, "sides", 16, 3, 128);
    const auto thickness = optionalDouble(params, "thickness", 16.0);
    if (!finitePositive(thickness))
    {
      error = "thickness must be greater than zero";
      return std::nullopt;
    }
    auto brushes = builder.createHollowCylinder(
      *bounds, thickness, mdl::EdgeAlignedCircle{sides}, *axis, material);
    if (brushes.is_error())
    {
      error = "Could not create pipe brushes from the given bounds";
      return std::nullopt;
    }
    return std::move(brushes).value();
  }
  else if (type == "sphere")
  {
    if (params.value("iterations").isDouble())
    {
      const auto iterations = clampedSizeParam(params, "iterations", 1, 1, 4);
      brush = builder.createIcoSphere(*bounds, iterations, material);
    }
    else
    {
      const auto axis = axisFromJson(params, "axis", vm::axis::z, error);
      if (!axis)
      {
        return std::nullopt;
      }
      const auto sides = clampedSizeParam(params, "sides", 12, 3, 64);
      const auto rings = clampedSizeParam(params, "rings", 6, 1, 64);
      brush = builder.createUvSphere(
        *bounds, mdl::EdgeAlignedCircle{sides}, rings, *axis, material);
    }
  }
  else if (type == "pyramid")
  {
    const auto axis = axisFromJson(params, "axis", vm::axis::z, error);
    if (!axis)
    {
      return std::nullopt;
    }
    brush = createPyramidBrush(builder, *bounds, *axis, material);
  }
  else if (type == "tetrahedron")
  {
    const auto axis = axisFromJson(params, "axis", vm::axis::z, error);
    if (!axis)
    {
      return std::nullopt;
    }
    brush = createTetrahedronBrush(builder, *bounds, *axis, material);
  }
  else if (type == "arch" || type == "torus")
  {
    error = QString{"Brush primitive type is not supported yet: %1"}.arg(type);
    return std::nullopt;
  }
  else
  {
    error = QString{"Unknown brush primitive type: %1"}.arg(type);
    return std::nullopt;
  }

  if (brush.is_error())
  {
    error = QString{"Could not create %1 brush from the given parameters"}.arg(type);
    return std::nullopt;
  }
  return std::vector<mdl::Brush>{std::move(brush).value()};
}

} // namespace

McpBridgeToolResult blockoutValidateResult(const QJsonObject& params)
{
  const auto type = params.value("type").toString().trimmed();
  if (type.isEmpty())
  {
    return invalidParamsFailure("blockout_validate requires type");
  }

  auto error = QString{};
  const auto result = validateBlockoutParams(type, params, error);
  if (!result)
  {
    return McpBridgeToolResult::success(QJsonObject{
      {"valid", false},
      {"type", type},
      {"errors", QJsonArray{error}},
    });
  }
  return McpBridgeToolResult::success(*result);
}

McpBridgeToolResult blockoutCreateResult(
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

  if (toolName == "blockout_create_spiral_stairs")
  {
    return blockoutCreateSpiralStairsForMapResult(
      mapWindow->document().map(), params, history, nextOperationIndex);
  }

  const auto removedToolPrefix = QString{"blockout_create_"};
  if (
    toolName.startsWith(removedToolPrefix)
    && RemovedPrefabBatchTypes.contains(toolName.mid(removedToolPrefix.size())))
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::ToolNotFound,
      QString{"%1 moved to trenchbroom-mcp-scene-workflow recipes"}.arg(toolName),
      preMutationFailureDetails({}, "use_skill_recipe_then_ir_apply"));
  }

  return McpBridgeToolResult::failure(
    mcp::McpErrorCode::ToolNotFound,
    QString{"Unknown MCP tool: %1"}.arg(toolName),
    preMutationFailureDetails({}, "search_tools_or_use_blockout_create_batch"));
}

McpBridgeToolResult blockoutCreateSpiralStairsForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  auto error = QString{};
  const auto spiralParams = spiralStairsParamsFromJson(params, error);
  if (!spiralParams)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      error,
      preMutationFailureDetails({}, "fix_spiral_stairs_parameters_then_retry"));
  }

  const auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};
  const auto materialResolution = materialResolutionJson(map, params);
  if (
    params.value("requireMaterialAvailable").toBool(false)
    && !materialResolution.value("currentlyLoaded").toBool(false)
    && !materialResolution.value("placeholderMaterial").toBool(false))
  {
    return strictMaterialFailure(QJsonObject{
      {"defaultMaterial", materialResolution},
      {"missingMaterialCount", 1},
      {"missingMaterialSample",
       QJsonArray{materialResolution.value("effectiveMaterial")}},
    });
  }
  auto material = materialNameFromParams(map, params);
  auto brushes = createSpiralStairBrushes(builder, *spiralParams, material, error);
  if (!brushes)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      error,
      preMutationFailureDetails({}, "fix_spiral_stairs_parameters_then_retry"));
  }

  auto nodes = std::vector<mdl::Node*>{};
  nodes.reserve(brushes->size());
  for (auto& brush : *brushes)
  {
    nodes.push_back(new mdl::BrushNode{std::move(brush)});
  }

  const auto brushCount = nodes.size();
  const auto bounds = boundsForNodes(nodes);
  const auto transactionName = QString{"MCP: Blockout spiral stairs"};
  const auto changedObjectIds = addNodesWithTransaction(
    map, transactionName, nodes, mcpOptionalBool(params, "select", true));
  if (!changedObjectIds)
  {
    for (auto* node : nodes)
    {
      delete node;
    }
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not add spiral stair brushes");
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history,
    nextOperationIndex,
    map,
    "blockout_create_spiral_stairs",
    transactionName,
    *changedObjectIds,
    result);
  result.insert("brushCount", static_cast<int>(brushCount));
  result.insert("material", QString::fromStdString(material));
  result.insert("materialResolution", materialResolution);
  result.insert(
    "materials",
    stringListToJsonArray(brushMaterialsForObjectIds(map, *changedObjectIds)));
  result.insert("validation", spiralValidationJson(*spiralParams, brushCount));
  result.insert("bounds", boundsToJson(bounds));
  applyDetailLevel(result, *changedObjectIds, idDetailFromParams(params));
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult geometryAnalyzeSlopesForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry* objectRegistry,
  const std::map<QString, McpBrushMetadataRecord>* metadataStore,
  const std::map<QString, McpModuleRecord>* moduleStore)
{
  auto error = QString{};
  auto warnings = QJsonArray{};
  const auto resolvedParams = paramsWithSelectorObjectIds(
    map, params, history, objectRegistry, metadataStore, moduleStore, warnings, error);
  if (!error.isEmpty())
  {
    return invalidParamsFailure(error);
  }
  const auto useSelectionTargets = !hasExplicitBrushTargetParams(params);
  auto brushes = std::optional<std::vector<mdl::BrushNode*>>{};
  if (useSelectionTargets)
  {
    brushes = selectedBrushNodes(map);
  }
  else
  {
    brushes = brushNodesFromObjectIdsAndOperations(
      map, resolvedParams, history, objectRegistry, error);
    if (!brushes)
    {
      return invalidParamsFailure(error);
    }
  }
  if (brushes->empty())
  {
    return invalidParamsFailure(
      "geometry_analyze_slopes requires operationIds, objectIds, selector targets, "
      "or selected brush nodes");
  }

  const auto routeDirection = optionalRouteDirectionFromParams(resolvedParams, error);
  if (!error.isEmpty())
  {
    return invalidParamsFailure(error);
  }

  const auto minSlopeDegrees =
    optionalClampedDouble(resolvedParams, "minSlopeDegrees", 0.5, 0.0, 89.0);
  const auto maxSlopeDegrees =
    optionalClampedDouble(resolvedParams, "maxSlopeDegrees", 89.0, minSlopeDegrees, 89.9);
  const auto detail = summaryOrFullDetail(resolvedParams);
  auto slopes = QJsonArray{};
  auto ascendingCount = 0;
  auto descendingCount = 0;
  auto crossSlopeCount = 0;
  auto unknownDirectionCount = 0;
  auto minReportedSlope = std::numeric_limits<double>::max();
  auto maxReportedSlope = 0.0;
  auto maxAbsHeightDeltaAlongRoute = 0.0;
  for (const auto* brush : *brushes)
  {
    const auto& faces = brush->brush().faces();
    for (size_t i = 0; i < faces.size(); ++i)
    {
      const auto& face = faces[i];
      const auto normal = face.normal();
      if (normal.z() <= GeometryEpsilon || normal.z() >= 1.0 - GeometryEpsilon)
      {
        continue;
      }

      const auto slopeDegrees = std::acos(std::clamp(normal.z(), -1.0, 1.0)) * 180.0 / Pi;
      if (slopeDegrees < minSlopeDegrees || slopeDegrees > maxSlopeDegrees)
      {
        continue;
      }
      auto slope = slopeFaceJson(*brush, face, i, routeDirection, map.worldNode());
      const auto classification = slope.value("classification").toString();
      if (classification == "ascending")
      {
        ++ascendingCount;
      }
      else if (classification == "descending")
      {
        ++descendingCount;
      }
      else if (classification == "cross_slope")
      {
        ++crossSlopeCount;
      }
      else
      {
        ++unknownDirectionCount;
      }
      minReportedSlope = std::min(minReportedSlope, slopeDegrees);
      maxReportedSlope = std::max(maxReportedSlope, slopeDegrees);
      maxAbsHeightDeltaAlongRoute = std::max(
        maxAbsHeightDeltaAlongRoute,
        std::abs(slope.value("heightDeltaAlongRoute").toDouble()));
      slopes.push_back(std::move(slope));
    }
  }

  if (!routeDirection)
  {
    warnings.push_back(
      "lowConfidence: provide routeDirection or start/end to classify each slope as "
      "ascending, descending, or cross_slope.");
  }
  if (slopes.isEmpty())
  {
    warnings.push_back("noSlopedFaces: no upward non-flat slope faces matched filters.");
    minReportedSlope = 0.0;
  }
  const auto passed = !slopes.isEmpty();

  auto result = QJsonObject{
    {"tool", "geometry_analyze_slopes"},
    {"detail", detail},
    {"passed", passed},
    {"recoveryAction",
     passed ? "continue_validation_or_review"
            : "rebuild_with_true_slope_or_adjust_targets"},
    {"source", useSelectionTargets ? "selection" : "targets"},
    {"targetSource", useSelectionTargets ? "selection" : "targets"},
    {"targetBrushCount", static_cast<int>(brushes->size())},
    {"slopeCount", slopes.size()},
    {"ascendingCount", ascendingCount},
    {"descendingCount", descendingCount},
    {"crossSlopeCount", crossSlopeCount},
    {"unknownDirectionCount", unknownDirectionCount},
    {"minReportedSlopeDegrees", minReportedSlope},
    {"maxReportedSlopeDegrees", maxReportedSlope},
    {"maxAbsHeightDeltaAlongRoute", maxAbsHeightDeltaAlongRoute},
    {"slopeSample", jsonSample(slopes)},
    {"warnings", warnings},
    {"minSlopeDegrees", minSlopeDegrees},
    {"maxSlopeDegrees", maxSlopeDegrees},
    {"routeDirectionProvided", routeDirection.has_value()},
  };
  if (detail == "full")
  {
    result.insert("slopes", slopes);
  }
  if (resolvedParams.contains("selectorMatchedCount"))
  {
    result.insert("selectorMatchedCount", resolvedParams.value("selectorMatchedCount"));
  }
  if (routeDirection)
  {
    result.insert("routeDirection", vecToJson(*routeDirection));
  }
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult geometryAnalyzeSlopesResult(
  AppController& appController,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry* objectRegistry,
  const std::map<QString, McpBrushMetadataRecord>* metadataStore,
  const std::map<QString, McpModuleRecord>* moduleStore)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return geometryAnalyzeSlopesForMapResult(
    mapWindow->document().map(),
    params,
    history,
    objectRegistry,
    metadataStore,
    moduleStore);
}

McpBridgeToolResult geometryAnalyzeRouteContinuityForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry* objectRegistry,
  const std::map<QString, McpBrushMetadataRecord>* metadataStore,
  const std::map<QString, McpModuleRecord>* moduleStore)
{
  auto error = QString{};
  auto warnings = QJsonArray{};
  const auto resolvedParams = paramsWithSelectorObjectIds(
    map, params, history, objectRegistry, metadataStore, moduleStore, warnings, error);
  if (!error.isEmpty())
  {
    return invalidParamsFailure(error);
  }
  const auto useSelectionTargets = !hasExplicitBrushTargetParams(params);
  auto brushes = std::optional<std::vector<mdl::BrushNode*>>{};
  if (useSelectionTargets)
  {
    brushes = selectedBrushNodes(map);
  }
  else
  {
    brushes = brushNodesFromObjectIdsAndOperations(
      map, resolvedParams, history, objectRegistry, error);
    if (!brushes)
    {
      return invalidParamsFailure(error);
    }
  }
  if (brushes->size() < 2u)
  {
    return invalidParamsFailure(
      "geometry_analyze_route_continuity requires at least two operation/object/"
      "selector target brushes or at least two selected brush nodes");
  }
  const auto selectorProvided = params.value("selector").isObject();
  const auto targetMix =
    targetMetadataMixJson(map, *brushes, metadataStore, objectRegistry);
  if (!selectorProvided && (!targetMix.value("mixedTargetWarning").toString().isEmpty()))
  {
    warnings.push_back(targetMix.value("mixedTargetWarning").toString());
  }

  const auto orderBy =
    resolvedParams.value("orderBy").toString("projection").trimmed().toLower();
  const auto useMetadataOrder =
    orderBy == "metadataorder" || orderBy == "metadata" || orderBy == "order";
  if (!useMetadataOrder && orderBy != "projection" && !orderBy.isEmpty())
  {
    return invalidParamsFailure("orderBy must be projection or metadataOrder");
  }
  auto orderedBrushes = *brushes;
  if (useMetadataOrder)
  {
    std::ranges::stable_sort(orderedBrushes, [&](auto* lhs, auto* rhs) {
      const auto lhsMetadata =
        lhs != nullptr ? metadataForBrushNode(map, *lhs, metadataStore, objectRegistry)
                       : std::nullopt;
      const auto rhsMetadata =
        rhs != nullptr ? metadataForBrushNode(map, *rhs, metadataStore, objectRegistry)
                       : std::nullopt;
      const auto lhsOrder = lhsMetadata ? lhsMetadata->value("order").toDouble(0.0) : 0.0;
      const auto rhsOrder = rhsMetadata ? rhsMetadata->value("order").toDouble(0.0) : 0.0;
      return lhsOrder < rhsOrder;
    });
    warnings.push_back(
      "orderBy=metadataOrder uses session metadata.order for route seam order; "
      "ensure all route surfaces have unique order values.");
  }

  auto routeDirection = optionalRouteDirectionFromParams(resolvedParams, error);
  if (!error.isEmpty())
  {
    return invalidParamsFailure(error);
  }

  if (!routeDirection)
  {
    auto warning = QString{};
    const auto inferred = routeDirectionFromBrushCenters(orderedBrushes, warning);
    if (vm::is_zero(inferred, GeometryEpsilon))
    {
      return invalidParamsFailure(warning);
    }
    routeDirection = inferred;
    warnings.push_back(warning);
  }

  const auto minUpNormal =
    optionalClampedDouble(resolvedParams, "minUpNormal", 0.2, 0.0, 1.0);
  const auto horizontalTolerance =
    optionalClampedDouble(resolvedParams, "horizontalTolerance", 1.0, 0.0, 1024.0);
  const auto verticalTolerance =
    optionalClampedDouble(resolvedParams, "verticalTolerance", 0.5, 0.0, 1024.0);
  const auto detail = summaryOrFullDetail(resolvedParams);
  auto qualityError = QString{};
  const auto qualityPolicy = qualityPolicyFromJson(resolvedParams, qualityError);
  if (!qualityPolicy)
  {
    return invalidParamsFailure(qualityError);
  }
  auto routeMode =
    resolvedParams.value("routeMode")
      .toString(
        resolvedParams.value("validationMode")
          .toString(resolvedParams.value("continuityMode").toString("continuous")))
      .trimmed()
      .toLower();
  if (routeMode == "walkable_continuous")
  {
    routeMode = "continuous";
  }
  else if (routeMode == "stairs_or_steps")
  {
    routeMode = "stepped";
  }
  else if (routeMode == "slide_or_surf")
  {
    routeMode = "continuous";
  }
  else if (routeMode == "spiral_ascending")
  {
    routeMode = "spiral";
  }
  else if (routeMode == "jump_gaps")
  {
    routeMode = "jump_chain";
  }
  const auto validRouteMode = routeMode == "continuous" || routeMode == "stepped"
                              || routeMode == "jump_chain" || routeMode == "spiral"
                              || routeMode == "closed_loop";
  if (!validRouteMode)
  {
    return invalidParamsFailure(
      "routeMode/continuityMode must be continuous, stepped, jump_chain, spiral, or "
      "closed_loop");
  }
  const auto continuityMode =
    routeMode == "closed_loop" ? QString{"continuous"} : routeMode;
  const auto maxStepHeight =
    optionalClampedDouble(resolvedParams, "maxStepHeight", 24.0, 0.0, 256.0);
  const auto maxJumpGap =
    optionalClampedDouble(resolvedParams, "maxJumpGap", 128.0, 0.0, 4096.0);
  const auto checkClosedLoop =
    routeMode == "closed_loop" || resolvedParams.value("closedLoop").toBool(false);

  auto surfaces = std::vector<PlayableSurface>{};
  auto unsupportedObjectIds = QJsonArray{};
  surfaces.reserve(orderedBrushes.size());
  for (auto* brush : orderedBrushes)
  {
    auto surface =
      playableSurfaceForBrush(*brush, *routeDirection, map.worldNode(), minUpNormal);
    if (!surface)
    {
      unsupportedObjectIds.push_back(nodePathId(*brush, map.worldNode()));
      continue;
    }
    if (
      const auto metadata =
        metadataForBrushNode(map, *brush, metadataStore, objectRegistry))
    {
      if (metadata->contains("order"))
      {
        surface->metadataOrder = metadata->value("order").toDouble();
      }
    }
    surfaces.push_back(*surface);
  }

  if (!useMetadataOrder)
  {
    std::ranges::sort(surfaces, [](const auto& lhs, const auto& rhs) {
      return lhs.minProjection < rhs.minProjection;
    });
  }

  auto surfaceJson = QJsonArray{};
  for (const auto& surface : surfaces)
  {
    auto surfaceObject = playableSurfaceJson(surface);
    if (surface.metadataOrder)
    {
      surfaceObject.insert("metadataOrder", *surface.metadataOrder);
    }
    surfaceJson.push_back(surfaceObject);
  }

  auto maxDirectionChangeDegrees = 0.0;
  auto directionChangeSample = QJsonArray{};
  const auto directionSampleCount = checkClosedLoop && surfaces.size() >= 3u
                                      ? surfaces.size()
                                    : surfaces.size() >= 3u ? surfaces.size() - 2u
                                                            : 0u;
  for (size_t sampleIndex = 0; sampleIndex < directionSampleCount; ++sampleIndex)
  {
    const auto centerIndex = checkClosedLoop ? sampleIndex : sampleIndex + 1u;
    const auto previousIndex =
      centerIndex == 0u ? surfaces.size() - 1u : centerIndex - 1u;
    const auto nextIndex = (centerIndex + 1u) % surfaces.size();
    const auto incomingDelta =
      surfaces[centerIndex].bounds.center() - surfaces[previousIndex].bounds.center();
    const auto outgoingDelta =
      surfaces[nextIndex].bounds.center() - surfaces[centerIndex].bounds.center();
    auto incoming = vm::vec3d{incomingDelta.x(), incomingDelta.y(), 0.0};
    auto outgoing = vm::vec3d{outgoingDelta.x(), outgoingDelta.y(), 0.0};
    if (vm::is_zero(incoming, GeometryEpsilon) || vm::is_zero(outgoing, GeometryEpsilon))
    {
      continue;
    }
    incoming = vm::normalize(incoming);
    outgoing = vm::normalize(outgoing);
    const auto directionChange =
      std::acos(std::clamp(vm::dot(incoming, outgoing), -1.0, 1.0)) * 180.0 / Pi;
    maxDirectionChangeDegrees = std::max(maxDirectionChangeDegrees, directionChange);
    if (directionChangeSample.size() < DefaultGeometrySampleLimit)
    {
      directionChangeSample.push_back(QJsonObject{
        {"surfaceIndex", static_cast<int>(centerIndex)},
        {"objectId", surfaces[centerIndex].objectId},
        {"directionChangeDegrees", directionChange},
      });
    }
  }

  const auto directionMetricAvailable = !directionChangeSample.isEmpty();
  const auto directionExceeded =
    directionMetricAvailable
    && maxDirectionChangeDegrees > qualityPolicy->maxDirectionChangeDegrees;
  const auto qualityStatus =
    !directionMetricAvailable ? QString{"not_evaluated"}
    : directionExceeded ? qualityPolicy->strict() ? QString{"failed"} : QString{"warning"}
                        : QString{"passed"};
  if (qualityStatus == "warning" || qualityStatus == "failed")
  {
    warnings.push_back(
      QString{"routeCurveQuality%1: centerline direction change %2 degrees exceeds "
              "the %3 policy limit of %4 degrees."}
        .arg(qualityStatus == "failed" ? "Failed" : "Warning")
        .arg(maxDirectionChangeDegrees)
        .arg(qualityPolicy->intent)
        .arg(qualityPolicy->maxDirectionChangeDegrees));
  }

  const auto seamForPair = [&](
                             const size_t fromIndex,
                             const size_t toIndex,
                             const size_t seamIndex,
                             const bool loopClosure) -> std::optional<QJsonObject> {
    if (!useMetadataOrder)
    {
      auto seam = seamJson(
        surfaces[fromIndex],
        surfaces[toIndex],
        seamIndex,
        horizontalTolerance,
        verticalTolerance);
      if (loopClosure)
      {
        seam.insert("loopClosure", true);
      }
      return seam;
    }

    auto* fromBrush = orderedBrushes[fromIndex];
    auto* toBrush = orderedBrushes[toIndex];
    auto localDirection =
      toBrush->logicalBounds().center() - fromBrush->logicalBounds().center();
    if (vm::is_zero(vm::vec2d{localDirection.x(), localDirection.y()}, GeometryEpsilon))
    {
      localDirection = *routeDirection;
    }
    else
    {
      localDirection =
        vm::normalize(vm::vec3d{localDirection.x(), localDirection.y(), 0.0});
    }
    const auto fromSurface =
      playableSurfaceForBrush(*fromBrush, localDirection, map.worldNode(), minUpNormal);
    const auto toSurface =
      playableSurfaceForBrush(*toBrush, localDirection, map.worldNode(), minUpNormal);
    if (!fromSurface || !toSurface)
    {
      return std::nullopt;
    }
    auto seam = seamJson(
      *fromSurface, *toSurface, seamIndex, horizontalTolerance, verticalTolerance);
    seam.insert("localRouteDirection", vecToJson(localDirection));
    if (loopClosure)
    {
      seam.insert("loopClosure", true);
    }
    return seam;
  };

  auto seams = QJsonArray{};
  auto maxAbsVerticalStep = 0.0;
  auto maxHorizontalGap = 0.0;
  auto maxEdgeGap = 0.0;
  auto maxPositiveGap = 0.0;
  auto maxOverlapDepth = 0.0;
  auto maxEdgeVerticalStep = 0.0;
  auto continuous = surfaces.size() >= 2u;
  auto centerlineContinuous = surfaces.size() >= 2u;
  auto fullWidthContinuous = surfaces.size() >= 2u;
  auto semanticContinuous = surfaces.size() >= 2u;
  auto failingSeamCount = 0;
  auto semanticFailingSeamCount = 0;
  const auto seamPairCount =
    surfaces.size() >= 2u ? surfaces.size() - 1u + (checkClosedLoop ? 1u : 0u) : 0u;
  for (size_t i = 0; i < seamPairCount; ++i)
  {
    const auto fromIndex = i;
    const auto toIndex = (i + 1u) % surfaces.size();
    auto maybeSeam = seamForPair(fromIndex, toIndex, i, checkClosedLoop && toIndex == 0u);
    if (!maybeSeam)
    {
      continue;
    }
    auto seam = *maybeSeam;
    const auto verticalStep = seam.value("verticalStep").toDouble();
    const auto horizontalGap = seam.value("horizontalGap").toDouble();
    const auto edgeGap = seam.value("edgeGapMax").toDouble();
    const auto positiveGap = seam.value("positiveGap").toDouble();
    const auto overlapDepth = seam.value("overlapDepth").toDouble();
    const auto edgeVerticalStep = seam.value("edgeVerticalStepMax").toDouble();
    maxAbsVerticalStep = std::max(maxAbsVerticalStep, std::abs(verticalStep));
    maxHorizontalGap = std::max(maxHorizontalGap, horizontalGap);
    maxEdgeGap = std::max(maxEdgeGap, edgeGap);
    maxPositiveGap = std::max(maxPositiveGap, positiveGap);
    maxOverlapDepth = std::max(maxOverlapDepth, overlapDepth);
    maxEdgeVerticalStep = std::max(maxEdgeVerticalStep, edgeVerticalStep);
    centerlineContinuous =
      centerlineContinuous && seam.value("centerlineContinuous").toBool(false);
    fullWidthContinuous =
      fullWidthContinuous && seam.value("fullWidthContinuous").toBool(false);
    continuous = continuous && seam.value("continuous").toBool(false);
    const auto seamSemanticContinuous =
      seamSemanticallyContinuous(seam, continuityMode, maxStepHeight, maxJumpGap);
    seam.insert("semanticContinuous", seamSemanticContinuous);
    if (!seam.value("continuous").toBool(false))
    {
      ++failingSeamCount;
    }
    if (!seamSemanticContinuous)
    {
      ++semanticFailingSeamCount;
    }
    semanticContinuous = semanticContinuous && seamSemanticContinuous;
    seams.push_back(seam);
  }

  if (!unsupportedObjectIds.isEmpty())
  {
    warnings.push_back(
      "unsupportedTargets: some brushes had no upward playable face matching "
      "minUpNormal.");
  }
  if (!continuous)
  {
    warnings.push_back(
      "routeNotContinuous: at least one adjacent playable surface has a vertical "
      "step, positive horizontal gap, or full-width edge gap outside tolerance.");
  }
  if (centerlineContinuous && !fullWidthContinuous)
  {
    warnings.push_back(
      "fullWidthRouteNotContinuous: centerline seam continuity passed, but inner/"
      "outer playable edges do not meet within tolerance.");
  }
  if (continuityMode == "stepped")
  {
    warnings.push_back(
      "continuityMode=stepped treats step_up/step_down seams within maxStepHeight as "
      "semantically continuous; inspect raw continuous/classification fields for "
      "strict geometry.");
  }
  else if (continuityMode == "jump_chain")
  {
    warnings.push_back(
      "routeMode=jump_chain treats horizontal_gap seams within maxJumpGap as "
      "intentional jumps; inspect raw continuous/classification fields for strict "
      "geometry.");
  }
  else if (continuityMode == "spiral")
  {
    warnings.push_back(
      "routeMode=spiral checks ordered adjacent segment continuity but does not check "
      "the final surface back to the first unless closedLoop is also true.");
  }
  const auto passed = semanticContinuous && unsupportedObjectIds.isEmpty();
  const auto acceptancePassed = passed && qualityStatus != "failed";

  auto result = QJsonObject{
    {"tool", "geometry_analyze_route_continuity"},
    {"detail", detail},
    {"passed", passed},
    {"acceptancePassed", acceptancePassed},
    {"recoveryAction",
     !passed                     ? "inspect_failing_seam_sample_then_fix_route_geometry"
     : qualityStatus == "failed" ? "increase_segments_then_rebuild_and_revalidate"
                                 : "continue_validation_or_review"},
    {"source", useSelectionTargets ? "selection" : "targets"},
    {"targetSource", useSelectionTargets ? "selection" : "targets"},
    {"targetBrushCount", static_cast<int>(brushes->size())},
    {"surfaceCount", static_cast<int>(surfaces.size())},
    {"seamCount", seams.size()},
    {"failingSeamCount", failingSeamCount},
    {"semanticFailingSeamCount", semanticFailingSeamCount},
    {"continuous", continuous},
    {"strictSurfaceContinuous", continuous},
    {"centerlineContinuous", centerlineContinuous},
    {"fullWidthContinuous", fullWidthContinuous},
    {"semanticContinuous", semanticContinuous},
    {"walkableContinuous", semanticContinuous},
    {"continuityMode", continuityMode},
    {"routeMode", routeMode},
    {"orderBy", useMetadataOrder ? "metadataOrder" : "projection"},
    {"closedLoop", checkClosedLoop},
    {"routeDirection", vecToJson(*routeDirection)},
    {"horizontalTolerance", horizontalTolerance},
    {"verticalTolerance", verticalTolerance},
    {"maxStepHeight", maxStepHeight},
    {"maxJumpGap", maxJumpGap},
    {"maxAbsVerticalStep", maxAbsVerticalStep},
    {"maxHorizontalGap", maxHorizontalGap},
    {"maxEdgeGap", maxEdgeGap},
    {"maxEndpointDistance", maxEdgeGap},
    {"maxPositiveGap", maxPositiveGap},
    {"maxOverlapDepth", maxOverlapDepth},
    {"maxEdgeVerticalStep", maxEdgeVerticalStep},
    {"surfaceSample", jsonSample(surfaceJson)},
    {"failingSeamSample", seamSummarySample(seams, true)},
    {"seamSample", seamSummarySample(seams, false)},
    {"unsupportedObjectCount", unsupportedObjectIds.size()},
    {"warnings", warnings},
    {"qualityPolicy", qualityPolicyJson(*qualityPolicy)},
    {"qualityStatus", qualityStatus},
    {"directionMetricMethod", "centerline_polyline"},
    {"maxDirectionChangeDegrees", maxDirectionChangeDegrees},
    {"directionChangeSample", directionChangeSample},
    {"passScope", QJsonArray{"walkable_surface_continuity"}},
    {"evaluatedMetrics",
     directionMetricAvailable ? QJsonArray{"direction_change"} : QJsonArray{}},
    {"unavailableMetrics", QJsonArray{"sagitta", "snap_displacement"}},
    {"notEvaluated", QJsonArray{"aesthetic_intent", "bsp_compile", "game_collision"}},
  };
  if (!selectorProvided && (!targetMix.value("mixedTargetWarning").toString().isEmpty()))
  {
    result.insert("mixedTargetWarning", targetMix.value("mixedTargetWarning"));
    result.insert("partCounts", targetMix.value("partCounts"));
    result.insert("roleCounts", targetMix.value("roleCounts"));
    result.insert("recommendedSelector", targetMix.value("recommendedSelector"));
  }
  if (detail == "full")
  {
    result.insert("surfaces", surfaceJson);
    result.insert("seams", seams);
    result.insert("unsupportedObjectIds", unsupportedObjectIds);
  }
  if (resolvedParams.contains("selectorMatchedCount"))
  {
    result.insert("selectorMatchedCount", resolvedParams.value("selectorMatchedCount"));
  }
  return McpBridgeToolResult::success(result);
}

McpBridgeToolResult geometryAnalyzeRouteContinuityResult(
  AppController& appController,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry* objectRegistry,
  const std::map<QString, McpBrushMetadataRecord>* metadataStore,
  const std::map<QString, McpModuleRecord>* moduleStore)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return geometryAnalyzeRouteContinuityForMapResult(
    mapWindow->document().map(),
    params,
    history,
    objectRegistry,
    metadataStore,
    moduleStore);
}

McpBridgeToolResult geometryAnalyzeShellSeamsForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry* objectRegistry,
  const std::map<QString, McpBrushMetadataRecord>* metadataStore,
  const std::map<QString, McpModuleRecord>* moduleStore)
{
  auto error = QString{};
  auto warnings = QJsonArray{};
  const auto resolvedParams = paramsWithSelectorObjectIds(
    map, params, history, objectRegistry, metadataStore, moduleStore, warnings, error);
  if (!error.isEmpty())
  {
    return invalidParamsFailure(error);
  }

  const auto useSelectionTargets = !hasExplicitBrushTargetParams(params);
  auto brushes = std::optional<std::vector<mdl::BrushNode*>>{};
  if (useSelectionTargets)
  {
    brushes = selectedBrushNodes(map);
  }
  else
  {
    brushes = brushNodesFromObjectIdsAndOperations(
      map, resolvedParams, history, objectRegistry, error);
    if (!brushes)
    {
      return invalidParamsFailure(error);
    }
  }
  if (brushes->empty())
  {
    return invalidParamsFailure(
      "geometry_analyze_shell_seams requires operationIds, objectIds, selector "
      "targets, or selected brush nodes");
  }

  const auto tolerance =
    optionalClampedDouble(resolvedParams, "tolerance", 0.01, 0.0, 1024.0);
  const auto detail = summaryOrFullDetail(resolvedParams);
  auto seamGroups = std::map<QString, std::vector<ShellSeamParticipant>>{};
  auto annotatedBrushCount = 0;
  auto invalidAnnotationError = QString{};
  for (auto* brush : *brushes)
  {
    if (brush == nullptr)
    {
      continue;
    }
    const auto metadata =
      metadataForBrushNode(map, *brush, metadataStore, objectRegistry);
    if (!metadata)
    {
      continue;
    }
    const auto shellSeamsValue = metadata->value("shellSeams");
    if (!shellSeamsValue.isArray())
    {
      continue;
    }
    const auto shellSeams = shellSeamsValue.toArray();
    if (shellSeams.isEmpty())
    {
      continue;
    }
    ++annotatedBrushCount;
    for (const auto& seamValue : shellSeams)
    {
      if (!seamValue.isObject())
      {
        invalidAnnotationError = "shellSeams entries must be objects";
        break;
      }
      const auto seam = seamValue.toObject();
      const auto key = seam.value("key").toString().trimmed();
      if (key.isEmpty())
      {
        invalidAnnotationError = "shellSeams entries must include key";
        break;
      }
      const auto vertices = shellSeamVerticesFromJson(seam, invalidAnnotationError);
      if (!vertices)
      {
        break;
      }
      seamGroups[key].push_back(ShellSeamParticipant{
        objectIdForBrushNode(map, *brush, objectRegistry),
        metadata->value("part").toString(),
        metadata->value("order").toDouble(0.0),
        *vertices,
      });
    }
    if (!invalidAnnotationError.isEmpty())
    {
      break;
    }
  }

  if (!invalidAnnotationError.isEmpty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      invalidAnnotationError,
      QJsonObject{
        {"mutatedDocument", false},
        {"retrySafe", true},
        {"recoveryAction", "use_annotated_recipe_or_visual_review"},
      });
  }

  if (seamGroups.empty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      "No shellSeams metadata found on target brushes; shell seam analysis only "
      "validates annotated recipe output.",
      QJsonObject{
        {"mutatedDocument", false},
        {"retrySafe", true},
        {"targetBrushCount", static_cast<int>(brushes->size())},
        {"annotatedBrushCount", annotatedBrushCount},
        {"recoveryAction", "use_annotated_recipe_or_visual_review"},
      });
  }

  if (annotatedBrushCount < static_cast<int>(brushes->size()))
  {
    warnings.push_back(
      "unannotatedTargetBrushes: some target brushes have no shellSeams metadata; "
      "acceptancePassed is false because the shell cannot be fully verified.");
  }

  auto seams = QJsonArray{};
  auto maxVertexDelta = 0.0;
  auto failingSeamCount = 0;
  auto shellContinuous = true;
  for (const auto& [key, participants] : seamGroups)
  {
    auto seam = shellSeamDiagnosticJson(key, participants, tolerance);
    const auto seamContinuous = seam.value("continuous").toBool(false);
    shellContinuous = shellContinuous && seamContinuous;
    if (!seamContinuous)
    {
      ++failingSeamCount;
    }
    maxVertexDelta = std::max(maxVertexDelta, seam.value("maxVertexDelta").toDouble(0.0));
    seams.push_back(std::move(seam));
  }
  if (!shellContinuous)
  {
    warnings.push_back(
      "shellSeamsNotContinuous: at least one annotated shell seam has a missing "
      "participant, mismatched vertex count, or vertex offset above tolerance.");
  }
  const auto allTargetsAnnotated =
    annotatedBrushCount == static_cast<int>(brushes->size());
  const auto acceptancePassed = shellContinuous && allTargetsAnnotated;

  auto result = QJsonObject{
    {"tool", "geometry_analyze_shell_seams"},
    {"detail", detail},
    {"source", useSelectionTargets ? "selection" : "targets"},
    {"targetSource", useSelectionTargets ? "selection" : "targets"},
    {"targetBrushCount", static_cast<int>(brushes->size())},
    {"annotatedBrushCount", annotatedBrushCount},
    {"seamCount", seams.size()},
    {"failingSeamCount", failingSeamCount},
    {"maxVertexDelta", maxVertexDelta},
    {"tolerance", tolerance},
    {"shellContinuous", shellContinuous},
    {"acceptancePassed", acceptancePassed},
    {"failingSeamSample", shellSeamSummarySample(seams, true)},
    {"seamSample", shellSeamSummarySample(seams, false)},
    {"warnings", warnings},
    {"mutatedDocument", false},
    {"recoveryAction",
     acceptancePassed ? "continue_validation_or_review"
                      : "inspect_failing_seam_sample_then_rebuild_annotated_shell"},
  };
  if (detail == "full")
  {
    result.insert("seams", seams);
  }
  if (resolvedParams.contains("selectorMatchedCount"))
  {
    result.insert("selectorMatchedCount", resolvedParams.value("selectorMatchedCount"));
  }
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult geometryAnalyzeShellSeamsResult(
  AppController& appController,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry* objectRegistry,
  const std::map<QString, McpBrushMetadataRecord>* metadataStore,
  const std::map<QString, McpModuleRecord>* moduleStore)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return geometryAnalyzeShellSeamsForMapResult(
    mapWindow->document().map(),
    params,
    history,
    objectRegistry,
    metadataStore,
    moduleStore);
}

McpBridgeToolResult blockoutCreateBatchResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  std::map<QString, McpBrushMetadataRecord>* metadataStore,
  std::map<QString, McpModuleRecord>* moduleStore,
  const McpObjectRegistry* objectRegistry)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return blockoutCreateBatchForMapResult(
    mapWindow->document().map(),
    toolName,
    params,
    history,
    nextOperationIndex,
    metadataStore,
    moduleStore,
    objectRegistry);
}

McpBridgeToolResult createBoxesBatchResult(
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

  return createBoxesBatchForMapResult(
    mapWindow->document().map(), toolName, params, history, nextOperationIndex);
}

McpBridgeToolResult createBoxesBatchForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  const auto boxesValue = params.value("boxes");
  if (!boxesValue.isArray())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      "brush_create_boxes_batch requires boxes array",
      preMutationFailureDetails(
        QJsonObject{{"targetSource", "boxes"}}, "provide_box_specs_then_retry"));
  }

  const auto boxes = boxesValue.toArray();
  if (boxes.isEmpty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      "boxes must not be empty",
      preMutationFailureDetails(
        QJsonObject{{"targetSource", "boxes"}}, "provide_box_specs_then_retry"));
  }

  auto operations = QJsonArray{};
  for (auto i = 0; i < boxes.size(); ++i)
  {
    if (!boxes[i].isObject())
    {
      return McpBridgeToolResult::failure(
        mcp::McpErrorCode::InvalidParams,
        QString{"boxes[%1] must be an object"}.arg(i),
        preMutationFailureDetails(
          QJsonObject{{"targetSource", "boxes"}}, "fix_box_spec_objects_then_retry"));
    }
    auto operation = boxes[i].toObject();
    operation.insert("type", "box");
    operations.push_back(operation);
  }

  auto batchParams = QJsonObject{
    {"name", params.value("name").toString("MCP: Create box brush batch")},
    {"operations", operations},
    {"select", params.value("select").toBool(true)},
    {"detail", params.value("detail").toString("summary")},
  };
  if (params.contains("grid"))
  {
    batchParams.insert("grid", params.value("grid"));
  }
  if (params.contains("material"))
  {
    batchParams.insert("material", params.value("material"));
  }

  return blockoutCreateBatchForMapResult(
    map, toolName, batchParams, history, nextOperationIndex);
}

McpBridgeToolResult blockoutCreateBatchForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  std::map<QString, McpBrushMetadataRecord>* metadataStore,
  std::map<QString, McpModuleRecord>* moduleStore)
{
  return blockoutCreateBatchForMapResult(
    map,
    toolName,
    params,
    history,
    nextOperationIndex,
    metadataStore,
    moduleStore,
    nullptr);
}

McpBridgeToolResult blockoutCreateBatchForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  std::map<QString, McpBrushMetadataRecord>* metadataStore,
  std::map<QString, McpModuleRecord>* moduleStore,
  const McpObjectRegistry* objectRegistry)
{
  auto batchParams = params;
  if (toolName == "blockout_create_curved_corridor")
  {
    auto operation = params;
    operation.insert("type", "curved_corridor");
    batchParams = QJsonObject{
      {"name", "MCP: Blockout curved corridor"},
      {"operations", QJsonArray{operation}},
      {"select", params.value("select").toBool(true)},
      {"detail", params.value("detail").toString("summary")},
      {"grid", params.value("grid").toDouble(1.0)},
    };
    for (const auto& key :
         {"defaultMetadata",
          "metadata",
          "partMetadata",
          "qualityPolicy",
          "requireMaterialAvailable"})
    {
      if (params.contains(key))
      {
        batchParams.insert(key, params.value(key));
      }
    }
  }

  const auto operationsValue = batchParams.value("operations");
  if (!operationsValue.isArray())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      "blockout_create_batch requires operations array",
      preMutationFailureDetails(
        QJsonObject{{"targetSource", "operations"}},
        "provide_batch_operations_then_retry"));
  }
  const auto operations = operationsValue.toArray();
  if (operations.isEmpty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      "operations must not be empty",
      preMutationFailureDetails(
        QJsonObject{{"targetSource", "operations"}},
        "provide_batch_operations_then_retry"));
  }
  for (auto i = 0; i < operations.size(); ++i)
  {
    if (!operations[i].isObject())
    {
      return McpBridgeToolResult::failure(
        mcp::McpErrorCode::InvalidParams,
        QString{"operations[%1] must be an object"}.arg(i),
        preMutationFailureDetails(
          QJsonObject{{"targetSource", "operations"}},
          "fix_batch_operation_objects_then_retry"));
    }
  }

  const auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};
  const auto defaultMaterial = materialNameFromParams(map, batchParams);
  const auto grid = optionalDouble(batchParams, "grid", 1.0);
  const auto defaultMetadata = batchParams.value("defaultMetadata").isObject()
                                 ? batchParams.value("defaultMetadata").toObject()
                                 : QJsonObject{};
  if (!finitePositive(grid))
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      "grid must be greater than zero",
      preMutationFailureDetails(
        QJsonObject{{"targetSource", "grid"}}, "provide_positive_grid_then_retry"));
  }
  auto qualityError = QString{};
  const auto qualityPolicy = qualityPolicyFromJson(batchParams, qualityError);
  if (!qualityPolicy)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      qualityError,
      preMutationFailureDetails(
        QJsonObject{{"targetSource", "qualityPolicy"}}, "fix_quality_policy_then_retry"));
  }
  const auto expandedOperations = expandedBatchOperations(operations, defaultMetadata);
  const auto materialPreflight = batchMaterialPreflight(map, batchParams, operations);
  if (
    batchParams.value("requireMaterialAvailable").toBool(false)
    && materialPreflight.value("missingMaterialCount").toInt() > 0)
  {
    return strictMaterialFailure(materialPreflight);
  }

  auto nodes = std::vector<mdl::Node*>{};
  auto errors = QJsonArray{};
  auto warnings = blockoutWarningsForOperations(operations, grid);
  if (materialPreflight.value("missingMaterialCount").toInt() > 0)
  {
    warnings.push_back(QJsonObject{
      {"type", "missingMaterials"},
      {"count", materialPreflight.value("missingMaterialCount")},
      {"sample", materialPreflight.value("missingMaterialSample")},
      {"placeholderNamesAllowed", true},
    });
    if (params.value("requireMaterialAvailable").toBool(false))
    {
      errors.push_back("one or more requested materials are not loaded");
    }
  }
  const auto curveQuality =
    curveQualityForOperations(operations, grid, *qualityPolicy, warnings);
  const auto expansion =
    expansionSummaryJson(operations, expandedOperations, warnings, grid);
  auto intentSummaries = QJsonArray{};
  auto failedOperationIndex = -1;
  auto failedOperationType = QString{};
  auto failedOperationPreview = QJsonObject{};
  for (auto i = 0; i < static_cast<int>(expandedOperations.size()); ++i)
  {
    auto error = QString{};
    const auto operation = expandedOperations[static_cast<size_t>(i)];
    auto operationNodes = compileBatchOperation(
      map, builder, operation, defaultMaterial, grid, error, defaultMetadata);
    if (!error.isEmpty() || operationNodes.empty())
    {
      errors.push_back(
        error.isEmpty() ? QString{"operations[%1] generated no brushes"}.arg(i)
                        : QString{"operations[%1]: %2"}.arg(i).arg(error));
      failedOperationIndex = i;
      failedOperationType = operation.value("type").toString().trimmed().toLower();
      failedOperationPreview = batchOperationPreviewJson(operation);
      deleteNodes(operationNodes);
      break;
    }

    const auto type = operation.value("type").toString().trimmed().toLower();
    if (type == "ramp" || type == "wedge" || type == "ramp_between")
    {
      auto intentError = QString{};
      auto intent = rampLikeIntentSummary(operation, grid, intentError);
      if (!intent || !intentError.isEmpty())
      {
        errors.push_back(QString{"operations[%1]: %2"}.arg(i).arg(
          intentError.isEmpty() ? "Could not summarize ramp intent" : intentError));
        failedOperationIndex = i;
        failedOperationType = type;
        failedOperationPreview = batchOperationPreviewJson(operation);
        deleteNodes(operationNodes);
        break;
      }
      intent->insert("operationIndex", i);
      intentSummaries.push_back(*intent);
    }

    nodes.insert(nodes.end(), operationNodes.begin(), operationNodes.end());
  }

  if (!errors.isEmpty())
  {
    auto validation = batchValidationJson(
      false,
      errors,
      static_cast<int>(expandedOperations.size()),
      static_cast<int>(nodes.size()));
    validation.insert(
      "compiledOperationCount", failedOperationIndex < 0 ? 0 : failedOperationIndex);
    validation.insert("compiledBrushCount", static_cast<int>(nodes.size()));
    if (failedOperationIndex >= 0)
    {
      validation.insert("failedOperationIndex", failedOperationIndex);
    }
    if (!failedOperationType.isEmpty())
    {
      validation.insert("failedOperationType", failedOperationType);
    }
    if (!failedOperationPreview.isEmpty())
    {
      validation.insert("failedOperationPreview", failedOperationPreview);
    }

    deleteNodes(nodes);
    return McpBridgeToolResult::success(QJsonObject{
      {"valid", false},
      {"validation", validation},
      {"expansion", expansion},
      {"qualityPolicy", qualityPolicyJson(*qualityPolicy)},
      {"curveQuality", curveQuality},
      {"qualityStatus", curveQuality.value("qualityStatus")},
      {"acceptancePassed", false},
      {"materialPreflight", materialPreflight},
    });
  }

  if (!curveQuality.value("acceptancePassed").toBool(true))
  {
    const auto compiledBrushCount = static_cast<int>(nodes.size());
    deleteNodes(nodes);
    auto validation = batchValidationJson(
      false,
      QJsonArray{"curve quality does not satisfy smooth qualityPolicy"},
      static_cast<int>(expandedOperations.size()),
      compiledBrushCount);
    validation.insert("failureKind", "curve_quality");
    return McpBridgeToolResult::success(QJsonObject{
      {"valid", false},
      {"mutatedDocument", false},
      {"retrySafe", true},
      {"recoveryAction", "increase_segments_or_adjust_snap_then_preview_again"},
      {"validation", validation},
      {"expansion", expansion},
      {"warnings", warnings},
      {"qualityPolicy", qualityPolicyJson(*qualityPolicy)},
      {"curveQuality", curveQuality},
      {"qualityStatus", curveQuality.value("qualityStatus")},
      {"acceptancePassed", false},
      {"materialPreflight", materialPreflight},
    });
  }

  const auto transactionName =
    batchParams.value("name").toString("MCP: Blockout batch").trimmed();
  const auto brushCount = static_cast<int>(nodes.size());
  const auto bounds = boundsForNodes(nodes);
  const auto fullResults = pendingNodeSummariesJson(nodes);
  const auto validation = batchValidationJson(
    true,
    {},
    static_cast<int>(expandedOperations.size()),
    static_cast<int>(nodes.size()));
  const auto changedObjectIds = addNodesWithTransaction(
    map,
    transactionName.isEmpty() ? QString{"MCP: Blockout batch"} : transactionName,
    nodes,
    batchParams.value("select").toBool(true));
  if (!changedObjectIds)
  {
    deleteNodes(nodes);
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not add blockout batch brushes");
  }

  auto result = QJsonObject{};
  auto detailObject = QJsonObject{
    {"input", batchParams},
    {"grid", grid},
    {"validation", validation},
    {"results", fullResults},
    {"expansion", expansion},
    {"expandedOperations", jsonArrayFromOperations(expandedOperations)},
    {"expandedOperationsTruncated", expandedOperations.size() > 512u},
    {"qualityPolicy", qualityPolicyJson(*qualityPolicy)},
    {"curveQuality", curveQuality},
  };
  if (!intentSummaries.isEmpty())
  {
    detailObject.insert("intentSummaries", intentSummaries);
  }
  mcpRecordOperation(
    history,
    nextOperationIndex,
    map,
    toolName,
    transactionName.isEmpty() ? QString{"MCP: Blockout batch"} : transactionName,
    *changedObjectIds,
    result,
    detailObject);
  result.insert("brushCount", brushCount);
  result.insert("bounds", boundsToJson(bounds));
  result.insert("validation", validation);
  result.insert("expansion", expansion);
  result.insert("material", QString::fromStdString(defaultMaterial));
  result.insert(
    "materials",
    stringListToJsonArray(brushMaterialsForObjectIds(map, *changedObjectIds)));
  result.insert("grid", grid);
  result.insert("warnings", warnings);
  result.insert("materialPreflight", materialPreflight);
  result.insert("qualityPolicy", qualityPolicyJson(*qualityPolicy));
  result.insert("curveQuality", curveQuality);
  result.insert("qualityStatus", curveQuality.value("qualityStatus"));
  result.insert("acceptancePassed", true);
  if (!intentSummaries.isEmpty())
  {
    result.insert("intentSummaries", intentSummaries);
  }
  applyDetailLevel(
    result, *changedObjectIds, idDetailFromParams(batchParams), fullResults);
  if (metadataStore != nullptr)
  {
    const auto changedIds =
      !history.empty()
          && history.back().operationId == result.value("operationId").toString()
        ? history.back().changedObjectIds
        : QStringList{};
    auto metadataObjectIds = changedIds;
    if (objectRegistry != nullptr)
    {
      for (auto& objectId : metadataObjectIds)
      {
        objectId = objectRegistry->externalIdForLegacy(map, objectId);
      }
    }
    const auto metadataCount = storeBatchOperationMetadata(
      [&]() {
        auto expanded = QJsonArray{};
        for (const auto& operation : expandedOperations)
        {
          expanded.push_back(operation);
        }
        return expanded;
      }(),
      metadataObjectIds,
      documentFingerprintForMap(map),
      defaultMetadata,
      *metadataStore,
      moduleStore,
      result.value("operationId").toString());
    if (metadataCount > 0)
    {
      result.insert("metadataCount", metadataCount);
    }
  }
  if (
    !history.empty()
    && history.back().operationId == result.value("operationId").toString())
  {
    history.back().setSummary(result);
  }
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult blockoutCompilePreviewForMapResult(
  mdl::Map& map, const QJsonObject& params)
{
  const auto operationsValue = params.value("operations");
  if (!operationsValue.isArray())
  {
    return invalidParamsFailure("blockout preview requires operations array");
  }
  const auto operations = operationsValue.toArray();
  if (operations.isEmpty())
  {
    return invalidParamsFailure("operations must not be empty");
  }
  for (auto i = 0; i < operations.size(); ++i)
  {
    if (!operations[i].isObject())
    {
      return invalidParamsFailure(QString{"operations[%1] must be an object"}.arg(i));
    }
  }

  const auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};
  const auto defaultMaterial = materialNameFromParams(map, params);
  const auto grid = optionalDouble(params, "grid", 1.0);
  const auto defaultMetadata = params.value("defaultMetadata").isObject()
                                 ? params.value("defaultMetadata").toObject()
                                 : QJsonObject{};
  if (!finitePositive(grid))
  {
    return invalidParamsFailure("grid must be greater than zero");
  }
  auto qualityError = QString{};
  const auto qualityPolicy = qualityPolicyFromJson(params, qualityError);
  if (!qualityPolicy)
  {
    return invalidParamsFailure(qualityError);
  }
  const auto expandedOperations = expandedBatchOperations(operations, defaultMetadata);
  const auto materialPreflight = batchMaterialPreflight(map, params, operations);

  auto nodes = std::vector<mdl::Node*>{};
  auto errors = QJsonArray{};
  auto warnings = blockoutWarningsForOperations(operations, grid);
  if (materialPreflight.value("missingMaterialCount").toInt() > 0)
  {
    warnings.push_back(QJsonObject{
      {"type", "missingMaterials"},
      {"count", materialPreflight.value("missingMaterialCount")},
      {"sample", materialPreflight.value("missingMaterialSample")},
      {"placeholderNamesAllowed", true},
    });
    if (params.value("requireMaterialAvailable").toBool(false))
    {
      errors.push_back("one or more requested materials are not loaded");
    }
  }
  const auto curveQuality =
    curveQualityForOperations(operations, grid, *qualityPolicy, warnings);
  auto failedOperationIndex = -1;
  auto failedOperationType = QString{};
  auto failedOperationPreview = QJsonObject{};
  for (auto i = 0; i < static_cast<int>(expandedOperations.size()); ++i)
  {
    auto error = QString{};
    const auto operation = expandedOperations[static_cast<size_t>(i)];
    auto operationNodes = compileBatchOperation(
      map, builder, operation, defaultMaterial, grid, error, defaultMetadata);
    if (!error.isEmpty() || operationNodes.empty())
    {
      errors.push_back(
        error.isEmpty() ? QString{"operations[%1] generated no brushes"}.arg(i)
                        : QString{"operations[%1]: %2"}.arg(i).arg(error));
      failedOperationIndex = i;
      failedOperationType = operation.value("type").toString().trimmed().toLower();
      failedOperationPreview = batchOperationPreviewJson(operation);
      deleteNodes(operationNodes);
      break;
    }
    nodes.insert(nodes.end(), operationNodes.begin(), operationNodes.end());
  }

  const auto valid =
    errors.isEmpty() && curveQuality.value("acceptancePassed").toBool(true);
  auto result = QJsonObject{
    {"valid", valid},
    {"willCommit", false},
    {"operationCount", operations.size()},
    {"compiledPrimitiveOperationCount", static_cast<int>(expandedOperations.size())},
    {"compiledOperationCount",
     valid ? static_cast<int>(expandedOperations.size())
           : std::max(0, failedOperationIndex)},
    {"estimatedBrushCount", static_cast<int>(nodes.size())},
    {"grid", grid},
    {"material", QString::fromStdString(defaultMaterial)},
    {"errors", errors},
    {"warnings", warnings},
    {"qualityPolicy", qualityPolicyJson(*qualityPolicy)},
    {"curveQuality", curveQuality},
    {"qualityStatus", curveQuality.value("qualityStatus")},
    {"acceptancePassed", valid},
    {"materialPreflight", materialPreflight},
  };
  if (!nodes.empty())
  {
    result.insert("bounds", boundsToJson(boundsForNodes(nodes)));
  }
  if (failedOperationIndex >= 0)
  {
    result.insert("failedOperationIndex", failedOperationIndex);
  }
  if (!failedOperationType.isEmpty())
  {
    result.insert("failedOperationType", failedOperationType);
  }
  if (!failedOperationPreview.isEmpty())
  {
    result.insert("failedOperationPreview", failedOperationPreview);
  }

  deleteNodes(nodes);
  return McpBridgeToolResult::success(result);
}

McpBridgeToolResult blockoutCreateBatchForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  return blockoutCreateBatchForMapResult(
    map, toolName, params, history, nextOperationIndex, nullptr, nullptr, nullptr);
}

McpBridgeToolResult brushTypesListResult()
{
  return McpBridgeToolResult::success(QJsonObject{
    {"types",
     QJsonArray{
       brushTypeJson("box", true, "Single convex cuboid brush."),
       brushTypeJson("wedge", true, "Single convex wedge brush."),
       brushTypeJson("cylinder", true, "Single convex cylinder brush."),
       brushTypeJson("cone", true, "Single convex cone brush."),
       brushTypeJson(
         "pipe", true, "Hollow cylinder made from convex brush segments.", true),
       brushTypeJson("sphere", true, "Single convex UV or ico sphere brush."),
       brushTypeJson("pyramid", true, "Single convex square pyramid brush."),
       brushTypeJson("tetrahedron", true, "Single convex tetrahedron brush."),
       brushTypeJson("prism", true, "Single convex vertical prism from 2D points."),
       brushTypeJson("cylinder_sector", true, "Single convex annular sector brush."),
       brushTypeJson("from_planes", true, "Expert brush from plane point triples."),
       brushTypeJson(
         "arch", false, "Arch primitives need a dedicated stable generator.", true),
       brushTypeJson(
         "torus",
         false,
         "Torus geometry cannot be represented as one convex BSP brush.",
         true),
     }},
  });
}

McpBridgeToolResult createBrushResult(
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

  return createBrushForMapResult(
    mapWindow->document().map(), toolName, params, history, nextOperationIndex);
}

McpBridgeToolResult createBrushForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  auto error = QString{};
  const auto materialResolution = materialResolutionJson(map, params);
  if (
    params.value("requireMaterialAvailable").toBool(false)
    && !materialResolution.value("currentlyLoaded").toBool(false)
    && !materialResolution.value("placeholderMaterial").toBool(false))
  {
    return strictMaterialFailure(QJsonObject{
      {"defaultMaterial", materialResolution},
      {"missingMaterialCount", 1},
      {"missingMaterialSample",
       QJsonArray{materialResolution.value("effectiveMaterial")}},
    });
  }
  const auto material = materialNameFromParams(map, params);
  const auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};

  const auto type = brushTypeFromToolName(toolName, params);
  auto brushes = createBrushesForType(map, builder, type, params, material, error);
  if (!brushes)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      error,
      preMutationFailureDetails(
        QJsonObject{{"type", type}}, "fix_brush_parameters_then_retry"));
  }

  auto nodes = std::vector<mdl::Node*>{};
  nodes.reserve(brushes->size());
  for (auto& brush : *brushes)
  {
    nodes.push_back(new mdl::BrushNode{std::move(brush)});
  }

  const auto bounds = boundsForNodes(nodes);
  auto brushJson = pendingNodeSummariesJson(nodes);
  const auto transactionName = QString{"MCP: Create %1 brush"}.arg(type);
  const auto changedObjectIds = addNodesWithTransaction(
    map, transactionName, nodes, mcpOptionalBool(params, "select", true));
  if (!changedObjectIds)
  {
    for (auto* node : nodes)
    {
      delete node;
    }
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not add brush to the active document");
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history,
    nextOperationIndex,
    map,
    toolName,
    transactionName,
    *changedObjectIds,
    result);
  result.insert("type", type);
  result.insert("brushCount", brushJson.size());
  result.insert("bounds", boundsToJson(bounds));
  result.insert("materialResolution", materialResolution);
  applyDetailLevel(result, *changedObjectIds, idDetailFromParams(params), brushJson);
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult geometryAnalyzeSelectionResult(
  mdl::Map& map, const QJsonObject& params)
{
  const auto grid = optionalDouble(params, "grid", 1.0);
  auto error = std::string{};
  const auto analysis = automation::analyzeSelectionGeometry(map, grid, error);
  if (!analysis)
  {
    return invalidParamsFailure(QString::fromStdString(error));
  }
  const auto detail = params.value("detail").toString("summary").trimmed().toLower();
  const auto detailLevel = detail == "full"  ? QString{"full"}
                           : detail == "ids" ? QString{"ids"}
                                             : QString{"summary"};
  const auto maxBrushes =
    std::clamp(optionalSize(params, "maxBrushes", 100), size_t{0}, size_t{1000});
  const auto includeVertices =
    detailLevel == "full" && mcpOptionalBool(params, "includeVertices", false);
  const auto includeBrushEntries = detailLevel == "ids" || detailLevel == "full";

  auto brushResults = QJsonArray{};
  auto objectIds = QJsonArray{};
  auto invalidObjectIds = QJsonArray{};
  auto nonGridAlignedObjectIds = QJsonArray{};
  auto materials = QStringList{};
  auto truncated = false;
  for (const auto& fact : analysis->brushes)
  {
    const auto* brushNode = fact.brush;
    const auto& brush = brushNode->brush();
    const auto objectId = nodePathId(*brushNode, map.worldNode());
    if (!fact.gridAligned)
    {
      if (nonGridAlignedObjectIds.size() < 10)
      {
        nonGridAlignedObjectIds.push_back(objectId);
      }
    }
    if (!fact.convex)
    {
      if (invalidObjectIds.size() < 10)
      {
        invalidObjectIds.push_back(objectId);
      }
    }

    for (const auto& material : fact.materials)
    {
      const auto name = QString::fromStdString(material);
      if (!materials.contains(name))
      {
        materials.push_back(name);
      }
    }

    if (!includeBrushEntries)
    {
      continue;
    }

    const auto returnedEntryCount = detailLevel == "ids"
                                      ? static_cast<size_t>(objectIds.size())
                                      : static_cast<size_t>(brushResults.size());
    if (returnedEntryCount >= maxBrushes)
    {
      truncated = true;
      continue;
    }

    objectIds.push_back(objectId);
    if (detailLevel == "full")
    {
      auto brushJson = nodeSummaryJson(*brushNode, map.worldNode());
      brushJson.insert("vertexCount", static_cast<int>(fact.vertexCount));
      brushJson.insert("edgeCount", static_cast<int>(fact.edgeCount));
      brushJson.insert("closed", fact.closed);
      brushJson.insert("convex", fact.convex);
      brushJson.insert("gridAligned", fact.gridAligned);
      auto brushMaterials = QStringList{};
      for (const auto& material : fact.materials)
      {
        brushMaterials.push_back(QString::fromStdString(material));
      }
      brushJson.insert("materials", stringListToJsonArray(brushMaterials));
      if (includeVertices)
      {
        brushJson.insert("vertices", vertexPositionsToJson(brush.vertexPositions()));
      }
      brushResults.push_back(std::move(brushJson));
    }
  }

  auto result = QJsonObject{
    {"brushCount", static_cast<int>(analysis->brushes.size())},
    {"invalidBrushCount", static_cast<int>(analysis->invalidBrushCount)},
    {"nonGridAlignedCount", static_cast<int>(analysis->nonGridAlignedCount)},
    {"grid", grid},
    {"detail", detailLevel},
    {"truncated", truncated},
    {"returnedBrushCount", includeBrushEntries ? brushResults.size() : 0},
    {"materials", stringListToJsonArray(materials)},
  };
  if (analysis->bounds)
  {
    result.insert("bounds", boundsToJson(*analysis->bounds));
  }
  if (!invalidObjectIds.isEmpty())
  {
    result.insert("invalidObjectIds", invalidObjectIds);
  }
  if (!nonGridAlignedObjectIds.isEmpty())
  {
    result.insert("nonGridAlignedObjectIds", nonGridAlignedObjectIds);
  }
  if (detailLevel == "ids")
  {
    result.insert("objectIds", objectIds);
    result.insert("returnedBrushCount", objectIds.size());
  }
  else if (detailLevel == "full")
  {
    result.insert("brushes", brushResults);
  }

  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult blockoutValidateSpiralStairsResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history)
{
  auto error = QString{};
  auto expected = spiralStairsParamsFromJson(params, error);
  if (!expected)
  {
    return invalidParamsFailure(error);
  }

  const auto grid = optionalDouble(params, "grid", 1.0);
  if (!finitePositive(grid))
  {
    return invalidParamsFailure("grid must be greater than zero");
  }

  auto source = QString{"selection"};
  auto brushes = std::vector<mdl::BrushNode*>{};
  const auto operationIdValue = params.value("operationId");
  if (operationIdValue.isString() && !operationIdValue.toString().isEmpty())
  {
    source = "operationId";
    const auto operationId = operationIdValue.toString();
    brushes = brushNodesFromOperationId(map, operationId, history, error);
    if (!error.isEmpty())
    {
      return invalidParamsFailure(error);
    }
  }
  else
  {
    brushes = selectedBrushNodes(map);
  }

  auto invalidBrushCount = 0;
  auto nonGridAlignedCount = 0;
  for (const auto* brushNode : brushes)
  {
    const auto& brush = brushNode->brush();
    if (!brush.closed() || !brush.fullySpecified())
    {
      ++invalidBrushCount;
    }
    if (!brushGridAligned(brush, grid))
    {
      ++nonGridAlignedCount;
    }
  }

  const auto expectedBrushCount =
    expected->steps + (expected->column ? 1u : 0u) + (expected->landing ? 1u : 0u);
  const auto brushCountMatches = brushes.size() == expectedBrushCount;
  const auto geometryChecks = analyzeSpiralGeometry(brushes, *expected);
  auto validation = spiralValidationJson(*expected, brushes.size(), invalidBrushCount);
  validation.insert("source", source);
  validation.insert("expectedBrushCount", static_cast<int>(expectedBrushCount));
  validation.insert("brushCountMatches", brushCountMatches);
  validation.insert("nonGridAlignedCount", nonGridAlignedCount);
  validation.insert("gridAligned", nonGridAlignedCount == 0);
  validation.insert("gapCount", geometryChecks.gapCount);
  validation.insert("radiusMismatch", geometryChecks.radiusMismatch);
  validation.insert("columnFits", geometryChecks.columnFits);
  validation.insert("landingConnected", geometryChecks.landingConnected);
  validation.insert("zProgression", geometryChecks.zProgression);
  validation.insert(
    "valid",
    invalidBrushCount == 0 && nonGridAlignedCount == 0 && brushCountMatches
      && geometryChecks.gapCount == 0 && !geometryChecks.radiusMismatch
      && geometryChecks.columnFits && geometryChecks.landingConnected
      && geometryChecks.zProgression);

  auto errors = geometryChecks.errors;
  if (brushes.empty())
  {
    errors.push_back(
      source == "operationId" ? "The referenced operation contains no live brush nodes"
                              : "No selected brush nodes to validate");
  }
  if (!brushCountMatches)
  {
    errors.push_back(QString{"Expected %1 brushes, got %2"}
                       .arg(static_cast<int>(expectedBrushCount))
                       .arg(static_cast<int>(brushes.size())));
  }
  if (invalidBrushCount > 0)
  {
    errors.push_back(QString{"%1 selected brushes are invalid"}.arg(invalidBrushCount));
  }
  if (nonGridAlignedCount > 0)
  {
    errors.push_back(
      QString{"%1 selected brushes are not grid aligned"}.arg(nonGridAlignedCount));
  }
  validation.insert("errors", errors);

  return McpBridgeToolResult::success(std::move(validation));
}

McpBridgeToolResult geometryAnalyzeSelectionResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return geometryAnalyzeSelectionResult(mapWindow->document().map(), params);
}

McpBridgeToolResult blockoutValidateSpiralStairsResult(
  AppController& appController,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return blockoutValidateSpiralStairsResult(mapWindow->document().map(), params, history);
}

} // namespace tb::ui
