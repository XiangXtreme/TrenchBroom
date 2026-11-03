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

#include <QProcess>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include "mdl/ExportOptions.h"
#include "mdl/Map.h"
#include "prefs/Preferences.h"
#include "ui/LightmapBakeReader.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <system_error>
#include <thread>

namespace tb::ui
{

namespace
{

std::atomic<int> nextBakeId = 0;

constexpr auto MapFileName = "lightmap.map";
constexpr auto BspFileName = "lightmap.bsp";
constexpr auto AtlasFileName = "lightmap.bmp";
constexpr auto ManifestFileName = "lightmap.json";

QString toQString(const std::filesystem::path& path)
{
  return QString::fromStdString(path.string());
}

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

void LightmapBaker::start(
  const mdl::Map& map,
  const std::filesystem::path& executable,
  const int threadCount,
  const std::string& quality,
  Callback callback)
{
  cancel();

  m_callback = std::move(callback);
  m_executable = executable;
  m_threadCount = clampedThreadCount(threadCount);
  m_quality = quality;
  m_output.clear();
  m_progressBuffer.clear();
  m_stage = 0;

  // Each bake gets its own directory: two views may bake at the same time, and sharing
  // one directory would have them overwrite each other's output.
  m_workDir = std::filesystem::temp_directory_path() / "tb-lightmap"
              / ("bake-" + std::to_string(++nextBakeId));
  auto ec = std::error_code{};
  std::filesystem::remove_all(m_workDir, ec);
  std::filesystem::create_directories(m_workDir, ec);
  if (!std::filesystem::exists(m_workDir))
  {
    finish(std::nullopt, "could not create the lightmap scratch directory");
    return;
  }

  // Use the live map and the same layer filtering as a normal compile export.
  const auto exported = map.exportAs(mdl::MapExportOptions{
    m_workDir / MapFileName,
    false,
    std::nullopt,
    std::nullopt,
  });
  if (exported.is_error())
  {
    finish(std::nullopt, std::get<Error>(exported.error()).msg);
    return;
  }

  m_process = std::make_unique<QProcess>();
  m_process->setProcessChannelMode(QProcess::MergedChannels);
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
        finish(std::nullopt, "could not start the map toolchain");
      }
    });

  runStage();
}

void LightmapBaker::cancel()
{
  m_stage = -1;
  m_callback = nullptr;
  if (m_process && m_process->state() != QProcess::NotRunning)
  {
    m_process->kill();
    m_process->waitForFinished(3000);
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
    R"((?:^|[\r\n])[ \t]*[^\r\n\[]+\[[# ]+\][ \t]+(\d{1,3})%[ \t]+(\d+(?:\.\d+)?)s[ \t])"};
  m_progressBuffer += chunk;
  auto matches = progressLine.globalMatch(QString::fromUtf8(m_progressBuffer));
  auto last = QRegularExpressionMatch{};
  while (matches.hasNext())
    last = matches.next();

  if (last.hasMatch())
  {
    const auto percent = last.captured(1).toInt();
    const auto elapsed = last.captured(2).toDouble();
    const auto seconds =
      percent > 0 && percent < 100 ? std::ceil(elapsed * (100 - percent) / percent) : 0.0;
    emit remainingSecondsChanged(
      seconds > 0 && seconds <= std::numeric_limits<int>::max()
        ? static_cast<int>(seconds)
        : -1);
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
    arguments = {
      QStringLiteral("compile"),
      toQString(m_workDir / MapFileName),
      QStringLiteral("-minlight"),
      QStringLiteral("1"),
      QStringLiteral("-threads"),
      QString::number(m_threadCount)};
    if (m_quality == Preferences::LightmapQualityFast)
      arguments << QStringLiteral("-fast");
    else if (m_quality == Preferences::LightmapQualityUltra)
      arguments << QStringLiteral("-extra");
  }
  else
  {
    arguments = {
      QStringLiteral("lightmap"),
      toQString(m_workDir / BspFileName),
      toQString(m_workDir / AtlasFileName),
      QStringLiteral("-manifest"),
      toQString(m_workDir / ManifestFileName),
      QStringLiteral("-force")};
  }

  m_process->setProgram(toQString(m_executable));
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
    finish(std::nullopt, m_output.empty() ? "the map toolchain failed" : m_output);
    return;
  }

  if (m_stage == 0)
  {
    m_stage = 1;
    emit remainingSecondsChanged(-1);
    runStage();
    return;
  }

  auto error = std::string{};
  auto bake = readLightmapBake(
    m_workDir / ManifestFileName, m_workDir / AtlasFileName, error);
  if (!bake)
  {
    finish(std::nullopt, error);
    return;
  }
  finish(std::move(bake), {});
}

void LightmapBaker::finish(
  std::optional<render::LightmapBake> bake, std::string error)
{
  m_stage = -1;
  if (m_callback)
  {
    auto callback = std::move(m_callback);
    m_callback = nullptr;
    callback(std::move(bake), error);
  }
}

} // namespace tb::ui
