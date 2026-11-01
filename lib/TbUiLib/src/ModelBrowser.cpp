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

#include "ui/ModelBrowser.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QImage>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStringList>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "base/PreferenceManager.h"
#include "fs/DiskIO.h"
#include "fs/FileSystem.h"
#include "fs/PathInfo.h"
#include "fs/PathMatcher.h"
#include "fs/TraversalMode.h"
#include "mdl/BrushNode.h"
#include "mdl/EntityModelManager.h"
#include "mdl/EntityNode.h"
#include "mdl/GameConfig.h"
#include "mdl/GameFileSystem.h"
#include "mdl/GameInfo.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Assets.h"
#include "mdl/Map_Brushes.h"
#include "mdl/Map_CopyPaste.h"
#include "mdl/Map_Selection.h"
#include "mdl/Map_World.h"
#include "mdl/PatchNode.h"
#include "mdl/SetVisibilityCommand.h"
#include "mdl/Transaction.h"
#include "mdl/UpdateBrushFaceAttributes.h"
#include "mdl/WadPropertyUtils.h"
#include "mdl/WorldNode.h"
#include "prefs/Preferences.h"
#include "project/AssetLibrary.h"
#include "ui/AppController.h"
#include "ui/AssetBrowserModel.h"
#include "ui/AssetInspector.h"
#include "ui/AssetLibrarySource.h"
#include "ui/AssetLibraryWindow.h"
#include "ui/BitmapButton.h"
#include "ui/CatalogAssetDrop.h"
#include "ui/ImageUtils.h"
#include "ui/MapDocument.h"
#include "ui/MapView3D.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/ModelBrowserView.h"
#include "ui/PrefabAsset.h"
#include "ui/ProjectCaptureConflictDialog.h"
#include "ui/ProjectSession.h"
#include "ui/QPathUtils.h"
#include "ui/QWidgetUtils.h"
#include "ui/SearchBox.h"

#include "kd/overload.h"
#include "kd/path_utils.h"
#include "kd/ranges/to.h"

#include <fmt/format.h>

#include <algorithm>
#include <optional>
#include <ranges>

