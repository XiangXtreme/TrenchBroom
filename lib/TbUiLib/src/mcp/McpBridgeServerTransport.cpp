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

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>
#include <QUuid>

#include "McpBridgeServerTools.h"
#include "mcp/McpToolCatalog.h"
#include "mdl/Map.h"
#include "ui/QPathUtils.h"
#include "ui/mcp/McpBridgeServer.h"

#include <algorithm>

namespace tb::ui
{
namespace
{

mcp::McpBridgeResponse makeFailure(
  const mcp::McpBridgeRequest& request,
  const mcp::McpErrorCode code,
  const QString& message)
{
  return mcp::McpBridgeResponse::failure(request.id, mcp::McpError{code, message});
}

mcp::McpBridgeResponse makeFailure(
  const mcp::McpBridgeRequest& request,
  const mcp::McpErrorCode code,
  const QString& message,
  QJsonObject details)
{
  return mcp::McpBridgeResponse::failure(
    request.id, mcp::McpError{code, message, std::move(details)});
}

void applyDocumentIdentityToOperation(
  McpOperationRecord& operation,
  mdl::Map& map,
  const McpObjectRegistry& objectRegistry,
  const QJsonObject& result)
{
  operation.documentPath = result.value("activeDocumentPath").toString();
  if (operation.documentPath.isEmpty() && !map.path().empty())
  {
    operation.documentPath = pathAsQString(map.path());
  }

  operation.documentFingerprint = result.value("documentFingerprint").toString();
  if (operation.documentFingerprint.isEmpty())
  {
    operation.documentFingerprint = objectRegistry.documentFingerprint(map);
  }
}

void syncOneOperationHistoryWithExternalResult(
  std::vector<McpOperationRecord>& history,
  const QString& operationId,
  mdl::Map& map,
  const McpObjectRegistry& objectRegistry,
  const QJsonObject& result)
{
  if (operationId.isEmpty())
  {
    return;
  }

  const auto it = std::ranges::find_if(
    history, [&](const auto& operation) { return operation.operationId == operationId; });
  if (it == history.end())
  {
    auto operation = McpOperationRecord{};
    operation.operationId = operationId;
    operation.toolName = result.value("toolName").toString();
    operation.transactionName = result.value("transactionName").toString();
    operation.operationKind = result.value("operationKind").toString("mutation");
    operation.setChangedObjectIds(result.value("changedObjectIds").toArray());
    operation.setDeletedObjectIds(result.value("deletedObjectIds").toArray());
    operation.setSummary(result);
    applyDocumentIdentityToOperation(operation, map, objectRegistry, result);
    appendMcpOperationRecord(history, std::move(operation));
    return;
  }

  const auto changedObjectIds = result.value("changedObjectIds").toArray();
  if (!changedObjectIds.isEmpty())
  {
    it->setChangedObjectIds(changedObjectIds);
  }
  const auto deletedObjectIds = result.value("deletedObjectIds").toArray();
  if (!deletedObjectIds.isEmpty())
  {
    it->setDeletedObjectIds(deletedObjectIds);
  }
  it->setSummary(result);
  applyDocumentIdentityToOperation(*it, map, objectRegistry, result);
}

void syncOperationHistoryWithExternalResult(
  std::vector<McpOperationRecord>& history,
  mdl::Map& map,
  const McpObjectRegistry& objectRegistry,
  const QJsonObject& result)
{
  syncOneOperationHistoryWithExternalResult(
    history, result.value("operationId").toString(), map, objectRegistry, result);

  if (!result.value("parentOperationId").toString().isEmpty())
  {
    return;
  }

  const auto operationIds = result.value("operationIds").toArray();
  for (const auto& operationId : operationIds)
  {
    syncOneOperationHistoryWithExternalResult(
      history, operationId.toString(), map, objectRegistry, result);
  }
}

QString mutationUndoOperationId(const QJsonObject& result)
{
  for (const auto& key : {"undoOperationId", "parentOperationId", "operationId"})
  {
    const auto value = result.value(key).toString().trimmed();
    if (!value.isEmpty())
    {
      return value;
    }
  }
  return result.value("operation").toObject().value("operationId").toString().trimmed();
}

QJsonArray mutationAuditOperationIds(
  const QJsonObject& result, const QString& undoOperationId)
{
  auto ids = QStringList{};
  for (const auto& key : {"auditOperationIds", "childOperationIds", "operationIds"})
  {
    for (const auto& value : result.value(key).toArray())
    {
      const auto id = value.toString().trimmed();
      if (!id.isEmpty())
      {
        ids.push_back(id);
      }
    }
    if (!ids.isEmpty())
    {
      break;
    }
  }
  if (!undoOperationId.isEmpty())
  {
    ids.push_back(undoOperationId);
  }
  ids.removeDuplicates();
  return QJsonArray::fromStringList(ids);
}

bool operationIsUndoable(
  const std::vector<McpOperationRecord>& history, const QString& operationId)
{
  const auto it = std::ranges::find_if(
    history, [&](const auto& operation) { return operation.operationId == operationId; });
  return it == history.end() ? !operationId.isEmpty() : it->undoable;
}

} // namespace

bool McpBridgeServer::start(const mcp::McpBridgeConfig& config, QString* error)
{
  stop();
  m_config = config;
  m_bridgeInstanceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  m_bridgeStartedAtUtc = QDateTime::currentDateTimeUtc();

  if (m_config.mode == mcp::McpMode::Off)
  {
    return true;
  }

  m_server = std::make_unique<QLocalServer>();
  m_server->setMaxPendingConnections(m_transportLimits.maxConnections);
  connect(
    m_server.get(),
    &QLocalServer::newConnection,
    this,
    &McpBridgeServer::handleNewConnection);

  auto probe = QLocalSocket{};
  probe.connectToServer(m_config.pipeName);
  if (probe.waitForConnected(250))
  {
    if (error)
    {
      *error =
        QString{"Another TrenchBroom MCP instance is already listening on pipe '%1'"}.arg(
          m_config.pipeName);
    }
    m_server.reset();
    return false;
  }

  if (
    probe.error() != QLocalSocket::ServerNotFoundError
    && probe.error() != QLocalSocket::ConnectionRefusedError)
  {
    if (error)
    {
      *error = QString{"Could not verify whether MCP pipe '%1' is active: %2"}.arg(
        m_config.pipeName, probe.errorString());
    }
    m_server.reset();
    return false;
  }

  if (m_server->listen(m_config.pipeName))
  {
    return true;
  }

  if (
    !QLocalServer::removeServer(m_config.pipeName)
    || !m_server->listen(m_config.pipeName))
  {
    if (error)
    {
      *error = QString{"Could not claim inactive MCP pipe '%1': %2"}.arg(
        m_config.pipeName, m_server->errorString());
    }
    m_server.reset();
    return false;
  }

  return true;
}

void McpBridgeServer::clearSessionState()
{
  m_overlayState = QJsonObject{};
  m_session.clear();
  m_pythonExecutionReplays.clear();
  m_pythonExecutionReplayOrder.clear();
}

void McpBridgeServer::stop()
{
  const auto connections = m_connections.values();
  for (auto* socket : connections)
  {
    socket->disconnectFromServer();
    socket->deleteLater();
  }
  m_requestDeadlines.clear();
  m_connections.clear();

  if (m_server)
  {
    m_server->close();
    QLocalServer::removeServer(m_config.pipeName);
    m_server.reset();
  }
  clearSessionState();
}

bool McpBridgeServer::isListening() const
{
  return m_server != nullptr && m_server->isListening();
}

QString McpBridgeServer::pipeName() const
{
  return m_config.pipeName;
}

mcp::McpMode McpBridgeServer::mode() const
{
  return m_config.mode;
}

const QJsonObject& McpBridgeServer::overlayState() const
{
  return m_overlayState;
}

namespace
{

QJsonObject resourceObject(
  const McpOperationRecord& operation, const std::optional<QJsonObject>& liveState)
{
  auto result = QJsonObject{
    {"operationId", operation.operationId},
    {"toolName", operation.toolName},
    {"transactionName", operation.transactionName},
    {"operationKind", operation.operationKind},
    {"documentPath", operation.documentPath},
    {"documentFingerprint", operation.documentFingerprint},
    {"createdAt", operation.createdAt},
    {"createdAtMs", operation.createdAtMs},
    {"changedObjectCount", operation.changedObjectIds.size()},
    {"deletedObjectCount", operation.deletedObjectIds.size()},
    {"undone", operation.undone},
    {"undoable", operation.undoable},
    {"parentOperationId", operation.parentOperationId},
    {"childOperationIds", QJsonArray::fromStringList(operation.childOperationIds)},
    {"summary", operation.summary()},
    {"detail", operation.detail()},
  };
  result.insert(
    "idsDetail",
    "compact; use operation_inspect(detail=ids) or operation_inspect(detail=full) "
    "for changedObjectIds/deletedObjectIds");
  if (liveState)
  {
    for (auto it = liveState->begin(); it != liveState->end(); ++it)
    {
      result.insert(it.key(), it.value());
    }
  }
  return result;
}

} // namespace

std::optional<QJsonObject> McpBridgeServer::readResource(const QString& uri) const
{
  static const auto ReviewPrefix = QString{"tbmcp://review/"};
  if (uri.startsWith(ReviewPrefix))
  {
    const auto it = m_session.reviewResources.find(uri);
    if (it != m_session.reviewResources.end())
    {
      return it->second.resource;
    }
    return m_session.evictedResourceHint(uri);
  }

  static const auto Prefix = QString{"tbmcp://operation/"};
  if (!uri.startsWith(Prefix))
  {
    return std::nullopt;
  }

  const auto operationId = uri.mid(Prefix.size());
  const auto it = std::ranges::find_if(
    m_session.operationHistory,
    [&](const auto& operation) { return operation.operationId == operationId; });
  if (it == m_session.operationHistory.end())
  {
    return m_session.evictedResourceHint(uri);
  }

  if (m_activeMapProvider)
  {
    if (auto* map = m_activeMapProvider())
    {
      return m_session.objectRegistry.externalizeResult(
        *map,
        resourceObject(
          *it,
          m_session.objectRegistry.liveStateJson(
            *map, it->changedObjectIds, it->undone)));
    }
  }

  return resourceObject(*it, std::nullopt);
}

std::optional<QJsonObject> McpBridgeServer::listResources(
  const QString& cursor, QString* error) const
{
  return m_session.listResources(cursor, error);
}

mcp::McpBridgeResponse McpBridgeServer::dispatchToolCall(
  const mcp::McpBridgeRequest& request) const
{
  if (m_dispatchInProgress)
  {
    return makeFailure(
      request,
      mcp::McpErrorCode::Forbidden,
      "MCP bridge is already handling another request; retry after it finishes");
  }

  const auto tool = mcp::findToolDefinition(request.tool);
  if (!tool)
  {
    return makeFailure(
      request,
      mcp::McpErrorCode::ToolNotFound,
      QString{"Unknown MCP tool: %1"}.arg(request.tool));
  }

  if (!tool->implemented)
  {
    return makeFailure(
      request,
      mcp::McpErrorCode::ToolNotFound,
      QString{"MCP tool is registered but not implemented yet: %1"}.arg(request.tool));
  }

  const auto effectiveMode =
    request.requestedMode && mcp::allowsMode(m_config.mode, *request.requestedMode)
      ? *request.requestedMode
      : m_config.mode;
  if (!mcp::canCallTool(*tool, effectiveMode))
  {
    return makeFailure(
      request,
      mcp::McpErrorCode::Forbidden,
      QString{"MCP tool is not available in mode %1"}.arg(mcp::modeName(effectiveMode)));
  }
  if (
    request.tool == "tb_history"
    && (request.params.value("action").toString("status").trimmed().compare(
          "undo", Qt::CaseInsensitive)
          == 0
        || request.params.value("action").toString("status").trimmed().compare(
             "redo", Qt::CaseInsensitive)
             == 0)
    && !mcp::allowsMode(effectiveMode, mcp::McpMode::Edit))
  {
    return makeFailure(
      request,
      mcp::McpErrorCode::Forbidden,
      "tb_history undo and redo require Edit mode");
  }

  struct DispatchGuard
  {
    bool& dispatchInProgress;

    explicit DispatchGuard(bool& i_dispatchInProgress)
      : dispatchInProgress{i_dispatchInProgress}
    {
      dispatchInProgress = true;
    }

    ~DispatchGuard() { dispatchInProgress = false; }
  };

  const auto dispatchGuard = DispatchGuard{m_dispatchInProgress};

  auto params = request.params;
  auto* map = m_activeMapProvider ? m_activeMapProvider() : nullptr;
  if (tool->mutatesDocument)
  {
    const auto expectedDocumentPath =
      params.value("expectedDocumentPath").toString().trimmed();
    if (!expectedDocumentPath.isEmpty())
    {
      const auto actualDocumentPath =
        map != nullptr && !map->path().empty() ? pathAsQString(map->path()) : QString{};
      if (actualDocumentPath != expectedDocumentPath)
      {
        return makeFailure(
          request,
          mcp::McpErrorCode::Forbidden,
          QString{"Active document does not match expectedDocumentPath. Expected '%1', "
                  "actual '%2'."}
            .arg(expectedDocumentPath, actualDocumentPath),
          QJsonObject{
            {"expectedDocumentPath", expectedDocumentPath},
            {"actualDocumentPath", actualDocumentPath},
            {"mutatedDocument", false},
            {"retrySafe", true},
            {"recoveryAction", "activate_expected_document_then_retry"},
            {"processId", static_cast<int>(QCoreApplication::applicationPid())},
            {"bridgeInstanceId", m_bridgeInstanceId},
            {"bridgeStartedAt", m_bridgeStartedAtUtc.toString(Qt::ISODateWithMs)},
            {"httpPort", static_cast<int>(m_config.httpPort)},
          });
      }
    }

    const auto expectedDocumentFingerprint =
      params.value("expectedDocumentFingerprint").toString().trimmed();
    if (!expectedDocumentFingerprint.isEmpty())
    {
      const auto actualDocumentFingerprint =
        map != nullptr ? m_session.objectRegistry.documentFingerprint(*map) : QString{};
      if (actualDocumentFingerprint != expectedDocumentFingerprint)
      {
        return makeFailure(
          request,
          mcp::McpErrorCode::Forbidden,
          QString{"Active document does not match expectedDocumentFingerprint. "
                  "Expected '%1', actual '%2'."}
            .arg(expectedDocumentFingerprint, actualDocumentFingerprint),
          QJsonObject{
            {"expectedDocumentFingerprint", expectedDocumentFingerprint},
            {"actualDocumentFingerprint", actualDocumentFingerprint},
            {"actualDocumentPath",
             map != nullptr && !map->path().empty() ? pathAsQString(map->path())
                                                    : QString{}},
            {"mutatedDocument", false},
            {"retrySafe", true},
            {"recoveryAction", "refresh_status_or_activate_expected_document"},
            {"processId", static_cast<int>(QCoreApplication::applicationPid())},
            {"bridgeInstanceId", m_bridgeInstanceId},
            {"bridgeStartedAt", m_bridgeStartedAtUtc.toString(Qt::ISODateWithMs)},
            {"httpPort", static_cast<int>(m_config.httpPort)},
          });
      }
    }
  }
  if (map != nullptr)
  {
    auto error = QString{};
    const auto internalParams =
      m_session.objectRegistry.internalizeParams(*map, params, error);
    if (!internalParams)
    {
      return makeFailure(request, mcp::McpErrorCode::InvalidParams, error);
    }
    params = *internalParams;
  }

  const auto requestDocumentFingerprint =
    map != nullptr ? m_session.objectRegistry.documentFingerprint(*map) : QString{};
  m_session.rememberDocumentFingerprint(requestDocumentFingerprint);

  const auto metadataBefore = tool->mutatesDocument
                                ? m_session.brushMetadata
                                : std::map<QString, McpBrushMetadataRecord>{};
  const auto modulesBefore =
    tool->mutatesDocument ? m_session.modules : std::map<QString, McpModuleRecord>{};
  auto result = m_toolHandler(request.tool, params);
  if (result.ok)
  {
    auto* resultMap = m_activeMapProvider ? m_activeMapProvider() : nullptr;
    if (resultMap != nullptr)
    {
      if (tool->mutatesDocument && result.result.value("mutatedDocument").toBool(true))
      {
        const auto undoOperationId = mutationUndoOperationId(result.result);
        reconcileMcpSessionForMap(
          *resultMap,
          m_session.brushMetadata,
          m_session.modules,
          m_session.objectRegistry,
          undoOperationId);
        const auto historyToolManagesSessionDelta =
          request.tool == "history_undo_mcp"
          || request.tool == "history_undo_to_operation"
          || request.tool == "history_redo_mcp";
        if (!undoOperationId.isEmpty() && !historyToolManagesSessionDelta)
        {
          attachMcpSessionDelta(
            m_session.operationHistory,
            undoOperationId,
            metadataBefore,
            modulesBefore,
            m_session.brushMetadata,
            m_session.modules);
        }
        result.result.insert("undoOperationId", undoOperationId);
        result.result.insert(
          "undoable",
          !undoOperationId.isEmpty()
            && operationIsUndoable(m_session.operationHistory, undoOperationId));
        result.result.insert(
          "auditOperationIds", mutationAuditOperationIds(result.result, undoOperationId));
      }
      if (tool->mutatesDocument)
      {
        const auto documentModified = result.result.value("mutatedDocument").toBool(true);
        result.result.insert(
          "completionState",
          QJsonObject{
            {"documentModified", documentModified},
            {"saveRequired", documentModified},
            {"visualReview", "not_run"},
            {"bspCompile", "not_run"},
          });
      }
      auto externalResult =
        m_session.objectRegistry.externalizeResult(*resultMap, result.result);
      syncOperationHistoryWithExternalResult(
        m_session.operationHistory, *resultMap, m_session.objectRegistry, externalResult);
      const auto activeDocumentFingerprint =
        m_session.objectRegistry.documentFingerprint(*resultMap);
      m_session.cacheReviewResource(externalResult, activeDocumentFingerprint);
      m_session.prune(activeDocumentFingerprint, QDateTime::currentMSecsSinceEpoch());
      return mcp::McpBridgeResponse::success(request.id, std::move(externalResult));
    }
    m_session.cacheReviewResource(result.result);
    m_session.prune({}, QDateTime::currentMSecsSinceEpoch());
    return mcp::McpBridgeResponse::success(request.id, result.result);
  }
  m_session.prune(requestDocumentFingerprint, QDateTime::currentMSecsSinceEpoch());
  return mcp::McpBridgeResponse::failure(request.id, result.error);
}

void McpBridgeServer::startRequestDeadline(QLocalSocket& socket)
{
  auto* timer = new QTimer{&socket};
  timer->setSingleShot(true);
  connect(timer, &QTimer::timeout, this, [this, socketPtr = &socket]() {
    if (!m_connections.contains(socketPtr))
    {
      return;
    }
    rejectAndDisconnect(*socketPtr, "MCP bridge request timed out");
  });
  m_requestDeadlines.insert(&socket, timer);
  timer->start(m_transportLimits.incompleteRequestTimeoutMs);
}

void McpBridgeServer::restartRequestDeadline(QLocalSocket& socket)
{
  if (auto* timer = m_requestDeadlines.value(&socket))
  {
    timer->start(m_transportLimits.incompleteRequestTimeoutMs);
  }
}

void McpBridgeServer::rejectAndDisconnect(
  QLocalSocket& socket, const QString& message) const
{
  writeResponse(
    socket,
    mcp::McpBridgeResponse::failure(
      {}, mcp::McpError{mcp::McpErrorCode::InvalidRequest, message}));
  socket.disconnectFromServer();
}

void McpBridgeServer::removeConnection(QLocalSocket& socket)
{
  m_requestDeadlines.remove(&socket);
  m_connections.remove(&socket);
  socket.deleteLater();
}

mcp::McpBridgeResponse McpBridgeServer::dispatchRequest(
  const mcp::McpBridgeRequest& request) const
{
  if (request.type == mcp::McpBridgeRequestType::ToolCall)
  {
    return dispatchToolCall(request);
  }

  if (request.type == mcp::McpBridgeRequestType::ResourcesList)
  {
    const auto cursorValue = request.params.value("cursor");
    if (!cursorValue.isUndefined() && !cursorValue.isString())
    {
      return makeFailure(
        request, mcp::McpErrorCode::InvalidParams, "Resource cursor must be a string");
    }
    auto error = QString{};
    const auto result = listResources(cursorValue.toString(), &error);
    return result ? mcp::McpBridgeResponse::success(request.id, *result)
                  : makeFailure(request, mcp::McpErrorCode::InvalidParams, error);
  }

  const auto uriValue = request.params.value("uri");
  if (!uriValue.isString() || uriValue.toString().trimmed().isEmpty())
  {
    return makeFailure(
      request,
      mcp::McpErrorCode::InvalidParams,
      "Resource read requires a non-empty uri");
  }
  const auto resource = readResource(uriValue.toString());
  return resource ? mcp::McpBridgeResponse::success(request.id, *resource)
                  : makeFailure(
                      request,
                      mcp::McpErrorCode::InvalidParams,
                      QString{"Resource not found: %1"}.arg(uriValue.toString()));
}

void McpBridgeServer::handleNewConnection()
{
  while (auto* socket = m_server->nextPendingConnection())
  {
    socket->setParent(this);
    connect(socket, &QLocalSocket::disconnected, this, [this, socket]() {
      if (m_connections.contains(socket))
      {
        removeConnection(*socket);
      }
      else
      {
        socket->deleteLater();
      }
    });

    if (m_connections.size() >= m_transportLimits.maxConnections)
    {
      rejectAndDisconnect(*socket, "MCP bridge connection limit reached");
      continue;
    }

    m_connections.insert(socket);
    socket->setReadBufferSize(m_transportLimits.maxRequestBytes + 1);
    startRequestDeadline(*socket);
    connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
      handleSocketReadyRead(*socket);
    });
  }
}

