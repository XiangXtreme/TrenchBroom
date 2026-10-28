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

namespace tb::ui
{
class AppController;
class ProjectSession;

class ProjectWindow : public QMainWindow
{
  AppController& m_appController;
  std::shared_ptr<ProjectSession> m_session;
  QTreeWidget* m_assets = nullptr;
  QTreeWidget* m_history = nullptr;
  QLineEdit* m_search = nullptr;

public:
  ProjectWindow(AppController& appController, std::shared_ptr<ProjectSession> session);
  const std::shared_ptr<ProjectSession>& session() const;

protected:
  void closeEvent(QCloseEvent* event) override;

private:
  void refresh();
  void refreshTitle();
  void importFiles();
  Result<bool> prepareImport(
    project::AssetImport& asset,
    const std::optional<project::Asset>& previous = std::nullopt);
  void newMap();
  void openSelected();
  void renameSelected();
  void moveSelected();
  void reimportSelected();
  void exportSelected();
  void checkResources(std::optional<std::string> assetId = std::nullopt);
  void createCheckpoint();
  void compareCheckpoint();
  void restoreCheckpoint();
  bool save();
  std::optional<std::string> selectedAsset() const;
  bool report(const Result<void>& result);
};
} // namespace tb::ui