namespace tb::ui
{
namespace
{

const auto AssetRootPath = std::filesystem::path{};
const auto DocumentLoadAssetRefreshDelay = 1500;
constexpr auto PrefabThumbnailMaxSize = 512;

std::optional<float> assetIconSizeForComboIndex(QComboBox* combo, const int index)
{
  if (!combo || index < 0)
  {
    return std::nullopt;
  }

  auto ok = false;
  const auto value = combo->itemData(index).toFloat(&ok);
  return ok ? std::optional<float>{value} : std::nullopt;
}

std::filesystem::path userPrefabAssetPath(const std::filesystem::path& path)
{
  const auto prefix = std::filesystem::path{"prefabs"};
  if (path.empty() || !kdl::path_has_prefix(path, prefix))
  {
    return {};
  }

  return configuredPrefabDirectory() / path.lexically_relative(prefix);
}

std::filesystem::path userPrefabBrowserPath(const std::filesystem::path& path)
{
  return std::filesystem::path{"prefabs"}
         / path.lexically_relative(configuredPrefabDirectory());
}

bool isolateSelectionForThumbnail(mdl::Map& map)
{
  auto selectedNodes = std::vector<mdl::Node*>{};
  auto unselectedNodes = std::vector<mdl::Node*>{};

  const auto collectNode = [&](auto& node) {
    if (node.transitivelySelected() || node.descendantSelected())
    {
      selectedNodes.push_back(&node);
    }
    else
    {
      unselectedNodes.push_back(&node);
    }
  };

  map.worldNode().accept(kdl::overload(
    [](auto&& thisLambda, mdl::WorldNode& worldNode) {
      worldNode.visitChildren(thisLambda);
    },
    [](auto&& thisLambda, mdl::LayerNode& layerNode) {
      layerNode.visitChildren(thisLambda);
    },
    [&](auto&& thisLambda, mdl::GroupNode& groupNode) {
      collectNode(groupNode);
      groupNode.visitChildren(thisLambda);
    },
    [&](auto&& thisLambda, mdl::EntityNode& entityNode) {
      collectNode(entityNode);
      entityNode.visitChildren(thisLambda);
    },
    [&](mdl::BrushNode& brushNode) { collectNode(brushNode); },
    [&](mdl::PatchNode& patchNode) { collectNode(patchNode); }));

  return map.executeAndStore(mdl::SetVisibilityCommand::hide(unselectedNodes))
         && map.executeAndStore(mdl::SetVisibilityCommand::show(selectedNodes));
}

MapWindow* parentMapWindow(QWidget& widget)
{
  return dynamic_cast<MapWindow*>(widget.window());
}

Result<void> savePrefabThumbnail(
  MapWindow& mapWindow, mdl::Map& map, const std::filesystem::path& prefabPath)
{
  auto* view = mapWindow.currentOrFirstVisible3DMapView();
  if (!view)
  {
    return Error{"No visible 3D view available for prefab thumbnail"};
  }

  auto transaction = mdl::Transaction{map, "Capture Prefab Thumbnail"};
  const auto selectedNodes = map.selection().nodes;
  if (!isolateSelectionForThumbnail(map))
  {
    transaction.cancel();
    return Error{"Failed to isolate selection for prefab thumbnail"};
  }
  mdl::deselectAll(map);

  auto image =
    cropPrefabThumbnailImage(view->grabPrefabThumbnailFramebuffer(selectedNodes));
  transaction.cancel();

  if (image.isNull())
  {
    return Error{"Failed to capture prefab thumbnail"};
  }

  if (std::max(image.width(), image.height()) > PrefabThumbnailMaxSize)
  {
    image = image.scaled(
      PrefabThumbnailMaxSize,
      PrefabThumbnailMaxSize,
      Qt::KeepAspectRatio,
      Qt::SmoothTransformation);
  }

  if (!image.save(pathAsQString(prefabThumbnailPath(prefabPath)), "PNG"))
  {
    return Error{"Failed to write prefab thumbnail"};
  }

  return kdl::void_success;
}

std::vector<std::string> currentPrefabWadPaths(const mdl::Map& map)
{
  if (const auto wadPropertyKey = map.gameInfo().gameConfig.materialConfig.property)
  {
    if (const auto* wadProperty = map.worldNode().entity().property(*wadPropertyKey))
    {
      return mdl::splitWadProperty(*wadProperty);
    }
  }
  return {};
}

} // namespace

ModelBrowser::ModelBrowser(
  AppController& appController,
  MapDocument& document,
  QWidget* parent,
  std::shared_ptr<ProjectSession> project)
  : QWidget{parent}
  , m_document{&document}
  , m_project{project ? std::move(project) : appController.projectFor(document)}
{
  if (m_project)
  {
    // A project owns all three layers, so the browser starts with the unified listing
    // instead of hiding project content behind a selector.
    m_sourceFilter = BrowserSourceFilter::All;
  }
  createGui(appController);
  bindEvents(appController);
  connectObservers();
  updateFolderEdit();
  if (m_project)
  {
    connect(
      &AssetLibraryChanges::instance(),
      &AssetLibraryChanges::changed,
      this,
      [this]() {
        markAssetsDirty();
        scheduleAssetRefresh();
      },
      Qt::QueuedConnection);
    refreshLibraryChoices();
  }
  updateSourceControls();
  if (m_project)
  {
    connect(m_project.get(), &ProjectSession::contentsChanged, this, [this]() {
      markAssetsDirty();
      scheduleAssetRefresh();
    });
    connect(m_project.get(), &ProjectSession::libraryLinksChanged, this, [this]() {
      refreshLibraryChoices();
      markAssetsDirty();
      scheduleAssetRefresh();
    });
  }
}

ModelBrowser::~ModelBrowser()
{
  if (m_fileSystemWatcher)
  {
    delete std::exchange(m_fileSystemWatcher, nullptr);
  }
}

void ModelBrowser::setDocument(MapDocument& document)
{
  if (m_document == &document)
    return;
  m_mapNotifierConnection.disconnect();
  m_notifierConnection.disconnect();
  m_document = &document;
  m_usageRefreshTimer->stop();
  m_view->setDocument(document);
  connectObservers();
  markAssetsDirty();
  scheduleAssetRefresh();
}

void ModelBrowser::createGui(AppController& appController)
{
  m_pathStack = new QStackedWidget{};
  m_pathStack->setObjectName("ModelBrowser_PathStack");

  m_breadcrumbBar = new QWidget{};
  m_breadcrumbBar->setObjectName("ModelBrowser_BreadcrumbBar");
  m_breadcrumbLayout = new QHBoxLayout{};
  m_breadcrumbLayout->setContentsMargins(6, 0, 0, 0);
  m_breadcrumbLayout->setSpacing(4);
  m_breadcrumbBar->setLayout(m_breadcrumbLayout);
  m_breadcrumbBar->installEventFilter(this);

  m_folderEdit = new QLineEdit{};
  m_folderEdit->setObjectName("ModelBrowser_FolderEdit");
  m_folderEdit->installEventFilter(this);

  m_pathStack->addWidget(m_breadcrumbBar);
  m_pathStack->addWidget(m_folderEdit);
  m_pathStack->setCurrentWidget(m_breadcrumbBar);

  m_reloadButton = new QToolButton{};
  m_reloadButton->setObjectName("ModelBrowser_ReloadButton");
  m_reloadButton->setIcon(loadSVGIcon(std::filesystem::path{"Refresh.svg"}));
  m_reloadButton->setToolTip(tr("Reload assets"));
  m_reloadButton->setAutoRaise(true);
  m_reloadButton->setProperty("tbControlRole", "compact");
  m_reloadButton->setFixedSize(QSize{24, 24});

  m_savePrefabButton = createBitmapButton("Add.svg", tr("Save selection as prefab"));
  m_savePrefabButton->setObjectName("ModelBrowser_SavePrefabButton");
  m_savePrefabButton->setProperty("tbControlRole", "compact");
  m_savePrefabButton->setFixedSize(QSize{24, 24});
  m_savePrefabButton->setEnabled(false);

  m_iconSizeCombo = new QComboBox{};
  m_iconSizeCombo->setObjectName("ModelBrowser_IconSize");
  m_iconSizeCombo->setMinimumWidth(58);
  m_iconSizeCombo->setToolTip(tr("Asset icon size"));
  m_iconSizeCombo->addItem("50%", 0.5f);
  m_iconSizeCombo->addItem("75%", 0.75f);
  m_iconSizeCombo->addItem("100%", 1.0f);
  m_iconSizeCombo->addItem("150%", 1.5f);
  m_iconSizeCombo->addItem("200%", 2.0f);
  m_iconSizeCombo->addItem("250%", 2.5f);
  m_iconSizeCombo->addItem("300%", 3.0f);
  updateIconSizeCombo();

  // The source filter chooses which layers the browser lists. The unified listing keeps
  // each entry's real source marker, so project content, library content and game files
  // never look alike.
  m_sourceCombo = new QComboBox{};
  m_sourceCombo->setObjectName("ModelBrowser_Source");
  m_sourceCombo->setMinimumWidth(96);
  m_sourceCombo->setToolTip(tr("Asset source filter"));
  if (m_project)
  {
    m_sourceCombo->addItem(tr("All Sources"), int(BrowserSourceFilter::All));
  }
  m_sourceCombo->addItem(tr("Game Files"), int(BrowserSourceFilter::GameFiles));
  if (m_project)
  {
    m_sourceCombo->addItem(tr("Project"), int(BrowserSourceFilter::Project));
    m_sourceCombo->addItem(tr("Asset Library"), int(BrowserSourceFilter::AssetLibrary));
  }

  m_libraryCombo = new QComboBox{};
  m_libraryCombo->setObjectName("ModelBrowser_Library");
  m_libraryCombo->setMinimumWidth(120);
  m_libraryCombo->setToolTip(tr("Asset library"));
  m_libraryCombo->setVisible(false);

  // The unified listing mixes maps, textures, models and sounds, so the type filter is a
  // second axis next to the source layer.
  m_typeCombo = new QComboBox{};
  m_typeCombo->setObjectName("ModelBrowser_Type");
  m_typeCombo->setMinimumWidth(88);
  m_typeCombo->setToolTip(tr("Asset type filter"));
  m_typeCombo->addItem(tr("All Types"), -1);
  m_typeCombo->addItem(tr("Maps"), int(BrowserCellType::Map));
  m_typeCombo->addItem(tr("Textures"), int(BrowserCellType::Texture));
  m_typeCombo->addItem(tr("Models"), int(BrowserCellType::Model));
  m_typeCombo->addItem(tr("Sprites"), int(BrowserCellType::Sprite));
  m_typeCombo->addItem(tr("Sounds"), int(BrowserCellType::Sound));
  m_typeCombo->addItem(tr("Prefabs"), int(BrowserCellType::Prefab));

  m_searchBox = createSearchBox();
  m_searchBox->setObjectName("ModelBrowser_Search");
  m_searchBox->setPlaceholderText(tr("Search assets…"));
  m_searchBox->setProperty("tbControlRole", "compact");
  m_searchBox->setMinimumWidth(140);
  m_searchBox->setMaximumWidth(320);
  for (auto* combo : {m_sourceCombo, m_typeCombo, m_libraryCombo, m_iconSizeCombo})
    combo->setProperty("tbControlRole", "compact");

  auto* pathRowLayout = new QHBoxLayout{};
  pathRowLayout->setContentsMargins(8, 3, 8, 3);
  pathRowLayout->setSpacing(6);
  pathRowLayout->addWidget(m_savePrefabButton);
  pathRowLayout->addWidget(m_reloadButton);
  pathRowLayout->addWidget(m_pathStack, 1);
  pathRowLayout->addWidget(m_searchBox, 1);
  pathRowLayout->addWidget(m_sourceCombo);
  pathRowLayout->addWidget(m_typeCombo);
  pathRowLayout->addWidget(m_libraryCombo);
  m_libraryCombo->setMaximumWidth(200);

  auto* controls = new QWidget{};
  controls->setObjectName("ModelBrowser_Controls");
  controls->setAttribute(Qt::WA_StyledBackground);
  controls->setLayout(pathRowLayout);

  m_folderTree = new QTreeWidget{};
  m_folderTree->setObjectName("ModelBrowser_FolderTree");
  m_folderTree->setHeaderHidden(true);
  m_folderTree->setUniformRowHeights(true);
  m_folderTree->setMouseTracking(true);
  m_folderTree->setAllColumnsShowFocus(true);
  m_folderTree->setIconSize(QSize{16, 16});

  m_scrollBar = new QScrollBar{Qt::Vertical};
  m_scrollBar->setObjectName("ModelBrowser_ScrollBar");
  m_view = new ModelBrowserView{appController, m_scrollBar, *m_document};
  m_view->setProject(m_project);
  m_view->setSearchText(m_searchBox->text());
  if (m_project)
  {
    connect(
      m_view,
      &ModelBrowserView::projectAssetSelected,
      m_project.get(),
      &ProjectSession::browserAssetSelected);
  }
  if (m_project)
  {
    // Library textures get a thumbnail from their compatible content; the file is written
    // on demand for visible cells so browsing never copies the whole library.
    m_view->setPreviewPathResolver([project = m_project](const BrowserAsset& asset) {
      return project->libraryAssetFile(asset.libraryId, asset.libraryAssetId);
    });
  }

  auto* browserLayout = new QHBoxLayout{};
  browserLayout->setContentsMargins(0, 0, 0, 0);
  browserLayout->setSpacing(0);
  browserLayout->addWidget(m_view, 1);
  browserLayout->addWidget(m_scrollBar, 0);

  auto* browser = new QWidget{};
  browser->setMinimumHeight(180);
  browser->setObjectName("ModelBrowser_AssetView");
  browser->setLayout(browserLayout);

  auto* sources = new QWidget{};
  sources->setObjectName("ModelBrowser_Sources");
  sources->setAttribute(Qt::WA_StyledBackground);
  auto* sourcesLayout = new QVBoxLayout{sources};
  sourcesLayout->setContentsMargins(0, 0, 0, 0);
  sourcesLayout->setSpacing(0);
  auto* sourcesTitle = new QLabel{tr("Sources")};
  sourcesTitle->setProperty("regionAnchor", true);
  sourcesLayout->addWidget(sourcesTitle);
  sourcesLayout->addWidget(m_folderTree, 1);

  auto* footer = new QWidget{};
  footer->setObjectName("ModelBrowser_ViewOptions");
  footer->setAttribute(Qt::WA_StyledBackground);
  auto* footerLayout = new QHBoxLayout{footer};
  footerLayout->setContentsMargins(8, 3, 8, 3);
  footerLayout->setSpacing(6);
  footerLayout->addStretch();
  m_usedButton = new QToolButton{};
  m_usedButton->setObjectName("ModelBrowser_UsedInMap");
  m_usedButton->setText(tr("Current map uses"));
  m_usedButton->setProperty("tbControlRole", "compact");
  m_usedButton->setCheckable(true);
  m_usedButton->setEnabled(bool(m_project));
  m_usageRefreshTimer = new QTimer{this};
  m_usageRefreshTimer->setSingleShot(true);
  m_usageRefreshTimer->setInterval(150);
  connect(m_usageRefreshTimer, &QTimer::timeout, this, [this]() {
    if (m_usedButton->isChecked())
    {
      refreshUsage();
      applyTypeFilter();
    }
  });
  footerLayout->insertWidget(0, m_usedButton);
  m_favoritesButton = new QToolButton{};
  m_favoritesButton->setObjectName("ModelBrowser_Favorites");
  m_favoritesButton->setText(tr("Favourites"));
  m_favoritesButton->setCheckable(true);
  m_favoritesButton->setProperty("tbControlRole", "compact");
  m_favoritesButton->setVisible(bool(m_project));
  m_tagCombo = new QComboBox{};
  m_tagCombo->setObjectName("ModelBrowser_Tag");
  m_tagCombo->setProperty("tbControlRole", "compact");
  m_tagCombo->addItem(tr("All tags"), QString{});
  m_tagCombo->setMaximumWidth(160);
  m_tagCombo->setVisible(bool(m_project));
  const auto metadataScope = tr(
    "Live library favourites and tags, including linked project snapshots. Unavailable "
    "library metadata does not match these filters.");
  m_favoritesButton->setToolTip(metadataScope);
  m_tagCombo->setToolTip(metadataScope);
  footerLayout->insertWidget(1, m_favoritesButton);
  footerLayout->insertWidget(2, m_tagCombo);
  m_soundCombo = new QComboBox{};
  m_soundCombo->setObjectName("ModelBrowser_SoundKind");
  m_soundCombo->setProperty("tbControlRole", "compact");
  m_soundCombo->addItem(tr("All sounds"), int(inspection::WaveKind::All));
  m_soundCombo->addItem(tr("Looping"), int(inspection::WaveKind::Looping));
  m_soundCombo->addItem(tr("One-shot"), int(inspection::WaveKind::OneShot));
  m_soundCombo->setMaximumWidth(128);
  m_soundCombo->setToolTip(tr(
    "Filter sound assets by their WAV sampler loop chunk. Waveforms are read on demand "
    "and cached until the asset revision changes."));
  footerLayout->insertWidget(3, m_soundCombo);
  connect(
    m_favoritesButton, &QToolButton::toggled, this, [this]() { applyTypeFilter(); });
  connect(
    m_tagCombo, &QComboBox::currentIndexChanged, this, [this]() { applyTypeFilter(); });
  connect(m_soundCombo, &QComboBox::currentIndexChanged, this, [this]() {
    m_waveKind = inspection::WaveKind(m_soundCombo->currentData().toInt());
    applyTypeFilter();
  });
  connect(m_usedButton, &QToolButton::toggled, this, [this]() {
    m_usageRefreshTimer->stop();
    refreshUsage();
    applyTypeFilter();
  });
  footerLayout->addWidget(new QLabel{tr("Thumbnail size")});
  footerLayout->addWidget(m_iconSizeCombo);

  auto* assets = new QWidget{};
  auto* assetsLayout = new QVBoxLayout{assets};
  assetsLayout->setContentsMargins(0, 0, 0, 0);
  assetsLayout->setSpacing(0);
  assetsLayout->addWidget(controls);
  assetsLayout->addWidget(browser, 1);
  assetsLayout->addWidget(footer);

  auto* splitter = new QSplitter{Qt::Horizontal};
  splitter->setObjectName("ModelBrowser_Splitter");
  splitter->setChildrenCollapsible(false);
  splitter->addWidget(sources);
  splitter->addWidget(assets);
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);
  splitter->setSizes({200, 700});

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(splitter, 1);
  setLayout(layout);

