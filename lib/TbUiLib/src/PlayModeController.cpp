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

#include <QFutureWatcher>
#include <QKeyEvent>
#include <QtConcurrent/QtConcurrent>

#include "base/Logger.h"
#include "base/PreferenceManager.h"
#include "gl/AttrString.h"
#include "gl/FontDescriptor.h"
#include "gl/PerspectiveCamera.h"
#include "mdl/GameFileSystem.h"
#include "mdl/Map.h"
#include "prefs/Preferences.h"
#include "render/RenderBatch.h"
#include "render/RenderService.h"
#include "render/RenderTiming.h"
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
static_assert(Preferences::PlayModeQtKeyP == Qt::Key_P);
static_assert(Preferences::PlayModeQtKeyL == Qt::Key_L);
static_assert(Preferences::PlayModeQtKeyC == Qt::Key_C);
static_assert(Preferences::PlayModeQtKeyZ == Qt::Key_Z);

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

constexpr auto SpeedFontScale = 2.5f;
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
  bool m_leftAligned;

public:
  explicit ScreenTextAnchor(const vm::vec2f& position, const bool leftAligned = false)
    : m_position{position}
    , m_leftAligned{leftAligned}
  {
  }

  vm::vec3f offset(const gl::Camera&, const vm::vec2f& size) const override
  {
    return vm::vec3f{
      std::round(m_position.x() - (m_leftAligned ? 0.0f : size.x() / 2.0f)),
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
  const float alpha = 1.0f,
  const bool leftAligned = false)
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
    ScreenTextAnchor{pixel + vm::vec2f{shadowOffset, -shadowOffset}, leftAligned});
  textRenderer->renderStringOnTop(
    renderContext, color, transparent, string, ScreenTextAnchor{pixel, leftAligned});
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
  , m_worldBuildWatcher{std::make_unique<QFutureWatcher<WorldBuildResult>>()}
{
  m_clock.start();
  QObject::connect(
    m_worldBuildWatcher.get(), &QFutureWatcher<WorldBuildResult>::finished, [this]() {
      finishStarting();
    });
}

PlayModeController::~PlayModeController()
{
  if (m_worldBuildWatcher->isRunning())
  {
    m_worldBuildWatcher->waitForFinished();
  }
}

bool PlayModeController::active() const
{
  return m_active;
}

void PlayModeController::start(
  const bool fromCamera, std::function<void(bool)> completion)
{
  if (m_active || m_starting)
  {
    return;
  }

  m_startCompletion = std::move(completion);
  m_startFromCamera = fromCamera;

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
  config.noclip_speed_boost = 3.0f;

  // The game's sounds come from the map's game file system, which changes with the game
  // path, so the player is recreated with the context.
  m_context.reset();
  m_sound = std::make_unique<PlayModeSound>(m_document.map().gameFileSystem());
  m_sound->setVolume(pref(Preferences::PlayModeSoundVolume));
  m_sound->preload();
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
    if (m_startCompletion)
    {
      m_startCompletion(false);
    }
    return;
  }

  const auto compiler = std::clamp(
    pref(Preferences::PlayModeCompiler),
    Preferences::PlayModeCompilerVhlt,
    Preferences::PlayModeCompilerZhlt);
  const auto clipType = std::clamp(
    pref(Preferences::PlayModeClipType),
    Preferences::PlayModeClipTypeSmallest,
    Preferences::PlayModeClipTypeLegacy);
  const auto* map = &m_document.map();
  const auto cacheValid = m_world.world && m_worldMap == map
                          && m_worldModificationCount == map->modificationCount()
                          && m_worldCompiler == compiler && m_worldClipType == clipType;
  m_starting = true;
  m_startUsingCachedWorld = cacheValid;
  if (cacheValid)
  {
    finishStarting();
    return;
  }

  m_worldCompiler = compiler;
  m_worldClipType = clipType;
  m_worldBuildWatcher->setFuture(
    QtConcurrent::run([map, compiler, clipType]() -> WorldBuildResult {
      return std::make_shared<PlayModeWorld>(
        buildPlayModeWorld(*map, {.compiler = compiler, .clipType = clipType}));
    }));
}

bool PlayModeController::starting() const
{
  return m_starting;
}

