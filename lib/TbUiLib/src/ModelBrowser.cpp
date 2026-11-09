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

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStringList>
#include <QTabBar>
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
#include "mdl/EntityDefinitionUtils.h"
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
#include "ui/BuiltinAssetLibrary.h"
#include "ui/CatalogAssetDrop.h"
#include "ui/EntityBrowser.h"
#include "ui/EntityBrowserView.h"
#include "ui/ImageUtils.h"
#include "ui/MapDocument.h"
#include "ui/MapView3D.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/ModelBrowserView.h"
#include "ui/PanelLayout.h"
#include "ui/PopupWindow.h"
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
#include <unordered_map>

namespace tb::ui
{
namespace
{

const auto AssetRootPath = std::filesystem::path{};
const auto DocumentLoadAssetRefreshDelay = 1500;
constexpr auto PrefabThumbnailMaxSize = 512;
constexpr auto SkyboxTypeFilter = -2;
constexpr auto EntityFolderRole = Qt::UserRole + 1;

BrowserCellType catalogCellType(
  const project::AssetType type, const std::filesystem::path& path)
{
  switch (type)
  {
  case project::AssetType::Map:
    return BrowserCellType::Map;
  case project::AssetType::Texture:
  case project::AssetType::Skybox:
    return BrowserCellType::Texture;
  case project::AssetType::Material:
    return BrowserCellType::Material;
  default:
    return assetTypeForExtension(path);
  }
}

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
  , m_appController{appController}
  , m_project{project ? std::move(project) : appController.projectFor(document)}
{
  if (m_project)
  {
    // Start in the reusable library; project snapshots have their own listing.
    m_sourceFilter = BrowserSourceFilter::AssetLibrary;
  }
  createGui(appController);
  bindEvents(appController);
  connectObservers();
  updateFolderEdit();
  // The initial folder is the first history entry, so leaving it can be undone.
  recordFolderHistory();
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
    refreshLibraryConnection();
  }
  updateSourceControls();
  if (m_project)
  {
    connect(m_project.get(), &ProjectSession::mapContentsChanged, this, [this]() {
      if (m_unusedButton->isChecked())
        m_usageRefreshTimer->start();
    });
    connect(m_project.get(), &ProjectSession::contentsChanged, this, [this]() {
      markAssetsDirty();
      scheduleAssetRefresh();
    });
    connect(m_project.get(), &ProjectSession::libraryLinksChanged, this, [this]() {
      refreshLibraryConnection();
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
  if (m_entityBrowser)
    m_entityBrowser->setDocument(document);
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

  // Project browsers list project and library content; standalone maps list game files.
  m_sourceTabs = new QTabBar{};
  m_sourceTabs->setObjectName("ModelBrowser_Source");
  m_sourceTabs->setAccessibleName(tr("Content browser mode"));
  m_sourceTabs->setToolTip(tr("Content browser mode"));
  m_sourceTabs->setExpanding(false);
  m_sourceTabs->setDrawBase(false);
  m_sourceTabs->setUsesScrollButtons(false);
  if (m_project)
  {
    m_sourceTabs->setTabData(
      m_sourceTabs->addTab(tr("Asset Library")), int(BrowserSourceFilter::AssetLibrary));
    m_sourceTabs->setTabData(
      m_sourceTabs->addTab(tr("Current Project")), int(BrowserSourceFilter::Project));
  }
  else
  {
    m_sourceTabs->setTabData(
      m_sourceTabs->addTab(tr("Game Files")), int(BrowserSourceFilter::GameFiles));
  }
  m_sourceTabs->setVisible(bool(m_project));

  // The unified listing mixes maps, textures, models and sounds, so the type filter is a
  // second axis next to the source layer.
  m_typeCombo = new QComboBox{};
  m_typeCombo->setObjectName("ModelBrowser_Type");
  m_typeCombo->setMinimumWidth(88);
  m_typeCombo->setMaximumWidth(190);
  m_typeCombo->setToolTip(tr("Asset type filter"));
  m_typeCombo->addItem(tr("All Types"), -1);
  m_typeCombo->addItem(tr("Maps"), int(BrowserCellType::Map));
  m_typeCombo->addItem(tr("Textures"), int(BrowserCellType::Texture));
  m_typeCombo->addItem(tr("Terrain Materials"), int(BrowserCellType::Material));
  m_typeCombo->addItem(tr("Models"), int(BrowserCellType::Model));
  m_typeCombo->addItem(tr("Sprites"), int(BrowserCellType::Sprite));
  m_typeCombo->addItem(tr("Sounds"), int(BrowserCellType::Sound));
  m_typeCombo->addItem(tr("Skyboxes"), SkyboxTypeFilter);
  m_typeCombo->addItem(tr("Prefabs"), int(BrowserCellType::Prefab));
  m_typeCombo->setAccessibleName(tr("Content type"));

  m_backButton = new QToolButton{};
  m_backButton->setObjectName("ModelBrowser_Back");
  m_backButton->setText(QString::fromUtf8("←"));
  m_backButton->setToolTip(tr("Back"));
  m_backButton->setAutoRaise(true);
  m_backButton->setProperty("tbControlRole", "compact");
  m_backButton->setFixedSize(QSize{24, 24});
  m_backButton->setEnabled(false);

  m_forwardButton = new QToolButton{};
  m_forwardButton->setObjectName("ModelBrowser_Forward");
  m_forwardButton->setText(QString::fromUtf8("→"));
  m_forwardButton->setToolTip(tr("Forward"));
  m_forwardButton->setAutoRaise(true);
  m_forwardButton->setProperty("tbControlRole", "compact");
  m_forwardButton->setFixedSize(QSize{24, 24});
  m_forwardButton->setEnabled(false);

  m_searchBox = createSearchBox();
  m_searchBox->setObjectName("ModelBrowser_Search");
  m_searchBox->setProperty("tbControlRole", "compact");
  m_searchBox->setMinimumWidth(140);
  m_searchBox->setMaximumWidth(320);

  // The search scope has to be visible before the user types: text alone cannot say
  // whether a search covers the current folder or the whole listing.
  m_searchScopeCombo = new QComboBox{};
  m_searchScopeCombo->setObjectName("ModelBrowser_SearchScope");
  m_searchScopeCombo->setToolTip(tr("Search scope"));
  m_searchScopeCombo->addItem(tr("This folder"), false);
  m_searchScopeCombo->addItem(tr("All assets"), true);
  m_searchScopeCombo->setMinimumWidth(104);
  updateSearchPlaceholder();

  for (auto* combo : {m_typeCombo, m_iconSizeCombo, m_searchScopeCombo})
  {
    combo->setProperty("tbControlRole", "compact");
  }

  m_filterButton = new QToolButton{};
  m_filterButton->setObjectName("ModelBrowser_FiltersButton");
  m_filterButton->setText(tr("Filters"));
  m_filterButton->setIcon(loadSVGIcon(std::filesystem::path{"Filter.svg"}));
  m_filterButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  m_filterButton->setToolTip(tr("Choose which assets are listed"));
  m_filterButton->setCheckable(true);
  m_filterButton->setProperty("tbControlRole", "chip");

  m_viewButton = new QToolButton{};
  m_viewButton->setObjectName("ModelBrowser_ViewButton");
  m_viewButton->setText(tr("View"));
  m_viewButton->setIcon(loadSVGIcon(std::filesystem::path{"ViewGrid.svg"}));
  m_viewButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  m_viewButton->setToolTip(tr("Thumbnail size and grouping"));
  m_viewButton->setCheckable(true);
  m_viewButton->setProperty("tbControlRole", "chip");

  auto* pathRowLayout = new QHBoxLayout{};
  setPanelInsets(*pathRowLayout, PanelInsets::Toolbar);
  pathRowLayout->setSpacing(4);
  pathRowLayout->addWidget(m_sourceTabs);
  pathRowLayout->addWidget(m_backButton);
  pathRowLayout->addWidget(m_forwardButton);
  pathRowLayout->addWidget(m_savePrefabButton);
  pathRowLayout->addWidget(m_reloadButton);
  pathRowLayout->addWidget(m_pathStack, 1);
  pathRowLayout->addWidget(m_searchBox, 1);
  pathRowLayout->addWidget(m_searchScopeCombo);
  pathRowLayout->addWidget(m_filterButton);
  pathRowLayout->addWidget(m_viewButton);
  m_entitySettings = new QToolButton{};
  m_entitySettings->setObjectName("EntityBrowser_Settings");
  m_entitySettings->setText(tr("Definitions…"));
  m_entitySettings->setToolTip(
    tr("Configure entity definition files for the current map"));
  m_entitySettings->hide();
  pathRowLayout->addWidget(m_entitySettings);
  connect(m_entitySettings, &QToolButton::clicked, this, [this] {
    if (m_entityBrowser)
      m_entityBrowser->showSettings();
  });
  m_toolbarLayout = pathRowLayout;

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
    // Library textures get a thumbnail from their compatible content; the files are
    // written on demand for the visible cells so browsing never copies the whole library.
    // One catalog pass per library serves the whole visible page.
    m_view->setPreviewPathResolver(
      [project = m_project](const std::vector<BrowserAsset>& assets) {
        using ResolvedFiles = std::map<std::filesystem::path, std::filesystem::path>;
        auto byLibrary = std::map<std::string, std::vector<const BrowserAsset*>>{};
        for (const auto& asset : assets)
          if (!asset.libraryId.empty())
            byLibrary[asset.libraryId].push_back(&asset);

        auto files = ResolvedFiles{};
        for (const auto& [libraryId, entries] : byLibrary)
        {
          auto ids = std::vector<std::string>{};
          ids.reserve(entries.size());
          for (const auto* entry : entries)
            ids.push_back(entry->libraryAssetId);

          auto resolved = project->libraryAssetFiles(libraryId, ids);
          if (resolved.is_error())
            return Result<ResolvedFiles>{std::get<Error>(resolved.error())};
          for (const auto* entry : entries)
            if (const auto file = resolved.value().find(entry->libraryAssetId);
                file != resolved.value().end())
              files.emplace(entry->path, file->second);
        }
        return Result<ResolvedFiles>{std::move(files)};
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

  m_usedButton = new QToolButton{};
  m_usedButton->setObjectName("ModelBrowser_UsedInMap");
  m_usedButton->setText(tr("Used in map"));
  m_usedButton->setProperty("tbControlRole", "compact");
  m_usedButton->setCheckable(true);
  m_usedButton->setEnabled(bool(m_project));
  m_unusedButton = new QToolButton{};
  m_unusedButton->setObjectName("ModelBrowser_Unreferenced");
  m_unusedButton->setText(tr("No map references"));
  m_unusedButton->setProperty("tbControlRole", "compact");
  m_unusedButton->setCheckable(true);
  m_usageRefreshTimer = new QTimer{this};
  m_usageRefreshTimer->setSingleShot(true);
  m_usageRefreshTimer->setInterval(150);
  connect(m_usageRefreshTimer, &QTimer::timeout, this, [this]() {
    if (m_usedButton->isChecked() || m_unusedButton->isChecked())
    {
      refreshUsage();
      applyTypeFilter();
    }
  });
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
  m_tagCombo->setVisible(bool(m_project));
  const auto metadataScope = tr(
    "Live library favourites and tags, including linked project snapshots. Unavailable "
    "library metadata does not match these filters.");
  m_favoritesButton->setToolTip(metadataScope);
  m_tagCombo->setToolTip(metadataScope);
  m_soundCombo = new QComboBox{};
  m_soundCombo->setObjectName("ModelBrowser_SoundKind");
  m_soundCombo->setProperty("tbControlRole", "compact");
  m_soundCombo->addItem(tr("All sounds"), int(inspection::WaveKind::All));
  m_soundCombo->addItem(tr("Looping"), int(inspection::WaveKind::Looping));
  m_soundCombo->addItem(tr("One-shot"), int(inspection::WaveKind::OneShot));
  m_soundCombo->setToolTip(tr(
    "Filter sound assets by their WAV sampler loop chunk. Waveforms are read on demand "
    "and cached until the asset revision changes."));
  m_groupWadTextures = new QCheckBox{tr("Group by WAD")};
  m_groupWadTextures->setObjectName("ModelBrowser_GroupWadTextures");
  m_groupWadTextures->setProperty("tbControlRole", "compact");
  m_groupWadTextures->setToolTip(
    tr("Show WAD texture members in subfolders of textures."));
  m_groupWadTextures->setChecked(
    QSettings{}.value("AssetBrowser/GroupWadTextures", false).toBool());
  connect(m_groupWadTextures, &QCheckBox::toggled, this, [this](const bool checked) {
    QSettings{}.setValue("AssetBrowser/GroupWadTextures", checked);
    // Keep texture navigation useful when a WAD subfolder disappears.
    if (!m_currentFolderPath.empty() && *m_currentFolderPath.begin() == "textures")
      m_currentFolderPath = "textures";
    applyTypeFilter();
    updateFolderEdit();
  });
  connect(
    m_favoritesButton, &QToolButton::toggled, this, [this]() { applyTypeFilter(); });
  connect(
    m_tagCombo, &QComboBox::currentIndexChanged, this, [this]() { applyTypeFilter(); });
  connect(m_soundCombo, &QComboBox::currentIndexChanged, this, [this]() {
    m_waveKind = inspection::WaveKind(m_soundCombo->currentData().toInt());
    applyTypeFilter();
  });
  connect(m_unusedButton, &QToolButton::toggled, this, [this](const bool checked) {
    if (checked)
    {
      const auto blocker = QSignalBlocker{m_usedButton};
      m_usedButton->setChecked(false);
    }
    m_usageRefreshTimer->stop();
    refreshUsage();
    applyTypeFilter();
  });
  connect(m_usedButton, &QToolButton::toggled, this, [this](const bool checked) {
    if (checked)
    {
      const auto blocker = QSignalBlocker{m_unusedButton};
      m_unusedButton->setChecked(false);
    }
    m_usageRefreshTimer->stop();
    refreshUsage();
    applyTypeFilter();
  });

  // Filters narrow what is listed; grouping and thumbnail size only change the view.
  // Both live in popovers so opening them never squeezes the asset grid or turns the
  // toolbar into a form.
  m_filterPopup = new PopupWindow{this};
  m_filterPopup->setObjectName("ModelBrowser_FilterPopup");
  m_filterPopup->setAttribute(Qt::WA_StyledBackground);
  auto* filterForm = createPanelForm(nullptr, PanelInsets::Content);
  filterForm->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
  filterForm->setSizeConstraint(QLayout::SetFixedSize);
  // A popover reads best when every field lines up, so the filter fields share a width.
  for (auto* combo : {m_soundCombo, m_tagCombo})
  {
    combo->setMinimumWidth(190);
  }
  filterForm->addRow(tr("Type"), m_typeCombo);
  m_entityUsed = new QCheckBox{tr("Used in map")};
  m_entityUsed->setObjectName("ModelBrowser_EntityUsed");
  filterForm->addRow(m_entityUsed);
  m_entityUsed->hide();
  connect(m_entityUsed, &QCheckBox::toggled, this, [this](const bool used) {
    if (m_entityBrowser)
      m_entityBrowser->view().setHideUnused(used);
    updateFilterSummary();
  });
  filterForm->addRow(tr("Sound"), m_soundCombo);
  filterForm->addRow(tr("Tag"), m_tagCombo);
  auto* metadataRow = new QWidget{};
  auto* metadataLayout = new QHBoxLayout{metadataRow};
  metadataLayout->setContentsMargins(0, 0, 0, 0);
  metadataLayout->setSpacing(6);
  metadataLayout->addWidget(m_usedButton);
  metadataLayout->addWidget(m_unusedButton);
  metadataLayout->addWidget(m_favoritesButton);
  metadataLayout->addStretch(1);
  filterForm->addRow(metadataRow);
  m_filterPopup->setLayout(filterForm);
  m_filterLayout = filterForm;
  m_metadataFilterRow = metadataRow;

  m_viewPopup = new PopupWindow{this};
  m_viewPopup->setObjectName("ModelBrowser_ViewPopup");
  m_viewPopup->setAttribute(Qt::WA_StyledBackground);
  auto* viewForm = createPanelForm(nullptr, PanelInsets::Content);
  viewForm->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
  viewForm->setSizeConstraint(QLayout::SetFixedSize);
  viewForm->addRow(tr("Thumbnail size"), m_iconSizeCombo);
  viewForm->addRow(QString{}, m_groupWadTextures);
  m_entitySort = new QComboBox{};
  m_entitySort->setObjectName("ModelBrowser_EntitySort");
  m_entitySort->addItems({tr("Name"), tr("Usage")});
  viewForm->addRow(tr("Sort"), m_entitySort);
  m_entityGroup = new QCheckBox{tr("Group entities")};
  m_entityGroup->setObjectName("ModelBrowser_EntityGroup");
  viewForm->addRow(m_entityGroup);
  m_viewLayout = viewForm;
  viewForm->setRowVisible(m_entitySort, false);
  viewForm->setRowVisible(m_entityGroup, false);
  connect(m_entitySort, &QComboBox::currentIndexChanged, this, [this](const int index) {
    if (m_entityBrowser)
      m_entityBrowser->view().setSortOrder(
        index == 0 ? mdl::EntityDefinitionSortOrder::Name
                   : mdl::EntityDefinitionSortOrder::Usage);
  });
  connect(m_entityGroup, &QCheckBox::toggled, this, [this](const bool group) {
    if (m_entityBrowser)
      m_entityBrowser->view().setGroup(group);
  });
  m_viewPopup->setLayout(viewForm);

  connect(m_filterButton, &QToolButton::toggled, this, [this](const bool shown) {
    if (!shown)
    {
      m_filterPopup->close();
      return;
    }
    m_viewButton->setChecked(false);
    m_filterPopup->adjustSize();
    m_filterPopup->positionTouchingWidget(m_filterButton);
    m_filterPopup->show();
    m_filterPopup->raise();
  });
  connect(m_viewButton, &QToolButton::toggled, this, [this](const bool shown) {
    if (!shown)
    {
      m_viewPopup->close();
      return;
    }
    m_filterButton->setChecked(false);
    m_viewPopup->adjustSize();
    m_viewPopup->positionTouchingWidget(m_viewButton);
    m_viewPopup->show();
    m_viewPopup->raise();
  });
  connect(m_filterPopup, &PopupWindow::visibilityChanged, this, [this](const bool shown) {
    // Clicking outside (or on the button itself) closes the popover; the button follows.
    const auto blocker = QSignalBlocker{m_filterButton};
    m_filterButton->setChecked(shown);
  });
  connect(m_viewPopup, &PopupWindow::visibilityChanged, this, [this](const bool shown) {
    const auto blocker = QSignalBlocker{m_viewButton};
    m_viewButton->setChecked(shown);
  });

  auto* resultBar = new QWidget{};
  resultBar->setObjectName("ModelBrowser_ResultBar");
  resultBar->setAttribute(Qt::WA_StyledBackground);
  auto* resultLayout = new QHBoxLayout{resultBar};
  setPanelInsets(*resultLayout, PanelInsets::Toolbar);
  resultLayout->setSpacing(6);
  m_resultCount = new QLabel{};
  m_resultCount->setObjectName("ModelBrowser_ResultCount");
  resultLayout->addWidget(m_resultCount);
  resultLayout->addStretch(1);

  auto* assets = new QWidget{};
  auto* assetsLayout = new QVBoxLayout{assets};
  assetsLayout->setContentsMargins(0, 0, 0, 0);
  assetsLayout->setSpacing(0);
  assetsLayout->addWidget(resultBar);
  assetsLayout->addWidget(browser, 1);
  m_contentLayout = assetsLayout;

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
  m_assetSurface = browser;
  layout->addWidget(controls);
  layout->addWidget(splitter, 1);
  setLayout(layout);

  m_fileSystemWatcher = new QFileSystemWatcher{this};
  m_rescanTimer = new QTimer{this};
  m_rescanTimer->setSingleShot(true);
}

void ModelBrowser::showEntityBrowser()
{
  if (m_selectionOnly)
    return;
  setEntityMode(true);
  m_searchBox->setFocus(Qt::ShortcutFocusReason);
}

bool ModelBrowser::isShowingEntities() const
{
  return m_showingEntities;
}

void ModelBrowser::setEntityMode(const bool entities)
{
  if (m_selectionOnly || m_showingEntities == entities)
    return;
  const auto wasEntities = isShowingEntities();
  saveBrowsingState();
  m_showingEntities = entities;
  if (entities && !m_entityBrowser)
  {
    m_entityBrowser = new EntityBrowser{m_appController, *m_document, this};
    m_entityBrowser->setMapActionsEnabled(m_mapActionsEnabled);
    m_contentLayout->addWidget(m_entityBrowser, 1);
    connect(
      m_entityBrowser,
      &EntityBrowser::createEntityRequested,
      this,
      &ModelBrowser::createEntityRequested);
    connect(
      m_entityBrowser,
      &EntityBrowser::contentsChanged,
      this,
      &ModelBrowser::refreshResultCount);
  }
  if (entities)
  {
    auto& view = m_entityBrowser->view();
    view.setSortOrder(
      m_entitySort->currentIndex() == 0 ? mdl::EntityDefinitionSortOrder::Name
                                        : mdl::EntityDefinitionSortOrder::Usage);
    view.setGroup(m_entityGroup->isChecked());
    view.setHideUnused(m_entityUsed->isChecked());
    if (!wasEntities)
      m_searchBox->setText(m_entitySearch);
    view.setFilterText(m_searchBox->text().toStdString());
  }
  else if (wasEntities)
    restoreBrowsingState();
  m_filterButton->setChecked(false);
  m_viewButton->setChecked(false);
  m_assetSurface->setVisible(!entities);
  if (m_entityBrowser)
    m_entityBrowser->setVisible(entities);
  m_entitySettings->setVisible(entities);
  m_entitySettings->setEnabled(m_mapActionsEnabled);
  updateSourceControls();
  m_searchScopeCombo->setVisible(!entities);
  m_filterLayout->setRowVisible(m_typeCombo, !entities);
  m_filterLayout->setRowVisible(m_entityUsed, entities);
  m_viewLayout->setRowVisible(m_entitySort, entities);
  m_viewLayout->setRowVisible(m_entityGroup, entities);
  m_viewLayout->setRowVisible(m_groupWadTextures, !entities);
  if (!entities)
    applyTypeFilter();
  else
    rebuildFolderTree();
  updateFolderEdit();
  updateSearchPlaceholder();
  refreshResultCount();
  updateFilterVisibility();
  updateFilterSummary();
  recordFolderHistory();
  if (entities && !wasEntities && m_entityBrowser->isVisible())
    m_entityBrowser->view().setScrollPosition(m_entityScroll);
  emit entityModeChanged(entities);
}

void ModelBrowser::addViewOption(QWidget* control)
{
  if (!m_toolbarLayout || !control)
  {
    return;
  }
  // Host panels contribute their own view toggles next to the shared view settings.
  m_toolbarLayout->addWidget(control);
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
  m_entitySettings->setEnabled(enabled);
  if (m_entityBrowser)
    m_entityBrowser->setMapActionsEnabled(enabled);
  updateSourceControls();
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
  browser->setTypeFilter(
    type, type == BrowserCellType::Texture ? std::optional{skybox} : std::nullopt);
  browser->m_typeCombo->setCurrentIndex(
    browser->m_typeCombo->findData(skybox ? SkyboxTypeFilter : int(type)));
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
      applyAssetToMap(inspector.asset()->resource);
    });
  refresh();
}

void ModelBrowser::applyAssetToMap(BrowserAsset asset)
{
  if (
    !m_mapActionsEnabled || m_selectionOnly || asset.missing
    || asset.type != BrowserCellType::Texture)
    return;
  auto* target = m_document;
  const auto faces = target->map().selection().allBrushFaces();
  if (!asset.skybox && faces.empty())
    return;
  auto name = asset.skybox ? asset.path.filename().string() : asset.path.stem().string();
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
  else if (!mdl::setBrushFaceAttributes(target->map(), faces, {.materialName = name}))
  {
    QMessageBox::warning(
      this,
      tr("Apply Asset"),
      tr("The selected texture could not be applied to the selected brush faces."));
    return;
  }
  emit actionContextChanged();
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
  connect(
    m_view, &ModelBrowserView::assetApplyRequested, this, &ModelBrowser::applyAssetToMap);
  connect(m_searchBox, &QLineEdit::textChanged, this, [this](const QString& text) {
    if (isShowingEntities())
      m_entityBrowser->view().setFilterText(text.toStdString());
    else if (m_view)
      m_view->setSearchText(text);
    refreshResultCount();
  });

  connect(
    m_searchScopeCombo,
    QOverload<int>::of(&QComboBox::currentIndexChanged),
    this,
    [this](const int) {
      if (m_view)
      {
        m_view->setSearchAllAssets(searchAllAssets());
      }
      updateSearchPlaceholder();
      refreshResultCount();
    });

  connect(m_backButton, &QToolButton::clicked, this, [this]() { navigateHistory(-1); });
  connect(m_forwardButton, &QToolButton::clicked, this, [this]() { navigateHistory(1); });

  connect(m_folderEdit, &QLineEdit::editingFinished, this, [&]() {
    if (!m_folderEdit->isModified())
    {
      showBreadcrumbBar();
      return;
    }

    const auto typedPath = pathFromQString(m_folderEdit->text()).lexically_normal();
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
      const auto entity = current->data(0, EntityFolderRole);
      if (entity.isValid())
      {
        setEntityMode(true);
        return;
      }
      const auto rel = current->data(0, Qt::UserRole).toString();
      setCurrentFolderPath(pathFromQString(rel));
    });

  connect(
    m_view, &ModelBrowserView::folderActivated, this, [&](const QString& folderPath) {
      setCurrentFolderPath(pathFromQString(folderPath));
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
    if (isShowingEntities())
    {
      m_entityBrowser->reload();
      return;
    }
    m_lastWriteTimes.clear();
    m_assetRefreshPending = false;
    if (m_sourceFilter != BrowserSourceFilter::GameFiles)
      refreshLibraryConnection();
    rescanWatchedDirectory();
  });

  connect(m_sourceTabs, &QTabBar::currentChanged, this, [&](const int index) {
    setAssetSource(BrowserSourceFilter(m_sourceTabs->tabData(index).toInt()));
  });

  connect(
    m_typeCombo,
    QOverload<int>::of(&QComboBox::currentIndexChanged),
    this,
    [&](const int index) {
      const auto value = m_typeCombo->itemData(index).toInt();
      if (value == SkyboxTypeFilter)
        setTypeFilter(BrowserCellType::Texture, true);
      else
        setTypeFilter(
          value < 0 ? std::nullopt : std::optional{BrowserCellType(value)},
          value == int(BrowserCellType::Texture) ? std::optional{false} : std::nullopt);
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
    m_view,
    &ModelBrowserView::projectAssetRemoveRequested,
    this,
    &ModelBrowser::removeProjectAsset);

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
          if (m_entityBrowser)
          {
            m_entityBrowser->view().invalidate();
            m_entityBrowser->view().update();
          }
        }
      });
  connectMapObservers();
}

void ModelBrowser::connectMapObservers()
{
  m_mapNotifierConnection.disconnect();
  m_mapNotifierConnection +=
    m_document->documentDidChangeNotifier.connect([this](const auto&...) {
      if (m_usedButton->isChecked() || m_unusedButton->isChecked())
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
  if (m_project)
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

QString ModelBrowser::listingTitle() const
{
  if (!m_project)
    return tr("Game Files");
  if (m_sourceFilter == BrowserSourceFilter::Project)
    return QString::fromStdString(m_project->store().info().name);
  return m_libraryName.isEmpty() ? tr("Asset Library") : m_libraryName;
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
    sep->setProperty("breadcrumbSeparator", true);
    m_breadcrumbLayout->addWidget(sep, 0);
  };

  const auto addCrumb = [&](
                          const QString& title, const std::filesystem::path& folderPath) {
    auto* button = new QToolButton{};
    button->setAutoRaise(true);
    button->setText(title);
    button->setProperty("tbControlRole", "compact");
    connect(button, &QToolButton::clicked, this, [this, folderPath]() {
      setCurrentFolderPath(folderPath);
    });
    m_breadcrumbLayout->addWidget(button, 0);
  };

  if (isShowingEntities())
  {
    auto* root = new QToolButton{};
    root->setText(tr("Entities"));
    root->setAutoRaise(true);
    root->setProperty("tbControlRole", "compact");
    connect(root, &QToolButton::clicked, this, [this] { setEntityMode(true); });
    m_breadcrumbLayout->addWidget(root);
    m_breadcrumbLayout->addStretch(1);
    return;
  }

  // The root crumb names the listing the paths are relative to, so "sprites" reads as
  // assets/sprites instead of looking like a top-level folder.
  const auto rootTitle = m_folderPath.filename().empty()
                           ? listingTitle()
                           : pathAsGenericQString(m_folderPath.filename());
  addCrumb(rootTitle, std::filesystem::path{});

  auto accumulated = std::filesystem::path{};
  for (const auto& part : m_currentFolderPath)
  {
    accumulated /= part;
    addSeparator();
    addCrumb(
      m_folderPath.empty() ? browserFolderDisplayName(accumulated, m_partitionNames)
                           : pathAsGenericQString(part),
      accumulated);
  }

  m_breadcrumbLayout->addStretch(1);
}

void ModelBrowser::showPathEditor()
{
  if (isShowingEntities() || !m_pathStack || !m_folderEdit)
  {
    return;
  }

  auto displayedPath = m_folderPath;
  if (!m_currentFolderPath.empty())
  {
    displayedPath /= m_currentFolderPath;
  }

  auto displayedText = pathAsGenericQString(displayedPath);
  if (displayedText.isEmpty())
  {
    displayedText = QStringLiteral("assets");
  }

  m_folderEdit->setText(displayedText);
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

bool ModelBrowser::searchAllAssets() const
{
  return m_searchScopeCombo && m_searchScopeCombo->currentData().toBool();
}

void ModelBrowser::updateSearchPlaceholder()
{
  if (!m_searchBox)
  {
    return;
  }
  if (isShowingEntities())
  {
    m_searchBox->setPlaceholderText(tr("Search entity types…"));
    return;
  }
  // The placeholder states the active scope, so the box never implies a wider or
  // narrower search than it performs.
  m_searchBox->setPlaceholderText(
    searchAllAssets() ? tr("Search all assets…") : tr("Search this folder…"));
}

bool ModelBrowser::includeDefaultRootsForListing() const
{
  return browserFilterIncludesSource(m_sourceFilter, BrowserSource::GameFiles)
         && !m_typeFilter && !m_usedButton->isChecked() && !m_unusedButton->isChecked()
         && !m_favoritesButton->isChecked()
         && m_tagCombo->currentData().toString().isEmpty();
}

void ModelBrowser::refreshResultCount()
{
  if (!m_resultCount || !m_view)
  {
    return;
  }

  if (isShowingEntities())
  {
    m_resultCount->setToolTip({});
    m_resultCount->setText(
      tr("%1 entity types")
        .arg(qulonglong(m_entityBrowser->view().filteredDefinitions().size())));
    return;
  }
  if (m_unusedButton->isChecked() && !m_projectReferenceError.isEmpty())
  {
    m_resultCount->setText(tr("Reference scan failed"));
    m_resultCount->setToolTip(m_projectReferenceError);
    return;
  }
  m_resultCount->setToolTip({});
  const auto entries = modelBrowserEntries(
    m_folderPath,
    m_view->assets(),
    m_currentFolderPath,
    m_searchBox ? m_searchBox->text() : QString{},
    includeDefaultRootsForListing(),
    searchAllAssets());
  // Folders are listed next to assets, so the count describes everything on screen
  // instead of claiming an empty folder when only subfolders are shown.
  const auto count = entries.size();
  m_resultCount->setText(
    count == 1 ? tr("1 item") : tr("%1 items").arg(qulonglong(count)));
}

void ModelBrowser::updateFilterVisibility()
{
  if (!m_filterLayout || !m_view)
  {
    return;
  }

  if (isShowingEntities())
  {
    m_filterLayout->setRowVisible(m_soundCombo, false);
    m_filterLayout->setRowVisible(m_tagCombo, false);
    m_filterLayout->setRowVisible(m_metadataFilterRow, false);
    return;
  }
  // A filter that cannot change the assets below the current folder only raises the
  // question of what it filters, so each one is kept for the types it can apply to.
  const auto currentFolderAbs =
    m_folderPath.empty() ? m_currentFolderPath : (m_folderPath / m_currentFolderPath);
  const auto& assets = m_view->assets();
  const auto hasType = [&](const BrowserCellType type) {
    return std::ranges::any_of(assets, [&](const BrowserAsset& asset) {
      if (asset.type != type)
      {
        return false;
      }
      if (currentFolderAbs.empty())
      {
        return true;
      }
      const auto& assetPath = asset.catalogPath.empty() ? asset.path : asset.catalogPath;
      return kdl::path_has_prefix(assetPath.parent_path(), currentFolderAbs);
    });
  };

  m_filterLayout->setRowVisible(
    m_soundCombo,
    hasType(BrowserCellType::Sound) || m_waveKind != inspection::WaveKind::All);
  m_filterLayout->setRowVisible(m_tagCombo, m_tagCombo->count() > 1);
  if (m_metadataFilterRow)
  {
    // The controls stay in reach when they cannot act right now; disabling them keeps an
    // already active filter explainable instead of hiding it.
    m_filterLayout->setRowVisible(m_metadataFilterRow, bool(m_project));
  }
  // A filter change can hide its own row; keep the open popover anchored to its button.
  if (m_filterPopup && m_filterPopup->isVisible())
  {
    m_filterPopup->positionTouchingWidget(m_filterButton);
  }
}

void ModelBrowser::updateFilterSummary()
{
  if (!m_filterButton)
  {
    return;
  }

  if (isShowingEntities())
  {
    m_filterButton->setText(
      m_entityUsed->isChecked() ? tr("Filters · %1").arg(1) : tr("Filters"));
    return;
  }
  auto active = 0;
  if (m_waveKind != inspection::WaveKind::All)
  {
    ++active;
  }
  if (
    (m_usedButton && m_usedButton->isChecked())
    || (m_unusedButton && m_unusedButton->isChecked()))
  {
    ++active;
  }
  if (m_favoritesButton && m_favoritesButton->isChecked())
  {
    ++active;
  }
  if (m_tagCombo && !m_tagCombo->currentData().toString().isEmpty())
  {
    ++active;
  }
  m_filterButton->setText(active > 0 ? tr("Filters · %1").arg(active) : tr("Filters"));
}

void ModelBrowser::recordFolderHistory()
{
  if (m_navigatingHistory)
  {
    return;
  }

  // Navigating somewhere new drops the forward branch, like a browser history.
  m_folderHistory.resize(size_t(m_folderHistoryIndex + 1));
  const auto location = BrowserLocation{m_currentFolderPath, m_showingEntities};
  if (m_folderHistory.empty() || m_folderHistory.back() != location)
  {
    m_folderHistory.push_back(location);
    m_folderHistoryIndex = std::ptrdiff_t(m_folderHistory.size()) - 1;
  }
  updateHistoryButtons();
}

void ModelBrowser::updateHistoryButtons()
{
  if (!m_backButton || !m_forwardButton)
  {
    return;
  }

  m_backButton->setEnabled(m_folderHistoryIndex > 0);
  m_forwardButton->setEnabled(
    m_folderHistoryIndex + 1 < std::ptrdiff_t(m_folderHistory.size()));
}

void ModelBrowser::navigateHistory(const int offset)
{
  const auto target = m_folderHistoryIndex + offset;
  if (target < 0 || target >= std::ptrdiff_t(m_folderHistory.size()))
  {
    return;
  }

  m_folderHistoryIndex = target;
  m_navigatingHistory = true;
  const auto location = m_folderHistory[size_t(target)];
  if (location.entities)
    setEntityMode(true);
  else
    setCurrentFolderPath(location.folder);
  m_navigatingHistory = false;
  updateHistoryButtons();
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
  setEntityMode(false);
  folderPath = folderPath.lexically_normal();
  if (m_folderPath == folderPath)
  {
    return;
  }

  m_folderPath = std::move(folderPath);
  m_currentFolderPath.clear();
  m_folderHistory.clear();
  m_folderHistoryIndex = -1;
  recordFolderHistory();
  updateFolderEdit();

  markAssetsDirty();
}

void ModelBrowser::setCurrentFolderPath(std::filesystem::path currentFolderPath)
{
  const auto leavingEntities = isShowingEntities();
  if (leavingEntities)
  {
    const auto navigating = m_navigatingHistory;
    m_navigatingHistory = true;
    setEntityMode(false);
    m_navigatingHistory = navigating;
  }
  currentFolderPath = currentFolderPath.lexically_normal();
  if (currentFolderPath == std::filesystem::path{"."})
  {
    currentFolderPath.clear();
  }

  if (m_currentFolderPath == currentFolderPath)
  {
    if (leavingEntities)
      recordFolderHistory();
    return;
  }

  m_currentFolderPath = std::move(currentFolderPath);
  recordFolderHistory();
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
  refreshResultCount();
  updateFilterVisibility();
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
  const auto catalog = browsableProjectAssets(m_project->store());
  if (catalog.is_error())
    return std::nullopt;
  const auto keys = assetPreviewKeys(m_project->store());
  const auto thumbnails = m_project->store().mapThumbnailHashes();
  auto assets = std::vector<BrowserAsset>{};
  for (const auto& asset : catalog.value())
  {
    const auto path = pathFromQString(QString::fromStdString(asset.publishPath));
    const auto type = catalogCellType(asset.type, path);
    if (type == BrowserCellType::Folder)
      continue;
    auto entry = BrowserAsset{type, path};
    if (type != BrowserCellType::Material)
    {
      const auto file = m_project->assetFile(asset);
      if (file.is_error())
        continue;
      entry.absolutePath = file.value();
    }
    entry.source = BrowserSource::Project;
    entry.skybox = asset.type == project::AssetType::Skybox;
    entry.projectId = m_project->store().info().id;
    entry.assetId = asset.id;
    entry.revision = asset.revision;
    entry.displayName = asset.displayName;
    entry.catalogPath = pathFromQString(QString::fromStdString(asset.virtualPath));
    if (keys.is_success())
      if (const auto key = keys.value().find(asset.id); key != keys.value().end())
        entry.previewIdentity = key->second.toStdString();
    if (asset.type == project::AssetType::Texture && !asset.library.sourceMember.empty())
      entry.textureCollection =
        pathFromQString(QString::fromStdString(asset.library.sourceContainer));
    if (asset.type == project::AssetType::Map && thumbnails.is_success())
      if (const auto thumbnail = thumbnails.value().find(asset.id);
          thumbnail != thumbnails.value().end())
        entry.previewIdentity += "/thumbnail/" + thumbnail->second;
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
  const auto connections = m_project->libraries();
  if (connections.is_error())
    return std::nullopt;

  // Update state is project state, so it is queried once per scan instead of per entry.
  auto updates = std::vector<project::LibraryUpdate>{};
  if (auto result = m_project->checkLibraryUpdates(); result.is_success())
    updates = std::move(result).value();

  const auto linked = m_project->store().libraryLinkedAssets();
  auto updatesByAsset = std::unordered_map<std::string, project::LibraryUpdateState>{};
  for (const auto& update : updates)
    updatesByAsset.try_emplace(update.projectAssetId, update.state);
  auto assets = std::vector<BrowserAsset>{};
  for (const auto& connection : connections.value())
  {
    if (!connection.available)
      continue;
    const auto source = openLocalAssetLibrary(connection.info.path);
    if (!source || source->libraryId() != connection.info.id)
      continue;
    const auto catalog = source->assets();
    if (catalog.is_error())
      continue;
    const auto keys = assetPreviewKeys(*source);
    auto capturedAssets = std::unordered_map<std::string, project::Asset>{};
    if (linked.is_success())
      for (const auto& asset : linked.value())
        if (asset.library.libraryId == connection.info.id)
          capturedAssets.try_emplace(asset.library.libraryAssetId, asset);

    for (const auto& asset : catalog.value())
    {
      const auto type = catalogCellType(
        asset.type, pathFromQString(QString::fromStdString(asset.virtualPath)));
      if (type == BrowserCellType::Folder)
        continue;
      auto entry = BrowserAsset{};
      entry.type = type;
      // View selection and preview caches use path keys. Scope those keys by catalog
      // so identically organized user and built-in assets cannot select each other.
      // catalogPath below retains the folder names shown to the user.
      entry.path = pathFromQString(QString::fromStdString(connection.info.id))
                   / pathFromQString(QString::fromStdString(asset.virtualPath));
      entry.source = BrowserSource::AssetLibrary;
      // A non-empty project ID marks a catalog-backed entry, which is what makes the
      // browser show display names and search them.
      entry.projectId = connection.info.id;
      entry.libraryId = connection.info.id;
      entry.libraryAssetId = asset.id;
      entry.libraryRevision = asset.revision;
      entry.libraryContentHash = asset.contentHash;
      entry.displayName = asset.displayName;
      entry.catalogPath =
        pathFromQString(QString::fromStdString(asset.group)) / entry.path.filename();
      if (keys.is_success())
        if (const auto key = keys.value().find(asset.id); key != keys.value().end())
          entry.previewIdentity = key->second.toStdString();
      if (asset.type == project::AssetType::Texture && !asset.sourceMember.empty())
        entry.textureCollection =
          pathFromQString(QString::fromStdString(asset.sourceContainer));
      entry.missing = asset.missing;
      entry.skybox = asset.type == project::AssetType::Skybox;
      if (const auto captured = capturedAssets.find(asset.id);
          captured != capturedAssets.end())
      {
        const auto& projectAsset = captured->second;
        entry.assetId = projectAsset.id;
        entry.projectAssetId = projectAsset.id;
        entry.projectRevision = projectAsset.revision;
        entry.projectPublishPath =
          pathFromQString(QString::fromStdString(projectAsset.publishPath));

        const auto update = updatesByAsset.find(projectAsset.id);
        entry.projectUpdateAvailable =
          update != updatesByAsset.end()
          && (update->second == project::LibraryUpdateState::NewVersion || update->second == project::LibraryUpdateState::DependenciesChanged);
      }
      assets.push_back(std::move(entry));
    }

    // Models, textures and terrain materials are resolved when their previews are
    // visible.
    auto fileIds = std::vector<std::string>{};
    for (const auto& entry : assets)
      if (entry.libraryId == connection.info.id && (
      entry.type != BrowserCellType::Texture && entry.type != BrowserCellType::Material
      && entry.type != BrowserCellType::Model))
        fileIds.push_back(entry.libraryAssetId);

    if (const auto files = m_project->libraryAssetFiles(connection.info.id, fileIds);
        files.is_success())
    {
      for (auto& entry : assets)
      {
        if (entry.libraryId != connection.info.id || entry.libraryAssetId.empty())
          continue;
        if (const auto file = files.value().find(entry.libraryAssetId);
            file != files.value().end())
          entry.absolutePath = file->second;
      }
    }
  }

  return assets;
}

std::optional<std::vector<BrowserAsset>> ModelBrowser::scanAssets()
{
  m_partitionNames.clear();
  if (m_project)
    if (const auto connections = m_project->libraries(); connections.is_success())
      for (const auto& connection : connections.value())
        if (connection.available && connection.info.id != BuiltinAssetLibraryId)
          if (const auto source = openLocalAssetLibrary(connection.info.path))
            if (const auto names = source->partitionNames(); names.is_success())
              for (const auto& [root, label] : names.value())
                m_partitionNames.insert_or_assign(root, label);
  const auto setTitle = [this](const int type, const char* root) {
    const auto index = m_typeCombo->findData(type);
    if (index >= 0)
      m_typeCombo->setItemText(index, browserFolderDisplayName(root, m_partitionNames));
  };
  setTitle(int(BrowserCellType::Map), "maps");
  setTitle(int(BrowserCellType::Texture), "textures");
  setTitle(int(BrowserCellType::Material), "materials");
  setTitle(int(BrowserCellType::Model), "models");
  setTitle(int(BrowserCellType::Sprite), "sprites");
  setTitle(int(BrowserCellType::Sound), "sound");
  setTitle(SkyboxTypeFilter, "skybox");
  setTitle(int(BrowserCellType::Prefab), "prefabs");
  m_view->setPartitionNames(m_partitionNames);

  switch (m_sourceFilter)
  {
  case BrowserSourceFilter::Project:
    return scanProjectAssets();
  case BrowserSourceFilter::AssetLibrary:
    return scanLibraryAssets();
  case BrowserSourceFilter::GameFiles:
    return m_project ? std::nullopt : scanGameAssets();
  }
  return std::nullopt;
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
  setEntityMode(false);
  if (m_sourceFilter == source)
  {
    return;
  }
  saveBrowsingState();
  if (m_sourceFilter == BrowserSourceFilter::Project)
  {
    m_projectUsedInMap = m_usedButton->isChecked();
    m_projectUnused = m_unusedButton->isChecked();
  }
  m_sourceFilter = source;
  {
    const auto blocker = QSignalBlocker{m_usedButton};
    const auto unusedBlocker = QSignalBlocker{m_unusedButton};
    m_unusedButton->setChecked(source == BrowserSourceFilter::Project && m_projectUnused);
    m_usedButton->setChecked(
      source == BrowserSourceFilter::Project && m_projectUsedInMap);
  }
  m_folderPath.clear();
  m_currentFolderPath.clear();
  restoreBrowsingState();
  // Another source is another listing; folders from the previous one are not reachable.
  m_folderHistory.clear();
  m_folderHistoryIndex = -1;
  recordFolderHistory();
  updateFolderEdit();
  refreshLibraryConnection();
  updateSourceControls();
  m_assetRefreshPending = true;
  scheduleAssetRefresh();
}

void ModelBrowser::setTypeFilter(
  const std::optional<BrowserCellType> type, const std::optional<bool> skybox)
{
  if (m_typeFilter == type && m_skyboxFilter == skybox)
  {
    return;
  }
  saveBrowsingState();
  m_typeFilter = type;
  m_skyboxFilter = skybox;
  restoreBrowsingState();
  updateFolderEdit();
  applyTypeFilter();
}

int ModelBrowser::browsingStateKey() const
{
  // Skyboxes and ordinary textures retain independent folder/search/scroll state.
  return int(m_sourceFilter) * 16
    + (m_skyboxFilter.value_or(false) ? 15 : m_typeFilter ? int(*m_typeFilter) + 1 : 0);
}

void ModelBrowser::saveBrowsingState()
{
  if (isShowingEntities())
  {
    m_entitySearch = m_searchBox->text();
    m_entityScroll = m_entityBrowser->findChild<QScrollBar*>()->value();
    return;
  }
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
  m_view->setIncludeDefaultRoots(includeDefaultRootsForListing());
  m_view->setAssets(m_folderPath, visibleAssets());
  m_view->setCurrentFolderPath(m_currentFolderPath);
  m_view->setSearchAllAssets(searchAllAssets());
  if (!m_assetRefreshPending && m_pendingScrollRestore)
  {
    m_view->setScrollPosition(*m_pendingScrollRestore);
    m_pendingScrollRestore.reset();
  }
  refreshResultCount();
  updateFilterVisibility();
  updateFilterSummary();
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
  if (m_unusedButton->isChecked())
    std::erase_if(assets, [&](const auto& asset) {
      return !m_projectReferences || asset.type == BrowserCellType::Map
             || m_projectReferences->contains(asset.assetId);
    });
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
  for (auto& asset : assets)
    asset.catalogPath = browserAssetDisplayPath(asset, m_groupWadTextures->isChecked());
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
  setAssetSource(BrowserSourceFilter::Project);
  if (!m_selectionOnly)
    m_typeCombo->setCurrentIndex(0);
  m_usedButton->setChecked(false);
  m_unusedButton->setChecked(false);
  m_favoritesButton->setChecked(false);
  m_tagCombo->setCurrentIndex(0);
  m_searchBox->clear();
  markAssetsDirty();
  ensureAssetsLoaded();
  const auto found = std::ranges::find(m_assets, assetId, &BrowserAsset::assetId);
  if (found == m_assets.end())
    return;
  setCurrentFolderPath(
    browserAssetDisplayPath(*found, m_groupWadTextures->isChecked()).parent_path());
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
        tr("Show in Asset Library"),
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
  m_projectReferences.reset();
  m_projectReferenceError.clear();
  if (m_unusedButton->isChecked() && m_project)
  {
    auto references = projectAssetMapReferences(*m_project);
    if (references.is_success())
      m_projectReferences = std::move(references).value();
    else
      m_projectReferenceError =
        QString::fromStdString(std::get<Error>(references.error()).msg);
  }
  m_unusedButton->setToolTip(
    m_projectReferenceError.isEmpty()
      ? tr("Assets unused by all project maps, including unsaved edits and indirect "
           "dependencies. Maps are excluded.")
      : tr("Cannot verify unused assets: %1").arg(m_projectReferenceError));
  m_usedButton->setToolTip(
    !id ? tr("An active project map is required")
    : !m_usageError.isEmpty()
      ? tr("Cannot scan current map: %1").arg(m_usageError)
      : tr("Explicit MAP resource references, including unsaved edits. Captured "
           "resources match by identity."));
}

void ModelBrowser::updateSourceControls()
{
  const auto projectMode = m_sourceFilter == BrowserSourceFilter::Project;
  m_sourceTabs->setVisible(bool(m_project) && !isShowingEntities());
  m_unusedButton->setVisible(projectMode);
  m_unusedButton->setEnabled(projectMode);
  m_usedButton->setVisible(projectMode);
  m_usedButton->setEnabled(projectMode && m_mapActionsEnabled);
  if (m_sourceTabs)
  {
    const QSignalBlocker blocker{m_sourceTabs};
    for (auto index = 0; index < m_sourceTabs->count(); ++index)
    {
      if (BrowserSourceFilter(m_sourceTabs->tabData(index).toInt()) == m_sourceFilter)
      {
        m_sourceTabs->setCurrentIndex(index);
        break;
      }
    }
  }
  // Project prefabs are saved in the project; standalone prefabs use the configured path.
  if (m_savePrefabButton)
  {
    m_savePrefabButton->setVisible(
      !isShowingEntities() && !m_selectionOnly
      && (!m_project || m_sourceFilter != BrowserSourceFilter::AssetLibrary));
  }
}

void ModelBrowser::refreshLibraryConnection()
{
  m_libraryName = tr("No asset library");
  m_libraryError.clear();
  m_libraryId.clear();
  if (!m_project)
    return;

  const auto libraries = m_project->libraries();
  if (libraries.is_success() && !libraries.value().empty())
  {
    const auto& library = libraries.value().front();
    const auto name = QString::fromStdString(library.info.name);
    m_libraryName = libraries.value().size() > 1 ? tr("Asset Library")
                    : library.available          ? name
                                                 : tr("%1 (unavailable)").arg(name);
    m_libraryId = library.info.id;
    m_libraryError = QString::fromStdString(library.error);
  }
  rebuildBreadcrumbBar();
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

void ModelBrowser::removeProjectAsset(const QString& projectAssetId)
{
  if (!m_project)
    return;
  const auto asset = m_project->store().findAsset(projectAssetId.toStdString());
  if (asset.is_error() || !asset.value())
    return;
  if (
    QMessageBox::question(
      this,
      tr("Remove from Project"),
      tr("Remove \"%1\" from this project? The asset library is kept.")
        .arg(QString::fromStdString(asset.value()->displayName)),
      QMessageBox::Yes | QMessageBox::Cancel,
      QMessageBox::Cancel)
    != QMessageBox::Yes)
    return;
  // Recheck live map contents after confirmation; filtering is only a cached listing.
  const auto removed = m_project->removeAsset(projectAssetId.toStdString());
  if (removed.is_error())
    QMessageBox::warning(
      this,
      tr("Remove from Project"),
      QString::fromStdString(std::get<Error>(removed.error()).msg));
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
    refreshResultCount();
    updateFilterVisibility();
    updateFilterSummary();
    return;
  }

  m_assets = std::move(*nextAssets);
  m_view->setPartitionNames(m_partitionNames);
  if (m_project)
    loadBrowserAssetMetadata(m_assets, *m_project);
  refreshMetadataFilters();
  m_lastWriteTimes = assetLastWriteTimes(m_assets);

  rebuildFolderTree();
  m_view->setIncludeDefaultRoots(includeDefaultRootsForListing());
  m_view->setShowSourceMarkers(m_sourceFilter == BrowserSourceFilter::AssetLibrary);
  m_view->setAssets(m_folderPath, visibleAssets());
  m_view->setCurrentFolderPath(m_currentFolderPath);
  m_view->setSearchAllAssets(searchAllAssets());
  m_view->setScrollPosition(scroll);
  m_pendingScrollRestore.reset();
  refreshResultCount();
  updateFilterVisibility();
  updateFilterSummary();
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

  const auto rootTitle = m_folderPath.filename().empty()
                           ? listingTitle()
                           : pathAsGenericQString(m_folderPath.filename());
  auto* rootItem = new QTreeWidgetItem{m_folderTree, QStringList{rootTitle}};
  rootItem->setData(0, Qt::UserRole, QString{});
  rootItem->setIcon(0, folderIcon);
  m_folderTreeItems.emplace(std::filesystem::path{}, rootItem);

  if (
    m_folderPath.empty() && !m_typeFilter && !m_usedButton->isChecked()
    && !m_unusedButton->isChecked() && !m_favoritesButton->isChecked()
    && m_tagCombo->currentData().toString().isEmpty())
  {
    for (const auto& [type, rootPath] : assetBrowserRoots())
    {
      unused(type);
      if (rootPath.empty() || m_folderTreeItems.contains(rootPath))
      {
        continue;
      }

      auto* item = new QTreeWidgetItem{
        rootItem, QStringList{browserFolderDisplayName(rootPath, m_partitionNames)}};
      item->setData(0, Qt::UserRole, pathAsGenericQString(rootPath));
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

      const auto title = m_folderPath.empty()
                           ? browserFolderDisplayName(accumulatedPath, m_partitionNames)
                           : pathAsGenericQString(part);
      auto* item = new QTreeWidgetItem{parentItem, QStringList{title}};
      item->setData(0, Qt::UserRole, pathAsGenericQString(accumulatedPath));
      item->setIcon(0, folderIcon);
      parentItem = item;
      m_folderTreeItems.emplace(accumulatedPath, item);
    }
  }

  m_folderTree->expandItem(rootItem);
  if (!m_selectionOnly)
  {
    auto* entityRoot = new QTreeWidgetItem{m_folderTree, {tr("Entities")}};
    entityRoot->setIcon(0, folderIcon);
    entityRoot->setData(0, EntityFolderRole, true);
    if (isShowingEntities())
    {
      m_folderTree->setCurrentItem(entityRoot);
      return;
    }
  }

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

  // Project and library changes notify through ProjectSession instead of a game
  // directory.
  if (m_project)
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
    m_document->map().reloadEntityModels(entityModelAssetPaths(m_assets));
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
  m_view->setIncludeDefaultRoots(includeDefaultRootsForListing());
  m_view->setShowSourceMarkers(m_sourceFilter == BrowserSourceFilter::AssetLibrary);
  m_view->setAssets(m_folderPath, visibleAssets());
  m_view->setCurrentFolderPath(m_currentFolderPath);
  refreshResultCount();
  updateFilterVisibility();
  updateFilterSummary();
}

} // namespace tb::ui
