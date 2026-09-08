/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.

 TrenchBroom is free software:
 * you can redistribute it and/or modify
 it under the terms of the GNU General Public
 * License as published by
 the Free Software Foundation, either version 3 of the License,
 * or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that
 * it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of

 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public
 * License for more details.

 You should have received a copy of the GNU General Public
 * License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include "McpBridgeServerTools.h"
#include "McpResponseUtils.h"
#include "McpSelectionQuery.h"
#include "McpToolSupport.h"
#include "base/PreferenceManager.h"
#include "gl/Material.h"
#include "gl/MaterialManager.h"
#include "mcp/McpError.h"
#include "mdl/Brush.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushFaceHandle.h"
#include "mdl/BrushNode.h"
#include "mdl/EditorContext.h"
#include "mdl/Map.h"
#include "mdl/Map_Brushes.h"
#include "mdl/Map_Selection.h"
#include "mdl/Node.h"
#include "mdl/NodeQueries.h"
#include "mdl/Selection.h"
#include "mdl/Transaction.h"
#include "mdl/UpdateBrushFaceAttributes.h"
#include "mdl/WorldNode.h"
#include "prefs/Preferences.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/QPathUtils.h"
#include "ui/automation/AutomationMaterials.h"
#include "ui/mcp/McpObjectRegistry.h"

#include "kd/string_compare.h"

#include "vm/bbox.h"
#include "vm/vec.h"

#include <algorithm>
#include <filesystem>
#include <optional>
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

QString pathToQString(const std::filesystem::path& path)
{
  return path.empty() ? QString{} : pathAsQString(path);
}

