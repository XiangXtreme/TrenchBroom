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

#include "ui/LightmapBaker.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include "mdl/ExportOptions.h"
#include "mdl/Map.h"
#include "prefs/Preferences.h"
#include "ui/BuiltinGoldSrcAssets.h"
#include "ui/CompilationHltoolsTaskRunner.h"
#include "ui/LightmapBakeReader.h"
#include "ui/QPathUtils.h"

#include <algorithm>
#include <thread>

namespace tb::ui
{

namespace
{

constexpr auto MapFileName = "lightmap.map";
constexpr auto BspFileName = "lightmap.bsp";
constexpr auto AtlasFileName = "lightmap.bmp";
constexpr auto ManifestFileName = "lightmap.json";

int clampedThreadCount(const int threadCount)
{
  const auto available = static_cast<int>(std::thread::hardware_concurrency());
  const auto roomForUi = available > 2 ? available - 2 : 1;
  return std::max(1, std::min(threadCount, roomForUi));
}

} // namespace

LightmapBaker::LightmapBaker(QObject* parent)
  : QObject{parent}
{
}

LightmapBaker::~LightmapBaker()
{
  cancel();
}

bool LightmapBaker::running() const
{
  return m_stage >= 0;
}

const std::string& LightmapBaker::output() const
{
  return m_output;
}

std::filesystem::path LightmapBaker::diagnosticDirectory() const
{
  return m_failedWorkDir ? pathFromQString(m_failedWorkDir->path())
                         : std::filesystem::path{};
}

bool LightmapBaker::reusedGeometry() const
{
  return m_reusedGeometry;
}

void LightmapBaker::clearCache()
{
  m_cachedWorkDir.reset();
}

std::filesystem::path LightmapBaker::workerPath()
{
  return hltoolsWorkerPath();
}

void LightmapBaker::start(
  const mdl::Map& map,
  const int threadCount,
  const std::string& quality,
  Callback callback,
  ProjectInfoProvider projectInfoProvider)
{
  cancel();

  m_callback = std::move(callback);
  m_executable = workerPath();
  m_threadCount = clampedThreadCount(threadCount);
  m_quality = quality;
  m_project.reset();
  m_output.clear();
  m_progressBuffer.clear();
  m_reusedGeometry = false;
  m_stage = 0;

  m_workDir = std::make_shared<QTemporaryDir>(
    QDir::temp().filePath(QStringLiteral("tb-lightmap-XXXXXX")));
  if (!m_workDir->isValid())
  {
    finish(std::nullopt, "could not create the lightmap scratch directory");
    return;
  }

  if (m_cachedWorkDir)
    for (const auto* suffix : {".bsp", ".key", ".wa_"})
      QFile::copy(
        m_cachedWorkDir->filePath(QStringLiteral("geometry") + suffix),
        m_workDir->filePath(QStringLiteral("geometry") + suffix));

  if (projectInfoProvider)
  {
    auto project = projectInfoProvider(pathFromQString(m_workDir->path()));
    if (project.is_error())
    {
      finish(std::nullopt, std::get<Error>(project.error()).msg);
      return;
    }
    m_project = std::move(project).value();
  }

  if (!m_project)
  {
    // Use the live map and the same layer filtering as a normal compile export.
    const auto exported = map.exportAs(mdl::MapExportOptions{
      pathFromQString(m_workDir->filePath(MapFileName)),
      false,
      std::nullopt,
      std::nullopt,
    });
    if (exported.is_error())
    {
      finish(std::nullopt, std::get<Error>(exported.error()).msg);
      return;
    }
    if (mapUsesEmptyTexture(map))
    {
      const auto config = prepareLightmapWads(map, pathFromQString(m_workDir->path()));
      if (config.is_error())
      {
        finish(std::nullopt, std::get<Error>(config.error()).msg);
        return;
      }
    }
  }
  else if (mapUsesEmptyTexture(map))
  {
    const auto prepared = prepareLightmapProject(
      m_project->projectPath, pathFromQString(m_workDir->filePath("lightmap.tbproj")));
    if (prepared.is_error())
    {
      finish(std::nullopt, std::get<Error>(prepared.error()).msg);
      return;
    }
    m_project->projectPath = prepared.value();
  }

  m_process = std::make_unique<QProcess>();
  m_process->setProcessChannelMode(QProcess::MergedChannels);
  // Project extraction must stay inside the owned scratch directory when killed.
  // Plain MAP builds retain the editor's working directory for relative WAD paths.
  if (m_project)
    m_process->setWorkingDirectory(m_workDir->path());
  connect(m_process.get(), &QProcess::readyRead, this, &LightmapBaker::readOutput);
  connect(
    m_process.get(),
    &QProcess::finished,
    this,
    [this](const int exitCode, const QProcess::ExitStatus exitStatus) {
      stageFinished(exitCode, static_cast<int>(exitStatus));
    });
  connect(
    m_process.get(), &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
      if (running() && m_process->error() == QProcess::FailedToStart)
      {
        finish(
          std::nullopt,
          "could not start the bundled lightmap compiler: " + m_executable.string());
      }
    });

