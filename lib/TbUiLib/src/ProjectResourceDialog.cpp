/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/ProjectResourceDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "ui/ProjectSession.h"

#include <algorithm>
#include <map>

namespace tb::ui
{
ProjectResourceDialog::ProjectResourceDialog(
  const ProjectSession& session,
  std::optional<std::string> assetId,
  std::function<void(const std::string&)> openMap,
  QWidget* parent)
  : QDialog{parent}
  , m_session{session}
  , m_assetId{std::move(assetId)}
  , m_openMap{std::move(openMap)}
{
  setObjectName("ProjectResourceDialog");
  setWindowTitle(m_assetId ? tr("Find Asset Uses") : tr("Check Project Resources"));
  resize(1120, 620);
  auto* layout = new QVBoxLayout{this};
  auto* scope = new QLabel{
    tr("Checks explicit GoldSrc MAP references, including unsaved edits. Embedded means "
       "present in the project. Game defaults, scripts, model companion files and "
       "animated "
       "texture sequences need additional checks. Lines refer to the scanned MAP "
       "snapshot."),
    this};
  scope->setWordWrap(true);
  layout->addWidget(scope);
  if (m_assetId)
  {
    const auto asset = m_session.store().findAsset(*m_assetId);
    if (asset.is_success() && asset.value())
    {
      auto* selected = new QLabel{
        tr("Asset: %1  •  Publish path: %2")
          .arg(
            QString::fromStdString(asset.value()->virtualPath),
            QString::fromStdString(asset.value()->publishPath)),
        this};
      selected->setTextFormat(Qt::PlainText);
      selected->setWordWrap(true);
      layout->addWidget(selected);
    }
  }
  m_summary = new QLabel{this};
  m_summary->setObjectName("ProjectResourceSummary");
  layout->addWidget(m_summary);
  m_search = new QLineEdit{this};
  m_search->setPlaceholderText(tr("Filter references, maps and locations"));
  layout->addWidget(m_search);
  m_issuesOnly = new QCheckBox{tr("Show only issues"), this};
  m_issuesOnly->setChecked(!m_assetId);
  layout->addWidget(m_issuesOnly);
  m_uses = new QTreeWidget{this};
  m_uses->setObjectName("ProjectResourceUses");
  m_uses->setRootIsDecorated(false);
  m_uses->setHeaderLabels(
    {tr("Status"),
     tr("Map"),
     tr("Line"),
     tr("Location"),
     tr("Reference"),
     tr("Embedded Asset"),
     tr("Details")});
  m_uses->setSortingEnabled(true);
  m_uses->header()->setSectionResizeMode(4, QHeaderView::Stretch);
  layout->addWidget(m_uses, 1);
  auto* buttons = new QDialogButtonBox{QDialogButtonBox::Close, this};
  auto* rescanButton =
    buttons->addButton(tr("Check Again"), QDialogButtonBox::ActionRole);
  auto* open = buttons->addButton(tr("Open Map"), QDialogButtonBox::ActionRole);
  open->setEnabled(false);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(rescanButton, &QPushButton::clicked, this, &ProjectResourceDialog::rescan);
  connect(open, &QPushButton::clicked, this, &ProjectResourceDialog::openMap);
  connect(m_uses, &QTreeWidget::itemDoubleClicked, this, [this]() { this->openMap(); });
  connect(m_uses, &QTreeWidget::itemSelectionChanged, this, [this, open]() {
    const auto* item = m_uses->currentItem();
    open->setEnabled(item && !item->data(0, Qt::UserRole).toString().isEmpty());
  });
  connect(m_search, &QLineEdit::textChanged, this, &ProjectResourceDialog::refresh);
  connect(m_issuesOnly, &QCheckBox::toggled, this, &ProjectResourceDialog::refresh);
  rescan();
}

void ProjectResourceDialog::rescan()
{
  QApplication::setOverrideCursor(Qt::WaitCursor);
  const auto result = auditGoldSrcProjectResources(m_session);
  QApplication::restoreOverrideCursor();
  if (result.is_error())
  {
    m_audit = {};
    m_uses->clear();
    m_summary->setText(
      tr("Check failed: %1")
        .arg(QString::fromStdString(std::get<Error>(result.error()).msg)));
    return;
  }
  m_audit = result.value();
  refresh();
}

void ProjectResourceDialog::refresh()
{
  auto paths = std::map<std::string, QString>{};
  const auto assets = m_session.store().assets();
  if (assets.is_success())
    for (const auto& asset : assets.value())
      paths.emplace(asset.id, QString::fromStdString(asset.virtualPath));
  m_uses->clear();
  auto errors = size_t{0};
  auto matches = size_t{0};
  auto issues = size_t{0};
  for (const auto& use : m_audit.uses)
  {
    // Never hide failed scans in Find Uses: an empty list cannot establish non-use.
    const auto failed = use.status == ProjectResourceStatus::ScanError;
    if (failed)
      ++errors;
    if (
      m_assetId && !failed
      && std::ranges::find(use.assetIds, *m_assetId) == use.assetIds.end())
      continue;
    if (!failed)
      ++matches;
    if (use.status != ProjectResourceStatus::Embedded)
      ++issues;
    if (m_issuesOnly->isChecked() && use.status == ProjectResourceStatus::Embedded)
      continue;
    auto status = QString{};
    switch (use.status)
    {
    case ProjectResourceStatus::Embedded:
      status = tr("Embedded");
      break;
    case ProjectResourceStatus::Missing:
      status = tr("Not embedded");
      break;
    case ProjectResourceStatus::Ambiguous:
      status = tr("Ambiguous");
      break;
    case ProjectResourceStatus::InvalidPath:
      status = tr("Invalid path");
      break;
    case ProjectResourceStatus::Unsupported:
      status = tr("Needs lookup");
      break;
    case ProjectResourceStatus::ScanError:
      status = tr("Scan failed");
      break;
    }
    auto embedded = QStringList{};
    for (const auto& id : use.assetIds)
      embedded.push_back(paths.contains(id) ? paths.at(id) : QString::fromStdString(id));
    const auto columns = QStringList{
      status,
      QString::fromStdString(use.mapPath),
      use.line ? QString::number(qulonglong(use.line)) : QString{},
      QString::fromStdString(use.location),
      QString::fromStdString(use.reference),
      embedded.join("; "),
      QString::fromStdString(use.detail)};
    if (!columns.join(' ').contains(m_search->text(), Qt::CaseInsensitive))
      continue;
    auto* item = new QTreeWidgetItem{m_uses, columns};
    item->setData(0, Qt::UserRole, QString::fromStdString(use.mapId));
    for (auto column = 0; column < columns.size(); ++column)
      item->setToolTip(column, columns[column]);
  }
  m_summary->setText(tr("%1 maps scanned • %2 references • %3 issues • %4 failed scans")
                       .arg(qulonglong(m_audit.mapsScanned))
                       .arg(qulonglong(matches))
                       .arg(qulonglong(issues))
                       .arg(qulonglong(errors)));
}

void ProjectResourceDialog::openMap()
{
  if (const auto* item = m_uses->currentItem())
  {
    const auto id = item->data(0, Qt::UserRole).toString().toStdString();
    if (!id.empty())
    {
      accept();
      m_openMap(id);
    }
  }
}
} // namespace tb::ui
