/*
 Copyright (C) 2026 KreedzBroom contributors

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <QElapsedTimer>

#include "cs/scs.h"
#include "ui/PlayModeShowPre.h"
#include "ui/PlayModeSound.h"
#include "ui/PlayModeTimeline.h"
#include "ui/PlayModeWorld.h"

#include "vm/vec.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

class QInputEvent;
class QKeyEvent;

namespace tb
{
namespace gl
{
class PerspectiveCamera;
}

namespace render
{
class RenderBatch;
class RenderContext;
} // namespace render

namespace ui
{
class MapDocument;

/**
 * Drives the embedded Counter-Strike 1.6 movement simulation (TbCsLib) for one 3D map
 * view: turns key and mouse input into usercmds the way the CS client does, steps the
 * simulation at a fixed 100 Hz and moves the view camera to the player's eyes.
 *
 * The player state is kept when play mode is left, so it can be resumed exactly where it
 * stopped (including velocity and duck / jump timers).
 */
class PlayModeController
{
private:
  struct ContextDeleter
  {
    void operator()(scs_context* context) const;
  };

  MapDocument& m_document;
  gl::PerspectiveCamera& m_camera;
  float m_editorFov = 90.0f;
  float m_editorZoom = 1.0f;

  std::unique_ptr<PlayModeSound> m_sound; // must outlive m_context, which calls it
  std::unique_ptr<scs_context, ContextDeleter> m_context;
  PlayModeWorld m_world;
  bool m_active = false;

  PlayModeTimeline m_timeline;
  QElapsedTimer m_clock;
  // Input sources and their clocks (Qt events vs raw mouse motion), see eventTime()
  enum class EventSource
  {
    Qt,
    RawMouse,
  };
  // event time minus m_clock time, per source
  std::optional<double> m_eventClockOffset[2];
  PlayModeShowPre m_showPre;
  // when the last hard landing was reported on the HUD
  double m_fallShownMs = -1.0e9;
  uint32_t m_fallShownCount = 0;
  // showkeys: when each movement button was last down in a usercmd (forward, back,
  // move left, move right, duck, jump)
  double m_keyLastDownMs[6] = {};
  double m_lastEventMs = 0.0;

  vm::vec3f m_viewAngles{}; // spawn view angles, pitch / yaw / roll in GoldSrc degrees
  vm::vec3f m_previousEye{};
  vm::vec3f m_currentEye{};

  std::optional<scs_player_state> m_savedState;
  std::string m_statusMessage;

public:
  PlayModeController(MapDocument& document, gl::PerspectiveCamera& camera);
  ~PlayModeController();

  bool active() const;

  /**
   * Compiles the current map and starts simulating. Resumes the last saved player state
   * unless fromCamera is set or that state is no longer valid, in which case the player
   * spawns at the camera (or at a spawn point if the camera position is blocked).
   */
  void start(bool fromCamera);
  void stop();

  void keyDown(const QKeyEvent& event);
  void keyUp(const QKeyEvent& event);
  void releaseAllKeys();

  /** Mouse movement in device pixels (counts), positive y is down. */
  void look(const QInputEvent& event, float dx, float dy);

  /**
   * Unaccelerated mouse movement in device counts (m_rawinput 1), with the windowing
   * system timestamp of the motion.
   */
  void rawLook(double dx, double dy, double timestampMs);

  /** Logged to the console, e.g. which mouse capture backend is active. */
  void setInputDescription(std::string description);

  /**
   * One mouse wheel notch (up if up is set): performs the action configured for that
   * direction for exactly one usercmd, like "bind mwheelup +duck" in the game.
   */
  void wheel(const QInputEvent& event, bool up);

  /** Advances the simulation to the current time and moves the camera. */
  void update();

  void render(render::RenderContext& renderContext, render::RenderBatch& renderBatch);

private:
  bool rebuildWorld();
  bool placePlayer(bool fromCamera);
  bool tryPlaceAt(const vm::vec3f& origin, const vm::vec3f& angles);
  vm::vec3f eyePosition() const;
  void syncCamera(float alpha);
  bool handleKey(const QKeyEvent& event, bool down);
  double nowMs() const;
  double eventTime(double timestampMs, EventSource source);
  void turn(double timeMs, double dx, double dy);
};

} // namespace ui
} // namespace tb
