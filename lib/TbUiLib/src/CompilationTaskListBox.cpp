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

#include "ui/CompilationTaskListBox.h"

#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>

#include "el/Interpolate.h"
#include "mdl/CompilationProfile.h"
#include "mdl/CompilationTask.h"
#include "mdl/GameInfo.h"
#include "mdl/Map.h"
#include "ui/BorderLine.h"
#include "ui/CollapsibleTitledPanel.h"
#include "ui/CompilationVariables.h"
#include "ui/FileDialogDefaultDir.h"
#include "ui/FixedWidthFont.h"
#include "ui/HltoolsOptionsEditor.h"
#include "ui/MapDocument.h"
#include "ui/MultiCompletionLineEdit.h"
#include "ui/PanelLayout.h"
#include "ui/QStyleUtils.h"
#include "ui/TitleBar.h"
#include "ui/TitledPanel.h"
#include "ui/VariableStoreModel.h"
#include "ui/ViewConstants.h"

#include "kd/contracts.h"
#include "kd/overload.h"

namespace tb::ui
{
// CompilationTaskEditorBase

CompilationTaskEditorBase::CompilationTaskEditorBase(
  QString title,
  MapDocument& document,
  mdl::CompilationProfile& profile,
  mdl::CompilationTask& task,
  QWidget* parent)
  : ControlListBoxItemRenderer{parent}
  , m_title{std::move(title)}
  , m_document{document}
  , m_profile{profile}
  , m_task{task}
{
  // request customContextMenuRequested() to be emitted
  setContextMenuPolicy(Qt::CustomContextMenu);

  auto* panel = new TitledPanel{m_title};
  m_titleBar = panel->getTitleBar();
  m_titleBar->setAutoFillBackground(true);

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(panel);
  layout->addWidget(new BorderLine{});
  setLayout(layout);

  m_enabledCheckbox = new QCheckBox{tr("Enabled")};
  m_enabledCheckbox->setToolTip(
    tr("Whether to include this task when running the compile profile"));

  m_taskLayout = new QHBoxLayout{};
  m_taskLayout->setContentsMargins(0, 0, 0, 0);
  panel->getTitleBar()->layout()->addWidget(m_enabledCheckbox);
  // Subclasses add the form below the task's title and enable toggle.
  panel->getPanel()->setLayout(m_taskLayout);

  connect(m_enabledCheckbox, &QCheckBox::clicked, this, [&](const bool checked) {
    std::visit([&](auto& t) { t.enabled = checked; }, m_task);
  });
}

void CompilationTaskEditorBase::setupCompleter(MultiCompletionLineEdit* lineEdit)
{
  auto* completer = new QCompleter{};
  completer->setCaseSensitivity(Qt::CaseInsensitive);
  lineEdit->setMultiCompleter(completer);
  lineEdit->setWordDelimiters(QRegularExpression{"\\$"}, QRegularExpression{"\\}"});

  m_completers.push_back(completer);
  updateCompleter(completer);
}

void CompilationTaskEditorBase::addMainLayout(QLayout* layout)
{
  m_taskLayout->addLayout(layout, 1);
}

void CompilationTaskEditorBase::updateItem()
{
  std::visit([&](const auto& t) { m_enabledCheckbox->setChecked(t.enabled); }, m_task);
}

void CompilationTaskEditorBase::setSelected(
  const bool selected, const QListWidget* listWidget)
{
  // A task is an editable form. Keep selection feedback in its header rather
  // than changing the surface and label colors of every field and diagnostic.
  ControlListBoxItemRenderer::setSelected(false, listWidget);
  m_titleBar->setBackgroundRole(selected ? QPalette::AlternateBase : QPalette::Base);
  auto headerPalette = palette();
  for (const auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled})
  {
    headerPalette.setColor(
      group,
      QPalette::AlternateBase,
      listWidget->palette().color(group, QPalette::AlternateBase));
    headerPalette.setColor(
      group, QPalette::WindowText, listWidget->palette().color(group, QPalette::Text));
  }
  m_titleBar->setPalette(headerPalette);
}