  m_fileSystemWatcher = new QFileSystemWatcher{this};
  m_rescanTimer = new QTimer{this};
  m_rescanTimer->setSingleShot(true);
}

void ModelBrowser::addViewOption(QWidget* control)
{
  auto* footer = findChild<QWidget*>("ModelBrowser_ViewOptions");
  auto* layout = qobject_cast<QHBoxLayout*>(footer->layout());
  layout->insertWidget(0, control);
}

void ModelBrowser::previewAsset(AppController&, const QString& path)
{
  const auto found = std::ranges::find_if(m_assets, [&](const auto& asset) {
    return pathAsGenericQString(asset.path) == path;
  });
  if (found == m_assets.end())
    return;
  const auto asset = *found;
  auto dialog = QDialog{this};
  dialog.setObjectName("AssetPreviewDialog");
  dialog.setWindowTitle(
    asset.displayName.empty() ? path : QString::fromStdString(asset.displayName));
  dialog.resize(640, 560);
  auto* layout = new QVBoxLayout{&dialog};
  auto* inspector = new AssetInspector{&dialog};
  if (!m_selectionOnly)
    bindInspector(*inspector, true);
  inspector->setAsset(inspection::readBrowserAsset(asset, m_project.get(), *m_document));
  layout->addWidget(inspector, 1);
  auto* close = new QDialogButtonBox{QDialogButtonBox::Close, &dialog};
  connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(close);
  dialog.exec();
}

void ModelBrowser::setMapActionsEnabled(const bool enabled)
{
  m_mapActionsEnabled = enabled;
  m_usedButton->setEnabled(enabled && bool(m_project));
  refreshUsage();
  applyTypeFilter();
  emit actionContextChanged();
}

Result<std::optional<std::string>> ModelBrowser::chooseAssetPath(
  AppController& app,
  MapDocument& document,
  const BrowserCellType type,
  QWidget* parent,
  const std::function<bool()>& targetValid,
  const bool skybox)
{
  auto session = app.projectFor(document);
  auto dialog = QDialog{parent};
  dialog.setObjectName("AssetSelectionDialog");
  dialog.setWindowTitle(tr("Select asset for current target"));
  dialog.resize(1000, 640);
  auto* layout = new QVBoxLayout{&dialog};
  auto* scope =
    new QLabel{tr("Target map: %1. The resource is captured only when you confirm.")
                 .arg(pathAsQString(document.map().path()))};
  scope->setWordWrap(true);
  layout->addWidget(scope);
  auto* split = new QSplitter{Qt::Horizontal};
  auto* browser = new ModelBrowser{app, document, split, session};
  browser->m_selectionOnly = true;
  browser->m_view->setSelectionOnly(true);
  browser->m_skyboxFilter = skybox;
  browser->setTypeFilter(type);
  browser->m_typeCombo->setCurrentIndex(browser->m_typeCombo->findData(int(type)));
  browser->m_typeCombo->setEnabled(false);
  browser->m_savePrefabButton->hide();
  auto* inspector = new AssetInspector{split};
  split->setStretchFactor(0, 3);
  split->setStretchFactor(1, 2);
  layout->addWidget(split, 1);
  auto* buttons = new QDialogButtonBox{QDialogButtonBox::Ok | QDialogButtonBox::Cancel};
  auto* ok = buttons->button(QDialogButtonBox::Ok);
  ok->setEnabled(false);
  layout->addWidget(buttons);
  auto selection = std::optional<BrowserAsset>{};
  connect(
    browser->m_view,
    &ModelBrowserView::assetSelected,
    &dialog,
    [&](const BrowserAsset& asset) {
      inspector->setAsset(inspection::readBrowserAsset(asset, session.get(), document));
      selection =
        inspector->asset() ? std::optional{inspector->asset()->resource} : std::nullopt;
      ok->setEnabled(
        selection && !selection->missing && selection->type == type
        && selection->skybox == skybox && targetValid());
    });
  connect(browser->m_view, &ModelBrowserView::selectionCleared, &dialog, [&]() {
    selection.reset();
    inspector->clear();
    ok->setEnabled(false);
  });
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() != QDialog::Accepted || !selection)
    return std::optional<std::string>{};
  if (!targetValid())
    return Error{"The editing target changed. Select the target again."};
  auto path = selection->path.generic_string();
  if (selection->source != BrowserSource::GameFiles)
  {
    if (!session)
      return Error{"A catalog resource requires a project target."};
    auto prepared = PreparedAssetDrop::prepare(
      session, catalogAssetDragData(*selection, session->store().info().id));
    if (prepared.is_error())
      return std::get<Error>(prepared.error());
    auto drop = std::move(prepared).value();
    for (const auto& conflict : drop->conflicts())
      if (!confirmCaptureConflict(parent, conflict))
        return std::optional<std::string>{};
    if (!targetValid())
      return Error{"The editing target changed. Select the target again."};
    drop->resolveConflicts(CaptureConflictChoice::Replace);
    const auto payload = drop->nativePayload();
    path = payload.substr(payload.find(':') + 1);
    const auto committed = drop->commit();
    if (committed.is_error())
      return std::get<Error>(committed.error());
  }
  else if (type == BrowserCellType::Texture && !skybox)
    path = selection->path.stem().string();
  return std::optional{path};
}

void ModelBrowser::bindInspector(AssetInspector& inspector, const bool mapActions)
{
  const auto refresh = [this, &inspector, mapActions]() {
    if (!inspector.asset())
      return;
    const auto& asset = inspector.asset()->resource;
    inspector.setNavigation(
      !asset.libraryId.empty() && !asset.libraryAssetId.empty(),
      asset.source == BrowserSource::AssetLibrary && !asset.projectAssetId.empty());
    const auto canApply =
      mapActions && m_mapActionsEnabled && asset.type == BrowserCellType::Texture
      && (asset.skybox || !m_document->map().selection().allBrushFaces().empty());
    inspector.setActions(
      canApply && !asset.missing,
      m_project && isLibraryAsset(asset) && !asset.missing
        && asset.projectAssetId.empty(),
      m_project && asset.projectUpdateAvailable && !asset.missing,
      m_project && (!asset.projectAssetId.empty() || (asset.source == BrowserSource::Project && !asset.assetId.empty())),
      tr("Project: %1\nMap: %2")
        .arg(
          m_project ? QString::fromStdString(m_project->store().info().name) : tr("None"))
        .arg(
          mapActions && m_mapActionsEnabled ? pathAsQString(m_document->map().path())
                                            : tr("None")));
  };
  connect(&inspector, &AssetInspector::assetChanged, this, refresh);
  connect(&inspector, &AssetInspector::showLibraryRequested, this, [this, &inspector]() {
    if (inspector.asset())
      showInAssetLibrary(inspector.asset()->resource);
  });
  connect(&inspector, &AssetInspector::showSnapshotRequested, this, [this, &inspector]() {
    if (inspector.asset())
      revealProjectAsset(inspector.asset()->resource.projectAssetId);
  });
  connect(this, &ModelBrowser::actionContextChanged, &inspector, refresh);
  connect(&inspector, &AssetInspector::captureRequested, this, [this, &inspector]() {
    if (inspector.asset())
    {
      const auto asset = inspector.asset()->resource;
      captureLibraryAsset(
        QString::fromStdString(asset.libraryId),
        QString::fromStdString(asset.libraryAssetId));
    }
  });
  connect(&inspector, &AssetInspector::updateRequested, this, [this, &inspector]() {
    if (inspector.asset())
    {
      const auto asset = inspector.asset()->resource;
      updateProjectAsset(QString::fromStdString(
        asset.projectAssetId.empty() ? asset.assetId : asset.projectAssetId));
    }
  });
  connect(&inspector, &AssetInspector::findUsesRequested, this, [this, &inspector]() {
    if (inspector.asset())
    {
      const auto& asset = inspector.asset()->resource;
      findProjectAssetUses(QString::fromStdString(
        asset.projectAssetId.empty() ? asset.assetId : asset.projectAssetId));
    }
  });
  connect(
    &inspector, &AssetInspector::applyRequested, this, [this, &inspector, mapActions]() {
      if (!mapActions || !m_mapActionsEnabled || !inspector.asset())
        return;
      const auto asset = inspector.asset()->resource;
      if (asset.type != BrowserCellType::Texture)
        return;
      auto* target = m_document;
      const auto faces = target->map().selection().allBrushFaces();
      if (!asset.skybox && faces.empty())
        return;
      auto name = asset.path.stem().string();
      if (asset.source != BrowserSource::GameFiles)
      {
        if (!m_project)
          return;
        auto prepared = PreparedAssetDrop::prepare(
          m_project, catalogAssetDragData(asset, m_project->store().info().id));
        if (prepared.is_error())
        {
          QMessageBox::warning(
            this,
            tr("Apply Asset"),
            QString::fromStdString(std::get<Error>(prepared.error()).msg));
          return;
        }
        auto drop = std::move(prepared).value();
        for (const auto& conflict : drop->conflicts())
          if (!confirmCaptureConflict(this, conflict))
            return;
        if (
          m_document != target || !m_mapActionsEnabled
          || faces != target->map().selection().allBrushFaces())
          return;
        drop->resolveConflicts(CaptureConflictChoice::Replace);
        const auto& payload = drop->nativePayload();
        name = payload.substr(payload.find(':') + 1);
        const auto committed = drop->commit();
        if (committed.is_error())
        {
          QMessageBox::warning(
            this,
            tr("Apply Asset"),
            QString::fromStdString(std::get<Error>(committed.error()).msg));
          return;
        }
      }
      // The same native commands as catalog drops: map Undo reverts the map edit;
      // the captured resource remains an explicit project snapshot.
      if (asset.skybox)
        mdl::setWorldProperty(target->map(), "skyname", name);
      else
        mdl::setBrushFaceAttributes(target->map(), faces, {.materialName = name});
      emit actionContextChanged();
    });
  refresh();
}

