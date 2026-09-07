/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.
 */

#include "ui/automation/AutomationEntities.h"

#include "base/Color.h"
#include "mdl/Entity.h"
#include "mdl/EntityDefinition.h"
#include "mdl/EntityDefinitionManager.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityProperties.h"
#include "mdl/Map.h"

#include <type_traits>

namespace tb::ui::automation
{
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

int removeEmptyProperties(mdl::Entity& entity)
{
  auto keys = std::vector<std::string>{};
  for (const auto& property : entity.properties())
  {
    if (property.key() != mdl::EntityPropertyKeys::Classname && property.value().empty())
    {
      keys.push_back(property.key());
    }
  }
  for (const auto& key : keys)
  {
    entity.removeProperty(key);
  }
  return static_cast<int>(keys.size());
}

} // namespace

void deletePointEntityNodes(std::vector<mdl::EntityNode*>& nodes)
{
  for (auto* node : nodes)
  {
    delete node;
  }
  nodes.clear();
}

QJsonArray listEntityDefinitionSummaries(
  const mdl::Map& map, const QString& type, const QString& query, const size_t limit)
{
  auto result = QJsonArray{};
  if (limit == 0u)
  {
    return result;
  }
  for (const auto& definition : map.entityDefinitionManager().definitions())
  {
    const auto definitionType = entityDefinitionTypeName(definition);
    if (!type.isEmpty() && definitionType != type)
    {
      continue;
    }
    const auto name = QString::fromStdString(definition.name);
    const auto description = QString::fromStdString(definition.description);
    if (
      !query.isEmpty() && !name.contains(query, Qt::CaseInsensitive)
      && !description.contains(query, Qt::CaseInsensitive))
    {
      continue;
    }
    result.push_back(QJsonObject{
      {"classname", name},
      {"type", definitionType},
      {"description", description},
      {"propertyCount", static_cast<int>(definition.propertyDefinitions.size())},
    });
    if (result.size() >= static_cast<qsizetype>(limit))
    {
      break;
    }
  }
  return result;
}

std::optional<QJsonObject> entityDefinitionSchema(const mdl::Map& map, const QString& classname)
{
  const auto* definition =
    map.entityDefinitionManager().definition(classname.trimmed().toStdString());
  if (definition == nullptr)
  {
    return std::nullopt;
  }

  auto properties = QJsonArray{};
  for (const auto& property : definition->propertyDefinitions)
  {
    properties.push_back(propertyDefinitionJson(property));
  }
  auto result = QJsonObject{
    {"classname", QString::fromStdString(definition->name)},
    {"type", entityDefinitionTypeName(*definition)},
    {"description", QString::fromStdString(definition->description)},
    {"propertyCount", properties.size()},
    {"properties", properties},
  };
  if (const auto* pointDefinition = mdl::getPointEntityDefinition(definition))
  {
    result.insert("bounds", boundsToJson(pointDefinition->bounds));
  }
  return result;
}

AutomationPointEntityBuildResult buildCheckedPointEntities(
  const mdl::Map& map, const std::vector<AutomationPointEntitySpec>& entities)
{
  auto result = AutomationPointEntityBuildResult{};
  if (entities.empty())
  {
    result.error = "entities must not be empty";
    return result;
  }

  result.nodes.reserve(entities.size());
  result.classnames.reserve(entities.size());
  for (size_t index = 0; index < entities.size(); ++index)
  {
    const auto& spec = entities[index];
    result.failedIndex = static_cast<int>(index);
    if (spec.classname.empty())
    {
      result.error = "classname must not be empty";
      deletePointEntityNodes(result.nodes);
      return result;
    }
    const auto* definition = map.entityDefinitionManager().definition(spec.classname);
    if (definition == nullptr)
    {
      result.error = QString{"FGD does not define entity classname: %1"}
                       .arg(QString::fromStdString(spec.classname));
      deletePointEntityNodes(result.nodes);
      return result;
    }
    if (mdl::getType(*definition) != mdl::EntityDefinitionType::Point)
    {
      result.error = QString{"entity must be a point entity: %1"}
                       .arg(QString::fromStdString(spec.classname));
      deletePointEntityNodes(result.nodes);
      return result;
    }

    auto entity = mdl::Entity{{{mdl::EntityPropertyKeys::Classname, spec.classname}}};
    mdl::setDefaultProperties(*definition, entity, mdl::SetDefaultPropertyMode::SetAll);
    entity.setOrigin(spec.origin);
    for (const auto& [key, value] : spec.properties)
    {
      if (!value.empty() && key != mdl::EntityPropertyKeys::Classname)
      {
        entity.addOrUpdateProperty(key, value);
      }
    }
    result.removedEmptyPropertyCount += removeEmptyProperties(entity);
    result.nodes.push_back(new mdl::EntityNode{std::move(entity)});
    result.classnames.push_back(spec.classname);
  }
  result.failedIndex = -1;
  return result;
}

} // namespace tb::ui::automation
