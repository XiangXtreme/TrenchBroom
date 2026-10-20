/*
 Copyright (C) 2026 KreedzBroom contributors

 This file is part of TrenchBroom.
 SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ui/PlayModeFullScreen.h"

#include <QLayout>
#include <QPointer>
#include <QScopeGuard>
#include <QSplitter>
#include <QToolBar>
#include <QWidget>

#include <utility>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace tb::ui
{

struct PlayModeFullScreen::State
{
  struct Layout
  {
    QPointer<QLayout> layout;
    QMargins margins;
    int spacing;
  };
  struct Splitter
  {
    QPointer<QSplitter> splitter;
    QByteArray state;
    int handleWidth;
  };

  QPointer<QWidget> window;
  QByteArray geometry;
  Qt::WindowStates windowState;
  std::vector<QPointer<QWidget>> hiddenWidgets;
  std::vector<Layout> layouts;
  std::vector<Splitter> splitters;
};

PlayModeFullScreen::PlayModeFullScreen() = default;

PlayModeFullScreen::~PlayModeFullScreen()
{
  leave();
}

void PlayModeFullScreen::enter(QWidget& view)
{
  if (m_state)
  {
    return;
  }

  m_state = std::make_unique<State>();
  auto& state = *m_state;
  state.window = view.window();
  state.geometry = state.window->saveGeometry();
  state.windowState = state.window->windowState();

  // Keep the widget hierarchy and GL context intact. Hide only siblings of the path
  // from the playing view to its window, preserving already-hidden editor panels.
  auto* child = &view;
  for (auto* parent = child->parentWidget(); parent; parent = parent->parentWidget())
  {
    if (auto* splitter = qobject_cast<QSplitter*>(parent))
    {
      state.splitters.push_back(
        {splitter, splitter->saveState(), splitter->handleWidth()});
      splitter->setHandleWidth(0);
    }
    if (auto* layout = parent->layout())
    {
      state.layouts.push_back({layout, layout->contentsMargins(), layout->spacing()});
      layout->setContentsMargins(0, 0, 0, 0);
      layout->setSpacing(0);
    }
    for (auto* sibling : parent->findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
    {
      if (
        sibling != child && !sibling->isHidden()
        && (!sibling->isWindow() || qobject_cast<QToolBar*>(sibling)))
      {
        state.hiddenWidgets.push_back(sibling);
        sibling->hide();
      }
    }
    child = parent;
    if (parent == state.window)
    {
      break;
    }
  }

  state.window->showFullScreen();
}

void PlayModeFullScreen::leave()
{
  if (!m_state)
  {
    return;
  }
  auto state = std::move(m_state);
  if (!state->window)
  {
    return;
  }

#ifdef Q_OS_WIN
  // Windows can activate a window when restoring its maximized state, even with
  // WA_ShowWithoutActivating. Preserve the foreground owner across that transition.
  const auto foreground = GetForegroundWindow();
  const auto restoreForeground = qScopeGuard([foreground]() {
    if (foreground && foreground != GetForegroundWindow())
    {
      SetForegroundWindow(foreground);
    }
  });
#endif

  // Do not reactivate the window: leaving play on Alt+Tab must let the other app
  // retain focus. Also retain minimization if it caused play mode to stop.
  const auto minimized = state->window->windowState() & Qt::WindowMinimized;
  const auto showWithoutActivating =
    state->window->testAttribute(Qt::WA_ShowWithoutActivating);
  state->window->setAttribute(Qt::WA_ShowWithoutActivating);
  // Leave full screen before restoring geometry. On Windows, restoring a maximized
  // geometry while still full screen can retain both flags and then lose maximization.
  state->window->setWindowState((state->windowState & ~Qt::WindowActive) | minimized);
  state->window->restoreGeometry(state->geometry);
  if (minimized)
  {
    state->window->setWindowState(state->window->windowState() | minimized);
  }
  for (const auto& entry : state->layouts)
  {
    if (entry.layout)
    {
      entry.layout->setContentsMargins(entry.margins);
      entry.layout->setSpacing(entry.spacing);
    }
  }
  for (const auto& widget : state->hiddenWidgets)
  {
    if (widget)
    {
      widget->show();
    }
  }
  for (const auto& entry : state->splitters)
  {
    if (entry.splitter)
    {
      entry.splitter->setHandleWidth(entry.handleWidth);
      entry.splitter->restoreState(entry.state);
    }
  }
  state->window->setAttribute(Qt::WA_ShowWithoutActivating, showWithoutActivating);
}

} // namespace tb::ui