void ModelBrowser::bindEvents(AppController& appController)
{
  connect(
    m_view,
    &ModelBrowserView::showInLibraryRequested,
    this,
    &ModelBrowser::showInAssetLibrary);
  connect(
    m_view,
    &ModelBrowserView::showProjectSnapshotRequested,
    this,
    [this](const QString& id) { revealProjectAsset(id.toStdString()); });
  connect(
    m_view,
    &ModelBrowserView::assetPreviewRequested,
    this,
    [this, &appController](const QString& path) { previewAsset(appController, path); });
  connect(m_searchBox, &QLineEdit::textChanged, this, [&](const QString& text) {
    if (m_view)
    {
      m_view->setSearchText(text);
    }
  });

  connect(m_folderEdit, &QLineEdit::editingFinished, this, [&]() {
    if (!m_folderEdit->isModified())
    {
      showBreadcrumbBar();
      return;
    }

    const auto typedPath =
      std::filesystem::path{m_folderEdit->text().toStdString()}.lexically_normal();
    if (typedPath.empty() || typedPath == std::filesystem::path{"."})
    {
      setCurrentFolderPath(std::filesystem::path{});
      showBreadcrumbBar();
      return;
    }

    const auto rootPath = m_folderPath.lexically_normal();
    if (!rootPath.empty())
    {
      if (typedPath == rootPath)
      {
        setCurrentFolderPath(std::filesystem::path{});
        showBreadcrumbBar();
        return;
      }

      if (kdl::path_has_prefix(typedPath, rootPath))
      {
        setCurrentFolderPath(typedPath.lexically_relative(rootPath));
        showBreadcrumbBar();
        return;
      }
    }

    setFolderPath(typedPath);
    showBreadcrumbBar();
  });

  connect(m_fileSystemWatcher, &QFileSystemWatcher::directoryChanged, this, [&]() {
    scheduleRescan();
  });

  connect(m_rescanTimer, &QTimer::timeout, this, [&]() { rescanWatchedDirectory(); });

  connect(
    m_folderTree,
    &QTreeWidget::currentItemChanged,
    this,
    [&](QTreeWidgetItem* current, QTreeWidgetItem*) {
      if (!current)
      {
        return;
      }
      const auto rel = current->data(0, Qt::UserRole).toString();
      setCurrentFolderPath(std::filesystem::path{rel.toStdString()});
    });

  connect(
    m_view, &ModelBrowserView::folderActivated, this, [&](const QString& folderPath) {
      setCurrentFolderPath(std::filesystem::path{folderPath.toStdString()});
    });
  connect(
    m_view,
    &ModelBrowserView::mapActivated,
    this,
    [this, &appController](const QString& path) {
      if (!m_project)
        return;
      const auto asset =
        std::ranges::find(m_assets, pathFromQString(path), &BrowserAsset::path);
      if (asset == m_assets.end())
        return;
      const auto result =
        appController.mapWindowManager().loadProjectMap(m_project, asset->assetId);
      if (result.is_error())
        QMessageBox::critical(
          this,
          tr("Open Map"),
          QString::fromStdString(std::get<Error>(result.error()).msg));
    });
  connect(
    m_view, &ModelBrowserView::renamePrefabRequested, this, [&](const QString& path) {
      renamePrefab(pathFromQString(path));
    });
  connect(
    m_view, &ModelBrowserView::deletePrefabRequested, this, [&](const QString& path) {
      deletePrefab(pathFromQString(path));
    });

  connect(m_reloadButton, &QToolButton::clicked, this, [&]() {
    m_lastWriteTimes.clear();
    m_assetRefreshPending = false;
    if (m_sourceFilter != BrowserSourceFilter::GameFiles)
      refreshLibraryChoices();
    rescanWatchedDirectory();
  });

  connect(
    m_sourceCombo,
    QOverload<int>::of(&QComboBox::currentIndexChanged),
    this,
    [&](const int index) {
      setAssetSource(BrowserSourceFilter(m_sourceCombo->itemData(index).toInt()));
    });

  connect(
    m_typeCombo,
    QOverload<int>::of(&QComboBox::currentIndexChanged),
    this,
    [&](const int index) {
      const auto value = m_typeCombo->itemData(index).toInt();
      setTypeFilter(value < 0 ? std::nullopt : std::optional{BrowserCellType(value)});
    });

  connect(
    m_libraryCombo,
    QOverload<int>::of(&QComboBox::currentIndexChanged),
    this,
    [&](const int index) {
      m_libraryId = m_libraryCombo->itemData(index).toString().toStdString();
      markAssetsDirty();
      scheduleAssetRefresh();
    });

  connect(
    m_view,
    &ModelBrowserView::libraryAssetCaptureRequested,
    this,
    &ModelBrowser::captureLibraryAsset);
  connect(
    m_view,
    &ModelBrowserView::libraryAssetUpdateRequested,
    this,
    &ModelBrowser::updateProjectAsset);
  connect(
    m_view,
    &ModelBrowserView::libraryAssetFindUsesRequested,
    this,
    &ModelBrowser::findProjectAssetUses);

  connect(
    m_savePrefabButton, &QToolButton::clicked, this, [&]() { saveSelectionAsPrefab(); });

  connect(
    m_iconSizeCombo,
    QOverload<int>::of(&QComboBox::currentIndexChanged),
    this,
    [&](const int index) {
      if (const auto value = assetIconSizeForComboIndex(m_iconSizeCombo, index))
      {
        setPref(Preferences::AssetBrowserIconSize, *value);
      }
    });
}

void ModelBrowser::connectObservers()
{
  m_notifierConnection +=
    m_document->documentWasLoadedNotifier.connect(this, &ModelBrowser::documentWasLoaded);
  m_notifierConnection +=
    PreferenceManager::instance().preferenceDidChangeNotifier.connect(
      [this](const auto& path) {
        if (path == Preferences::AssetBrowserIconSize.path)
        {
          updateIconSizeCombo();
          m_view->invalidate();
          m_view->update();
        }
      });
  connectMapObservers();
}

void ModelBrowser::connectMapObservers()
{
  m_mapNotifierConnection.disconnect();
  m_mapNotifierConnection +=
    m_document->documentDidChangeNotifier.connect([this](const auto&...) {
      if (m_usedButton->isChecked())
        m_usageRefreshTimer->start();
    });
  m_mapNotifierConnection +=
    m_document->map().modsDidChangeNotifier.connect(this, &ModelBrowser::modsDidChange);
  m_mapNotifierConnection +=
    m_document->selectionDidChangeNotifier.connect([this](const auto&) {
      updateSavePrefabButton();
      emit actionContextChanged();
    });
  updateSavePrefabButton();
}

void ModelBrowser::documentWasLoaded()
{
  connectMapObservers();
  ++m_assetRefreshDeferralGeneration;
  m_assetRefreshPending = true;
  m_deferAssetRefreshAfterDocumentLoad = isVisible();
  scheduleDeferredAssetRefreshAfterDocumentLoad();
  updateSavePrefabButton();
}

void ModelBrowser::modsDidChange()
{
  // Project and library layers do not depend on the enabled game mods; only the game-file
  // layer does, and the unified listing includes it.
  if (
    m_sourceFilter == BrowserSourceFilter::Project
    || m_sourceFilter == BrowserSourceFilter::AssetLibrary)
  {
    return;
  }
  if (m_folderPath != AssetRootPath)
  {
    setFolderPath(AssetRootPath);
    updateSavePrefabButton();
    return;
  }

  markAssetsDirty();
  updateSavePrefabButton();
}

void ModelBrowser::showEvent(QShowEvent* event)
{
  QWidget::showEvent(event);
  scheduleDeferredAssetRefreshAfterDocumentLoad();
  scheduleAssetRefresh();
}

void ModelBrowser::updateIconSizeCombo()
{
  if (!m_iconSizeCombo)
  {
    return;
  }

  const auto value = pref(Preferences::AssetBrowserIconSize);
  auto index = 2;
  for (auto i = 0; i < m_iconSizeCombo->count(); ++i)
  {
    if (assetIconSizeForComboIndex(m_iconSizeCombo, i) == value)
    {
      index = i;
      break;
    }
  }

  const auto blocker = QSignalBlocker{m_iconSizeCombo};
  m_iconSizeCombo->setCurrentIndex(index);
}

bool ModelBrowser::eventFilter(QObject* obj, QEvent* event)
{
  if (obj == m_breadcrumbBar && event->type() == QEvent::MouseButtonDblClick)
  {
    showPathEditor();
    return true;
  }

  if (obj == m_folderEdit && event->type() == QEvent::KeyPress)
  {
    const auto* keyEvent = static_cast<QKeyEvent*>(event);
    if (keyEvent->key() == Qt::Key_Escape)
    {
      showBreadcrumbBar();
      return true;
    }
  }

  return QWidget::eventFilter(obj, event);
}

void ModelBrowser::rebuildBreadcrumbBar()
{
  if (!m_breadcrumbLayout)
  {
    return;
  }

  while (auto* item = m_breadcrumbLayout->takeAt(0))
  {
    if (auto* w = item->widget())
    {
      w->deleteLater();
    }
    delete item;
  }

  const auto addSeparator = [&]() {
    auto* sep = new QLabel{QString::fromUtf8("›")};
    sep->setEnabled(false);
    m_breadcrumbLayout->addWidget(sep, 0);
  };

  const auto addCrumb = [&](
                          const QString& title, const std::filesystem::path& folderPath) {
    auto* button = new QToolButton{};
    button->setAutoRaise(true);
    button->setText(title);
    connect(button, &QToolButton::clicked, this, [this, folderPath]() {
      setCurrentFolderPath(folderPath);
    });
    m_breadcrumbLayout->addWidget(button, 0);
  };

  addCrumb(tr("All"), std::filesystem::path{});

  auto accumulated = std::filesystem::path{};
  for (const auto& part : m_currentFolderPath)
  {
    accumulated /= part;
    addSeparator();
    addCrumb(QString::fromStdString(part.generic_string()), accumulated);
  }

  m_breadcrumbLayout->addStretch(1);
}

