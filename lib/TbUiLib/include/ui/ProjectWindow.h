/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <QMainWindow>

#include "base/Result.h"
#include "project/ProjectAsset.h"

#include <memory>
#include <optional>
#include <string>

class QTreeWidget;
class QLineEdit;
class QCloseEvent;
class QMenu;
class QTabBar;
class QStackedWidget;
class QAction;
class QTimer;
class QToolBar;
class QToolButton;

namespace tb::ui
{
class AppController;
class AssetLibraryWatcher;
class MapDocument;
class MapCommandSurface;
class MapWindow;
class ProjectSession;

class ProjectWindow : public QMainWindow
{
  AppController& m_appController;
  std::shared_ptr<ProjectSession> m_session;
  QTreeWidget* m_assets = nullptr;
  QTreeWidget* m_history = nullptr;
  QLineEdit* m_search = nullptr;
  QTabBar* m_mapTabs = nullptr;
  QToolButton* m_reopenMaps = nullptr;
  QMenu* m_reopenMapsMenu = nullptr;
  bool m_mapTabsRefreshScheduled = false;
  bool m_closing = false;
  AssetLibraryWatcher* m_libraryWatcher = nullptr;
  QStackedWidget* m_maps = nullptr;
  std::unique_ptr<MapCommandSurface> m_commands;
  QAction* m_saveProject = nullptr;
  QToolBar* m_mapToolbar = nullptr;
  QTimer* m_autosaveTimer = nullptr;

public:
  ProjectWindow(AppController& appController, std::shared_ptr<ProjectSession> session);
  ~ProjectWindow() override;
  const std::shared_ptr<ProjectSession>& session() const;
  MapWindow* activeMapWindow() const;

protected:
  void closeEvent(QCloseEvent* event) override;

private:
  void activateMapTab(MapWindow* window);
  void refreshCommands();
  void refresh();
  void refreshTitle();
  /*
   * The map tab strip is the project workspace: a tab is a live editing view, not the
   * document. Closing a tab destroys the view and keeps the document and its
   * modifications in the session, so saving the project still writes them and "Reopen
   * map" restores the same editing state.
   */
  void refreshMapTabs();
  void scheduleMapTabsRefresh();
  void openMapTab(const std::string& assetId);
  MapWindow* mapWindowFor(const MapDocument& document) const;
  void importFiles();
  Result<bool> prepareImport(
    project::AssetImport& asset,
    const std::optional<project::Asset>& previous = std::nullopt);
  void newMap();
  void openSelected();
  void renameSelected();
  void moveSelected();
  void renamePublishedPath();
  void reimportSelected();
  void exportSelected();
  void checkResources(std::optional<std::string> assetId = std::nullopt);
  void exportCompileSnapshot();
  void attachAssetLibrary();
  void relinkAssetLibrary();
  void captureMapResources();
  void checkLibraryUpdates(bool rescan);
  // Shows the update review when a captured resource has a newer library revision.
  void maybePromptForLibraryUpdates();
  void createCheckpoint();
  void compareCheckpoint();
  void restoreCheckpoint();
  void cleanUnusedRevisions();
  void autosaveProject();
  bool save();
  std::optional<std::string> selectedAsset() const;
  bool report(const Result<void>& result);
};
} // namespace tb::ui
