/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.
 */

#include "ui/automation/AutomationIrExecution.h"

#include "mdl/BrushBuilder.h"
#include "mdl/BrushNode.h"
#include "mdl/EntityNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Selection.h"
#include "mdl/Node.h"
#include "mdl/WorldNode.h"
#include "ui/automation/AutomationBrushes.h"
#include "ui/automation/AutomationEntities.h"
#include "ui/automation/AutomationIr.h"
#include "ui/automation/AutomationNodes.h"
#include "ui/automation/AutomationTransaction.h"

#include "vm/bbox.h"

#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace tb::ui::automation
{
namespace
{

void deleteNodes(std::vector<mdl::Node*>& nodes)
{
  for (auto* node : nodes)
  {
    delete node;
  }
  nodes.clear();
}

std::optional<vm::vec3d> vec3FromJson(const QJsonValue& value, QString& error)
{
  if (!value.isArray())
  {
    error = "must be a three-number array";
    return std::nullopt;
  }
  const auto values = value.toArray();
  if (values.size() != 3)
  {
    error = "must contain exactly three numbers";
    return std::nullopt;
  }
  for (const auto& entry : values)
  {
    if (!entry.isDouble() || !std::isfinite(entry.toDouble()))
    {
      error = "must contain finite numbers";
      return std::nullopt;
    }
  }
  return vm::vec3d{values[0].toDouble(), values[1].toDouble(), values[2].toDouble()};
}

std::optional<std::vector<vm::vec2d>> vec2ListFromJson(
  const QJsonValue& value, QString& error)
{
  if (!value.isArray())
  {
    error = "points2d must be an array";
    return std::nullopt;
  }
  auto result = std::vector<vm::vec2d>{};
  for (const auto& entry : value.toArray())
  {
    if (!entry.isArray() || entry.toArray().size() != 2)
    {
      error = "points2d entries must contain exactly two numbers";
      return std::nullopt;
    }
    const auto point = entry.toArray();
    if (
      !point[0].isDouble() || !point[1].isDouble() || !std::isfinite(point[0].toDouble())
      || !std::isfinite(point[1].toDouble()))
    {
      error = "points2d entries must contain finite numbers";
      return std::nullopt;
    }
    result.emplace_back(point[0].toDouble(), point[1].toDouble());
  }
  return result;
}

std::optional<std::string> materialFromOperation(
  const QJsonObject& operation, const std::string& defaultMaterial, QString& error)
{
  const auto material = operation.value("material");
  if (material.isUndefined() || material.isNull())
  {
    return defaultMaterial;
  }
  if (!material.isString())
  {
    error = "material must be a string";
    return std::nullopt;
  }
  return material.toString().toStdString();
}

std::optional<AutomationBoxSpec> boxSpecFromOperation(
  const QJsonObject& operation, const std::string& defaultMaterial, QString& error)
{
  const auto material = materialFromOperation(operation, defaultMaterial, error);
  if (!material)
  {
    return std::nullopt;
  }
  const auto minValue = operation.value("min");
  const auto maxValue = operation.value("max");
  if (!minValue.isUndefined() || !maxValue.isUndefined())
  {
    if (minValue.isUndefined() || maxValue.isUndefined())
    {
      error = "box requires both min and max";
      return std::nullopt;
    }
    const auto min = vec3FromJson(minValue, error);
    if (!min)
    {
      error = QString{"box min %1"}.arg(error);
      return std::nullopt;
    }
    const auto max = vec3FromJson(maxValue, error);
    if (!max)
    {
      error = QString{"box max %1"}.arg(error);
      return std::nullopt;
    }
    return AutomationBoxSpec{.bounds = vm::bbox3d{*min, *max}, .material = *material};
  }

  const auto size = vec3FromJson(operation.value("size"), error);
  if (!size)
  {
    error = QString{"box size %1"}.arg(error);
    return std::nullopt;
  }
  auto origin = vm::vec3d{0.0, 0.0, 0.0};
  if (!operation.value("origin").isUndefined())
  {
    const auto parsedOrigin = vec3FromJson(operation.value("origin"), error);
    if (!parsedOrigin)
    {
      error = QString{"box origin %1"}.arg(error);
      return std::nullopt;
    }
    origin = *parsedOrigin;
  }
  return AutomationBoxSpec{
    .bounds = vm::bbox3d{origin, origin + *size}, .material = *material};
}

bool appendPrismNode(
  const mdl::Map& map,
  const QJsonObject& operation,
  const std::string& defaultMaterial,
  std::vector<mdl::Node*>& nodes,
  QString& error)
{
  const auto material = materialFromOperation(operation, defaultMaterial, error);
  if (!material)
  {
    return false;
  }
  const auto points = vec2ListFromJson(operation.value("points2d"), error);
  if (!points)
  {
    return false;
  }
  const auto minZ = operation.value("minZ");
  const auto maxZ = operation.value("maxZ");
  if (
    !minZ.isDouble() || !maxZ.isDouble() || !std::isfinite(minZ.toDouble())
    || !std::isfinite(maxZ.toDouble()))
  {
    error = "prism minZ and maxZ must be finite numbers";
    return false;
  }
  const auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};
  const auto brush = createPrismBrush(
    builder, *points, minZ.toDouble(), maxZ.toDouble(), *material, error);
  if (!brush)
  {
    return false;
  }
  nodes.push_back(new mdl::BrushNode{std::move(*brush)});
  return true;
}

std::optional<std::vector<AutomationPointEntitySpec>> entitySpecsFromIr(
  const QJsonArray& entities, QString& error)
{
  auto result = std::vector<AutomationPointEntitySpec>{};
  result.reserve(entities.size());
  for (auto index = 0; index < entities.size(); ++index)
  {
    if (!entities[index].isObject())
    {
      error = QString{"entities[%1] must be an object"}.arg(index);
      return std::nullopt;
    }
    const auto entity = entities[index].toObject();
    const auto classname = entity.value("classname");
    if (!classname.isString() || classname.toString().trimmed().isEmpty())
    {
      error = QString{"entities[%1] requires classname"}.arg(index);
      return std::nullopt;
    }
    auto spec = AutomationPointEntitySpec{};
    spec.classname = classname.toString().trimmed().toStdString();
    if (const auto properties = entity.value("properties"); !properties.isUndefined())
    {
      if (!properties.isObject())
      {
        error = QString{"entities[%1].properties must be an object"}.arg(index);
        return std::nullopt;
      }
      const auto propertyObject = properties.toObject();
      for (auto it = propertyObject.begin(); it != propertyObject.end(); ++it)
      {
        if (!it.value().isString())
        {
          error = QString{"entities[%1].properties.%2 must be a string"}.arg(index).arg(
            it.key());
          return std::nullopt;
        }
        spec.properties.emplace(
          it.key().toStdString(), it.value().toString().toStdString());
      }
    }
    if (!entity.value("origin").isUndefined())
    {
      const auto origin = vec3FromJson(entity.value("origin"), error);
      if (!origin)
      {
        error = QString{"entities[%1].origin %2"}.arg(index).arg(error);
        return std::nullopt;
      }
      spec.origin = *origin;
    }
    result.push_back(std::move(spec));
  }
  return result;
}

} // namespace

