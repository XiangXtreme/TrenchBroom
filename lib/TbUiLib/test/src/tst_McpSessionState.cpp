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

#include <QDateTime>

#include "ui/automation/AutomationStateStore.h"
#include "ui/mcp/McpBridgeServer.h"

#include <algorithm>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("McpSessionState enforces bounded caches", "[McpBridgeServer][McpSessionState]")
{
  const auto nowMs = QDateTime::currentMSecsSinceEpoch();

  SECTION("operation records prefer evicting undone entries")
  {
    auto state = McpSessionState{};
    for (auto i = 0; i <= static_cast<int>(McpSessionState::MaxOperationRecords); ++i)
    {
      auto operation = McpOperationRecord{};
      operation.operationId = QString{"mcp-op-%1"}.arg(i);
      operation.documentFingerprint = "doc:active";
      operation.createdAtMs = nowMs + i;
      operation.undone = i == 0;
      state.operationHistory.push_back(std::move(operation));
    }

    state.prune("doc:active", nowMs);

    CHECK(state.operationHistory.size() == McpSessionState::MaxOperationRecords);
    CHECK(state.evictions.operationRecords == 1u);
    CHECK_FALSE(std::ranges::any_of(state.operationHistory, [](const auto& operation) {
      return operation.operationId == "mcp-op-0";
    }));
    const auto hint = state.evictedResourceHint("tbmcp://operation/mcp-op-0");
    REQUIRE(hint);
    CHECK(hint->value("recoveryAction").toString() == "refresh_history_status");
  }

  SECTION("shared automation state owns module, metadata, and preview records")
  {
    auto automationState = automation::AutomationStateStore{};
    auto state = McpSessionState{automationState};
    state.brushMetadata["doc:active|mcp:1"] =
      McpBrushMetadataRecord{"mcp:1", "doc:active", {}, false};
    state.modules["doc:active|module"] =
      McpModuleRecord{"module", "doc:active", {}, {}, {}, 1, {}, {}, {}};
    state.irPreviewCache["ir-preview-1"] = McpIrPreviewCacheRecord{
      "ir-preview-1", {}, {}, "doc:active", {}, nowMs, nowMs + 1, {}};
    ++state.nextIrPreviewIndex;

    CHECK(automationState.objectMetadata.size() == 1u);
    CHECK(automationState.modules.size() == 1u);
    CHECK(automationState.irPreviews.size() == 1u);
    CHECK(automationState.nextIrPreviewIndex == 2);

    state.clear();
    CHECK(automationState.objectMetadata.empty());
    CHECK(automationState.modules.empty());
    CHECK(automationState.irPreviews.empty());
    CHECK(automationState.nextIrPreviewIndex == 1);
  }

  SECTION("IR previews expire and stay within the fixed budget")
  {
    auto state = McpSessionState{};
    for (auto i = 0; i < 66; ++i)
    {
      const auto previewId = QString{"ir-preview-%1"}.arg(i);
      state.irPreviewCache[previewId] = McpIrPreviewCacheRecord{
        previewId,
        {},
        {},
        "doc:active",
        {},
        nowMs + i,
        i == 0 ? nowMs - 1 : nowMs + McpSessionState::IrPreviewTtlMs,
        {},
      };
    }

    state.prune("doc:active", nowMs);

    CHECK(state.irPreviewCache.size() == McpSessionState::MaxIrPreviews);
    CHECK(state.evictions.irPreviews == 2u);
    CHECK_FALSE(state.irPreviewCache.contains("ir-preview-0"));
    CHECK_FALSE(state.irPreviewCache.contains("ir-preview-1"));
  }

  SECTION("only the current and three recent document fingerprints are retained")
  {
    auto state = McpSessionState{};
    state.brushMetadata["doc:1|mcp:1:1"] =
      McpBrushMetadataRecord{"mcp:1:1", "doc:1", {}, false};
    state.modules["doc:1|module"] =
      McpModuleRecord{"module", "doc:1", {}, {}, {}, 0, {}, {}, {}};

    for (auto i = 1; i <= 5; ++i)
    {
      state.rememberDocumentFingerprint(QString{"doc:%1"}.arg(i));
    }

    CHECK(
      state.recentDocumentFingerprints
      == QStringList{"doc:5", "doc:4", "doc:3", "doc:2"});
    CHECK(state.evictions.documentFingerprints == 1u);
    CHECK(state.brushMetadata.empty());
    CHECK(state.modules.empty());
  }

  SECTION("diagnostics expose limits, counts, and eviction counters")
  {
    auto state = McpSessionState{};
    state.rememberDocumentFingerprint("doc:active");
    const auto diagnostics = state.diagnosticsJson();

    CHECK(
      diagnostics.value("limits").toObject().value("reviewResources").toInt()
      == static_cast<int>(McpSessionState::MaxReviewResources));
    CHECK(
      diagnostics.value("counts").toObject().value("documentFingerprints").toInt() == 1);
    CHECK(
      diagnostics.value("limits").toObject().value("resourcesPerPage").toInt()
      == McpSessionState::MaxResourcesPerPage);
    CHECK(
      diagnostics.value("evictions").toObject().value("operationRecords").toInt() == 0);
  }

  SECTION("retained operation and review resources use bounded pagination")
  {
    auto state = McpSessionState{};
    for (auto i = 0; i < 101; ++i)
    {
      auto operation = McpOperationRecord{};
      operation.operationId = QString{"mcp-op-%1"}.arg(i, 3, 10, QLatin1Char{'0'});
      operation.toolName = "blockout_create_batch";
      operation.transactionName = QString{"MCP operation %1"}.arg(i);
      state.operationHistory.push_back(std::move(operation));
    }
    const auto reviewUri = QString{"tbmcp://review/review-1"};
    state.reviewResources[reviewUri] = McpReviewResourceRecord{
      QJsonObject{
        {"resourceUri", reviewUri},
        {"reviewId", "review-1"},
        {"tool", "render_review_current_scene"},
      },
      "doc:active",
      nowMs,
    };
    state.evictedResourceHints["tbmcp://review/evicted"] = QJsonObject{
      {"evicted", true},
    };

    auto error = QString{};
    const auto firstPage = state.listResources({}, &error);
    REQUIRE(firstPage);
    CHECK(error.isEmpty());
    const auto firstResources = firstPage->value("resources").toArray();
    REQUIRE(firstResources.size() == 100);
    CHECK(firstResources.first().toObject().value("type").toString() == "operation");
    CHECK(
      firstResources.first().toObject().value("mimeType").toString()
      == "application/json");
    const auto nextCursor = firstPage->value("nextCursor").toString();
    CHECK_FALSE(nextCursor.isEmpty());
    CHECK(nextCursor != "100");

    const auto secondPage = state.listResources(nextCursor, &error);
    REQUIRE(secondPage);
    const auto secondResources = secondPage->value("resources").toArray();
    REQUIRE(secondResources.size() == 2);
    CHECK_FALSE(secondPage->contains("nextCursor"));
    CHECK(std::ranges::any_of(secondResources, [&](const auto& value) {
      const auto resource = value.toObject();
      return resource.value("uri").toString() == reviewUri
             && resource.value("type").toString() == "review";
    }));
    CHECK_FALSE(std::ranges::any_of(secondResources, [](const auto& value) {
      return value.toObject().value("uri").toString() == "tbmcp://review/evicted";
    }));

    CHECK_FALSE(state.listResources("not-a-valid-cursor", &error));
    CHECK(error.contains("cursor"));
  }
}

} // namespace tb::ui
