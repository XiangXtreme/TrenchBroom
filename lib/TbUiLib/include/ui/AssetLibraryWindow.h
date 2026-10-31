/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom, licensed under GPL version 3 or later.
*/
#pragma once

#include <QDialog>
#include <QString>

#include "project/ProjectAsset.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QSlider;
class QToolButton;

namespace tb::ui
{
class AssetLibraryItemDelegate;
class AssetLibrarySource;

/**
 * The application-wide asset library browser.
 *
 * Mirrors the Blender-style layout the project settled on: categories and smart filters
 * on the left, an asset grid in the middle and an inspector on the right. Everything is
 * read through AssetLibrarySource, so a remote library can replace the local folder
 * without touching this window.
 */
class AssetLibraryWindow : public QDialog
{
  Q_OBJECT
  std::unique_ptr<AssetLibrarySource> m_source;
  std::vector<project::LibraryAsset> m_assets;
  std::optional<std::string> m_selectedId;
  QString m_filter = QStringLiteral("all");
  QString m_search;

  QLabel* m_libraryLabel = nullptr;
  QLineEdit* m_searchEdit = nullptr;
  QListWidget* m_categories = nullptr;
  QListWidget* m_grid = nullptr;
  AssetLibraryItemDelegate* m_delegate = nullptr;
  QSlider* m_sizeSlider = nullptr;
  QToolButton* m_gridButton = nullptr;
  QToolButton* m_listButton = nullptr;
  QLabel* m_preview = nullptr;
  QComboBox* m_previewMode = nullptr;
  QLabel* m_name = nullptr;
  QLabel* m_status = nullptr;
  QFormLayout* m_properties = nullptr;
  QListWidget* m_dependencies = nullptr;
  QLabel* m_footer = nullptr;
  QLabel* m_empty = nullptr;

public:
  explicit AssetLibraryWindow(QWidget* parent = nullptr);
  ~AssetLibraryWindow() override;

private:
  void chooseLibrary();
  void openLibrary(const QString& folder);
  void reload();
  void refreshCategories();
  void refreshGrid();
  void showAsset(const project::LibraryAsset* asset);
  void clearInspector();
  void updatePreview();
  void updateStatus();
  void runImport();
  void runRescan();
  void runConvertPending();
  void revealSelected();

  const project::LibraryAsset* selectedAsset() const;
  int pendingCount() const;
  int missingCount() const;
};
} // namespace tb::ui