void ModelBrowser::showPathEditor()
{
  if (!m_pathStack || !m_folderEdit)
  {
    return;
  }

  auto displayedPath = m_folderPath;
  if (!m_currentFolderPath.empty())
  {
    displayedPath /= m_currentFolderPath;
  }

  auto displayedText = displayedPath.generic_string();
  if (displayedText.empty())
  {
    displayedText = "assets";
  }

  m_folderEdit->setText(QString::fromStdString(displayedText));
  m_folderEdit->setModified(false);
  m_pathStack->setCurrentWidget(m_folderEdit);
  m_folderEdit->setFocus();
  m_folderEdit->selectAll();
}

void ModelBrowser::showBreadcrumbBar()
{
  if (!m_pathStack || !m_breadcrumbBar)
  {
    return;
  }

  updateFolderEdit();
  m_pathStack->setCurrentWidget(m_breadcrumbBar);
}

void ModelBrowser::updateFolderEdit()
{
  rebuildBreadcrumbBar();
  if (!m_folderEdit || !m_pathStack || m_pathStack->currentWidget() == m_folderEdit)
  {
    return;
  }
}

void ModelBrowser::updateSavePrefabButton()
{
  if (m_savePrefabButton)
  {
    m_savePrefabButton->setEnabled(m_document->map().selection().hasNodes());
  }
}

void ModelBrowser::saveSelectionAsPrefab()
{
  auto& map = m_document->map();
  if (!map.selection().hasNodes())
  {
    return;
  }

  auto ok = false;
  auto prefabName = QInputDialog::getText(
    this, tr("Save Prefab"), tr("Prefab name:"), QLineEdit::Normal, {}, &ok);
  if (!ok)
  {
    return;
  }

  prefabName = prefabName.trimmed();
  if (prefabName.isEmpty())
  {
    return;
  }

  if (m_project)
  {
    const auto text = mdl::serializeSelectedNodes(map);
    auto input = project::AssetImport{};
    input.type = project::AssetType::Prefab;
    input.displayName = prefabName.toStdString();
    input.virtualPath = "prefabs/" + input.displayName + ".tbprefab";
    input.original.assign(text.begin(), text.end());
    input.compatible = input.original;
    const auto result = m_project->importAsset(input);
    if (result.is_error())
      QMessageBox::warning(
        this,
        tr("Save Prefab"),
        QString::fromStdString(std::get<Error>(result.error()).msg));
    return;
  }

  const auto prefabDirectory = configuredPrefabDirectory();
  if (const auto result =
        checkPrefabNameAvailable(prefabDirectory, prefabName.toStdString());
      result.is_error())
  {
    const auto error = std::get<tb::Error>(result.error());
    QMessageBox::warning(this, tr("Save Prefab"), QString::fromStdString(error.msg));
    return;
  }

  const auto prefabText = appendPrefabWadPaths(
    appendPrefabMaterialCollections(
      mdl::serializeSelectedNodes(map), mdl::enabledMaterialCollections(map)),
    currentPrefabWadPaths(map));
  const auto filePath = prefabPathForName(prefabDirectory, prefabName.toStdString());
  const auto result = writePrefabAsset(filePath, prefabText);
  if (result.is_error())
  {
    const auto error = std::get<tb::Error>(result.error());
    QMessageBox::warning(this, tr("Save Prefab"), QString::fromStdString(error.msg));
    return;
  }

  if (auto* mapWindow = parentMapWindow(*this))
  {
    if (const auto thumbnailResult = savePrefabThumbnail(*mapWindow, map, filePath);
        thumbnailResult.is_error())
    {
      const auto error = std::get<tb::Error>(thumbnailResult.error());
      QMessageBox::warning(this, tr("Save Prefab"), QString::fromStdString(error.msg));
    }
  }
  else
  {
    QMessageBox::warning(
      this, tr("Save Prefab"), tr("Could not find map window for prefab thumbnail"));
  }

  m_lastWriteTimes.clear();
  m_assetRefreshPending = false;
  rescanWatchedDirectory();
}

void ModelBrowser::renamePrefab(std::filesystem::path prefabPath)
{
  if (m_project)
  {
    for (const auto& entry : m_assets)
    {
      if (entry.path != prefabPath || entry.assetId.empty())
        continue;
      bool ok = false;
      const auto name = QInputDialog::getText(
                          this,
                          tr("Rename Prefab"),
                          tr("Prefab name:"),
                          QLineEdit::Normal,
                          QString::fromStdString(entry.displayName),
                          &ok)
                          .trimmed();
      if (ok && !name.isEmpty())
      {
        const auto result = m_project->renameAsset(entry.assetId, name.toStdString());
        if (result.is_error())
          QMessageBox::warning(
            this,
            tr("Rename Prefab"),
            QString::fromStdString(std::get<Error>(result.error()).msg));
      }
      return;
    }
    return;
  }
  const auto absPath = userPrefabAssetPath(prefabPath);
  if (absPath.empty())
  {
    return;
  }

  auto ok = false;
  auto prefabName = QInputDialog::getText(
    this,
    tr("Rename Prefab"),
    tr("Prefab name:"),
    QLineEdit::Normal,
    pathAsQString(absPath.stem()),
    &ok);
  if (!ok)
  {
    return;
  }

  prefabName = prefabName.trimmed();
  if (prefabName.isEmpty())
  {
    return;
  }

  const auto result = renamePrefabAsset(absPath, prefabName.toStdString());
  if (result.is_error())
  {
    const auto error = std::get<tb::Error>(result.error());
    QMessageBox::warning(this, tr("Rename Prefab"), QString::fromStdString(error.msg));
    return;
  }

  m_lastWriteTimes.clear();
  m_assetRefreshPending = false;
  rescanWatchedDirectory();
}

void ModelBrowser::deletePrefab(std::filesystem::path prefabPath)
{
  if (m_project)
  {
    for (const auto& entry : m_assets)
    {
      if (entry.path != prefabPath || entry.assetId.empty())
        continue;
      if (
        QMessageBox::question(
          this,
          tr("Delete Prefab"),
          tr("Delete prefab \"%1\"?").arg(QString::fromStdString(entry.displayName)))
        == QMessageBox::Yes)
      {
        const auto id = entry.assetId;
        const auto result = m_project->removeAsset(id);
        if (result.is_error())
          QMessageBox::warning(
            this,
            tr("Delete Prefab"),
            QString::fromStdString(std::get<Error>(result.error()).msg));
      }
      return;
    }
    return;
  }
  const auto absPath = userPrefabAssetPath(prefabPath);
  if (absPath.empty())
  {
    return;
  }

  if (
    QMessageBox::question(
      this,
      tr("Delete Prefab"),
      tr("Delete prefab \"%1\"?").arg(pathAsQString(absPath.stem())))
    != QMessageBox::Yes)
  {
    return;
  }

  const auto result = deletePrefabAsset(absPath);
  if (result.is_error())
  {
    const auto error = std::get<tb::Error>(result.error());
    QMessageBox::warning(this, tr("Delete Prefab"), QString::fromStdString(error.msg));
    return;
  }

  removePrefabFromBrowser(prefabPath);
}

void ModelBrowser::removePrefabFromBrowser(const std::filesystem::path& prefabPath)
{
  const auto oldSize = m_assets.size();
  std::erase_if(m_assets, [&](const auto& asset) {
    return asset.type == BrowserCellType::Prefab && asset.path == prefabPath;
  });

  if (m_assets.size() == oldSize)
  {
    m_lastWriteTimes.clear();
    scheduleRescan();
    return;
  }

  m_lastWriteTimes = assetLastWriteTimes(m_assets);
  rebuildFolderTree();
  m_view->setAssets(m_folderPath, visibleAssets());
  m_view->setCurrentFolderPath(m_currentFolderPath);
}

void ModelBrowser::setFolderPath(std::filesystem::path folderPath)
{
  folderPath = folderPath.lexically_normal();
  if (m_folderPath == folderPath)
  {
    return;
  }

  m_folderPath = std::move(folderPath);
  m_currentFolderPath.clear();
  updateFolderEdit();

  markAssetsDirty();
}

void ModelBrowser::setCurrentFolderPath(std::filesystem::path currentFolderPath)
{
  currentFolderPath = currentFolderPath.lexically_normal();
  if (currentFolderPath == std::filesystem::path{"."})
  {
    currentFolderPath.clear();
  }

  if (m_currentFolderPath == currentFolderPath)
  {
    return;
  }

  m_currentFolderPath = std::move(currentFolderPath);
  if (m_view)
  {
    m_view->setCurrentFolderPath(m_currentFolderPath);
  }

  if (m_folderTree)
  {
    if (auto it = m_folderTreeItems.find(m_currentFolderPath);
        it != m_folderTreeItems.end())
    {
      if (m_folderTree->currentItem() != it->second)
      {
        m_folderTree->setCurrentItem(it->second);
      }
    }
  }

  updateFolderEdit();
}

void ModelBrowser::markAssetsDirty()
{
  m_deferAssetRefreshAfterDocumentLoad = false;
  ++m_assetRefreshDeferralGeneration;
  m_assetRefreshPending = true;
  scheduleAssetRefresh();
}

void ModelBrowser::ensureAssetsLoaded()
{
  m_assetRefreshQueued = false;
  if (!m_assetRefreshPending)
  {
    return;
  }

  m_assetRefreshPending = false;
  reloadModels();
  setWatchedDirectory();
}

