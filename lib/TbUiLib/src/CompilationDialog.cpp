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

#include "ui/CompilationDialog.h"

#include <QApplication>
#include <QBoxLayout>
#include <QClipboard>
#include <QCloseEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>

#include "base/Logger.h"
#include "mdl/CompilationProfile.h"
#include "mdl/GameInfo.h"
#include "mdl/GameManager.h"
#include "mdl/Map.h"
#include "ui/AppController.h"
#include "ui/CompilationProfileManager.h"
#include "ui/CompilationRunner.h"
#include "ui/DialogButtonLayout.h"
#include "ui/FixedWidthFont.h"
#include "ui/LaunchGameEngineDialog.h"
#include "ui/MapDocument.h" // IWYU pragma: keep
#include "ui/MapWindow.h"
#include "ui/ProjectResourceAudit.h"
#include "ui/ProjectSession.h"
#include "ui/QPathUtils.h"
#include "ui/QStyleUtils.h"
#include "ui/Splitter.h"
#include "ui/TitledPanel.h"
#include "ui/ViewConstants.h"

#include "kd/contracts.h"

#include <algorithm>
#include <string>

namespace tb::ui
{
CompilationDialog::CompilationDialog(
  AppController& appController,
  MapDocument& document,
  const gl::PerspectiveCamera& camera,
  QWidget* parent,
  const bool persistProfiles)
  : QDialog{parent}
  , m_appController{appController}
  , m_document{document}
  , m_camera{camera}
  , m_persistProfiles{persistProfiles}
{
  createGui();
  setMinimumSize(760, 580);
  resize(1000, 760);
  updatePlan();
  updateCompileButtons();
}

bool CompilationDialog::selectProfile(const mdl::CompilationProfile& profile)
{
  return m_profileManager->selectProfile(profile);
}

void CompilationDialog::selectFirstProfile()
{
  m_profileManager->selectFirstProfile();
}

void CompilationDialog::runSelectedProfile()
{
  if (m_profileManager->selectedProfile())
  {
    startCompilation(false);
  }
}

bool CompilationDialog::runProfileByName(const std::string& profileName)
{
  const auto& profiles = m_profileManager->config().profiles;
  const auto it = std::ranges::find_if(
    profiles, [&](const auto& profile) { return profile.name == profileName; });
  if (it == std::end(profiles) || it->tasks.empty())
  {
    return false;
  }

  selectProfile(*it);
  startCompilation(false);
  return true;
}

bool CompilationDialog::isRunning() const
{
  return m_run.running();
}

QString CompilationDialog::outputText() const
{
  return m_output->toPlainText();
}

void CompilationDialog::createGui()
{
  setWindowIconTB(this);
  setWindowTitle(tr("Compile"));

  const auto& compilationConfig = m_document.map().gameInfo().compilationConfig;

  m_profileManager =
    new CompilationProfileManager{m_appController, m_document, compilationConfig};

  auto* outputPanel = new QWidget{};
  auto* outputLayout = new QVBoxLayout{outputPanel};
  outputLayout->setContentsMargins(16, 8, 16, 0);
  outputLayout->setSpacing(8);
  auto* outputTools = new QHBoxLayout{};
  auto* outputTitle = new QLabel{tr("Build log")};
  setEmphasizedStyle(outputTitle);
  outputTools->addWidget(outputTitle);
  outputTools->addStretch();
  auto* copyButton = new QPushButton{tr("Copy log")};
  auto* saveButton = new QPushButton{tr("Save log...")};
  m_openOutputButton = new QPushButton{tr("Output folder")};
  for (auto* button : {copyButton, saveButton, m_openOutputButton})
  {
    button->setAutoDefault(false);
    outputTools->addWidget(button);
  }
  m_openOutputButton->setEnabled(false);
  connect(copyButton, &QPushButton::clicked, this, [this] {
    QApplication::clipboard()->setText(m_output->toPlainText());
  });
  connect(saveButton, &QPushButton::clicked, this, &CompilationDialog::saveLog);
  connect(m_openOutputButton, &QPushButton::clicked, this, [this] {
    if (!m_runOutputs.empty())
      QDesktopServices::openUrl(
        QUrl::fromLocalFile(pathAsQString(m_runOutputs.back().parent_path())));
  });
  outputLayout->addLayout(outputTools);
  m_logIdentityLabel = new QLabel{tr("No compilation has run")};
  m_logIdentityLabel->setObjectName("CompilationLogIdentity");
  m_logIdentityLabel->setTextFormat(Qt::PlainText);
  m_logIdentityLabel->setWordWrap(true);
  m_logIdentityLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  setInfoStyle(m_logIdentityLabel);
  outputLayout->addWidget(m_logIdentityLabel);
  m_currentRunLabel = new QLabel{};
  m_currentRunLabel->setObjectName("CompilationRunStatus");
  m_currentRunLabel->setTextFormat(Qt::PlainText);
  m_currentRunLabel->setWordWrap(true);
  outputLayout->addWidget(m_currentRunLabel);
  m_output = new QTextEdit{};
  m_output->setObjectName("CompilationOutput");
  m_output->setReadOnly(true);
  m_output->setPlaceholderText(tr("Compiler output will appear here"));
  m_output->setAcceptRichText(false);
  m_output->setFont(Fonts::fixedWidthFont());
  m_output->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(
    m_output,
    &QWidget::customContextMenuRequested,
    this,
    &CompilationDialog::showContextMenu);
  outputLayout->addWidget(m_output, 1);
  copyButton->setEnabled(false);
  saveButton->setEnabled(false);
  connect(m_output, &QTextEdit::textChanged, this, [this, copyButton, saveButton] {
    const auto hasOutput = !m_output->document()->isEmpty();
    copyButton->setEnabled(hasOutput);
    saveButton->setEnabled(hasOutput);
  });

  auto* splitter = new Splitter{Qt::Vertical};
  splitter->addWidget(m_profileManager);
  splitter->addWidget(outputPanel);
  splitter->setStretchFactor(0, 1);
  splitter->setStretchFactor(1, 1);
  splitter->setSizes({320, 280});
  splitter->setChildrenCollapsible(false);

  m_launchButton = new QPushButton{tr("Launch...")};
  m_launchButton->setAutoDefault(false);
  m_locateButton = new QPushButton{tr("Locate setting")};
  m_locateButton->setObjectName("CompilationLocateSetting");
  m_locateButton->setAutoDefault(false);
  connect(m_locateButton, &QPushButton::clicked, this, [this] {
    if (m_plan.diagnostic)
      m_profileManager->focusDiagnostic(*m_plan.diagnostic);
  });
  m_testCompileButton = new QPushButton{tr("Simulate")};
  m_testCompileButton->setObjectName("CompilationSimulate");
  m_testCompileButton->setToolTip(
    tr("Resolve variables and list commands without running tools"));
  m_stopCompileButton = new QPushButton{tr("Cancel compilation")};
  m_compileButton = new QPushButton{tr("Compile")};
  m_compileButton->setObjectName("CompilationCompile");
  m_closeButton = new QPushButton{tr("Close")};
  auto* buttonLayout = new QHBoxLayout{};
  buttonLayout->setContentsMargins(0, 0, 0, 0);
  buttonLayout->addWidget(m_launchButton);
  buttonLayout->addStretch();
  buttonLayout->addWidget(m_locateButton);
  // Keep action positions stable when the running state swaps Compile for Cancel.
  for (auto* button :
       {m_testCompileButton, m_compileButton, m_stopCompileButton, m_closeButton})
  {
    button->setAutoDefault(false);
    buttonLayout->addWidget(button);
  }

  auto* dialogLayout = new QVBoxLayout{};
  dialogLayout->setContentsMargins(0, 0, 0, 0);
  dialogLayout->setSpacing(0);
  dialogLayout->addWidget(splitter, 1);
  dialogLayout->addLayout(wrapDialogButtonBox(buttonLayout));
  insertTitleBarSeparator(dialogLayout);
  setLayout(dialogLayout);

  auto* timer = new QTimer{this};
  timer->setInterval(500);
  connect(timer, &QTimer::timeout, this, [this] {
    if (m_run.running())
      updateRunStatus();
  });
  timer->start();
  connect(&m_run, &CompilationRun::planReady, this, [this](const auto& plan) {
    m_runOutputs = plan.outputs;
  });
  connect(
    &m_run,
    &CompilationRun::taskChanged,
    this,
    [this](const int index, const auto& name) {
      m_currentTaskName = tr("Task %1 — %2").arg(index + 1).arg(name);
      m_currentStage.clear();
      updateRunStatus();
    });
  connect(&m_run, &CompilationRun::stageChanged, this, [this](const auto& stage) {
    m_currentStage = stage;
    updateRunStatus();
  });
  connect(&m_run, &CompilationRun::compilationFinished, this, [this](const auto outcome) {
    m_outcome = outcome;
  });

  m_compileButton->setDefault(true);

  connect(
    &m_run,
    &CompilationRun::compilationStarted,
    this,
    &CompilationDialog::compilationStarted);
  connect(
    &m_run,
    &CompilationRun::compilationEnded,
    this,
    &CompilationDialog::compilationEnded);
  connect(
    m_profileManager,
    &CompilationProfileManager::selectedProfileChanged,
    this,
    &CompilationDialog::selectedProfileChanged);
  connect(
    m_profileManager,
    &CompilationProfileManager::profileChanged,
    this,
    &CompilationDialog::profileChanged);

  connect(
    m_compileButton, &QPushButton::clicked, this, [&]() { startCompilation(false); });
  connect(
    m_testCompileButton, &QPushButton::clicked, this, [&]() { startCompilation(true); });
  connect(m_stopCompileButton, &QPushButton::clicked, this, [&]() { stopCompilation(); });
  connect(m_launchButton, &QPushButton::clicked, this, [&]() {
    auto dialog = LaunchGameEngineDialog{m_appController, m_document, this};
    dialog.exec();
  });
  connect(m_closeButton, &QPushButton::clicked, this, &CompilationDialog::close);
}

void CompilationDialog::keyPressEvent(QKeyEvent* event)
{
  // Dismissing the dialog with Escape doesn't invoke CompilationDialog::closeEvent, so
  // handle it here, so we can potentially block it.
  if (event->key() == Qt::Key_Escape)
  {
    close();
  }
  else
  {
    QDialog::keyPressEvent(event);
  }
}

void CompilationDialog::updateCompileButtons()
{
  const auto* profile = m_profileManager->selectedProfile();

  const auto ready = profile && !m_plan.diagnostic;
  m_compileButton->setEnabled(!m_run.running() && ready);
  m_testCompileButton->setEnabled(!m_run.running() && ready);
  m_compileButton->setVisible(!m_run.running());
  m_testCompileButton->setVisible(!m_run.running());
  m_stopCompileButton->setVisible(m_run.running());
  m_stopCompileButton->setEnabled(m_run.running());
  m_profileManager->setEnabled(!m_run.running());
  m_locateButton->setVisible(!m_run.running() && m_plan.diagnostic.has_value());
}

void CompilationDialog::updatePlan()
{
  const auto* profile = m_profileManager->selectedProfile();
  if (!profile)
  {
    m_sourceIdentity.clear();
    m_profileManager->setDiagnostic(std::nullopt);
    m_plan = {};
    return;
  }
  const auto& map = m_document.map();
  auto mapPath = map.path();
  auto projectInfo = std::optional<ProjectCompileInfo>{};
  auto source = pathAsQString(mapPath);
  auto frozen = false;
  if (auto project = m_appController.projectFor(m_document))
  {
    const auto assetId = project->assetFor(m_document);
    if (assetId)
    {
      const auto asset = project->store().findAsset(*assetId);
      if (asset.is_success() && asset.value())
      {
        auto name = asset.value()->displayName;
        if (name.empty())
          name = std::filesystem::path{asset.value()->publishPath}.stem().string();
        projectInfo = ProjectCompileInfo{project->path(), name};
        source =
          tr("%1 / %2").arg(pathAsQString(project->path()), QString::fromStdString(name));
        frozen = !isDirectProjectCompilation(*profile);
        // A snapshot does not exist yet. Only its filename and persistent outputs
        // are predictable; temporary paths are displayed when the run is prepared.
        mapPath = project->path().parent_path()
                  / std::filesystem::path{asset.value()->publishPath}.filename();
      }
    }
  }
  m_plan = buildCompilationPlan(*profile, map, mapPath, projectInfo, !frozen);
  m_sourceIdentity = (projectInfo ? tr("Project: %1") : tr("Map: %1")).arg(source);
  m_profileManager->setDiagnostic(m_plan.diagnostic);
}

void CompilationDialog::startCompilation(const bool test)
{
  if (m_run.running())
    return;
  updatePlan();
  const auto* profile = m_profileManager->selectedProfile();
  if (!profile)
    return;
  saveProfile();
  if (m_plan.diagnostic)
  {
    m_profileManager->focusDiagnostic(*m_plan.diagnostic);
    return;
  }
  m_simulation = test;
  m_outcome.reset();
  m_currentTaskName.clear();
  m_currentStage.clear();
  m_runOutputs.clear();
  m_openOutputButton->setEnabled(false);
  m_runProfileName = QString::fromStdString(profile->name);
  m_runIdentity = tr("%1 · %2 · %3")
                    .arg(
                      m_runProfileName,
                      m_sourceIdentity,
                      QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
  m_logIdentityLabel->setText(
    m_runIdentity + (test ? " · " + tr("Simulation") : QString{}));
  m_runTimer.start();
  m_output->clear();
  m_currentRunLabel->setText(tr("Preparing input"));
  runProfile(*profile, test) | kdl::transform_error([&](const auto& e) {
    m_outcome = CompilationOutcome::Failed;
    m_output->insertPlainText(
      tr("Compilation failed: %1").arg(QString::fromStdString(e.msg)));
    if (m_run.diagnostic() && m_run.diagnostic()->message.toStdString() == e.msg)
    {
      m_plan.diagnostic = m_run.diagnostic();
      m_profileManager->setDiagnostic(m_plan.diagnostic);
    }
    updateRunStatus();
    updateCompileButtons();
  });
}

void CompilationDialog::updateRunStatus()
{
  if (!m_runTimer.isValid())
    return;
  const auto seconds = QString::number(double(m_runTimer.elapsed()) / 1000.0, 'f', 1);
  auto status = m_simulation ? tr("Simulating") : tr("Running");
  if (m_outcome)
  {
    switch (*m_outcome)
    {
    case CompilationOutcome::Succeeded:
      status = m_simulation ? tr("Simulation completed") : tr("Succeeded");
      break;
    case CompilationOutcome::Failed:
      status = tr("Failed");
      break;
    case CompilationOutcome::Cancelled:
      status = tr("Cancelled");
      break;
    }
  }
  m_currentRunLabel->setText(
    tr("%1 · %2 s%3%4")
      .arg(
        status,
        seconds,
        m_currentTaskName.isEmpty() ? QString{} : " · " + m_currentTaskName,
        m_currentStage.isEmpty() ? QString{} : " · " + m_currentStage));
  if (m_outcome == CompilationOutcome::Failed)
    setErrorStyle(m_currentRunLabel);
  else
    setDefaultStyle(m_currentRunLabel);
}

void CompilationDialog::saveLog()
{
  const auto path = QFileDialog::getSaveFileName(
    this,
    tr("Save compilation log"),
    "compile.log",
    tr("Log files (*.log);;All files (*)"));
  if (path.isEmpty())
    return;
  auto file = QSaveFile{path};
  const auto bytes = m_output->toPlainText().toUtf8();
  if (
    !file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()
    || !file.commit())
    QMessageBox::warning(
      this,
      tr("Save compilation log"),
      tr("Could not save log: %1").arg(file.errorString()));
}

Result<void> CompilationDialog::runProfile(
  const mdl::CompilationProfile& profile, const bool test)
{
  const auto& map = m_document.map();
  if (auto project = m_appController.projectFor(m_document))
  {
    // The compiler reads the project from disk, so unsaved edits are written first; a
    // failed save aborts instead of compiling an older state.
    if (project->modified())
    {
      if (const auto saved = project->save(); saved.is_error())
        return std::get<Error>(saved.error());
    }

    // A single project-only tool reads the project file directly. Profiles with map
    // tasks or map path arguments need a frozen snapshot with the captured resources.
    const auto assetId = project->assetFor(m_document);
    if (!assetId)
      return Error{"This document is not a project map."};
    const auto terrainNeedsSave = project->terrainCompileNeedsSave(*assetId);
    if (terrainNeedsSave.is_error())
      return std::get<Error>(terrainNeedsSave.error());
    if (terrainNeedsSave.value())
    {
      if (const auto saved = project->save(); saved.is_error())
        return std::get<Error>(saved.error());
    }
    const auto asset = project->store().findAsset(*assetId);
    if (asset.is_error())
      return std::get<Error>(asset.error());
    if (!asset.value())
      return Error{"The project map no longer exists."};

    const auto settings = project->store().publishSettings();
    if (settings.is_error())
      return std::get<Error>(settings.error());
    const auto target =
      pathFromQString(QString::fromStdString(settings.value().gameDirectory));
    auto mapName = asset.value()->displayName;
    if (mapName.empty())
      mapName = std::filesystem::path{asset.value()->publishPath}.stem().string();
    const auto projectInfo = ProjectCompileInfo{project->path(), mapName};

    if (isDirectProjectCompilation(profile))
    {
      const auto audit = auditGoldSrcProjectResources(*project, *assetId);
      if (audit.is_error())
        return std::get<Error>(audit.error());
      if (const auto valid = validateGoldSrcCompileResources(audit.value(), *assetId);
          valid.is_error())
        return std::get<Error>(valid.error());
      if (!test && !target.empty())
        if (const auto published = project->publishCompileResources(*assetId, target);
            published.is_error())
          return std::get<Error>(published.error());
      return m_run.runProjectTool(profile, map, m_camera, m_output, projectInfo, test);
    }

    return prepareProjectSnapshot(*project)
           | kdl::and_then([&](const auto& mapPath) -> Result<void> {
               if (!test && !target.empty())
                 if (const auto published = ProjectSession::publishSnapshotResources(
                       mapPath.parent_path(), target);
                     published.is_error())
                   return std::get<Error>(published.error());
               return m_document.prepareReload(mapPath)
                      | kdl::and_then([&](auto frozenMap) {
                          auto snapshot =
                            std::shared_ptr<const mdl::Map>{std::move(frozenMap)};
                          return test
                                   ? m_run.testSnapshot(
                                       profile, snapshot, m_camera, m_output, projectInfo)
                                   : m_run.runSnapshot(
                                       profile,
                                       snapshot,
                                       m_camera,
                                       m_output,
                                       projectInfo);
                        });
             });
  }
  return test ? m_run.test(profile, map, m_camera, m_output)
              : m_run.run(profile, map, m_camera, m_output);
}

Result<std::filesystem::path> CompilationDialog::prepareProjectSnapshot(
  ProjectSession& project)
{
  const auto assetId = project.assetFor(m_document);
  if (!assetId)
    return Error{"This document is not a project map."};
  const auto asset = project.store().findAsset(*assetId);
  if (asset.is_error())
    return std::get<Error>(asset.error());
  if (!asset.value())
    return Error{"The project map no longer exists."};

  auto folder = std::make_unique<QTemporaryDir>();
  if (!folder->isValid())
    return Error{"Could not create a folder for the compile snapshot."};
  const auto folderPath = pathFromQString(folder->path());
  // The exported file keeps the map's own name, so profiles that derive their outputs
  // from
  // ${MAP_BASE_NAME} produce the same artifact names as a direct compile.
  const auto fileName =
    pathFromQString(QString::fromStdString(asset.value()->publishPath)).filename();
  if (const auto exported =
        project.exportCompileSnapshot(*assetId, folderPath, fileName.string());
      exported.is_error())
    return std::get<Error>(exported.error());

  const auto mapPath = folderPath / fileName;
  m_snapshots.push_back(std::move(folder));
  return mapPath;
}

void CompilationDialog::stopCompilation()
{
  if (m_run.running())
  {
    m_run.terminate();
  }
}

void CompilationDialog::closeEvent(QCloseEvent* event)
{
  if (m_run.running())
  {
    const auto result = QMessageBox::warning(
      this,
      tr("Warning"),
      tr("Closing this dialog will stop the running compilation. Are you sure?"),
      QMessageBox::Yes | QMessageBox::No,
      QMessageBox::Yes);

    if (result != QMessageBox::Yes)
    {
      event->ignore();
      return;
    }

    stopCompilation();
  }
  saveProfile();
  event->accept();
}

void CompilationDialog::compilationStarted()
{
  emit compilationProfileStarted(m_runProfileName.toStdString());
  m_output->insertPlainText("#### " + m_runIdentity + "\n");
  for (const auto& output : m_runOutputs)
    m_output->insertPlainText("#### BSP: " + pathAsQString(output) + "\n");
  updateRunStatus();
  updateCompileButtons();
}

void CompilationDialog::compilationEnded()
{
  updateRunStatus();
  if (m_outcome == CompilationOutcome::Failed)
  {
    // The result is typed; the log only supplies a concise human-readable detail.
    const auto lines = m_output->toPlainText().split('\n');
    for (auto it = lines.crbegin(); it != lines.crend(); ++it)
      if (
        it->startsWith("#### Execution failed:")
        || it->startsWith("#### Compilation failed:") || it->startsWith("#### Error")
        || it->startsWith("#### Finished with exit code"))
      {
        m_currentRunLabel->setText(
          m_currentRunLabel->text() + "\n" + it->mid(5).left(500));
        break;
      }
  }
  if (!m_runOutputs.empty())
    m_openOutputButton->setEnabled(
      QDir{pathAsQString(m_runOutputs.back().parent_path())}.exists());
  updateCompileButtons();
}

void CompilationDialog::selectedProfileChanged()
{
  updatePlan();
  updateCompileButtons();
}

void CompilationDialog::profileChanged()
{
  updatePlan();
  updateCompileButtons();
}

void CompilationDialog::showContextMenu(const QPoint& pos)
{
  auto* menu = m_output->createStandardContextMenu();
  menu->setAttribute(Qt::WA_DeleteOnClose);
  menu->addSeparator();
  menu->addAction(tr("Clear"), m_output, &QTextEdit::clear);
  menu->popup(m_output->mapToGlobal(pos));
}

void CompilationDialog::saveProfile()
{
  if (!m_persistProfiles)
    return;
  const auto& map = m_document.map();
  const auto& gameName = map.gameInfo().gameConfig.name;

  auto& gameManager = m_appController.gameManager();
  gameManager.updateCompilationConfig(
    gameName, m_profileManager->config(), m_document.logger())
    | kdl::transform_error([&](const auto& e) { m_document.logger().error() << e.msg; });
}

} // namespace tb::ui
