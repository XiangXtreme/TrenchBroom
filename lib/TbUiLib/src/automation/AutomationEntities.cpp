/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.
 */

#include "ui/automation/AutomationEntities.h"

#include "base/Color.h"
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
#include "mdl/ModelUtils.h"
#include "mdl/Node.h"
#include "mdl/WorldNode.h"
#include "ui/automation/AutomationTransaction.h"

#include "kd/vector_utils.h"

#include <algorithm>
#include <map>
#include <ranges>
#include <set>
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

QString entityPropertyString(const mdl::Entity& entity, const QString& key)
{
  if (const auto* value = entity.property(key.toStdString()))
  {
    return QString::fromStdString(*value);
  }
  return {};
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

std::vector<mdl::BrushNode*> normalizeBrushes(
  std::vector<mdl::BrushNode*> brushes, const mdl::Map& map)
{
  brushes.erase(
    std::remove_if(
      brushes.begin(),
      brushes.end(),
      [&](const auto* brush) {
        return brush == nullptr || !brush->isDescendantOf(map.worldNode());
      }),
    brushes.end());
  std::ranges::sort(brushes);
  brushes.erase(std::unique(brushes.begin(), brushes.end()), brushes.end());
  return brushes;
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

AutomationBrushEntityResult tieBrushesToEntity(
  mdl::Map& map, const std::string& classname, std::vector<mdl::BrushNode*> brushes)
{
  auto result = AutomationBrushEntityResult{};
  if (classname.empty())
  {
    result.error = "classname must not be empty";
    return result;
  }
  const auto* definition = map.entityDefinitionManager().definition(classname);
  if (definition == nullptr)
  {
    result.error = QString{"FGD does not define entity classname: %1"}.arg(
      QString::fromStdString(classname));
    return result;
  }
  if (mdl::getType(*definition) != mdl::EntityDefinitionType::Brush)
  {
    result.error =
      QString{"entity must be a brush entity: %1"}.arg(QString::fromStdString(classname));
    return result;
  }

  brushes = normalizeBrushes(std::move(brushes), map);
  if (brushes.empty())
  {
    result.error = "brushes must not be empty";
    return result;
  }

  auto transaction = AutomationTransaction{map, "Tie brushes to " + classname};
  mdl::deselectAll(map);
  mdl::selectNodes(map, kdl::vec_static_cast<mdl::Node*>(brushes));
  auto* entity = mdl::createBrushEntity(map, *definition);
  if (entity == nullptr)
  {
    transaction.cancel();
    result.error = "Could not tie brushes to entity";
    return result;
  }
  if (!transaction.commit())
  {
    result.error = "Could not commit tied brushes";
    return result;
  }

  result.entity = entity;
  result.brushes = std::move(brushes);
  return result;
}

AutomationBrushEntityResult untieBrushesFromEntity(
  mdl::Map& map, std::vector<mdl::BrushNode*> brushes)
{
  auto result = AutomationBrushEntityResult{};
  brushes = normalizeBrushes(std::move(brushes), map);
  brushes.erase(
    std::remove_if(
      brushes.begin(),
      brushes.end(),
      [&](const auto* brush) { return brush->entity() == &map.worldNode(); }),
    brushes.end());
  if (brushes.empty())
  {
    result.error = "No brush entity brushes supplied";
    return result;
  }

  const auto nodes = kdl::vec_static_cast<mdl::Node*>(brushes);
  auto& parent = mdl::parentForNodes(map, nodes);
  auto transaction = AutomationTransaction{map, "Untie brushes"};
  // Reparenting can remove an empty brush entity. Clear child selection first so its
  // cached ancestor selection counts remain consistent while that parent is removed.
  mdl::deselectAll(map);
  if (!mdl::reparentNodes(map, {{&parent, nodes}}))
  {
    transaction.cancel();
    result.error = "Could not untie brushes";
    return result;
  }
  mdl::selectNodes(map, nodes);
  if (!transaction.commit())
  {
    result.error = "Could not commit untied brushes";
    return result;
  }

  result.brushes = std::move(brushes);
  return result;
}

AutomationEntityLinkChainResult inspectEntityLinkChain(
  const mdl::Map& map,
  const mdl::EntityNodeBase& start,
  const QString& classname,
  const QString& nameKey,
  const QString& nextKey)
{
  auto result = AutomationEntityLinkChainResult{};
  if (nameKey.trimmed().isEmpty() || nextKey.trimmed().isEmpty())
  {
    result.error = "nameKey and nextKey must not be empty";
    return result;
  }
  if (!entityMatchesClassname(start, classname))
  {
    result.error = "start entity does not match classname";
    return result;
  }

  const auto& worldNode = map.worldNode();
  collectEntityNodes(worldNode, worldNode, classname, result.candidates);
  const auto startIt =
    std::find(result.candidates.begin(), result.candidates.end(), &start);
  if (startIt == result.candidates.end())
  {
    result.error = "start entity does not belong to this map";
    return result;
  }

  auto byName = std::map<QString, std::vector<const mdl::EntityNodeBase*>>{};
  for (const auto* entityNode : result.candidates)
  {
    const auto name = entityPropertyString(entityNode->entity(), nameKey);
    if (!name.isEmpty())
    {
      byName[name].push_back(entityNode);
    }
  }
  for (const auto& [name, nodes] : byName)
  {
    if (nodes.size() > 1u)
    {
      result.duplicateNames.push_back(QJsonObject{
        {"name", name},
        {"count", static_cast<int>(nodes.size())},
      });
    }
  }

  auto visited = std::set<const mdl::EntityNodeBase*>{};
  auto* current = &start;
  while (current != nullptr)
  {
    result.nodes.push_back(current);
    if (!visited.insert(current).second)
    {
      result.chainComplete = false;
      result.hasCycle = true;
      break;
    }

    const auto fromName = entityPropertyString(current->entity(), nameKey);
    const auto nextName = entityPropertyString(current->entity(), nextKey);
    if (fromName.isEmpty())
    {
      result.warnings.push_back({current, "missing_name", nameKey});
    }
    if (nextName.isEmpty())
    {
      break;
    }

    auto edge = QJsonObject{{"from", fromName}, {"to", nextName}};
    const auto targetIt = byName.find(nextName);
    if (targetIt == byName.end())
    {
      edge.insert("status", "missing_target");
      result.edges.push_back(edge);
      result.failures.push_back(QJsonObject{
        {"status", "missing_target"},
        {"from", fromName},
        {"to", nextName},
        {"recoveryAction", "fix_missing_entity_target_or_stop_chain"},
      });
      result.chainComplete = false;
      break;
    }
    if (targetIt->second.size() != 1u)
    {
      edge.insert("status", "duplicate_targetname");
      edge.insert("matchCount", static_cast<int>(targetIt->second.size()));
      result.edges.push_back(edge);
      result.failures.push_back(QJsonObject{
        {"status", "duplicate_targetname"},
        {"from", fromName},
        {"to", nextName},
        {"recoveryAction", "rename_duplicate_targetname_then_retry"},
      });
      result.chainComplete = false;
      break;
    }

    const auto* target = targetIt->second.front();
    if (visited.contains(target))
    {
      edge.insert("status", "cycle");
      result.edges.push_back(edge);
      result.failures.push_back(QJsonObject{
        {"status", "cycle"},
        {"from", fromName},
        {"to", nextName},
        {"recoveryAction", "break_entity_link_cycle_then_retry"},
      });
      result.chainComplete = false;
      result.hasCycle = true;
      break;
    }
    edge.insert("status", "resolved");
    result.edges.push_back(edge);
    current = target;
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