void ModelBrowser::scheduleAssetRefresh()
{
  if (!isVisible() || m_assetRefreshQueued || m_deferAssetRefreshAfterDocumentLoad)
  {
    return;
  }

  m_assetRefreshQueued = true;
  QTimer::singleShot(0, this, [this]() { ensureAssetsLoaded(); });
}

void ModelBrowser::scheduleDeferredAssetRefreshAfterDocumentLoad()
{
  if (!isVisible() || !m_deferAssetRefreshAfterDocumentLoad || !m_assetRefreshPending)
  {
    return;
  }

  const auto generation = m_assetRefreshDeferralGeneration;
  QTimer::singleShot(DocumentLoadAssetRefreshDelay, this, [this, generation]() {
    if (generation != m_assetRefreshDeferralGeneration)
    {
      return;
    }

    m_deferAssetRefreshAfterDocumentLoad = false;
    scheduleAssetRefresh();
  });
}

std::optional<std::vector<BrowserAsset>> ModelBrowser::scanProjectAssets() const
{
  if (!m_project)
    return std::nullopt;
  const auto catalog = m_project->store().assets();
  if (catalog.is_error())
    return std::nullopt;
  auto assets = std::vector<BrowserAsset>{};
  for (const auto& asset : catalog.value())
  {
    const auto path = pathFromQString(QString::fromStdString(asset.publishPath));
    const auto type = asset.type == project::AssetType::Map ? BrowserCellType::Map
                      : asset.type == project::AssetType::Texture
                          || asset.type == project::AssetType::Skybox
                        ? BrowserCellType::Texture
                        : assetTypeForExtension(path);
    if (type == BrowserCellType::Folder)
      continue;
    const auto file = m_project->assetFile(asset);
    if (file.is_error())
      continue;
    auto entry = BrowserAsset{type, path, file.value()};
    entry.source = BrowserSource::Project;
    entry.skybox = asset.type == project::AssetType::Skybox;
    entry.projectId = m_project->store().info().id;
    entry.assetId = asset.id;
    entry.revision = asset.revision;
    entry.displayName = asset.displayName;
    entry.catalogPath = pathFromQString(QString::fromStdString(asset.virtualPath));
    entry.projectAssetId = asset.id;
    entry.projectRevision = asset.revision;
    entry.projectPublishPath = path;
    if (!asset.library.empty())
    {
      entry.libraryId = asset.library.libraryId;
      entry.libraryAssetId = asset.library.libraryAssetId;
      entry.libraryRevision = asset.library.libraryRevision;
      entry.libraryContentHash = asset.library.libraryContentHash;
    }
    assets.push_back(std::move(entry));
  }
  return assets;
}

std::optional<std::vector<BrowserAsset>> ModelBrowser::scanLibraryAssets() const
{
  if (!m_project || m_libraryId.empty())
    return std::nullopt;
  auto state = m_project->libraryState(m_libraryId, false);
  if (state.is_error())
    return std::nullopt;
  if (!state.value().available)
    return std::nullopt;

  // Update state is project state, so it is queried once per scan instead of per entry.
  auto updates = std::vector<project::LibraryUpdate>{};
  if (auto result = m_project->checkLibraryUpdates(); result.is_success())
    updates = std::move(result).value();

  auto assets = std::vector<BrowserAsset>{};
  for (const auto& asset : state.value().assets)
  {
    const auto type = asset.type == project::AssetType::Map ? BrowserCellType::Map
                      : asset.type == project::AssetType::Texture
                          || asset.type == project::AssetType::Skybox
                        ? BrowserCellType::Texture
                        : assetTypeForExtension(
                            pathFromQString(QString::fromStdString(asset.virtualPath)));
    if (type == BrowserCellType::Folder)
      continue;
    auto entry =
      BrowserAsset{type, pathFromQString(QString::fromStdString(asset.virtualPath))};
    entry.source = BrowserSource::AssetLibrary;
    // A non-empty project ID marks a catalog-backed entry, which is what makes the
    // browser show display names and search them.
    entry.projectId = m_libraryId;
    entry.libraryId = m_libraryId;
    entry.libraryAssetId = asset.id;
    entry.libraryRevision = asset.revision;
    entry.libraryContentHash = asset.contentHash;
    entry.displayName = asset.displayName;
    entry.catalogPath = entry.path;
    entry.missing = asset.missing;
    entry.skybox = asset.type == project::AssetType::Skybox;
    if (const auto captured =
          m_project->projectAssetForLibraryAsset(m_libraryId, asset.id);
        captured.is_success() && captured.value())
    {
      const auto& projectAsset = *captured.value();
      entry.assetId = projectAsset.id;
      entry.projectAssetId = projectAsset.id;
      entry.projectRevision = projectAsset.revision;
      entry.projectPublishPath =
        pathFromQString(QString::fromStdString(projectAsset.publishPath));

      const auto update = std::ranges::find_if(updates, [&](const auto& candidate) {
        return candidate.projectAssetId == projectAsset.id;
      });
      entry.projectUpdateAvailable =
        update != updates.end()
        && (update->state == project::LibraryUpdateState::NewVersion || update->state == project::LibraryUpdateState::DependenciesChanged);
    }
    if (type != BrowserCellType::Texture)
    {
      // Non-texture resources have a real file in the library. Textures stay unresolved
      // here and get their compatible WAD only when a cell becomes visible, so browsing
      // never copies the whole library into the cache.
      if (const auto file = m_project->libraryAssetFile(m_libraryId, asset.id);
          file.is_success())
        entry.absolutePath = file.value();
    }
    assets.push_back(std::move(entry));
  }
  return assets;
}

std::optional<std::vector<BrowserAsset>> ModelBrowser::scanAssets()
{
  switch (m_sourceFilter)
  {
  case BrowserSourceFilter::Project:
    return scanProjectAssets();
  case BrowserSourceFilter::AssetLibrary:
    return scanLibraryAssets();
  case BrowserSourceFilter::GameFiles:
    return scanGameAssets();
  case BrowserSourceFilter::All:
    break;
  }

  // The unified listing tolerates a missing layer: a project without an attached library,
  // or a document without enabled game mods, still lists the remaining sources.
  auto gameAssets = scanGameAssets();
  auto projectAssets = scanProjectAssets();
  auto libraryAssets = scanLibraryAssets();
  if (!gameAssets && !projectAssets && !libraryAssets)
  {
    return std::nullopt;
  }

  return mergeBrowserAssets(
    projectAssets ? std::move(*projectAssets) : std::vector<BrowserAsset>{},
    libraryAssets ? std::move(*libraryAssets) : std::vector<BrowserAsset>{},
    gameAssets ? std::move(*gameAssets) : std::vector<BrowserAsset>{});
}

std::optional<std::vector<BrowserAsset>> ModelBrowser::scanGameAssets()
{
  if (m_folderPath.is_absolute())
  {
    return collectBrowserAssets(
      m_folderPath,
      {},
      [&](const auto& rootPath) {
        return fs::Disk::find(
          rootPath,
          fs::TraversalMode::Recursive,
          fs::makeExtensionPathMatcher(assetBrowserExtensions()));
      },
      [](const auto& path) { return Result<std::filesystem::path>{path}; },
      assetBrowserRoots());
  }

  const auto& map = m_document->map();
  const auto& fs = map.gameFileSystem();
  const auto enabledMods = mdl::enabledMods(map);
  if (enabledMods.empty())
  {
    return std::nullopt;
  }

  const auto modRoots =
    enabledMods | std::views::transform([&](const auto& mod) {
      return (map.gamePath() / std::filesystem::path{mod}).lexically_normal();
    })
    | kdl::ranges::to<std::vector>();

  return collectBrowserAssets(
    m_folderPath,
    modRoots,
    [&](const auto& rootPath) {
      if (rootPath == std::filesystem::path{"prefabs"})
      {
        return fs::Disk::find(
                 configuredPrefabDirectory(),
                 fs::TraversalMode::Recursive,
                 fs::makeExtensionPathMatcher(
                   std::vector<std::filesystem::path>{".tbprefab"}))
               | kdl::transform([](const auto& paths) {
                   return paths | std::views::transform(userPrefabBrowserPath)
                          | kdl::ranges::to<std::vector>();
                 });
      }

      return fs.find(
        rootPath,
        fs::TraversalMode::Recursive,
        fs::makeExtensionPathMatcher(assetBrowserExtensions()));
    },
    [&](const auto& path) {
      if (isPrefabAssetPath(path))
      {
        const auto absPath = userPrefabAssetPath(path);
        return absPath.empty()
                 ? Result<std::filesystem::path>{Error{"Invalid prefab path"}}
                 : Result<std::filesystem::path>{absPath};
      }
      return fs.makeAbsolute(path);
    },
    assetBrowserRoots());
}

void ModelBrowser::setAssetSource(const BrowserSourceFilter source)
{
  if (m_sourceFilter == source)
  {
    return;
  }
  saveBrowsingState();
  m_sourceFilter = source;
  m_folderPath.clear();
  m_currentFolderPath.clear();
  restoreBrowsingState();
  updateFolderEdit();
  refreshLibraryChoices();
  updateSourceControls();
  m_assetRefreshPending = true;
  scheduleAssetRefresh();
}

void ModelBrowser::setTypeFilter(const std::optional<BrowserCellType> type)
{
  if (m_typeFilter == type)
  {
    return;
  }
  saveBrowsingState();
  m_typeFilter = type;
  restoreBrowsingState();
  updateFolderEdit();
  applyTypeFilter();
}

int ModelBrowser::browsingStateKey() const
{
  return int(m_sourceFilter) * 16 + (m_typeFilter ? int(*m_typeFilter) + 1 : 0);
}

void ModelBrowser::saveBrowsingState()
{
  m_browsingStates[browsingStateKey()] = {
    m_currentFolderPath,
    m_searchBox->text(),
    m_pendingScrollRestore.value_or(m_scrollBar->value())};
}

void ModelBrowser::restoreBrowsingState()
{
  const auto key = browsingStateKey();
  const auto found = m_browsingStates.find(key);
  const auto state = found == m_browsingStates.end() ? BrowsingState{} : found->second;
  m_currentFolderPath = state.folder;
  m_pendingScrollRestore = state.scroll;
  m_searchBox->setText(state.search);
}

