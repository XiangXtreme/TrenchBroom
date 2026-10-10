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

#include "ui/KreedzBroomPreferencePane.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QSpinBox>

#include "base/PreferenceManager.h"
#include "prefs/Preferences.h"
#include "ui/FormWithSectionsLayout.h"
#include "ui/QStyleUtils.h"
#include "ui/ViewConstants.h"

#include <algorithm>
#include <utility>

namespace tb::ui
{
namespace
{

constexpr auto FieldWidth = 120;

QWidget* infoLabel(const QString& text)
{
  auto* label = new QLabel{text};
  label->setWordWrap(true);
  return setInfoStyle(label);
}

} // namespace

QString playModeKeyName(const int key)
{
  switch (key)
  {
  case 0:
    return QObject::tr("(none)");
  case Qt::Key_Control:
    return QObject::tr("Ctrl");
  case Qt::Key_Shift:
    return QObject::tr("Shift");
  case Qt::Key_Alt:
    return QObject::tr("Alt");
  case Qt::Key_Meta:
    return QObject::tr("Meta");
  case Qt::Key_AltGr:
    return QObject::tr("AltGr");
  default:
    return QKeySequence{key}.toString(QKeySequence::NativeText);
  }
}

PlayModeKeyButton::PlayModeKeyButton(std::function<void(int)> keyChanged, QWidget* parent)
  : QPushButton{parent}
  , m_keyChanged{std::move(keyChanged)}
{
  setFocusPolicy(Qt::StrongFocus);
  connect(this, &QPushButton::clicked, this, [this]() { startRecording(); });
}

int PlayModeKeyButton::key() const
{
  return m_key;
}

void PlayModeKeyButton::setKey(const int key)
{
  m_key = key;
  if (!m_recording)
  {
    setText(playModeKeyName(m_key));
  }
}

void PlayModeKeyButton::keyPressEvent(QKeyEvent* event)
{
  if (!m_recording)
  {
    QPushButton::keyPressEvent(event);
    return;
  }

  // While recording every key is the answer, so the dialog must not see it (Space would
  // click the button, Return would close the dialog).
  event->accept();
  if (event->isAutoRepeat())
  {
    return;
  }

  if (event->key() != Qt::Key_Escape && event->key() != Qt::Key_unknown)
  {
    m_key = event->key();
    m_keyChanged(m_key);
  }
  stopRecording();
}

void PlayModeKeyButton::focusOutEvent(QFocusEvent* event)
{
  stopRecording();
  QPushButton::focusOutEvent(event);
}

void PlayModeKeyButton::startRecording()
{
  m_recording = true;
  setText(tr("Press a key..."));
  setFocus(Qt::OtherFocusReason);
}

void PlayModeKeyButton::stopRecording()
{
  m_recording = false;
  setText(playModeKeyName(m_key));
}

KreedzBroomPreferencePane::KreedzBroomPreferencePane(QWidget* parent)
  : PreferencePane{parent}
{
  createGui();
}

void KreedzBroomPreferencePane::createGui()
{
  auto* layout = new FormWithSectionsLayout{};
  layout->setContentsMargins(
    LayoutConstants::DialogOuterMargin,
    LayoutConstants::DialogOuterMargin,
    LayoutConstants::DialogOuterMargin,
    LayoutConstants::DialogOuterMargin);
  layout->setVerticalSpacing(LayoutConstants::NarrowVMargin);

  layout->addSection("Play Mode");
  layout->addRow(infoLabel(
    "Run > Play (F7) walks the map with Counter-Strike 1.6 movement. Changes apply the "
    "next time play mode starts. The defaults are the game's defaults."));

  layout->addSection("Mouse");
  addFloat(
    *layout, "Sensitivity", Preferences::PlayModeMouseSensitivity, 0.01, 50.0, 2, 0.1);
  layout->addRow(
    "",
    infoLabel(
      QString{
        "Same scale as the game's sensitivity: 0.022 degrees per mouse count (m_yaw)."}));
  addBool(*layout, "Invert mouse", Preferences::PlayModeInvertMouse);
  addBool(*layout, "Raw mouse input", Preferences::PlayModeRawInput);
  layout->addRow(
    "",
    infoLabel(
      QString{
        "Like m_rawinput 1: unaccelerated mouse counts. Turn off to use the desktop's "
        "pointer acceleration. The cursor stays locked in the view either way."}));

  layout->addSection("Keys");
  addKey(*layout, "Forward", Preferences::PlayModeKeyForward);
  addKey(*layout, "Back", Preferences::PlayModeKeyBack);
  addKey(*layout, "Move left", Preferences::PlayModeKeyMoveLeft);
  addKey(*layout, "Move right", Preferences::PlayModeKeyMoveRight);
  addKey(*layout, "Jump", Preferences::PlayModeKeyJump);
  addKey(*layout, "Duck", Preferences::PlayModeKeyDuck);
  addKey(*layout, "Walk", Preferences::PlayModeKeyWalk);
  addKey(*layout, "Use", Preferences::PlayModeKeyUse);
  addKey(*layout, "Turn left", Preferences::PlayModeKeyTurnLeft);
  addKey(*layout, "Turn right", Preferences::PlayModeKeyTurnRight);
  addFloat(*layout, "cl_yawspeed", Preferences::PlayModeYawSpeed, 0.0, 1000.0, 0, 5.0);
  const auto wheelChoices = std::vector<std::pair<QString, int>>{
    {"Nothing", Preferences::PlayModeWheelNothing},
    {"Jump", Preferences::PlayModeWheelJump},
    {"Duck", Preferences::PlayModeWheelDuck},
  };
  addChoice(*layout, "Mouse wheel up", Preferences::PlayModeWheelUp, wheelChoices);
  addChoice(*layout, "Mouse wheel down", Preferences::PlayModeWheelDown, wheelChoices);
  layout->addRow(
    "",
    infoLabel(
      QString{"Click a key button, then press the key. Each mouse wheel notch presses "
              "its action "
              "for one frame, like \"bind mwheelup +duck\" in the game (duck tap, scroll "
              "jump). "
              "Escape or F7 leaves play mode."}));

  layout->addSection("Server");
  addFloat(
    *layout, "sv_airaccelerate", Preferences::PlayModeAirAccelerate, 0.0, 1000.0, 1, 1.0);
  layout->addRow(
    "",
    infoLabel(QString{"10 is the CS 1.6 default; some KZ and surf servers use 100."}));
  addFloat(*layout, "sv_accelerate", Preferences::PlayModeAccelerate, 0.0, 100.0, 1, 0.5);
  addFloat(*layout, "sv_friction", Preferences::PlayModeFriction, 0.0, 100.0, 1, 0.5);
  addFloat(*layout, "sv_stopspeed", Preferences::PlayModeStopSpeed, 0.0, 1000.0, 0, 5.0);
  addFloat(*layout, "sv_gravity", Preferences::PlayModeGravity, 0.0, 5000.0, 0, 10.0);
  addFloat(*layout, "sv_maxspeed", Preferences::PlayModeMaxSpeed, 0.0, 2000.0, 0, 10.0);
  addFloat(*layout, "edgefriction", Preferences::PlayModeEdgeFriction, 0.0, 10.0, 1, 0.5);
  addFloat(*layout, "sv_stepsize", Preferences::PlayModeStepSize, 0.0, 64.0, 0, 1.0);

  layout->addSection("Compiler");
  addChoice(
    *layout,
    "Compiler",
    Preferences::PlayModeCompiler,
    {
      {"VHLT", Preferences::PlayModeCompilerVhlt},
      {"ZHLT", Preferences::PlayModeCompilerZhlt},
    });
  layout->addRow(
    "",
    infoLabel(
      QString{"The map compiler to imitate. VHLT and ZHLT differ in the Simple, Precise "
              "and Normalized cliptypes, and only VHLT knows the CLIPHULLn, NOCLIP and "
              "BEVELBRUSH tool textures."}));
  addChoice(
    *layout,
    "Cliptype",
    Preferences::PlayModeClipType,
    {
      {"Legacy", Preferences::PlayModeClipTypeLegacy},
      {"Simple", Preferences::PlayModeClipTypeSimple},
      {"Precise", Preferences::PlayModeClipTypePrecise},
      {"Normalized", Preferences::PlayModeClipTypeNormalized},
      {"Smallest", Preferences::PlayModeClipTypeSmallest},
    });
  layout->addRow(
    "",
    infoLabel(
      QString{"How hlcsg grows brushes into the player's collision hulls (-cliptype). "
              "Legacy was ZHLT's default and is what most CS 1.6 maps use; Simple is "
              "VHLT's default. Legacy slopes are lower and depend on how the brushes "
              "are split."}));
  addBool(
    *layout,
    "Use the map's info_compile_parameters",
    Preferences::PlayModeUseMapClipType);
  layout->addRow(
    "",
    infoLabel(
      QString{"Like the compiler, an info_compile_parameters entity's cliptype wins "
              "(a missing key means Smallest)."}));

  layout->addSection("Player");
  addFloat(
    *layout,
    "Weapon speed",
    Preferences::PlayModeWeaponSpeed,
    1.0,
    2000.0,
    0,
    5.0,
    " u/s");
  layout->addRow(
    "",
    infoLabel(
      QString{
        "Maximum speed of the held weapon: 250 knife and pistols, 260 Scout, 221 AK-47, "
        "230 M4A1, 210 AWP."}));
  addInt(*layout, "fps_max", Preferences::PlayModeFrameRate, 4, 1000, " fps");
  layout->addRow(
    "",
    infoLabel(
      QString{"The frame rate changes the physics like in the game (it sets the usercmd "
              "length). 100 is the KZ standard."}));

  layout->addSection("View");
  addFloat(*layout, "Field of view", Preferences::PlayModeFov, 10.0, 170.0, 0, 5.0, "°");
  addBool(*layout, "Show HUD", Preferences::PlayModeShowHud);
  addBool(*layout, "Show fall damage", Preferences::PlayModeShowFallDamage);
  layout->addRow(
    "",
    infoLabel(
      QString{"There are no deaths: hard landings only show the damage they would "
              "deal."}));
  addBool(*layout, "Show crosshair", Preferences::PlayModeShowCrosshair);

  layout->addSection("Sound");
  addFloat(*layout, "Volume", Preferences::PlayModeSoundVolume, 0.0, 1.0, 2, 0.05);
  layout->addRow(
    "",
    infoLabel(
      QString{
        "Footsteps, ladders and water from the game's sound folder, played like the "
        "game does (a new sound cuts the previous one on the same channel). 0 mutes."}));

  createScrollableContent(layout);
}

void KreedzBroomPreferencePane::addFloat(
  FormWithSectionsLayout& layout,
  const QString& label,
  Preference<float>& preference,
  const double minimum,
  const double maximum,
  const int decimals,
  const double step,
  const QString& suffix)
{
  auto* spinBox = new QDoubleSpinBox{};
  spinBox->setRange(minimum, maximum);
  spinBox->setDecimals(decimals);
  spinBox->setSingleStep(step);
  spinBox->setSuffix(suffix);
  spinBox->setKeyboardTracking(false);
  spinBox->setMaximumWidth(FieldWidth);
  layout.addRow(label, spinBox);

  connect(
    spinBox, &QDoubleSpinBox::valueChanged, this, [&preference](const double value) {
      PreferenceManager::instance().set(preference, static_cast<float>(value));
    });
  m_bindings.push_back({
    [spinBox, &preference]() {
      const auto blocker = QSignalBlocker{spinBox};
      spinBox->setValue(
        static_cast<double>(PreferenceManager::instance().getPendingValue(preference)));
    },
    [&preference]() { PreferenceManager::instance().resetToDefault(preference); },
  });
}

void KreedzBroomPreferencePane::addInt(
  FormWithSectionsLayout& layout,
  const QString& label,
  Preference<int>& preference,
  const int minimum,
  const int maximum,
  const QString& suffix)
{
  auto* spinBox = new QSpinBox{};
  spinBox->setRange(minimum, maximum);
  spinBox->setSuffix(suffix);
  spinBox->setKeyboardTracking(false);
  spinBox->setMaximumWidth(FieldWidth);
  layout.addRow(label, spinBox);

  connect(spinBox, &QSpinBox::valueChanged, this, [&preference](const int value) {
    PreferenceManager::instance().set(preference, value);
  });
  m_bindings.push_back({
    [spinBox, &preference]() {
      const auto blocker = QSignalBlocker{spinBox};
      spinBox->setValue(PreferenceManager::instance().getPendingValue(preference));
    },
    [&preference]() { PreferenceManager::instance().resetToDefault(preference); },
  });
}

void KreedzBroomPreferencePane::addBool(
  FormWithSectionsLayout& layout, const QString& label, Preference<bool>& preference)
{
  auto* checkBox = new QCheckBox{label};
  layout.addRow("", checkBox);

  connect(
    checkBox,
    &QCheckBox::checkStateChanged,
    this,
    [&preference](const Qt::CheckState state) {
      PreferenceManager::instance().set(preference, state == Qt::Checked);
    });
  m_bindings.push_back({
    [checkBox, &preference]() {
      const auto blocker = QSignalBlocker{checkBox};
      checkBox->setChecked(PreferenceManager::instance().getPendingValue(preference));
    },
    [&preference]() { PreferenceManager::instance().resetToDefault(preference); },
  });
}

void KreedzBroomPreferencePane::addKey(
  FormWithSectionsLayout& layout, const QString& label, Preference<int>& preference)
{
  auto* button = new PlayModeKeyButton{
    [&preference](const int key) { PreferenceManager::instance().set(preference, key); }};
  button->setMaximumWidth(FieldWidth);
  layout.addRow(label, button);

  m_keyButtons.push_back(button);
  m_bindings.push_back({
    [button, &preference]() {
      button->setKey(PreferenceManager::instance().getPendingValue(preference));
    },
    [&preference]() { PreferenceManager::instance().resetToDefault(preference); },
  });
}

void KreedzBroomPreferencePane::addChoice(
  FormWithSectionsLayout& layout,
  const QString& label,
  Preference<int>& preference,
  const std::vector<std::pair<QString, int>>& choices)
{
  auto* comboBox = new QComboBox{};
  for (const auto& [text, value] : choices)
  {
    comboBox->addItem(text, value);
  }
  comboBox->setMaximumWidth(FieldWidth);
  layout.addRow(label, comboBox);

  connect(
    comboBox, &QComboBox::currentIndexChanged, this, [comboBox, &preference](const int) {
      PreferenceManager::instance().set(preference, comboBox->currentData().toInt());
    });
  m_bindings.push_back({
    [comboBox, &preference]() {
      const auto blocker = QSignalBlocker{comboBox};
      comboBox->setCurrentIndex(
        comboBox->findData(PreferenceManager::instance().getPendingValue(preference)));
    },
    [&preference]() { PreferenceManager::instance().resetToDefault(preference); },
  });
}

bool KreedzBroomPreferencePane::canResetToDefaults()
{
  return true;
}

void KreedzBroomPreferencePane::doResetToDefaults()
{
  for (const auto& binding : m_bindings)
  {
    binding.resetToDefault();
  }
}

void KreedzBroomPreferencePane::updateControls()
{
  for (const auto& binding : m_bindings)
  {
    binding.updateControl();
  }
}

bool KreedzBroomPreferencePane::validate()
{
  // Two actions on one key would make one of them unreachable.
  for (auto i = size_t{0}; i < m_keyButtons.size(); ++i)
  {
    for (auto j = i + 1; j < m_keyButtons.size(); ++j)
    {
      if (m_keyButtons[i]->key() != 0 && m_keyButtons[i]->key() == m_keyButtons[j]->key())
      {
        QMessageBox::warning(
          this,
          tr("Play Mode Keys"),
          tr("The key %1 is assigned to more than one play mode action.")
            .arg(playModeKeyName(m_keyButtons[i]->key())));
        return false;
      }
    }
  }
  return true;
}

} // namespace tb::ui
