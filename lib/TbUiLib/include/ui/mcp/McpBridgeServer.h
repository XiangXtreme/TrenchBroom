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

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocalServer>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QtGlobal>

#include "mcp/McpBridgeConfig.h"
#include "mcp/McpBridgeMessages.h"
#include "mcp/McpError.h"
#include "ui/automation/AutomationStateRecords.h"
#include "ui/mcp/McpObjectRegistry.h"

#include <functional>
#include <map>
#include <memory>
#include <vector>

class QLocalSocket;
class QTimer;

namespace tb::mdl
{
class Map;
}

namespace tb::ui
{
namespace mcp = tb::mcp;

class AppController;

struct McpBridgeTransportLimits
{
  qsizetype maxRequestBytes = 4 * 1024 * 1024;
  int incompleteRequestTimeoutMs = 10'000;
  int maxConnections = 32;
};

struct McpBridgeToolResult
{
  bool ok = true;
  QJsonObject result;
  mcp::McpError error;

  static McpBridgeToolResult success(QJsonObject result = {});
  static McpBridgeToolResult failure(mcp::McpErrorCode code, QString message);
  static McpBridgeToolResult failure(
    mcp::McpErrorCode code, QString message, QJsonObject details);
};

struct McpPythonExecutionReplay
{
  QByteArray requestHash;
  McpBridgeToolResult response;
};

struct McpOperationRecord
{
  QString operationId;
  QString toolName;
  QString transactionName;
  QString operationKind;
  QString documentPath;
  QString documentFingerprint;
  QStringList changedObjectIds;
  QStringList deletedObjectIds;
  QString createdAt;
  qint64 createdAtMs = 0;
  QByteArray summaryJson;
  QByteArray detailJson;
  bool undone = false;
  bool undoable = true;
  bool redoable = false;
  QString parentOperationId;
  QStringList childOperationIds;
  QByteArray sessionBeforeJson;
  QByteArray sessionAfterJson;

  McpOperationRecord();
  void setChangedObjectIds(const QJsonArray& ids);
  void setDeletedObjectIds(const QJsonArray& ids);
  QJsonArray changedObjectIdsJson() const;
  QJsonArray deletedObjectIdsJson() const;
  void setSummary(const QJsonObject& value);
  QJsonObject summary() const;
  void setDetail(const QJsonObject& value);
  QJsonObject detail() const;
};

// Compatibility names for legacy MCP adapter code. New services use the
// automation names above and must not depend on this bridge header.
using McpBrushMetadataRecord = automation::AutomationObjectMetadataRecord;
using McpModuleRecord = automation::AutomationModuleRecord;
using McpIrPreviewCacheRecord = automation::AutomationIrPreviewRecord;

struct McpReviewResourceRecord
{
  QJsonObject resource;
  QString documentFingerprint;
  qint64 createdAtMs = 0;
};

struct McpSessionEvictionCounters
{
  quint64 operationRecords = 0;
  quint64 reviewResources = 0;
  quint64 irPreviews = 0;
  quint64 documentFingerprints = 0;
  quint64 objectRegistryRecords = 0;
};

class McpSessionState
{
public:
  static constexpr auto MaxOperationRecords = size_t{1024};
  static constexpr auto MaxReviewResources = size_t{128};
  static constexpr auto MaxIrPreviews = size_t{64};
  static constexpr auto MaxDocumentFingerprints = qsizetype{4};
  static constexpr auto MaxResourcesPerPage = qsizetype{100};
  static constexpr auto IrPreviewTtlMs = qint64{10 * 60 * 1000};

  int nextOperationIndex = 1;
  std::vector<McpOperationRecord> operationHistory;
  std::map<QString, McpBrushMetadataRecord> brushMetadata;
  std::map<QString, McpModuleRecord> modules;
  std::map<QString, McpIrPreviewCacheRecord> irPreviewCache;
  std::map<QString, McpReviewResourceRecord> reviewResources;
  int nextIrPreviewIndex = 1;
  McpObjectRegistry objectRegistry;
  QStringList recentDocumentFingerprints;
  McpSessionEvictionCounters evictions;
  std::map<QString, QJsonObject> evictedResourceHints;