  emit stageProgressChanged(tr("Starting"), -1);
  runStage();
}

void LightmapBaker::cancel()
{
  m_stage = -1;
  m_callback = nullptr;
  auto workDir = std::move(m_workDir);
  if (auto* process = m_process.release())
  {
    disconnect(process, nullptr, this, nullptr);
    if (process->state() == QProcess::NotRunning)
    {
      process->deleteLater();
      return;
    }

    // Destroying a running QProcess waits for it. Keep the process and its files
    // alive until the OS reports exit, independently of the next bake or view.
    process->setParent(QCoreApplication::instance());
    connect(process, &QProcess::finished, process, &QObject::deleteLater);
    connect(process, &QProcess::errorOccurred, process, [process](const auto error) {
      if (error == QProcess::FailedToStart)
        process->deleteLater();
    });
    connect(process, &QObject::destroyed, [workDir = std::move(workDir)] {});
    if (process->state() == QProcess::Starting)
      connect(process, &QProcess::started, process, &QProcess::kill);
    else
      process->kill();
  }
}

void LightmapBaker::readOutput()
{
  const auto chunk = m_process->readAll();
  if (!running())
    return;
  m_output += chunk.toStdString();
  if (m_stage != 0)
    return;

  // HLTools repaints progress with carriage returns and can split a line across reads.
  static const auto progressLine = QRegularExpression{
    R"((?:^|[\r\n])[ \t]*([^\r\n\[]+?)\s*\[[# ]+\][ \t]+(\d{1,3})%[ \t]+(\d+(?:\.\d+)?)s[ \t])"};
  m_progressBuffer += chunk;
  auto matches = progressLine.globalMatch(QString::fromUtf8(m_progressBuffer));
  auto last = QRegularExpressionMatch{};
  while (matches.hasNext())
    last = matches.next();

  if (last.hasMatch())
  {
    const auto percent = last.captured(2).toInt();
    emit stageProgressChanged(last.captured(1).trimmed(), percent);
  }

  if (
    !m_reusedGeometry && m_output.find("preview: reusing geometry") != std::string::npos)
  {
    m_reusedGeometry = true;
    emit stageProgressChanged(tr("Updating lighting"), -1);
  }

  const auto lastBreak =
    std::max(m_progressBuffer.lastIndexOf('\r'), m_progressBuffer.lastIndexOf('\n'));
  m_progressBuffer.remove(0, lastBreak + 1);
}

