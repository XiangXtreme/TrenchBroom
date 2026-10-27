/*
 Copyright (C) 2010 Kristian Duske

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

#include "ui/CrashReporter.h"

#include <QStandardPaths>
#include <QtSystemDetection>

#include "fs/DiskIO.h"
#include "fs/PathInfo.h"
#include "gl/GlManager.h"
#include "mdl/Map.h"
#include "ui/AppController.h"
#include "ui/CrashDialog.h"
#include "ui/GetVersion.h"
#include "ui/ImageUtils.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/QPathUtils.h"
#include "ui/SystemPaths.h"

#include "kd/filesystem_utils.h"
#include "kd/path_utils.h"

#include <cpptrace/basic.hpp>
#include <cpptrace/from_current.hpp>
#include <fmt/format.h>
#include <fmt/std.h>

#include <array>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <sstream>
#include <utility>

#if defined(Q_OS_WIN) && defined(_MSC_VER)
#include <DbgHelp.h>
#include <windows.h>
#endif

namespace tb::ui
{
namespace
{

AppController* appControllerForCrashReporter = nullptr;
bool crashReporterGuiEnabled = true;
bool crashReporterIsReportingCrash = false;
std::string windowsExceptionDetails;

#if defined(_WIN32) && defined(_MSC_VER)
PEXCEPTION_POINTERS windowsExceptionPointers = nullptr;
#endif

const MapDocument* topDocument()
{
  if (appControllerForCrashReporter)
  {
    if (
      const auto* topMapWindow =
        appControllerForCrashReporter->mapWindowManager().topMapWindow())
    {
      return &topMapWindow->document();
    }
  }
  return nullptr;
}

std::string makeCrashReport(const auto& stacktrace, const auto& reason)
{
  auto ss = std::stringstream{};

  ss << "OS:\t" << QSysInfo::prettyProductName().toStdString() << std::endl;
  ss << "Qt:\t" << qVersion() << std::endl;

  const auto& glInfo = gl::GlManager::glInfo();
  ss << "GL_VENDOR:\t" << glInfo.vendor << std::endl;
  ss << "GL_RENDERER:\t" << glInfo.renderer << std::endl;
  ss << "GL_VERSION:\t" << glInfo.version << std::endl;

  ss << "TrenchBroom Version:\t" << getBuildVersion().toStdString() << std::endl;
  ss << "TrenchBroom Build:\t" << getBuildIdStr().toStdString() << std::endl;

  ss << "Reason:\t" << reason << std::endl;
  if (!windowsExceptionDetails.empty())
  {
    ss << windowsExceptionDetails;
  }
  if (const auto svgPath = currentSvgRenderPath(); !svgPath.empty())
  {
    ss << "Current SVG render path:\t" << svgPath << std::endl;
  }

  stacktrace.print(ss);

  return ss.str();
}

// returns the empty path for unsaved maps, or if we can't determine the current map
std::filesystem::path savedMapPath()
{
  const auto* document = topDocument();
  return document ? document->map().path() : std::filesystem::path{};
}

void writeMiniDump(const std::filesystem::path& path)
{
#if defined(_WIN32) && defined(_MSC_VER)
  const auto file = CreateFileW(
    path.wstring().c_str(),
    GENERIC_WRITE,
    0,
    nullptr,
    CREATE_ALWAYS,
    FILE_ATTRIBUTE_NORMAL,
    nullptr);
  if (file == INVALID_HANDLE_VALUE)
  {
    std::cerr << "could not create minidump: " << GetLastError() << std::endl;
    return;
  }

  auto exceptionInfo = MINIDUMP_EXCEPTION_INFORMATION{};
  auto* exceptionInfoPtr = static_cast<MINIDUMP_EXCEPTION_INFORMATION*>(nullptr);
  if (windowsExceptionPointers != nullptr)
  {
    exceptionInfo.ThreadId = GetCurrentThreadId();
    exceptionInfo.ExceptionPointers = windowsExceptionPointers;
    exceptionInfo.ClientPointers = FALSE;
    exceptionInfoPtr = &exceptionInfo;
  }

  const auto written = MiniDumpWriteDump(
    GetCurrentProcess(),
    GetCurrentProcessId(),
    file,
    MiniDumpNormal,
    exceptionInfoPtr,
    nullptr,
    nullptr);
  CloseHandle(file);

  if (written)
  {
    std::cerr << "wrote minidump to " << path.string() << std::endl;
  }
  else
  {
    std::cerr << "could not write minidump: " << GetLastError() << std::endl;
  }
#else
  (void)path;
#endif
}

std::filesystem::path crashReportBasePath()
{
  const auto mapPath = savedMapPath();
  const auto documentsPath =
    pathFromQString(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
  return makeCrashReportBasePath(mapPath, documentsPath, [](const auto& path) {
    return fs::Disk::pathInfo(path) == fs::PathInfo::File;
  });
}

[[noreturn]] void reportCrashAndExit(
  const cpptrace::stacktrace& stacktrace, const std::string& reason)
{
  // just abort if we reenter reportCrashAndExit (i.e. if it crashes)
  if (std::exchange(crashReporterIsReportingCrash, true))
  {
    std::signal(SIGABRT, SIG_DFL);
    std::abort();
  }

  // get the crash report as a string
  const auto report = makeCrashReport(stacktrace, reason);

  // write it to the crash log file
  const auto basePath = crashReportBasePath();

  // ensure the containing directory exists
  fs::Disk::createDirectory(basePath.parent_path()) | kdl::transform([&](auto) {
    const auto reportPath = kdl::path_add_extension(basePath, ".txt");
    auto logPath = kdl::path_add_extension(basePath, ".log");
    auto mapPath = kdl::path_add_extension(basePath, ".map");
    auto dumpPath = std::filesystem::path{};
#if defined(_WIN32) && defined(_MSC_VER)
    dumpPath = crashReportArtifactPath(basePath, ".dmp");
#endif

    kdl::with_ostream(reportPath, [&](auto& stream) {
      stream << report;
      std::cerr << "wrote crash log to " << reportPath.string() << std::endl;
    }) | kdl::transform_error([](const auto& e) {
      std::cerr << "could not write crash log: " << e.msg << std::endl;
    });

    if (!dumpPath.empty())
    {
      writeMiniDump(dumpPath);
    }

    // save the map
    if (const auto* document = topDocument())
    {
      document->map().saveTo(mapPath) | kdl::transform([&]() {
        std::cerr << "wrote map to " << mapPath.string() << std::endl;
      }) | kdl::transform_error([](const auto& e) {
        std::cerr << "could not write map: " << e.msg << std::endl;
      });
    }
    else
    {
      mapPath = std::filesystem::path{};
    }

    // Copy the log file
    auto ec = std::error_code{};
    if (!std::filesystem::copy_file(SystemPaths::logFilePath(), logPath, ec) || ec)
    {
      logPath = std::filesystem::path{};
    }

    if (crashReporterGuiEnabled)
    {
      auto dialog = CrashDialog{reason, reportPath, mapPath, logPath, dumpPath};
      dialog.exec();
    }
  }) | kdl::transform_error([](const auto& e) {
    std::cerr << "could not create crash folder: " << e.msg << std::endl;
  });

  // write the crash log to stderr
  std::cerr << "crash log:" << std::endl;
  std::cerr << report << std::endl;

  std::signal(SIGABRT, SIG_DFL);
  std::abort();
}

#if defined(Q_OS_WIN) && defined(_MSC_VER)
std::string moduleNameForAddress(const void* address)
{
  auto module = HMODULE{};
  if (
    GetModuleHandleExA(
      GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
        | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
      static_cast<LPCSTR>(address),
      &module)
    == 0)
  {
    return {};
  }

  auto filename = std::array<char, MAX_PATH>{};
  if (GetModuleFileNameA(module, filename.data(), DWORD(filename.size())) == 0)
  {
    return {};
  }
  return filename.data();
}

LONG WINAPI TrenchBroomUnhandledExceptionFilter(PEXCEPTION_POINTERS pExceptionPtrs)
{
  windowsExceptionPointers = pExceptionPtrs;
  windowsExceptionDetails = fmt::format(
    "Exception code:\t0x{:08x}\n"
    "Exception address:\t{}\n"
    "Exception module:\t{}\n",
    pExceptionPtrs->ExceptionRecord->ExceptionCode,
    pExceptionPtrs->ExceptionRecord->ExceptionAddress,
    moduleNameForAddress(pExceptionPtrs->ExceptionRecord->ExceptionAddress));

  reportCrashAndExit(
    cpptrace::generate_trace(),
    std::to_string(pExceptionPtrs->ExceptionRecord->ExceptionCode));
  // return EXCEPTION_EXECUTE_HANDLER; unreachable
}
#endif

void SignalCrashHandler(const int signum)
{
  std::signal(signum, SIG_DFL);
  reportCrashAndExit(cpptrace::generate_trace(), fmt::format("signal {}", signum));
}

#if !defined(_WIN32) || !defined(_MSC_VER)
void CrashHandler(const int signum)
{
  SignalCrashHandler(signum);
}
#endif

void TerminateHandler()
{
  auto reason = std::string{"std::terminate"};
  if (const auto exception = std::current_exception())
  {
    try
    {
      std::rethrow_exception(exception);
    }
    catch (const std::exception& e)
    {
      reason = fmt::format("std::terminate: {}", e.what());
    }
    catch (...)
    {
      reason = "std::terminate: unknown exception";
    }
  }
  reportCrashAndExit(cpptrace::generate_trace(), reason);
}

} // namespace

std::filesystem::path makeCrashReportBasePath(
  const std::filesystem::path& savedMapPath,
  const std::filesystem::path& documentsPath,
  const CrashReportPathExists& pathExists)
{
  const auto crashLogPath =
    !savedMapPath.empty()
      ? savedMapPath.parent_path() / (savedMapPath.stem().string() + "-crash.txt")
      : documentsPath / "trenchbroom-crash.txt";

  auto index = 0;
  auto testCrashLogPath = crashLogPath;
  while (pathExists(testCrashLogPath))
  {
    ++index;
    testCrashLogPath = crashLogPath.parent_path()
                       / fmt::format("{}-{}.txt", crashLogPath.stem().string(), index);
  }

  return kdl::path_remove_extension(testCrashLogPath);
}

std::filesystem::path crashReportArtifactPath(
  const std::filesystem::path& basePath, const std::filesystem::path& extension)
{
  auto result = basePath;
  result.replace_extension(extension);
  return result;
}

void installCrashHandlers()
{
  std::set_terminate(TerminateHandler);
  std::signal(SIGABRT, SignalCrashHandler);

#if defined(Q_OS_WIN) && defined(_MSC_VER)
  // with MSVC, set our own handler for segfaults so we can access the context
  // pointer, to allow StackWalker to read the backtrace.
  // see also: http://crashrpt.sourceforge.net/docs/html/exception_handling.html
  SetUnhandledExceptionFilter(TrenchBroomUnhandledExceptionFilter);
#else
  signal(SIGSEGV, CrashHandler);
#endif
}

CrashReporter::CrashReporter(AppController& appController)
{
  appControllerForCrashReporter = &appController;
  installCrashHandlers();
}

[[noreturn]] void CrashReporter::reportCrashAndExit(const std::string& reason)
{
  tb::ui::reportCrashAndExit(cpptrace::generate_trace(), reason);
}

} // namespace tb::ui
