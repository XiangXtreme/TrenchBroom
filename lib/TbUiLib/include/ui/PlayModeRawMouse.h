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

#include <QAbstractNativeEventFilter>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

class QTimer;
class QWidget;

namespace tb::ui
{

struct RawMouseMotion
{
  double dx = 0.0;
  double dy = 0.0;
  uint64_t timestamp = 0;
};

/**
 * Decodes an XInput2 raw motion event (as delivered in an xcb_generic_event_t), returning
 * the unaccelerated x / y counts, or the values after pointer acceleration if accelerated
 * is set. Returns nullopt for any other event, or if TrenchBroom was built without
 * XInput2 support.
 */
std::optional<RawMouseMotion> parseXInputRawMotion(
  const void* event, uint8_t xinputOpcode, bool accelerated = false);

/**
 * Captures the mouse the way a game does: the cursor cannot leave the view and motion is
 * read as relative movement, either as unaccelerated device counts (the equivalent of
 * the game's m_rawinput 1) or after the desktop's pointer acceleration.
 *
 * - Wayland: the pointer is locked to the window (pointer-constraints) and motion comes
 *   from the relative-pointer protocol.
 * - X11 / XWayland: the pointer is grabbed and confined to the window with the cursor
 *   hidden through XFixes, which makes XWayland lock it, and motion comes from XInput2
 *   raw events.
 * - Elsewhere, or if the required protocols are missing, start() returns Backend::None
 *   and the caller keeps the cursor in the view itself.
 */
class PlayModeRawMouse : public QAbstractNativeEventFilter
{
public:
  /** dx / dy in counts (or accelerated pixels), timestamp in the source's clock (ms). */
  using MotionFn = std::function<void(double dx, double dy, double timestampMs)>;

  enum class Backend
  {
    None,
    Wayland,
    XInput2,
  };

private:
  struct WaylandState;

  MotionFn m_motion;
  Backend m_backend = Backend::None;
  bool m_confined = false;
  bool m_raw = true;

  uint8_t m_xinputOpcode = 0;
  uint32_t m_x11Window = 0;

  std::unique_ptr<WaylandState> m_wayland;
  QTimer* m_pollTimer = nullptr;

public:
  explicit PlayModeRawMouse(MotionFn motion);
  ~PlayModeRawMouse() override;

  PlayModeRawMouse(const PlayModeRawMouse&) = delete;
  PlayModeRawMouse& operator=(const PlayModeRawMouse&) = delete;

  /**
   * Captures the mouse for the given view, delivering unaccelerated motion if raw is set
   * and accelerated motion otherwise; returns the backend that delivers motion.
   */
  Backend start(QWidget& view, bool raw);
  void stop();

  /** Which backend delivers motion (None while stopped or unsupported). */
  Backend backend() const;
  /** Whether the windowing system keeps the pointer inside the window. */
  bool confinesPointer() const;

  /** Delivers queued motion now (Wayland events are dispatched on demand). */
  void poll();

  bool nativeEventFilter(
    const QByteArray& eventType, void* message, qintptr* result) override;

private:
  bool startWayland(QWidget& view);
  void stopWayland();
  bool startXInput2(QWidget& view);
  void stopXInput2();
};

} // namespace tb::ui