void PlayModeController::finishStarting()
{
  if (!m_starting)
  {
    return;
  }

  m_starting = false;
  const auto usingCachedWorld = m_startUsingCachedWorld;
  if (!usingCachedWorld)
  {
    const auto world = m_worldBuildWatcher->result();
    if (!world || !world->world)
    {
      if (world && !world->error.empty())
      {
        m_document.logger().error() << "Play mode: " << world->error;
      }
      m_context.reset();
      if (m_startCompletion)
      {
        m_startCompletion(false);
      }
      m_startUsingCachedWorld = false;
      return;
    }

    m_world = std::move(*world);
    for (const auto& warning : m_world.warnings)
    {
      m_document.logger().warn() << "Play mode: " << warning;
    }
    m_worldMap = &m_document.map();
    m_worldModificationCount = m_document.map().modificationCount();
  }
  m_startUsingCachedWorld = false;

  if (!m_world.world)
  {
    m_context.reset();
    if (m_startCompletion)
    {
      m_startCompletion(false);
    }
    return;
  }

  scs_set_world(m_context.get(), m_world.world.get());
  if (!usingCachedWorld)
  {
    m_document.logger().info() << "Play mode: compiled " << m_world.worldBrushCount
                               << " world brushes and " << m_world.entityCount
                               << " brush entities, " << m_world.teleportCount
                               << " teleport triggers in "
                               << formatFloat(
                                    static_cast<float>(m_world.buildMilliseconds), 1)
                               << " ms (" << m_world.skippedBrushCount << " skipped), "
                               << (pref(Preferences::PlayModeCompiler)
                                       == Preferences::PlayModeCompilerZhlt
                                     ? "ZHLT"
                                     : "VHLT")
                               << " cliptype " << clipTypeName(m_world.clipType);
  }

  placePlayer(m_startFromCamera);

  const auto now = nowMs();
  const auto frameRate = std::clamp(pref(Preferences::PlayModeFrameRate), 4, 1000);
  m_timeline.reset(now, m_viewAngles, static_cast<uint8_t>(1000 / frameRate));
  m_timeline.setYawSpeed(pref(Preferences::PlayModeYawSpeed));
  m_timeline.setHookSpeed(pref(Preferences::PlayModeHookSpeed));
  m_timeline.setGravity(pref(Preferences::PlayModeGravity));
  m_lastEventMs = now;
  m_previousEye = m_currentEye = eyePosition();
  m_active = true;

  auto state = scs_player_state{};
  scs_get_state(m_context.get(), &state);
  m_jumpGuide = {};
  m_fallShownMs = -1.0e9;
  m_fallShownCount = state.fall_count;
  m_teleportCount = state.teleport_count;
  std::ranges::fill(m_keyLastDownMs, 0.0);
  m_document.logger().info() << "Play mode: " << m_statusMessage;

  m_editorFov = m_camera.fov();
  m_editorZoom = m_camera.zoom();
  m_camera.setFov(pref(Preferences::PlayModeFov));
  m_camera.setZoom(1.0f);
  syncCamera(1.0f);

  if (m_startCompletion)
  {
    auto completion = std::move(m_startCompletion);
    completion(true);
  }
}