void CompilationTaskEditorBase::setDiagnostic(
  const std::optional<CompilationDiagnostic>& diagnostic)
{
  auto* form = qobject_cast<QFormLayout*>(m_taskLayout->itemAt(0)->layout());
  if (!diagnostic)
  {
    if (m_diagnosticLabel)
      form->setRowVisible(m_diagnosticLabel, false);
    return;
  }
  if (m_diagnosticLabel && m_diagnosticField != diagnostic->field)
  {
    form->removeRow(m_diagnosticLabel);
    m_diagnosticLabel = nullptr;
  }
  if (!m_diagnosticLabel)
  {
    auto row = form->rowCount();
    for (auto* input : findChildren<QWidget*>())
      if (
        input->property("compilationField").isValid()
        && input->property("compilationField").toInt() == int(diagnostic->field))
        for (int i = 0; i < form->rowCount(); ++i)
          if (
            auto* item = form->itemAt(i, QFormLayout::FieldRole);
            item
            && (item->widget() == input || (item->layout() && item->layout()->indexOf(input) >= 0)))
            row = i + 1;
    m_diagnosticLabel = new QLabel{};
    m_diagnosticLabel->setObjectName("CompilationTaskDiagnostic");
    m_diagnosticLabel->setTextFormat(Qt::PlainText);
    m_diagnosticLabel->setWordWrap(true);
    m_diagnosticLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    setErrorStyle(m_diagnosticLabel);
    form->insertRow(row, QString{}, m_diagnosticLabel);
    m_diagnosticField = diagnostic->field;
  }
  m_diagnosticLabel->setText(
    diagnostic->summary.isEmpty() ? diagnostic->message : diagnostic->summary);
  m_diagnosticLabel->setToolTip(diagnostic->message);
  form->setRowVisible(m_diagnosticLabel, true);
}

void CompilationTaskEditorBase::updateCompleter(QCompleter* completer)
{
  const auto& map = m_document.map();
  const auto workDir =
    el::interpolate(CompilationWorkDirVariables{map}, m_profile.workDirSpec)
    | kdl::value_or(std::string{});

  const auto variables = CompilationVariables{map, workDir};
  completer->setModel(new VariableStoreModel{variables});
}

// CompilationExportMapTaskEditor

CompilationExportMapTaskEditor::CompilationExportMapTaskEditor(
  MapDocument& document,
  mdl::CompilationProfile& profile,
  mdl::CompilationTask& task,
  QWidget* parent)
  : CompilationTaskEditorBase{"Export Map", document, profile, task, parent}
{
  contract_pre(std::holds_alternative<mdl::CompilationExportMap>(task));

  auto* formLayout = createPanelForm(nullptr, PanelInsets::Content);
  addMainLayout(formLayout);

  m_targetEditor = new MultiCompletionLineEdit{};
  m_targetEditor->setProperty("compilationField", int(CompilationField::Target));
  m_targetEditor->setFont(Fonts::fixedWidthFont());
  m_targetEditor->setToolTip(R"(The path of the exported file.
Variables are allowed.)");
  setupCompleter(m_targetEditor);
  formLayout->addRow("File Path", m_targetEditor);

  m_stripEntityPattern = new QLineEdit{};
  m_stripEntityPattern->setToolTip(
    tr("Set a GLOB pattern to strip any entities with a matching classname. Example: "
       "'info_player_*' to strip all info_player_start and all info_player_deatchmatch "
       "entities."));
  m_stripEntityPattern->setPlaceholderText("enter GLOB pattern");
  formLayout->addRow("Strip Entities", m_stripEntityPattern);

  m_dropEntity = new QLineEdit{};
  m_dropEntity->setToolTip(tr(
    "Set the classname of an entity that will be added to the map. The entities' origin "
    "and angles will be set to the camera position and view direction, respectively."));
  m_dropEntity->setPlaceholderText("enter classname");
  formLayout->addRow("Add Entity", m_dropEntity);

  m_stripTbProperties = new QCheckBox{"Strip TrenchBroom specific entity properties"};
  m_stripTbProperties->setToolTip(
    tr("Set this checkbox to strip any entity properties starting with _tb_ from the "
       "exported map file. Some compilers cannot handle these properties."));
  formLayout->addRow("", m_stripTbProperties);

  connect(
    m_targetEditor,
    &QLineEdit::textChanged,
    this,
    &CompilationExportMapTaskEditor::targetSpecChanged);
  connect(
    m_stripEntityPattern,
    &QLineEdit::textChanged,
    this,
    &CompilationExportMapTaskEditor::stripEntityPatternChanged);
  connect(
    m_dropEntity,
    &QLineEdit::textChanged,
    this,
    &CompilationExportMapTaskEditor::dropEntityChanged);
  connect(
    m_stripTbProperties,
    &QCheckBox::checkStateChanged,
    this,
    &CompilationExportMapTaskEditor::stripTbPropertiesChanged);
}

