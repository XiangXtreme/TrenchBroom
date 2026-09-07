/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.
 */

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "vm/bbox.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace tb::mdl
{
class BrushNode;
class EntityNode;
class Map;
} // namespace tb::mdl

namespace tb::ui::automation
{

struct AutomationPointEntitySpec
{
  std::string classname;
  std::map<std::string, std::string> properties;
  vm::vec3d origin = vm::vec3d{0.0, 0.0, 0.0};
};

struct AutomationPointEntityBuildResult
{
  std::vector<mdl::EntityNode*> nodes;
  std::vector<std::string> classnames;
  int removedEmptyPropertyCount = 0;
  int failedIndex = -1;
  QString error;
};

struct AutomationBrushEntityResult
{
  mdl::EntityNode* entity = nullptr;
  std::vector<mdl::BrushNode*> brushes;
  QString error;
};

/**
 * Validates FGD point entity definitions and creates unattached entity nodes.
 * The caller owns all returned nodes until it passes them to addNodes.
 */
AutomationPointEntityBuildResult buildCheckedPointEntities(
  const mdl::Map& map, const std::vector<AutomationPointEntitySpec>& entities);

void deletePointEntityNodes(std::vector<mdl::EntityNode*>& nodes);

/** Compact protocol-neutral summaries for querying the active FGD definitions. */
QJsonArray listEntityDefinitionSummaries(
  const mdl::Map& map, const QString& type, const QString& query, size_t limit);

/** Returns the full FGD schema for an active entity classname, if present. */
std::optional<QJsonObject> entityDefinitionSchema(const mdl::Map& map, const QString& classname);

/**
 * Ties the supplied live brushes to an FGD brush entity through the native map command.
 * The caller must resolve and validate document-bound node handles before this boundary.
 */
AutomationBrushEntityResult tieBrushesToEntity(
  mdl::Map& map, const std::string& classname, std::vector<mdl::BrushNode*> brushes);

/**
 * Moves brush-entity brushes back to their native parent through the native map command.
 */
AutomationBrushEntityResult untieBrushesFromEntity(
  mdl::Map& map, std::vector<mdl::BrushNode*> brushes);

} // namespace tb::ui::automation
