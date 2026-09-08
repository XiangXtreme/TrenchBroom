#pragma once

#include "ui/mcp/McpBridgeServer.h"

#include <map>

namespace tb::ui
{
class McpPythonExecutor
{
private:
  struct Replay
  {
    QByteArray requestHash;
    McpBridgeToolResult response;
    qsizetype bytes;
    bool expired;
  };

  AppController& m_appController;
  automation::AutomationObjectRegistry& m_objectRegistry;
  std::map<QString, Replay> m_pythonExecutionReplays;
  QStringList m_pythonExecutionReplayOrder;
  qsizetype m_retainedBytes = 0;

public:
  McpPythonExecutor(
    AppController& appController, automation::AutomationObjectRegistry& objectRegistry);
  McpBridgeToolResult execute(const QJsonObject& params, const QJsonObject& active);
  void clear();

private:
  void cacheResponse(
    const QString& id, const QByteArray& hash, const McpBridgeToolResult& response);
};
} // namespace tb::ui
