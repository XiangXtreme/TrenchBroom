/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <QObject>
#include <QStringList>

#include <memory>

namespace tb::ui
{
/** Watches library folders without opening catalogs or changing project snapshots. */
class AssetLibraryWatcher : public QObject
{
  Q_OBJECT
  class Impl;
  std::unique_ptr<Impl> m_impl;

public:
  explicit AssetLibraryWatcher(QObject* parent = nullptr);
  ~AssetLibraryWatcher() override;
  void setFolders(QStringList folders);
  /** Accept the current disk state after an explicit scan/review. */
  void acknowledgeChanges();
signals:
  void foldersChanged();
};
} // namespace tb::ui