QString genericPathToQString(const std::filesystem::path& path)
{
  return pathAsGenericQString(path);
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

const McpOperationRecord* findOperation(
  const std::vector<McpOperationRecord>& history, const QString& operationId)
{
  const auto it = std::ranges::find_if(
    history, [&](const auto& operation) { return operation.operationId == operationId; });
  return it == history.end() ? nullptr : &*it;
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

std::optional<std::vector<mdl::Node*>> nodesFromObjectIdsAndOperations(
  mdl::Map& map,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry* objectRegistry,
  QString& error)
{
  auto result = std::vector<mdl::Node*>{};

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
    result.push_back(node);
  }

  const auto operationIds = operationIdsFromParams(params, error);
  if (!operationIds)
  {
    return std::nullopt;
  }
  for (const auto& operationId : *operationIds)
  {
    const auto* operation = findOperation(history, operationId);
    if (operation == nullptr)
    {
      error = QString{"Unknown MCP operation id: %1"}.arg(operationId);
      return std::nullopt;
    }
    if (operation->undone)
    {
      error = QString{"MCP operation is already undone: %1"}.arg(operationId);
      return std::nullopt;
    }
    for (const auto& objectId : operation->changedObjectIds)
    {
      auto* node = resolveExternalNodeId(map, objectId, objectRegistry, error);
      if (node == nullptr)
      {
        return std::nullopt;
      }
      result.push_back(node);
    }
  }

  std::ranges::sort(result);
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
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

std::vector<mdl::BrushFaceHandle> brushFaceHandlesFromNodes(
  const std::vector<mdl::Node*>& nodes)
{
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

  auto result = std::vector<mdl::BrushFaceHandle>{};
  for (auto* brushNode : brushes)
  {
    const auto handles = mdl::toHandles(brushNode);
    result.insert(std::end(result), std::begin(handles), std::end(handles));
  }
  return result;
}

std::optional<vm::vec3d> vec3FromJson(const QJsonValue& value, QString& error)
{
  if (!value.isArray())
  {
    return std::nullopt;
  }
  const auto array = value.toArray();
  if (array.size() != 3)
  {
    error = "normal must be a [x,y,z] array";
    return std::nullopt;
  }
  for (const auto& entry : array)
  {
    if (!entry.isDouble())
    {
      error = "normal must contain only numbers";
      return std::nullopt;
    }
  }
  const auto normal =
    vm::vec3d{array[0].toDouble(), array[1].toDouble(), array[2].toDouble()};
  if (vm::is_zero(vm::squared_length(normal), vm::Cd::almost_zero()))
  {
    error = "normal must not be zero";
    return std::nullopt;
  }
  return vm::normalize(normal);
}

bool matchesFaceSemantic(
  const mdl::BrushFaceHandle& handle,
  const QString& faceSemantic,
  const std::optional<vm::vec3d>& requestedNormal,
  const double normalTolerance)
{
  const auto normal = handle.face().normal();
  if (requestedNormal)
  {
    return vm::dot(normal, *requestedNormal) >= normalTolerance;
  }

  if (faceSemantic.isEmpty() || faceSemantic == "all")
  {
    return true;
  }
  if (faceSemantic == "top")
  {
    return normal.z() >= normalTolerance;
  }
  if (faceSemantic == "bottom")
  {
    return normal.z() <= -normalTolerance;
  }
  if (faceSemantic == "side" || faceSemantic == "sides")
  {
    return std::abs(normal.z()) <= 1.0 - normalTolerance;
  }
  return false;
}

std::vector<mdl::BrushFaceHandle> filterFaceHandlesBySemantic(
  std::vector<mdl::BrushFaceHandle> handles, const QJsonObject& params, QString& error)
{
  const auto faceSemantic =
    params.value("faceSemantic").toString("all").trimmed().toLower();
  const auto hasNormal = params.contains("normal");
  auto requestedNormal = std::optional<vm::vec3d>{};
  if (hasNormal)
  {
    requestedNormal = vec3FromJson(params.value("normal"), error);
    if (!requestedNormal)
    {
      return {};
    }
  }

  const auto toleranceValue = params.value("normalTolerance");
  auto normalTolerance = 0.75;
  if (!toleranceValue.isUndefined())
  {
    if (!toleranceValue.isDouble())
    {
      error = "normalTolerance must be a number";
      return {};
    }
    normalTolerance = std::clamp(toleranceValue.toDouble(), 0.0, 1.0);
  }

  if (
    !requestedNormal && !faceSemantic.isEmpty() && faceSemantic != "all"
    && faceSemantic != "top" && faceSemantic != "bottom" && faceSemantic != "side"
    && faceSemantic != "sides")
  {
    error = "faceSemantic must be all, top, bottom, or side";
    return {};
  }

  if (!requestedNormal && (faceSemantic.isEmpty() || faceSemantic == "all"))
  {
    return handles;
  }

  handles.erase(
    std::remove_if(
      handles.begin(),
      handles.end(),
      [&](const auto& handle) {
        return !matchesFaceSemantic(
          handle, faceSemantic, requestedNormal, normalTolerance);
      }),
    handles.end());
  if (handles.empty())
  {
    error = requestedNormal
              ? "normal matched no brush faces"
              : QString{"faceSemantic '%1' matched no brush faces"}.arg(faceSemantic);
  }
  return handles;
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
  operation.documentPath = pathToQString(map.path());
  operation.documentFingerprint = documentFingerprintForMap(map);
  operation.setChangedObjectIds(changedObjectIds);
  result = mutationResultJson(operation, idsMode);
  appendMcpOperationRecord(history, std::move(operation));
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

} // namespace

QJsonObject materialJson(const gl::Material& material)
{
  return QJsonObject{
    {"name", QString::fromStdString(material.name())},
    {"collection", QString::fromStdString(material.collectionName())},
    {"relativePath", genericPathToQString(material.relativePath())},
    {"absolutePath", pathToQString(material.absolutePath())},
    {"usageCount", static_cast<int>(material.usageCount())},
  };
}

QString currentMaterialName(mdl::Map& map)
{
  return QString::fromStdString(map.currentMaterialName());
}

QString fallbackMaterialName(mdl::Map& map)
{
  const auto current = currentMaterialName(map);
  return current.isEmpty() ? QString::fromStdString(mdl::BrushFace::NoMaterialName)
                           : current;
}

bool materialExists(mdl::Map& map, const QString& name)
{
  const auto materialName = name.trimmed().toStdString();
  if (materialName.empty())
  {
    return false;
  }
  if (kdl::ci::str_is_equal(materialName, mdl::BrushFace::NoMaterialName))
  {
    return true;
  }

  const auto& materials = map.materialManager().materials();
  return std::any_of(
    std::begin(materials), std::end(materials), [&](const auto* material) {
      return material && kdl::ci::str_is_equal(material->name(), materialName);
    });
}

void insertMissingMaterialFallback(
  QJsonObject& result, mdl::Map& map, const QString& material)
{
  if (!materialExists(map, material))
  {
    result.insert("fallbackMaterial", fallbackMaterialName(map));
  }
}

QJsonObject brushFaceAttributesJson(const mdl::BrushFace& face)
{
  const auto uvAttributes = face.uvAttributes();
  return QJsonObject{
    {"material", QString::fromStdString(face.materialName())},
    {"xOffset", uvAttributes.offset.x()},
    {"yOffset", uvAttributes.offset.y()},
    {"xScale", uvAttributes.scale.x()},
    {"yScale", uvAttributes.scale.y()},
    {"rotation", uvAttributes.rotation},
  };
}

vm::bbox3d brushFaceBounds(const mdl::BrushFace& face)
{
  auto vertices = face.vertexPositions();
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

QJsonObject brushFaceJson(
  const mdl::BrushFaceHandle& handle, const mdl::WorldNode& worldNode)
{
  const auto& face = handle.face();
  return QJsonObject{
    {"objectId", nodePathId(*handle.node(), worldNode)},
    {"faceIndex", static_cast<int>(handle.faceIndex())},
    {"material", QString::fromStdString(face.materialName())},
    {"normal", vecToJson(face.normal())},
    {"bounds", boundsToJson(brushFaceBounds(face))},
    {"attributes", brushFaceAttributesJson(face)},
  };
}

McpBridgeToolResult textureSearchResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return textureSearchForMapResult(mapWindow->document().map(), params);
}

McpBridgeToolResult textureSearchForMapResult(mdl::Map& map, const QJsonObject& params)
{
  const auto query = params.value("query").toString().trimmed();
  const auto limit = std::max(1, params.value("limit").toInt(50));
  auto results = QJsonArray{};
  auto materialNames = QJsonArray{};
  auto sampleMaterials = QJsonArray{};

  const auto& materials = map.materialManager().materials();
  for (const auto* material : materials)
  {
    if (!material)
    {
      continue;
    }
    if (sampleMaterials.size() < std::min(limit, 12))
    {
      sampleMaterials.push_back(QString::fromStdString(material->name()));
    }
    const auto name = QString::fromStdString(material->name());
    const auto relativePath = genericPathToQString(material->relativePath());
    if (
      !query.isEmpty() && !name.contains(query, Qt::CaseInsensitive)
      && !relativePath.contains(query, Qt::CaseInsensitive))
    {
      continue;
    }

    const auto json = materialJson(*material);
    results.push_back(json);
    materialNames.push_back(json.value("name").toString());
    if (results.size() >= limit)
    {
      break;
    }
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"query", query},
    {"results", results},
    {"materials", results},
    {"materialNames", materialNames},
    {"count", results.size()},
    {"currentMaterial", currentMaterialName(map)},
    {"fallbackMaterial", fallbackMaterialName(map)},
    {"suggestedFallbackMaterial", fallbackMaterialName(map)},
    {"sampleMaterialNames", sampleMaterials},
  });
}

