/*
 Copyright (C) 2010 Kristian Duske

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

#include "mdl/SwapNodeContentsCommand.h"

#include "base/Notifier.h"
#include "mdl/BrushFace.h"
#include "mdl/GameInfo.h"
#include "mdl/Map.h"
#include "mdl/Map_Assets.h"
#include "mdl/Map_World.h"
#include "mdl/Node.h"
#include "mdl/NodeQueries.h"
#include "mdl/TagManager.h"

#include "kd/ranges/to.h"

#include <algorithm>
#include <ranges>

namespace tb::mdl
{
namespace
{

auto notifySpecialWorldProperties(
  const std::vector<std::pair<Node*, NodeContents>>& nodesToSwap)
{
  for (const auto& [node, contents] : nodesToSwap)
  {
    if (const auto* worldNode = dynamic_cast<const WorldNode*>(node))
    {
      const auto& oldEntity = worldNode->entity();
      const auto& newEntity = std::get<Entity>(contents.get());

      const auto* oldWads = oldEntity.property(EntityPropertyKeys::Wad);
      const auto* newWads = newEntity.property(EntityPropertyKeys::Wad);

      const bool notifyWadsChange =
        (oldWads == nullptr) != (newWads == nullptr)
        || (oldWads != nullptr && newWads != nullptr && *oldWads != *newWads);

      const auto oldEntityDefinitionSpec = entityDefinitionFile(oldEntity);
      const auto newEntityDefinitionSpec = entityDefinitionFile(newEntity);
      const bool notifyEntityDefinitionsChange =
        oldEntityDefinitionSpec != newEntityDefinitionSpec;

      const auto oldMods = enabledMods(oldEntity);
      const auto newMods = enabledMods(newEntity);
      const bool notifyModsChange = oldMods != newMods;

      return std::tuple{
        notifyWadsChange, notifyEntityDefinitionsChange, notifyModsChange};
    }
  }

  return std::tuple{false, false, false};
}

void doSwapNodeContents(
  std::vector<std::pair<Node*, NodeContents>>& nodesToSwap, Map& map)
{
  auto change = NodeContentsChange{};
  change.nodes = nodesToSwap | std::views::keys | kdl::ranges::to<std::vector>();
  change.updateTags = !std::ranges::all_of(
    map.tagManager().smartTags(), &SmartTag::canPreserveTagsOnGeometryChange);
  auto smartTagMask = TagType::NoType;
  for (const auto& tag : map.tagManager().smartTags())
  {
    smartTagMask |= tag.type();
  }

  for (auto& [node, contents] : nodesToSwap)
  {
    const auto indexChanged = node->accept(kdl::overload(
      [&](const WorldNode& world) {
        const auto& entity = std::get<Entity>(contents.get());
        change.updateTags |= world.entity().classname() != entity.classname();
        return world.entity().properties() != entity.properties();
      },
      [](const LayerNode&) { return false; },
      [&](const GroupNode& group) {
        return group.group().name() != std::get<Group>(contents.get()).name();
      },
      [&](const EntityNode& entityNode) {
        const auto& entity = std::get<Entity>(contents.get());
        change.updateTags |= entityNode.entity().classname() != entity.classname();
        return entityNode.entity().properties() != entity.properties();
      },
      [&](const BrushNode& brushNode) {
        const auto& oldBrush = brushNode.brush();
        auto& newBrush = std::get<Brush>(contents.get());
        if (oldBrush.faceCount() != newBrush.faceCount())
        {
          change.updateTags = true;
          return true;
        }
        auto materialNamesChanged = false;
        for (size_t i = 0; i < oldBrush.faceCount(); ++i)
        {
          const auto& oldFace = oldBrush.face(i);
          auto& newFace = newBrush.face(i);
          const auto sameMaterial = oldFace.materialName() == newFace.materialName();
          materialNamesChanged |= !sameMaterial;
          if (
            sameMaterial && oldFace.surfaceAttributes() == newFace.surfaceAttributes()
            && (oldFace.tagMask() & ~smartTagMask) == (newFace.tagMask() & ~smartTagMask))
          {
            // Undo contents may predate a resource reload. Use the live face's tags,
            // rather than restoring stale cached matches from the undo stack.
            static_cast<Taggable&>(newFace) = static_cast<const Taggable&>(oldFace);
          }
          else
          {
            change.updateTags = true;
          }
        }
        return materialNamesChanged;
      },
      [&](const PatchNode& patchNode) {
        const auto materialChanged =
          patchNode.patch().materialName()
          != std::get<BezierPatch>(contents.get()).materialName();
        change.updateTags |= materialChanged;
        return materialChanged;
      }));
    if (indexChanged)
    {
      change.indexedNodes.push_back(node);
    }
  }

  auto notifyNodes = NotifyNodesChange{map, change};

  const auto [notifyWadsChange, notifyEntityDefinitionsChange, notifyModsChange] =
    notifySpecialWorldProperties(nodesToSwap);
  change.updateTags |=
    notifyWadsChange || notifyEntityDefinitionsChange || notifyModsChange;
  auto notifyWads = NotifyBeforeAndAfter{
    notifyWadsChange,
    map.materialCollectionsWillChangeNotifier,
    map.materialCollectionsDidChangeNotifier};
  auto notifyEntityDefinitions = NotifyBeforeAndAfter{
    notifyEntityDefinitionsChange,
    map.entityDefinitionsWillChangeNotifier,
    map.entityDefinitionsDidChangeNotifier};
  auto notifyMods = NotifyBeforeAndAfter{
    notifyModsChange, map.modsWillChangeNotifier, map.modsDidChangeNotifier};

  for (auto& pair : nodesToSwap)
  {
    auto* node = pair.first;
    auto& contents = pair.second.get();

    pair.second = node->accept(kdl::overload(
      [&](WorldNode& worldNode) {
        return NodeContents{worldNode.setEntity(std::get<Entity>(std::move(contents)))};
      },
      [&](LayerNode& layerNode) {
        return NodeContents(layerNode.setLayer(std::get<Layer>(std::move(contents))));
      },
      [&](GroupNode& groupNode) {
        return NodeContents{groupNode.setGroup(std::get<Group>(std::move(contents)))};
      },
      [&](EntityNode& entityNode) {
        return NodeContents{entityNode.setEntity(std::get<Entity>(std::move(contents)))};
      },
      [&](BrushNode& brushNode) {
        return NodeContents{brushNode.setBrush(std::get<Brush>(std::move(contents)))};
      },
      [&](PatchNode& patchNode) {
        return NodeContents{
          patchNode.setPatch(std::get<BezierPatch>(std::move(contents)))};
      }));
  }
}

} // namespace

SwapNodeContentsCommand::SwapNodeContentsCommand(
  std::string name, std::vector<std::pair<Node*, NodeContents>> nodes)
  : UpdateLinkedGroupsCommandBase{std::move(name), true}
  , m_nodes{std::move(nodes)}
{
}

SwapNodeContentsCommand::~SwapNodeContentsCommand() = default;

bool SwapNodeContentsCommand::doPerformDo(Map& map)
{
  doSwapNodeContents(m_nodes, map);
  return true;
}

bool SwapNodeContentsCommand::doPerformUndo(Map& map)
{
  doSwapNodeContents(m_nodes, map);
  return true;
}

bool SwapNodeContentsCommand::doCollateWith(UndoableCommand& command)
{
  if (auto* other = dynamic_cast<SwapNodeContentsCommand*>(&command))
  {
    auto myNodes = m_nodes | std::views::keys | kdl::ranges::to<std::vector>();
    auto theirNodes = other->m_nodes | std::views::keys | kdl::ranges::to<std::vector>();

    std::ranges::sort(myNodes);
    std::ranges::sort(theirNodes);

    return myNodes == theirNodes;
  }

  return false;
}

} // namespace tb::mdl
