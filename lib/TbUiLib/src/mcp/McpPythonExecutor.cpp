#include "McpPythonExecutor.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

#include "McpThinBridgeTools.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/python/PythonRuntime.h"

namespace tb::ui
{
McpPythonExecutor::McpPythonExecutor(
  AppController& appController, automation::AutomationObjectRegistry& objectRegistry)
  : m_appController{appController}
  , m_objectRegistry{objectRegistry}
{
}

McpBridgeToolResult McpPythonExecutor::execute(
  const QJsonObject& params, const QJsonObject& active)
{
  const auto executionId = params.value("executionId").toString().trimmed();
  if (executionId.isEmpty() || executionId.size() > 256)
  {
    return invalidParamsFailure(
      "tb_execute_python requires executionId with 1..256 characters");
  }
  const auto hasCode = params.value("code").isString();
  const auto hasPath = params.value("path").isString();
  if (hasCode == hasPath)
  {
    return invalidParamsFailure("Provide exactly one of code or path");
  }
  auto source = QString{};
  auto filename = QString{"<mcp-python:%1>"}.arg(executionId);
  if (hasCode)
  {
    source = params.value("code").toString();
  }
  else
  {
    const auto path = params.value("path").toString();
    const auto info = QFileInfo{path};
    if (!info.isAbsolute() || !info.isFile())
    {
      return invalidParamsFailure("MCP Python path must be an existing absolute file");
    }
    auto file = QFile{info.absoluteFilePath()};
    if (!file.open(QIODevice::ReadOnly))
    {
      return invalidParamsFailure("Could not read MCP Python file");
    }
    if (file.size() > 256 * 1024)
    {
      return invalidParamsFailure("MCP Python source exceeds 256 KiB");
    }
    source = QString::fromUtf8(file.read(256 * 1024 + 1));
    filename = info.absoluteFilePath();
  }
  if (source.isEmpty() || source.toUtf8().size() > 256 * 1024)
  {
    return invalidParamsFailure(
      "MCP Python source must be non-empty and at most 256 KiB");
  }
  if (params.value("arguments").isUndefined())
  {
    // The execution request defaults arguments to an empty JSON object.
  }
  else if (!params.value("arguments").isObject())
  {
    return invalidParamsFailure("MCP Python arguments must be an object");
  }
  const auto mode = params.value("mode").toString("transaction").trimmed().toLower();
  if (mode != "transaction" && mode != "action")
  {
    return invalidParamsFailure("MCP Python mode must be transaction or action");
  }
  const auto timeoutMs = params.value("timeoutMs").toInt(30'000);
  if (timeoutMs < 1 || timeoutMs > 90'000)
  {
    return invalidParamsFailure("MCP Python timeoutMs must be between 1 and 90000");
  }
  const auto document = params.value("document");
  if (!document.isObject())
  {
    return invalidParamsFailure("MCP Python transaction mode requires document");
  }
  const auto requested = document.toObject();
  const auto sourceHash = QString::fromLatin1(
    QCryptographicHash::hash(source.toUtf8(), QCryptographicHash::Sha256).toHex());
  const auto requestHash = QCryptographicHash::hash(
    QJsonDocument{QJsonObject{
                    {"sourceHash", sourceHash},
                    {"filename", filename},
                    {"arguments", params.value("arguments").toObject()},
                    {"document", requested},
                    {"mode", mode},
                    {"name", params.value("name").toString("MCP Python")},
                    {"timeoutMs", timeoutMs},
                  }}
      .toJson(QJsonDocument::Compact),
    QCryptographicHash::Sha256);
  if (const auto it = m_pythonExecutionReplays.find(executionId);
      it != m_pythonExecutionReplays.end())
  {
    if (it->second.requestHash != requestHash)
    {
      return McpBridgeToolResult::failure(
        mcp::McpErrorCode::InvalidParams,
        "executionId was already used with different request content",
        QJsonObject{{"executionId", executionId}, {"retrySafe", false}});
    }
    auto replay = it->second.response;
    if (replay.ok)
    {
      replay.result.insert("historicalReplay", true);
    }
    else
    {
      replay.error.details.insert("historicalReplay", true);
    }
    return replay;
  }
  auto* mapWindow = m_appController.mapWindowManager().topMapWindow();
  if (mapWindow == nullptr)
  {
    return noActiveDocumentFailure();
  }
  const auto expectedFingerprint = requested.value("fingerprint").toString().trimmed();
  const auto actualFingerprint = active.value("documentFingerprint").toString();
  if (expectedFingerprint.isEmpty() || expectedFingerprint != actualFingerprint)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::Forbidden,
      "MCP Python document fingerprint does not match the active document",
      QJsonObject{
        {"mutatedDocument", false},
        {"retrySafe", true},
        {"expectedFingerprint", expectedFingerprint},
        {"actualFingerprint", actualFingerprint},
      });
  }
  const auto expectedPath = requested.value("path").toString().trimmed();
  const auto actualPath = active.value("path").toString();
  if (
    (!actualPath.isEmpty() && expectedPath.isEmpty())
    || (!expectedPath.isEmpty() && expectedPath != actualPath))
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::Forbidden,
      actualPath.isEmpty() ? "MCP Python document path does not match the active document"
                           : "MCP Python requires the saved document path",
      QJsonObject{
        {"mutatedDocument", false},
        {"retrySafe", true},
        {"expectedPath", expectedPath},
        {"actualPath", actualPath},
      });
  }
  auto context = PythonExecutionContext{};
  context.mapWindow = mapWindow;
  context.document = &mapWindow->document();
  context.appController = &m_appController;
  context.currentMapView = mapWindow->currentMapViewBase();
  context.logger = &mapWindow->pythonLogger();
  context.objectRegistry = &m_objectRegistry;
  context.mcpExecution = true;
  context.allowNonTransactionalActions = mode == "action";
  context.allowPersistentUi = false;
  auto elapsed = QElapsedTimer{};
  elapsed.start();
  const auto execution = PythonRuntime::instance().runMcpScript(
    context,
    PythonMcpExecutionRequest{
      source,
      filename,
      params.value("arguments").toObject(),
      params.value("name").toString("MCP Python"),
      timeoutMs,
      mode == "transaction",
    });
  auto receipt = QJsonObject{
    {"executionId", executionId},
    {"bridgeInstanceId", active.value("bridgeInstanceId")},
    {"sourceHash", sourceHash},
    {"document", active},
    {"mode", mode},
    {"status",
     execution.ok         ? "completed"
     : execution.executed ? "failed"
                          : "rejected"},
    {"durationMs", elapsed.elapsed()},
    {"mutatedDocument", execution.mutatedDocument},
    {"partialMutation",
     mode == "action" && !execution.ok
       && (execution.mutatedDocument || !execution.completedActions.isEmpty())},
    {"completedActions", execution.completedActions},
    {"rolledBack", execution.rolledBack},
    {"retrySafe", !execution.executed},
    {"logs",
     QJsonObject{
       {"stdoutBytes", execution.stdoutText.size()},
       {"stderrBytes", execution.stderrText.size()},
       {"discardedBytes", execution.discardedLogBytes},
       {"stdout", QString::fromUtf8(execution.stdoutText.left(4096))},
       {"stderr", QString::fromUtf8(execution.stderrText.left(4096))},
       {"previewTruncated",
        execution.stdoutText.size() > 4096 || execution.stderrText.size() > 4096},
       {"truncated", execution.discardedLogBytes > 0},
     }},
  };
  if (!execution.ok)
  {
    receipt.insert("error", execution.error);
    const auto response = McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "MCP Python execution failed", receipt);
    cacheResponse(executionId, requestHash, response);
    return response;
  }
  receipt.insert("result", execution.value);
  const auto response = McpBridgeToolResult::success(std::move(receipt));
  cacheResponse(executionId, requestHash, response);
  return response;
}

