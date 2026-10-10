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

#include "ui/PlayModeRawMouse.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QTimer>
#include <QWidget>
#include <QWindow>

#ifdef TB_PLAY_MODE_WAYLAND
#include <QtGui/qpa/qplatformwindow_p.h> // QNativeInterface::Private::QWaylandWindow
#endif

#ifdef TB_PLAY_MODE_XINPUT2
#include <xcb/xcb.h>
#include <xcb/xfixes.h>
#include <xcb/xinput.h>
#endif

#ifdef TB_PLAY_MODE_WAYLAND
#include "pointer-constraints-unstable-v1-client-protocol.h"
#include "relative-pointer-unstable-v1-client-protocol.h"

#include <wayland-client.h>
#endif

#include <cstdlib>
#include <cstring>
#include <utility>

namespace tb::ui
{

#ifdef TB_PLAY_MODE_WAYLAND

struct PlayModeRawMouse::WaylandState
{
  MotionFn motion;
  bool raw = true;

  wl_display* display = nullptr;
  wl_event_queue* queue = nullptr;
  wl_display* displayWrapper = nullptr;
  wl_registry* registry = nullptr;
  zwp_pointer_constraints_v1* constraints = nullptr;
  zwp_relative_pointer_manager_v1* relativePointerManager = nullptr;
  zwp_relative_pointer_v1* relativePointer = nullptr;
  zwp_locked_pointer_v1* lockedPointer = nullptr;

  static const wl_registry_listener registryListener;
  static const zwp_relative_pointer_v1_listener relativePointerListener;
  static const zwp_locked_pointer_v1_listener lockedPointerListener;

  static void registryGlobal(
    void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t);
  static void registryGlobalRemove(void*, wl_registry*, uint32_t) {}
  static void relativeMotion(
    void* data,
    zwp_relative_pointer_v1*,
    uint32_t utimeHi,
    uint32_t utimeLo,
    wl_fixed_t dx,
    wl_fixed_t dy,
    wl_fixed_t dxUnaccelerated,
    wl_fixed_t dyUnaccelerated);
  static void locked(void*, zwp_locked_pointer_v1*) {}
  static void unlocked(void*, zwp_locked_pointer_v1*) {}

