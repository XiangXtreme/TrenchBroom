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

#pragma once

#include <QByteArray>
#include <QObject>

#include "base/Result.h"
#include "render/LightmapData.h"
#include "ui/CompilationRun.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

class QProcess;
class QTemporaryDir;

namespace tb::mdl
{
class Map;
}

namespace tb::ui
{

/**
 * Bakes a lightmap for a map by running the map toolchain in a scratch directory.
 *
 * The toolchain runs in its own process: baking takes seconds, and a compiler that
 * gives up on a malformed map must not take the editor with it.
 *
 * The recipe is two calls - compile the map, then export the lightmap with its
 * manifest - and the result comes back as a bake that can be turned into a lightmap
 * atlas.
 */
class LightmapBaker : public QObject
{
  Q_OBJECT
public:
  using Callback = std::function<void(
    std::optional<render::LightmapBake> bake, const std::string& error)>;
  using ProjectInfoProvider = std::function<Result<std::optional<ProjectCompileInfo>>(
    const std::filesystem::path& scratchDirectory)>;

  explicit LightmapBaker(QObject* parent = nullptr);
  ~LightmapBaker() override;

  bool running() const;

  /**
   * Starts a bake, replacing any bake that is still running. `executable` is the map
   * toolchain binary; `threadCount` caps how many cores it may use, so that the editor
   * stays responsive while it works.
   */
  void start(
    const mdl::Map& map,
    const std::filesystem::path& executable,
    int threadCount,
    const std::string& quality,
    Callback callback,
    ProjectInfoProvider projectInfoProvider = {});

  void cancel();

  /**
   * The output of the last run, for diagnosing a failed bake.
   */
  const std::string& output() const;

signals:
  void remainingSecondsChanged(int seconds);

private:
  void readOutput();
  void runStage();
  void stageFinished(int exitCode, int exitStatus);
  void finish(std::optional<render::LightmapBake> bake, std::string error);

  std::unique_ptr<QProcess> m_process;
  std::shared_ptr<QTemporaryDir> m_workDir;
  std::filesystem::path m_executable;
  std::string m_quality;
  std::string m_output;
  QByteArray m_progressBuffer;
  int m_stage = -1;
  int m_threadCount = 1;
  Callback m_callback;
  std::optional<ProjectCompileInfo> m_project;
};

} // namespace tb::ui