QJsonObject textureLockJson(mdl::Map& map)
{
  const auto& editorContext = map.editorContext();
  return QJsonObject{
    {"textureLock", editorContext.alignmentLock()},
    {"uvLock", editorContext.uvLock()},
  };
}

McpBridgeToolResult textureLockGetResult(AppController& appController)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return McpBridgeToolResult::success(textureLockJson(mapWindow->document().map()));
}

McpBridgeToolResult textureLockSetForMapResult(mdl::Map& map, const QJsonObject& params)
{
  const auto textureLockValue = params.value("textureLock");
  const auto uvLockValue = params.value("uvLock");
  if (textureLockValue.isUndefined() && uvLockValue.isUndefined())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      "texture_lock_set requires textureLock or uvLock",
      preMutationFailureDetails(QJsonObject{}, "provide_texture_or_uv_lock_then_retry"));
  }
  if (!textureLockValue.isUndefined() && !textureLockValue.isBool())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      "textureLock must be a boolean",
      preMutationFailureDetails(
        QJsonObject{{"targetSource", "textureLock"}}, "fix_texture_lock_then_retry"));
  }
  if (!uvLockValue.isUndefined() && !uvLockValue.isBool())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      "uvLock must be a boolean",
      preMutationFailureDetails(
        QJsonObject{{"targetSource", "uvLock"}}, "fix_uv_lock_then_retry"));
  }

  auto& editorContext = map.editorContext();
  if (textureLockValue.isBool())
  {
    const auto textureLock = textureLockValue.toBool();
    setPref(Preferences::AlignmentLock, textureLock);
    editorContext.setAlignmentLock(textureLock);
  }
  if (uvLockValue.isBool())
  {
    const auto uvLock = uvLockValue.toBool();
    setPref(Preferences::UvLock, uvLock);
    editorContext.setUvLock(uvLock);
  }

  auto result = textureLockJson(map);
  result.insert("changed", true);
  result.insert("mutatedDocument", false);
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult textureLockSetResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return textureLockSetForMapResult(mapWindow->document().map(), params);
}