void ModelBrowser::applyTypeFilter()
{
  rebuildFolderTree();
  m_view->setIncludeDefaultRoots(
    browserFilterIncludesSource(m_sourceFilter, BrowserSource::GameFiles) && !m_typeFilter
    && !m_usedButton->isChecked() && !m_favoritesButton->isChecked()
    && m_tagCombo->currentData().toString().isEmpty());
  m_view->setAssets(m_folderPath, visibleAssets());
  m_view->setCurrentFolderPath(m_currentFolderPath);
  if (!m_assetRefreshPending && m_pendingScrollRestore)
  {
    m_view->setScrollPosition(*m_pendingScrollRestore);
    m_pendingScrollRestore.reset();
  }
}

std::vector<BrowserAsset> ModelBrowser::visibleAssets() const
{
  auto assets = filterBrowserAssets(m_assets, m_typeFilter);
  std::erase_if(assets, [&](const auto& asset) {
    return !assetMetadataMatches(
      asset.libraryMetadata,
      m_favoritesButton->isChecked(),
      m_tagCombo->currentData().toString());
  });
  if (m_usedButton->isChecked())
  {
    const auto id =
      m_project && m_mapActionsEnabled ? m_project->assetFor(*m_document) : std::nullopt;
    assets =
      id ? browserAssetsUsedInMap(assets, m_usage, *id) : std::vector<BrowserAsset>{};
  }
  if (m_skyboxFilter)
    std::erase_if(
      assets, [&](const auto& asset) { return asset.skybox != *m_skyboxFilter; });
  if (m_waveKind != inspection::WaveKind::All)
    std::erase_if(assets, [&](const auto& asset) {
      if (asset.type != BrowserCellType::Sound)
        return true;
      const auto info = waveInfoFor(asset);
      return !info || !inspection::matchesWaveKind(*info, m_waveKind);
    });
  return assets;
}

std::optional<inspection::WaveInfo> ModelBrowser::waveInfoFor(
  const BrowserAsset& asset) const
{
  const auto key = [&]() {
    if (asset.source == BrowserSource::AssetLibrary)
      return fmt::format(
        "library/{}/{}/{}", asset.libraryId, asset.libraryAssetId, asset.libraryRevision);
    if (asset.source == BrowserSource::Project)
      return fmt::format(
        "project/{}/{}/{}",
        asset.projectId,
        asset.projectAssetId.empty() ? asset.assetId : asset.projectAssetId,
        asset.revision);
    return fmt::format(
      "path/{}/{}/{}",
      asset.path.generic_string(),
      asset.absolutePath.generic_string(),
      asset.lastModified ? asset.lastModified->time_since_epoch().count() : 0);
  }();
  if (const auto cached = m_waveInfoCache.find(key); cached != m_waveInfoCache.end())
    return cached->second;

  auto info = std::optional<inspection::WaveInfo>{};
  if (!asset.path.empty())
  {
    const auto inspected =
      inspection::readBrowserAsset(asset, m_project.get(), *m_document);
    if (inspected.is_success())
    {
      const auto wave = inspection::readWaveInfo(inspected.value().compatible);
      if (wave.is_success())
        info = wave.value();
    }
  }
  if (m_waveInfoCache.size() > 4096)
    m_waveInfoCache.clear();
  m_waveInfoCache[key] = info;
  return info;
}

void ModelBrowser::refreshMetadataFilters()
{
  const auto selected = m_tagCombo->currentData().toString();
  const auto blocker = QSignalBlocker{m_tagCombo};
  auto tags = QStringList{};
  for (const auto& asset : m_assets)
    if (asset.libraryMetadata)
      for (const auto& tag : asset.libraryMetadata->tags)
        if (!tags.contains(QString::fromStdString(tag), Qt::CaseInsensitive))
          tags.push_back(QString::fromStdString(tag));
  tags.sort(Qt::CaseInsensitive);
  m_tagCombo->clear();
  m_tagCombo->addItem(tr("All tags"), QString{});
  for (const auto& tag : tags)
    m_tagCombo->addItem(tag, tag);
  // Keep an active filter when a library disappears instead of showing unrelated assets.
  if (!selected.isEmpty() && m_tagCombo->findData(selected) < 0)
    m_tagCombo->addItem(selected, selected);
  m_tagCombo->setCurrentIndex(std::max(0, m_tagCombo->findData(selected)));
}

void ModelBrowser::revealProjectAsset(const std::string& assetId)
{
  if (!m_project || assetId.empty())
    return;
  const auto asset = m_project->store().findAsset(assetId);
  if (asset.is_error() || !asset.value())
    return;
  emit m_project->browserFocusRequested();
  m_sourceCombo->setCurrentIndex(
    m_sourceCombo->findData(int(BrowserSourceFilter::Project)));
  if (!m_selectionOnly)
    m_typeCombo->setCurrentIndex(0);
  m_usedButton->setChecked(false);
  m_favoritesButton->setChecked(false);
  m_tagCombo->setCurrentIndex(0);
  m_searchBox->clear();
  markAssetsDirty();
  ensureAssetsLoaded();
  const auto found = std::ranges::find(m_assets, assetId, &BrowserAsset::assetId);
  if (found == m_assets.end())
    return;
  setCurrentFolderPath(
    (found->catalogPath.empty() ? found->path : found->catalogPath).parent_path());
  m_view->selectProjectAsset(assetId);
}

void ModelBrowser::showInAssetLibrary(const BrowserAsset& asset)
{
  if (!m_project || asset.libraryId.empty() || asset.libraryAssetId.empty())
    return;
  const auto connections = m_project->libraries();
  if (connections.is_error())
    return;
  for (const auto& connection : connections.value())
  {
    if (connection.info.id != asset.libraryId)
      continue;
    if (!connection.available)
    {
      QMessageBox::warning(
        this, tr("Asset library unavailable"), QString::fromStdString(connection.error));
      return;
    }
    auto source = openLocalAssetLibrary(connection.info.path);
    if (!source || source->libraryId() != asset.libraryId)
    {
      QMessageBox::warning(
        this,
        tr("Asset library unavailable"),
        tr("The folder no longer contains the expected library."));
      return;
    }
    const auto created =
      !m_libraryWindow || m_libraryWindow->libraryId() != asset.libraryId;
    if (created)
    {
      m_libraryWindow =
        new AssetLibraryWindow(pathAsQString(connection.info.path), false, this);
      m_libraryWindow->setAttribute(Qt::WA_DeleteOnClose);
      m_libraryWindow->setProjectContext(m_project);
    }
    const auto shown =
      m_libraryWindow->revealAsset(asset.libraryId, asset.libraryAssetId);
    if (shown.is_error())
    {
      QMessageBox::warning(
        this,
        tr("Show in asset library"),
        QString::fromStdString(std::get<Error>(shown.error()).msg));
      if (created)
        m_libraryWindow->close();
      return;
    }
    m_libraryWindow->show();
    m_libraryWindow->raise();
    m_libraryWindow->activateWindow();
    return;
  }
  QMessageBox::warning(
    this,
    tr("Asset library unavailable"),
    tr("This project's library connection no longer exists."));
}

void ModelBrowser::refreshUsage()
{
  m_usage = {};
  m_usageError.clear();
  const auto id =
    m_project && m_mapActionsEnabled ? m_project->assetFor(*m_document) : std::nullopt;
  if (m_usedButton->isChecked() && id)
  {
    const auto audit = auditGoldSrcProjectResources(*m_project, *id);
    if (audit.is_success())
      m_usage = audit.value();
    else
      m_usageError = QString::fromStdString(std::get<Error>(audit.error()).msg);
    for (const auto& use : m_usage.uses)
      if (use.status == ProjectResourceStatus::ScanError)
        m_usageError += QString::fromStdString(use.detail) + "\n";
  }
  m_usedButton->setToolTip(
    !id ? tr("An active project map is required")
    : !m_usageError.isEmpty()
      ? tr("Cannot scan current map: %1").arg(m_usageError)
      : tr("Explicit MAP resource references, including unsaved edits. Captured "
           "resources match by identity; game files match by engine path."));
}

void ModelBrowser::updateSourceControls()
{
  const auto includesLibrary =
    browserFilterIncludesSource(m_sourceFilter, BrowserSource::AssetLibrary);
  if (m_libraryCombo)
    m_libraryCombo->setVisible(includesLibrary);
  if (m_sourceCombo)
  {
    const QSignalBlocker blocker{m_sourceCombo};
    for (auto index = 0; index < m_sourceCombo->count(); ++index)
    {
      if (BrowserSourceFilter(m_sourceCombo->itemData(index).toInt()) == m_sourceFilter)
      {
        m_sourceCombo->setCurrentIndex(index);
        break;
      }
    }
  }
  // Saving a selection as a prefab is a game-file action, and the unified listing
  // includes the game files it works with.
  if (m_savePrefabButton)
  {
    m_savePrefabButton->setVisible(
      browserFilterIncludesSource(m_sourceFilter, BrowserSource::GameFiles));
  }
}

void ModelBrowser::refreshLibraryChoices()
{
  if (!m_libraryCombo)
    return;

  const QSignalBlocker blocker{m_libraryCombo};
  m_libraryCombo->clear();
  m_libraryError.clear();
  m_libraryId.clear();
  if (!m_project)
  {
    m_libraryCombo->setEnabled(false);
    return;
  }

  const auto libraries = m_project->libraries();
  if (libraries.is_success() && !libraries.value().empty())
  {
    // A single application-wide library backs every project, so the combo is only an
    // indicator of which catalog the browser lists.
    const auto& library = libraries.value().front();
    const auto name = QString::fromStdString(library.info.name);
    m_libraryCombo->addItem(
      library.available ? name : tr("%1 (unavailable)").arg(name),
      QString::fromStdString(library.info.id));
    if (!library.error.empty())
      m_libraryCombo->setItemData(
        0, QString::fromStdString(library.error), Qt::ToolTipRole);
    m_libraryId = library.info.id;
    m_libraryCombo->setCurrentIndex(0);
  }
  else
  {
    m_libraryCombo->addItem(tr("No asset library"), QString{});
    m_libraryCombo->setCurrentIndex(0);
  }
  m_libraryCombo->setEnabled(false);
}


