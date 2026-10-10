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

#include "ui/PlayModeController.h"

#include <QKeyEvent>

#include "base/Logger.h"
#include "base/PreferenceManager.h"
#include "prefs/Preferences.h"
#include "gl/AttrString.h"
#include "gl/FontDescriptor.h"
#include "gl/PerspectiveCamera.h"
#include "mdl/GameFileSystem.h"
#include "mdl/Map.h"
#include "render/RenderBatch.h"
#include "render/RenderService.h"
#include "render/TextAnchor.h"
#include "render/TextRenderer.h"
#include "ui/MapDocument.h"

#include "vm/scalar.h"
#include "vm/vec.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

namespace tb::ui
{
namespace
{

// The default key preferences are Qt key codes written out without Qt.
static_assert(Preferences::PlayModeQtKeyW == Qt::Key_W);
static_assert(Preferences::PlayModeQtKeyS == Qt::Key_S);
static_assert(Preferences::PlayModeQtKeyA == Qt::Key_A);
static_assert(Preferences::PlayModeQtKeyD == Qt::Key_D);
static_assert(Preferences::PlayModeQtKeySpace == Qt::Key_Space);
static_assert(Preferences::PlayModeQtKeyControl == Qt::Key_Control);
static_assert(Preferences::PlayModeQtKeyShift == Qt::Key_Shift);
static_assert(Preferences::PlayModeQtKeyE == Qt::Key_E);
static_assert(Preferences::PlayModeQtKeyLeft == Qt::Key_Left);
static_assert(Preferences::PlayModeQtKeyRight == Qt::Key_Right);

constexpr auto PlayerViewHeight = 17.0f; // CS VEC_VIEW
constexpr auto MouseYawDegrees = 0.022f; // m_yaw / m_pitch
constexpr auto UnstuckStep = 8.0f;
constexpr auto UnstuckRange = 128.0f;

float toDegrees(const float value)
{
  return value * 180.0f / vm::Cf::pi();
}

float toRadians(const float value)
{
  return value * vm::Cf::pi() / 180.0f;
}

// GoldSrc AngleVectors forward vector (positive pitch looks down).
vm::vec3f forwardFromAngles(const vm::vec3f& angles)
{
  const auto pitch = toRadians(angles[0]);
  const auto yaw = toRadians(angles[1]);
  return {
    std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), -std::sin(pitch)};
}

vm::vec3f anglesFromDirection(const vm::vec3f& direction)
{
  const auto dir = vm::normalize(direction);
  return {
    -toDegrees(std::asin(std::clamp(dir.z(), -1.0f, 1.0f))),
    toDegrees(std::atan2(dir.y(), dir.x())),
    0.0f};
}

const char* clipTypeName(const int clipType)
{
  switch (clipType)
  {
  case SCS_CLIPTYPE_SMALLEST:
    return "smallest";
  case SCS_CLIPTYPE_NORMALIZED:
    return "normalized";
  case SCS_CLIPTYPE_SIMPLE:
    return "simple";
  case SCS_CLIPTYPE_PRECISE:
    return "precise";
  default:
    return "legacy";
  }
}

int usehullFor(const scs_player_state& state)
{
  return (state.flags & SCS_FL_DUCKING) ? SCS_HULL_DUCKED : SCS_HULL_STANDING;
}

constexpr auto SpeedFontScale = 2.0f;
constexpr auto KeysFontScale = 1.3f;
// A wheel jump or duck is down for a single 10 ms usercmd; keep keys lit long enough to
// be seen.
constexpr auto KeyMinVisibleMs = 100.0;

struct ShowKey
{
  int button;
  const char* label;
  float column; // in key widths from the center
  float row;    // in lines below the speed
};

// The classic KZ showkeys layout:
//        W
//    A   S   D
//   DUCK   JUMP
constexpr auto ShowKeys = std::array<ShowKey, 6>{{
  {SCS_IN_FORWARD, "W", 0.0f, 0.0f},
  {SCS_IN_BACK, "S", 0.0f, 1.0f},
  {SCS_IN_MOVELEFT, "A", -1.0f, 1.0f},
  {SCS_IN_MOVERIGHT, "D", 1.0f, 1.0f},
  {SCS_IN_DUCK, "DUCK", -1.4f, 2.0f},
  {SCS_IN_JUMP, "JUMP", 1.4f, 2.0f},
}};

// The HUD shows whole units like KZ speedometers (truncated, not rounded).
std::string formatSpeed(const float speed)
{
  return std::to_string(static_cast<int>(speed));
}

constexpr auto PreFontScale = 1.5f;
constexpr auto PreHoldMs = 1500.0;
constexpr auto PreFadeMs = 500.0;

// The last hard landing's damage stays up for a moment.
constexpr auto FallFontScale = 1.3f;
constexpr auto FallHoldMs = 2500.0;
constexpr auto FallFadeMs = 500.0;

float fadeAlpha(const double ageMs, const double holdMs, const double fadeMs)
{
  return static_cast<float>(std::clamp(1.0 - (ageMs - holdMs) / fadeMs, 0.0, 1.0));
}

// Text centered on a screen position given in pixels from the bottom left.
class ScreenTextAnchor : public render::TextAnchor
{
private:
  vm::vec2f m_position;

public:
  explicit ScreenTextAnchor(const vm::vec2f& position)
    : m_position{position}
  {
  }

