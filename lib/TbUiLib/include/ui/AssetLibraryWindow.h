/*
  Copyright (C) 2026 XiangXtreme
  This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <QDialog>

#include "project/AssetLibrary.h"

#include <memory>

class QLabel;
class QLineEdit;
class QTreeWidget;

namespace tb::ui
{

/**
 * Standalone asset library window.
 *
 * A library is an independent resource collection, so it must be possible to create,
 * inspect and rescan one without opening a project. The window owns nothing but the
 * catalog handle: it never touches project state.
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
  void rescan();
  void convertImages();
  void reload();
};
} // namespace tb::ui
