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

#pragma once

#include <QObject>

#include "base/Result.h"
#include "ui/CompilationPlan.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

class QTextEdit;

namespace tb
{
class VariableTable;

namespace gl
{
class PerspectiveCamera;
}

namespace mdl
{
class Map;

struct CompilationProfile;
} // namespace mdl

namespace ui
{
class CompilationRunner;

/** Whether a profile consists of one enabled tool that takes its input from a project. */
bool isDirectProjectCompilation(const mdl::CompilationProfile& profile);

class CompilationRun : public QObject
{
  Q_OBJECT
private:
  CompilationRunner* m_currentRun{nullptr};
  std::optional<CompilationDiagnostic> m_diagnostic;

public:
  ~CompilationRun() override;

  bool running() const;
  const std::optional<CompilationDiagnostic>& diagnostic() const;
  /** Runs the profile against the live map. */
  Result<void> run(
    const mdl::CompilationProfile& profile,
    const mdl::Map& map,
    const gl::PerspectiveCamera& camera,
    QTextEdit* currentOutput,
    std::optional<ProjectCompileInfo> project = std::nullopt);
  Result<void> test(
    const mdl::CompilationProfile& profile,
    const mdl::Map& map,
    const gl::PerspectiveCamera& camera,
    QTextEdit* currentOutput,
    std::optional<ProjectCompileInfo> project = std::nullopt);
  /** Runs a project-only tool without retaining the live map or camera. */
  Result<void> runProjectTool(
    const mdl::CompilationProfile& profile,
    const mdl::Map& map,
    const gl::PerspectiveCamera& camera,
    QTextEdit* currentOutput,
    ProjectCompileInfo project,
    bool test);
  /**
   * Runs the profile against a frozen map file.
   *
   * The context owns the frozen map for the entire run, including Export Map tasks.
   */
  Result<void> runSnapshot(
    const mdl::CompilationProfile& profile,
    std::shared_ptr<const mdl::Map> map,
    const gl::PerspectiveCamera& camera,
    QTextEdit* currentOutput,
    std::optional<ProjectCompileInfo> project = std::nullopt);
  Result<void> testSnapshot(
    const mdl::CompilationProfile& profile,
    std::shared_ptr<const mdl::Map> map,
    const gl::PerspectiveCamera& camera,
    QTextEdit* currentOutput,
    std::optional<ProjectCompileInfo> project = std::nullopt);
  void terminate();

private:
  bool doIsRunning() const;
  Result<void> run(
    const mdl::CompilationProfile& profile,
    const mdl::Map& map,
    const gl::PerspectiveCamera& camera,
    QTextEdit* currentOutput,
    bool test,
    const std::optional<std::filesystem::path>& mapPath,
    std::shared_ptr<const mdl::Map> snapshot = {},
    std::optional<ProjectCompileInfo> project = std::nullopt,
    bool projectToolOnly = false);

private:
  void cleanup();
signals:
  void compilationStarted();
  void planReady(const CompilationPlan& plan);
  void taskChanged(int index, const QString& name);
  void stageChanged(const QString& stage);
  void compilationFinished(CompilationOutcome outcome);
  void compilationEnded();
};

} // namespace ui
} // namespace tb
