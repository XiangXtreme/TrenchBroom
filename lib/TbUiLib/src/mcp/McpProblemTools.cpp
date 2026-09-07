/*
 Copyright (C) 2026 XiangXtreme

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

#include <QJsonArray>
#include <QJsonObject>

#include "McpBridgeServerTools.h"
#include "McpResponseUtils.h"
#include "McpToolSupport.h"
#include "mcp/McpError.h"
#include "mdl/Issue.h"
#include "mdl/IssueQuickFix.h"
#include "mdl/Map.h"
#include "mdl/Transaction.h"
#include "mdl/WorldNode.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/QPathUtils.h"
#include "ui/automation/AutomationValidation.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <set>
#include <vector>

namespace tb::ui
{
namespace mcp = tb::mcp;

namespace
{

QString makeOperationId(int& nextOperationIndex)
{
  return QString{"mcp-op-%1"}.arg(nextOperationIndex++);
}

QJsonObject mutationResultJson(
  const McpOperationRecord& operation, const QString& idsMode)
{
  auto result = QJsonObject{};
  result.insert("operationId", operation.operationId);
  result.insert("transactionName", operation.transactionName);
  result.insert("mutatedDocument", true);
  result.insert("activeDocumentPath", operation.documentPath);
  result.insert("documentFingerprint", operation.documentFingerprint);
  mcpApplyChangedObjectIdsMode(result, operation.changedObjectIdsJson(), idsMode);
  return result;
}

void mcpRecordOperation(
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  mdl::Map& map,
  const QString& toolName,
  const QString& transactionName,
  const QJsonArray& changedObjectIds,
  QJsonObject& result,
  const QString& idsMode = "count")
{
  auto operation = McpOperationRecord{};
  operation.operationId = makeOperationId(nextOperationIndex);
  operation.toolName = toolName;
  operation.transactionName = transactionName;
  operation.documentPath = map.path().empty() ? QString{} : pathAsQString(map.path());
  operation.documentFingerprint = documentFingerprintForMap(map);
  operation.setChangedObjectIds(changedObjectIds);
  result = mutationResultJson(operation, idsMode);
  appendMcpOperationRecord(history, std::move(operation));
}

bool mcpOptionalBool(
  const QJsonObject& params, const QString& key, const bool defaultValue)
{
  const auto value = params.value(key);
  return value.isBool() ? value.toBool() : defaultValue;
}

size_t optionalSize(
  const QJsonObject& params, const QString& key, const size_t defaultValue)
{
  const auto value = params.value(key);
  if (!value.isDouble())
  {
    return defaultValue;
  }
  return static_cast<size_t>(std::max(0, value.toInt()));
}

std::optional<std::vector<QString>> stringListFromJson(
  const QJsonObject& params, const QString& key, QString& error)
{
  const auto value = params.value(key);
  if (!value.isArray())
  {
    error = QString{"%1 must be an array"}.arg(key);
    return std::nullopt;
  }

  auto result = std::vector<QString>{};
  for (const auto& item : value.toArray())
  {
    if (!item.isString())
    {
      error = QString{"%1 must contain only strings"}.arg(key);
      return std::nullopt;
    }
    result.push_back(item.toString());
  }
  return result;
}

std::optional<std::vector<QString>> requiredStringListFromJson(
  const QJsonObject& params, const QString& key, QString& error)
{
  const auto values = stringListFromJson(params, key, error);
  if (!values)
  {
    return std::nullopt;
  }
  if (values->empty())
  {
    error = QString{"%1 must not be empty"}.arg(key);
    return std::nullopt;
  }
  return values;
}

QJsonObject issueJson(const AutomationValidationIssue& issue)
{
  auto result = QJsonObject{
    {"id", QString::fromStdString(issue.id)},
    {"stableKey", QString::fromStdString(issue.stableKey)},
    {"type", issue.type},
    {"severity", "warning"},
    {"message", QString::fromStdString(issue.message)},
    {"objectId", QString::fromStdString(issue.objectId)},
    {"objectType", QString::fromStdString(issue.objectType)},
    {"lineNumber", static_cast<int>(issue.lineNumber)},
    {"hidden", issue.hidden},
  };

  auto safeQuickFixes = QJsonArray{};
  for (const auto& quickFix : issue.safeQuickFixes)
  {
    safeQuickFixes.push_back(QString::fromStdString(quickFix));
  }
  result.insert("safeQuickFixes", safeQuickFixes);

  if (issue.faceIndex)
  {
    result.insert("faceIndex", static_cast<int>(*issue.faceIndex));
  }
  if (issue.propertyKey)
  {
    result.insert("propertyKey", QString::fromStdString(*issue.propertyKey));
  }
  return result;
}

QJsonObject problemsJson(mdl::Map& map, const QJsonObject& params)
{
  const auto includeHidden = mcpOptionalBool(params, "includeHidden", false);
  const auto limit = optionalSize(params, "limit", 500);
  const auto issues = collectAutomationValidationIssues(map, includeHidden);
  const auto returnedCount = std::min(limit, issues.size());

  auto results = QJsonArray{};
  auto safeFixableCount = 0;
  for (auto i = size_t{0}; i < returnedCount; ++i)
  {
    const auto& issue = issues[i];
    const auto json = issueJson(issue);
    if (!json.value("safeQuickFixes").toArray().empty())
    {
      ++safeFixableCount;
    }
    results.push_back(json);
  }

  return QJsonObject{
    {"valid", issues.empty()},
    {"passed", issues.empty()},
    {"count", results.size()},
    {"totalCount", static_cast<qint64>(issues.size())},
    {"returnedCount", results.size()},
    {"truncated", returnedCount < issues.size()},
    {"safeFixableCount", safeFixableCount},
    {"recoveryAction",
     issues.empty() ? "continue_validation_or_review"
                    : "inspect_problem_summary_then_fix_or_review"},
    {"problems", results},
  };
}

QJsonArray groupedIssuesJson(mdl::Map& map, const bool includeHidden)
{
  struct Group
  {
    int count = 0;
    QString message;
    QJsonArray sampleObjectIds;
    QJsonArray sampleBounds;
  };

  auto groups = std::map<QString, Group>{};
  for (const auto& issue : collectAutomationValidationIssues(map, includeHidden))
  {
    const auto message = QString::fromStdString(issue.message);
    const auto key = QString{"%1|%2"}.arg(QString::number(issue.type), message);
    auto& group = groups[key];
    ++group.count;
    group.message = message;
    if (group.sampleObjectIds.size() < 5)
    {
      group.sampleObjectIds.push_back(QString::fromStdString(issue.objectId));
      group.sampleBounds.push_back(QJsonObject{
        {"min", QJsonArray{issue.boundsMin[0], issue.boundsMin[1], issue.boundsMin[2]}},
        {"max", QJsonArray{issue.boundsMax[0], issue.boundsMax[1], issue.boundsMax[2]}},
      });
    }
  }

  auto result = QJsonArray{};
  for (const auto& [key, group] : groups)
  {
    Q_UNUSED(key);
    result.push_back(QJsonObject{
      {"message", group.message},
      {"count", group.count},
      {"sampleObjectIds", group.sampleObjectIds},
      {"sampleBounds", group.sampleBounds},
    });
  }
  return result;
}

std::vector<AutomationValidationIssue> findIssuesByIds(
  mdl::Map& map,
  const std::vector<QString>& problemIds,
  const bool includeHidden,
  QString& error)
{
  const auto wantedIds = std::set<QString>{std::begin(problemIds), std::end(problemIds)};
  auto foundIds = std::set<QString>{};
  auto result = std::vector<AutomationValidationIssue>{};
  for (const auto& issue : collectAutomationValidationIssues(map, includeHidden))
  {
    const auto id = QString::fromStdString(issue.id);
    if (wantedIds.contains(id))
    {
      foundIds.insert(id);
      result.push_back(issue);
    }
  }

  if (foundIds.size() != wantedIds.size())
  {
    for (const auto& id : wantedIds)
    {
      if (!foundIds.contains(id))
      {
        error = QString{"Unknown or stale problem id: %1"}.arg(id);
        break;
      }
    }
    return {};
  }
  return result;
}

const mdl::IssueQuickFix* findSafeQuickFix(
  const mdl::WorldNode& worldNode,
  const std::vector<const mdl::Issue*>& issues,
  const QString& description)
{
  if (!isAutomationSafeQuickFixDescription(description.toStdString()))
  {
    return nullptr;
  }

  auto issueTypes = ~static_cast<mdl::IssueType>(0);
  for (const auto* issue : issues)
  {
    issueTypes &= issue->type();
  }

  for (const auto* quickFix : worldNode.quickFixes(issueTypes))
  {
    if (QString::fromStdString(quickFix->description()) == description)
    {
      return quickFix;
    }
  }
  return nullptr;
}

} // namespace

McpBridgeToolResult problemsCheckResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return problemsCheckForMapResult(mapWindow->document().map(), params);
}

McpBridgeToolResult problemsCheckForMapResult(mdl::Map& map, const QJsonObject& params)
{
  return McpBridgeToolResult::success(problemsJson(map, params));
}

McpBridgeToolResult mapValidateResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return mapValidateForMapResult(mapWindow->document().map(), params);
}

McpBridgeToolResult mapValidateForMapResult(mdl::Map& map, const QJsonObject& params)
{
  auto result = problemsJson(map, params);
  if (mcpOptionalBool(params, "groupByType", false))
  {
    result.insert(
      "groups", groupedIssuesJson(map, mcpOptionalBool(params, "includeHidden", false)));
    result.insert("detail", "groupedSummary");
  }
  if (!mcpOptionalBool(params, "includeProblems", false))
  {
    result.remove("problems");
  }
  else
  {
    result.insert("detail", "summaryWithProblems");
    result.insert("limit", static_cast<int>(optionalSize(params, "limit", 500)));
  }
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult problemsFixResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return problemsFixForMapResult(
    mapWindow->document().map(), toolName, params, history, nextOperationIndex);
}

McpBridgeToolResult problemsFixForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  auto error = QString{};
  const auto problemIds = requiredStringListFromJson(params, "problemIds", error);
  if (!problemIds)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      error,
      preMutationFailureDetails(
        QJsonObject{{"targetSource", "problemIds"}}, "provide_problem_ids_then_retry"));
  }

  const auto quickFixDescription = params.value("quickFix").toString().trimmed();
  if (quickFixDescription.isEmpty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      "problems_fix requires quickFix",
      preMutationFailureDetails(
        QJsonObject{{"targetSource", "quickFix"}}, "provide_quick_fix_then_retry"));
  }

  const auto issues = findIssuesByIds(
    map, *problemIds, mcpOptionalBool(params, "includeHidden", false), error);
  if (!error.isEmpty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      error,
      preMutationFailureDetails(
        QJsonObject{{"targetSource", "problemIds"}}, "refresh_problems_then_retry"));
  }

  auto sourceIssues = std::vector<const mdl::Issue*>{};
  sourceIssues.reserve(issues.size());
  auto changedObjectIds = QJsonArray{};
  for (const auto& issue : issues)
  {
    sourceIssues.push_back(issue.source);
    changedObjectIds.push_back(QString::fromStdString(issue.objectId));
  }

  const auto transactionName = QString{"MCP: Fix problems (%1)"}.arg(quickFixDescription);
  const auto* quickFix =
    findSafeQuickFix(map.worldNode(), sourceIssues, quickFixDescription);
  if (!quickFix)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::Forbidden,
      QString{"Quick fix is not safe or not applicable: %1"}.arg(quickFixDescription),
      preMutationFailureDetails(
        QJsonObject{{"quickFix", quickFixDescription}},
        "choose_safe_applicable_quick_fix"));
  }
  const auto ok = executeTransaction(map, transactionName, [&]() {
    quickFix->apply(map, sourceIssues);
    return true;
  });
  if (!ok)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not apply problem quick fix");
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history,
    nextOperationIndex,
    map,
    toolName,
    transactionName,
    changedObjectIds,
    result,
    mcpIdsModeFromParams(params));
  result.insert("fixedCount", static_cast<int>(issues.size()));
  result.insert("quickFix", quickFixDescription);
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult mapFixAllSafeResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return mapFixAllSafeForMapResult(
    mapWindow->document().map(), toolName, params, history, nextOperationIndex);
}

McpBridgeToolResult mapFixAllSafeForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  const auto includeHidden = mcpOptionalBool(params, "includeHidden", false);
  auto changedObjectIds = QJsonArray{};
  auto fixedCount = 0;
  auto appliedFixes = QJsonArray{};
  const auto transactionName = QString{"MCP: Fix all safe problems"};
  const auto ok = executeTransaction(map, transactionName, [&]() {
    auto appliedAny = false;
    for (auto pass = 0; pass < 8; ++pass)
    {
      auto issues = collectAutomationValidationIssues(map, includeHidden);
      auto didApply = false;
      for (const auto& issue : issues)
      {
        if (issue.safeQuickFixes.empty())
        {
          continue;
        }

        const auto quickFixDescription =
          QString::fromStdString(issue.safeQuickFixes.front());
        const auto* quickFix =
          findSafeQuickFix(map.worldNode(), {issue.source}, quickFixDescription);
        if (!quickFix)
        {
          continue;
        }

        changedObjectIds.push_back(QString::fromStdString(issue.objectId));
        quickFix->apply(map, {issue.source});
        appliedFixes.push_back(quickFixDescription);
        ++fixedCount;
        didApply = true;
        appliedAny = true;
        break;
      }
      if (!didApply)
      {
        break;
      }
    }
    return appliedAny;
  });
  if (!ok)
  {
    return McpBridgeToolResult::success(QJsonObject{
      {"fixedCount", 0},
      {"appliedFixes", QJsonArray{}},
      {"message", "No safe problem fixes were available"},
    });
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history,
    nextOperationIndex,
    map,
    toolName,
    transactionName,
    changedObjectIds,
    result,
    mcpIdsModeFromParams(params));
  result.insert("fixedCount", fixedCount);
  result.insert("appliedFixes", appliedFixes);
  return McpBridgeToolResult::success(std::move(result));
}

} // namespace tb::ui