  vm::vec3f offset(const gl::Camera&, const vm::vec2f& size) const override
  {
    return vm::vec3f{
      std::round(m_position.x() - size.x() / 2.0f),
      std::round(m_position.y() - size.y() / 2.0f),
      0.0f};
  }

  vm::vec3f position(const gl::Camera& camera) const override
  {
    return camera.unproject(m_position.x(), m_position.y(), 0.0f);
  }
};

// Draws HUD text in a larger font with a drop shadow, like GoldSrc HUD messages.
void renderHudText(
  render::RenderContext& renderContext,
  render::RenderBatch& renderBatch,
  const std::string& text,
  const float fontScale,
  const vm::vec2f& position,
  const float red,
  const float green,
  const float blue,
  const float alpha = 1.0f)
{
  const auto fontSize = static_cast<size_t>(
    std::round(static_cast<float>(pref(Preferences::RendererFontSize)) * fontScale));
  auto* textRenderer = new render::TextRenderer{
    gl::FontDescriptor{pref(Preferences::RendererFontPath), fontSize}};

  const auto transparent = RgbaF{0.0f, 0.0f, 0.0f, 0.0f};
  const auto color = RgbaF{red, green, blue, alpha};
  const auto shadow = RgbaF{0.0f, 0.0f, 0.0f, 0.8f * alpha};
  const auto string = gl::AttrString{text};
  // Whole pixels keep the glyphs sharp, and a shadow offset that grows with the font
  // (1 px for small text) keeps it attached to thin strokes.
  const auto pixel = vm::round(position);
  const auto shadowOffset =
    std::max(1.0f, std::round(static_cast<float>(fontSize) / 14.0f));
  textRenderer->renderStringOnTop(
    renderContext,
    shadow,
    transparent,
    string,
    ScreenTextAnchor{pixel + vm::vec2f{shadowOffset, -shadowOffset}});
  textRenderer->renderStringOnTop(
    renderContext, color, transparent, string, ScreenTextAnchor{pixel});
  renderBatch.addOneShot(textRenderer);
}

std::string formatFloat(const float value, const int precision = 2)
{
  auto stream = std::ostringstream{};
  stream << std::fixed << std::setprecision(precision) << value;
  return stream.str();
}

} // namespace

void PlayModeController::ContextDeleter::operator()(scs_context* context) const
{
  scs_shutdown(context);
}

PlayModeController::PlayModeController(
  MapDocument& document, gl::PerspectiveCamera& camera)
  : m_document{document}
  , m_camera{camera}
{
  m_clock.start();
}

PlayModeController::~PlayModeController() = default;

bool PlayModeController::active() const
{
  return m_active;
}

