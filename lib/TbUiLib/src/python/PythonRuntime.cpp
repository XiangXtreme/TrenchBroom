#include "ui/python/PythonRuntime.h"

#include "base/Logger.h"
#include "mdl/Map.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/python/PythonApiCatalog.h"
#include "ui/python/PythonApiModule.h"
#include "ui/python/PythonPluginSession.h"

#include "kd/invoke.h"

#if defined(slots)
#undef slots
#endif

#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

#include <Python.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace tb::ui
{
struct PythonRuntimeState
{
  std::unordered_map<MapWindow*, PyObject*> consoleGlobals;
};

namespace
{
thread_local PythonExecutionContext* g_currentExecutionContext = nullptr;
thread_local PythonPluginSession* g_currentPluginSession = nullptr;

struct PythonMcpLogCapture
{
  static constexpr auto MaxBytes = qsizetype{1024 * 1024};

  PythonMcpExecutionResult& execution;

  void append(const char* text, const qsizetype size, const bool isError)
  {
    const auto retainedBytes = execution.stdoutText.size() + execution.stderrText.size();
    const auto appendBytes = std::clamp(MaxBytes - retainedBytes, qsizetype{0}, size);
    auto& destination = isError ? execution.stderrText : execution.stdoutText;
    destination.append(text, appendBytes);
    execution.discardedLogBytes += size - appendBytes;
  }
};

thread_local PythonMcpLogCapture* g_currentMcpLogCapture = nullptr;

class ScopedMcpLogCapture
{
private:
  PythonMcpLogCapture* m_previous = nullptr;

public:
  explicit ScopedMcpLogCapture(PythonMcpLogCapture& capture)
    : m_previous{g_currentMcpLogCapture}
  {
    g_currentMcpLogCapture = &capture;
  }

  ~ScopedMcpLogCapture() { g_currentMcpLogCapture = m_previous; }
};

struct PyRuntimeLogWriter
{
  PyObject_HEAD int isError = 0;
};

PyTypeObject* g_logWriterType = nullptr;

class ScopedExecutionContext
{
private:
  std::optional<PythonExecutionContext> m_ownedContext;
  PythonExecutionContext* m_contextPtr = nullptr;
  PythonExecutionContext* m_previous = nullptr;
  PythonPluginSession* m_previousSession = nullptr;

public:
  ScopedExecutionContext(
    const PythonExecutionContext& context, PythonPluginSession* session = nullptr)
    : m_ownedContext{session == nullptr ? std::make_optional(context) : std::nullopt}
    , m_contextPtr{session != nullptr ? &session->context() : &*m_ownedContext}
    , m_previous{g_currentExecutionContext}
    , m_previousSession{g_currentPluginSession}
  {
    g_currentExecutionContext = m_contextPtr;
    g_currentPluginSession = session;
  }

  ~ScopedExecutionContext()
  {
    g_currentExecutionContext = m_previous;
    g_currentPluginSession = m_previousSession;
  }
};

class ScopedSysPath
{
private:
  bool m_inserted = false;

public:
  explicit ScopedSysPath(const std::filesystem::path& path)
  {
    auto* sysPath = PySys_GetObject("path");
    if (sysPath == nullptr || !PyList_Check(sysPath))
    {
      return;
    }

    const auto pathStr = path.u8string();
    auto* pyPath = PyUnicode_FromStringAndSize(
      reinterpret_cast<const char*>(pathStr.c_str()),
      static_cast<Py_ssize_t>(pathStr.size()));
    if (pyPath == nullptr)
    {
      return;
    }

    m_inserted = PyList_Insert(sysPath, 0, pyPath) == 0;
    Py_DECREF(pyPath);
  }

  ~ScopedSysPath()
  {
    if (!m_inserted)
    {
      return;
    }

    auto* sysPath = PySys_GetObject("path");
    if (sysPath != nullptr && PyList_Check(sysPath) && PyList_Size(sysPath) > 0)
    {
      PySequence_DelItem(sysPath, 0);
    }
  }
};

struct McpExecutionDeadline
{
  QElapsedTimer timer;
  int timeoutMs = 0;
  bool timedOut = false;
};

int mcpExecutionDeadlineTrace(PyObject* object, PyFrameObject*, int, PyObject*)
{
  auto* deadline = static_cast<McpExecutionDeadline*>(
    PyCapsule_GetPointer(object, "trenchbroom.mcpExecutionDeadline"));
  if (deadline == nullptr)
  {
    return -1;
  }
  if (!deadline->timer.hasExpired(deadline->timeoutMs))
  {
    return 0;
  }

  deadline->timedOut = true;
  PyErr_SetString(
    PyExc_TimeoutError, "MCP Python execution exceeded its cooperative timeout");
  return -1;
}

class ScopedMcpExecutionDeadline
{
private:
  McpExecutionDeadline m_deadline;
  PyObject* m_sys = nullptr;
  PyObject* m_previousTrace = nullptr;
  bool m_installed = false;

public:
  explicit ScopedMcpExecutionDeadline(const int timeoutMs)
  {
    m_deadline.timeoutMs = timeoutMs;
    m_deadline.timer.start();
    m_sys = PyImport_ImportModule("sys");
    if (m_sys == nullptr)
    {
      return;
    }
    m_previousTrace = PyObject_CallMethod(m_sys, "gettrace", nullptr);
    if (m_previousTrace == nullptr)
    {
      return;
    }
    auto* capsule =
      PyCapsule_New(&m_deadline, "trenchbroom.mcpExecutionDeadline", nullptr);
    if (capsule == nullptr)
    {
      return;
    }
    PyEval_SetTrace(mcpExecutionDeadlineTrace, capsule);
    Py_DECREF(capsule);
    m_installed = !PyErr_Occurred();
  }

  ~ScopedMcpExecutionDeadline()
  {
    if (m_installed)
    {
      PyEval_SetTrace(nullptr, nullptr);
      auto* result = PyObject_CallMethod(m_sys, "settrace", "O", m_previousTrace);
      Py_XDECREF(result);
      PyErr_Clear();
    }
    Py_XDECREF(m_previousTrace);
    Py_XDECREF(m_sys);
  }

  bool valid() const { return m_installed; }
  bool hasExpired()
  {
    m_deadline.timedOut =
      m_deadline.timedOut || m_deadline.timer.hasExpired(m_deadline.timeoutMs);
    return m_deadline.timedOut;
  }
};

PyObject* pythonObjectFromJson(const QJsonValue& value)
{
  if (value.isNull() || value.isUndefined())
  {
    Py_RETURN_NONE;
  }
  if (value.isBool())
  {
    return PyBool_FromLong(value.toBool() ? 1 : 0);
  }
  if (value.isDouble())
  {
    return PyFloat_FromDouble(value.toDouble());
  }
  if (value.isString())
  {
    const auto utf8 = value.toString().toUtf8();
    return PyUnicode_FromStringAndSize(utf8.constData(), utf8.size());
  }
  if (value.isArray())
  {
    const auto array = value.toArray();
    auto* result = PyList_New(array.size());
    if (result == nullptr)
    {
      return nullptr;
    }
    for (auto i = 0; i < array.size(); ++i)
    {
      auto* item = pythonObjectFromJson(array.at(i));
      if (item == nullptr)
      {
        Py_DECREF(result);
        return nullptr;
      }
      PyList_SET_ITEM(result, i, item);
    }
    return result;
  }

  auto* result = PyDict_New();
  if (result == nullptr)
  {
    return nullptr;
  }
  const auto object = value.toObject();
  for (auto it = object.begin(); it != object.end(); ++it)
  {
    auto* item = pythonObjectFromJson(it.value());
    if (item == nullptr)
    {
      Py_DECREF(result);
      return nullptr;
    }
    const auto key = it.key().toUtf8();
    const auto inserted = PyDict_SetItemString(result, key.constData(), item);
    Py_DECREF(item);
    if (inserted != 0)
    {
      Py_DECREF(result);
      return nullptr;
    }
  }
  return result;
}

std::optional<QJsonValue> jsonValueFromPython(
  PyObject* object, const int depth, QString& error)
{
  if (depth > 32)
  {
    error = "Python result exceeds the supported nesting depth";
    return std::nullopt;
  }
  if (object == Py_None)
  {
    return QJsonValue{QJsonValue::Null};
  }
  if (PyBool_Check(object))
  {
    return QJsonValue{object == Py_True};
  }
  if (PyLong_Check(object))
  {
    const auto value = PyLong_AsLongLong(object);
    if (PyErr_Occurred())
    {
      PyErr_Clear();
      error = "Python integer result is outside the JSON number range";
      return std::nullopt;
    }
    return QJsonValue{static_cast<double>(value)};
  }
  if (PyFloat_Check(object))
  {
    const auto value = PyFloat_AsDouble(object);
    if (!std::isfinite(value))
    {
      error = "Python result contains a non-finite number";
      return std::nullopt;
    }
    return QJsonValue{value};
  }
  if (PyUnicode_Check(object))
  {
    auto size = Py_ssize_t{0};
    const auto* value = PyUnicode_AsUTF8AndSize(object, &size);
    if (value == nullptr)
    {
      PyErr_Clear();
      error = "Python string result cannot be encoded as UTF-8";
      return std::nullopt;
    }
    return QJsonValue{QString::fromUtf8(value, static_cast<qsizetype>(size))};
  }
  if (PyList_Check(object) || PyTuple_Check(object))
  {
    const auto size = PySequence_Size(object);
    auto array = QJsonArray{};
    for (auto i = Py_ssize_t{0}; i < size; ++i)
    {
      auto* item = PySequence_GetItem(object, i);
      if (item == nullptr)
      {
        error = "Python sequence result could not be read";
        return std::nullopt;
      }
      const auto json = jsonValueFromPython(item, depth + 1, error);
      Py_DECREF(item);
      if (!json)
      {
        return std::nullopt;
      }
      array.push_back(*json);
    }
    return QJsonValue{array};
  }
  if (PyDict_Check(object))
  {
    auto result = QJsonObject{};
    auto position = Py_ssize_t{0};
    PyObject* key = nullptr;
    PyObject* value = nullptr;
    while (PyDict_Next(object, &position, &key, &value))
    {
      if (!PyUnicode_Check(key))
      {
        error = "Python result object keys must be strings";
        return std::nullopt;
      }
      auto keySize = Py_ssize_t{0};
      const auto* keyText = PyUnicode_AsUTF8AndSize(key, &keySize);
      if (keyText == nullptr)
      {
        PyErr_Clear();
        error = "Python result object key cannot be encoded as UTF-8";
        return std::nullopt;
      }
      const auto json = jsonValueFromPython(value, depth + 1, error);
      if (!json)
      {
        return std::nullopt;
      }
      result.insert(QString::fromUtf8(keyText, static_cast<qsizetype>(keySize)), *json);
    }
    return QJsonValue{result};
  }

  error = "Python result must be JSON-compatible";
  return std::nullopt;
}

class ScopedStdStreamRedirect
{
private:
  PyObject* m_oldStdout = nullptr;
  PyObject* m_oldStderr = nullptr;

public:
  ScopedStdStreamRedirect()
  {
    m_oldStdout = PySys_GetObject("stdout");
    m_oldStderr = PySys_GetObject("stderr");

    Py_XINCREF(m_oldStdout);
    Py_XINCREF(m_oldStderr);

    auto* newStdout = createLogWriterObject(0);
    auto* newStderr = createLogWriterObject(1);
    if (newStdout != nullptr && newStderr != nullptr)
    {
      PySys_SetObject("stdout", newStdout);
      PySys_SetObject("stderr", newStderr);
    }
    Py_XDECREF(newStdout);
    Py_XDECREF(newStderr);
  }

  ~ScopedStdStreamRedirect()
  {
    if (m_oldStdout != nullptr)
    {
      PySys_SetObject("stdout", m_oldStdout);
      Py_DECREF(m_oldStdout);
    }
    if (m_oldStderr != nullptr)
    {
      PySys_SetObject("stderr", m_oldStderr);
      Py_DECREF(m_oldStderr);
    }
  }

private:
  static PyObject* createLogWriterObject(const int isError)
  {
    if (g_logWriterType == nullptr)
    {
      return nullptr;
    }

    auto* object = reinterpret_cast<PyRuntimeLogWriter*>(
      g_logWriterType->tp_alloc(g_logWriterType, 0));
    if (object != nullptr)
    {
      object->isError = isError;
    }
    return reinterpret_cast<PyObject*>(object);
  }
};

PyObject* logWriterWrite(PyObject* self, PyObject* args)
{
  PyObject* value = nullptr;
  if (!PyArg_ParseTuple(args, "O", &value))
  {
    return nullptr;
  }

  auto* str = PyObject_Str(value);
  if (str == nullptr)
  {
    return nullptr;
  }

  Py_ssize_t size = 0;
  const auto* utf8 = PyUnicode_AsUTF8AndSize(str, &size);
  if (utf8 == nullptr)
  {
    Py_DECREF(str);
    return nullptr;
  }

  auto message = std::string_view{utf8, static_cast<size_t>(size)};
  auto* writer = reinterpret_cast<PyRuntimeLogWriter*>(self);
  if (g_currentMcpLogCapture != nullptr)
  {
    g_currentMcpLogCapture->append(
      utf8, static_cast<qsizetype>(size), writer->isError != 0);
  }
  while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
  {
    message.remove_suffix(1);
  }

  if (!message.empty())
  {
    auto* context = currentPythonExecutionContext();
    if (context != nullptr && context->logger != nullptr)
    {
      if (writer->isError != 0)
      {
        context->logger->error() << message;
      }
      else
      {
        context->logger->info() << message;
      }
    }
  }

  Py_DECREF(str);
  return PyLong_FromSsize_t(size);
}

PyObject* logWriterFlush(PyObject*, PyObject*)
{
  Py_RETURN_NONE;
}

PyObject* logWriterIsatty(PyObject*, PyObject*)
{
  Py_RETURN_FALSE;
}

void logWriterDealloc(PyObject* self)
{
  Py_TYPE(self)->tp_free(self);
}

bool ensureLogWriterType()
{
  if (g_logWriterType != nullptr)
  {
    return true;
  }

  static PyMethodDef methods[] = {
    {"write", logWriterWrite, METH_VARARGS, nullptr},
    {"flush", logWriterFlush, METH_NOARGS, nullptr},
    {"isatty", logWriterIsatty, METH_NOARGS, nullptr},
    {nullptr, nullptr, 0, nullptr}};

  static auto type = PyTypeObject{};
  type.tp_name = "trenchbroom._LogWriter";
  type.tp_basicsize = sizeof(PyRuntimeLogWriter);
  type.tp_flags = Py_TPFLAGS_DEFAULT;
  type.tp_methods = methods;
  type.tp_dealloc = logWriterDealloc;
  type.tp_new = PyType_GenericNew;

  if (PyType_Ready(&type) != 0)
  {
    return false;
  }

  g_logWriterType = &type;
  return true;
}

std::string readFile(const std::filesystem::path& path)
{
  auto stream = std::ifstream{path, std::ios::binary};
  auto buffer = std::ostringstream{};
  buffer << stream.rdbuf();
  return buffer.str();
}

PyObject* createConsoleGlobals()
{
  auto* globals = PyDict_New();
  if (globals == nullptr)
  {
    return nullptr;
  }

  auto* name = PyUnicode_FromString("__tb_console__");
  auto* trenchbroomModule = PyImport_ImportModule("trenchbroom");
  const auto initialized =
    name != nullptr && trenchbroomModule != nullptr
    && PyDict_SetItemString(globals, "__builtins__", PyEval_GetBuiltins()) == 0
    && PyDict_SetItemString(globals, "__name__", name) == 0
    && PyDict_SetItemString(globals, "trenchbroom", trenchbroomModule) == 0;

  if (initialized && trenchbroomModule != nullptr)
  {
    for (const auto helper : pythonConsoleHelperNames())
    {
      auto* attr = PyObject_GetAttrString(trenchbroomModule, helper.data());
      if (attr != nullptr)
      {
        PyDict_SetItemString(globals, helper.data(), attr);
        Py_DECREF(attr);
      }
      else
      {
        PyErr_Clear();
      }
    }
  }

  Py_XDECREF(name);
  Py_XDECREF(trenchbroomModule);

  if (!initialized)
  {
    Py_DECREF(globals);
    return nullptr;
  }
  return globals;
}

std::optional<PythonApiValueType> apiValueTypeForObject(
  PyObject* object, const size_t sequenceDepth = 0u)
{
  if (PyModule_Check(object))
  {
    const auto* moduleName = PyModule_GetName(object);
    if (moduleName != nullptr && std::string_view{moduleName} == "trenchbroom")
    {
      return PythonApiValueType{PythonApiType::Module, sequenceDepth};
    }
    PyErr_Clear();
    return std::nullopt;
  }

  const auto typeName = std::string_view{Py_TYPE(object)->tp_name};
  for (const auto& apiType : pythonApiTypes())
  {
    auto expectedName = std::string{"trenchbroom."};
    expectedName += apiType.name;
    if (typeName == expectedName)
    {
      return PythonApiValueType{apiType.type, sequenceDepth};
    }
  }

  const auto sequenceSize = PyList_Check(object)    ? PyList_GET_SIZE(object)
                            : PyTuple_Check(object) ? PyTuple_GET_SIZE(object)
                                                    : Py_ssize_t{-1};
  for (auto i = Py_ssize_t{0}; i < sequenceSize; ++i)
  {
    auto* item =
      PyList_Check(object) ? PyList_GET_ITEM(object, i) : PyTuple_GET_ITEM(object, i);
    if (item != Py_None)
    {
      if (const auto itemType = apiValueTypeForObject(item, sequenceDepth + 1u))
      {
        return itemType;
      }
    }
  }
  return std::nullopt;
}
} // namespace

PythonRuntime::PythonRuntime()
  : m_state{std::make_unique<PythonRuntimeState>()}
{
}

PythonRuntime::~PythonRuntime() = default;

PythonRuntime& PythonRuntime::instance()
{
  static auto instance = PythonRuntime{};
  return instance;
}

bool PythonRuntime::ensureInitialized()
{
  if (!installApiModule())
  {
    return false;
  }

  if (!Py_IsInitialized())
  {
    Py_Initialize();
  }
  if (!Py_IsInitialized())
  {
    return false;
  }

  auto gil = PyGILState_Ensure();
  if (!ensureLogWriterType())
  {
    PyGILState_Release(gil);
    return false;
  }

  auto* trenchbroomModule = PyImport_ImportModule("trenchbroom");
  if (trenchbroomModule == nullptr)
  {
    PyGILState_Release(gil);
    return false;
  }

  Py_DECREF(trenchbroomModule);
  PyGILState_Release(gil);
  return true;
}

bool PythonRuntime::runScript(
  const PythonExecutionContext& context, const std::filesystem::path& path)
{
  return runScript(context, path, nullptr);
}

bool PythonRuntime::runScript(PythonPluginSession& session)
{
  return runScript(session.context(), session.context().scriptPath, &session);
}

bool PythonRuntime::runConsoleCommand(
  const PythonExecutionContext& context, const std::string_view source)
{
  m_lastError.clear();
  if (context.mapWindow == nullptr)
  {
    m_lastError = "Python console requires an active map window";
    if (context.logger != nullptr)
    {
      context.logger->error() << m_lastError;
    }
    return false;
  }
  if (!ensureInitialized())
  {
    m_lastError = "Python API initialization failed";
    if (context.logger != nullptr)
    {
      context.logger->error() << m_lastError;
    }
    return false;
  }

  auto* mapDoc =
    context.document != nullptr
      ? context.document
      : (context.mapWindow != nullptr ? &context.mapWindow->document() : nullptr);
  auto consoleTx = mapDoc != nullptr ? std::make_unique<PythonDocumentTransaction>(
                                         *mapDoc, "Python Console Command")
                                     : nullptr;

  auto gil = PyGILState_Ensure();
  auto releaseGil = kdl::invoke_later{[&]() { PyGILState_Release(gil); }};
  auto scopedContext = ScopedExecutionContext{context};
  auto scopedStdStreamRedirect = ScopedStdStreamRedirect{};
  const auto reportError = [&]() {
    if (consoleTx)
    {
      consoleTx->cancel();
    }
    m_lastError = formatCurrentException();
    if (context.logger != nullptr)
    {
      context.logger->error() << m_lastError;
    }
    return false;
  };

  auto [globalsIt, inserted] =
    m_state->consoleGlobals.try_emplace(context.mapWindow, nullptr);
  if (inserted)
  {
    globalsIt->second = createConsoleGlobals();
    if (globalsIt->second == nullptr)
    {
      m_state->consoleGlobals.erase(globalsIt);
      return reportError();
    }
  }
  auto* globals = globalsIt->second;

  auto* trenchbroomModule = PyImport_ImportModule("trenchbroom");
  if (trenchbroomModule != nullptr)
  {
    auto* docFunc = PyObject_GetAttrString(trenchbroomModule, "current_document");
    if (docFunc != nullptr)
    {
      auto* docObj = PyObject_CallNoArgs(docFunc);
      if (docObj != nullptr)
      {
        PyDict_SetItemString(globals, "doc", docObj);
        auto* selObj = PyObject_GetAttrString(docObj, "selection");
        if (selObj != nullptr)
        {
          PyDict_SetItemString(globals, "sel", selObj);
          Py_DECREF(selObj);
        }
        Py_DECREF(docObj);
      }
      else
      {
        PyErr_Clear();
      }
      Py_DECREF(docFunc);
    }
    Py_DECREF(trenchbroomModule);
  }

  const auto sourceString = std::string{source};
  auto expression = true;
  auto* code = Py_CompileString(sourceString.c_str(), "<console>", Py_eval_input);
  if (code == nullptr && PyErr_ExceptionMatches(PyExc_SyntaxError))
  {
    PyErr_Clear();
    expression = false;
    code = Py_CompileString(sourceString.c_str(), "<console>", Py_file_input);
  }
  if (code == nullptr)
  {
    return reportError();
  }

  auto* result = PyEval_EvalCode(code, globals, globals);
  Py_DECREF(code);
  if (result == nullptr)
  {
    return reportError();
  }

  if (consoleTx)
  {
    if (!consoleTx->commit())
    {
      Py_DECREF(result);
      return reportError();
    }
  }

  if (expression && result != Py_None && context.logger != nullptr)
  {
    auto* representation = PyObject_Repr(result);
    if (representation == nullptr)
    {
      Py_DECREF(result);
      return reportError();
    }

    auto representationSize = Py_ssize_t{0};
    const auto* representationText =
      PyUnicode_AsUTF8AndSize(representation, &representationSize);
    if (representationText == nullptr)
    {
      Py_DECREF(representation);
      Py_DECREF(result);
      return reportError();
    }
    context.logger->info() << "=> "
                           << std::string_view{
                                representationText,
                                static_cast<size_t>(representationSize)};
    Py_DECREF(representation);
  }

  Py_DECREF(result);
  return true;
}

PythonMcpExecutionResult PythonRuntime::runMcpScript(
  const PythonExecutionContext& context, const PythonMcpExecutionRequest& request)
{
  auto execution = PythonMcpExecutionResult{};
  if (context.mapWindow == nullptr || context.document == nullptr)
  {
    execution.error = "MCP Python execution requires an active map document";
    return execution;
  }
  if (request.source.isEmpty())
  {
    execution.error = "MCP Python source is empty";
    return execution;
  }
  if (request.timeoutMs < 1 || request.timeoutMs > 90'000)
  {
    execution.error = "MCP Python timeout must be between 1 and 90000 ms";
    return execution;
  }
  if (!ensureInitialized())
  {
    execution.error = "Python API initialization failed";
    return execution;
  }

  const auto modifiedBefore = context.document->map().modified();

  auto transaction = request.transactional
                       ? std::make_optional(PythonDocumentTransaction{
                           *context.document, request.transactionName.toStdString()})
                       : std::nullopt;
  auto cancel = [&]() {
    if (transaction)
    {
      transaction->cancel();
      execution.rolledBack = true;
    }
  };

  auto gil = PyGILState_Ensure();
  auto releaseGil = kdl::invoke_later{[&]() { PyGILState_Release(gil); }};
  auto scopedContext = ScopedExecutionContext{context};
  auto logCapture = PythonMcpLogCapture{execution};
  auto scopedLogCapture = ScopedMcpLogCapture{logCapture};
  auto scopedStdStreamRedirect = ScopedStdStreamRedirect{};
  auto* globals = PyDict_New();
  if (globals == nullptr)
  {
    cancel();
    execution.error = "Could not create Python execution globals";
    return execution;
  }
  auto releaseGlobals = kdl::invoke_later{[&]() { Py_DECREF(globals); }};

  PyDict_SetItemString(globals, "__builtins__", PyEval_GetBuiltins());
  auto* arguments = pythonObjectFromJson(request.arguments);
  if (arguments == nullptr || PyDict_SetItemString(globals, "arguments", arguments) != 0)
  {
    Py_XDECREF(arguments);
    cancel();
    execution.error = "Could not prepare MCP Python arguments";
    return execution;
  }
  Py_DECREF(arguments);

  const auto filename =
    request.filename.isEmpty() ? QString{"<mcp-python>"} : request.filename;
  const auto filenameUtf8 = filename.toUtf8();
  auto* filenameObject =
    PyUnicode_FromStringAndSize(filenameUtf8.constData(), filenameUtf8.size());
  if (
    filenameObject == nullptr
    || PyDict_SetItemString(globals, "__file__", filenameObject) != 0)
  {
    Py_XDECREF(filenameObject);
    cancel();
    execution.error = "Could not prepare MCP Python filename";
    return execution;
  }
  Py_DECREF(filenameObject);

  const auto scriptInfo = QFileInfo{filename};
  auto scriptPath = std::optional<ScopedSysPath>{};
  if (scriptInfo.isAbsolute())
  {
    scriptPath.emplace(std::filesystem::path{scriptInfo.absolutePath().toStdWString()});
  }
  auto deadline = ScopedMcpExecutionDeadline{request.timeoutMs};
  if (!deadline.valid())
  {
    PyErr_Clear();
    cancel();
    execution.error = "Could not install the MCP Python timeout guard";
    return execution;
  }

  const auto source = request.source.toUtf8();
  auto* result =
    PyRun_StringFlags(source.constData(), Py_file_input, globals, globals, nullptr);
  execution.executed = true;
  if (result == nullptr)
  {
    cancel();
    execution.timedOut = deadline.hasExpired();
    execution.error = execution.timedOut
                        ? "MCP Python execution exceeded its cooperative timeout"
                        : QString::fromStdString(formatCurrentException());
    return execution;
  }
  Py_DECREF(result);

  if (deadline.hasExpired())
  {
    cancel();
    execution.timedOut = true;
    execution.error = "MCP Python execution exceeded its cooperative timeout";
    return execution;
  }

  auto* resultObject = PyDict_GetItemString(globals, "result");
  auto conversionError = QString{};
  const auto jsonResult = resultObject != nullptr
                            ? jsonValueFromPython(resultObject, 0, conversionError)
                            : std::optional<QJsonValue>{QJsonValue{QJsonValue::Null}};
  if (!jsonResult)
  {
    cancel();
    execution.error = conversionError;
    return execution;
  }
  const auto compactResult =
    QJsonDocument{
      jsonResult->isObject() ? jsonResult->toObject()
                             : QJsonObject{{"result", *jsonResult}}}
      .toJson(QJsonDocument::Compact);
  if (compactResult.size() > 1024 * 1024)
  {
    cancel();
    execution.error = "Python result exceeds the 1 MiB response limit";
    return execution;
  }

  if (transaction && !transaction->commit())
  {
    execution.rolledBack = true;
    execution.error = "Could not commit the MCP Python transaction";
    return execution;
  }
  execution.ok = true;
  execution.committed = transaction.has_value();
  execution.mutatedDocument = !modifiedBefore && context.document->map().modified();
  execution.value = *jsonResult;
  return execution;
}

PythonCompletionRoot PythonRuntime::consoleCompletionRoot(
  MapWindow& mapWindow, const std::string_view name) const
{
  if (!Py_IsInitialized())
  {
    return {};
  }

  auto gil = PyGILState_Ensure();
  auto releaseGil = kdl::invoke_later{[&]() { PyGILState_Release(gil); }};
  const auto globalsIt = m_state->consoleGlobals.find(&mapWindow);
  if (globalsIt == std::end(m_state->consoleGlobals))
  {
    return {};
  }

  const auto rootName = std::string{name};
  auto* object = PyDict_GetItemString(globalsIt->second, rootName.c_str());
  return object != nullptr ? PythonCompletionRoot{true, apiValueTypeForObject(object)}
                           : PythonCompletionRoot{};
}

void PythonRuntime::runCallback(PythonPluginSession& session, void* callback)
{
  if (!ensureInitialized())
  {
    return;
  }

  {
    auto gil = PyGILState_Ensure();
    auto releaseGil = kdl::invoke_later{[&]() { PyGILState_Release(gil); }};
    auto scopedContext = ScopedExecutionContext{session.context(), &session};
    auto scopedStdStreamRedirect = ScopedStdStreamRedirect{};
    auto* result = PyObject_CallObject(reinterpret_cast<PyObject*>(callback), nullptr);
    if (result == nullptr)
    {
      m_lastError = formatCurrentException();
      if (session.context().logger != nullptr)
      {
        session.context().logger->error() << m_lastError;
      }
    }
    Py_XDECREF(result);
  }
}

bool PythonRuntime::runScript(
  const PythonExecutionContext& context,
  const std::filesystem::path& path,
  PythonPluginSession* session)
{
  if (!ensureInitialized())
  {
    if (context.logger)
    {
      m_lastError = "Python API initialization failed";
      context.logger->error() << m_lastError;
    }
    return false;
  }

  m_lastError.clear();

  {
    auto gil = PyGILState_Ensure();
    auto releaseGil = kdl::invoke_later{[&]() { PyGILState_Release(gil); }};
    auto scopedContext = ScopedExecutionContext{context, session};
    auto scopedSysPath = ScopedSysPath{path.parent_path()};
    auto scopedStdStreamRedirect = ScopedStdStreamRedirect{};

    auto source = readFile(path);
    if (source.empty() && std::filesystem::file_size(path) > 0)
    {
      m_lastError = "Could not read Python script: " + path.generic_string();
      if (context.logger)
      {
        context.logger->error() << m_lastError;
      }
      return false;
    }

    auto* globals = PyDict_New();
    if (globals == nullptr)
    {
      m_lastError = "Could not create Python globals";
      if (context.logger)
      {
        context.logger->error() << m_lastError;
      }
      return false;
    }

    auto* builtins = PyEval_GetBuiltins();
    PyDict_SetItemString(globals, "__builtins__", builtins);
    const auto filename = path.u8string();
    auto* filenameObj = PyUnicode_FromStringAndSize(
      reinterpret_cast<const char*>(filename.c_str()),
      static_cast<Py_ssize_t>(filename.size()));
    if (filenameObj != nullptr)
    {
      PyDict_SetItemString(globals, "__file__", filenameObj);
      Py_DECREF(filenameObj);
    }

    auto* result =
      PyRun_StringFlags(source.c_str(), Py_file_input, globals, globals, nullptr);
    Py_DECREF(globals);

    if (result == nullptr)
    {
      if (context.logger)
      {
        m_lastError = formatCurrentException();
        context.logger->error() << m_lastError;
      }
      return false;
    }

    Py_DECREF(result);
  }
  return true;
}

void PythonRuntime::emitEvent(const std::string& eventName, MapWindow& mapWindow)
{
  emitEvent(eventName, mapWindow, true);
}

void PythonRuntime::emitEvent(
  const std::string& eventName, MapWindow& mapWindow, const bool initializeIfNeeded)
{
  if (!initializeIfNeeded && !Py_IsInitialized())
  {
    return;
  }

  if (!ensureInitialized())
  {
    return;
  }

  auto context = PythonExecutionContext{};
  context.mapWindow = &mapWindow;
  context.document = &mapWindow.document();
  context.appController = &mapWindow.appController();
  context.currentMapView = mapWindow.currentMapViewBase();
  context.logger = &mapWindow.pythonLogger();

  auto gil = PyGILState_Ensure();
  auto scopedContext = ScopedExecutionContext{context};
  auto* module = PyImport_ImportModule("trenchbroom");
  if (module != nullptr)
  {
    auto* hasCallbacks =
      PyObject_CallMethod(module, "_has_event_callbacks", "s", eventName.c_str());
    const auto shouldEmit = hasCallbacks != nullptr && PyObject_IsTrue(hasCallbacks) == 1;
    Py_XDECREF(hasCallbacks);
    if (!shouldEmit)
    {
      Py_DECREF(module);
      PyGILState_Release(gil);
      return;
    }

    auto* result = PyObject_CallMethod(module, "_emit_event", "s", eventName.c_str());
    if (result == nullptr && context.logger)
    {
      context.logger->error() << formatCurrentException();
    }
    Py_XDECREF(result);
    Py_DECREF(module);
  }
  PyGILState_Release(gil);
}

void PythonRuntime::cleanupPlugin(const std::string& pluginId)
{
  if (!Py_IsInitialized() || !ensureInitialized())
  {
    return;
  }
  auto gil = PyGILState_Ensure();
  auto* module = PyImport_ImportModule("trenchbroom");
  if (module != nullptr)
  {
    auto* result = PyObject_CallMethod(module, "_cleanup_plugin", "s", pluginId.c_str());
    Py_XDECREF(result);
    Py_DECREF(module);
  }
  PyGILState_Release(gil);
}

void PythonRuntime::cleanupPluginSession(PythonPluginSession& session)
{
  if (!Py_IsInitialized() || !ensureInitialized())
  {
    return;
  }

  auto gil = PyGILState_Ensure();
  auto* module = PyImport_ImportModule("trenchbroom");
  if (module != nullptr)
  {
    auto* capsule = PyCapsule_New(&session, nullptr, nullptr);
    if (capsule != nullptr)
    {
      auto* methodName = PyUnicode_FromString("_cleanup_plugin_session");
      auto* result = methodName != nullptr
                       ? PyObject_CallMethodObjArgs(module, methodName, capsule, nullptr)
                       : nullptr;
      Py_XDECREF(result);
      Py_XDECREF(methodName);
      Py_DECREF(capsule);
    }
    Py_DECREF(module);
  }
  PyGILState_Release(gil);
}

void PythonRuntime::cleanupDocument(MapWindow& mapWindow)
{
  if (!Py_IsInitialized())
  {
    return;
  }

  if (!ensureInitialized())
  {
    return;
  }

  auto gil = PyGILState_Ensure();
  if (const auto globalsIt = m_state->consoleGlobals.find(&mapWindow);
      globalsIt != std::end(m_state->consoleGlobals))
  {
    Py_DECREF(globalsIt->second);
    m_state->consoleGlobals.erase(globalsIt);
  }
  auto* module = PyImport_ImportModule("trenchbroom");
  if (module != nullptr)
  {
    auto* capsule = PyCapsule_New(&mapWindow.document(), nullptr, nullptr);
    if (capsule != nullptr)
    {
      auto* methodName = PyUnicode_FromString("_invalidate_document");
      auto* result = methodName != nullptr
                       ? PyObject_CallMethodObjArgs(module, methodName, capsule, nullptr)
                       : nullptr;
      Py_XDECREF(result);
      Py_XDECREF(methodName);
      Py_DECREF(capsule);
    }
    Py_DECREF(module);
  }
  PyGILState_Release(gil);
}

std::string PythonRuntime::formatCurrentException() const
{
  if (!PyErr_Occurred())
  {
    return "Unknown Python error";
  }

  PyObject* type = nullptr;
  PyObject* value = nullptr;
  PyObject* traceback = nullptr;
  PyErr_Fetch(&type, &value, &traceback);
  PyErr_NormalizeException(&type, &value, &traceback);

  auto result = std::string{};
  auto* tracebackModule = PyImport_ImportModule("traceback");
  if (tracebackModule != nullptr)
  {
    auto* formatException = PyObject_GetAttrString(tracebackModule, "format_exception");
    if (formatException != nullptr)
    {
      auto* formatted = PyObject_CallFunctionObjArgs(
        formatException,
        type != nullptr ? type : Py_None,
        value != nullptr ? value : Py_None,
        traceback != nullptr ? traceback : Py_None,
        nullptr);
      if (formatted != nullptr)
      {
        auto* separator = PyUnicode_FromString("");
        auto* joined =
          separator != nullptr ? PyUnicode_Join(separator, formatted) : nullptr;
        Py_XDECREF(separator);
        if (joined != nullptr)
        {
          const auto* utf8 = PyUnicode_AsUTF8(joined);
          if (utf8 != nullptr)
          {
            result = utf8;
          }
          Py_DECREF(joined);
        }
        Py_DECREF(formatted);
      }
      Py_DECREF(formatException);
    }
    Py_DECREF(tracebackModule);
  }

  if (result.empty() && value != nullptr)
  {
    auto* str = PyObject_Str(value);
    if (str != nullptr)
    {
      const auto* utf8 = PyUnicode_AsUTF8(str);
      if (utf8 != nullptr)
      {
        result = utf8;
      }
      Py_DECREF(str);
    }
  }
  if (result.empty())
  {
    result = "Python error";
  }

  Py_XDECREF(type);
  Py_XDECREF(value);
  Py_XDECREF(traceback);
  return result;
}

const std::string& PythonRuntime::lastError() const
{
  return m_lastError;
}

bool PythonRuntime::installApiModule()
{
  return installPythonApiModule();
}

PythonExecutionContext* currentPythonExecutionContext()
{
  return g_currentExecutionContext;
}

PythonPluginSession* currentPythonPluginSession()
{
  return g_currentPluginSession;
}

} // namespace tb::ui