void ModelBrowser::captureLibraryAsset(
  const QString& libraryId, const QString& libraryAssetId)
{
  if (!m_project)
    return;
  const auto captured = m_project->captureLibraryAssetResolving(
    libraryId.toStdString(),
    libraryAssetId.toStdString(),
    [this](const CaptureConflict& conflict) {
      return confirmCaptureConflict(this, conflict) ? CaptureConflictChoice::Replace
                                                    : CaptureConflictChoice::Cancel;
    });
  if (captured.is_error())
  {
    QMessageBox::critical(
      this,
      tr("Asset Library"),
      QString::fromStdString(std::get<Error>(captured.error()).msg));
    return;
  }
  if (!captured.value())
    return; // Replacing the existing resource was declined.
  markAssetsDirty();
  scheduleAssetRefresh();
}

void ModelBrowser::updateProjectAsset(const QString& projectAssetId)
{
  if (!m_project)
    return;
  const auto updated = m_project->updateLibraryAssets({projectAssetId.toStdString()});
  if (updated.is_error())
  {
    QMessageBox::critical(
      this,
      tr("Asset Library"),
      QString::fromStdString(std::get<Error>(updated.error()).msg));
    return;
  }
  markAssetsDirty();
  scheduleAssetRefresh();
}

void ModelBrowser::findProjectAssetUses(const QString& projectAssetId)
{
  if (m_project)
    emit m_project->browserFindUsesRequested(projectAssetId);
}

void ModelBrowser::reloadModels()
{
  const auto scroll = m_pendingScrollRestore.value_or(m_scrollBar->value());
  refreshUsage();
  auto nextAssets = scanAssets();

  if (!nextAssets)
  {
    m_assets.clear();
    m_lastWriteTimes.clear();
    m_currentFolderPath.clear();
    updateFolderEdit();
    rebuildFolderTree();
    m_view->setAssets(m_folderPath, {});
    m_view->setCurrentFolderPath(m_currentFolderPath);
    return;
  }

  m_assets = std::move(*nextAssets);
  if (m_project)
    loadBrowserAssetMetadata(m_assets, *m_project);
  refreshMetadataFilters();
  m_lastWriteTimes = assetLastWriteTimes(m_assets);

  rebuildFolderTree();
  m_view->setIncludeDefaultRoots(
    browserFilterIncludesSource(m_sourceFilter, BrowserSource::GameFiles) && !m_typeFilter
    && !m_usedButton->isChecked() && !m_favoritesButton->isChecked()
    && m_tagCombo->currentData().toString().isEmpty());
  m_view->setShowSourceMarkers(m_sourceFilter == BrowserSourceFilter::All);
  m_view->setAssets(m_folderPath, visibleAssets());
  m_view->setCurrentFolderPath(m_currentFolderPath);
  m_view->setScrollPosition(scroll);
  m_pendingScrollRestore.reset();
}

void ModelBrowser::rebuildFolderTree()
{
  if (!m_folderTree)
  {
    return;
  }

  auto blocker = QSignalBlocker{m_folderTree};

  m_folderTree->clear();
  m_folderTreeItems.clear();

  const auto folderIcon = loadSVGIcon(std::filesystem::path{"BrowserFolderSmall.svg"});

  auto rootTitle = m_folderPath.filename().empty() ? std::filesystem::path{"assets"}
                                                   : m_folderPath.filename();
  auto rootTitleText = rootTitle.generic_string();
  if (rootTitleText.empty())
  {
    rootTitleText = ".";
  }

  auto* rootItem =
    new QTreeWidgetItem{m_folderTree, QStringList{QString::fromStdString(rootTitleText)}};
  rootItem->setData(0, Qt::UserRole, QString{});
  rootItem->setIcon(0, folderIcon);
  m_folderTreeItems.emplace(std::filesystem::path{}, rootItem);

  if (
    m_folderPath.empty() && !m_typeFilter && !m_usedButton->isChecked()
    && !m_favoritesButton->isChecked() && m_tagCombo->currentData().toString().isEmpty())
  {
    for (const auto& [type, rootPath] : assetBrowserRoots())
    {
      unused(type);
      if (rootPath.empty() || m_folderTreeItems.contains(rootPath))
      {
        continue;
      }

      auto* item = new QTreeWidgetItem{
        rootItem, QStringList{QString::fromStdString(rootPath.generic_string())}};
      item->setData(0, Qt::UserRole, QString::fromStdString(rootPath.generic_string()));
      item->setIcon(0, folderIcon);
      m_folderTreeItems.emplace(rootPath, item);
    }
  }

  for (const auto& asset : visibleAssets())
  {
    const auto& assetPath = asset.catalogPath.empty() ? asset.path : asset.catalogPath;
    if (assetPath.empty())
    {
      continue;
    }

    if (!m_folderPath.empty() && !kdl::path_has_prefix(assetPath, m_folderPath))
    {
      continue;
    }

    const auto relAssetPath =
      m_folderPath.empty() ? assetPath : assetPath.lexically_relative(m_folderPath);
    const auto relFolderPath = relAssetPath.parent_path();

    auto* parentItem = rootItem;
    auto accumulatedPath = std::filesystem::path{};
    for (const auto& part : relFolderPath)
    {
      accumulatedPath /= part;
      if (m_folderTreeItems.contains(accumulatedPath))
      {
        parentItem = m_folderTreeItems[accumulatedPath];
        continue;
      }

      auto* item = new QTreeWidgetItem{
        parentItem, QStringList{QString::fromStdString(part.generic_string())}};
      item->setData(
        0, Qt::UserRole, QString::fromStdString(accumulatedPath.generic_string()));
      item->setIcon(0, folderIcon);
      parentItem = item;
      m_folderTreeItems.emplace(accumulatedPath, item);
    }
  }

  m_folderTree->expandItem(rootItem);

  if (auto it = m_folderTreeItems.find(m_currentFolderPath);
      it != m_folderTreeItems.end())
  {
    m_folderTree->setCurrentItem(it->second);
  }
  else
  {
    m_currentFolderPath.clear();
    m_folderTree->setCurrentItem(rootItem);
    if (m_view)
    {
      m_view->setCurrentFolderPath(m_currentFolderPath);
    }
    updateFolderEdit();
  }
}

void ModelBrowser::setWatchedDirectory()
{
  if (!m_fileSystemWatcher)
  {
    return;
  }

  if (!m_fileSystemWatcher->directories().isEmpty())
  {
    m_fileSystemWatcher->removePaths(m_fileSystemWatcher->directories());
  }

  // Project and library entries live in the project file or in a library catalog, so
  // there is no directory to watch; both notify through ProjectSession instead. The
  // unified listing still contains game files, so it keeps watching their roots.
  if (
    m_sourceFilter == BrowserSourceFilter::Project
    || m_sourceFilter == BrowserSourceFilter::AssetLibrary)
  {
    return;
  }

  if (m_folderPath.is_absolute())
  {
    if (fs::Disk::pathInfo(m_folderPath) == fs::PathInfo::Directory)
    {
      m_fileSystemWatcher->addPath(pathAsQString(m_folderPath));
    }
    return;
  }

  const auto& map = m_document->map();
  const auto& fs = map.gameFileSystem();
  if (mdl::enabledMods(map).empty())
  {
    return;
  }

  if (m_folderPath.empty())
  {
    for (const auto& [type, rootPath] : assetBrowserRoots())
    {
      unused(type);
      if (auto absPathResult = fs.makeAbsolute(rootPath); !absPathResult.is_error())
      {
        const auto& absPath = absPathResult.value();
        if (fs::Disk::pathInfo(absPath) == fs::PathInfo::Directory)
        {
          m_fileSystemWatcher->addPath(pathAsQString(absPath));
        }
      }
    }
    return;
  }

  if (auto absPathResult = fs.makeAbsolute(m_folderPath); !absPathResult.is_error())
  {
    const auto& absPath = absPathResult.value();
    if (fs::Disk::pathInfo(absPath) == fs::PathInfo::Directory)
    {
      m_fileSystemWatcher->addPath(pathAsQString(absPath));
    }
  }
}

void ModelBrowser::scheduleRescan()
{
  if (m_rescanTimer)
  {
    m_rescanTimer->start(200);
  }
}

void ModelBrowser::rescanWatchedDirectory()
{
  auto nextAssets = scanAssets();

  if (!nextAssets)
  {
    for (const auto& p : entityModelAssetPaths(m_assets))
    {
      m_document->map().entityModelManager().invalidateModel(p);
    }
    m_assets.clear();
    m_lastWriteTimes.clear();
    m_currentFolderPath.clear();
    updateFolderEdit();
    rebuildFolderTree();
    m_view->setAssets(m_folderPath, {});
    m_view->setCurrentFolderPath(m_currentFolderPath);
    return;
  }

  auto newLastWriteTimes = assetLastWriteTimes(*nextAssets);
  const auto changedPaths =
    changedAssetPaths(m_lastWriteTimes, newLastWriteTimes, m_assets, *nextAssets);
  if (changedPaths.empty() && *nextAssets == m_assets)
  {
    return;
  }

  m_document->map().reloadEntityModels(changedPaths);

  m_assets = std::move(*nextAssets);
  m_lastWriteTimes = std::move(newLastWriteTimes);
  rebuildFolderTree();
  m_view->setIncludeDefaultRoots(
    browserFilterIncludesSource(m_sourceFilter, BrowserSource::GameFiles) && !m_typeFilter
    && !m_usedButton->isChecked() && !m_favoritesButton->isChecked()
    && m_tagCombo->currentData().toString().isEmpty());
  m_view->setShowSourceMarkers(m_sourceFilter == BrowserSourceFilter::All);
  m_view->setAssets(m_folderPath, visibleAssets());
  m_view->setCurrentFolderPath(m_currentFolderPath);
}

} // namespace tb::ui
