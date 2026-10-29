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

#include <QDialog>

#include "ui/CompilationRun.h"

#include <filesystem>
#include <memory>
#include <vector>

class QLabel;
class QPushButton;
class QTextEdit;
class QTemporaryDir;

namespace tb
{
namespace gl
{
class PerspectiveCamera;
}

namespace mdl
{
struct CompilationProfile;
}

namespace ui
{
class AppController;
class CompilationProfileManager;
class MapDocument;
class ProjectSession;

class CompilationDialog : public QDialog
{
  Q_OBJECT
private:
  AppController& m_appController;
  MapDocument& m_document;
  const gl::PerspectiveCamera& m_camera;

  CompilationProfileManager* m_profileManager = nullptr;
  QPushButton* m_launchButton = nullptr;
  QPushButton* m_compileButton = nullptr;
  QPushButton* m_testCompileButton = nullptr;
  QPushButton* m_stopCompileButton = nullptr;
  QPushButton* m_closeButton = nullptr;
  QLabel* m_currentRunLabel = nullptr;
  QTextEdit* m_output = nullptr;
  CompilationRun m_run;
  // One frozen input folder per run; kept alive until the dialog closes so a finished
  // run's output directory can still be inspected.
  std::vector<std::unique_ptr<QTemporaryDir>> m_snapshots;

public:
  explicit CompilationDialog(
    AppController& appController,
    MapDocument& document,
    const gl::PerspectiveCamera& camera,
    QWidget* parent = nullptr);

  bool selectProfile(const mdl::CompilationProfile& profile);
  void selectFirstProfile();

  void runSelectedProfile();
  bool runProfileByName(const std::string& profileName);
  bool isRunning() const;
  QString outputText() const;

signals:
  void compilationProfileStarted(const std::string& profileName);

private:
  void createGui();

  void keyPressEvent(QKeyEvent* event) override;

  void updateCompileButtons();
  void startCompilation(bool test);
  Result<void> runProfile(const mdl::CompilationProfile& profile, bool test);
  /**
   * Exports the current project map into a fresh folder and returns the frozen map file.
   *
   * Each run gets its own folder, so an input can never be rewritten by a later edit or
   * by a second run while the first one is still compiling.
   */
  Result<std::filesystem::path> prepareProjectSnapshot(ProjectSession& project);
  void stopCompilation();
  void closeEvent(QCloseEvent* event) override;
private slots:
  void compilationStarted();
  void compilationEnded();

  void selectedProfileChanged();
  void profileChanged();
  void showContextMenu(const QPoint& pos);

private:
  void saveProfile();
};

} // namespace ui
} // namespace tb
