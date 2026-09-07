#pragma once

#include "ui/automation/AutomationStateRecords.h"

#include <filesystem>
#include <map>
#include <string>

namespace tb::ui
{
class AppController;
class MapDocument;
class MapViewBase;
class MapWindow;
namespace automation
{
class AutomationObjectRegistry;
}
} // namespace tb::ui

namespace tb
{
class Logger;
} // namespace tb

namespace tb::ui
{

struct PythonExecutionContext
{
  MapWindow* mapWindow = nullptr;
  MapDocument* document = nullptr;
  AppController* appController = nullptr;
  MapViewBase* currentMapView = nullptr;
  Logger* logger = nullptr;
  std::filesystem::path scriptPath;
  std::filesystem::path pluginDirectory;
  std::string pluginId;
  automation::AutomationObjectRegistry* objectRegistry = nullptr;
  std::map<QString, automation::AutomationModuleRecord>* moduleStore = nullptr;
  bool mcpExecution = false;
  bool allowNonTransactionalActions = true;
  bool allowPersistentUi = true;
};

} // namespace tb::ui
