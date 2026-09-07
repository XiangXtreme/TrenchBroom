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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "ui/mcp/McpBridgeServer.h"

#include <algorithm>

namespace tb::ui
{
namespace
{

constexpr auto ResourceCursorPrefix = "tbmcp-resources-v1:";

QString resourceCursor(const qsizetype offset)
{
  const auto payload = QByteArray{ResourceCursorPrefix} + QByteArray::number(offset);
  return QString::fromLatin1(
    payload.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

std::optional<qsizetype> resourceOffset(const QString& cursor)
{
  if (cursor.isEmpty())
  {
    return qsizetype{0};
  }

  const auto encoded = cursor.toLatin1();
  const auto decoded = QByteArray::fromBase64(encoded, QByteArray::Base64UrlEncoding);
  if (
    decoded.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)
      != encoded
    || !decoded.startsWith(ResourceCursorPrefix))
  {
    return std::nullopt;
  }

  auto ok = false;
  const auto offset =
    decoded.mid(QByteArray{ResourceCursorPrefix}.size()).toLongLong(&ok);
  if (!ok || offset < 0)
  {
    return std::nullopt;
  }
  return static_cast<qsizetype>(offset);
}

QJsonObject compactReviewResource(const QJsonObject& result)
{
  auto compact = QJsonObject{};
  const auto copy = [&](const QString& key) {
    if (result.contains(key))
    {
      compact.insert(key, result.value(key));
    }
  };
  for (const auto& key :
       {"tool",
        "renderer",
        "style",
        "edgeMode",
        "edgeInterpretation",
        "internalBrushEdgesDrawn",
        "visualReviewRequired",
        "reviewId",
        "resourceUri",
        "preferredCapturePath",
        "absolutePreferredCapturePath",
        "outputDir",
        "absoluteOutputDir",
        "manifestPath",
        "absoluteManifestPath",
        "targetObjectCount",
        "targetObjectIdsCount",
        "targetObjectIdsSample",
        "idsMode",
        "targetBrushCount",
        "unsupportedObjectCount",
        "targetBounds",
        "captureCount",
        "renderReadable",
        "qualityValid",
        "semanticAcceptance",
        "warnings",
        "faceCount",
        "edgeCount",
        "labelCount",
        "entityLabelCount",
        "orderLabelCount",
        "partLabelCount",
        "simplified",
        "verticalExaggeration"})
  {
    copy(key);
  }

  const auto contactSheet = result.value("contactSheet").toObject();
  if (!contactSheet.isEmpty())
  {
    compact.insert(
      "contactSheet",
      QJsonObject{
        {"path", contactSheet.value("path")},
        {"absolutePath", contactSheet.value("absolutePath")},
        {"width", contactSheet.value("width")},
        {"height", contactSheet.value("height")},
        {"fileSize", contactSheet.value("fileSize")},
        {"valid", contactSheet.value("valid")},
      });
  }
  return compact;
}

} // namespace

McpSessionState::McpSessionState()
  : m_ownedAutomationState{std::make_unique<automation::AutomationStateStore>()}
  , brushMetadata{m_ownedAutomationState->objectMetadata}
  , modules{m_ownedAutomationState->modules}
  , irPreviewCache{m_ownedAutomationState->irPreviews}
  , nextIrPreviewIndex{m_ownedAutomationState->nextIrPreviewIndex}
{
}

McpSessionState::McpSessionState(automation::AutomationStateStore& automationState)
  : brushMetadata{automationState.objectMetadata}
  , modules{automationState.modules}
  , irPreviewCache{automationState.irPreviews}
  , nextIrPreviewIndex{automationState.nextIrPreviewIndex}
{
}

void McpSessionState::clear()
{
  nextOperationIndex = 1;
  operationHistory.clear();
  brushMetadata.clear();
  modules.clear();
  irPreviewCache.clear();
  reviewResources.clear();
  nextIrPreviewIndex = 1;
  objectRegistry.clear();
  recentDocumentFingerprints.clear();
  evictions = {};
  evictedResourceHints.clear();
}

void McpSessionState::rememberDocumentFingerprint(const QString& documentFingerprint)
{
  if (documentFingerprint.isEmpty())
  {
    return;
  }

  recentDocumentFingerprints.removeAll(documentFingerprint);
  recentDocumentFingerprints.prepend(documentFingerprint);
  while (recentDocumentFingerprints.size() > MaxDocumentFingerprints)
  {
    const auto evictedFingerprint = recentDocumentFingerprints.takeLast();
    ++evictions.documentFingerprints;

    const auto oldOperationCount = operationHistory.size();
    std::erase_if(operationHistory, [&](const auto& operation) {
      if (operation.documentFingerprint != evictedFingerprint)
      {
        return false;
      }
      evictedResourceHints[QString{"tbmcp://operation/%1"}.arg(operation.operationId)] =
        QJsonObject{
          {"evicted", true},
          {"resourceUri", QString{"tbmcp://operation/%1"}.arg(operation.operationId)},
          {"reason", "document_fingerprint_budget"},
          {"recoveryAction", "refresh_history_for_active_document"},
        };
      return true;
    });
    evictions.operationRecords += oldOperationCount - operationHistory.size();

    std::erase_if(brushMetadata, [&](const auto& entry) {
      return entry.second.documentFingerprint == evictedFingerprint;
    });
    std::erase_if(modules, [&](const auto& entry) {
      return entry.second.documentFingerprint == evictedFingerprint;
    });

    const auto oldPreviewCount = irPreviewCache.size();
    std::erase_if(irPreviewCache, [&](const auto& entry) {
      return entry.second.documentFingerprint == evictedFingerprint;
    });
    evictions.irPreviews += oldPreviewCount - irPreviewCache.size();

    const auto oldReviewCount = reviewResources.size();
    std::erase_if(reviewResources, [&](const auto& entry) {
      if (entry.second.documentFingerprint != evictedFingerprint)
      {
        return false;
      }
      evictedResourceHints[entry.first] = QJsonObject{
        {"evicted", true},
        {"resourceUri", entry.first},
        {"reason", "document_fingerprint_budget"},
        {"recoveryAction", "activate_document_and_regenerate_review"},
      };
      return true;
    });
    evictions.reviewResources += oldReviewCount - reviewResources.size();
  }

  evictions.objectRegistryRecords +=
    objectRegistry.retainDocumentFingerprints(recentDocumentFingerprints);
  while (evictedResourceHints.size() > 512u)
  {
    evictedResourceHints.erase(evictedResourceHints.begin());
  }
}

void McpSessionState::prune(const QString& activeDocumentFingerprint, const qint64 nowMs)
{
  rememberDocumentFingerprint(activeDocumentFingerprint);

  const auto rememberEvictedResource = [&](const QString& uri, QJsonObject hint) {
    hint.insert("resourceUri", uri);
    hint.insert("evictedAtMs", nowMs);
    evictedResourceHints[uri] = std::move(hint);
    while (evictedResourceHints.size() > 512u)
    {
      evictedResourceHints.erase(evictedResourceHints.begin());
    }
  };

  for (auto it = irPreviewCache.begin(); it != irPreviewCache.end();)
  {
    if (it->second.expiresAtMs > nowMs)
    {
      ++it;
      continue;
    }
    rememberEvictedResource(
      QString{"tbmcp://ir-preview/%1"}.arg(it->first),
      QJsonObject{
        {"evicted", true},
        {"reason", "expired"},
        {"recoveryAction", "compile_preview_again"},
      });
    it = irPreviewCache.erase(it);
    ++evictions.irPreviews;
  }
  while (irPreviewCache.size() > MaxIrPreviews)
  {
    const auto oldest = std::ranges::min_element(
      irPreviewCache, {}, [](const auto& entry) { return entry.second.createdAtMs; });
    rememberEvictedResource(
      QString{"tbmcp://ir-preview/%1"}.arg(oldest->first),
      QJsonObject{
        {"evicted", true},
        {"reason", "preview_budget"},
        {"recoveryAction", "compile_preview_again"},
      });
    irPreviewCache.erase(oldest);
    ++evictions.irPreviews;
  }

  while (reviewResources.size() > MaxReviewResources)
  {
    const auto oldest = std::ranges::min_element(
      reviewResources, {}, [](const auto& entry) { return entry.second.createdAtMs; });
    rememberEvictedResource(
      oldest->first,
      QJsonObject{
        {"evicted", true},
        {"reason", "review_resource_budget"},
        {"recoveryAction", "regenerate_review"},
      });
    reviewResources.erase(oldest);
    ++evictions.reviewResources;
  }

  const auto eraseOperationFamily = [&](const McpOperationRecord& candidate) {
    const auto parentOperationId = candidate.parentOperationId.isEmpty()
                                     ? candidate.operationId
                                     : candidate.parentOperationId;
    const auto oldSize = operationHistory.size();
    std::erase_if(operationHistory, [&](const auto& operation) {
      const auto remove = operation.operationId == parentOperationId
                          || operation.parentOperationId == parentOperationId;
      if (remove)
      {
        rememberEvictedResource(
          QString{"tbmcp://operation/%1"}.arg(operation.operationId),
          QJsonObject{
            {"evicted", true},
            {"reason", "operation_record_budget"},
            {"recoveryAction", "refresh_history_status"},
          });
      }
      return remove;
    });
    evictions.operationRecords += oldSize - operationHistory.size();
  };

  while (operationHistory.size() > MaxOperationRecords)
  {
    const auto oldestMatching = [&](const auto& predicate) {
      auto result = operationHistory.end();
      for (auto it = operationHistory.begin(); it != operationHistory.end(); ++it)
      {
        if (
          predicate(*it)
          && (result == operationHistory.end() || it->createdAtMs < result->createdAtMs))
        {
          result = it;
        }
      }
      return result;
    };

    auto candidate =
      oldestMatching([](const auto& operation) { return operation.undone; });
    if (candidate == operationHistory.end())
    {
      candidate = oldestMatching([&](const auto& operation) {
        return !activeDocumentFingerprint.isEmpty()
               && operation.documentFingerprint != activeDocumentFingerprint;
      });
    }
    if (candidate == operationHistory.end())
    {
      candidate = oldestMatching([](const auto&) { return true; });
    }
    eraseOperationFamily(*candidate);
  }
}

void McpSessionState::cacheReviewResource(
  const QJsonObject& result, const QString& documentFingerprint)
{
  const auto resourceUri = result.value("resourceUri").toString();
  if (!resourceUri.startsWith("tbmcp://review/"))
  {
    return;
  }
  reviewResources[resourceUri] = McpReviewResourceRecord{
    compactReviewResource(result),
    result.value("documentFingerprint").toString(documentFingerprint),
    QDateTime::currentMSecsSinceEpoch(),
  };
  evictedResourceHints.erase(resourceUri);
}

std::optional<QJsonObject> McpSessionState::listResources(
  const QString& cursor, QString* error) const
{
  if (error)
  {
    error->clear();
  }

  auto resources = std::vector<QJsonObject>{};
  resources.reserve(operationHistory.size() + reviewResources.size());
  for (const auto& operation : operationHistory)
  {
    if (operation.operationId.isEmpty())
    {
      continue;
    }
    const auto uri = QString{"tbmcp://operation/%1"}.arg(operation.operationId);
    auto name = operation.transactionName.trimmed();
    if (name.isEmpty())
    {
      name = operation.toolName.trimmed();
    }
    if (name.isEmpty())
    {
      name = operation.operationId;
    }
    resources.push_back(QJsonObject{
      {"uri", uri},
      {"name", name},
      {"description", QString{"MCP operation %1"}.arg(operation.operationId)},
      {"mimeType", "application/json"},
      {"type", "operation"},
    });
  }

  for (const auto& [uri, record] : reviewResources)
  {
    auto name = record.resource.value("reviewId").toString().trimmed();
    if (name.isEmpty())
    {
      name = record.resource.value("tool").toString().trimmed();
    }
    if (name.isEmpty())
    {
      name = uri;
    }
    resources.push_back(QJsonObject{
      {"uri", uri},
      {"name", name},
      {"description", QString{"MCP review %1"}.arg(name)},
      {"mimeType", "application/json"},
      {"type", "review"},
    });
  }

  std::ranges::sort(
    resources, {}, [](const auto& resource) { return resource.value("uri").toString(); });

  const auto offset = resourceOffset(cursor);
  if (!offset || *offset > static_cast<qsizetype>(resources.size()))
  {
    if (error)
    {
      *error = "Invalid MCP resource cursor";
    }
    return std::nullopt;
  }

  const auto end =
    std::min(*offset + MaxResourcesPerPage, static_cast<qsizetype>(resources.size()));
  auto page = QJsonArray{};
  for (auto i = *offset; i < end; ++i)
  {
    page.push_back(resources[static_cast<size_t>(i)]);
  }

  auto result = QJsonObject{{"resources", page}};
  if (end < static_cast<qsizetype>(resources.size()))
  {
    result.insert("nextCursor", resourceCursor(end));
  }
  return result;
}

std::optional<QJsonObject> McpSessionState::evictedResourceHint(const QString& uri) const
{
  const auto it = evictedResourceHints.find(uri);
  return it != evictedResourceHints.end() ? std::optional{it->second} : std::nullopt;
}

QJsonObject McpSessionState::diagnosticsJson() const
{
  return QJsonObject{
    {"limits",
     QJsonObject{
       {"operationRecords", static_cast<qint64>(MaxOperationRecords)},
       {"reviewResources", static_cast<qint64>(MaxReviewResources)},
       {"irPreviews", static_cast<qint64>(MaxIrPreviews)},
       {"irPreviewTtlMs", IrPreviewTtlMs},
       {"documentFingerprints", MaxDocumentFingerprints},
       {"resourcesPerPage", MaxResourcesPerPage},
     }},
    {"counts",
     QJsonObject{
       {"operationRecords", static_cast<qint64>(operationHistory.size())},
       {"reviewResources", static_cast<qint64>(reviewResources.size())},
       {"irPreviews", static_cast<qint64>(irPreviewCache.size())},
       {"documentFingerprints", recentDocumentFingerprints.size()},
       {"objectRegistryRecords", static_cast<qint64>(objectRegistry.recordCount())},
     }},
    {"evictions",
     QJsonObject{
       {"operationRecords", static_cast<qint64>(evictions.operationRecords)},
       {"reviewResources", static_cast<qint64>(evictions.reviewResources)},
       {"irPreviews", static_cast<qint64>(evictions.irPreviews)},
       {"documentFingerprints", static_cast<qint64>(evictions.documentFingerprints)},
       {"objectRegistryRecords", static_cast<qint64>(evictions.objectRegistryRecords)},
     }},
  };
}

McpOperationRecord::McpOperationRecord()
{
  const auto now = QDateTime::currentDateTimeUtc();
  createdAt = now.toString(Qt::ISODateWithMs);
  createdAtMs = now.toMSecsSinceEpoch();
  operationKind = "mutation";
}

void McpOperationRecord::setChangedObjectIds(const QJsonArray& ids)
{
  changedObjectIds.clear();
  changedObjectIds.reserve(ids.size());
  for (const auto& value : ids)
  {
    if (value.isString())
    {
      changedObjectIds.push_back(value.toString());
    }
  }
}

void McpOperationRecord::setDeletedObjectIds(const QJsonArray& ids)
{
  deletedObjectIds.clear();
  deletedObjectIds.reserve(ids.size());
  for (const auto& value : ids)
  {
    if (value.isString())
    {
      deletedObjectIds.push_back(value.toString());
    }
  }
}

QJsonArray McpOperationRecord::changedObjectIdsJson() const
{
  auto result = QJsonArray{};
  for (const auto& id : changedObjectIds)
  {
    result.push_back(id);
  }
  return result;
}

QJsonArray McpOperationRecord::deletedObjectIdsJson() const
{
  auto result = QJsonArray{};
  for (const auto& id : deletedObjectIds)
  {
    result.push_back(id);
  }
  return result;
}

void McpOperationRecord::setSummary(const QJsonObject& value)
{
  summaryJson = QJsonDocument{value}.toJson(QJsonDocument::Compact);
}

QJsonObject McpOperationRecord::summary() const
{
  const auto document = QJsonDocument::fromJson(summaryJson);
  return document.isObject() ? document.object() : QJsonObject{};
}

void McpOperationRecord::setDetail(const QJsonObject& value)
{
  detailJson = QJsonDocument{value}.toJson(QJsonDocument::Compact);
}

QJsonObject McpOperationRecord::detail() const
{
  const auto document = QJsonDocument::fromJson(detailJson);
  return document.isObject() ? document.object() : QJsonObject{};
}

} // namespace tb::ui
