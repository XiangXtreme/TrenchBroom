#include "ui/python/PythonPluginSession.h"

#include <QTimer>

#include "ui/python/PythonRuntime.h"

#if defined(slots)
#undef slots
#endif

#include <Python.h>

#include <algorithm>
#include <utility>

namespace tb::ui
{

class PythonSessionTimer : public QObject
{
private:
  PythonPluginSession& m_session;
  int m_token = 0;
  PyObject* m_callback = nullptr;
  QTimer* m_timer = nullptr;

public:
  PythonSessionTimer(
    PythonPluginSession& session,
    const int token,
    PyObject* callback,
    const int milliseconds,
    const bool singleShot)
    : m_session{session}
    , m_token{token}
    , m_callback{callback}
    , m_timer{new QTimer{this}}
  {
    Py_INCREF(m_callback);
    m_timer->setInterval(milliseconds);
    m_timer->setSingleShot(singleShot);
    connect(m_timer, &QTimer::timeout, this, &PythonSessionTimer::run);
    m_timer->start();
  }

  ~PythonSessionTimer() override
  {
    m_timer->stop();
    auto gil = PyGILState_Ensure();
    Py_DECREF(m_callback);
    PyGILState_Release(gil);
  }

  int token() const { return m_token; }

private:
  void run()
  {
    PythonRuntime::instance().runCallback(m_session, m_callback);
    if (m_timer->isSingleShot())
    {
      m_session.clearTimer(m_token);
    }
  }
};

namespace
{

int g_nextTimerToken = 1;

} // namespace

PythonPluginSession::PythonPluginSession(
  PythonPluginManifest manifest, PythonExecutionContext context)
  : m_manifest{std::move(manifest)}
  , m_context{std::move(context)}
{
  m_context.pluginId = m_manifest.id;
  m_context.pluginDirectory = m_manifest.directory;
  m_context.scriptPath = m_manifest.directory / m_manifest.entry;
}

PythonPluginSession::~PythonPluginSession()
{
  clearTimers();
  closePluginPanels();
}

const PythonPluginManifest& PythonPluginSession::manifest() const
{
  return m_manifest;
}

const std::string& PythonPluginSession::pluginId() const
{
  return m_manifest.id;
}

PythonExecutionContext& PythonPluginSession::context()
{
  return m_context;
}

const PythonExecutionContext& PythonPluginSession::context() const
{
  return m_context;
}

void PythonPluginSession::addCallbackToken(const int token)
{
  m_callbackTokens.push_back(token);
}

std::vector<int> PythonPluginSession::takeCallbackTokens()
{
  auto result = std::move(m_callbackTokens);
  m_callbackTokens.clear();
  return result;
}

void PythonPluginSession::addPluginPanel(PluginPanelRegistration panel)
{
  m_pluginPanels.push_back(std::move(panel));
}

void PythonPluginSession::closePluginPanels()
{
  for (const auto& panel : m_pluginPanels)
  {
    panel.close();
  }
  m_pluginPanels.clear();
}

int PythonPluginSession::addIntervalTimer(
  PyObject* callback, const int milliseconds, const bool singleShot)
{
  const auto token = g_nextTimerToken++;
  m_timers.push_back(std::make_unique<PythonSessionTimer>(
    *this, token, callback, milliseconds, singleShot));
  return token;
}

void PythonPluginSession::clearTimer(const int token)
{
  m_timers.erase(
    std::remove_if(
      m_timers.begin(),
      m_timers.end(),
      [&](const auto& timer) {
        return static_cast<PythonSessionTimer*>(timer.get())->token() == token;
      }),
    m_timers.end());
}

void PythonPluginSession::clearTimers()
{
  m_timers.clear();
}

const std::string& PythonPluginSession::error() const
{
  return m_error;
}

void PythonPluginSession::setError(std::string error)
{
  m_error = std::move(error);
}

void PythonPluginSession::clearError()
{
  m_error.clear();
}

} // namespace tb::ui
