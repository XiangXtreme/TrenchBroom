/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 */

#include "ui/automation/AutomationValidation.h"

#include "mdl/BrushNode.h"
#include "mdl/EntityNode.h"
#include "mdl/GroupNode.h"
#include "mdl/Issue.h"
#include "mdl/IssueQuickFix.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/Node.h"
#include "mdl/PatchNode.h"
#include "mdl/WorldNode.h"

#include "kd/overload.h"

#include <string_view>
#include <unordered_set>

namespace tb::ui
{
namespace
{

std::string nodePathId(const mdl::Node& node, const mdl::WorldNode& worldNode)
{
  if (&node == &worldNode)
  {
    return "node:world";
  }

  auto result = std::string{"node:"};
  auto first = true;
  for (const auto index : node.pathFrom(worldNode).indices)
  {
    if (!first)
    {
      result += '/';
    }
    result += std::to_string(index);
    first = false;
  }
  return result;
}

std::string nodeTypeName(const mdl::Node& node)
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

std::string issueId(const mdl::Issue& issue, const mdl::WorldNode& worldNode)
{
  auto id = "issue:" + std::to_string(issue.type()) + ':'
            + nodePathId(issue.node(), worldNode) + ':'
            + std::to_string(issue.lineNumber());
  if (const auto* faceIssue = dynamic_cast<const mdl::BrushFaceIssue*>(&issue))
  {
    id += ":face:" + std::to_string(faceIssue->faceIndex());
  }
  if (const auto* propertyIssue = dynamic_cast<const mdl::EntityPropertyIssue*>(&issue))
  {
    id += ":property:" + propertyIssue->propertyKey();
  }
  return id;
}

std::string issueStableKey(const mdl::Issue& issue, const mdl::WorldNode& worldNode)
{
  auto key =
    "issue:" + std::to_string(issue.type()) + ':' + nodePathId(issue.node(), worldNode);
  if (const auto* faceIssue = dynamic_cast<const mdl::BrushFaceIssue*>(&issue))
  {
    key += ":face:" + std::to_string(faceIssue->faceIndex());
  }
  if (const auto* propertyIssue = dynamic_cast<const mdl::EntityPropertyIssue*>(&issue))
  {
    key += ":property:" + propertyIssue->propertyKey();
  }
  return key;
}

bool isSafeQuickFixDescription(const std::string_view description)
{
  static const auto safeDescriptions = std::unordered_set<std::string>{
    "Delete Property",
    "Remove Mod",
    "Replace \" with '",
    "Reset UV Scale",
    "Snap Vertices",
    "Truncate Property Values",
  };
  return safeDescriptions.contains(std::string{description});
}

std::vector<std::string> safeQuickFixDescriptions(
  const mdl::WorldNode& worldNode, const mdl::Issue& issue)
{
  auto result = std::vector<std::string>{};
  for (const auto* quickFix : worldNode.quickFixes(issue.type()))
  {
    if (isSafeQuickFixDescription(quickFix->description()))
    {
      result.push_back(quickFix->description());
    }
  }
  return result;
}

AutomationValidationIssue makeIssue(
  const mdl::Issue& issue, const mdl::WorldNode& worldNode)
{
  const auto& bounds = issue.node().logicalBounds();
  auto result = AutomationValidationIssue{
    .source = &issue,
    .id = issueId(issue, worldNode),
    .stableKey = issueStableKey(issue, worldNode),
    .type = issue.type(),
    .message = issue.description(),
    .objectId = nodePathId(issue.node(), worldNode),
    .objectType = nodeTypeName(issue.node()),
    .lineNumber = issue.lineNumber(),
    .hidden = issue.hidden(),
    .boundsMin = {bounds.min.x(), bounds.min.y(), bounds.min.z()},
    .boundsMax = {bounds.max.x(), bounds.max.y(), bounds.max.z()},
    .safeQuickFixes = safeQuickFixDescriptions(worldNode, issue),
  };
  if (const auto* faceIssue = dynamic_cast<const mdl::BrushFaceIssue*>(&issue))
  {
    result.faceIndex = faceIssue->faceIndex();
  }
  if (const auto* propertyIssue = dynamic_cast<const mdl::EntityPropertyIssue*>(&issue))
  {
    result.propertyKey = propertyIssue->propertyKey();
  }
  return result;
}

} // namespace

bool isAutomationSafeQuickFixDescription(const std::string_view description)
{
  return isSafeQuickFixDescription(description);
}

std::vector<AutomationValidationIssue> collectAutomationValidationIssues(
  mdl::Map& map, const bool includeHidden)
{
  const auto validators = map.worldNode().registeredValidators();
  auto result = std::vector<AutomationValidationIssue>{};
  const auto collectIssues = [&](auto& node) {
    for (const auto* issue : node.issues(validators))
    {
      if (includeHidden || !issue->hidden())
      {
        result.push_back(makeIssue(*issue, map.worldNode()));
      }
    }
  };

  map.worldNode().accept(kdl::overload(
    [&](auto&& thisLambda, mdl::WorldNode& worldNode) {
      collectIssues(worldNode);
      worldNode.visitChildren(thisLambda);
    },
    [&](auto&& thisLambda, mdl::LayerNode& layerNode) {
      collectIssues(layerNode);
      layerNode.visitChildren(thisLambda);
    },
    [&](auto&& thisLambda, mdl::GroupNode& groupNode) {
      collectIssues(groupNode);
      groupNode.visitChildren(thisLambda);
    },
    [&](auto&& thisLambda, mdl::EntityNode& entityNode) {
      collectIssues(entityNode);
      entityNode.visitChildren(thisLambda);
    },
    [&](mdl::BrushNode& brushNode) { collectIssues(brushNode); },
    [&](mdl::PatchNode& patchNode) { collectIssues(patchNode); }));

  return result;
}

} // namespace tb::ui