void CompilationExportMapTaskEditor::updateItem()
{
  CompilationTaskEditorBase::updateItem();

  const auto targetSpec = QString::fromStdString(task().targetSpec);
  if (m_targetEditor->text() != targetSpec)
  {
    m_targetEditor->setText(targetSpec);
  }

  if (m_stripTbProperties->isChecked() != task().stripTbProperties)
  {
    m_stripTbProperties->setCheckState(
      task().stripTbProperties ? Qt::CheckState::Checked : Qt::CheckState::Unchecked);
  }

  const auto stripEntityPattern =
    QString::fromStdString(task().stripEntityPattern.value_or(""));
  if (m_stripEntityPattern->text() != stripEntityPattern)
  {
    m_stripEntityPattern->setText(stripEntityPattern);
  }

  const auto dropEntity =
    QString::fromStdString(task().entityToAdd ? task().entityToAdd->classname() : "");
  if (m_dropEntity->text() != dropEntity)
  {
    m_dropEntity->setText(dropEntity);
  }
}

mdl::CompilationExportMap& CompilationExportMapTaskEditor::task()
{
  // This is safe because we know what type of task the editor was initialized with.
  // We have to do this to avoid using a template as the base class.
  return std::get<mdl::CompilationExportMap>(m_task);
}

void CompilationExportMapTaskEditor::targetSpecChanged(const QString& text)
{
  task().targetSpec = text.toStdString();
}

void CompilationExportMapTaskEditor::dropEntityChanged(const QString& text)
{
  if (text.isEmpty())
  {
    task().entityToAdd = std::nullopt;
  }
  else
  {
    task().entityToAdd = mdl::Entity{{
      {mdl::EntityPropertyKeys::Classname, text.toStdString()},
    }};
  }
}

void CompilationExportMapTaskEditor::stripEntityPatternChanged(const QString& text)
{
  if (text.isEmpty())
  {
    task().stripEntityPattern = std::nullopt;
  }
  else
  {
    task().stripEntityPattern = text.toStdString();
  }
}

void CompilationExportMapTaskEditor::stripTbPropertiesChanged(const int state)
{
  const auto value = (state == Qt::Checked);
  task().stripTbProperties = value;
}

CompilationCopyFilesTaskEditor::CompilationCopyFilesTaskEditor(
  MapDocument& document,
  mdl::CompilationProfile& profile,
  mdl::CompilationTask& task,
  QWidget* parent)
  : CompilationTaskEditorBase{"Copy Files", document, profile, task, parent}
{
  contract_pre(std::holds_alternative<mdl::CompilationCopyFiles>(task));

  auto* formLayout = createPanelForm(nullptr, PanelInsets::Content);
  addMainLayout(formLayout);

  m_sourceEditor = new MultiCompletionLineEdit{};
  m_sourceEditor->setProperty("compilationField", int(CompilationField::Source));
  m_sourceEditor->setFont(Fonts::fixedWidthFont());
  m_sourceEditor->setToolTip(R"(The file(s) or directory to copy.

Use wildcards (*,?) in the filename to specify more than one file.
A directory copies all its contents, including subdirectories, into the target.
Variables are allowed.)");
  setupCompleter(m_sourceEditor);
  formLayout->addRow("Source File Path", m_sourceEditor);

  m_targetEditor = new MultiCompletionLineEdit{};
  m_targetEditor->setProperty("compilationField", int(CompilationField::Target));
  m_targetEditor->setFont(Fonts::fixedWidthFont());
  m_targetEditor->setToolTip(R"(The directory to copy the files to.

The directory is recursively created if it does not exist.
Existing files are overwritten without prompt.
Variables are allowed.)");
  setupCompleter(m_targetEditor);
  formLayout->addRow("Target Directory Path", m_targetEditor);

  connect(
    m_sourceEditor,
    &QLineEdit::textChanged,
    this,
    &CompilationCopyFilesTaskEditor::sourceSpecChanged);
  connect(
    m_targetEditor,
    &QLineEdit::textChanged,
    this,
    &CompilationCopyFilesTaskEditor::targetSpecChanged);
}

void CompilationCopyFilesTaskEditor::updateItem()
{
  CompilationTaskEditorBase::updateItem();

  const auto sourceSpec = QString::fromStdString(task().sourceSpec);
  if (m_sourceEditor->text() != sourceSpec)
  {
    m_sourceEditor->setText(sourceSpec);
  }

  const auto targetSpec = QString::fromStdString(task().targetSpec);
  if (m_targetEditor->text() != targetSpec)
  {
    m_targetEditor->setText(targetSpec);
  }
}

mdl::CompilationCopyFiles& CompilationCopyFilesTaskEditor::task()
{
  // This is safe because we know what type of task the editor was initialized with.
  // We have to do this to avoid using a template as the base class.
  return std::get<mdl::CompilationCopyFiles>(m_task);
}