void McpBridgeServer::handleSocketReadyRead(QLocalSocket& socket)
{
  if (
    !socket.canReadLine() && socket.bytesAvailable() > m_transportLimits.maxRequestBytes)
  {
    rejectAndDisconnect(socket, "MCP bridge request too large");
    return;
  }

  while (socket.canReadLine())
  {
    auto line = socket.readLine();
    if (line.endsWith('\n'))
    {
      line.chop(1);
    }
    if (line.size() > m_transportLimits.maxRequestBytes)
    {
      rejectAndDisconnect(socket, "MCP bridge request too large");
      return;
    }

    auto parseError = QJsonParseError{};
    const auto document = QJsonDocument::fromJson(line.trimmed(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
      writeResponse(
        socket,
        mcp::McpBridgeResponse::failure(
          {},
          mcp::McpError{
            mcp::McpErrorCode::InvalidRequest, "Invalid MCP bridge JSON request"}));
      restartRequestDeadline(socket);
      continue;
    }

    auto error = QString{};
    const auto request = mcp::bridgeRequestFromJson(document.object(), &error);
    if (!request)
    {
      writeResponse(
        socket,
        mcp::McpBridgeResponse::failure(
          {}, mcp::McpError{mcp::McpErrorCode::InvalidRequest, error}));
      restartRequestDeadline(socket);
      continue;
    }

    writeResponse(socket, dispatchRequest(*request));
    restartRequestDeadline(socket);
  }
}

void McpBridgeServer::writeResponse(
  QLocalSocket& socket, const mcp::McpBridgeResponse& response) const
{
  socket.write(QJsonDocument{mcp::toJson(response)}.toJson(QJsonDocument::Compact));
  socket.write("\n");
  socket.flush();
}

} // namespace tb::ui