std::optional<mdl::BrushFaceHandle> brushFaceHandleFromJson(
  mdl::Map& map,
  const QJsonObject& params,
  const QString& objectIdKey,
  const QString& faceIndexKey,
  QString& error)
{
  const auto objectId = params.value(objectIdKey).toString().trimmed();
  if (objectId.isEmpty())
  {
    error = QString{"%1 is required"}.arg(objectIdKey);
    return std::nullopt;
  }

  auto* node = resolveNodeId(map.worldNode(), objectId);
  auto* brushNode = dynamic_cast<mdl::BrushNode*>(node);
  if (!brushNode)
  {
    error = QString{"%1 is not a brush: %2"}.arg(objectIdKey, objectId);
    return std::nullopt;
  }

  const auto faceIndexValue = params.value(faceIndexKey);
  if (!faceIndexValue.isDouble())
  {
    error = QString{"%1 must be an integer"}.arg(faceIndexKey);
    return std::nullopt;
  }

  const auto faceIndex = static_cast<size_t>(faceIndexValue.toInt());
  if (faceIndex >= brushNode->brush().faceCount())
  {
    error = QString{"%1 is out of range"}.arg(faceIndexKey);
    return std::nullopt;
  }

  return mdl::BrushFaceHandle{brushNode, faceIndex};
}

std::vector<mdl::BrushFaceHandle> brushFaceHandlesFromParamsOrSelection(
  mdl::Map& map, const QJsonObject& params, QString& error)
{
  auto result = std::vector<mdl::BrushFaceHandle>{};
  const auto objectId = params.value("objectId").toString().trimmed();
  if (!objectId.isEmpty())
  {
    auto* node = resolveNodeId(map.worldNode(), objectId);
    auto* brushNode = dynamic_cast<mdl::BrushNode*>(node);
    if (!brushNode)
    {
      error = QString{"objectId is not a brush: %1"}.arg(objectId);
      return {};
    }

    const auto faceIndexValue = params.value("faceIndex");
    if (faceIndexValue.isUndefined())
    {
      return mdl::toHandles(brushNode);
    }

    if (!faceIndexValue.isDouble())
    {
      error = "faceIndex must be an integer";
      return {};
    }
    const auto faceIndex = static_cast<size_t>(faceIndexValue.toInt());
    if (faceIndex >= brushNode->brush().faceCount())
    {
      error = "faceIndex is out of range";
      return {};
    }
    result.emplace_back(brushNode, faceIndex);
    return result;
  }

  if (!map.selection().brushFaces.empty())
  {
    return map.selection().brushFaces;
  }

  for (auto* brushNode : map.selection().brushes)
  {
    const auto handles = mdl::toHandles(brushNode);
    result.insert(std::end(result), std::begin(handles), std::end(handles));
  }

  if (result.empty())
  {
    error = "tool requires objectId or selected brush faces/brushes";
  }
  return result;
}

std::vector<mdl::BrushFaceHandle> brushFaceHandlesFromTargetsOrSelection(
  mdl::Map& map,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry* objectRegistry,
  QString& error)
{
  if (
    params.value("objectIds").isArray() || params.contains("operationId")
    || params.value("operationIds").isArray())
  {
    auto nodes =
      nodesFromObjectIdsAndOperations(map, params, history, objectRegistry, error);
    if (!nodes)
    {
      return {};
    }
    auto handles = brushFaceHandlesFromNodes(*nodes);
    if (handles.empty())
    {
      error = "target objects contain no brush faces";
      return {};
    }
    return filterFaceHandlesBySemantic(std::move(handles), params, error);
  }

  auto handles = brushFaceHandlesFromParamsOrSelection(map, params, error);
  if (handles.empty())
  {
    return handles;
  }
  return filterFaceHandlesBySemantic(std::move(handles), params, error);
}

std::vector<mdl::BrushFaceHandle> brushFaceHandlesFromFacesArray(
  mdl::Map& map, const QJsonArray& faces, QString& error)
{
  auto result = std::vector<mdl::BrushFaceHandle>{};
  for (const auto& faceValue : faces)
  {
    if (!faceValue.isObject())
    {
      error = "faces must contain objects";
      return {};
    }
    const auto faceObject = faceValue.toObject();
    auto handle =
      brushFaceHandleFromJson(map, faceObject, "objectId", "faceIndex", error);
    if (!handle)
    {
      return {};
    }
    result.push_back(*handle);
  }
  return result;
}

