#pragma once

#include "ui/PluginInspector.h"
#include "ui/python/PythonExecutionContext.h"
#include "ui/python/PythonPluginManifest.h"

#include <memory>
#include <string>
#include <vector>

struct _object;
using PyObject = _object;

class QWidget;

namespace tb::ui
{
class PythonSessionTimer;

class PythonPluginSession
{
private:
  PythonPluginManifest m_manifest;
  PythonExecutionContext m_context;
  std::vector<int> m_callbackTokens;
  std::vector<PluginPanelRegistration> m_pluginPanels;
  std::vector<std::unique_ptr<PythonSessionTimer>> m_timers;
  std::string m_error;

public:
  explicit PythonPluginSession(
    PythonPluginManifest manifest, PythonExecutionContext context);
  ~PythonPluginSession();

  const PythonPluginManifest& manifest() const;
  const std::string& pluginId() const;
  PythonExecutionContext& context();
  const PythonExecutionContext& context() const;

  void addCallbackToken(int token);
  std::vector<int> takeCallbackTokens();

  void addPluginPanel(PluginPanelRegistration panel);
  void closePluginPanels();

  int addIntervalTimer(PyObject* callback, int milliseconds, bool singleShot);
  void clearTimer(int token);
  void clearTimers();

  const std::string& error() const;
  void setError(std::string error);
  void clearError();
};

} // namespace tb::ui
