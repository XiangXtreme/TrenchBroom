/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/ProjectSettingsDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "ui/PanelLayout.h"
#include "ui/ProjectSession.h"

namespace tb::ui
{
ProjectSettingsDialog::ProjectSettingsDialog(
  std::shared_ptr<ProjectSession> session, QWidget* parent)
  : QDialog{parent}
  , m_session{std::move(session)}
{
  setObjectName("ProjectSettingsDialog");
  setWindowTitle(tr("Project Settings"));
  resize(760, 600);
  auto* layout = createPanelColumn(this);
  auto* form = createPanelForm();
  m_namespace = new QLineEdit{this};
  m_namespace->setObjectName("ProjectResourceNamespace");
  m_namespace->setMaxLength(32);
  form->addRow(tr("Resource namespace"), m_namespace);
  auto* directoryRow = createPanelActions();
  m_directory = new QLineEdit{this};
  m_directory->setObjectName("ProjectGameDirectory");
  auto* browse = new QPushButton{tr("Browse…"), this};
  directoryRow->addWidget(m_directory, 1);
  directoryRow->addWidget(browse);
  form->addRow(tr("Game / mod directory"), directoryRow);
  layout->addLayout(form);
  auto* help = new QLabel{
    tr("New models, sprites and sounds use the resource namespace. Leave the game "
       "directory empty to disable automatic publishing. Select the mod folder (for "
       "example cstrike), where models, sprites and sound are located. Existing files "
       "with different content stop publishing before any file is copied."),
    this};
  help->setWordWrap(true);
  layout->addWidget(help);
  m_examples = new QLabel{this};
  m_examples->setTextFormat(Qt::PlainText);
  m_examples->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_examples->setWordWrap(true);
  layout->addWidget(m_examples);
  m_migrate =
    new QCheckBox{tr("Migrate existing resource paths and map references"), this};
  m_migrate->setObjectName("ProjectMigrateResources");
  layout->addWidget(m_migrate);
  auto* migrationHelp = new QLabel{
    tr("Migration creates a safety version including unsaved map edits. Open maps reload "
       "and their undo history is cleared. Save the project to keep these settings and "
       "changes."),
    this};
  migrationHelp->setWordWrap(true);
  layout->addWidget(migrationHelp);
  m_preview = new QTreeWidget{this};
  m_preview->setObjectName("ProjectMigrationPreview");
  m_preview->setHeaderLabels({tr("Current game path"), tr("New game path")});
  m_preview->setRootIsDecorated(false);
  m_preview->header()->setSectionResizeMode(QHeaderView::Stretch);
  layout->addWidget(m_preview, 1);
  m_status = new QLabel{this};
  m_status->setWordWrap(true);
  layout->addWidget(m_status);
  auto* buttons =
    new QDialogButtonBox{QDialogButtonBox::Save | QDialogButtonBox::Cancel, this};
  m_save = buttons->button(QDialogButtonBox::Save);
  layout->addWidget(buttons);
  const auto current = m_session->store().publishSettings();
  if (current.is_success())
  {
    m_namespace->setText(QString::fromStdString(current.value().assetNamespace));
    m_directory->setText(QString::fromStdString(current.value().gameDirectory));
  }
  connect(m_namespace, &QLineEdit::textChanged, this, &ProjectSettingsDialog::refresh);
  connect(m_directory, &QLineEdit::textChanged, this, &ProjectSettingsDialog::refresh);
  connect(m_migrate, &QCheckBox::toggled, this, &ProjectSettingsDialog::refresh);
  connect(browse, &QPushButton::clicked, this, [this] {
    const auto path = QFileDialog::getExistingDirectory(
      this, tr("Game / mod directory"), m_directory->text());
    if (!path.isEmpty())
      m_directory->setText(path);
  });
  connect(buttons, &QDialogButtonBox::accepted, this, &ProjectSettingsDialog::save);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  refresh();
}

project::PublishSettings ProjectSettingsDialog::settings() const
{
  return {
    m_namespace->text().trimmed().toStdString(),
    m_directory->text().trimmed().toStdString()};
}

void ProjectSettingsDialog::refresh()
{
  m_preview->clear();
  const auto config = settings();
  const auto prefix = config.assetNamespace.empty()
                        ? QString{}
                        : QString::fromStdString(config.assetNamespace) + "/";
  m_examples->setText(
    tr("Examples: models/%1crate.mdl · sprites/%1glow.spr · sound/%1wind.wav\nSkyboxes: "
       "gfx/env/<skyname>bk.tga (all six faces, without a namespace)")
      .arg(prefix));
  m_preview->setEnabled(m_migrate->isChecked());
  const auto valid = project::ProjectStore::validatePublishSettings(config);
  m_save->setEnabled(valid.is_success());
  if (valid.is_error())
  {
    m_status->setText(QString::fromStdString(std::get<Error>(valid.error()).msg));
    return;
  }
  if (!m_migrate->isChecked())
  {
    m_status->setText(tr(
      "Existing resource paths stay unchanged. The namespace applies to new resources."));
    return;
  }
  const auto changes = m_session->publishMigration(config.assetNamespace);
  m_save->setEnabled(changes.is_success());
  if (changes.is_error())
  {
    m_status->setText(QString::fromStdString(std::get<Error>(changes.error()).msg));
    return;
  }
  for (const auto& [from, to] : changes.value())
    new QTreeWidgetItem{
      m_preview, {QString::fromStdString(from), QString::fromStdString(to)}};
  m_status->setText(
    tr("%1 resource paths will change.").arg(qulonglong(changes.value().size())));
}

void ProjectSettingsDialog::save()
{
  const auto result = m_session->setPublishSettings(settings(), m_migrate->isChecked());
  if (result.is_error())
  {
    QMessageBox::warning(
      this,
      tr("Project Settings"),
      QString::fromStdString(std::get<Error>(result.error()).msg));
    return;
  }
  accept();
}
} // namespace tb::ui