std::vector<mdl::BrushFaceHandle> faceSelectionHandlesFromParams(
  mdl::Map& map,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry* objectRegistry,
  QString& error)
{
  if (params.value("faces").isArray())
  {
    auto handles =
      brushFaceHandlesFromFacesArray(map, params.value("faces").toArray(), error);
    if (handles.empty() && error.isEmpty())
    {
      error = "faces must not be empty";
    }
    return handles.empty()
             ? handles
             : filterFaceHandlesBySemantic(std::move(handles), params, error);
  }
  return brushFaceHandlesFromTargetsOrSelection(
    map, params, history, objectRegistry, error);
}

QJsonArray changedBrushIds(
  const std::vector<mdl::BrushFaceHandle>& handles, const mdl::WorldNode& worldNode)
{
  auto result = QJsonArray{};
  auto nodes = std::vector<const mdl::Node*>{};
  for (const auto* node : mdl::toNodes(handles))
  {
    if (std::find(std::begin(nodes), std::end(nodes), node) != std::end(nodes))
    {
      continue;
    }
    nodes.push_back(node);
    result.push_back(nodePathId(*node, worldNode));
  }
  return result;
}

McpBridgeToolResult textureApplyResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry)
{
  const auto material = params.value("material").toString().trimmed();
  if (material.isEmpty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      "texture_apply requires material",
      preMutationFailureDetails(
        QJsonObject{{"targetSource", "material"}}, "add_material_then_retry"));
  }

  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  auto& map = mapWindow->document().map();
  auto error = QString{};
  auto handles =
    brushFaceHandlesFromTargetsOrSelection(map, params, history, &objectRegistry, error);
  if (handles.empty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      error.isEmpty() ? "texture_apply matched no brush faces" : error,
      preMutationFailureDetails(
        QJsonObject{{"targetSource", "faces_or_selection"}},
        "select_faces_or_fix_texture_targets"));
  }

  auto changedNodes = changedBrushIds(handles, map.worldNode());

  const auto transactionName = QString{"MCP: Apply texture"};
  auto ok = executeTransaction(map, transactionName, [&]() {
    mdl::deselectAll(map);
    mdl::selectBrushFaces(map, handles);
    return mdl::setBrushFaceAttributes(
      map, mdl::UpdateBrushFaceAttributes{.materialName = material.toStdString()});
  });
  if (!ok)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not apply texture");
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history,
    nextOperationIndex,
    map,
    toolName,
    transactionName,
    changedNodes,
    result,
    mcpIdsModeFromParams(params));
  result.insert("material", material);
  result.insert("materialExists", materialExists(map, material));
  insertMissingMaterialFallback(result, map, material);
  result.insert("faceCount", static_cast<int>(handles.size()));
  result.insert("faceSemantic", params.value("faceSemantic").toString("all"));
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult textureApplyByFilterResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return textureApplyByFilterForMapResult(
    mapWindow->document().map(),
    toolName,
    params,
    history,
    nextOperationIndex,
    &objectRegistry);
}