void PlayModeController::stop()
{
  if (!m_active)
  {
    return;
  }

  m_timeline.clearTravelInput();
  auto state = scs_player_state{};
  scs_get_state(m_context.get(), &state);
  m_savedState = state;

  m_camera.setFov(m_editorFov);
  m_camera.setZoom(m_editorZoom);
  m_sound->stopAll();
  m_active = false;
  m_jumpGuide = {};
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
  if (key == pref(Preferences::PlayModeKeyHook))
  {
    m_timeline.hook(
      eventTime(static_cast<double>(event.timestamp()), EventSource::Qt), down);
    return true;
  }
  if (key == pref(Preferences::PlayModeKeyNoclip))
  {
    if (down)
      m_timeline.toggleNoclip(
        eventTime(static_cast<double>(event.timestamp()), EventSource::Qt));
    return true;
  }
  if (key == pref(Preferences::PlayModeKeySavePoint))
  {
    if (down)
    {
      saveCheckpoint();
    }
    return true;
  }
  if (key == pref(Preferences::PlayModeKeyLoadPoint))
  {
    if (down)
    {
      loadCheckpoint();
    }
    return true;
  }

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

void PlayModeController::saveCheckpoint()
{
  auto state = scs_player_state{};
  scs_get_state(m_context.get(), &state);
  if (state.movetype == SCS_MOVETYPE_NOCLIP || m_timeline.travel().hookAnchor())
  {
    m_document.logger().info()
      << "Play mode: release travel aids before saving a checkpoint";
    return;
  }
  if (std::abs(state.velocity[2]) > 0.01f)
  {
    m_document.logger().info() << "Play mode: checkpoint requires zero vertical speed";
    return;
  }

  m_checkpoint = Checkpoint{
    vm::vec3f{state.origin[0], state.origin[1], state.origin[2]},
    m_timeline.latestViewAngles()};
  m_document.logger().info() << "Play mode: checkpoint saved";
}

void PlayModeController::loadCheckpoint()
{
  if (!m_checkpoint)
  {
    m_document.logger().info() << "Play mode: no checkpoint saved";
    return;
  }

  auto current = scs_player_state{};
  scs_get_state(m_context.get(), &current);
  auto state = current;
  for (size_t i = 0; i < 3; ++i)
  {
    state.origin[i] = m_checkpoint->origin[i];
    state.velocity[i] = 0.0f;
    state.basevelocity[i] = 0.0f;
  }
  state.flags |= SCS_FL_ONGROUND;
  state.ground_entity = 0;
  state.gravity = 1.0f;
  state.fall_velocity = 0.0f;
  state.server_fall_velocity = 0.0f;
  state.last_fall_damage = 0.0f;
  state.last_fall_speed = 0.0f;
  state.teleport_time = 0.0f;
  state.movetype = SCS_MOVETYPE_WALK;

  if (!scs_position_is_free(m_context.get(), state.origin, usehullFor(state)))
  {
    m_document.logger().info() << "Play mode: checkpoint position is blocked";
    return;
  }

  if (pref(Preferences::PlayModeRestoreCheckpointView))
  {
    for (size_t i = 0; i < 3; ++i)
    {
      state.v_angle[i] = m_checkpoint->viewAngles[i];
    }
  }
  m_timeline.clearTravelInput();
  scs_set_state(m_context.get(), &state);
  m_timeline.interruptJumpStats();
  if (pref(Preferences::PlayModeRestoreCheckpointView))
  {
    m_viewAngles = m_checkpoint->viewAngles;
    m_timeline.setViewAngles(m_checkpoint->viewAngles);
  }
  m_previousEye = m_currentEye = eyePosition();
  m_fallShownMs = -1.0e9;
  syncCamera(1.0f);
  m_document.logger().info() << "Play mode: checkpoint loaded";
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
  m_timeline.look(
    timeMs, static_cast<float>(dy * scale), static_cast<float>(-dx * scale));
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
  const auto timing = render::RenderTimingScope{"cpu.play.update"};
  if (!m_active)
  {
    return;
  }

  // Replay all input up to now, one 10 ms tick at a time, tracking the last two eye
  // positions for interpolation.
  const auto now = nowMs();
  while (m_timeline.advance(m_context.get(), now, 1) == 1)
  {
    render::RenderTiming::count("play.physics_ticks", 1);
    m_previousEye = m_currentEye;
    m_currentEye = eyePosition();

    auto state = scs_player_state{};
    scs_get_state(m_context.get(), &state);
    if (state.teleport_count != m_teleportCount)
    {
      m_teleportCount = state.teleport_count;
      m_previousEye = m_currentEye;
      m_jumpGuide = {};
    }

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

  const auto showSpan = pref(Preferences::PlayModeShowJumpSpan);
  auto state = scs_player_state{};
  scs_get_state(m_context.get(), &state);
  if (
    !showSpan || state.ground_entity == -1 || !(state.flags & SCS_FL_ONGROUND)
    || state.movetype != SCS_MOVETYPE_WALK || state.waterlevel >= 2
    || state.teleport_time != 0.0f)
  {
    m_jumpGuide = {};
  }
  else
  {
    m_jumpGuide = measurePlayModeJumpGuide(
      m_context.get(),
      m_world.jumpSurfaces,
      state,
      forwardFromAngles({0, m_timeline.latestViewAngles().y(), 0}));
  }
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
  const auto timing = render::RenderTimingScope{"cpu.play.hud_build"};
  if (!m_active)
  {
    return;
  }

  const auto& viewport = m_camera.viewport();
  const auto width = static_cast<float>(viewport.width);
  const auto height = static_cast<float>(viewport.height);

  renderPlayModeJumpGuide(m_jumpGuide, renderContext, renderBatch);

  if (const auto& anchor = m_timeline.travel().hookAnchor())
  {
    auto service = render::RenderService{renderContext, renderBatch};
    service.setHideOccludedObjects();
    service.setLineWidth(2.0f);
    service.setForegroundColor(RgbF{0.2f, 0.8f, 1.0f});
    service.renderLine(
      m_camera.position() + m_camera.direction() * 8.0f - m_camera.up() * 6.0f, *anchor);
  }
  auto travelState = scs_player_state{};
  scs_get_state(m_context.get(), &travelState);
  if (travelState.movetype == SCS_MOVETYPE_NOCLIP)
  {
    const auto text = m_timeline.travel().noclipExitBlocked()
                        ? "NOCLIP: move out of solid geometry to turn off"
                        : "NOCLIP";
    renderHudText(
      renderContext,
      renderBatch,
      text,
      1.3f,
      {width / 2.0f, height - 32.0f},
      0.2f,
      0.8f,
      1.0f);
  }

  if (pref(Preferences::PlayModeShowHud))
  {
    // Keep one live speed readout below the crosshair. Screen positions are measured
    // from the bottom left.
    auto state = scs_player_state{};
    scs_get_state(m_context.get(), &state);

    const auto speedPosition = vm::vec2f{width / 2.0f, height * 0.38f};

    if (pref(Preferences::PlayModeShowJumpStats))
    {
      const auto& stats = m_timeline.jumpStats();
      if (const auto& takeoff = stats.takeoff();
          takeoff && stats.timeMs() - takeoff->timeMs < 1000.0
          && takeoff->technique != PlayModeJumpStats::Technique::Unsupported)
      {
        const auto preName = [&]() -> std::string_view {
          switch (takeoff->technique)
          {
          case PlayModeJumpStats::Technique::Bhop:
            return "bhop";
          case PlayModeJumpStats::Technique::StandupBhop:
            return "sbj";
          case PlayModeJumpStats::Technique::DuckBhop:
            return "duckbhop";
          case PlayModeJumpStats::Technique::CountJump:
            return "dd";
          case PlayModeJumpStats::Technique::StandupCountJump:
            return "dd";
          case PlayModeJumpStats::Technique::DoubleCountJump:
            return "dd";
          case PlayModeJumpStats::Technique::MultiCountJump:
            return "dd";
          default:
            return PlayModeJumpStats::name(takeoff->technique);
          }
        };
        const auto isLongJump =
          takeoff->technique == PlayModeJumpStats::Technique::LongJump
          || takeoff->technique == PlayModeJumpStats::Technique::HighJump;
        const auto lines = isLongJump
                             ? std::array{
                                 std::string{takeoff->technique
                                                 == PlayModeJumpStats::Technique::HighJump
                                               ? "hj pre:"
                                               : "lj pre:"},
                                 formatFloat(takeoff->postSpeed, 3),
                                 std::string{}}
                             : std::array{
                                 std::string{preName()}
                                   + " " + std::to_string(takeoff->fog) + " FOG:",
                                 formatFloat(takeoff->preSpeed, 3),
                                 formatFloat(takeoff->postSpeed, 3)};
        const auto lineCount = isLongJump ? 2u : 3u;
        const auto lineHeight =
          static_cast<float>(pref(Preferences::RendererFontSize)) * 1.5f;
        for (size_t i = 0; i < lineCount; ++i)
          renderHudText(
            renderContext,
            renderBatch,
            lines[i],
            1.15f,
            {width * 0.5f, height * 0.40f - static_cast<float>(i) * lineHeight},
            0.35f,
            0.95f,
            1.0f);
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

  if (pref(Preferences::PlayModeShowHud) && pref(Preferences::PlayModeShowJumpStats))
  {
    const auto& stats = m_timeline.jumpStats();
    const auto* result = stats.result() ? &*stats.result() : nullptr;
    const auto hold =
      static_cast<double>(pref(Preferences::PlayModeJumpStatsDuration)) * 1000.0;
    const auto alpha =
      result ? fadeAlpha(stats.timeMs() - result->timeMs, hold, 500.0) : 0.0f;
    if (
      result && !stats.current() && alpha > 0.0f
      && PlayModeJumpStats::visible(*result, pref(Preferences::PlayModeShowFailedJumps)))
    {
      const auto fontSize = static_cast<float>(pref(Preferences::RendererFontSize));
      const auto y = std::max(fontSize * 6.5f, height * 0.13f);
      const auto line = fontSize * 1.55f;
      const auto title = std::string{PlayModeJumpStats::name(result->technique)};
      const auto hasFog = result->technique != PlayModeJumpStats::Technique::LongJump
                          && result->technique != PlayModeJumpStats::Technique::HighJump;
      const auto lines = std::array{
        title,
        "Distance: " + formatFloat(result->distance, 3),
        "Prestrafe: " + formatFloat(result->preSpeed, 3)
          + (hasFog ? "(" + std::to_string(result->takeoff.fog) + ")" : ""),
        "Strafes: " + std::to_string(result->strafes.size())};
      for (size_t i = 0; i < lines.size(); ++i)
        renderHudText(
          renderContext,
          renderBatch,
          lines[i],
          i == 1 ? 1.3f : 1.2f,
          {width * 0.5f, y + static_cast<float>(3 - i) * line},
          result->failed ? 1.0f : 0.35f,
          result->failed ? 70.0f / 255.0f : 0.95f,
          result->failed ? 120.0f / 255.0f : 1.0f,
          alpha);
    }

    const auto* analysis = result && !stats.current() && alpha > 0.0f
                               && PlayModeJumpStats::visible(
                                 *result, pref(Preferences::PlayModeShowFailedJumps))
                             ? result
                             : nullptr;
    if (analysis && pref(Preferences::PlayModeShowStrafes))
    {
      // Independent columns keep proportional fonts aligned.
      const auto font = static_cast<float>(pref(Preferences::RendererFontSize));
      const auto scale = std::min(1.2f, width * 0.22f / (font * 22.0f));
      const auto rowHeight = font * scale * 1.7f;
      const auto tableX = width * 0.77f;
      const auto tableY = height * 0.78f;
      const auto tableAlpha = alpha;
      const auto maxRows =
        static_cast<size_t>(std::clamp(height * 0.4f / rowHeight - 2.0f, 1.0f, 10.0f));
      const auto first =
        analysis->strafes.size() > maxRows ? analysis->strafes.size() - maxRows : 0;
      const auto columns = std::array{0.0f, 0.15f, 0.38f, 0.60f, 0.78f};
      const auto fontSize = static_cast<size_t>(std::max(1.0f, std::round(font * scale)));
      auto* tableRenderer = new render::TextRenderer{
        gl::FontDescriptor{pref(Preferences::RendererFontPath), fontSize}};
      const auto drawRow = [&](const std::array<std::string, 5>& cells, const float y) {
        for (size_t i = 0; i < cells.size(); ++i)
        {
          const auto position =
            vm::round(vm::vec2f{tableX + columns[i] * width * 0.22f, y});
          const auto string = gl::AttrString{cells[i]};
          tableRenderer->renderStringOnTop(
            renderContext,
            RgbaF{0, 0, 0, tableAlpha * 0.8f},
            RgbaF{0, 0, 0, 0},
            string,
            ScreenTextAnchor{position + vm::vec2f{1, -1}, true});
          tableRenderer->renderStringOnTop(
            renderContext,
            result->failed ? RgbaF{1.0f, 70.0f / 255.0f, 120.0f / 255.0f, tableAlpha}
                           : RgbaF{0.35f, 0.95f, 1.0f, tableAlpha},
            RgbaF{0, 0, 0, 0},
            string,
            ScreenTextAnchor{position, true});
        }
      };
      renderBatch.addOneShot(tableRenderer);
      for (size_t i = first; i < analysis->strafes.size(); ++i)
      {
        const auto& strafe = analysis->strafes[i];
        drawRow(
          {std::string(1, strafe.key),
           formatFloat(strafe.gain, 1),
           formatFloat(strafe.loss, 1),
           std::to_string(strafe.frames),
           formatFloat(strafe.efficiency(), 0) + "%"},
          tableY - static_cast<float>(i - first) * rowHeight);
      }
      const auto footer = analysis->truncated ? "..."
                          : first             ? "+" + std::to_string(first) + " earlier"
                          : analysis->duckFrames
                            ? std::to_string(analysis->duckFrames) + " duck frames"
                            : "noduck";
      renderHudText(
        renderContext,
        renderBatch,
        footer,
        scale,
        {tableX,
         tableY
           - static_cast<float>(std::min(maxRows, analysis->strafes.size()) + 1)
               * rowHeight},
        0.35f,
        0.95f,
        1.0f,
        tableAlpha,
        true);
    }
  }

  if (pref(Preferences::PlayModeShowHud) && pref(Preferences::PlayModeShowFallDamage))
  {
    // Below showkeys: the damage of the last hard landing (the player never dies).
    if (const auto alpha = fadeAlpha(nowMs() - m_fallShownMs, FallHoldMs, FallFadeMs);
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

bool PlayModeController::placePlayer(const bool fromCamera)
{
  // Resume exactly where the last session ended (velocity, duck and jump state too).
  if (!fromCamera && m_savedState)
  {
    scs_set_state(m_context.get(), &*m_savedState);
    if (
      m_savedState->movetype == SCS_MOVETYPE_NOCLIP
      || scs_position_is_free(
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