void CompilationCopyFilesTaskEditor::sourceSpecChanged(const QString& text)
{
  task().sourceSpec = text.toStdString();
}

void CompilationCopyFilesTaskEditor::targetSpecChanged(const QString& text)
{
  task().targetSpec = text.toStdString();
}

CompilationRenameFileTaskEditor::CompilationRenameFileTaskEditor(
  MapDocument& document,
  mdl::CompilationProfile& profile,
  mdl::CompilationTask& task,
  QWidget* parent)
  : CompilationTaskEditorBase{"Rename File", document, profile, task, parent}
{
  contract_pre(std::holds_alternative<mdl::CompilationRenameFile>(task));

  auto* formLayout = createPanelForm(nullptr, PanelInsets::Content);
  addMainLayout(formLayout);

  m_sourceEditor = new MultiCompletionLineEdit{};
  m_sourceEditor->setProperty("compilationField", int(CompilationField::Source));
  m_sourceEditor->setFont(Fonts::fixedWidthFont());
  m_sourceEditor->setToolTip(R"(The file to rename or move.

Wildcards are not supported.
Variables are allowed.)");
  setupCompleter(m_sourceEditor);
  formLayout->addRow("Source File Path", m_sourceEditor);

  m_targetEditor = new MultiCompletionLineEdit{};
  m_targetEditor->setProperty("compilationField", int(CompilationField::Target));
  m_targetEditor->setFont(Fonts::fixedWidthFont());
  m_targetEditor->setToolTip(R"(The new path for the file.

The path must end in a filename.
The containing directory is recursively created if it does not exist.
Existing files are overwritten without prompt.
Variables are allowed.)");
  setupCompleter(m_targetEditor);
  formLayout->addRow("Target File Path", m_targetEditor);

  connect(
    m_sourceEditor,
    &QLineEdit::textChanged,
    this,
    &CompilationRenameFileTaskEditor::sourceSpecChanged);
  connect(
    m_targetEditor,
    &QLineEdit::textChanged,
    this,
    &CompilationRenameFileTaskEditor::targetSpecChanged);
}

void CompilationRenameFileTaskEditor::updateItem()
{
  CompilationTaskEditorBase::updateItem();

  const auto sourceSpec = QString::fromStdString(task().sourceSpec);
  if (m_sourceEditor->text() != sourceSpec)
  {
    m_sourceEditor->setText(sourceSpec);
  }

  const auto targetSpec = QString::fromStdString(task().targetSpec);
  if (m_targetEditor->text() != targetSpec)
  {
    m_targetEditor->setText(targetSpec);
  }
}

mdl::CompilationRenameFile& CompilationRenameFileTaskEditor::task()
{
  // This is safe because we know what type of task the editor was initialized with.
  // We have to do this to avoid using a template as the base class.
  return std::get<mdl::CompilationRenameFile>(m_task);
}

void CompilationRenameFileTaskEditor::sourceSpecChanged(const QString& text)
{
  task().sourceSpec = text.toStdString();
}

void CompilationRenameFileTaskEditor::targetSpecChanged(const QString& text)
{
  task().targetSpec = text.toStdString();
}

CompilationDeleteFilesTaskEditor::CompilationDeleteFilesTaskEditor(
  MapDocument& document,
  mdl::CompilationProfile& profile,
  mdl::CompilationTask& task,
  QWidget* parent)
  : CompilationTaskEditorBase{"Delete Files", document, profile, task, parent}
{
  contract_pre(std::holds_alternative<mdl::CompilationDeleteFiles>(task));

  auto* formLayout = createPanelForm(nullptr, PanelInsets::Content);
  addMainLayout(formLayout);

  m_targetEditor = new MultiCompletionLineEdit{};
  m_targetEditor->setProperty("compilationField", int(CompilationField::Target));
  m_targetEditor->setFont(Fonts::fixedWidthFont());
  m_targetEditor->setToolTip(R"(The file(s) to delete.

Use wildcards (*,?) in the filename to specify more than one file.
A directory copies all its contents, including subdirectories, into the target.
Variables are allowed.)");
  setupCompleter(m_targetEditor);
  formLayout->addRow("File Path", m_targetEditor);

  connect(
    m_targetEditor,
    &QLineEdit::textChanged,
    this,
    &CompilationDeleteFilesTaskEditor::targetSpecChanged);
}

void CompilationDeleteFilesTaskEditor::updateItem()
{
  CompilationTaskEditorBase::updateItem();

  const auto targetSpec = QString::fromStdString(task().targetSpec);
  if (m_targetEditor->text() != targetSpec)
  {
    m_targetEditor->setText(targetSpec);
  }
}