void PlayModeController::start(const bool fromCamera)
{
  if (m_active)
  {
    return;
  }

  // Recreate the context so preference changes (sv_airaccelerate) take effect; the
  // player state survives in m_savedState.
  auto config = scs_config{};
  scs_config_defaults(&config);
  config.movevars.airaccelerate = pref(Preferences::PlayModeAirAccelerate);
  config.movevars.accelerate = pref(Preferences::PlayModeAccelerate);
  config.movevars.friction = pref(Preferences::PlayModeFriction);
  config.movevars.stopspeed = pref(Preferences::PlayModeStopSpeed);
  config.movevars.gravity = pref(Preferences::PlayModeGravity);
  config.movevars.maxspeed = pref(Preferences::PlayModeMaxSpeed);
  config.movevars.edgefriction = pref(Preferences::PlayModeEdgeFriction);
  config.movevars.stepsize = pref(Preferences::PlayModeStepSize);
  config.clientmaxspeed = pref(Preferences::PlayModeWeaponSpeed);

  // The game's sounds come from the map's game file system, which changes with the game
  // path, so the player is recreated with the context.
  m_context.reset();
  m_sound = std::make_unique<PlayModeSound>(m_document.map().gameFileSystem());
  m_sound->setVolume(pref(Preferences::PlayModeSoundVolume));
  config.sound_user = m_sound.get();
  config.sound_fn = [](
                      void* user,
                      const int channel,
                      const char* sample,
                      const float volume,
                      float /* attenuation */,
                      int /* flags */,
                      int /* pitch */) {
    static_cast<PlayModeSound*>(user)->play(channel, sample, volume);
  };
  m_context.reset(scs_init(&config));
  if (!m_context)
  {
    m_document.logger().error() << "Play mode: could not create the physics context";
    return;
  }

  if (!rebuildWorld())
  {
    m_context.reset();
    return;
  }

  placePlayer(fromCamera);

  const auto now = nowMs();
  // The client sends usercmds of 1000 / fps_max milliseconds (truncated), which changes
  // the physics exactly like in the game.
  const auto frameRate = std::clamp(pref(Preferences::PlayModeFrameRate), 4, 1000);
  m_timeline.reset(now, m_viewAngles, static_cast<uint8_t>(1000 / frameRate));
  m_timeline.setYawSpeed(pref(Preferences::PlayModeYawSpeed));
  m_lastEventMs = now;
  m_previousEye = m_currentEye = eyePosition();
  m_active = true;

  auto state = scs_player_state{};
  scs_get_state(m_context.get(), &state);
  m_showPre.reset(state);
  m_fallShownMs = -1.0e9;
  m_fallShownCount = state.fall_count;
  std::ranges::fill(m_keyLastDownMs, 0.0);
  m_document.logger().info() << "Play mode: " << m_statusMessage;

  // See the world through the game's eyes: CS's field of view and no editor zoom.
  m_editorFov = m_camera.fov();
  m_editorZoom = m_camera.zoom();
  m_camera.setFov(pref(Preferences::PlayModeFov));
  m_camera.setZoom(1.0f);

  syncCamera(1.0f);
}

void PlayModeController::stop()
{
  if (!m_active)
  {
    return;
  }

  auto state = scs_player_state{};
  scs_get_state(m_context.get(), &state);
  m_savedState = state;

  m_camera.setFov(m_editorFov);
  m_camera.setZoom(m_editorZoom);
  m_sound->stopAll();
  m_active = false;
}

void PlayModeController::keyDown(const QKeyEvent& event)
{
  handleKey(event, true);
}

void PlayModeController::keyUp(const QKeyEvent& event)
{
  handleKey(event, false);
}

void PlayModeController::releaseAllKeys()
{
  m_timeline.releaseAllButtons(std::max(nowMs(), m_lastEventMs));
}

bool PlayModeController::handleKey(const QKeyEvent& event, const bool down)
{
  if (event.isAutoRepeat())
  {
    return true;
  }

  // Keys are matched by key code alone, so a key still works while another modifier is
  // held (e.g. jumping while ducking with Ctrl).
  const auto key = event.key();
  const auto bindings = {
    std::pair{pref(Preferences::PlayModeKeyForward), PlayModeButton::Forward},
    std::pair{pref(Preferences::PlayModeKeyBack), PlayModeButton::Back},
    std::pair{pref(Preferences::PlayModeKeyMoveLeft), PlayModeButton::MoveLeft},
    std::pair{pref(Preferences::PlayModeKeyMoveRight), PlayModeButton::MoveRight},
    std::pair{pref(Preferences::PlayModeKeyJump), PlayModeButton::Jump},
    std::pair{pref(Preferences::PlayModeKeyDuck), PlayModeButton::Duck},
    std::pair{pref(Preferences::PlayModeKeyWalk), PlayModeButton::Walk},
    std::pair{pref(Preferences::PlayModeKeyUse), PlayModeButton::Use},
    std::pair{pref(Preferences::PlayModeKeyTurnLeft), PlayModeButton::TurnLeft},
    std::pair{pref(Preferences::PlayModeKeyTurnRight), PlayModeButton::TurnRight},
  };
  const auto binding = std::ranges::find_if(
    bindings, [&](const auto& candidate) { return candidate.first == key; });
  if (binding == bindings.end())
  {
    return false;
  }
  const auto button = binding->second;

  m_timeline.button(
    eventTime(static_cast<double>(event.timestamp()), EventSource::Qt),
    button,
    event.key(),
    down);
  return true;
}