McpBridgeToolResult textureApplyByFilterForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry* objectRegistry)
{
  const auto material = params.value("material").toString().trimmed();
  if (material.isEmpty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      "texture_apply_by_filter requires material",
      preMutationFailureDetails(
        QJsonObject{{"targetSource", "material"}}, "add_material_then_retry"));
  }

  auto error = QString{};
  auto matches = std::vector<mdl::Node*>{};
  auto handles = std::vector<mdl::BrushFaceHandle>{};
  if (
    params.value("objectIds").isArray() || params.contains("operationId")
    || params.value("operationIds").isArray())
  {
    auto nodes =
      nodesFromObjectIdsAndOperations(map, params, history, objectRegistry, error);
    if (!nodes)
    {
      return McpBridgeToolResult::failure(
        mcp::McpErrorCode::InvalidParams,
        error,
        preMutationFailureDetails(
          QJsonObject{{"targetSource", "objectIdsOrOperations"}},
          "refresh_status_or_fix_texture_targets"));
    }
    matches = *nodes;
    handles = brushFaceHandlesFromNodes(matches);
  }
  else
  {
    auto filterParams = params;
    filterParams.remove("material");
    filterParams.remove("faceSemantic");
    filterParams.remove("normal");
    filterParams.remove("normalTolerance");
    if (filterParams.value("type").toString().trimmed().isEmpty())
    {
      filterParams.insert("type", "brush");
    }

    auto options = McpSelectionQueryOptions{};
    options.excludeWorld = true;
    options.selectableOnly = true;
    options.leafOnly = true;
    options.exactTypeOnly = true;
    matches = mcpFilteredNodes(map, filterParams, options, error);
    if (!error.isEmpty())
    {
      return McpBridgeToolResult::failure(
        mcp::McpErrorCode::InvalidParams,
        error,
        preMutationFailureDetails(
          QJsonObject{{"targetSource", "filter"}}, "fix_filter_then_retry"));
    }

    for (auto* node : matches)
    {
      auto* brushNode = dynamic_cast<mdl::BrushNode*>(node);
      if (!brushNode)
      {
        continue;
      }
      const auto brushHandles = mdl::toHandles(brushNode);
      handles.insert(std::end(handles), std::begin(brushHandles), std::end(brushHandles));
    }
  }

  handles = filterFaceHandlesBySemantic(std::move(handles), params, error);
  if (handles.empty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      error.isEmpty() ? "texture_apply_by_filter matched no brush faces" : error,
      preMutationFailureDetails(
        QJsonObject{
          {"targetSource", "filter"},
          {"matchedBrushCount", static_cast<int>(matches.size())},
        },
        "preview_filter_or_choose_face_semantic"));
  }

  auto changedNodes = changedBrushIds(handles, map.worldNode());
  const auto transactionName = QString{"MCP: Apply texture by filter"};
  auto ok = executeTransaction(map, transactionName, [&]() {
    mdl::deselectAll(map);
    mdl::selectBrushFaces(map, handles);
    return mdl::setBrushFaceAttributes(
      map, mdl::UpdateBrushFaceAttributes{.materialName = material.toStdString()});
  });
  if (!ok)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError,
      "Could not apply texture by filter",
      preMutationFailureDetails(
        QJsonObject{{"targetSource", "filter"}}, "refresh_status_or_retry"));
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history,
    nextOperationIndex,
    map,
    toolName,
    transactionName,
    changedNodes,
    result,
    mcpIdsModeFromParams(params));
  result.insert("material", material);
  result.insert("materialExists", materialExists(map, material));
  insertMissingMaterialFallback(result, map, material);
  result.insert("brushCount", static_cast<int>(matches.size()));
  result.insert("faceCount", static_cast<int>(handles.size()));
  result.insert("faceSemantic", params.value("faceSemantic").toString("all"));
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult faceListResult(
  AppController& appController,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry& objectRegistry)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return faceListForMapResult(
    mapWindow->document().map(), params, history, objectRegistry);
}

McpBridgeToolResult faceListForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry& objectRegistry)
{
  auto error = QString{};
  auto handles =
    brushFaceHandlesFromTargetsOrSelection(map, params, history, &objectRegistry, error);
  if (handles.empty() && !error.isEmpty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      error,
      QJsonObject{
        {"mutatedDocument", false},
        {"retrySafe", true},
        {"recoveryAction", "provide_face_targets_or_select_brush_faces"},
      });
  }

  const auto limit = optionalSize(params, "limit", 500);
  auto faces = QJsonArray{};
  for (const auto& handle : handles)
  {
    if (faces.size() >= static_cast<int>(limit))
    {
      break;
    }
    faces.push_back(brushFaceJson(handle, map.worldNode()));
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"faces", faces},
    {"count", faces.size()},
  });
}

McpBridgeToolResult faceSelectResult(
  AppController& appController,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry& objectRegistry)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return faceSelectForMapResult(
    mapWindow->document().map(), params, history, objectRegistry);
}

McpBridgeToolResult faceSelectForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry& objectRegistry)
{
  auto error = QString{};
  auto handles = std::vector<mdl::BrushFaceHandle>{};
  if (params.value("faces").isArray())
  {
    handles = brushFaceHandlesFromFacesArray(map, params.value("faces").toArray(), error);
    if (!handles.empty())
    {
      handles = filterFaceHandlesBySemantic(std::move(handles), params, error);
    }
  }
  else
  {
    handles = brushFaceHandlesFromTargetsOrSelection(
      map, params, history, &objectRegistry, error);
  }

  if (handles.empty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      error.isEmpty() ? "face_select requires faces or objectId/faceIndex" : error,
      preMutationFailureDetails(
        QJsonObject{{"targetSource", "faces_or_selection"}},
        "provide_faces_or_select_brush_faces"));
  }

  mdl::deselectAll(map);
  mdl::selectBrushFaces(map, handles);

  auto faces = QJsonArray{};
  for (const auto& handle : handles)
  {
    faces.push_back(brushFaceJson(handle, map.worldNode()));
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"faces", faces},
    {"selectedCount", faces.size()},
    {"mutatedDocument", false},
  });
}