mdl::CompilationDeleteFiles& CompilationDeleteFilesTaskEditor::task()
{
  // This is safe because we know what type of task the editor was initialized with.
  // We have to do this to avoid using a template as the base class.
  return std::get<mdl::CompilationDeleteFiles>(m_task);
}

void CompilationDeleteFilesTaskEditor::targetSpecChanged(const QString& text)
{
  task().targetSpec = text.toStdString();
}

// CompilationRunToolTaskEditor

CompilationRunToolTaskEditor::CompilationRunToolTaskEditor(
  MapDocument& document,
  mdl::CompilationProfile& profile,
  mdl::CompilationTask& task,
  QWidget* parent)
  : CompilationTaskEditorBase{"Run Tool", document, profile, task, parent}
{
  contract_pre(std::holds_alternative<mdl::CompilationRunTool>(task));

  auto* formLayout = createPanelForm(nullptr, PanelInsets::Content);
  addMainLayout(formLayout);

  m_toolEditor = new MultiCompletionLineEdit{};
  m_toolEditor->setProperty("compilationField", int(CompilationField::Tool));
  m_toolEditor->setFont(Fonts::fixedWidthFont());
  m_toolEditor->setToolTip(
    R"(The absolute path to the executable of the tool that should be run.

The working "directory is set to the profile's working directory if configured.
Variables are allowed.)");
  setupCompleter(m_toolEditor);

  auto* browseToolButton = new QPushButton{"..."};
  browseToolButton->setToolTip("Click to browse");

  auto* toolLayout = new QHBoxLayout{};
  toolLayout->setContentsMargins(0, 0, 0, 0);
  toolLayout->setSpacing(LayoutConstants::NarrowHMargin);
  toolLayout->addWidget(m_toolEditor, 1);
  toolLayout->addWidget(browseToolButton);

  formLayout->addRow("Tool Path", toolLayout);

  m_parametersEditor = new MultiCompletionLineEdit{};
  m_parametersEditor->setProperty("compilationField", int(CompilationField::Parameters));
  m_parametersEditor->setFont(Fonts::fixedWidthFont());
  m_parametersEditor->setToolTip(
    R"(The parameters that should be passed to the tool when it is executed.
Variables are allowed.)");

  setupCompleter(m_parametersEditor);
  formLayout->addRow("Parameters", m_parametersEditor);

  m_treatNonZeroResultCodeAsError = new QCheckBox{"Stop on nonzero error code"};
  m_treatNonZeroResultCodeAsError->setToolTip(
    tr("Stop compilation if the tool returns a nonzero error code"));
  formLayout->addRow("", m_treatNonZeroResultCodeAsError);

  connect(
    m_toolEditor,
    &QLineEdit::textChanged,
    this,
    &CompilationRunToolTaskEditor::toolSpecChanged);
  connect(
    browseToolButton,
    &QPushButton::clicked,
    this,
    &CompilationRunToolTaskEditor::browseTool);
  connect(
    m_parametersEditor,
    &QLineEdit::textChanged,
    this,
    &CompilationRunToolTaskEditor::parameterSpecChanged);
  connect(
    m_treatNonZeroResultCodeAsError,
    &QCheckBox::checkStateChanged,
    this,
    &CompilationRunToolTaskEditor::treatNonZeroResultCodeAsErrorChanged);
}

void CompilationRunToolTaskEditor::updateItem()
{
  CompilationTaskEditorBase::updateItem();

  const auto toolSpec = QString::fromStdString(task().toolSpec);
  if (m_toolEditor->text() != toolSpec)
  {
    m_toolEditor->setText(toolSpec);
  }

  const auto parametersSpec = QString::fromStdString(task().parameterSpec);
  if (m_parametersEditor->text() != parametersSpec)
  {
    m_parametersEditor->setText(parametersSpec);
  }

  if (
    m_treatNonZeroResultCodeAsError->isChecked() != task().treatNonZeroResultCodeAsError)
  {
    m_treatNonZeroResultCodeAsError->setCheckState(
      task().treatNonZeroResultCodeAsError ? Qt::CheckState::Checked
                                           : Qt::CheckState::Unchecked);
  }
}

mdl::CompilationRunTool& CompilationRunToolTaskEditor::task()
{
  // This is safe because we know what type of task the editor was initialized with.
  // We have to do this to avoid using a template as the base class.
  return std::get<mdl::CompilationRunTool>(m_task);
}

