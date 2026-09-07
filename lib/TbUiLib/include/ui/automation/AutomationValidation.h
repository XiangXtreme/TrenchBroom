/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 */

#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tb::mdl
{
class Issue;
class Map;
} // namespace tb::mdl

namespace tb::ui
{

/**
 * A validation snapshot for one map issue. source is valid only until the map
 * changes and exists solely for native callers that immediately apply a fix.
 * Automation clients consume the copied fields.
 */
struct AutomationValidationIssue
{
  const mdl::Issue* source = nullptr;
  std::string id;
  std::string stableKey;
  int type = 0;
  std::string message;
  std::string objectId;
  std::string objectType;
  size_t lineNumber = 0u;
  bool hidden = false;
  std::array<double, 3u> boundsMin{};
  std::array<double, 3u> boundsMax{};
  std::optional<size_t> faceIndex;
  std::optional<std::string> propertyKey;
  std::vector<std::string> safeQuickFixes;
};

/**
 * Runs the registered map validators and returns copied, compact issue facts.
 * This service is deliberately independent of MCP and Python serialization.
 */
std::vector<AutomationValidationIssue> collectAutomationValidationIssues(
  mdl::Map& map, bool includeHidden);

bool isAutomationSafeQuickFixDescription(std::string_view description);

} // namespace tb::ui
