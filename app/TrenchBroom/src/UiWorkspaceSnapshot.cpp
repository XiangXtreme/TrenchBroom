/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "UiWorkspaceSnapshot.h"

#include <QApplication>
#include <QComboBox>
#include <QMenu>
#include <QSplitter>
#include <QTabBar>
#include <QToolButton>

#include "mdl/Grid.h"
#include "mdl/Map.h"
#include "mdl/MapHeader.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/MapStatus.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/ProjectSession.h"
#include "ui/ProjectWindow.h"
#include "ui/QPathUtils.h"
#include "ui/WorkspaceToolPanel.h"

#include "kd/filesystem_utils.h"

namespace tb::ui
{
UiWorkspaceSnapshot::UiWorkspaceSnapshot() = default;
UiWorkspaceSnapshot::~UiWorkspaceSnapshot() = default;

Result<void> UiWorkspaceSnapshot::initialize(
  AppController& appController,
  const std::filesystem::path& mapPath,
  const QString& target)
{
  if (!m_directory.isValid())
    return Error{"Cannot create workspace snapshot directory"};
  auto header = kdl::with_istream(mapPath, mdl::readMapHeader);
  if (header.is_error())
    return std::get<Error>(header.error());
  const auto& [game, format] = header.value();
  if (!game)
    return Error{"Workspace snapshot fixture needs a game header"};
  const auto path = pathFromQString(m_directory.filePath("workspace.tbproj"));
  {
    auto created = ProjectSession::create(path, "Workspace", *game);
    if (created.is_error())
      return std::get<Error>(created.error());
  }
  if (!appController.openProject(path))
    return Error{"Cannot open workspace snapshot project"};
  for (auto* widget : QApplication::topLevelWidgets())
    if (auto* window = qobject_cast<ProjectWindow*>(widget);
        window && window->session()->path() == std::filesystem::weakly_canonical(path))
      m_window.reset(window);
  if (!m_window)
    return Error{"Workspace snapshot project window was not registered"};

  auto& manager = appController.mapWindowManager();
  // The input map supplies the installed game/format; the snapshot owns every map view.
  for (auto* window : manager.mapWindows())
  {
    manager.closeMapView(*window);
    delete window;
  }
  if (target != "project-empty")
  {
    for (const auto* name : {"alpha", "beta"})
    {
      auto result = manager.createProjectMap(m_window->session(), name, format);
      if (result.is_error())
        return std::get<Error>(result.error());
    }
    if (target == "project-all-closed")
      for (auto* window : manager.mapWindows())
      {
        manager.closeMapView(*window);
        delete window;
      }
  }
  m_window->resize(1440, 900);
  return {};
}

QWidget* UiWorkspaceSnapshot::widget() const
{
  return m_window.get();
}

void configureWorkspaceSnapshot(QWidget& widget, const QString& target)
{
  auto* window = qobject_cast<ProjectWindow*>(&widget);
  if (!window)
    return;
  auto* tools = window->findChild<WorkspaceToolPanel*>();
  auto* mode = tools->findChild<QComboBox*>("ProjectToolsMode");
  mode->setCurrentIndex(
    target == "project-pinned" || target == "project-map-switch" ? 1 : 0);
  if (target == "project-map-switch")
  {
    auto* tabs = window->findChild<QTabBar*>("ProjectMapTabs");
    // Exercise the real tab routing with different per-document status values.
    for (int i = 0; i < tabs->count(); ++i)
    {
      tabs->setCurrentIndex(i);
      if (auto* map = window->activeMapWindow())
        map->document().map().grid().setSize(tabs->tabText(i) == "beta" ? 5 : 2);
    }
    for (int i = 0; i < tabs->count(); ++i)
      if (tabs->tabText(i) == "beta")
        tabs->setCurrentIndex(i);
    tools->showInfoPage(InfoPanelPage::PythonConsole);
  }
  else
    tools->showContent();
  if (mode->currentIndex() == 1)
    tools->findChild<QSplitter*>("ProjectToolsSplitter")->setSizes({560, 300});
}

QString workspaceSnapshotPendingReason(QWidget& widget, const QString& target)
{
  auto* window = qobject_cast<ProjectWindow*>(&widget);
  if (!window)
    return "Workspace snapshot is not a project window";
  auto* tools = window->findChild<WorkspaceToolPanel*>();
  auto* tabs = window->findChild<QTabBar*>("ProjectMapTabs");
  const auto statuses = window->findChildren<MapStatus*>();
  if (!tools || !tabs || statuses.size() != 1)
    return "Workspace must have one tool host, tab strip and status surface";
  if (tools->isAnimating())
    return "Workspace drawer animation is still running";
  const auto tool = target == "project-map-switch" ? WorkspaceToolId::PythonConsole
                                                   : WorkspaceToolId::ContentBrowser;
  if (!tools->isToolVisible(tool))
    return "Workspace tool is not visible";
  const auto pinned = target == "project-pinned" || target == "project-map-switch";
  if ((tools->findChild<QComboBox*>("ProjectToolsMode")->currentIndex() == 1) != pinned)
    return "Workspace mode mode is incorrect";
  auto* grid = statuses.front()->findChild<QComboBox*>("MapWindow_GridChoice");
  const auto empty = target == "project-empty" || target == "project-all-closed";
  if (empty)
  {
    if (
      window->activeMapWindow() || tabs->count() != 1 || tabs->isTabEnabled(0)
      || grid->isEnabled())
      return "Workspace has not reached the no-map state";
    auto* reopen = window->findChild<QToolButton*>("ProjectReopenMaps");
    const auto closed = target == "project-all-closed";
    if (
      reopen->isEnabled() != closed || (closed && reopen->menu()->actions().size() != 2)
      || window->session()->documents().size() != (closed ? 2u : 0u))
      return "Closed project documents or reopen actions are incorrect";
  }
  else
  {
    if (!window->activeMapWindow() || tabs->count() != 2 || !grid->isEnabled())
      return "Workspace maps are not ready";
    if (
      target == "project-map-switch"
      && (tabs->tabText(tabs->currentIndex()) != "beta" || grid->currentData().toInt() != 5))
      return "Workspace status has not followed the beta map";
  }
  return {};
}
} // namespace tb::ui