void CompilationRunToolTaskEditor::browseTool()
{
  const QString toolSpec = QFileDialog::getOpenFileName(
    this, tr("Select Tool"), fileDialogDefaultDirectory(FileDialogDir::CompileTool));
  if (!toolSpec.isEmpty())
  {
    updateFileDialogDefaultDirectoryWithFilename(FileDialogDir::CompileTool, toolSpec);

    // will call toolSpecChanged and update the model there
    m_toolEditor->setText(toolSpec);
  }
}

void CompilationRunToolTaskEditor::toolSpecChanged(const QString& text)
{
  task().toolSpec = text.toStdString();
}

void CompilationRunToolTaskEditor::parameterSpecChanged(const QString& text)
{
  task().parameterSpec = text.toStdString();
}

void CompilationRunToolTaskEditor::treatNonZeroResultCodeAsErrorChanged(const int state)
{
  const auto value = (state == Qt::Checked);
  task().treatNonZeroResultCodeAsError = value;
}

CompilationHltoolsTaskEditor::CompilationHltoolsTaskEditor(
  MapDocument& document,
  mdl::CompilationProfile& profile,
  mdl::CompilationTask& task,
  QWidget* parent)
  : CompilationTaskEditorBase{
      tr("Built-in hltools (GoldSrc)"), document, profile, task, parent}
{
  contract_pre(std::holds_alternative<mdl::CompilationHltools>(task));
  auto* form = createPanelForm(nullptr, PanelInsets::Content);
  addMainLayout(form);

  m_targetEditor = new MultiCompletionLineEdit{};
  m_targetEditor->setProperty("compilationField", int(CompilationField::Target));
  m_targetEditor->setObjectName("HltoolsOutput");
  m_targetEditor->setFont(Fonts::fixedWidthFont());
  m_targetEditor->setToolTip(
    tr("The BSP output path. Variables are allowed. Logs are saved beside the BSP."));
  setupCompleter(m_targetEditor);
  auto* browse = new QPushButton{tr("...")};
  browse->setToolTip(tr("Choose BSP output"));
  auto* output = new QHBoxLayout{};
  output->setContentsMargins(0, 0, 0, 0);
  output->addWidget(m_targetEditor, 1);
  output->addWidget(browse);
  form->addRow(tr("BSP Output"), output);

  m_qualityEditor = new QComboBox{};
  m_qualityEditor->setObjectName("HltoolsQuality");
  m_qualityEditor->addItem(tr("Fast"), "fast");
  m_qualityEditor->addItem(tr("Normal"), "normal");
  m_qualityEditor->addItem(tr("Extra"), "extra");
  form->addRow(tr("Lighting Quality"), m_qualityEditor);
  m_gpuEditor = new QCheckBox{tr("Use GPU lighting when available")};
  m_gpuEditor->setObjectName("HltoolsGpu");
  m_gpuEditor->setToolTip(
    tr("Uses Vulkan compute and falls back to CPU when unavailable."));
  form->addRow("", m_gpuEditor);

  m_parametersEditor = new MultiCompletionLineEdit{};
  m_parametersEditor->setProperty("compilationField", int(CompilationField::Parameters));
  m_parametersEditor->setObjectName("HltoolsParameters");
  m_parametersEditor->setFont(Fonts::fixedWidthFont());
  m_parametersEditor->setToolTip(
    tr("Additional hltools compile options, for example -bounce 8 or -threads 4. "
       "Variables are allowed."));
  setupCompleter(m_parametersEditor);
  form->addRow(tr("Additional Parameters"), m_parametersEditor);

  auto* options = new CollapsibleTitledPanel{tr("Compiler options"), false};
  options->setObjectName("HltoolsOptionsPanel");
  m_optionsEditor = new HltoolsOptionsEditor{};
  options->createContentLayout()->addWidget(m_optionsEditor);
  form->addRow(options);
  connect(
    m_optionsEditor,
    &HltoolsOptionsEditor::parametersChanged,
    m_parametersEditor,
    &QLineEdit::setText);
  connect(
    m_parametersEditor,
    &QLineEdit::textChanged,
    m_optionsEditor,
    &HltoolsOptionsEditor::setParameters);

  connect(m_targetEditor, &QLineEdit::textChanged, this, [this](const auto& text) {
    std::get<mdl::CompilationHltools>(m_task).targetSpec = text.toStdString();
  });
  connect(browse, &QPushButton::clicked, this, [this] {
    const auto path = QFileDialog::getSaveFileName(
      this, tr("Choose BSP Output"), "", tr("GoldSrc BSP (*.bsp)"));
    if (!path.isEmpty())
      m_targetEditor->setText(path);
  });
  connect(
    m_qualityEditor, &QComboBox::currentIndexChanged, this, [this](const int index) {
      if (index >= 0)
        std::get<mdl::CompilationHltools>(m_task).quality =
          m_qualityEditor->itemData(index).toString().toStdString();
    });
  connect(m_gpuEditor, &QCheckBox::toggled, this, [this](const bool value) {
    std::get<mdl::CompilationHltools>(m_task).gpu = value;
  });
  connect(m_parametersEditor, &QLineEdit::textChanged, this, [this](const auto& text) {
    std::get<mdl::CompilationHltools>(m_task).parameterSpec = text.toStdString();
  });
}

