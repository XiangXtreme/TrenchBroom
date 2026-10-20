/*
 Copyright (C) 2010 Kristian Duske

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

#include "base/Color.h"
#include "base/KeySequence.h"
#include "base/Preference.h"

#include "vm/util.h"

#include <filesystem>
#include <string>
#include <vector>

namespace tb::Preferences
{

// Must be set to false for tests, see TestPreferenceManager::initialize
inline auto AskForAutoUpdates = Preference<bool>{"updater/Ask for auto updates", true};
inline auto AutoCheckForUpdates =
  Preference<bool>{"updater/Check for updates automatically", false};
inline auto IncludePreReleaseUpdates =
  Preference<bool>{"updater/Include pre-releases", false};
inline auto EnableDraftReleaseUpdates = Preference<bool>{
  "updater/Enable draft releases", false, PreferencePersistencePolicy::Transient};
inline auto IncludeDraftReleaseUpdates = Preference<bool>{
  "updater/Include draft releases", false, PreferencePersistencePolicy::Transient};

inline auto MapViewLayout = Preference<int>{"Views/Map view layout", 0};

inline const auto SystemTheme = std::string{"builtin.system"};
inline const auto LightTheme = std::string{"builtin.light"};
inline const auto DarkTheme = std::string{"builtin.dark"};
inline const auto BlenderTheme = std::string{"builtin.blender"};
inline auto Theme = Preference<std::string>{"Theme", SystemTheme};
inline constexpr auto MinPythonConsoleFontSize = 8;
inline constexpr auto MaxPythonConsoleFontSize = 32;
inline auto PythonConsoleFontFamily =
  Preference<std::string>{"Python Console/Font family", ""};
inline auto PythonConsoleFontSize = Preference<int>{"Python Console/Font size", 11};
inline const auto EnglishLanguage = std::string{"English"};
inline const auto ChineseLanguage = std::string{"Chinese"};
inline auto Language = Preference<std::string>{"Language", EnglishLanguage};

inline const std::string& languageEnglish()
{
  return EnglishLanguage;
}

inline const std::string& languageChinese()
{
  return ChineseLanguage;
}

inline auto DefaultPluginPaths =
  Preference<std::string>{"Python/Default plugin paths", ""};
inline auto PythonPluginDirectories =
  Preference<std::string>{"Python/Plugin directories", ""};
inline auto PieMenuAction = Preference<std::string>{"Controls/Pie menu actions", ""};
inline auto PrefixWorldspawnHeaderOnCopy =
  Preference<bool>{"Controls/Prefix worldspawn header on copy", false};
inline auto Enable2DBoxSelection =
  Preference<bool>{"Controls/Enable 2D box selection", false};

inline auto ShowAxes = Preference<bool>{"render/Show axes", true};
inline auto SoftMapBoundsColor =
  Preference<Color>{"render/Colors/Soft map bounds color", RgbB{241, 125, 37}};
inline auto BackgroundColor =
  Preference<Color>{"render/Colors/Background", RgbB{38, 38, 38}};
inline auto AxisLength = Preference<float>{"render/Axis length", 128.0f};
inline auto XAxisColor = Preference<Color>{
  "render/Colors/X axis",
  RgbaF{1.0f, 0.24f, 0.0f, 0.7f},
  PreferencePersistencePolicy::ReadOnly};
inline auto YAxisColor = Preference<Color>{
  "render/Colors/Y axis",
  RgbaF{0.29f, 0.58f, 0.0f, 0.7f},
  PreferencePersistencePolicy::ReadOnly};
inline auto ZAxisColor = Preference<Color>{
  "render/Colors/Z axis",
  RgbaF{0.06f, 0.61f, 1.0f, 0.7f},
  PreferencePersistencePolicy::ReadOnly};
inline auto PointFileColor =
  Preference<Color>{"render/Colors/Point file", RgbF{0.0f, 1.0f, 0.0f}};
inline auto PortalFileBorderColor =
  Preference<Color>{"render/Colors/Portal file border", RgbaF{1.0f, 1.0f, 1.0f, 0.5f}};
inline auto PortalFileFillColor =
  Preference<Color>{"render/Colors/Portal file fill", RgbaF{1.0f, 0.4f, 0.4f, 0.2f}};
inline auto ShowFPS = Preference<bool>{"render/Show FPS", false};
inline constexpr auto MaxRenderFpsLimit = 1000;
inline auto RenderFpsLimit = Preference<int>{"render/FPS limit", 0};
inline auto ShowSky = Preference<bool>{"render/Show sky", true};

Preference<Color>& axisColor(vm::axis::type axis);

inline auto CompassBackgroundColor = Preference<Color>{
  "render/Colors/Compass background",
  RgbaF{0.5f, 0.5f, 0.5f, 0.5f},
  PreferencePersistencePolicy::ReadOnly};
inline auto CompassBackgroundOutlineColor = Preference<Color>{
  "render/Colors/Compass background outline",
  RgbaF{1.0f, 1.0f, 1.0f, 0.5f},
  PreferencePersistencePolicy::ReadOnly};
inline auto CompassAxisOutlineColor = Preference<Color>{
  "render/Colors/Compass axis outline",
  RgbF{1.0f, 1.0f, 1.0f},
  PreferencePersistencePolicy::ReadOnly};

inline auto CameraFrustumColor =
  Preference<Color>{"render/Colors/Camera frustum", RgbF{0.0f, 1.0f, 1.0f}};

inline auto DefaultGroupColor =
  Preference<Color>{"render/Colors/Groups", RgbF{0.7f, 0.4f, 1.0f}};
inline auto LinkedGroupColor =
  Preference<Color>{"render/Colors/Linked Groups", RgbF{1.0f, 0.35f, 0.87f}};

inline auto TutorialOverlayTextColor =
  Preference<Color>{"render/Colors/Tutorial overlay text", RgbF{1.0f, 1.0f, 1.0f}};
inline auto TutorialOverlayBackgroundColor = Preference<Color>{
  "render/Colors/Tutorial overlay background", RgbaF{1.0f, 0.5f, 0.0f, 0.6f}};

inline auto FaceColor = Preference<Color>{"render/Colors/Faces", RgbF{0.2f, 0.2f, 0.2f}};
inline auto SelectedFaceColor =
  Preference<Color>{"render/Colors/Selected faces", RgbF{1.0f, 0.85f, 0.85f}};
inline auto LockedFaceColor =
  Preference<Color>{"render/Colors/Locked faces", RgbF{0.85f, 0.85f, 1.0f}};
inline auto TransparentFaceAlpha =
  Preference<float>{"render/Colors/Transparent faces", 0.4f};
inline auto EdgeColor = Preference<Color>{"render/Colors/Edges", RgbF{0.9f, 0.9f, 0.9f}};
inline auto SelectedEdgeColor =
  Preference<Color>{"render/Colors/Selected edges", RgbF{1.0f, 0.0f, 0.0f}};
inline auto OccludedSelectedEdgeAlpha =
  Preference<float>{"render/Colors/Occluded selected edge alpha", 0.4f};
inline auto LockedEdgeColor =
  Preference<Color>{"render/Colors/Locked edges", RgbF{0.13f, 0.3f, 1.0f}};
inline auto UndefinedEntityColor =
  Preference<Color>{"render/Colors/Undefined entity", RgbF{0.5f, 0.5f, 0.5f}};

inline auto SelectionBoundsColor =
  Preference<Color>{"render/Colors/Selection bounds", RgbaF{1.0f, 0.0f, 0.0f, 0.35f}};

inline auto InfoOverlayTextColor =
  Preference<Color>{"render/Colors/Info overlay text", RgbF{1.0f, 1.0f, 1.0f}};
inline auto GroupInfoOverlayTextColor =
  Preference<Color>{"render/Colors/Group info overlay text", RgbF{0.7f, 0.4f, 1.0f}};
inline auto InfoOverlayBackgroundColor = Preference<Color>{
  "render/Colors/Info overlay background", RgbaF{0.0f, 0.0f, 0.0f, 0.6f}};
inline auto WeakInfoOverlayBackgroundAlpha =
  Preference<float>{"render/Colors/Weak info overlay background alpha", 0.3f};
inline auto SelectedInfoOverlayTextColor =
  Preference<Color>{"render/Colors/Selected info overlay text", RgbF{1.0f, 1.0f, 1.0f}};
inline auto SelectedInfoOverlayBackgroundColor = Preference<Color>{
  "render/Colors/Selected info overlay background", RgbaF{1.0f, 0.0f, 0.0f, 0.6f}};
inline auto LockedInfoOverlayTextColor =
  Preference<Color>{"render/Colors/Locked info overlay text", RgbF{0.35f, 0.35f, 0.6f}};
inline auto LockedInfoOverlayBackgroundColor = Preference<Color>{
  "render/Colors/Locked info overlay background", RgbaF{0.0f, 0.0f, 0.0f, 0.6f}};

inline auto HandleRadius = Preference<float>{"Controls/Handle radius", 3.0f};
inline auto MaximumHandleDistance =
  Preference<float>{"Controls/Maximum handle distance", 1000.0f};
inline auto HandleColor =
  Preference<Color>{"render/Colors/Handle", RgbF{0.97f, 0.9f, 0.23f}};
inline auto OccludedHandleColor =
  Preference<Color>{"render/Colors/Occluded handle", RgbaF{0.87f, 0.9f, 0.23f, 0.4f}};
inline auto SelectedHandleColor =
  Preference<Color>{"render/Colors/Selected handle", RgbF{1.0f, 0.0f, 0.0f}};
inline auto OccludedSelectedHandleColor = Preference<Color>{
  "render/Colors/Occluded selected handle", RgbaF{1.0f, 0.0f, 0.0f, 0.4f}};

inline auto ClipHandleColor =
  Preference<Color>{"render/Colors/Clip handle", RgbF{1.0f, 0.5f, 0.0f}};
inline auto ClipFaceColor =
  Preference<Color>{"render/Colors/Clip face", RgbaF{0.6f, 0.4f, 0.0f, 0.35f}};

inline auto ExtrudeHandleColor =
  Preference<Color>{"render/Colors/Resize handle", RgbB{248, 230, 60}};
inline auto RotateHandleRadius =
  Preference<float>{"Controls/Rotate handle radius", 64.0f};
inline auto RotateHandleColor =
  Preference<Color>{"render/Colors/Rotate handle", RgbB{248, 230, 60}};

inline auto ScaleHandleColor =
  Preference<Color>{"render/Colors/Scale handle", RgbB{77, 255, 80}};
inline auto ScaleFillColor = Preference<Color>{
  "render/Colors/Scale fill", RgbaF{77 / 255.0f, 1.0f, 80 / 255.0f, 0.125f}};
inline auto ScaleOutlineColor =
  Preference<Color>{"render/Colors/Scale outline", RgbB{77, 255, 80}};
inline auto ScaleOutlineDimAlpha =
  Preference<float>{"render/Colors/Scale outline dim alpha", 0.3f};
inline auto ShearFillColor = Preference<Color>{
  "render/Colors/Shear fill", RgbaF{45 / 255.0f, 133 / 255.0f, 1.0f, 0.125f}};
inline auto ShearOutlineColor =
  Preference<Color>{"render/Colors/Shear outline", RgbB{45, 133, 255}};

inline auto MoveTraceColor =
  Preference<Color>{"render/Colors/Move trace", RgbF{0.0f, 1.0f, 1.0f}};
inline auto OccludedMoveTraceColor =
  Preference<Color>{"render/Colors/Move trace", RgbaF{0.0f, 1.0f, 1.0f, 0.4f}};

inline auto MoveIndicatorOutlineColor =
  Preference<Color>{"render/Colors/Move indicator outline", RgbF{1.0f, 1.0f, 1.0f}};
inline auto MoveIndicatorFillColor =
  Preference<Color>{"render/Colors/Move indicator fill", RgbaF{0.0f, 0.0f, 0.0f, 0.5f}};

inline auto AngleIndicatorColor =
  Preference<Color>{"render/Colors/Angle indicator", RgbF{1.0f, 1.0f, 1.0f}};

inline auto TextureSeamColor =
  Preference<Color>{"render/Colors/Texture seam", RgbF{1.0f, 1.0f, 0.0f}};

inline auto Brightness = Preference<float>{"render/Brightness", 1.4f};
inline auto GridAlpha = Preference<float>{"render/Grid/Alpha", 0.5f};
inline auto GridColor2D =
  Preference<Color>{"render/Grid/Color2D", RgbaF{0.8f, 0.8f, 0.8f, 0.8f}};

inline auto TextureMinFilter = Preference<int>{"render/Texture mode min filter", 0x2700};
inline auto TextureMagFilter = Preference<int>{"render/Texture mode mag filter", 0x2600};
inline auto EnableMSAA = Preference<bool>{"render/Enable multisampling", true};

inline auto AlignmentLock = Preference<bool>{"Editor/Texture lock", true};
inline auto UvLock = Preference<bool>{"Editor/UV lock", false};
inline auto GroupBrushesCreatedByShapeTool =
  Preference<bool>{"Editor/Group brushes created by shape tool", true};

inline auto RendererFontPath = Preference<std::filesystem::path>{
  "render/Font name", "fonts/SourceSansPro-Regular.otf"};

inline auto RendererFontSize = Preference<int>{"render/Font size", 13};

inline auto BrowserFontSize = Preference<int>{"Browser/Font size", 13};
inline auto BrowserTextColor =
  Preference<Color>{"Browser/Text color", RgbF{1.0f, 1.0f, 1.0f}};
inline auto BrowserSubTextColor =
  Preference<Color>{"Browser/Sub text color", RgbF{0.65f, 0.65f, 0.65f}};
inline auto BrowserGroupBackgroundColor =
  Preference<Color>{"Browser/Group background color", RgbaF{0.1f, 0.1f, 0.1f, 0.8f}};
inline auto BrowserBackgroundColor =
  Preference<Color>{"Browser/Background color", RgbF{0.14f, 0.14f, 0.14f}};
inline auto MaterialBrowserIconSize =
  Preference<float>{"Texture Browser/Icon size", 1.0f};
inline auto AssetBrowserIconSize = Preference<float>{"Asset Browser/Icon size", 1.0f};
inline auto PrefabDirectory = Preference<std::filesystem::path>{"Prefab/Directory", {}};
inline auto MaterialBrowserDefaultColor =
  Preference<Color>{"Texture Browser/Default color", RgbaF{0.0f, 0.0f, 0.0f, 0.0f}};
inline auto MaterialBrowserSelectedColor =
  Preference<Color>{"Texture Browser/Selected color", RgbF{1.0f, 0.0f, 0.0f}};
inline auto MaterialBrowserUsedColor =
  Preference<Color>{"Texture Browser/Used color", RgbF{1.0f, 0.7f, 0.0f}};

inline auto CameraLookSpeed = Preference<float>{"Controls/Camera/Look speed", 0.5f};
inline auto CameraLookInvertH =
  Preference<bool>{"Controls/Camera/Invert horizontal look", false};
inline auto CameraLookInvertV =
  Preference<bool>{"Controls/Camera/Invert vertical look", false};
inline auto CameraPanSpeed = Preference<float>{"Controls/Camera/Pan speed", 0.5f};
inline auto CameraPanInvertH =
  Preference<bool>{"Controls/Camera/Invert horizontal pan", false};
inline auto CameraPanInvertV =
  Preference<bool>{"Controls/Camera/Invert vertical pan", false};
inline auto CameraMouseWheelInvert =
  Preference<bool>{"Controls/Camera/Invert mouse wheel", false};
inline auto CameraMoveSpeed = Preference<float>{"Controls/Camera/Move speed", 0.3f};
inline auto CameraEnableAltMove =
  Preference<bool>{"Controls/Camera/Use alt to move", false};
inline auto CameraAltMoveInvert =
  Preference<bool>{"Controls/Camera/Invert zoom direction when using alt to move", false};
inline auto CameraMoveInCursorDir =
  Preference<bool>{"Controls/Camera/Move camera in cursor dir", false};
inline auto CameraFov = Preference<float>{"Controls/Camera/Field of vision", 90.0f};

inline constexpr auto MinCameraFlyMoveSpeed = 0.1f;
inline constexpr auto MaxCameraFlyMoveSpeed = 10.0f;
inline auto CameraFlyMoveSpeed =
  Preference<float>{"Controls/Camera/Fly move speed", 0.5f};

inline auto Link2DCameras = Preference<bool>{"Controls/Camera/Link 2D cameras", true};

inline auto CameraFlyForward = Preference<std::vector<KeySequence>>{
  "Controls/Camera/Move forward", std::vector<KeySequence>{KeySequence{"W"}}};
inline auto CameraFlyBackward = Preference<std::vector<KeySequence>>{
  "Controls/Camera/Move backward", std::vector<KeySequence>{KeySequence{"S"}}};
inline auto CameraFlyLeft = Preference<std::vector<KeySequence>>{
  "Controls/Camera/Move left", std::vector<KeySequence>{KeySequence{"A"}}};
inline auto CameraFlyRight = Preference<std::vector<KeySequence>>{
  "Controls/Camera/Move right", std::vector<KeySequence>{KeySequence{"D"}}};
inline auto CameraFlyUp = Preference<std::vector<KeySequence>>{
  "Controls/Camera/Move up", std::vector<KeySequence>{KeySequence{"Q"}}};
inline auto CameraFlyDown = Preference<std::vector<KeySequence>>{
  "Controls/Camera/Move down", std::vector<KeySequence>{KeySequence{"X"}}};

// Play mode (KreedzBroom: embedded Counter-Strike 1.6 movement). The defaults are the
// CS 1.6 client and server defaults.
inline auto PlayModeMouseSensitivity =
  Preference<float>{"Controls/Play mode/Mouse sensitivity", 2.5f};
inline auto PlayModeRawInput =
  Preference<bool>{"Controls/Play mode/Raw mouse input", true};

// Keys are single Qt key codes so that modifiers (Ctrl to duck) can be bound.
// Qt key codes of the default play mode keys. This library does not use Qt, so they
// are spelled out here; the play mode code checks them against Qt.
inline constexpr auto PlayModeQtKeyW = 0x57;
inline constexpr auto PlayModeQtKeyS = 0x53;
inline constexpr auto PlayModeQtKeyA = 0x41;
inline constexpr auto PlayModeQtKeyD = 0x44;
inline constexpr auto PlayModeQtKeySpace = 0x20;
inline constexpr auto PlayModeQtKeyControl = 0x01000021;
inline constexpr auto PlayModeQtKeyShift = 0x01000020;
inline constexpr auto PlayModeQtKeyE = 0x45;
inline constexpr auto PlayModeQtKeyLeft = 0x01000012;
inline constexpr auto PlayModeQtKeyRight = 0x01000014;
inline constexpr auto PlayModeQtKeyP = 0x50;
inline constexpr auto PlayModeQtKeyL = 0x4c;
inline auto PlayModeKeyForward =
  Preference<int>{"Controls/Play mode/Key forward", PlayModeQtKeyW};
inline auto PlayModeKeyBack =
  Preference<int>{"Controls/Play mode/Key back", PlayModeQtKeyS};
inline auto PlayModeKeyMoveLeft =
  Preference<int>{"Controls/Play mode/Key move left", PlayModeQtKeyA};
inline auto PlayModeKeyMoveRight =
  Preference<int>{"Controls/Play mode/Key move right", PlayModeQtKeyD};
inline auto PlayModeKeyJump =
  Preference<int>{"Controls/Play mode/Key jump", PlayModeQtKeySpace};
inline auto PlayModeKeyDuck =
  Preference<int>{"Controls/Play mode/Key duck", PlayModeQtKeyControl};
inline auto PlayModeKeyWalk =
  Preference<int>{"Controls/Play mode/Key walk", PlayModeQtKeyShift};
inline auto PlayModeKeyUse =
  Preference<int>{"Controls/Play mode/Key use", PlayModeQtKeyE};
inline auto PlayModeKeyTurnLeft =
  Preference<int>{"Controls/Play mode/Key turn left", PlayModeQtKeyLeft};
inline auto PlayModeKeyTurnRight =
  Preference<int>{"Controls/Play mode/Key turn right", PlayModeQtKeyRight};
inline auto PlayModeKeySavePoint =
  Preference<int>{"Controls/Play mode/Key save point", PlayModeQtKeyP};
inline auto PlayModeKeyLoadPoint =
  Preference<int>{"Controls/Play mode/Key load point", PlayModeQtKeyL};
inline auto PlayModeYawSpeed =
  Preference<float>{"Controls/Play mode/cl_yawspeed", 210.0f};
// What a mouse wheel notch does: one +action/-action pair, like "bind mwheelup +jump".
inline constexpr auto PlayModeWheelNothing = 0;
inline constexpr auto PlayModeWheelJump = 1;
inline constexpr auto PlayModeWheelDuck = 2;
inline auto PlayModeWheelUp =
  Preference<int>{"Controls/Play mode/Mouse wheel up", PlayModeWheelJump};
inline auto PlayModeWheelDown =
  Preference<int>{"Controls/Play mode/Mouse wheel down", PlayModeWheelDuck};

inline auto PlayModeAirAccelerate =
  Preference<float>{"Controls/Play mode/sv_airaccelerate", 10.0f};
inline auto PlayModeAccelerate =
  Preference<float>{"Controls/Play mode/sv_accelerate", 5.0f};
inline auto PlayModeFriction = Preference<float>{"Controls/Play mode/sv_friction", 4.0f};
inline auto PlayModeStopSpeed =
  Preference<float>{"Controls/Play mode/sv_stopspeed", 75.0f};
inline auto PlayModeGravity = Preference<float>{"Controls/Play mode/sv_gravity", 800.0f};
inline auto PlayModeMaxSpeed = Preference<float>{"Controls/Play mode/sv_maxspeed", 320.0f};
inline auto PlayModeEdgeFriction =
  Preference<float>{"Controls/Play mode/edgefriction", 2.0f};
inline auto PlayModeStepSize = Preference<float>{"Controls/Play mode/sv_stepsize", 18.0f};

// Whose hlcsg builds the clip hulls (SCS_COMPILER_*).
inline constexpr auto PlayModeCompilerVhlt = 0;
inline constexpr auto PlayModeCompilerZhlt = 1;
inline auto PlayModeCompiler =
  Preference<int>{"Controls/Play mode/Compiler", PlayModeCompilerVhlt};

// hlcsg -cliptype used to build the clip hulls, with VHLT's values (SCS_CLIPTYPE_*).
inline constexpr auto PlayModeClipTypeSmallest = 0;
inline constexpr auto PlayModeClipTypeNormalized = 1;
inline constexpr auto PlayModeClipTypeSimple = 2;
inline constexpr auto PlayModeClipTypePrecise = 3;
inline constexpr auto PlayModeClipTypeLegacy = 4;
inline auto PlayModeClipType =
  Preference<int>{"Controls/Play mode/Cliptype", PlayModeClipTypeLegacy};

inline auto PlayModeWeaponSpeed =
  Preference<float>{"Controls/Play mode/Weapon speed", 250.0f};
inline auto PlayModeFrameRate = Preference<int>{"Controls/Play mode/fps_max", 100};

inline auto PlayModeFov = Preference<float>{"Controls/Play mode/Field of view", 90.0f};
inline auto PlayModeRestoreCheckpointView =
  Preference<bool>{"Controls/Play mode/Restore checkpoint view", true};
inline auto PlayModeShowHud = Preference<bool>{"Controls/Play mode/Show HUD", true};
inline auto PlayModeShowFallDamage =
  Preference<bool>{"Controls/Play mode/Show fall damage", true};
// Like the game's "volume" cvar; 0 mutes the footsteps and other movement sounds.
inline auto PlayModeSoundVolume = Preference<float>{"Controls/Play mode/Volume", 0.8f};
inline auto PlayModeShowCrosshair =
  Preference<bool>{"Controls/Play mode/Show crosshair", true};

// Map view config
inline auto ShowEntityClassnames =
  Preference<bool>{"Map view/Show entity classnames", true};
inline auto ShowGroupBounds = Preference<bool>{"Map view/Show group bounds", true};
inline auto ShowBrushEntityBounds =
  Preference<bool>{"Map view/Show brush entity bounds", true};
inline auto ShowPointEntityBounds =
  Preference<bool>{"Map view/Show point entity bounds", true};
inline auto ShowPointEntityModels =
  Preference<bool>{"Map view/Show point entity models", true};

inline constexpr auto FaceRenderModeTextured = "textured";
inline constexpr auto FaceRenderModeFlat = "flat";
inline constexpr auto FaceRenderModeSkip = "skip";
inline constexpr auto FaceRenderModeAll = "all";
inline auto FaceRenderMode =
  Preference<std::string>{"Map view/Face render mode", FaceRenderModeTextured};

inline auto ShadeFaces = Preference<bool>{"Map view/Shade faces", true};
inline auto ShowFog = Preference<bool>{"Map view/Show fog", false};
inline auto ShowEdges = Preference<bool>{"Map view/Show edges", true};
inline auto UseReadable2DBrushOutlines =
  Preference<bool>{"Map view/Use readable 2D brush outlines", true};

inline auto ShowSoftMapBounds = Preference<bool>{"Map view/Show soft map bounds", true};

inline auto ShowPointEntities = Preference<bool>{"Map view/Show point entities", true};
inline auto ShowBrushes = Preference<bool>{"Map view/Show brushes", true};
inline auto ShowPatches = Preference<bool>{"Map view/Show patches", true};

inline constexpr auto EntityLinkModeAll = "all";
inline constexpr auto EntityLinkModeTransitive = "transitive";
inline constexpr auto EntityLinkModeDirect = "direct";
inline constexpr auto EntityLinkModeNone = "none";
inline auto EntityLinkMode =
  Preference<std::string>{"Map view/Entity link mode", EntityLinkModeDirect};

std::vector<Preference<Color>*> colorPreferences();

std::vector<Preference<std::vector<KeySequence>>*> keyPreferences();

} // namespace tb::Preferences
