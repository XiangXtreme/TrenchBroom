/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.
 */

#include "ui/automation/AutomationActions.h"

#include "ui/Action.h"
#include "ui/ActionExecutionContext.h"
#include "ui/ActionManager.h"
#include "ui/AppController.h"

#include <filesystem>

namespace tb::ui::automation
{

std::vector<std::string> automationActionIds()
{
  const auto& actions = ActionManager::instance().actionsMap();
  auto result = std::vector<std::string>{};
  result.reserve(actions.size());
  for (const auto& [path, action] : actions)
  {
    unused(action);
    result.push_back(path.generic_string());
  }
  return result;
}

AutomationActionResult executeAutomationAction(
  AppController& appController,
  MapWindow* const mapWindow,
  MapViewBase* const mapView,
  const std::string& actionId)
{
  const auto path = std::filesystem::path{actionId};
  const auto& actions = appController.actionManager().actionsMap();
  const auto actionIt = actions.find(path);
  if (actionIt == std::end(actions))
  {
    return {AutomationActionStatus::Unknown, actionId};
  }

  auto context = ActionExecutionContext{appController, mapWindow, mapView};
  if (!actionIt->second.enabled(context))
  {
    return {AutomationActionStatus::Disabled, actionId};
  }
  actionIt->second.execute(context);
  return {AutomationActionStatus::Executed, actionId};
}

} // namespace tb::ui::automation
