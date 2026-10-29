/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <QWidget>

#include "ui/AssetBrowserModel.h"

#include <memory>
#include <optional>

class QTreeWidget;
namespace tb::ui
{
class AppController;
class MapDocument;
class ModelBrowser;
class ProjectSession;

// One browser per project. A private game context keeps catalogs usable without map tabs.
class ProjectAssetPanel : public QWidget
{
  Q_OBJECT
  std::shared_ptr<ProjectSession> m_session;
  std::unique_ptr<MapDocument> m_fallback;
  ModelBrowser* m_browser = nullptr;
  QTreeWidget* m_details = nullptr;
  std::optional<BrowserAsset> m_selected;

public:
  ProjectAssetPanel(
    AppController& app,
    std::shared_ptr<ProjectSession> session,
    QWidget* parent = nullptr);
  ~ProjectAssetPanel() override;
  void setDocument(MapDocument* document);
  void showProjectAsset(const QString& id);

private:
  void showAsset(const BrowserAsset& asset);
  void refreshDetails();
};
} // namespace tb::ui
