/*
  Copyright (C) 2026 XiangXtreme
  This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/ProjectLibraryDialog.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "ui/ProjectResourceAudit.h"
#include "ui/ProjectSession.h"

#include <algorithm>
#include <map>

namespace tb::ui
{
namespace
{
QString stateText(const project::LibraryUpdateState state)
{
  switch (state)
  {
  case project::LibraryUpdateState::UpToDate:
    return QObject::tr("Up to date");
  case project::LibraryUpdateState::NewVersion:
    return QObject::tr("New library version");
  case project::LibraryUpdateState::Missing:
    return QObject::tr("Deleted or inaccessible in library");
  case project::LibraryUpdateState::LibraryUnavailable:
    return QObject::tr("Asset library unavailable");
  case project::LibraryUpdateState::DependenciesChanged:
    return QObject::tr("Dependencies changed in library");
  }
  return {};
}

QString typeText(const project::AssetType type)
{
  return QString::fromLatin1(project::assetTypeName(type));
}
} // namespace

ProjectLibraryDialog::ProjectLibraryDialog(
  ProjectSession& session, const bool rescan, QWidget* parent)
  : QDialog{parent}
  , m_session{session}
  , m_pendingRescan{rescan}
{
  setWindowTitle(tr("Project Asset Library"));
  resize(860, 460);

  auto* layout = new QVBoxLayout{this};
  m_summary = new QLabel{this};
  m_summary->setWordWrap(true);
  layout->addWidget(m_summary);

  m_updates = new QTreeWidget{this};
  m_updates->setObjectName("ProjectLibraryUpdates");
  m_updates->setRootIsDecorated(false);
  m_updates->setHeaderLabels(
    {tr("Asset"), tr("Type"), tr("Project"), tr("Library"), tr("Status"), tr("Used in")});
  m_updates->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_updates->setSelectionMode(QAbstractItemView::ExtendedSelection);
  layout->addWidget(m_updates, 1);

  auto* buttons = new QDialogButtonBox{this};
  auto* rescanButton =
    buttons->addButton(tr("Rescan Library"), QDialogButtonBox::ActionRole);
  m_updateButton =
    buttons->addButton(tr("Update Selected"), QDialogButtonBox::ActionRole);
  m_ignoreButton =
    buttons->addButton(tr("Ignore This Version"), QDialogButtonBox::ActionRole);
  auto* laterButton =
    buttons->addButton(tr("Remind Me Later"), QDialogButtonBox::RejectRole);
  buttons->addButton(QDialogButtonBox::Close);
  layout->addWidget(buttons);

  connect(rescanButton, &QPushButton::clicked, this, [this]() {
    m_pendingRescan = true;
    reload();
  });
  connect(m_updateButton, &QPushButton::clicked, this, [this]() { updateSelected(); });
  connect(m_ignoreButton, &QPushButton::clicked, this, [this]() { ignoreSelected(); });
  connect(laterButton, &QPushButton::clicked, this, [this]() {
    m_session.deferLibraryUpdates();
    reject();
  });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(m_updates, &QTreeWidget::itemChanged, this, [this]() { refreshButtons(); });

  reload();
}

void ProjectLibraryDialog::reload()
{
  const auto reportError = [this](const std::string& message) {
    QMessageBox::critical(this, tr("Asset Library"), QString::fromStdString(message));
  };

  // Map usage is only available for GoldSrc projects, and it is a preview, so a failure
  // here must not block reviewing or updating resources.
  auto usage = std::map<std::string, std::vector<std::string>>{};
  if (const auto audit = auditGoldSrcProjectResources(m_session); audit.is_success())
  {
    for (const auto& use : audit.value().uses)
    {
      for (const auto& id : use.assetIds)
      {
        usage[id].push_back(QString{"%1 — %2 (%3)"}
                              .arg(
                                QString::fromStdString(use.mapPath),
                                QString::fromStdString(use.location),
                                QString::fromStdString(use.reference))
                              .toStdString());
      }
    }
  }

  const auto updates = m_session.checkLibraryUpdates(m_pendingRescan);
  m_pendingRescan = false;
  if (updates.is_error())
  {
    reportError(std::get<Error>(updates.error()).msg);
    return;
  }

  m_updates->clear();
  auto attention = 0;
  auto outdated = 0;
  for (const auto& update : updates.value())
  {
    const auto projectRevision = QString::number(qulonglong(update.projectRevision));
    const auto libraryRevision =
      update.available ? QString::number(qulonglong(update.available->revision))
                       : QString{"-"};
    auto usedIn = QString{"-"};
    if (const auto it = usage.find(update.projectAssetId); it != usage.end())
      usedIn = QString::number(it->second.size());

    auto* item = new QTreeWidgetItem{
      m_updates,
      {QString::fromStdString(update.displayName),
       typeText(update.type),
       projectRevision,
       libraryRevision,
       stateText(update.state) + (update.ignored ? tr(" (ignored)") : QString{}),
       usedIn}};
    item->setData(0, Qt::UserRole, QString::fromStdString(update.projectAssetId));
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    const auto updateable =
      (update.state == project::LibraryUpdateState::NewVersion
       || update.state == project::LibraryUpdateState::DependenciesChanged)
      && !update.ignored && update.available.has_value();
    // Ignored revisions stay visible but unchecked, so "ignore" is a decision the user
    // can revisit, not a silent removal.
    item->setCheckState(0, updateable ? Qt::Checked : Qt::Unchecked);
    if (const auto it = usage.find(update.projectAssetId); it != usage.end())
    {
      auto lines = QStringList{};
      for (const auto& line : it->second)
        lines << QString::fromStdString(line);
      item->setToolTip(5, lines.join('\n'));
    }
    if (update.needsAttention())
      ++attention;
    if (
      update.state == project::LibraryUpdateState::NewVersion
      || update.state == project::LibraryUpdateState::DependenciesChanged)
      ++outdated;
  }

  m_summary->setText(
    attention == 0
      ? tr("Every project resource matches its asset library.")
      : tr("%1 project resource(s) need attention; %2 have a newer library version. "
           "Projects keep their own snapshots until you update them.")
          .arg(attention)
          .arg(outdated));
  refreshButtons();
}

std::vector<std::string> ProjectLibraryDialog::checkedAssets() const
{
  auto result = std::vector<std::string>{};
  for (auto row = 0; row < m_updates->topLevelItemCount(); ++row)
  {
    auto* item = m_updates->topLevelItem(row);
    if (item->checkState(0) == Qt::Checked)
      result.push_back(item->data(0, Qt::UserRole).toString().toStdString());
  }
  return result;
}

void ProjectLibraryDialog::refreshButtons()
{
  const auto selection = m_updates->selectedItems();
  const auto checked = checkedAssets();
  if (m_updateButton)
    m_updateButton->setEnabled(!checked.empty());
  if (m_ignoreButton)
    m_ignoreButton->setEnabled(!selection.isEmpty());
}

void ProjectLibraryDialog::updateSelected()
{
  const auto ids = checkedAssets();
  if (ids.empty())
    return;
  const auto updated = m_session.updateLibraryAssets(ids);
  if (updated.is_error())
  {
    // A failed batch leaves every resource on its previous revision, so reporting the
    // error is enough; nothing has to be undone.
    QMessageBox::critical(
      this,
      tr("Update Project Resources"),
      QString::fromStdString(std::get<Error>(updated.error()).msg));
    return;
  }
  m_summary->setText(tr("Updated %1 project resource(s).").arg(updated.value().size()));
  reload();
}

void ProjectLibraryDialog::ignoreSelected()
{
  auto ignored = 0;
  const auto updates = m_session.checkLibraryUpdates(false);
  if (updates.is_error())
    return;
  for (auto* item : m_updates->selectedItems())
  {
    const auto id = item->data(0, Qt::UserRole).toString().toStdString();
    const auto update = std::ranges::find_if(updates.value(), [&](const auto& candidate) {
      return candidate.projectAssetId == id;
    });
    if (update == updates.value().end() || !update->available)
      continue;
    if (m_session.ignoreLibraryUpdate(id, update->available->contentHash).is_success())
      ++ignored;
  }
  if (ignored > 0)
    reload();
}
} // namespace tb::ui
