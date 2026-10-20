/*
 Copyright (C) 2026 KreedzBroom contributors

 This file is part of TrenchBroom.
 SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <memory>

class QWidget;

namespace tb::ui
{

/** Temporarily presents a view full screen without reparenting its OpenGL widget. */
class PlayModeFullScreen
{
private:
  struct State;
  std::unique_ptr<State> m_state;

public:
  PlayModeFullScreen();
  ~PlayModeFullScreen();

  void enter(QWidget& view);
  void leave();
};

} // namespace tb::ui