void CompilationHltoolsTaskEditor::updateItem()
{
  CompilationTaskEditorBase::updateItem();
  const auto& task = std::get<mdl::CompilationHltools>(m_task);
  const auto targetBlocker = QSignalBlocker{m_targetEditor};
  const auto qualityBlocker = QSignalBlocker{m_qualityEditor};
  const auto gpuBlocker = QSignalBlocker{m_gpuEditor};
  const auto parametersBlocker = QSignalBlocker{m_parametersEditor};
  m_targetEditor->setText(QString::fromStdString(task.targetSpec));
  m_qualityEditor->setCurrentIndex(
    m_qualityEditor->findData(QString::fromStdString(task.quality)));
  m_gpuEditor->setChecked(task.gpu);
  m_parametersEditor->setText(QString::fromStdString(task.parameterSpec));
  m_optionsEditor->setParameters(QString::fromStdString(task.parameterSpec));
}

// CompilationLaunchEngineTaskEditor

CompilationLaunchEngineTaskEditor::CompilationLaunchEngineTaskEditor(
  MapDocument& document,
  mdl::CompilationProfile& profile,
  mdl::CompilationTask& task,
  QWidget* parent)
  : CompilationTaskEditorBase{"Launch Engine", document, profile, task, parent}
{
  contract_pre(std::holds_alternative<mdl::CompilationLaunchEngine>(task));

  auto* formLayout = createPanelForm(nullptr, PanelInsets::Content);
  addMainLayout(formLayout);

  m_engineProfileEditor = new QComboBox{};
  m_engineProfileEditor->setProperty("compilationField", int(CompilationField::Engine));
  m_engineProfileEditor->setToolTip(
    tr("The Launch Engine profile to start when this task runs"));
  formLayout->addRow("Engine Profile", m_engineProfileEditor);

  m_treatLaunchFailureAsError = new QCheckBox{"Stop on launch failure"};
  m_treatLaunchFailureAsError->setToolTip(
    tr("Stop compilation if the engine cannot be launched"));
  formLayout->addRow("", m_treatLaunchFailureAsError);

  connect(
    m_engineProfileEditor,
    QOverload<int>::of(&QComboBox::currentIndexChanged),
    this,
    &CompilationLaunchEngineTaskEditor::engineProfileChanged);
  connect(
    m_treatLaunchFailureAsError,
    &QCheckBox::checkStateChanged,
    this,
    &CompilationLaunchEngineTaskEditor::treatLaunchFailureAsErrorChanged);
}

void CompilationLaunchEngineTaskEditor::updateItem()
{
  CompilationTaskEditorBase::updateItem();

  const auto signalBlocker = QSignalBlocker{m_engineProfileEditor};
  m_engineProfileEditor->clear();

  const auto& engineProfiles = m_document.map().gameInfo().gameEngineConfig.profiles;
  for (const auto& engineProfile : engineProfiles)
  {
    m_engineProfileEditor->addItem(
      QString::fromStdString(engineProfile.name),
      QString::fromStdString(engineProfile.id));
  }

  m_engineProfileEditor->setCurrentIndex(
    m_engineProfileEditor->findData(QString::fromStdString(task().engineProfileId)));

  if (m_treatLaunchFailureAsError->isChecked() != task().treatLaunchFailureAsError)
  {
    m_treatLaunchFailureAsError->setCheckState(
      task().treatLaunchFailureAsError ? Qt::CheckState::Checked
                                       : Qt::CheckState::Unchecked);
  }
}

mdl::CompilationLaunchEngine& CompilationLaunchEngineTaskEditor::task()
{
  // This is safe because we know what type of task the editor was initialized with.
  // We have to do this to avoid using a template as the base class.
  return std::get<mdl::CompilationLaunchEngine>(m_task);
}

void CompilationLaunchEngineTaskEditor::engineProfileChanged(const int index)
{
  if (index >= 0)
  {
    task().engineProfileId =
      m_engineProfileEditor->itemData(index).toString().toStdString();
  }
}

void CompilationLaunchEngineTaskEditor::treatLaunchFailureAsErrorChanged(const int state)
{
  const auto value = (state == Qt::Checked);
  task().treatLaunchFailureAsError = value;
}