std::optional<mdl::UpdateBrushFaceAttributes> updateBrushFaceAttributesFromParams(
  const QJsonObject& params, QString& error)
{
  auto update = mdl::UpdateBrushFaceAttributes{};
  auto hasUpdate = false;

  const auto material = params.value("material").toString().trimmed();
  if (!material.isEmpty())
  {
    update.materialName = material.toStdString();
    hasUpdate = true;
  }

  const auto setFloat = [&](const QString& key, auto& target) {
    const auto value = params.value(key);
    if (!value.isUndefined())
    {
      if (!value.isDouble())
      {
        error = QString{"%1 must be a number"}.arg(key);
        return false;
      }
      target = mdl::SetValue{static_cast<float>(value.toDouble())};
      hasUpdate = true;
    }
    return true;
  };

  if (
    !setFloat("xOffset", update.xOffset) || !setFloat("yOffset", update.yOffset)
    || !setFloat("xScale", update.xScale) || !setFloat("yScale", update.yScale)
    || !setFloat("rotation", update.rotation))
  {
    return std::nullopt;
  }

  if (!hasUpdate)
  {
    error = "No face texture attributes were provided";
    return std::nullopt;
  }
  return update;
}

McpBridgeToolResult faceTextureSetResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return faceTextureSetForMapResult(
    mapWindow->document().map(),
    toolName,
    params,
    history,
    nextOperationIndex,
    objectRegistry);
}

