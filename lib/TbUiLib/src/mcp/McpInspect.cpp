#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

#include "McpThinBridgeTools.h"
#include "mdl/Map.h"
#include "mdl/Validator.h"
#include "mdl/WorldNode.h"
#include "ui/AppController.h"
#include "ui/IssueBrowserView.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/automation/AutomationValidation.h"

#include <cmath>
#include <limits>
#include <map>

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
  return QJsonObject{
    {"hasSelection", selection.hasAny()},
    {"nodeCount", static_cast<int>(selection.nodes.size())},
    {"groupCount", static_cast<int>(selection.groups.size())},
    {"entityCount", static_cast<int>(selection.entities.size())},
    {"brushCount", static_cast<int>(selection.brushes.size())},
    {"patchCount", static_cast<int>(selection.patches.size())}};
}

McpBridgeToolResult problemsCheckResult(
  AppController& appController, const QJsonObject& params)
{
  auto* window = appController.mapWindowManager().topMapWindow();
  if (window == nullptr)
  {
    return noActiveDocumentFailure();
  }
  const auto integer = [&](const char* key, const int fallback, const int maximum) {
    const auto value = params.value(key);
    if (value.isUndefined())
      return fallback;
    const auto number = value.toDouble(-1);
    return value.isDouble() && std::isfinite(number) && number >= 0 && number <= maximum
               && std::floor(number) == number
             ? static_cast<int>(number)
             : -1;
  };
  const auto limit = integer("limit", 20, 100);
  const auto offset = integer("offset", 0, std::numeric_limits<int>::max());
  const auto detail = params.value("detail").toString("summary");
  if (
    limit < 1 || offset < 0 || (detail != "summary" && detail != "issues")
    || (params.contains("detail") && !params.value("detail").isString())
    || (params.contains("includeHidden") && !params.value("includeHidden").isBool()))
  {
    return invalidParamsFailure(
      "problems requires detail summary|issues, integer limit 1..100, offset >= 0, "
      "and boolean includeHidden");
  }

  auto& map = window->document().map();
  auto names = std::map<int, QString>{};
  auto knownNames = QSet<QString>{};
  for (const auto* validator : map.worldNode().registeredValidators())
  {
    const auto name = QString::fromStdString(validator->description());
    names.emplace(validator->type(), name);
    knownNames.insert(name);
  }
  auto ignoredTypes = QSet<QString>{};
  auto onlyTypes = QSet<QString>{};
  const auto readTypes = [&](const char* key, QSet<QString>& target) {
    const auto value = params.value(key);
    if (value.isUndefined())
      return true;
    if (!value.isArray())
      return false;
    for (const auto& item : value.toArray())
    {
      if (!item.isString() || !knownNames.contains(item.toString()))
        return false;
      target.insert(item.toString());
    }
    return true;
  };
  if (!readTypes("ignoreTypes", ignoredTypes) || !readTypes("types", onlyTypes))
  {
    return invalidParamsFailure(
      "types and ignoreTypes must be arrays of exact validator names from the types "
      "summary (for example, Non-integer vertices)");
  }

  const auto includeHidden = params.value("includeHidden").toBool(false);
  const auto* browser = window->findChild<IssueBrowserView*>();
  const auto hiddenTypes = browser ? browser->hiddenIssueTypes() : 0;
  const auto issues = collectAutomationValidationIssues(map, true);
  auto matched = std::vector<const AutomationValidationIssue*>{};
  auto counts = std::map<QString, int>{};
  auto hiddenCount = 0;
  auto ignoredCount = 0;
  auto filteredCount = 0;
  for (const auto& issue : issues)
  {
    const auto& name = names.at(issue.type);
    if (!includeHidden && (issue.hidden || (issue.type & hiddenTypes) != 0))
      ++hiddenCount;
    else if (ignoredTypes.contains(name))
      ++ignoredCount;
    else if (!onlyTypes.isEmpty() && !onlyTypes.contains(name))
      ++filteredCount;
    else
    {
      matched.push_back(&issue);
      ++counts[name];
    }
  }
  auto types = QJsonArray{};
  for (const auto& [name, count] : counts)
    types.append(QJsonObject{{"type", name}, {"count", count}});

  auto result = QJsonObject{
    {"detail", detail},
    {"count", static_cast<qint64>(matched.size())},
    {"totalCount", static_cast<qint64>(issues.size())},
    {"hiddenCount", hiddenCount},
    {"ignoredCount", ignoredCount},
    {"filteredCount", filteredCount},
    {"types", types},
    {"mutatedDocument", false},
  };
  if (detail == "summary")
    return McpBridgeToolResult::success(std::move(result));

  auto page = QJsonArray{};
  auto next = static_cast<size_t>(offset);
  auto bytes = QJsonDocument{result}.toJson(QJsonDocument::Compact).size();
  while (next < matched.size() && page.size() < limit)
  {
    const auto& issue = *matched[next];
    const auto message = QString::fromStdString(issue.message);
    auto item = QJsonObject{
      {"type", names.at(issue.type)},
      {"message", message.left(1024)},
      {"messageTruncated", message.size() > 1024},
      {"objectId", QString::fromStdString(issue.objectId)},
      {"objectType", QString::fromStdString(issue.objectType)},
      {"hidden", issue.hidden || (issue.type & hiddenTypes) != 0},
    };
    // Oversized property keys can make issue IDs unbounded. Never return a
    // shortened identifier that could be mistaken for an actionable full ID.
    if (issue.id.size() <= 1024)
      item.insert("id", QString::fromStdString(issue.id));
    else
      item.insert("idOmitted", true);
    const auto size = QJsonDocument{item}.toJson(QJsonDocument::Compact).size();
    if (bytes + size > 15 * 1024)
      break;
    page.append(item);
    bytes += size + 1;
    ++next;
  }
  const auto truncated = next < matched.size();
  result.insert("issues", page);
  result.insert("returnedCount", page.size());
  result.insert("offset", offset);
  result.insert("limit", limit);
  result.insert("truncated", truncated);
  result.insert(
    "nextOffset",
    truncated ? QJsonValue{static_cast<qint64>(next)} : QJsonValue{QJsonValue::Null});
  return McpBridgeToolResult::success(std::move(result));
}
} // namespace tb::ui
