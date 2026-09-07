/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.
 */

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <vector>

namespace tb::mdl
{
class Map;
class Node;
} // namespace tb::mdl

namespace tb::ui::automation
{

/** Protocol-neutral options controlling a single IR map mutation. */
struct AutomationIrApplyOptions
{
  QString transactionName;
};

/**
 * Facts produced by a successful IR application. Nodes remain owned by the
 * map once this result is returned and are suitable for adapter-specific
 * handles or identity registration.
 */
struct AutomationIrApplyResult
{
  bool ok = false;
  QString error;
  QJsonObject ir;
  QJsonArray warnings;
  QJsonObject preview;
  std::vector<mdl::Node*> createdNodes;
  int brushCount = 0;
  int entityCount = 0;
};

/**
 * Validates, compiles, and atomically applies the native IR primitives.
 *
 * This service is intentionally independent of MCP operation history,
 * protocol object IDs, and Python runtime state. Adapters publish those
 * concerns only after this function has committed successfully.
 */
AutomationIrApplyResult applyAutomationIr(
  mdl::Map& map,
  const QJsonObject& request,
  const AutomationIrApplyOptions& options = {});

} // namespace tb::ui::automation