void McpPythonExecutor::clear()
{
  m_pythonExecutionReplays.clear();
  m_pythonExecutionReplayOrder.clear();
  m_retainedBytes = 0;
}

void McpPythonExecutor::cacheResponse(
  const QString& id, const QByteArray& hash, const McpBridgeToolResult& response)
{
  const auto responseBytes = [](const auto& value) {
    return QJsonDocument{value.ok ? value.result : value.error.details}
      .toJson(QJsonDocument::Compact)
      .size();
  };
  while (m_pythonExecutionReplayOrder.size() >= 1024)
  {
    const auto oldest = m_pythonExecutionReplayOrder.takeFirst();
    m_retainedBytes -= m_pythonExecutionReplays.at(oldest).bytes;
    m_pythonExecutionReplays.erase(oldest);
  }
  const auto bytes = responseBytes(response);
  m_pythonExecutionReplayOrder.push_back(id);
  m_pythonExecutionReplays.emplace(id, Replay{hash, response, bytes, false});
  m_retainedBytes += bytes;
  for (const auto& oldest : m_pythonExecutionReplayOrder)
  {
    if (m_retainedBytes <= 16 * 1024 * 1024)
      break;
    auto& entry = m_pythonExecutionReplays.at(oldest);
    if (entry.expired)
      continue;
    // Keep request identity after discarding a large payload. A replay must never
    // turn into another edit merely because its result exceeded the cache budget.
    m_retainedBytes -= entry.bytes;
    entry.response = McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError,
      "Execution receipt expired; inspect the editor before issuing a new executionId",
      QJsonObject{
        {"executionId", oldest}, {"status", "receipt_expired"}, {"retrySafe", false}});
    entry.bytes = responseBytes(entry.response);
    entry.expired = true;
    m_retainedBytes += entry.bytes;
  }
}
} // namespace tb::ui
