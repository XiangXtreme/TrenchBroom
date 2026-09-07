/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.
 */

#include "ui/automation/AutomationEntities.h"

#include "mdl/Entity.h"
#include "mdl/EntityDefinition.h"
#include "mdl/EntityDefinitionManager.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityProperties.h"
#include "mdl/Map.h"

namespace tb::ui::automation
{
namespace
{

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
