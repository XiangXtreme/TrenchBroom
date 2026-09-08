#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "ui/python/PythonCompletionEngine.h"
#include "ui/python/PythonExecutionContext.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace tb::ui
{
class PythonPluginSession;
struct PythonRuntimeState;

struct PythonMcpExecutionRequest
{
  QString source;
  QString filename;
  QJsonObject arguments;
  QString transactionName = "MCP Python";
  int timeoutMs = 30'000;
  bool transactional = true;
};

struct PythonMcpExecutionResult
{
  bool ok = false;
  QJsonValue value;
  QString error;
  bool executed = false;
  bool committed = false;
  bool mutatedDocument = false;
  bool rolledBack = false;
  bool timedOut = false;
  QJsonObject completedActions;
  QByteArray stdoutText;
  QByteArray stderrText;
  qsizetype discardedLogBytes = 0;
};

class PythonRuntime
{
private:
  PythonRuntime();

public:
  static PythonRuntime& instance();
  ~PythonRuntime();

  bool ensureInitialized();
  bool runScript(
    const PythonExecutionContext& context, const std::filesystem::path& path);
  bool runScript(PythonPluginSession& session);
  bool runConsoleCommand(const PythonExecutionContext& context, std::string_view source);
  PythonMcpExecutionResult runMcpScript(
    const PythonExecutionContext& context, const PythonMcpExecutionRequest& request);
  PythonCompletionRoot consoleCompletionRoot(
    MapWindow& mapWindow, std::string_view name) const;
  void runCallback(PythonPluginSession& session, void* callback);
  void emitEvent(const std::string& eventName, MapWindow& mapWindow);
  void emitEvent(
    const std::string& eventName, MapWindow& mapWindow, bool initializeIfNeeded);
  void cleanupPlugin(const std::string& pluginId);
  void cleanupPluginSession(PythonPluginSession& session);
  void cleanupDocument(MapWindow& mapWindow);

  const std::string& lastError() const;
  std::string formatCurrentException() const;

private:
  std::string m_lastError;
  std::unique_ptr<PythonRuntimeState> m_state;

  bool runScript(
    const PythonExecutionContext& context,
    const std::filesystem::path& path,
    PythonPluginSession* session);
  bool installApiModule();
  bool prependSysPath(const std::filesystem::path& path);
};

PythonExecutionContext* currentPythonExecutionContext();
PythonPluginSession* currentPythonPluginSession();
void recordCompletedPythonAction(const char* action);

} // namespace tb::ui