  void clear();
  void rememberDocumentFingerprint(const QString& documentFingerprint);
  void prune(const QString& activeDocumentFingerprint, qint64 nowMs);
  void cacheReviewResource(
    const QJsonObject& resource, const QString& documentFingerprint = {});
  std::optional<QJsonObject> listResources(
    const QString& cursor, QString* error = nullptr) const;
  std::optional<QJsonObject> evictedResourceHint(const QString& uri) const;
  QJsonObject diagnosticsJson() const;
};

class McpToolRegistry;

class McpBridgeServer : public QObject
{
  Q_OBJECT
public:
  using ToolHandler =
    std::function<McpBridgeToolResult(const QString&, const QJsonObject&)>;
  using ActiveMapProvider = std::function<mdl::Map*()>;

private:
  mcp::McpBridgeConfig m_config;
  McpBridgeTransportLimits m_transportLimits;
  ToolHandler m_toolHandler;
  ActiveMapProvider m_activeMapProvider;
  QJsonObject m_overlayState;
  mutable McpSessionState m_session;
  int& m_nextOperationIndex = m_session.nextOperationIndex;
  std::vector<McpOperationRecord>& m_operationHistory = m_session.operationHistory;
  std::map<QString, McpBrushMetadataRecord>& m_brushMetadata = m_session.brushMetadata;
  std::map<QString, McpModuleRecord>& m_modules = m_session.modules;
  std::map<QString, McpIrPreviewCacheRecord>& m_irPreviewCache = m_session.irPreviewCache;
  int& m_nextIrPreviewIndex = m_session.nextIrPreviewIndex;
  McpObjectRegistry& m_objectRegistry = m_session.objectRegistry;
  mutable std::map<QString, McpPythonExecutionReplay> m_pythonExecutionReplays;
  mutable QStringList m_pythonExecutionReplayOrder;
  std::unique_ptr<McpToolRegistry> m_toolRegistry;
  mutable bool m_dispatchInProgress = false;
  QString m_bridgeInstanceId;
  QDateTime m_bridgeStartedAtUtc;
  std::unique_ptr<QLocalServer> m_server;
  QSet<QLocalSocket*> m_connections;
  QHash<QLocalSocket*, QTimer*> m_requestDeadlines;

public:
  explicit McpBridgeServer(AppController& appController, QObject* parent = nullptr);
  McpBridgeServer(
    AppController& appController,
    McpBridgeTransportLimits transportLimits,
    QObject* parent = nullptr);
  explicit McpBridgeServer(ToolHandler toolHandler, QObject* parent = nullptr);
  McpBridgeServer(
    ToolHandler toolHandler,
    McpBridgeTransportLimits transportLimits,
    QObject* parent = nullptr);
  McpBridgeServer(
    ToolHandler toolHandler,
    ActiveMapProvider activeMapProvider,
    QObject* parent = nullptr);
  McpBridgeServer(
    ToolHandler toolHandler,
    ActiveMapProvider activeMapProvider,
    McpBridgeTransportLimits transportLimits,
    QObject* parent = nullptr);
  ~McpBridgeServer() override;

  bool start(const mcp::McpBridgeConfig& config, QString* error = nullptr);
  void stop();

  bool isListening() const;
  QString pipeName() const;
  mcp::McpMode mode() const;
  const QJsonObject& overlayState() const;
  QStringList registeredToolNames() const;
  int duplicateToolRegistrationCount() const;

  mcp::McpBridgeResponse dispatchRequest(const mcp::McpBridgeRequest& request) const;
  std::optional<QJsonObject> listResources(
    const QString& cursor, QString* error = nullptr) const;
  std::optional<QJsonObject> readResource(const QString& uri) const;

private:
  mcp::McpBridgeResponse dispatchToolCall(const mcp::McpBridgeRequest& request) const;
  void clearSessionState();
  void startRequestDeadline(QLocalSocket& socket);
  void restartRequestDeadline(QLocalSocket& socket);
  void rejectAndDisconnect(QLocalSocket& socket, const QString& message) const;
  void removeConnection(QLocalSocket& socket);
  void handleNewConnection();
  void handleSocketReadyRead(QLocalSocket& socket);
  void writeResponse(QLocalSocket& socket, const mcp::McpBridgeResponse& response) const;
};

} // namespace tb::ui
