/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom.
*/

#pragma once

#include "ui/ActionBuilder.h"
#include "ui/ActionExecutionContext.h"

#include <functional>
#include <unordered_map>

class QAction;
class QMenu;
class QMenuBar;
class QToolBar;

namespace tb::ui
{

class Action;
class AppController;

/**
 * The map command surface: the menu bar, the tool bar and the QActions for one map, plus
 * keeping their enabled and checked state in sync with the map.
 *
 * It is separate from MapWindow so more than one host can own map commands. The host only
 * supplies the execution context, which is what decides *which* map the commands act on.
 */
class MapCommandSurface
{
private:
  AppController& m_appController;
  std::function<ActionExecutionContext()> m_contextFactory;

  std::unordered_map<const Action*, QAction*> m_actionMap;
  PopulateMenuResult m_menuActions;
  bool m_shortcutsEnabled = true;

public:
  MapCommandSurface(
    AppController& appController, std::function<ActionExecutionContext()> contextFactory);

  PopulateMenuResult populateMenuBar(QMenuBar& menuBar);
  void populateToolBar(QToolBar& toolBar);

  void updateShortcuts();
  void setShortcutsEnabled(bool enabled);

  void updateActionState();

  QAction* findAction(const Action& action) const;

  QAction* undoAction() const;
  QAction* redoAction() const;
  QAction* rerunAction() const;
  QMenu* recentDocumentsMenu() const;
};

} // namespace tb::ui