void PlayModeController::look(const QInputEvent& event, const float dx, const float dy)
{
  turn(
    eventTime(static_cast<double>(event.timestamp()), EventSource::Qt),
    static_cast<double>(dx),
    static_cast<double>(dy));
}

void PlayModeController::rawLook(
  const double dx, const double dy, const double timestampMs)
{
  turn(eventTime(timestampMs, EventSource::RawMouse), dx, dy);
}

void PlayModeController::setInputDescription(std::string description)
{
  m_document.logger().info() << "Play mode: mouse input is " << description;
}

void PlayModeController::turn(const double timeMs, const double dx, const double dy)
{
  // IN_MouseMove: viewangles[YAW] -= m_yaw * sensitivity * mx, pitch += m_pitch * my
  const auto scale =
    static_cast<double>(pref(Preferences::PlayModeMouseSensitivity) * MouseYawDegrees);
  // m_pitch is negative when the mouse is inverted
  const auto pitchScale = pref(Preferences::PlayModeInvertMouse) ? -scale : scale;
  m_timeline.look(
    timeMs, static_cast<float>(dy * pitchScale), static_cast<float>(-dx * scale));
}

void PlayModeController::wheel(const QInputEvent& event, const bool up)
{
  const auto action =
    pref(up ? Preferences::PlayModeWheelUp : Preferences::PlayModeWheelDown);
  if (
    action == Preferences::PlayModeWheelJump || action == Preferences::PlayModeWheelDuck)
  {
    m_timeline.tap(
      eventTime(static_cast<double>(event.timestamp()), EventSource::Qt),
      action == Preferences::PlayModeWheelJump ? PlayModeButton::Jump
                                               : PlayModeButton::Duck);
  }
}

void PlayModeController::update()
{
  if (!m_active)
  {
    return;
  }

  // Replay all input up to now, one 10 ms tick at a time, tracking the last two eye
  // positions for interpolation.
  const auto now = nowMs();
  while (m_timeline.advance(m_context.get(), now, 1) == 1)
  {
    m_previousEye = m_currentEye;
    m_currentEye = eyePosition();

    auto state = scs_player_state{};
    scs_get_state(m_context.get(), &state);
    m_showPre.tick(m_timeline.lastCommand(), state, now);

    if (state.fall_count != m_fallShownCount)
    {
      m_fallShownCount = state.fall_count;
      m_fallShownMs = now;
    }

    const auto buttons = m_timeline.lastCommand().buttons;
    for (size_t i = 0; i < ShowKeys.size(); ++i)
    {
      if (buttons & ShowKeys[i].button)
      {
        m_keyLastDownMs[i] = now;
      }
    }
  }

  // Render between the last two ticks so 100 Hz physics looks smooth at any frame rate.
  syncCamera(m_timeline.tickFraction(now));
}

double PlayModeController::nowMs() const
{
  return static_cast<double>(m_clock.nsecsElapsed()) / 1.0e6;
}

double PlayModeController::eventTime(const double timestampMs, const EventSource source)
{
  // Input events carry a timestamp from their source's own clock (the windowing system
  // for Qt events, the compositor or X server for raw motion). Map it onto m_clock so
  // input lands in the tick in which it happened, even when the view only renders (and
  // thus processes events) every few ticks. Events reach us after they happened, so
  // (timestamp - receive time) never exceeds the true clock offset; its maximum is the
  // best estimate. Each source gets its own estimate because their clocks differ.
  constexpr auto MaxEventAgeMs = 250.0;

  auto& clockOffset = m_eventClockOffset[source == EventSource::Qt ? 0 : 1];
  const auto received = nowMs();
  auto time = received;
  if (timestampMs != 0.0)
  {
    const auto offset = timestampMs - received;
    if (!clockOffset || offset > *clockOffset)
    {
      clockOffset = offset;
    }
    time = timestampMs - *clockOffset;
    if (time < received - MaxEventAgeMs)
    {
      // The event clock jumped (or wrapped around): resynchronize.
      clockOffset = offset;
      time = received;
    }
  }

  // Keep the timeline ordered even if timestamps jitter.
  m_lastEventMs = std::max(time, m_lastEventMs);
  return m_lastEventMs;
}