// CompilationTaskListBox

CompilationTaskListBox::CompilationTaskListBox(MapDocument& document, QWidget* parent)
  : ControlListBox{"Click the '+' button to create a task.", QMargins{}, false, parent}
  , m_document{document}
{
}

void CompilationTaskListBox::setProfile(mdl::CompilationProfile* profile)
{
  m_profile = profile;
  reload();
}

void CompilationTaskListBox::reloadTasks()
{
  reload();
}

void CompilationTaskListBox::updateTasks()
{
  // Refresh existing task editors without rebuilding the list, so scroll position and
  // selection survive external changes such as editing Launch Engine profiles.
  updateItems();
}

size_t CompilationTaskListBox::itemCount() const
{
  return m_profile ? m_profile->tasks.size() : 0;
}

ControlListBoxItemRenderer* CompilationTaskListBox::createItemRenderer(
  QWidget* parent, const size_t index)
{
  contract_pre(m_profile != nullptr);

  auto& task = m_profile->tasks[index];
  auto* renderer = std::visit(
    kdl::overload(
      [&](const mdl::CompilationExportMap&) -> ControlListBoxItemRenderer* {
        return new CompilationExportMapTaskEditor{m_document, *m_profile, task, parent};
      },
      [&](const mdl::CompilationCopyFiles&) -> ControlListBoxItemRenderer* {
        return new CompilationCopyFilesTaskEditor{m_document, *m_profile, task, parent};
      },
      [&](const mdl::CompilationRenameFile&) -> ControlListBoxItemRenderer* {
        return new CompilationRenameFileTaskEditor{m_document, *m_profile, task, parent};
      },
      [&](const mdl::CompilationDeleteFiles&) -> ControlListBoxItemRenderer* {
        return new CompilationDeleteFilesTaskEditor(m_document, *m_profile, task, parent);
      },
      [&](const mdl::CompilationRunTool&) -> ControlListBoxItemRenderer* {
        return new CompilationRunToolTaskEditor{m_document, *m_profile, task, parent};
      },
      [&](const mdl::CompilationHltools&) -> ControlListBoxItemRenderer* {
        return new CompilationHltoolsTaskEditor{m_document, *m_profile, task, parent};
      },
      [&](const mdl::CompilationLaunchEngine&) -> ControlListBoxItemRenderer* {
        return new CompilationLaunchEngineTaskEditor{
          m_document, *m_profile, task, parent};
      }),
    task);

  connect(
    renderer,
    &QWidget::customContextMenuRequested,
    this,
    [&, renderer, index](const QPoint& pos) {
      emit this->taskContextMenuRequested(
        renderer->mapToGlobal(pos), m_profile->tasks[index]);
    });

  // Connect after the editor's model updates. Queue notification so refreshing the
  // validation cannot observe half-edited controls during renderer construction.
  for (auto* input : renderer->findChildren<QLineEdit*>())
    connect(
      input,
      &QLineEdit::textChanged,
      this,
      &CompilationTaskListBox::taskChanged,
      Qt::QueuedConnection);
  for (auto* input : renderer->findChildren<QCheckBox*>())
    connect(
      input,
      &QCheckBox::clicked,
      this,
      &CompilationTaskListBox::taskChanged,
      Qt::QueuedConnection);
  for (auto* input : renderer->findChildren<QComboBox*>())
    connect(
      input,
      &QComboBox::currentIndexChanged,
      this,
      &CompilationTaskListBox::taskChanged,
      Qt::QueuedConnection);
  return renderer;
}

void CompilationTaskListBox::focusField(const size_t index, const CompilationField field)
{
  if (index >= size_t(count()))
    return;
  setCurrentRow(static_cast<int>(index));
  auto* item = renderer(static_cast<int>(index));
  for (auto* input : item->findChildren<QWidget*>())
    if (
      input->property("compilationField").isValid()
      && input->property("compilationField").toInt() == int(field))
    {
      input->setFocus(Qt::OtherFocusReason);
      if (auto* line = qobject_cast<QLineEdit*>(input))
        line->selectAll();
      return;
    }
  item->setFocus(Qt::OtherFocusReason);
}

void CompilationTaskListBox::setDiagnostic(
  const std::optional<CompilationDiagnostic>& diagnostic)
{
  for (int i = 0; i < count(); ++i)
    if (auto* editor = qobject_cast<CompilationTaskEditorBase*>(renderer(i)))
      editor->setDiagnostic(
        diagnostic && diagnostic->taskIndex == size_t(i) ? diagnostic : std::nullopt);
}

} // namespace tb::ui