AutomationIrApplyResult applyAutomationIr(
  mdl::Map& map, const QJsonObject& request, const AutomationIrApplyOptions& options)
{
  auto result = AutomationIrApplyResult{};
  const auto parsed = parseAutomationIr(QJsonObject{{"ir", request}});
  if (!parsed.ir)
  {
    result.error = parsed.error;
    return result;
  }
  result.ir = *parsed.ir;
  result.warnings = parsed.warnings;
  result.preview = previewAutomationIr(*parsed.ir);

  if (parsed.ir->value("applyMode").toString("create") != "create")
  {
    result.error = "replace_module requires module state and preview guards";
    return result;
  }

  const auto defaultMaterial = parsed.ir->value("material").isString()
                                 ? parsed.ir->value("material").toString().toStdString()
                                 : map.currentMaterialName();
  auto geometryNodes = std::vector<mdl::Node*>{};
  auto entityNodes = std::vector<mdl::Node*>{};
  const auto cleanup = [&] {
    deleteNodes(geometryNodes);
    deleteNodes(entityNodes);
  };

  auto boxSpecs = std::vector<AutomationBoxSpec>{};
  for (auto index = 0; index < parsed.ir->value("operations").toArray().size(); ++index)
  {
    const auto operation = parsed.ir->value("operations").toArray()[index].toObject();
    const auto type = operation.value("type").toString().trimmed().toLower();
    auto error = QString{};
    if (type == "box")
    {
      const auto spec = boxSpecFromOperation(operation, defaultMaterial, error);
      if (!spec)
      {
        cleanup();
        result.error = QString{"operations[%1]: %2"}.arg(index).arg(error);
        return result;
      }
      boxSpecs.push_back(*spec);
    }
    else if (type == "prism")
    {
      if (!boxSpecs.empty())
      {
        auto boxes = createBoxNodes(map, boxSpecs, error);
        if (!boxes)
        {
          cleanup();
          result.error = error;
          return result;
        }
        for (auto* node : *boxes)
        {
          geometryNodes.push_back(node);
        }
        boxSpecs.clear();
      }
      if (!appendPrismNode(map, operation, defaultMaterial, geometryNodes, error))
      {
        cleanup();
        result.error = QString{"operations[%1]: %2"}.arg(index).arg(error);
        return result;
      }
    }
    else
    {
      cleanup();
      result.error =
        QString{"operations[%1]: unsupported IR operation type '%2'"}.arg(index).arg(
          type);
      return result;
    }
  }
  if (!boxSpecs.empty())
  {
    auto error = QString{};
    auto boxes = createBoxNodes(map, boxSpecs, error);
    if (!boxes)
    {
      cleanup();
      result.error = error;
      return result;
    }
    for (auto* node : *boxes)
    {
      geometryNodes.push_back(node);
    }
  }

  const auto entitySpecs =
    entitySpecsFromIr(parsed.ir->value("entities").toArray(), result.error);
  if (!entitySpecs)
  {
    cleanup();
    return result;
  }
  if (!entitySpecs->empty())
  {
    auto built = buildCheckedPointEntities(map, *entitySpecs);
    if (!built.error.isEmpty())
    {
      cleanup();
      result.error = built.error;
      return result;
    }
    for (auto* node : built.nodes)
    {
      entityNodes.push_back(node);
    }
    built.nodes.clear();
  }

  auto allNodes = geometryNodes;
  allNodes.insert(allNodes.end(), entityNodes.begin(), entityNodes.end());
  const auto transactionName =
    options.transactionName.trimmed().isEmpty()
      ? parsed.ir->value("name").toString("Automation: Apply IR")
      : options.transactionName;
  auto transaction = AutomationTransaction{map, transactionName.toStdString()};
  if (!addNodes(map, allNodes, false))
  {
    transaction.cancel();
    cleanup();
    result.error = "Could not add IR nodes";
    return result;
  }
  if (!transaction.commit())
  {
    result.error = "Could not commit IR transaction";
    return result;
  }

  auto selectedNodes = std::vector<mdl::Node*>{};
  if (parsed.ir->value("select").toBool(true))
  {
    selectedNodes.insert(selectedNodes.end(), geometryNodes.begin(), geometryNodes.end());
  }
  if (parsed.ir->value("selectEntities").toBool(false))
  {
    selectedNodes.insert(selectedNodes.end(), entityNodes.begin(), entityNodes.end());
  }
  if (!selectedNodes.empty())
  {
    mdl::deselectAll(map);
    mdl::selectNodes(map, selectedNodes);
  }

  result.ok = true;
  result.createdNodes = std::move(allNodes);
  result.brushCount = static_cast<int>(geometryNodes.size());
  result.entityCount = static_cast<int>(entityNodes.size());
  result.preview.insert("estimatedBrushCount", result.brushCount);
  result.preview.insert("estimatedObjectCount", result.brushCount + result.entityCount);
  return result;
}

} // namespace tb::ui::automation
