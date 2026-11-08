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

#pragma once

#include <QPointer>
#include <QWidget>

#include "base/NotifierConnection.h"
#include "project/LibrarySync.h"
#include "ui/AssetBrowserModel.h"
#include "ui/AssetInspection.h"
#include "ui/ModelBrowserView.h"
#include "ui/ProjectResourceAudit.h"

#include "kd/path_hash.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

class QAbstractButton;
class QCheckBox;
class QComboBox;
class QEvent;
class QFileSystemWatcher;
class QFormLayout;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QScrollBar;
class QShowEvent;
class QStackedWidget;
class QToolButton;
class QTabBar;
class QTreeWidget;
class QTreeWidgetItem;
class QTimer;

namespace tb::ui
{
class AppController;
class AssetInspector;
class EntityBrowser;
class AssetLibraryWindow;
class MapDocument;
class PopupWindow;
class ProjectSession;

class ModelBrowser : public QWidget
{
  Q_OBJECT
private:
  MapDocument* m_document;
  AppController& m_appController;
  EntityBrowser* m_entityBrowser = nullptr;
  QWidget* m_assetSurface = nullptr;
  QWidget* m_fileControls = nullptr;
  std::shared_ptr<ProjectSession> m_project;

  QStackedWidget* m_pathStack = nullptr;
  QWidget* m_breadcrumbBar = nullptr;
  QHBoxLayout* m_breadcrumbLayout = nullptr;
  QHBoxLayout* m_toolbarLayout = nullptr;
  QToolButton* m_backButton = nullptr;
  QToolButton* m_forwardButton = nullptr;
  QToolButton* m_savePrefabButton = nullptr;
  QToolButton* m_reloadButton = nullptr;
  QToolButton* m_filterButton = nullptr;
  QToolButton* m_viewButton = nullptr;
  PopupWindow* m_filterPopup = nullptr;
  PopupWindow* m_viewPopup = nullptr;
  QWidget* m_metadataFilterRow = nullptr;
  QFormLayout* m_filterLayout = nullptr;
  QLabel* m_resultCount = nullptr;
  QComboBox* m_searchScopeCombo = nullptr;
  QTabBar* m_sourceTabs = nullptr;
  QComboBox* m_typeCombo = nullptr;
  QString m_libraryName;
  QComboBox* m_iconSizeCombo = nullptr;
  QComboBox* m_soundCombo = nullptr;
  QCheckBox* m_groupWadTextures = nullptr;
  inspection::WaveKind m_waveKind = inspection::WaveKind::All;
  mutable std::unordered_map<std::string, std::optional<inspection::WaveInfo>>
    m_waveInfoCache;
  QToolButton* m_usedButton = nullptr;
  bool m_projectUsedInMap = false;
  QToolButton* m_unusedButton = nullptr;
  bool m_projectUnused = false;
  std::optional<std::map<std::string, std::vector<std::string>>> m_projectReferences;
  QString m_projectReferenceError;
  QToolButton* m_favoritesButton = nullptr;
  QComboBox* m_tagCombo = nullptr;
  QPointer<AssetLibraryWindow> m_libraryWindow;
  ProjectResourceAudit m_usage;
  QString m_usageError;
  QLineEdit* m_folderEdit = nullptr;
  QLineEdit* m_searchBox = nullptr;

  QScrollBar* m_scrollBar = nullptr;
  ModelBrowserView* m_view = nullptr;
  QTreeWidget* m_folderTree = nullptr;

  QFileSystemWatcher* m_fileSystemWatcher = nullptr;
  QTimer* m_rescanTimer = nullptr;
  QTimer* m_usageRefreshTimer = nullptr;

  BrowserSourceFilter m_sourceFilter = BrowserSourceFilter::GameFiles;
  // The unified listing mixes maps, textures, models and sounds; a type filter narrows it
  // without changing which sources are listed.
  std::optional<BrowserCellType> m_typeFilter;
  std::string m_libraryId;
  std::vector<project::LibraryUpdate> m_libraryUpdates;
  QString m_libraryError;
  struct BrowsingState
  {
    std::filesystem::path folder;
    QString search;
    int scroll = 0;
  };
  std::unordered_map<int, BrowsingState> m_browsingStates;
  std::optional<int> m_pendingScrollRestore;

  std::filesystem::path m_folderPath;
  std::filesystem::path m_currentFolderPath;
  // Folder navigation history behind the back and forward buttons. The breadcrumb and
  // the folder tree replace the old ".." card, so returning to a folder needs its own
  // affordance.
  std::vector<std::filesystem::path> m_folderHistory;
  std::ptrdiff_t m_folderHistoryIndex = -1;
  bool m_navigatingHistory = false;
  std::vector<BrowserAsset> m_assets;
  AssetWriteTimes m_lastWriteTimes;
  std::unordered_map<std::filesystem::path, QTreeWidgetItem*, kdl::path_hash>
    m_folderTreeItems;