  ~WaylandState()
  {
    if (lockedPointer)
    {
      zwp_locked_pointer_v1_destroy(lockedPointer);
    }
    if (relativePointer)
    {
      zwp_relative_pointer_v1_destroy(relativePointer);
    }
    if (relativePointerManager)
    {
      zwp_relative_pointer_manager_v1_destroy(relativePointerManager);
    }
    if (constraints)
    {
      zwp_pointer_constraints_v1_destroy(constraints);
    }
    if (registry)
    {
      wl_registry_destroy(registry);
    }
    if (displayWrapper)
    {
      wl_proxy_wrapper_destroy(displayWrapper);
    }
    if (display)
    {
      wl_display_flush(display);
    }
    if (queue)
    {
      wl_event_queue_destroy(queue);
    }
  }
};

const wl_registry_listener PlayModeRawMouse::WaylandState::registryListener = {
  &PlayModeRawMouse::WaylandState::registryGlobal,
  &PlayModeRawMouse::WaylandState::registryGlobalRemove,
};

const zwp_relative_pointer_v1_listener
  PlayModeRawMouse::WaylandState::relativePointerListener = {
    &PlayModeRawMouse::WaylandState::relativeMotion,
};

const zwp_locked_pointer_v1_listener
  PlayModeRawMouse::WaylandState::lockedPointerListener = {
    &PlayModeRawMouse::WaylandState::locked,
    &PlayModeRawMouse::WaylandState::unlocked,
};

void PlayModeRawMouse::WaylandState::registryGlobal(
  void* data, wl_registry* registry, const uint32_t name, const char* interface, uint32_t)
{
  auto& state = *static_cast<WaylandState*>(data);
  if (std::strcmp(interface, zwp_pointer_constraints_v1_interface.name) == 0)
  {
    state.constraints = static_cast<zwp_pointer_constraints_v1*>(
      wl_registry_bind(registry, name, &zwp_pointer_constraints_v1_interface, 1));
  }
  else if (std::strcmp(interface, zwp_relative_pointer_manager_v1_interface.name) == 0)
  {
    state.relativePointerManager = static_cast<zwp_relative_pointer_manager_v1*>(
      wl_registry_bind(registry, name, &zwp_relative_pointer_manager_v1_interface, 1));
  }
}

void PlayModeRawMouse::WaylandState::relativeMotion(
  void* data,
  zwp_relative_pointer_v1*,
  const uint32_t utimeHi,
  const uint32_t utimeLo,
  const wl_fixed_t dx,
  const wl_fixed_t dy,
  const wl_fixed_t dxUnaccelerated,
  const wl_fixed_t dyUnaccelerated)
{
  // Unaccelerated motion is in device counts (libinput normalizes mice with a known DPI
  // to 1000 dpi), accelerated motion in pixels; the timestamp is in microseconds.
  const auto& state = *static_cast<const WaylandState*>(data);
  const auto utime = (uint64_t{utimeHi} << 32) | uint64_t{utimeLo};
  state.motion(
    wl_fixed_to_double(state.raw ? dxUnaccelerated : dx),
    wl_fixed_to_double(state.raw ? dyUnaccelerated : dy),
    static_cast<double>(utime) / 1000.0);
}

#else

struct PlayModeRawMouse::WaylandState
{
};

#endif

namespace
{

#ifdef TB_PLAY_MODE_XINPUT2

xcb_connection_t* x11Connection()
{
  if (auto* x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>())
  {
    return x11->connection();
  }
  return nullptr;
}

xcb_window_t rootWindow(xcb_connection_t* connection)
{
  return xcb_setup_roots_iterator(xcb_get_setup(connection)).data->root;
}

// Raw events are only delivered for selections on the root window.
void selectRawMotion(xcb_connection_t* connection, const bool enable)
{
  struct
  {
    xcb_input_event_mask_t header;
    uint32_t mask;
  } selection = {
    {XCB_INPUT_DEVICE_ALL_MASTER, 1},
    enable ? uint32_t{XCB_INPUT_XI_EVENT_MASK_RAW_MOTION} : uint32_t{0},
  };
  xcb_input_xi_select_events(connection, rootWindow(connection), 1, &selection.header);
}

bool initXFixes(xcb_connection_t* connection)
{
  const auto* extension = xcb_get_extension_data(connection, &xcb_xfixes_id);
  if (!extension || !extension->present)
  {
    return false;
  }
  // XFixes requires the client to announce its version before use.
  auto* reply = xcb_xfixes_query_version_reply(
    connection, xcb_xfixes_query_version(connection, 4, 0), nullptr);
  const auto ok = reply && reply->major_version >= 4;
  std::free(reply);
  return ok;
}

double toDouble(const xcb_input_fp3232_t& value)
{
  return static_cast<double>(value.integral)
         + static_cast<double>(value.frac) / 4294967296.0;
}

#endif

} // namespace

PlayModeRawMouse::PlayModeRawMouse(MotionFn motion)
  : m_motion{std::move(motion)}
{
}

PlayModeRawMouse::~PlayModeRawMouse()
{
  stop();
}

PlayModeRawMouse::Backend PlayModeRawMouse::start(QWidget& view, const bool raw)
{
  stop();
  m_raw = raw;

  if (QGuiApplication::platformName().startsWith("wayland"))
  {
    if (startWayland(view))
    {
      m_backend = Backend::Wayland;
    }
  }
  else if (QGuiApplication::platformName() == "xcb")
  {
    if (startXInput2(view))
    {
      m_backend = Backend::XInput2;
    }
  }
  return m_backend;
}

void PlayModeRawMouse::stop()
{
  switch (m_backend)
  {
  case Backend::Wayland:
    stopWayland();
    break;
  case Backend::XInput2:
    stopXInput2();
    break;
  case Backend::None:
    break;
  }
  m_backend = Backend::None;
  m_confined = false;
}

PlayModeRawMouse::Backend PlayModeRawMouse::backend() const
{
  return m_backend;
}

bool PlayModeRawMouse::confinesPointer() const
{
  return m_confined;
}

void PlayModeRawMouse::poll()
{
#ifdef TB_PLAY_MODE_WAYLAND
  if (m_backend == Backend::Wayland && m_wayland)
  {
    // Qt's event thread reads the socket; our objects live on a private queue whose
    // events are only dispatched here.
    wl_display_dispatch_queue_pending(m_wayland->display, m_wayland->queue);
    wl_display_flush(m_wayland->display);
  }
#endif
}

bool PlayModeRawMouse::nativeEventFilter(
  const QByteArray& eventType, void* message, qintptr*)
{
  if (m_backend == Backend::XInput2 && eventType == "xcb_generic_event_t")
  {
    if (const auto motion = parseXInputRawMotion(message, m_xinputOpcode, !m_raw))
    {
      m_motion(motion->dx, motion->dy, static_cast<double>(motion->timestamp));
    }
  }
  // Never consume the event; Qt does not use raw motion itself.
  return false;
}

bool PlayModeRawMouse::startWayland([[maybe_unused]] QWidget& view)
{
#ifdef TB_PLAY_MODE_WAYLAND
  auto* application = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
  auto* window = view.window()->windowHandle();
  auto* waylandWindow =
    window ? window->nativeInterface<QNativeInterface::Private::QWaylandWindow>()
           : nullptr;
  if (!application || !waylandWindow)
  {
    return false;
  }

  auto* display = application->display();
  auto* pointer = application->pointer();
  auto* surface = waylandWindow->surface();
  if (!display || !pointer || !surface)
  {
    return false;
  }

  // Bind the protocols on a private event queue so their events never run inside Qt's
  // own dispatching.
  auto state = std::make_unique<WaylandState>();
  state->motion = m_motion;
  state->raw = m_raw;
  state->display = display;
  state->queue = wl_display_create_queue(display);
  state->displayWrapper = static_cast<wl_display*>(wl_proxy_create_wrapper(display));
  wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(state->displayWrapper), state->queue);
  state->registry = wl_display_get_registry(state->displayWrapper);
  wl_registry_add_listener(state->registry, &WaylandState::registryListener, state.get());
  wl_display_roundtrip_queue(display, state->queue);

