#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>

#include "ui/python/PythonRuntime.h"

#undef slots
#include <Python.h>

#include <filesystem>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
TEST_CASE("PythonRuntime")
{
#ifdef _WIN32
  const auto probe = qEnvironmentVariable("TB_TEST_PYTHON_RUNTIME_PROBE");
  if (!probe.isEmpty())
  {
    auto& runtime = PythonRuntime::instance();
    if (probe == "bundled")
    {
      REQUIRE(runtime.ensureInitialized());
      CHECK(
        PyRun_SimpleString(
          "import sys, os, encodings, json, pathlib, ssl, sqlite3, ctypes, trenchbroom\n"
          "assert json.loads('{\"ok\": true}')[\"ok\"]\n"
          "assert sys.flags.isolated\n"
          "assert pathlib.Path(sys.prefix) == "
          "pathlib.Path(os.environ['TB_TEST_PYTHON_EXPECTED_HOME'])\n")
        == 0);
    }
    else
    {
      CHECK_FALSE(runtime.ensureInitialized());
      const auto error = runtime.lastError();
      CHECK_FALSE(error.empty());
      CHECK_FALSE(runtime.ensureInitialized());
      CHECK(runtime.lastError() == error);
    }
    return;
  }

  auto temporary = QTemporaryDir{};
  REQUIRE(temporary.isValid());
  auto environment = QProcessEnvironment::systemEnvironment();
  environment.insert(
    "QT_QPA_PLATFORM_PLUGIN_PATH",
    QDir{QLibraryInfo::path(QLibraryInfo::PluginsPath)}.filePath("platforms"));
  environment.insert("PYTHONHOME", temporary.filePath("old-profile/Python"));
  environment.insert("PYTHONPATH", temporary.filePath("old-profile/Lib"));
  const auto source = QDir{QCoreApplication::applicationDirPath()};
  auto executable = QCoreApplication::applicationFilePath();
  environment.insert("TB_TEST_PYTHON_RUNTIME_PROBE", "bundled");
  SECTION("bundled runtime ignores invalid host Python paths") {}
  SECTION("bundled runtime works after relocation to a Unicode path")
  {
    const auto relocated =
      temporary.filePath(QString::fromUtf8("\xe4\xb8\xad\xe6\x96\x87"));
    REQUIRE(QDir{}.mkpath(relocated));
    executable = QDir{relocated}.filePath("TbUiLibTest.exe");
    REQUIRE(QFile::copy(QCoreApplication::applicationFilePath(), executable));
    std::filesystem::copy(
      std::filesystem::path{source.filePath("python").toStdWString()},
      std::filesystem::path{QDir{relocated}.filePath("python").toStdWString()},
      std::filesystem::copy_options::recursive);
    environment.insert("PATH", source.path() + ";" + environment.value("PATH"));
  }
  SECTION("missing runtime returns an error and repeated initialization is safe")
  {
    executable =
      temporary.filePath(QString::fromUtf8("\xe4\xb8\xad\xe6\x96\x87-probe.exe"));
    REQUIRE(QFile::copy(QCoreApplication::applicationFilePath(), executable));
    environment.insert("PATH", source.path() + ";" + environment.value("PATH"));
    environment.insert("TB_TEST_PYTHON_RUNTIME_PROBE", "missing");
  }
  environment.insert(
    "TB_TEST_PYTHON_EXPECTED_HOME", QFileInfo{executable}.dir().filePath("python"));
  if (executable != QCoreApplication::applicationFilePath())
  {
    const auto destination = QFileInfo{executable}.dir();
    for (const auto& dll : source.entryList({"*.dll"}, QDir::Files))
    {
      REQUIRE(QFile::copy(source.filePath(dll), destination.filePath(dll)));
    }
    if (source.exists("platforms"))
    {
      std::filesystem::copy(
        std::filesystem::path{source.filePath("platforms").toStdWString()},
        std::filesystem::path{destination.filePath("platforms").toStdWString()},
        std::filesystem::copy_options::recursive);
    }
  }

  auto process = QProcess{};
  process.setProcessEnvironment(environment);
  process.setWorkingDirectory(temporary.path());
  process.start(executable, {"PythonRuntime"});
  REQUIRE(process.waitForStarted());
  const auto finished = process.waitForFinished(30'000);
  const auto output = process.readAllStandardOutput();
  const auto error = process.readAllStandardError();
  INFO(output.constData());
  INFO(error.constData());
  INFO(process.errorString().toStdString());
  REQUIRE(finished);
  CHECK(process.exitStatus() == QProcess::NormalExit);
  CHECK(process.exitCode() == 0);
#else
  CHECK(PythonRuntime::instance().ensureInitialized());
#endif
}
} // namespace tb::ui