  NotifierConnection m_notifierConnection;
  NotifierConnection m_mapNotifierConnection;
  bool m_assetRefreshPending = true;
  bool m_mapActionsEnabled = true;
  bool m_selectionOnly = false;
  std::optional<bool> m_skyboxFilter;
  bool m_assetRefreshQueued = false;
  bool m_deferAssetRefreshAfterDocumentLoad = false;
  int m_assetRefreshDeferralGeneration = 0;

public:
  static Result<std::optional<std::string>> chooseAssetPath(
    AppController& app,
    MapDocument& document,
    BrowserCellType type,
    QWidget* parent,
    const std::function<bool()>& targetValid,
    bool skybox = false);
  ModelBrowser(
    AppController& appController,
    MapDocument& document,
    QWidget* parent = nullptr,
    std::shared_ptr<ProjectSession> project = nullptr);
  void setDocument(MapDocument& document);
  void showEntityBrowser();
  bool isShowingEntities() const;
  void addViewOption(QWidget* control);
  void bindInspector(AssetInspector& inspector, bool mapActions);
  void setMapActionsEnabled(bool enabled);
  void revealProjectAsset(const std::string& assetId);
  ~ModelBrowser() override;

signals:
  void actionContextChanged();
  void entityModeChanged(bool entities);
  void createEntityRequested(const QString& classname);

protected:
  bool eventFilter(QObject* obj, QEvent* event) override;

private:
  void showEvent(QShowEvent* event) override;
  void setEntityMode(bool entities);

  void previewAsset(AppController& appController, const QString& path);
  void applyAssetToMap(BrowserAsset asset);
  void createGui(AppController& appController);
  void bindEvents(AppController& appController);
  void connectObservers();
  void connectMapObservers();

  QString listingTitle() const;
  void rebuildBreadcrumbBar();
  bool searchAllAssets() const;
  void updateSearchPlaceholder();
  void refreshResultCount();
  bool includeDefaultRootsForListing() const;
  void updateFilterVisibility();
  void updateFilterSummary();
  void recordFolderHistory();
  void updateHistoryButtons();
  void navigateHistory(int offset);
  void showPathEditor();
  void showBreadcrumbBar();
  void updateFolderEdit();
  void updateSavePrefabButton();
  void updateIconSizeCombo();
  void saveSelectionAsPrefab();
  void renamePrefab(std::filesystem::path prefabPath);
  void deletePrefab(std::filesystem::path prefabPath);
  void removePrefabFromBrowser(const std::filesystem::path& prefabPath);
  void setFolderPath(std::filesystem::path folderPath);
  void setCurrentFolderPath(std::filesystem::path currentFolderPath);
  void markAssetsDirty();
  void ensureAssetsLoaded();
  void scheduleAssetRefresh();
  void scheduleDeferredAssetRefreshAfterDocumentLoad();
  std::optional<std::vector<BrowserAsset>> scanAssets();
  std::optional<std::vector<BrowserAsset>> scanGameAssets();
  std::optional<std::vector<BrowserAsset>> scanProjectAssets() const;
  std::optional<std::vector<BrowserAsset>> scanLibraryAssets() const;
  void setAssetSource(BrowserSourceFilter source);
  void setTypeFilter(
    std::optional<BrowserCellType> type, std::optional<bool> skybox = std::nullopt);
  int browsingStateKey() const;
  void saveBrowsingState();
  void restoreBrowsingState();
  void applyTypeFilter();
  std::vector<BrowserAsset> visibleAssets() const;
  std::optional<inspection::WaveInfo> waveInfoFor(const BrowserAsset& asset) const;
  void updateSourceControls();
  void refreshLibraryConnection();
  void captureLibraryAsset(const QString& libraryId, const QString& libraryAssetId);
  void updateProjectAsset(const QString& projectAssetId);
  void findProjectAssetUses(const QString& projectAssetId);
  void removeProjectAsset(const QString& projectAssetId);
  void reloadModels();
  void refreshUsage();
  void refreshMetadataFilters();
  void showInAssetLibrary(const BrowserAsset& asset);
  void rebuildFolderTree();

  void documentWasLoaded();
  void modsDidChange();

  void setWatchedDirectory();
  void scheduleRescan();
  void rescanWatchedDirectory();
};

} // namespace tb::ui
