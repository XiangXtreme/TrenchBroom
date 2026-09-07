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

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "mdl/Map.h"
#include "mdl/Transaction.h"
#include "ui/automation/AutomationTransaction.h"
#include "ui/mcp/McpBridgeServer.h"

#include <cmath>
#include <functional>
#include <optional>
#include <utility>

namespace tb::ui
{

struct McpQualityPolicy
{
  QString intent = "balanced";
  double maxDirectionChangeDegrees = 12.0;
  double maxSagitta = 2.0;
  double maxSnapDisplacement = 2.0;

  bool strict() const { return intent == "smooth"; }
};

inline QJsonObject qualityPolicyJson(const McpQualityPolicy& policy)
{
  return QJsonObject{
    {"intent", policy.intent},
    {"maxDirectionChangeDegrees", policy.maxDirectionChangeDegrees},
    {"maxSagitta", policy.maxSagitta},
    {"maxSnapDisplacement", policy.maxSnapDisplacement},
  };
}

inline std::optional<McpQualityPolicy> qualityPolicyFromJson(
  const QJsonObject& object, QString& error)
{
  auto policy = McpQualityPolicy{};
  const auto value = object.value("qualityPolicy");
  if (value.isUndefined() || value.isNull())
  {
    return policy;
  }
  if (!value.isObject())
  {
    error = "qualityPolicy must be an object";
    return std::nullopt;
  }

  const auto quality = value.toObject();
  policy.intent = quality.value("intent").toString("balanced").trimmed().toLower();
  if (policy.intent == "draft")
  {
    policy.maxDirectionChangeDegrees = 22.5;
    policy.maxSagitta = 8.0;
    policy.maxSnapDisplacement = 8.0;
  }
  else if (policy.intent == "balanced")
  {
    policy.maxDirectionChangeDegrees = 12.0;
    policy.maxSagitta = 2.0;
    policy.maxSnapDisplacement = 2.0;
  }
  else if (policy.intent == "smooth")
  {
    policy.maxDirectionChangeDegrees = 7.5;
    policy.maxSagitta = 1.0;
    policy.maxSnapDisplacement = 1.0;
  }
  else
  {
    error = "qualityPolicy.intent must be draft, balanced, or smooth";
    return std::nullopt;
  }

  const auto readLimit = [&](const char* key, double& target) {
    const auto limit = quality.value(key);
    if (limit.isUndefined() || limit.isNull())
    {
      return true;
    }
    if (!limit.isDouble() || !std::isfinite(limit.toDouble()) || limit.toDouble() <= 0.0)
    {
      error = QString{"qualityPolicy.%1 must be a positive finite number"}.arg(key);
      return false;
    }
    target = limit.toDouble();
    return true;
  };

  if (
    !readLimit("maxDirectionChangeDegrees", policy.maxDirectionChangeDegrees)
    || !readLimit("maxSagitta", policy.maxSagitta)
    || !readLimit("maxSnapDisplacement", policy.maxSnapDisplacement))
  {
    return std::nullopt;
  }
  return policy;
}

inline bool executeTransaction(
  mdl::Map& map, const QString& transactionName, const std::function<bool()>& operation)
{
  auto transaction =
    automation::AutomationTransaction{map, transactionName.toStdString()};
  if (!operation())
  {
    transaction.cancel();
    return false;
  }
  return transaction.commit();
}

inline QJsonObject preMutationFailureDetails(
  QJsonObject details, const QString& recoveryAction)
{
  details.insert("mutatedDocument", false);
  details.insert("retrySafe", true);
  details.insert("recoveryAction", recoveryAction);
  return details;
}

inline McpBridgeToolResult preMutationInvalidParamsFailure(
  const QString& message, const QString& recoveryAction, QJsonObject details = {})
{
  return McpBridgeToolResult::failure(
    mcp::McpErrorCode::InvalidParams,
    message,
    preMutationFailureDetails(std::move(details), recoveryAction));
}

} // namespace tb::ui