void PlayModeController::render(
  render::RenderContext& renderContext, render::RenderBatch& renderBatch)
{
  if (!m_active)
  {
    return;
  }

  const auto& viewport = m_camera.viewport();
  const auto width = static_cast<float>(viewport.width);
  const auto height = static_cast<float>(viewport.height);

  if (pref(Preferences::PlayModeShowHud))
  {
    // KZ jump stats style: the speed below the crosshair and, after a takeoff, the
    // prestrafe speed above it (showpre), fading out after a moment. Screen positions
    // are measured from the bottom left.
    auto state = scs_player_state{};
    scs_get_state(m_context.get(), &state);
    const auto speed = std::sqrt(
      state.velocity[0] * state.velocity[0] + state.velocity[1] * state.velocity[1]);

    const auto speedPosition = vm::vec2f{width / 2.0f, height * 0.38f};
    renderHudText(
      renderContext,
      renderBatch,
      formatSpeed(speed),
      SpeedFontScale,
      speedPosition,
      1.0f,
      1.0f,
      1.0f);

    if (const auto& pre = m_showPre.pre())
    {
      const auto age = nowMs() - pre->timeMs;
      const auto alpha =
        static_cast<float>(std::clamp(1.0 - (age - PreHoldMs) / PreFadeMs, 0.0, 1.0));
      if (alpha > 0.0f)
      {
        // Jump and duck takeoffs look the same: just the speed, in green.
        const auto lineHeight =
          static_cast<float>(pref(Preferences::RendererFontSize)) * SpeedFontScale * 1.4f;
        renderHudText(
          renderContext,
          renderBatch,
          formatSpeed(pre->speed),
          PreFontScale,
          speedPosition + vm::vec2f{0.0f, lineHeight},
          0.2f,
          1.0f,
          0.2f,
          alpha);
      }
    }

    // showkeys below the speed: only pressed keys are drawn, in fixed places
    const auto now = nowMs();
    const auto keyLineHeight =
      static_cast<float>(pref(Preferences::RendererFontSize)) * KeysFontScale * 1.3f;
    const auto keysTop =
      speedPosition.y()
      - static_cast<float>(pref(Preferences::RendererFontSize)) * SpeedFontScale * 1.1f;
    for (size_t i = 0; i < ShowKeys.size(); ++i)
    {
      if (now - m_keyLastDownMs[i] <= KeyMinVisibleMs && m_keyLastDownMs[i] > 0.0)
      {
        const auto& key = ShowKeys[i];
        renderHudText(
          renderContext,
          renderBatch,
          key.label,
          KeysFontScale,
          vm::vec2f{
            speedPosition.x() + key.column * keyLineHeight * 1.2f,
            keysTop - key.row * keyLineHeight},
          1.0f,
          1.0f,
          1.0f);
      }
    }
  }

  if (pref(Preferences::PlayModeShowHud) && pref(Preferences::PlayModeShowFallDamage))
  {
    // Below showkeys: the damage of the last hard landing (the player never dies).
    if (
      const auto alpha = fadeAlpha(nowMs() - m_fallShownMs, FallHoldMs, FallFadeMs);
      alpha > 0.0f)
    {
      const auto fontSize = static_cast<float>(pref(Preferences::RendererFontSize));
      auto state = scs_player_state{};
      scs_get_state(m_context.get(), &state);
      renderHudText(
        renderContext,
        renderBatch,
        "-" + std::to_string(static_cast<int>(state.last_fall_damage)) + " HP",
        FallFontScale,
        vm::vec2f{
          width / 2.0f,
          height * 0.38f - fontSize * (SpeedFontScale * 1.1f + KeysFontScale * 4.4f)},
        1.0f,
        0.4f,
        0.4f,
        alpha);
    }
  }

  if (pref(Preferences::PlayModeShowCrosshair))
  {
    // Crosshair, drawn on top of everything.
    const auto cx = width / 2.0f;
    const auto cy = height / 2.0f;
    constexpr auto Size = 6.0f;
    constexpr auto Depth = 0.5f;
    auto renderService = render::RenderService{renderContext, renderBatch};
    renderService.setShowOccludedObjects();
    renderService.setForegroundColor(RgbaF{0.2f, 1.0f, 0.2f, 1.0f});
    renderService.renderLine(
      m_camera.unproject(cx - Size, cy, Depth), m_camera.unproject(cx + Size, cy, Depth));
    renderService.renderLine(
      m_camera.unproject(cx, cy - Size, Depth), m_camera.unproject(cx, cy + Size, Depth));
  }
}

