/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom.
*/

#include "ui/MapCommandSurface.h"

#include <QAction>
#include <QMenuBar>
#include <QToolBar>

#include "mdl/Map.h"
#include "ui/Action.h"
#include "ui/AppController.h"

#include <utility>

namespace tb::ui
{

MapCommandSurface::MapCommandSurface(
  AppController& appController, std::function<ActionExecutionContext()> contextFactory)
  : m_appController{appController}
  , m_contextFactory{std::move(contextFactory)}
{
}

PopulateMenuResult MapCommandSurface::populateMenuBar(QMenuBar& menuBar)
{
  m_menuActions = tb::ui::populateMenuBar(
    m_appController.actionManager(), menuBar, m_actionMap, [this](const Action& action) {
      auto context = m_contextFactory();
      action.execute(context);
    });
  return m_menuActions;
}

void MapCommandSurface::populateToolBar(QToolBar& toolBar)
{
  tb::ui::populateToolBar(
    m_appController.actionManager(), toolBar, m_actionMap, [this](const Action& action) {
      auto context = m_contextFactory();
      action.execute(context);
    });
}

void MapCommandSurface::updateShortcuts()
{
  for (auto [tbAction, qtAction] : m_actionMap)
  {
    if (m_shortcutsEnabled)
      updateActionKeySequence(*qtAction, *tbAction);
    else
      qtAction->setShortcut({});
  }
}

void MapCommandSurface::setShortcutsEnabled(const bool enabled)
{
  m_shortcutsEnabled = enabled;
  updateShortcuts();
}

void MapCommandSurface::updateActionState()
{
  auto context = m_contextFactory();
  for (auto [tAction, qAction] : m_actionMap)
  {
    if (qAction == m_menuActions.undoAction || qAction == m_menuActions.redoAction)
    {
      // These are handled specially for performance reasons.
      continue;
    }
    qAction->setEnabled(tAction->enabled(context));
    if (qAction->isCheckable())
    {
      qAction->setChecked(tAction->checked(context));
    }
  }
}

QAction* MapCommandSurface::findAction(const Action& action) const
{
  const auto it = m_actionMap.find(&action);
  return it == m_actionMap.end() ? nullptr : it->second;
}

void MapCommandSurface::updateHistoryActions(const mdl::Map& map)
{
  const auto update = [](QAction* action, const std::string* command, const char* label) {
    if (!action)
      return;
    action->setText(QString::fromStdString(
      command ? std::string{label} + " " + *command : std::string{label}));
    action->setEnabled(command != nullptr);
  };
  update(undoAction(), map.undoCommandName(), "Undo");
  update(redoAction(), map.redoCommandName(), "Redo");
}

QAction& MapCommandSurface::actionFor(const Action& action, QObject& owner)
{
  auto& qtAction = findOrCreateQtAction(
    m_actionMap,
    action,
    [this](const Action& triggered) {
      auto context = m_contextFactory();
      triggered.execute(context);
    },
    &owner);
  if (!m_shortcutsEnabled)
    qtAction.setShortcut({});
  auto context = m_contextFactory();
  qtAction.setEnabled(action.enabled(context));
  if (qtAction.isCheckable())
    qtAction.setChecked(action.checked(context));
  return qtAction;
}

QAction* MapCommandSurface::undoAction() const
{
  return m_menuActions.undoAction;
}

QAction* MapCommandSurface::redoAction() const
{
  return m_menuActions.redoAction;
}

QAction* MapCommandSurface::rerunAction() const
{
  return m_menuActions.rerunAction;
}

QMenu* MapCommandSurface::recentDocumentsMenu() const
{
  return m_menuActions.recentDocumentsMenu;
}

} // namespace tb::ui