  if (!state->constraints || !state->relativePointerManager)
  {
    return false; // compositor without pointer locking / relative motion
  }

  state->relativePointer = zwp_relative_pointer_manager_v1_get_relative_pointer(
    state->relativePointerManager, pointer);
  zwp_relative_pointer_v1_add_listener(
    state->relativePointer, &WaylandState::relativePointerListener, state.get());

  // Lock the pointer where it is (it is hidden); when play mode ends it reappears at the
  // center of the view.
  state->lockedPointer = zwp_pointer_constraints_v1_lock_pointer(
    state->constraints,
    surface,
    pointer,
    nullptr,
    ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
  zwp_locked_pointer_v1_add_listener(
    state->lockedPointer, &WaylandState::lockedPointerListener, state.get());
  const auto center = view.mapTo(view.window(), view.rect().center());
  zwp_locked_pointer_v1_set_cursor_position_hint(
    state->lockedPointer, wl_fixed_from_int(center.x()), wl_fixed_from_int(center.y()));
  wl_display_flush(display);

  m_wayland = std::move(state);
  m_confined = true;

  // Deliver motion promptly even while the view is not rendering.
  if (!m_pollTimer)
  {
    m_pollTimer = new QTimer{&view};
    m_pollTimer->setTimerType(Qt::PreciseTimer);
    QObject::connect(m_pollTimer, &QTimer::timeout, [this]() { poll(); });
  }
  m_pollTimer->start(2);
  return true;
#else
  return false;
#endif
}

void PlayModeRawMouse::stopWayland()
{
  if (m_pollTimer)
  {
    m_pollTimer->stop();
  }
  m_wayland.reset();
}

bool PlayModeRawMouse::startXInput2([[maybe_unused]] QWidget& view)
{
#ifdef TB_PLAY_MODE_XINPUT2
  auto* connection = x11Connection();
  if (!connection)
  {
    return false;
  }

  const auto* extension = xcb_get_extension_data(connection, &xcb_input_id);
  if (!extension || !extension->present)
  {
    return false;
  }

  m_xinputOpcode = extension->major_opcode;
  selectRawMotion(connection, true);
  QCoreApplication::instance()->installNativeEventFilter(this);

  // Grab the pointer confined to the window and hide the cursor through XFixes. On a real
  // X server this keeps the pointer inside the window; XWayland additionally turns a
  // hidden, confined cursor into a locked pointer that does not move at all.
  m_x11Window = static_cast<uint32_t>(view.window()->winId());
  constexpr auto eventMask = uint16_t{
    XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE
    | XCB_EVENT_MASK_POINTER_MOTION};
  auto* grab = xcb_grab_pointer_reply(
    connection,
    xcb_grab_pointer(
      connection,
      1, // owner events: deliver to Qt's windows as usual
      m_x11Window,
      eventMask,
      XCB_GRAB_MODE_ASYNC,
      XCB_GRAB_MODE_ASYNC,
      m_x11Window, // confine to
      XCB_NONE,
      XCB_CURRENT_TIME),
    nullptr);
  m_confined = grab && grab->status == XCB_GRAB_STATUS_SUCCESS;
  std::free(grab);

  if (initXFixes(connection))
  {
    xcb_xfixes_hide_cursor(connection, m_x11Window);
  }
  xcb_flush(connection);
  return true;
#else
  return false;
#endif
}

void PlayModeRawMouse::stopXInput2()
{
  QCoreApplication::instance()->removeNativeEventFilter(this);
#ifdef TB_PLAY_MODE_XINPUT2
  if (auto* connection = x11Connection())
  {
    if (initXFixes(connection))
    {
      xcb_xfixes_show_cursor(connection, m_x11Window);
    }
    if (m_confined)
    {
      xcb_ungrab_pointer(connection, XCB_CURRENT_TIME);
    }
    selectRawMotion(connection, false);
    xcb_flush(connection);
  }
#endif
}

std::optional<RawMouseMotion> parseXInputRawMotion(
  [[maybe_unused]] const void* event,
  [[maybe_unused]] const uint8_t xinputOpcode,
  [[maybe_unused]] const bool accelerated)
{
#ifdef TB_PLAY_MODE_XINPUT2
  const auto* genericEvent = static_cast<const xcb_ge_generic_event_t*>(event);
  if (
    (genericEvent->response_type & 0x7f) != XCB_GE_GENERIC
    || genericEvent->extension != xinputOpcode
    || genericEvent->event_type != XCB_INPUT_RAW_MOTION)
  {
    return std::nullopt;
  }

  // The valuator mask says which axes are present; axis 0 is x and axis 1 is y for
  // relative pointing devices. axisvalues are the values after pointer acceleration,
  // axisvalues_raw the unaccelerated device counts. (Raw motion events share the raw
  // button press layout, hence the accessor names.)
  const auto* rawEvent = static_cast<const xcb_input_raw_motion_event_t*>(event);
  const auto* mask = xcb_input_raw_button_press_valuator_mask(rawEvent);
  const auto maskLength = xcb_input_raw_button_press_valuator_mask_length(rawEvent);
  const auto* values = accelerated ? xcb_input_raw_button_press_axisvalues(rawEvent)
                                   : xcb_input_raw_button_press_axisvalues_raw(rawEvent);

  auto motion = RawMouseMotion{0.0, 0.0, rawEvent->time};
  auto valueIndex = 0;
  for (auto axis = 0; axis < maskLength * 32 && axis < 2; ++axis)
  {
    if (mask[axis / 32] & (1u << (axis % 32)))
    {
      (axis == 0 ? motion.dx : motion.dy) = toDouble(values[valueIndex++]);
    }
  }

  if (motion.dx == 0.0 && motion.dy == 0.0)
  {
    return std::nullopt;
  }
  return motion;
#else
  return std::nullopt;
#endif
}

} // namespace tb::ui