bool PlayModeController::rebuildWorld()
{
  m_world = buildPlayModeWorld(
    m_document.map(),
    {
      .compiler = std::clamp(
        pref(Preferences::PlayModeCompiler),
        Preferences::PlayModeCompilerVhlt,
        Preferences::PlayModeCompilerZhlt),
      .clipType = std::clamp(
        pref(Preferences::PlayModeClipType),
        Preferences::PlayModeClipTypeSmallest,
        Preferences::PlayModeClipTypeLegacy),
      .useMapClipType = pref(Preferences::PlayModeUseMapClipType),
    });
  if (!m_world.world)
  {
    m_document.logger().error() << "Play mode: " << m_world.error;
    return false;
  }

  scs_set_world(m_context.get(), m_world.world.get());
  m_document.logger().info()
    << "Play mode: compiled " << m_world.worldBrushCount << " world brushes and "
    << m_world.entityCount << " brush entities in "
    << formatFloat(static_cast<float>(m_world.buildMilliseconds), 1) << " ms ("
    << m_world.skippedBrushCount << " skipped), "
    << (pref(Preferences::PlayModeCompiler) == Preferences::PlayModeCompilerZhlt ? "ZHLT"
                                                                                 : "VHLT")
    << " cliptype " << clipTypeName(m_world.clipType)
    << (m_world.clipTypeFromMap ? " (info_compile_parameters)" : "");
  return true;
}

bool PlayModeController::placePlayer(const bool fromCamera)
{
  // Resume exactly where the last session ended (velocity, duck and jump state too).
  if (!fromCamera && m_savedState)
  {
    scs_set_state(m_context.get(), &*m_savedState);
    if (
      scs_position_is_free(
        m_context.get(), m_savedState->origin, usehullFor(*m_savedState)))
    {
      m_viewAngles = vm::vec3f{
        m_savedState->v_angle[0], m_savedState->v_angle[1], m_savedState->v_angle[2]};
      m_statusMessage = "Resumed";
      return true;
    }
  }

  // Spawn with the eyes at the camera, then step upwards if that position is blocked.
  const auto cameraAngles = anglesFromDirection(m_camera.direction());
  const auto cameraOrigin =
    m_camera.position() - vm::vec3f{0, 0, PlayerViewHeight + 1.0f};
  for (auto dz = 0.0f; dz <= UnstuckRange; dz += UnstuckStep)
  {
    if (tryPlaceAt(cameraOrigin + vm::vec3f{0, 0, dz}, cameraAngles))
    {
      m_statusMessage = "Spawned at camera";
      return true;
    }
  }

  for (const auto& spawn : m_world.spawnPoints)
  {
    if (tryPlaceAt(spawn.origin, spawn.angles))
    {
      m_statusMessage = "Camera blocked, spawned at " + spawn.classname;
      return true;
    }
  }

  tryPlaceAt(cameraOrigin, cameraAngles);
  m_statusMessage = "STUCK: no free spawn position";
  return false;
}

bool PlayModeController::tryPlaceAt(const vm::vec3f& origin, const vm::vec3f& angles)
{
  const float originArray[3] = {origin.x(), origin.y(), origin.z()};
  const float anglesArray[3] = {angles.x(), angles.y(), 0.0f};
  scs_spawn(m_context.get(), originArray, anglesArray);
  m_viewAngles = vm::vec3f{angles.x(), angles.y(), 0.0f};

  auto state = scs_player_state{};
  scs_get_state(m_context.get(), &state);
  return scs_position_is_free(m_context.get(), state.origin, usehullFor(state)) != 0;
}

vm::vec3f PlayModeController::eyePosition() const
{
  auto state = scs_player_state{};
  scs_get_state(m_context.get(), &state);
  return {
    state.origin[0] + state.view_ofs[0],
    state.origin[1] + state.view_ofs[1],
    state.origin[2] + state.view_ofs[2]};
}

void PlayModeController::syncCamera(const float alpha)
{
  m_camera.moveTo(vm::mix(m_previousEye, m_currentEye, vm::vec3f{alpha, alpha, alpha}));
  m_camera.setDirection(
    forwardFromAngles(m_active ? m_timeline.latestViewAngles() : m_viewAngles),
    vm::vec3f{0, 0, 1});
}

} // namespace tb::ui
