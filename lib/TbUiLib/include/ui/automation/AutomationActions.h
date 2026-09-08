/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.
 */

#pragma once

#include <string>
#include <vector>

namespace tb::ui
{
class AppController;
class MapViewBase;
class MapWindow;
} // namespace tb::ui

namespace tb::ui::automation
{

enum class AutomationActionStatus
{
  Executed,
  Unknown,
  Disabled,
};

struct AutomationActionResult
{
  AutomationActionStatus status = AutomationActionStatus::Unknown;
  std::string actionId;
};

std::vector<std::string> automationActionIds();
AutomationActionResult executeAutomationAction(
  AppController& appController,
  MapWindow* mapWindow,
  MapViewBase* mapView,
  const std::string& actionId);

} // namespace tb::ui::automation
