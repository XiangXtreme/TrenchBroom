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

#include "mdl/ModelUtils.h"

#include "mdl/BrushFace.h"
#include "mdl/BrushFaceHandle.h"
#include "mdl/EditorContext.h"
#include "mdl/HitAdapter.h"
#include "mdl/NodeQueries.h"

#include "kd/contracts.h"
#include "kd/flat_set.h"
#include "kd/ranges/cartesian_product_view.h"
#include "kd/ranges/contains.h"
#include "kd/ranges/to.h"
#include "kd/stable_remove_duplicates.h"
#include "kd/string_compare.h"
#include "kd/vector_utils.h"

#include "vm/bbox.h"
#include "vm/distance.h"
#include "vm/intersection.h"
#include "vm/scalar.h"
#include "vm/segment.h"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <unordered_map>
#include <utility>
#include <vector>

namespace tb::mdl
{

HitType::Type nodeHitType()
{
  return EntityNode::EntityHitType | BrushNode::BrushHitType | PatchNode::PatchHitType;
}

LayerNode* findContainingLayer(Node* node)
{
  return node->accept(kdl::overload(
    [](WorldNode&) -> LayerNode* { return nullptr; },
    [](LayerNode& layerNode) -> LayerNode* { return &layerNode; },
    [](auto&& thisLambda, GroupNode& groupNode) -> LayerNode* {
      return groupNode.visitParent(thisLambda).value_or(nullptr);
    },
    [](auto&& thisLambda, EntityNode& entityNode) -> LayerNode* {
      return entityNode.visitParent(thisLambda).value_or(nullptr);
    },
    [](auto&& thisLambda, BrushNode& brushNode) -> LayerNode* {
      return brushNode.visitParent(thisLambda).value_or(nullptr);
    },
    [](auto&& thisLambda, PatchNode& patchNode) -> LayerNode* {
      return patchNode.visitParent(thisLambda).value_or(nullptr);
    }));
}

std::vector<LayerNode*> collectContainingLayersUserSorted(const std::vector<Node*>& nodes)
{
  std::vector<LayerNode*> layers;
  for (auto* node : nodes)
  {
    if (auto* layer = findContainingLayer(node))
    {
      layers.push_back(layer);
    }
  }
  return kdl::vec_sort_and_remove_duplicates(std::move(layers));
}

GroupNode* findContainingGroup(Node* node)
{
  return node
    ->visitParent(kdl::overload(
      [](WorldNode&) -> GroupNode* { return nullptr; },
      [](LayerNode&) -> GroupNode* { return nullptr; },
      [](GroupNode& groupNode) -> GroupNode* { return &groupNode; },
      [](auto&& thisLambda, EntityNode& entityNode) -> GroupNode* {
        return entityNode.visitParent(thisLambda).value_or(nullptr);
      },
      [](auto&& thisLambda, BrushNode& brushNode) -> GroupNode* {
        return brushNode.visitParent(thisLambda).value_or(nullptr);
      },
      [](auto&& thisLambda, PatchNode& patchNode) -> GroupNode* {
        return patchNode.visitParent(thisLambda).value_or(nullptr);
      }))
    .value_or(nullptr);
}

const GroupNode* findContainingGroup(const Node* node)
{
  return findContainingGroup(const_cast<Node*>(node));
}

EntityNodeBase* findContainingEntity(Node* node)
{
  return node->accept(kdl::overload(
    [](WorldNode&) -> EntityNodeBase* { return nullptr; },
    [](LayerNode&) -> EntityNodeBase* { return nullptr; },
    [](GroupNode&) -> EntityNodeBase* { return nullptr; },
    [](EntityNode&) -> EntityNodeBase* { return nullptr; },
    [](BrushNode& brushNode) { return brushNode.entity(); },
    [](PatchNode& patchNode) { return patchNode.entity(); }));
}

const EntityNodeBase* findContainingEntity(const Node* node)
{
  return findContainingEntity(const_cast<Node*>(node));
}

GroupNode* findOutermostClosedGroup(Node* node)
{
  return node
    ->visitParent(kdl::overload(
      [](WorldNode&) -> GroupNode* { return nullptr; },
      [](LayerNode&) -> GroupNode* { return nullptr; },
      [](auto&& thisLambda, GroupNode& groupNode) -> GroupNode* {
        if (auto* parentResult = groupNode.visitParent(thisLambda).value_or(nullptr))
        {
          return parentResult;
        }
        // we didn't find a result searching the parent chain, so either return
        // this group (if it's closed) or nullptr to indicate no result
        return groupNode.closed() ? &groupNode : nullptr;
      },
      [](auto&& thisLambda, EntityNode& entityNode) -> GroupNode* {
        return entityNode.visitParent(thisLambda).value_or(nullptr);
      },
      [](auto&& thisLambda, BrushNode& brushNode) -> GroupNode* {
        return brushNode.visitParent(thisLambda).value_or(nullptr);
      },
      [](auto&& thisLambda, PatchNode& patchNode) -> GroupNode* {
        return patchNode.visitParent(thisLambda).value_or(nullptr);
      }))
    .value_or(nullptr);
}

const GroupNode* findOutermostClosedGroup(const Node* node)
{
  return findOutermostClosedGroup(const_cast<Node*>(node));
}

Node* findOutermostClosedGroupOrNode(Node* node)
{
  if (auto* group = findOutermostClosedGroup(node))
  {
    return group;
  }

  return node;
}

std::vector<Node*> hitsToNodesWithGroupPicking(const std::vector<Hit>& hits)
{
  return kdl::col_stable_remove_duplicates(
    hits | std::views::transform([](const auto& hit) {
      return findOutermostClosedGroupOrNode(hitToNode(hit));
    })
    | kdl::ranges::to<std::vector>());
}

const Node* findOutermostClosedGroupOrNode(const Node* node)
{
  return findOutermostClosedGroupOrNode(const_cast<Node*>(node));
}

std::vector<GroupNode*> collectGroups(const std::vector<Node*>& nodes)
{
  auto result = std::vector<GroupNode*>{};
  Node::visitAll(
    nodes,
    kdl::overload(
      [](auto&& thisLambda, const WorldNode& worldNode) {
        worldNode.visitChildren(thisLambda);
      },
      [](auto&& thisLambda, const LayerNode& layerNode) {
        layerNode.visitChildren(thisLambda);
      },
      [&](auto&& thisLambda, GroupNode& groupNode) {
        result.push_back(&groupNode);
        groupNode.visitChildren(thisLambda);
      },
      [](const EntityNode&) {},
      [](const BrushNode&) {},
      [](const PatchNode&) {}));
  return result;
}


std::vector<GroupNode*> collectContainingGroups(const std::vector<Node*>& nodes)
{
  auto result = std::vector<GroupNode*>{};
  Node::visitAll(
    nodes,
    kdl::overload(
      [](const WorldNode&) {},
      [](const LayerNode&) {},
      [&](GroupNode& groupNode) {
        if (auto* containingGroupNode = groupNode.containingGroup())
        {
          result.push_back(containingGroupNode);
        }
      },
      [&](EntityNode& entityNode) {
        if (auto* containingGroupNode = entityNode.containingGroup())
        {
          result.push_back(containingGroupNode);
        }
      },
      [&](BrushNode& brushNode) {
        if (auto* containingGroupNode = brushNode.containingGroup())
        {
          result.push_back(containingGroupNode);
        }
      },
      [&](PatchNode& patchNode) {
        if (auto* containingGroupNode = patchNode.containingGroup())
        {
          result.push_back(containingGroupNode);
        }
      }));

  return kdl::vec_sort_and_remove_duplicates(std::move(result));
}

kdl::flat_map<Node*, std::vector<Node*>> parentChildrenMap(
  const std::vector<Node*>& nodes)
{
  auto result = kdl::flat_map<Node*, std::vector<Node*>>{};

  for (auto* node : nodes)
  {
    auto* parent = node->parent();
    contract_assert(parent != nullptr);

    result[parent].push_back(node);
  }

  return result;
}

/**
 * Recursively collect brushes and entities from the given vector of node trees such that
 * the returned nodes match the given predicate. A matching brush is only returned if it
 * isn't in the given vector brushes. A node matches the given predicate if there is a
 * brush in the given vector of brushes such that the predicate evaluates to true for that
 * pair of node and brush.
 *
 * The given predicate must be a function that maps a node and a brush to true or false.
 */
template <typename P>
static std::vector<Node*> collectMatchingNodes(
  const std::vector<Node*>& nodes,
  const std::vector<BrushNode*>& brushes,
  const P& predicate)
{
  auto result = std::vector<Node*>{};

  const auto collectIfMatching = [&](auto& node) {
    for (const auto* brush : brushes)
    {
      if (predicate(node, brush))
      {
        result.push_back(&node);
        return;
      }
    }
  };

  for (auto* node : nodes)
  {
    node->accept(kdl::overload(
      [](
        auto&& thisLambda, WorldNode& worldNode) { worldNode.visitChildren(thisLambda); },
      [](
        auto&& thisLambda, LayerNode& layerNode) { layerNode.visitChildren(thisLambda); },
      [&](auto&& thisLambda, GroupNode& groupNode) {
        if (groupNode.opened() || groupNode.hasOpenedDescendant())
        {
          groupNode.visitChildren(thisLambda);
        }
        else
        {
          collectIfMatching(groupNode);
        }
      },
      [&](auto&& thisLambda, EntityNode& entityNode) {
        if (entityNode.hasChildren())
        {
          entityNode.visitChildren(thisLambda);
        }
        else
        {
          collectIfMatching(entityNode);
        }
      },
      [&](BrushNode& brushNode) {
        // if `brush` is one of the search query nodes, don't count it as touching
        if (!kdl::ranges::contains(brushes, &brushNode))
        {
          collectIfMatching(brushNode);
        }
      },
      [&](PatchNode& patchNode) {
        // if `patch` is one of the search query nodes, don't count it as touching
        collectIfMatching(patchNode);
      }));
  }

  return result;
}

std::vector<Node*> collectTouchingNodes(
  const std::vector<Node*>& nodes, const std::vector<BrushNode*>& brushNodes)
{
  return collectMatchingNodes(
    nodes, brushNodes, [](const auto& node, const auto& brushNode) {
      return brushNode->intersects(node);
    });
}

std::vector<Node*> collectContainedNodes(
  const std::vector<Node*>& nodes, const std::vector<BrushNode*>& brushNodes)
{
  return collectMatchingNodes(
    nodes, brushNodes, [](const auto& node, const auto& brushNode) {
      return brushNode->contains(node);
    });
}

std::vector<Node*> collectSelectedNodes(const std::vector<Node*>& nodes)
{
  return collectNodesAndDescendants(
    nodes, [](const auto& node) { return node.selected(); });
}

std::vector<Node*> collectSelectableNodes(
  const std::vector<Node*>& nodes, const EditorContext& editorContext)
{
  auto result = std::vector<Node*>{};

  for (auto* node : nodes)
  {
    node->accept(kdl::overload(
      [&](
        auto&& thisLambda, WorldNode& worldNode) { worldNode.visitChildren(thisLambda); },
      [&](
        auto&& thisLambda, LayerNode& layerNode) { layerNode.visitChildren(thisLambda); },
      [&](auto&& thisLambda, GroupNode& groupNode) {
        if (editorContext.selectable(groupNode))
        {
          // implies that any containing group is opened and that group itself is closed
          // therefore we don't need to visit the group's children
          result.push_back(&groupNode);
        }
        else
        {
          groupNode.visitChildren(thisLambda);
        }
      },
      [&](auto&& thisLambda, EntityNode& entityNode) {
        if (editorContext.selectable(entityNode))
        {
          result.push_back(&entityNode);
        }
        entityNode.visitChildren(thisLambda);
      },
      [&](BrushNode& brushNode) {
        if (editorContext.selectable(brushNode))
        {
          result.push_back(&brushNode);
        }
      },
      [&](PatchNode& patchNode) {
        if (editorContext.selectable(patchNode))
        {
          result.push_back(&patchNode);
        }
      }));
  }

  return result;
}

std::vector<BrushFaceHandle> collectSelectedBrushFaces(const std::vector<Node*>& nodes)
{
  return collectBrushFaces(nodes, [](const BrushNode&, const BrushFace& brushFace) {
    return brushFace.selected();
  });
}

std::vector<BrushFaceHandle> collectSelectableBrushFaces(
  const std::vector<Node*>& nodes, const EditorContext& editorContext)
{
  return collectBrushFaces(
    nodes, [&](const BrushNode& brushNode, const BrushFace& brushFace) {
      return editorContext.selectable(brushNode, brushFace);
    });
}

std::vector<BrushFaceHandle> collectConnectedCoplanarFaces(
  const BrushFaceHandle& startFace,
  const EditorContext& editorContext,
  const NodeTree& nodeTree)
{
  constexpr auto epsilon = vm::constants<double>::almost_zero();
  const auto& startPlane = startFace.face().boundary();

  struct Candidate
  {
    BrushFaceHandle handle;
    std::vector<vm::segment3d> edges;
    vm::bbox3d bounds;
  };

  const auto makeCandidate = [&](const BrushFaceHandle& handle) {
    const auto vertices = handle.face().vertexPositions();
    contract_pre(!vertices.empty());

    const auto edges =
      handle.face().edges()
      | std::views::transform([](const auto* edge) { return edge->segment(); });

    return Candidate{
      handle,
      edges | kdl::ranges::to<std::vector>(),
      vm::bbox3d::build(vertices)->expand(epsilon),
    };
  };

  const auto facesShareEdgeSegment = [](const auto& lhs, const auto& rhs) {
    const auto allEdgePairs = kdl::views::cartesian_product(lhs, rhs);
    return std::ranges::any_of(allEdgePairs, [](const auto& edgePair) {
      const auto& [lhsEdge, rhsEdge] = edgePair;
      return vm::segments_overlap(lhsEdge, rhsEdge, vm::Cd::almost_zero());
    });
  };

  // Cache coplanar+selectable faces per brush node so a node hit from multiple flood
  // directions has its face list computed only once. Querying the node tree keeps the
  // flood local instead of scanning the whole map, and dropping unselectable faces stops
  // hidden or locked brushes from bridging the region.
  auto nodeCache = std::unordered_map<Node*, std::vector<BrushFaceHandle>>{};

  const auto coplanarFacesOf = [&](Node* node) -> const std::vector<BrushFaceHandle>& {
    auto [it, inserted] = nodeCache.emplace(node, std::vector<BrushFaceHandle>{});
    if (inserted)
    {
      it->second = collectSelectableBrushFaces(std::vector<Node*>{node}, editorContext)
                   | std::views::filter([&](const auto& handle) {
                       return handle.face().coplanarWith(startPlane);
                     })
                   | kdl::ranges::to<std::vector>();
    }
    return it->second;
  };

  const auto coplanarFacesNear = [&](const vm::bbox3d& bounds) {
    auto result = std::vector<BrushFaceHandle>{};
    for (auto* node : nodeTree.find_intersectors(bounds))
    {
      kdl::vec_append(result, coplanarFacesOf(node));
    }
    return result;
  };

  // Flood out from the start face, re-querying the tree around each face we reach so a
  // long row of brushes is followed without ever visiting the rest of the map.
  auto region = std::vector<BrushFaceHandle>{};
  auto visited = kdl::flat_set<BrushFaceHandle>{startFace};
  auto pending = std::vector<Candidate>{};
  pending.push_back(makeCandidate(startFace));

  while (!pending.empty())
  {
    const auto current = kdl::vec_pop_back(pending);
    region.push_back(current.handle);

    for (const auto& handle : coplanarFacesNear(current.bounds))
    {
      if (visited.count(handle) == 0)
      {
        auto candidate = makeCandidate(handle);
        if (
          current.bounds.intersects(candidate.bounds)
          && facesShareEdgeSegment(current.edges, candidate.edges))
        {
          visited.insert(handle);
          pending.push_back(std::move(candidate));
        }
      }
    }
  }

  return region;
}

std::vector<BrushFaceHandle> collectSmartFaceSelection(
  const std::vector<BrushFaceHandle>& seedFaces,
  const EditorContext& editorContext,
  const NodeTree& nodeTree,
  const SmartFaceSelectionOptions& options)
{
  if (seedFaces.empty())
  {
    return {};
  }

  constexpr auto epsilon = vm::constants<double>::almost_zero();
  const auto gapTolerance = std::max(0.0, options.gapTolerance);
  const auto maxAngle = std::clamp(options.angleTolerance, 0.0, 180.0);
  const auto minNormalDot = std::cos(vm::to_radians(maxAngle));

  struct Candidate
  {
    BrushFaceHandle handle;
    vm::vec3d normal;
    vm::vec3d center;
    std::vector<vm::segment3d> edges;
    vm::bbox3d bounds;
  };

  const auto makeCandidate = [&](const BrushFaceHandle& handle) {
    const auto vertices = handle.face().vertexPositions();
    const auto edges =
      handle.face().edges()
      | std::views::transform([](const auto* edge) { return edge->segment(); });

    const auto bounds = vm::bbox3d::build(vertices);

    return Candidate{
      handle,
      handle.face().normal(),
      handle.face().center(),
      edges | kdl::ranges::to<std::vector>(),
      bounds.value_or(vm::bbox3d{}).expand(gapTolerance + epsilon),
    };
  };

  const auto edgesWithinTolerance = [&](const auto& lhs, const auto& rhs) {
    const auto maxDistanceSquared = gapTolerance * gapTolerance;
    const auto allEdgePairs = kdl::views::cartesian_product(lhs, rhs);
    return std::ranges::any_of(allEdgePairs, [&](const auto& edgePair) {
      const auto& [lhsEdge, rhsEdge] = edgePair;
      if (vm::segments_overlap(lhsEdge, rhsEdge, epsilon))
      {
        return true;
      }
      if (gapTolerance <= 0.0)
      {
        return false;
      }

      return vm::squared_distance(lhsEdge, rhsEdge.start()).distance <= maxDistanceSquared
             || vm::squared_distance(lhsEdge, rhsEdge.end()).distance
                  <= maxDistanceSquared
             || vm::squared_distance(rhsEdge, lhsEdge.start()).distance
                  <= maxDistanceSquared
             || vm::squared_distance(rhsEdge, lhsEdge.end()).distance
                  <= maxDistanceSquared;
    });
  };

  const auto seedNormals =
    seedFaces
    | std::views::transform([](const auto& handle) { return handle.face().normal(); })
    | kdl::ranges::to<std::vector>();

  const auto normalMatches = [&](const Candidate& current, const Candidate& candidate) {
    if (options.mode == SmartFaceSelectionMode::FaceStrip)
    {
      return vm::dot(current.normal, candidate.normal) >= minNormalDot;
    }
    return std::ranges::any_of(seedNormals, [&](const auto& seedNormal) {
      return vm::dot(seedNormal, candidate.normal) >= minNormalDot;
    });
  };

  const auto materialMatches = [&](const Candidate& candidate) {
    return !options.sameMaterial
           || std::ranges::any_of(seedFaces, [&](const auto& seedFace) {
                return kdl::ci::str_is_equal(
                  seedFace.face().materialName(), candidate.handle.face().materialName());
              });
  };

  auto nodeCache = std::unordered_map<Node*, std::vector<BrushFaceHandle>>{};
  const auto selectableFacesOf = [&](Node* node) -> const std::vector<BrushFaceHandle>& {
    auto [it, inserted] = nodeCache.emplace(node, std::vector<BrushFaceHandle>{});
    if (inserted)
    {
      it->second = collectSelectableBrushFaces(std::vector<Node*>{node}, editorContext);
    }
    return it->second;
  };

  const auto findNeighbors = [&](const Candidate& current) {
    auto neighbors = std::vector<Candidate>{};
    for (auto* node : nodeTree.find_intersectors(current.bounds))
    {
      for (const auto& handle : selectableFacesOf(node))
      {
        if (handle == current.handle)
        {
          continue;
        }

        auto candidate = makeCandidate(handle);
        if (
          current.bounds.intersects(candidate.bounds) && normalMatches(current, candidate)
          && edgesWithinTolerance(current.edges, candidate.edges)
          && materialMatches(candidate))
        {
          neighbors.push_back(std::move(candidate));
        }
      }
    }
    return neighbors;
  };

  auto result = std::vector<BrushFaceHandle>{};
  auto visited = kdl::flat_set<BrushFaceHandle>{};
  auto seeds = std::vector<Candidate>{};
  for (const auto& seedFace : seedFaces)
  {
    if (visited.insert(seedFace).second)
    {
      result.push_back(seedFace);
      seeds.push_back(makeCandidate(seedFace));
    }
  }

  struct DirectionalCandidate
  {
    Candidate candidate;
    vm::vec3d direction;
  };

  const auto followSeedDirection = options.followSeedDirection && seeds.size() >= 2u;
  if (followSeedDirection)
  {
    auto firstSeedIndex = size_t{0u};
    auto secondSeedIndex = size_t{1u};
    auto maxDistanceSquared = 0.0;
    for (auto i = size_t{0u}; i < seeds.size(); ++i)
    {
      for (auto j = i + 1u; j < seeds.size(); ++j)
      {
        const auto distanceSquared =
          vm::squared_length(seeds[i].center - seeds[j].center);
        if (distanceSquared > maxDistanceSquared)
        {
          firstSeedIndex = i;
          secondSeedIndex = j;
          maxDistanceSquared = distanceSquared;
        }
      }
    }

    if (maxDistanceSquared <= epsilon * epsilon)
    {
      return result;
    }

    const auto outwardDirection = [&](
                                    const size_t endpointIndex,
                                    const size_t fallbackIndex) {
      auto nearestIndex = fallbackIndex;
      auto nearestDistanceSquared =
        vm::squared_length(seeds[endpointIndex].center - seeds[fallbackIndex].center);
      for (auto i = size_t{0u}; i < seeds.size(); ++i)
      {
        if (i == endpointIndex)
        {
          continue;
        }

        const auto distanceSquared =
          vm::squared_length(seeds[endpointIndex].center - seeds[i].center);
        if (
          distanceSquared > epsilon * epsilon && distanceSquared < nearestDistanceSquared)
        {
          nearestIndex = i;
          nearestDistanceSquared = distanceSquared;
        }
      }
      return vm::normalize(seeds[endpointIndex].center - seeds[nearestIndex].center);
    };

    const auto firstDirection = outwardDirection(firstSeedIndex, secondSeedIndex);
    const auto secondDirection = outwardDirection(secondSeedIndex, firstSeedIndex);
    auto pending = std::vector<DirectionalCandidate>{
      {seeds[firstSeedIndex], firstDirection}, {seeds[secondSeedIndex], secondDirection}};
    const auto minDirectionDot = std::cos(vm::to_radians(maxAngle));

    while (!pending.empty())
    {
      auto current = kdl::vec_pop_back(pending);
      auto continuations = std::vector<DirectionalCandidate>{};
      for (auto& candidate : findNeighbors(current.candidate))
      {
        if (visited.count(candidate.handle) != 0)
        {
          continue;
        }

        const auto delta = candidate.center - current.candidate.center;
        if (vm::is_zero(delta, epsilon))
        {
          continue;
        }

        const auto direction = vm::normalize(delta);
        if (vm::dot(current.direction, direction) + epsilon >= minDirectionDot)
        {
          continuations.push_back({std::move(candidate), direction});
        }
      }

      if (options.stopAtBranches && continuations.size() > 1u)
      {
        continue;
      }

      for (auto& continuation : continuations)
      {
        if (visited.insert(continuation.candidate.handle).second)
        {
          result.push_back(continuation.candidate.handle);
          pending.push_back(std::move(continuation));
        }
      }
    }

    return result;
  }

  auto pending = std::move(seeds);
  while (!pending.empty())
  {
    const auto current = kdl::vec_pop_back(pending);
    auto neighbors = findNeighbors(current);
    if (options.stopAtBranches && neighbors.size() > 2u)
    {
      continue;
    }

    for (auto& candidate : neighbors)
    {
      if (visited.insert(candidate.handle).second)
      {
        result.push_back(candidate.handle);
        pending.push_back(std::move(candidate));
      }
    }
  }

  return result;
}

vm::bbox3d computeLogicalBounds(
  const std::vector<Node*>& nodes, const vm::bbox3d& defaultBounds)
{
  auto bounds = std::vector<vm::bbox3d>{};
  Node::visitAll(
    nodes,
    kdl::overload(
      [](const WorldNode&) {},
      [](const LayerNode&) {},
      [&](const GroupNode& groupNode) { bounds.push_back(groupNode.logicalBounds()); },
      [&](const EntityNode& entityNode) { bounds.push_back(entityNode.logicalBounds()); },
      [&](const BrushNode& brushNode) { bounds.push_back(brushNode.logicalBounds()); },
      [&](const PatchNode& patchNode) { bounds.push_back(patchNode.logicalBounds()); }));
  return vm::bbox3d::build(bounds).value_or(defaultBounds);
}

vm::bbox3d computePhysicalBounds(
  const std::vector<Node*>& nodes, const vm::bbox3d& defaultBounds)
{
  auto bounds = std::vector<vm::bbox3d>{};
  Node::visitAll(
    nodes,
    kdl::overload(
      [](const WorldNode&) {},
      [](const LayerNode&) {},
      [&](const GroupNode& groupNode) { bounds.push_back(groupNode.physicalBounds()); },
      [&](const EntityNode& entityNode) {
        bounds.push_back(entityNode.physicalBounds());
      },
      [&](const BrushNode& brushNode) { bounds.push_back(brushNode.physicalBounds()); },
      [&](const PatchNode& patchNode) { bounds.push_back(patchNode.physicalBounds()); }));
  return vm::bbox3d::build(bounds).value_or(defaultBounds);
}

std::vector<BrushNode*> filterBrushNodes(const std::vector<Node*>& nodes)
{
  auto result = std::vector<BrushNode*>{};
  result.reserve(nodes.size());
  for (Node* node : nodes)
  {
    node->accept(kdl::overload(
      [](WorldNode&) {},
      [](LayerNode&) {},
      [](GroupNode&) {},
      [](EntityNode&) {},
      [&](BrushNode& brushNode) { result.push_back(&brushNode); },
      [](PatchNode&) {}));
  }
  return result;
}

std::vector<EntityNode*> filterEntityNodes(const std::vector<Node*>& nodes)
{
  auto result = std::vector<EntityNode*>{};
  result.reserve(nodes.size());
  for (Node* node : nodes)
  {
    node->accept(kdl::overload(
      [](WorldNode&) {},
      [](LayerNode&) {},
      [](GroupNode&) {},
      [&](EntityNode& entityNode) { result.push_back(&entityNode); },
      [](BrushNode&) {},
      [](PatchNode&) {}));
  }
  return result;
}

} // namespace tb::mdl