McpBridgeToolResult faceTextureSetForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry)
{
  auto error = QString{};
  const auto update = updateBrushFaceAttributesFromParams(params, error);
  if (!update)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      error,
      preMutationFailureDetails(
        QJsonObject{{"targetSource", "face_texture_attributes"}},
        "provide_face_texture_attributes_then_retry"));
  }

  auto handles =
    faceSelectionHandlesFromParams(map, params, history, &objectRegistry, error);
  if (handles.empty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      error.isEmpty() ? "face_texture_set matched no brush faces" : error,
      preMutationFailureDetails(
        QJsonObject{{"targetSource", "faces_or_selection"}},
        "provide_faces_or_select_brush_faces"));
  }

  const auto changedNodes = changedBrushIds(handles, map.worldNode());
  const auto transactionName = QString{"MCP: Set face texture"};
  const auto ok = executeTransaction(map, transactionName, [&]() {
    mdl::deselectAll(map);
    mdl::selectBrushFaces(map, handles);
    return mdl::setBrushFaceAttributes(map, *update);
  });
  if (!ok)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not set face texture attributes");
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history,
    nextOperationIndex,
    map,
    toolName,
    transactionName,
    changedNodes,
    result,
    mcpIdsModeFromParams(params));
  result.insert("faceCount", static_cast<int>(handles.size()));
  result.insert("faceSemantic", params.value("faceSemantic").toString("all"));
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult textureReplaceResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  const auto find = params.value("find").toString().trimmed();
  const auto replace = params.value("replace").toString().trimmed();
  if (find.isEmpty() || replace.isEmpty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      "texture_replace requires find and replace",
      preMutationFailureDetails(
        QJsonObject{{"targetSource", "material"}},
        "provide_find_and_replace_then_retry"));
  }

  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  auto& map = mapWindow->document().map();
  const auto scope = params.value("scope").toString("selection").trimmed().toLower();
  auto handles = scope == "map" ? mdl::collectBrushFaces({&map.worldNode()})
                                : map.selection().allBrushFaces();
  if (scope != "map" && scope != "selection")
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      "scope must be selection or map",
      preMutationFailureDetails(
        QJsonObject{{"scope", scope}}, "choose_selection_or_map_scope"));
  }
  if (handles.empty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      "texture_replace found no candidate faces",
      preMutationFailureDetails(
        QJsonObject{{"scope", scope}}, "select_faces_or_use_map_scope"));
  }

  const auto findMaterial = find.toStdString();
  handles.erase(
    std::remove_if(
      handles.begin(),
      handles.end(),
      [&](const auto& handle) {
        return !kdl::ci::str_is_equal(handle.face().materialName(), findMaterial);
      }),
    handles.end());
  if (handles.empty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      "No faces use the requested material",
      preMutationFailureDetails(
        QJsonObject{{"find", find}, {"scope", scope}},
        "choose_existing_material_or_expand_scope"));
  }

  const auto changedNodes = changedBrushIds(handles, map.worldNode());
  const auto transactionName = QString{"MCP: Replace texture"};
  const auto ok = executeTransaction(map, transactionName, [&]() {
    mdl::deselectAll(map);
    mdl::selectBrushFaces(map, handles);
    return mdl::setBrushFaceAttributes(
      map, mdl::UpdateBrushFaceAttributes{.materialName = replace.toStdString()});
  });
  if (!ok)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not replace texture");
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history,
    nextOperationIndex,
    map,
    toolName,
    transactionName,
    changedNodes,
    result,
    mcpIdsModeFromParams(params));
  result.insert("find", find);
  result.insert("replace", replace);
  result.insert("findMaterialExists", materialExists(map, find));
  result.insert("replaceMaterialExists", materialExists(map, replace));
  insertMissingMaterialFallback(result, map, replace);
  result.insert("faceCount", static_cast<int>(handles.size()));
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult textureAlignFaceResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry)
{
  const auto mode = params.value("mode").toString().trimmed().toLower();
  const auto alignment =
    automation::automationFaceAlignmentFromString(mode.toStdString());
  if (!alignment)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      "texture_align_face mode must be reset, paraxial, world, parallel, or face",
      preMutationFailureDetails(
        QJsonObject{{"mode", mode}}, "choose_supported_alignment_mode"));
  }

  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  auto& map = mapWindow->document().map();
  auto error = QString{};
  auto handles =
    brushFaceHandlesFromTargetsOrSelection(map, params, history, &objectRegistry, error);
  if (handles.empty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      error.isEmpty() ? "texture_align_face matched no brush faces" : error,
      preMutationFailureDetails(
        QJsonObject{{"targetSource", "faces_or_selection"}},
        "select_faces_or_fix_texture_targets"));
  }

  const auto changedNodes = changedBrushIds(handles, map.worldNode());
  const auto transactionName = QString{"MCP: Align face texture"};
  const auto ok = executeTransaction(map, transactionName, [&]() {
    return automation::alignBrushFaceAxes(map, handles, *alignment);
  });
  if (!ok)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not align face texture");
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history,
    nextOperationIndex,
    map,
    toolName,
    transactionName,
    changedNodes,
    result,
    mcpIdsModeFromParams(params));
  result.insert("mode", mode);
  result.insert("faceCount", static_cast<int>(handles.size()));
  result.insert("faceSemantic", params.value("faceSemantic").toString("all"));
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult textureCopyFromFaceResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return textureCopyFromFaceForMapResult(
    mapWindow->document().map(),
    toolName,
    params,
    history,
    nextOperationIndex,
    objectRegistry);
}

McpBridgeToolResult textureCopyFromFaceForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry)
{
  auto error = QString{};
  const auto source =
    brushFaceHandleFromJson(map, params, "sourceObjectId", "sourceFaceIndex", error);
  if (!source)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      error,
      preMutationFailureDetails(
        QJsonObject{{"targetSource", "source_face"}},
        "provide_valid_source_face_then_retry"));
  }

  auto targetParams = params;
  targetParams.remove("sourceObjectId");
  targetParams.remove("sourceFaceIndex");
  auto handles = brushFaceHandlesFromTargetsOrSelection(
    map, targetParams, history, &objectRegistry, error);
  if (handles.empty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      error.isEmpty() ? "texture_copy_from_face matched no target brush faces" : error,
      preMutationFailureDetails(
        QJsonObject{{"targetSource", "target_faces_or_selection"}},
        "provide_target_faces_or_select_brush_faces"));
  }

  const auto changedNodes = changedBrushIds(handles, map.worldNode());
  const auto transactionName = QString{"MCP: Copy face texture"};
  const auto ok = executeTransaction(map, transactionName, [&]() {
    return automation::copyBrushFaceAttributes(map, *source, handles);
  });
  if (!ok)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not copy face texture");
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history,
    nextOperationIndex,
    map,
    toolName,
    transactionName,
    changedNodes,
    result,
    mcpIdsModeFromParams(params));
  result.insert("faceCount", static_cast<int>(handles.size()));
  result.insert("source", brushFaceJson(*source, map.worldNode()));
  return McpBridgeToolResult::success(std::move(result));
}
} // namespace tb::ui
