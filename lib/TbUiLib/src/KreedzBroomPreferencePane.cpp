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
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QSpinBox>

#include "base/PreferenceManager.h"
#include "prefs/Preferences.h"
#include "ui/FormWithSectionsLayout.h"
#include "ui/ViewConstants.h"

#include <algorithm>
#include <utility>

namespace tb::ui
{
namespace
{

constexpr auto FieldWidth = 120;

QWidget* withToolTip(QWidget* widget, const QString& toolTip)
{
  widget->setToolTip(toolTip);
  return widget;
}

// Two related fields in one form row, e.g. the keys for forward and back.
QWidget* pair(QWidget* first, QWidget* second)
{
  auto* widget = new QWidget{};
  auto* layout = new QHBoxLayout{widget};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(LayoutConstants::MediumHMargin);
  layout->addWidget(first);
  layout->addWidget(second);
  layout->addStretch();
  return widget;
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
  // Two columns of sections use the width of the dialog instead of one long column.
  const auto createColumn = []() {
    auto* column = new FormWithSectionsLayout{};
    column->setVerticalSpacing(LayoutConstants::NarrowVMargin);
    return column;
  };
  auto* left = createColumn();
  auto* right = createColumn();

  left->addSection("Mouse");
  left->addRow(
    "Sensitivity",
    withToolTip(
      floatField(Preferences::PlayModeMouseSensitivity, 0.01, 50.0, 2, 0.1),
      "Same scale as the game's sensitivity."));
  left->addRow(
    "",
    withToolTip(
      boolField("Raw input", Preferences::PlayModeRawInput),
      "Unaccelerated mouse counts, like m_rawinput 1."));

  left->addSection("Server");
  left->addRow(
    "sv_airaccelerate",
    floatField(Preferences::PlayModeAirAccelerate, 0.0, 1000.0, 1, 1.0));
  left->addRow(
    "sv_accelerate", floatField(Preferences::PlayModeAccelerate, 0.0, 100.0, 1, 0.5));
  left->addRow(
    "sv_friction", floatField(Preferences::PlayModeFriction, 0.0, 100.0, 1, 0.5));
  left->addRow(
    "sv_stopspeed", floatField(Preferences::PlayModeStopSpeed, 0.0, 1000.0, 0, 5.0));
  left->addRow(
    "sv_gravity", floatField(Preferences::PlayModeGravity, 0.0, 5000.0, 0, 10.0));
  left->addRow(
    "sv_maxspeed", floatField(Preferences::PlayModeMaxSpeed, 0.0, 2000.0, 0, 10.0));
  left->addRow(
    "edgefriction", floatField(Preferences::PlayModeEdgeFriction, 0.0, 10.0, 1, 0.5));
  left->addRow(
    "sv_stepsize", floatField(Preferences::PlayModeStepSize, 0.0, 64.0, 0, 1.0));

  left->addSection("View");
  left->addRow(
    "",
    withToolTip(
      boolField(tr("Full screen during play"), Preferences::PlayModeFullScreen),
      tr("Fill the screen with the 3D view while playing. Restore the editor layout "
         "when play ends.")));
  left->addRow(
    "Field of view", floatField(Preferences::PlayModeFov, 10.0, 170.0, 0, 5.0, "°"));
  left->addRow(
    "", boolField("Restore checkpoint view", Preferences::PlayModeRestoreCheckpointView));
  left->addRow("", boolField("Show HUD", Preferences::PlayModeShowHud));
  left->addRow("", boolField("Show crosshair", Preferences::PlayModeShowCrosshair));
  left->addRow("", boolField("Show fall damage", Preferences::PlayModeShowFallDamage));

  right->addSection("Keys");
  right->addRow(
    "Forward / Back",
    pair(
      keyField(Preferences::PlayModeKeyForward), keyField(Preferences::PlayModeKeyBack)));
  right->addRow(
    "Move left / right",
    pair(
      keyField(Preferences::PlayModeKeyMoveLeft),
      keyField(Preferences::PlayModeKeyMoveRight)));
  right->addRow(
    "Jump / Duck",
    pair(keyField(Preferences::PlayModeKeyJump), keyField(Preferences::PlayModeKeyDuck)));
  right->addRow(
    "Walk / Use",
    pair(keyField(Preferences::PlayModeKeyWalk), keyField(Preferences::PlayModeKeyUse)));
  right->addRow(
    "Turn left / right",
    pair(
      keyField(Preferences::PlayModeKeyTurnLeft),
      keyField(Preferences::PlayModeKeyTurnRight)));
  right->addRow(
    "Save / load point",
    pair(
      keyField(Preferences::PlayModeKeySavePoint),
      keyField(Preferences::PlayModeKeyLoadPoint)));
  const auto wheelChoices = std::vector<std::pair<QString, int>>{
    {"Nothing", Preferences::PlayModeWheelNothing},
    {"Jump", Preferences::PlayModeWheelJump},
    {"Duck", Preferences::PlayModeWheelDuck},
  };
  right->addRow(
    "Wheel up / down",
    withToolTip(
      pair(
        choiceField(Preferences::PlayModeWheelUp, wheelChoices),
        choiceField(Preferences::PlayModeWheelDown, wheelChoices)),
      "Each notch presses the action for one frame, like \"bind mwheeldown +jump\"."));
  right->addRow(
    "cl_yawspeed", floatField(Preferences::PlayModeYawSpeed, 0.0, 1000.0, 0, 5.0));

  right->addSection("Player");
  right->addRow(
    "Weapon speed",
    withToolTip(
      floatField(Preferences::PlayModeWeaponSpeed, 1.0, 2000.0, 0, 5.0, " u/s"),
      "250 knife and pistols, 260 Scout, 230 M4A1, 221 AK-47, 210 AWP."));
  right->addRow(
    "fps_max",
    withToolTip(
      intField(Preferences::PlayModeFrameRate, 4, 1000, " fps"),
      "The frame rate changes the physics like in the game. 100 is the KZ standard."));

  right->addSection("Clip Hulls");
  right->addRow(
    "Compiler",
    choiceField(
      Preferences::PlayModeCompiler,
      {
        {"VHLT", Preferences::PlayModeCompilerVhlt},
        {"ZHLT", Preferences::PlayModeCompilerZhlt},
      }));
  right->addRow(
    "Cliptype",
    withToolTip(
      choiceField(
        Preferences::PlayModeClipType,
        {
          {"Legacy", Preferences::PlayModeClipTypeLegacy},
          {"Simple", Preferences::PlayModeClipTypeSimple},
          {"Precise", Preferences::PlayModeClipTypePrecise},
          {"Normalized", Preferences::PlayModeClipTypeNormalized},
          {"Smallest", Preferences::PlayModeClipTypeSmallest},
        }),
      "hlcsg -cliptype. Legacy is what most CS 1.6 maps use."));

  right->addSection("Sound");
  right->addRow(
    "Volume", floatField(Preferences::PlayModeSoundVolume, 0.0, 1.0, 2, 0.05));

  auto* layout = new QHBoxLayout{};
  layout->setContentsMargins(
    LayoutConstants::DialogOuterMargin,
    LayoutConstants::DialogOuterMargin,
    LayoutConstants::DialogOuterMargin,
    LayoutConstants::DialogOuterMargin);
  layout->setSpacing(3 * LayoutConstants::WideHMargin);
  layout->addLayout(left);
  layout->addLayout(right);
  layout->setAlignment(left, Qt::AlignTop);
  layout->setAlignment(right, Qt::AlignTop);

  createScrollableContent(layout);
}

QWidget* KreedzBroomPreferencePane::floatField(
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
  return spinBox;
}

QWidget* KreedzBroomPreferencePane::intField(
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
  return spinBox;
}

QWidget* KreedzBroomPreferencePane::boolField(
  const QString& label, Preference<bool>& preference)
{
  auto* checkBox = new QCheckBox{label};

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
  return checkBox;
}

QWidget* KreedzBroomPreferencePane::keyField(Preference<int>& preference)
{
  auto* button = new PlayModeKeyButton{
    [&preference](const int key) { PreferenceManager::instance().set(preference, key); }};
  button->setMaximumWidth(FieldWidth);
  button->setToolTip(tr("Click, then press a key"));

  m_keyButtons.push_back(button);
  m_bindings.push_back({
    [button, &preference]() {
      button->setKey(PreferenceManager::instance().getPendingValue(preference));
    },
    [&preference]() { PreferenceManager::instance().resetToDefault(preference); },
  });
  return button;
}

QWidget* KreedzBroomPreferencePane::choiceField(
  Preference<int>& preference, const std::vector<std::pair<QString, int>>& choices)
{
  auto* comboBox = new QComboBox{};
  for (const auto& [text, value] : choices)
  {
    comboBox->addItem(text, value);
  }
  comboBox->setMaximumWidth(FieldWidth);

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
  return comboBox;
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
