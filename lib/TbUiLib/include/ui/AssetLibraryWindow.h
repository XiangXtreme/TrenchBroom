/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom, licensed under GPL version 3 or later.
*/
#pragma once

#include <QDialog>
#include <QString>

#include "project/ProjectAsset.h"
#include "ui/AssetBrowserModel.h"

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

class QComboBox;
class QDragEnterEvent;
class QDropEvent;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QImage;
class QProgressBar;
class QPushButton;
class QTimer;
class QSlider;
class QToolButton;

namespace tb::ui
{
class AssetInspector;
class AssetLibraryItemDelegate;
class AssetLibraryListView;
class AssetLibrarySource;
class ProjectSession;

/**
 * The application-wide asset library browser.
 *
 * Mirrors the layout the project settled on: the library identity and its actions in a
 * header bar, categories and smart filters on the left, an asset grid in the middle and
 * an inspector on the right. Everything is read through AssetLibrarySource, so a remote
 * library can replace the local folder without touching this window.
 */
class AssetLibraryWindow : public QDialog
{
  Q_OBJECT
  std::unique_ptr<AssetLibrarySource> m_source;
  std::vector<project::LibraryAsset> m_assets;
  std::set<std::string> m_favorites;
  std::map<std::string, BrowserAssetMetadata> m_metadata;
  std::weak_ptr<ProjectSession> m_project;
  QMetaObject::Connection m_projectConnection;
  QMetaObject::Connection m_mapConnection;
  QTimer* m_usageTimer = nullptr;
  std::optional<std::string> m_projectAssetId;
  std::optional<std::string> m_selectedId;
  QString m_filter = QStringLiteral("all");
  QString m_search;
  bool m_listMode = false;

  QLabel* m_libraryBadge = nullptr;
  QLabel* m_libraryLabel = nullptr;
  QLabel* m_libraryPath = nullptr;
  QLabel* m_libraryChip = nullptr;
  QPushButton* m_changeButton = nullptr;
  QPushButton* m_importButton = nullptr;
  QPushButton* m_rescanButton = nullptr;
  QPushButton* m_convertButton = nullptr;
  QPushButton* m_revealButton = nullptr;
  QPushButton* m_favoriteButton = nullptr;
  QPushButton* m_renameButton = nullptr;
  QPushButton* m_deleteButton = nullptr;
  QLineEdit* m_tagsEdit = nullptr;
  QProgressBar* m_convertProgress = nullptr;
  QPushButton* m_convertCancel = nullptr;
  QTimer* m_convertTimer = nullptr;
  std::vector<std::string> m_convertQueue;
  int m_convertDone = 0;
  int m_convertFailed = 0;
  QStringList m_conversionReport;
  std::vector<std::string> m_failedConversions;
  QLineEdit* m_searchEdit = nullptr;
  QComboBox* m_tagCombo = nullptr;
  QLabel* m_projectScope = nullptr;
  QTimer* m_reloadTimer = nullptr;
  AssetLibraryListView* m_categories = nullptr;
  AssetLibraryListView* m_grid = nullptr;
  AssetLibraryItemDelegate* m_delegate = nullptr;
  QSlider* m_sizeSlider = nullptr;
  QLabel* m_sizeValue = nullptr;
  QToolButton* m_gridButton = nullptr;
  QToolButton* m_listButton = nullptr;
  QLabel* m_countLabel = nullptr;
  AssetInspector* m_inspector = nullptr;
  QLabel* m_name = nullptr;
  QLabel* m_status = nullptr;
  QFormLayout* m_properties = nullptr;
  QListWidget* m_dependencies = nullptr;
  QListWidget* m_usage = nullptr;
  QLabel* m_footer = nullptr;
  QLabel* m_empty = nullptr;

public:
  explicit AssetLibraryWindow(QWidget* parent = nullptr);
  /**
   * Opens `folder` instead of the remembered library.
   *
   * Used by the UI snapshot runner, which browses a disposable fixture and must not
   * rewrite the user's remembered library folder.
   */
  AssetLibraryWindow(const QString& folder, bool remember, QWidget* parent = nullptr);
  ~AssetLibraryWindow() override;
  void setProjectContext(const std::shared_ptr<ProjectSession>& project);
  Result<void> revealAsset(const std::string& libraryId, const std::string& assetId);
  std::string libraryId() const;

protected:
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dropEvent(QDropEvent* event) override;

private:
  void buildUi();
  void chooseLibrary();
  void openLibrary(const QString& folder, bool remember = true);
  void reload();
  void refreshCategories();
  void refreshGrid();
  void showAsset(const project::LibraryAsset* asset);
  void clearInspector();
  void updatePreview();
  void updateStatus();
  void updateHeader();
  void updateActions();
  void applyViewMode();
  void runImport();
  void importFiles(const QStringList& files);
  void runRescan();
  void runConvertPending();
  void stepConvertQueue();
  void cancelConvertQueue();
  void showOperationReport(
    const QStringList& lines,
    const QStringList& files = {},
    const std::vector<std::string>& conversions = {});
  void revealSelected();
  void toggleFavorite();
  void renameSelected();
  void deleteSelected();
  void applyTags();
  void refreshUsage();
  void updateProjectActions();
  void captureSelected();
  void updateSelected();
  QImage previewImage(const project::LibraryAsset& asset, bool converted, int size) const;

  const project::LibraryAsset* selectedAsset() const;
  int pendingCount() const;
  int missingCount() const;
};
} // namespace tb::ui