void LightmapBaker::runStage()
{
  if (!m_process)
  {
    return;
  }

  auto arguments = QStringList{};
  if (m_stage == 0)
  {
    // RAD discards all-zero lightmaps. Keep dark faces in the preview atlas with
    // the smallest nonzero luxel value so they do not fall back to full brightness.
    if (m_project)
    {
      arguments = {
        QStringLiteral("compile"),
        QStringLiteral("--project"),
        pathAsQString(m_project->projectPath),
        QStringLiteral("--map"),
        QString::fromStdString(m_project->mapName),
        QStringLiteral("--output"),
        m_workDir->filePath(BspFileName),
        QStringLiteral("-minlight"),
        QStringLiteral("1"),
        QStringLiteral("-gpu"),
        QStringLiteral("-threads"),
        QString::number(m_threadCount)};
    }
    else
    {
      arguments = {
        QStringLiteral("compile"),
        m_workDir->filePath(MapFileName),
        QStringLiteral("-minlight"),
        QStringLiteral("1"),
        QStringLiteral("-gpu"),
        QStringLiteral("-threads"),
        QString::number(m_threadCount)};
      if (QFile::exists(m_workDir->filePath("wads.cfg")))
        arguments << QStringLiteral("-wadcfgfile") << m_workDir->filePath("wads.cfg");
    }
    if (m_quality == Preferences::LightmapQualityFast)
      arguments << QStringLiteral("-fast");
    else if (m_quality == Preferences::LightmapQualityUltra)
      arguments << QStringLiteral("-extra");
    else
      arguments << QStringLiteral("-previewfastvis");
    arguments << QStringLiteral("-noclip") << QStringLiteral("-previewcache")
              << m_workDir->filePath("geometry");
  }
  else
  {
    arguments = {
      QStringLiteral("lightmap"),
      m_workDir->filePath(BspFileName),
      m_workDir->filePath(AtlasFileName),
      QStringLiteral("-manifest"),
      m_workDir->filePath(ManifestFileName),
      QStringLiteral("-force")};
  }

  m_process->setProgram(pathAsQString(m_executable));
  m_process->setArguments(arguments);
  m_process->start();
}

void LightmapBaker::stageFinished(const int exitCode, const int exitStatus)
{
  if (m_stage < 0)
  {
    return;
  }

  readOutput();
  if (exitStatus != static_cast<int>(QProcess::NormalExit) || exitCode != 0)
  {
    auto reason = tr("Compiler exited with code %1.").arg(exitCode).toStdString();
    if (exitStatus != static_cast<int>(QProcess::NormalExit))
      reason = tr("The lightmap compiler crashed.").toStdString();
    else if (m_output.find("!!! LEAK") != std::string::npos)
      reason = tr("The map is not sealed. Fix the leak before baking.").toStdString();
    else if (m_output.find("AllocBlock") != std::string::npos)
      reason = tr("The map exceeds the lightmap atlas budget.").toStdString();
    finish(std::nullopt, std::move(reason));
    return;
  }

  if (m_stage == 0)
  {
    m_stage = 1;
    emit stageProgressChanged(tr("Reading lightmaps"), -1);
    runStage();
    return;
  }

  auto error = std::string{};
  auto bake = readLightmapBake(
    pathFromQString(m_workDir->filePath(ManifestFileName)),
    pathFromQString(m_workDir->filePath(AtlasFileName)),
    error);
  if (!bake)
  {
    finish(std::nullopt, error);
    return;
  }
  finish(std::move(bake), {});
}

void LightmapBaker::finish(std::optional<render::LightmapBake> bake, std::string error)
{
  auto callback = std::move(m_callback);
  if (bake)
    m_cachedWorkDir = m_workDir;
  else if (m_workDir && m_workDir->isValid())
  {
    m_failedWorkDir = m_workDir;
    auto log = QFile{m_workDir->filePath("preview-output.log")};
    if (log.open(QIODevice::WriteOnly))
      log.write(QByteArray::fromStdString(error + "\n\n" + m_output));
  }
  cancel();
  if (callback)
    callback(std::move(bake), error);
}

} // namespace tb::ui
