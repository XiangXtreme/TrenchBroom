/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/ProjectAssetPanel.h"

#include <QHeaderView>
#include <QLabel>
#include <QSplitter>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "gl/GlManager.h"
#include "mdl/GameConfig.h"
#include "mdl/GameInfo.h"
#include "mdl/GameManager.h"
#include "mdl/MapFormat.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/ModelBrowser.h"
#include "ui/ModelBrowserView.h"
#include "ui/ProjectSession.h"
#include "ui/QPathUtils.h"

#include <algorithm>

namespace tb::ui
{
ProjectAssetPanel::ProjectAssetPanel(
  AppController& app, std::shared_ptr<ProjectSession> session, QWidget* parent)
  : QWidget{parent}
  , m_session{std::move(session)}
{
  setObjectName("ProjectAssetPanel");
  auto* layout = new QVBoxLayout{this};
  layout->setContentsMargins(0, 0, 0, 0);
  const auto* game = app.gameManager().gameInfo(m_session->store().info().gameId);
  if (!game || game->gameConfig.fileFormats.empty())
  {
    layout->addWidget(
      new QLabel{tr("The project game configuration is unavailable."), this});
    return;
  }
  auto document = MapDocument::createDocument(
    app.environmentConfig(),
    *game,
    mdl::formatFromName(game->gameConfig.fileFormats.front().format),
    MapDocument::DefaultWorldBounds,
    app.taskManager(),
    app.glManager().resourceManager());
  if (document.is_error())
  {
    auto* error = new QLabel{
      tr("Cannot initialize the asset browser: %1")
        .arg(QString::fromStdString(std::get<Error>(document.error()).msg)),
      this};
    error->setWordWrap(true);
    layout->addWidget(error);
    return;
  }
  m_fallback = std::move(document).value();
  auto* splitter = new QSplitter{Qt::Horizontal, this};
  m_browser = new ModelBrowser{app, *m_fallback, splitter, m_session};
  m_details = new QTreeWidget{};
  m_details->setObjectName("ProjectAssetDetails");
  m_details->setHeaderLabels({tr("Property"), tr("Value")});
  m_details->setAlternatingRowColors(true);
  m_details->setRootIsDecorated(false);
  m_details->setMinimumWidth(220);
  m_details->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  m_details->header()->setSectionResizeMode(1, QHeaderView::Stretch);
  auto* detailsSurface = new QWidget{splitter};
  detailsSurface->setObjectName("ProjectAssetDetailsSurface");
  detailsSurface->setAttribute(Qt::WA_StyledBackground);
  auto* detailsLayout = new QVBoxLayout{detailsSurface};
  detailsLayout->setContentsMargins(0, 0, 0, 0);
  detailsLayout->setSpacing(0);
  auto* detailsTitle = new QLabel{tr("Asset Details")};
  detailsTitle->setProperty("regionAnchor", true);
  detailsLayout->addWidget(detailsTitle);
  detailsLayout->addWidget(m_details, 1);
  splitter->addWidget(detailsSurface);
  splitter->setChildrenCollapsible(false);
  splitter->setHandleWidth(1);
  splitter->setStretchFactor(0, 3);
  splitter->setStretchFactor(1, 1);
  splitter->setSizes({950, 240});
  auto* detailsToggle = new QToolButton{};
  detailsToggle->setObjectName("ProjectAssetDetailsToggle");
  detailsToggle->setProperty("tbControlRole", "compact");
  detailsToggle->setText(tr("Details"));
  detailsToggle->setToolTip(tr("Show asset details"));
  detailsToggle->setCheckable(true);
  connect(detailsToggle, &QToolButton::toggled, detailsSurface, &QWidget::setVisible);
  m_browser->addViewOption(detailsToggle);
  detailsSurface->hide();
  layout->addWidget(splitter);
  auto* view = m_browser->findChild<ModelBrowserView*>();
  connect(view, &ModelBrowserView::assetSelected, this, &ProjectAssetPanel::showAsset);
  connect(view, &ModelBrowserView::selectionCleared, this, [this]() {
    m_selected.reset();
    refreshDetails();
  });
  connect(
    m_session.get(),
    &ProjectSession::contentsChanged,
    this,
    &ProjectAssetPanel::refreshDetails);
  connect(
    m_session.get(),
    &ProjectSession::libraryLinksChanged,
    this,
    &ProjectAssetPanel::refreshDetails);
  refreshDetails();
}

ProjectAssetPanel::~ProjectAssetPanel()
{
  // QWidget children otherwise outlive the private preview document.
  delete m_browser;
}

void ProjectAssetPanel::setDocument(MapDocument* document)
{
  if (m_browser)
    m_browser->setDocument(document ? *document : *m_fallback);
}

void ProjectAssetPanel::showProjectAsset(const QString& id)
{
  if (id.isEmpty())
    return; // Library and game selections have their own metadata.
  auto asset = BrowserAsset{};
  asset.source = BrowserSource::Project;
  asset.projectId = m_session->store().info().id;
  asset.projectAssetId = id.toStdString();
  showAsset(asset);
}

void ProjectAssetPanel::showAsset(const BrowserAsset& asset)
{
  m_selected = asset;
  refreshDetails();
}

void ProjectAssetPanel::refreshDetails()
{
  if (!m_details)
    return;
  m_details->clear();
  const auto add = [this](const QString& key, const QString& value) {
    auto* item = new QTreeWidgetItem{m_details, {key, value}};
    item->setToolTip(1, value);
  };
  if (!m_selected)
  {
    add(tr("Selection"), tr("Select an asset to inspect its details."));
    return;
  }
  const auto text = [](const std::string& value) {
    return QString::fromStdString(value);
  };
  auto selected = *m_selected;
  if (
    selected.source == BrowserSource::Project
    && selected.projectId != m_session->store().info().id)
  {
    add(tr("Selection"), tr("The selected asset belongs to another project."));
    return;
  }
  auto libraryMetadataAvailable = false;
  auto libraryAvailability = QString{};
  auto snapshotLookupFailed = false;
  if (selected.source == BrowserSource::AssetLibrary)
  {
    selected.projectAssetId.clear();
    const auto captured =
      m_session->projectAssetForLibraryAsset(selected.libraryId, selected.libraryAssetId);
    if (captured.is_success())
      selected.projectAssetId = captured.value() ? captured.value()->id : std::string{};
    else
    {
      snapshotLookupFailed = true;
      add(tr("Error"), text(std::get<Error>(captured.error()).msg));
    }
    const auto state = m_session->libraryState(selected.libraryId, false);
    if (state.is_success() && state.value().available)
    {
      const auto found = std::ranges::find(
        state.value().assets, selected.libraryAssetId, &project::LibraryAsset::id);
      if (found != state.value().assets.end())
      {
        libraryMetadataAvailable = true;
        selected.displayName = found->displayName;
        selected.catalogPath = pathFromQString(text(found->virtualPath));
        selected.libraryRevision = found->revision;
        selected.libraryContentHash = found->contentHash;
        selected.missing = found->missing;
        add(tr("Source Container"), text(found->sourceContainer));
        add(tr("Source Member"), text(found->sourceMember));
      }
      else
      {
        selected.missing = true;
        libraryAvailability = tr("Asset no longer exists in library");
      }
    }
    else
    {
      selected.missing = true;
      libraryAvailability = tr("Library unavailable");
    }
  }
  add(
    tr("Source"),
    selected.source == BrowserSource::Project        ? tr("Project")
    : selected.source == BrowserSource::AssetLibrary ? tr("Asset Library")
                                                     : tr("Game Files"));
  if (selected.source != BrowserSource::Project)
  {
    add(tr("Name"), text(selected.displayName));
    add(
      tr("Catalog Path"),
      pathAsQString(selected.catalogPath.empty() ? selected.path : selected.catalogPath));
    if (selected.source == BrowserSource::GameFiles)
      add(tr("File"), pathAsQString(selected.absolutePath));
    else
    {
      add(tr("Library ID"), text(selected.libraryId));
      add(tr("Library Asset ID"), text(selected.libraryAssetId));
      if (libraryMetadataAvailable)
      {
        add(tr("Library Revision"), QString::number(selected.libraryRevision));
        add(tr("Library Content Hash"), text(selected.libraryContentHash));
      }
      add(
        tr("Availability"),
        !libraryAvailability.isEmpty() ? libraryAvailability
        : selected.missing             ? tr("Source missing")
                                       : tr("Available"));
    }
  }
  if (selected.projectAssetId.empty())
  {
    if (selected.source == BrowserSource::AssetLibrary)
      add(
        tr("Project Snapshot"),
        snapshotLookupFailed ? tr("Cannot determine captured snapshot")
                             : tr("Not captured"));
    return;
  }
  const auto result = m_session->store().findAsset(selected.projectAssetId);
  if (result.is_error())
  {
    add(tr("Error"), text(std::get<Error>(result.error()).msg));
    return;
  }
  if (!result.value())
  {
    add(tr("Project Snapshot"), tr("Asset no longer exists"));
    return;
  }
  const auto& asset = *result.value();
  add(tr("Name"), text(asset.displayName));
  add(tr("Asset ID"), text(asset.id));
  add(tr("Virtual Path"), text(asset.virtualPath));
  add(tr("Published Path"), text(asset.publishPath));
  add(tr("Revision"), QString::number(asset.revision));
  add(tr("Original Source"), text(asset.sourceName));
  add(tr("Content Hash"), text(asset.contentHash));
  if (!asset.library.empty())
  {
    add(tr("Captured Library"), text(asset.library.libraryId));
    add(tr("Captured Revision"), QString::number(asset.library.libraryRevision));
    add(tr("Source Container"), text(asset.library.sourceContainer));
    add(tr("Source Member"), text(asset.library.sourceMember));
  }
  const auto assets = m_session->store().assets();
  const auto dependencies = m_session->store().dependencies(asset.id);
  const auto nameFor = [&](const std::string& id) {
    if (assets.is_success())
      for (const auto& candidate : assets.value())
        if (candidate.id == id)
          return text(candidate.virtualPath);
    return text(id);
  };
  if (dependencies.is_error())
    add(tr("Dependencies"), text(std::get<Error>(dependencies.error()).msg));
  else if (dependencies.value().empty())
    add(tr("Dependencies"), tr("No recorded dependencies"));
  else
    for (const auto& edge : dependencies.value())
      add(tr("Depends On"), nameFor(edge.dependencyId));
  // Recorded graph edges are distinct from MAP references; Find Uses performs a live
  // scan.
  const auto uses = m_session->store().dependents(asset.id);
  if (uses.is_error())
    add(tr("Used By"), text(std::get<Error>(uses.error()).msg));
  else if (uses.value().empty())
    add(
      tr("Used By"),
      tr("No recorded uses; use Asset > Find Uses for live map references."));
  else
    for (const auto& edge : uses.value())
      add(tr("Used By"), nameFor(edge.assetId));
}
} // namespace tb::ui
