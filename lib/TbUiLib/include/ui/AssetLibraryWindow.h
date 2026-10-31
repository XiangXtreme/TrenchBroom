/*
  Copyright (C) 2026 XiangXtreme
  This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <QDialog>
#include <QStringList>

#include "project/AssetLibrary.h"

#include <cstddef>
#include <memory>

class QLabel;
class QLineEdit;
class QTreeWidget;

namespace tb::ui
{

/**
 * The application-wide asset library manager, opened from the welcome page.
 *
 * The library is a single folder configured once; projects never choose their own. The
 * window imports source files into it, converts images to GoldSrc texture content and
 * shows the current catalog. It owns nothing but the catalog handle and never touches
 * project state.
 */
class AssetLibraryWatcher;

class AssetLibraryWindow : public QDialog
{
  Q_OBJECT
  QLineEdit* m_path = nullptr;
  AssetLibraryWatcher* m_watcher = nullptr;
  QLabel* m_summary = nullptr;
  QTreeWidget* m_assets = nullptr;
  std::unique_ptr<project::AssetLibrary> m_library;

public:
  explicit AssetLibraryWindow(QWidget* parent = nullptr);

private:
  void chooseFolder();
  void openFolder(const std::filesystem::path& folder, bool createIfMissing);
  void importFiles();
  void rescan();
  void convertImages();
  // Converts every library image without converted content. Returns how many textures
  // were stored; individual failures are appended to `problems`.
  std::size_t convertPendingImages(QStringList& problems);
  void reload();
};
} // namespace tb::ui
