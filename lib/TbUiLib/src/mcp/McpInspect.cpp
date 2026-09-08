#include <QJsonArray>

#include "McpThinBridgeTools.h"
#include "mdl/Map.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/automation/AutomationValidation.h"

namespace tb::ui
{
QJsonObject selectionJson(AppController& appController)
{
  auto* window = appController.mapWindowManager().topMapWindow();
  if (window == nullptr)
  {
    return QJsonObject{{"hasSelection", false}, {"nodeCount", 0}};
  }
  const auto& selection = window->document().map().selection();
  return QJsonObject{{"hasSelection", selection.hasAny()},
                     {"nodeCount", static_cast<int>(selection.nodes.size())},
                     {"groupCount", static_cast<int>(selection.groups.size())},
                     {"entityCount", static_cast<int>(selection.entities.size())},
                     {"brushCount", static_cast<int>(selection.brushes.size())},
                     {"patchCount", static_cast<int>(selection.patches.size())}};
}

McpBridgeToolResult problemsCheckResult(AppController& appController, const QJsonObject& params)
{
  auto* window = appController.mapWindowManager().topMapWindow();
  if (window == nullptr)
  {
    return noActiveDocumentFailure();
  }
  const auto issues = collectAutomationValidationIssues(
    window->document().map(), params.value("includeHidden").toBool(false));
  auto summary = QJsonArray{};
  for (const auto& issue : issues)
  {
    summary.push_back(QJsonObject{{"id", QString::fromStdString(issue.id)},
                                  {"message", QString::fromStdString(issue.message)},
                                  {"objectType", QString::fromStdString(issue.objectType)},
                                  {"hidden", issue.hidden}});
  }
  return McpBridgeToolResult::success(
    QJsonObject{{"count", summary.size()}, {"issues", summary}, {"mutatedDocument", false}});
}
} // namespace tb::ui
