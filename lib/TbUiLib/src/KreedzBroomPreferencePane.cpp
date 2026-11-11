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
#include <QTabWidget>
#include <QVBoxLayout>

#include "base/PreferenceManager.h"
#include "prefs/Preferences.h"
#include "ui/FormWithSectionsLayout.h"
#include "ui/PanelLayout.h"
#include "ui/QPathUtils.h"
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
  auto* controls = new FormWithSectionsLayout{};
  auto* physics = new FormWithSectionsLayout{};
  auto* navigation = new FormWithSectionsLayout{};
  auto* player = new FormWithSectionsLayout{};
  auto* display = new FormWithSectionsLayout{};
  auto* analysis = new FormWithSectionsLayout{};

  navigation->addSection(tr("Mouse"));
  navigation->addRow(
    tr("Sensitivity"),
    withToolTip(
      floatField(Preferences::PlayModeMouseSensitivity, 0.01, 50.0, 2, 0.1),
      "Same scale as the game's sensitivity."));
  navigation->addRow(
    "",
    withToolTip(
      boolField(tr("Raw input"), Preferences::PlayModeRawInput),
      "Unaccelerated mouse counts, like m_rawinput 1."));

  physics->addSection(tr("Movement physics"));
  physics->addRow(
    tr("Air acceleration"),
    floatField(Preferences::PlayModeAirAccelerate, 0.0, 1000.0, 1, 1.0));
  physics->addRow(
    tr("Ground acceleration"),
    floatField(Preferences::PlayModeAccelerate, 0.0, 100.0, 1, 0.5));
  physics->addRow(
    tr("Friction"), floatField(Preferences::PlayModeFriction, 0.0, 100.0, 1, 0.5));
  physics->addRow(
    tr("Stop speed"), floatField(Preferences::PlayModeStopSpeed, 0.0, 1000.0, 0, 5.0));
  physics->addRow(
    tr("Gravity"), floatField(Preferences::PlayModeGravity, 0.0, 5000.0, 0, 10.0));
  physics->addRow(
    tr("Maximum speed"), floatField(Preferences::PlayModeMaxSpeed, 0.0, 2000.0, 0, 10.0));
  physics->addRow(
    tr("Edge friction"),
    floatField(Preferences::PlayModeEdgeFriction, 0.0, 10.0, 1, 0.5));
  physics->addRow(
    tr("Step height"), floatField(Preferences::PlayModeStepSize, 0.0, 64.0, 0, 1.0));

  display->addSection(tr("View"));
  display->addRow(
    "",
    withToolTip(
      boolField(tr("Full screen during play"), Preferences::PlayModeFullScreen),
      tr("Fill the screen with the 3D view while playing. Restore the editor layout "
         "when play ends.")));
  display->addRow(
    tr("Field of view"), floatField(Preferences::PlayModeFov, 10.0, 170.0, 0, 5.0, "°"));
  display->addRow(
    "",
    boolField(tr("Restore checkpoint view"), Preferences::PlayModeRestoreCheckpointView));
  display->addRow("", boolField(tr("Show HUD"), Preferences::PlayModeShowHud));
  display->addRow(
    "", boolField(tr("Show crosshair"), Preferences::PlayModeShowCrosshair));
  display->addRow(
    "", boolField(tr("Show fall damage"), Preferences::PlayModeShowFallDamage));
  analysis->addSection(tr("Jump analysis"));
  analysis->addRow(
    "",
    withToolTip(
      boolField(tr("Show jump results"), Preferences::PlayModeShowJumpStats),
      tr("Jump results with takeoff timing, speed and strafe analysis. Assisted movement "
         "is excluded.")));
  analysis->addRow(
    "", boolField(tr("Show failed jumps"), Preferences::PlayModeShowFailedJumps));
  analysis->addRow(
    "", boolField(tr("Show strafe details"), Preferences::PlayModeShowStrafes));
  analysis->addRow(
    tr("Result duration"),
    floatField(Preferences::PlayModeJumpStatsDuration, 1.0, 15.0, 1, 0.5, " s"));
  analysis->addSection(tr("Jump guides"));
  analysis->addRow(
    "",
    withToolTip(
      boolField(tr("Show jump guides"), Preferences::PlayModeShowJumpSpan),
      tr("While grounded, look toward a platform edge to show jump guides. "
         "Green marks an LJ edge; red marks an HJ takeoff hint 13 units inside the edge. "
         "Measure perpendicular to the edge for gaps from 160 units up to, but not "
         "including, 300 units. Shows horizontal distance and height difference; "
         "does not predict whether a jump is possible.")));
  controls->addSection(tr("Keys"));
  controls->addRow(
    tr("Forward / Back"),
    pair(
      keyField(Preferences::PlayModeKeyForward), keyField(Preferences::PlayModeKeyBack)));
  controls->addRow(
    tr("Move left / right"),
    pair(
      keyField(Preferences::PlayModeKeyMoveLeft),
      keyField(Preferences::PlayModeKeyMoveRight)));
  controls->addRow(
    tr("Jump / Duck"),
    pair(keyField(Preferences::PlayModeKeyJump), keyField(Preferences::PlayModeKeyDuck)));
  controls->addRow(
    tr("Walk / Use"),
    pair(keyField(Preferences::PlayModeKeyWalk), keyField(Preferences::PlayModeKeyUse)));
  controls->addRow(
    tr("Turn left / right"),
    pair(
      keyField(Preferences::PlayModeKeyTurnLeft),
      keyField(Preferences::PlayModeKeyTurnRight)));
  controls->addRow(
    tr("Save / load point"),
    pair(
      keyField(Preferences::PlayModeKeySavePoint),
      keyField(Preferences::PlayModeKeyLoadPoint)));
  const auto wheelChoices = std::vector<std::pair<QString, int>>{
    {"Nothing", Preferences::PlayModeWheelNothing},
    {"Jump", Preferences::PlayModeWheelJump},
    {"Duck", Preferences::PlayModeWheelDuck},
  };
  controls->addRow(
    tr("Wheel up / down"),
    withToolTip(
      pair(
        choiceField(Preferences::PlayModeWheelUp, wheelChoices),
        choiceField(Preferences::PlayModeWheelDown, wheelChoices)),
      "Each notch presses the action for one frame, like \"bind mwheeldown +jump\"."));
  controls->addRow(
    tr("Turn speed"), floatField(Preferences::PlayModeYawSpeed, 0.0, 1000.0, 0, 5.0));

  navigation->addSection(tr("Travel aids"));
  navigation->addRow(
    tr("Hook (hold)"),
    withToolTip(
      keyField(Preferences::PlayModeKeyHook),
      tr("Hold to pull toward the surface under the crosshair. Release to let go.")));
  navigation->addRow(
    tr("Noclip (toggle)"),
    withToolTip(
      keyField(Preferences::PlayModeKeyNoclip),
      tr("Fly through geometry. Hold the walk key to move three times faster. "
         "Move out of solid geometry before turning noclip off.")));
  navigation->addRow(
    tr("Hook speed"),
    floatField(Preferences::PlayModeHookSpeed, 100.0, 2000.0, 0, 50.0, " u/s"));

  player->addSection(tr("Player"));
  player->addRow(
    tr("Weapon speed"),
    withToolTip(
      floatField(Preferences::PlayModeWeaponSpeed, 1.0, 2000.0, 0, 5.0, " u/s"),
      "250 knife and pistols, 260 Scout, 230 M4A1, 221 AK-47, 210 AWP."));
  player->addRow(
    tr("Physics frame rate"),
    withToolTip(
      intField(Preferences::PlayModeFrameRate, 4, 1000, " fps"),
      "The frame rate changes the physics like in the game. 100 is the KZ standard."));

  player->addSection(tr("Collision hulls"));
  auto* compilerField = qobject_cast<QComboBox*>(choiceField(
    Preferences::PlayModeCompiler,
    {
      {"VHLT", Preferences::PlayModeCompilerVhlt},
      {"ZHLT", Preferences::PlayModeCompilerZhlt},
      {"hltools", Preferences::PlayModeCompilerHltools},
    }));
  auto* cliptypeField = qobject_cast<QComboBox*>(choiceField(
    Preferences::PlayModeClipType,
    {
      {"Legacy", Preferences::PlayModeClipTypeLegacy},
      {"Legacytrim (hltools)", Preferences::PlayModeClipTypeLegacytrim},
      {"Simple", Preferences::PlayModeClipTypeSimple},
      {"Precise", Preferences::PlayModeClipTypePrecise},
      {"Normalized", Preferences::PlayModeClipTypeNormalized},
      {"Smallest", Preferences::PlayModeClipTypeSmallest},
    }));
  player->addRow(tr("Collision backend"), compilerField);
  player->addRow(
    tr("Hull expansion"),
    withToolTip(
      cliptypeField, "hlcsg -cliptype. Legacytrim uses the embedded hltools compiler."));
  connect(
    cliptypeField,
    &QComboBox::currentIndexChanged,
    this,
    [compilerField, cliptypeField]() {
      if (cliptypeField->currentData().toInt() == Preferences::PlayModeClipTypeLegacytrim)
        compilerField->setCurrentIndex(
          compilerField->findData(Preferences::PlayModeCompilerHltools));
    });
  connect(
    compilerField,
    &QComboBox::currentIndexChanged,
    this,
    [compilerField, cliptypeField]() {
      if (
        compilerField->currentData().toInt() != Preferences::PlayModeCompilerHltools
        && cliptypeField->currentData().toInt()
             == Preferences::PlayModeClipTypeLegacytrim)
        cliptypeField->setCurrentIndex(
          cliptypeField->findData(Preferences::PlayModeClipTypeLegacy));
    });

  display->addSection(tr("Sound"));
  display->addRow(
    tr("Volume"), floatField(Preferences::PlayModeSoundVolume, 0.0, 1.0, 2, 0.05));

  auto* tabs = new QTabWidget{};
  tabs->setObjectName("KreedzBroomPreference_Tabs");
  const auto columns = [](FormWithSectionsLayout* first, FormWithSectionsLayout* second) {
    auto* layout = new QHBoxLayout{};
    layout->setSpacing(24);
    layout->addLayout(first, 1);
    layout->addLayout(second, 1);
    layout->setAlignment(first, Qt::AlignTop);
    layout->setAlignment(second, Qt::AlignTop);
    return layout;
  };
  const auto addTab = [&](QLayout* form, const QString& title) {
    form->setContentsMargins(16, 16, 16, 16);
    auto* column = createPanelColumn(nullptr, PanelInsets::None);
    column->addLayout(form);
    column->addStretch();
    auto* content = new QWidget{};
    content->setLayout(column);
    tabs->addTab(createPanelScrollArea(content), title);
  };
  addTab(columns(controls, navigation), tr("Controls"));
  addTab(columns(physics, player), tr("Physics and collision"));
  addTab(display, tr("View and sound"));
  addTab(analysis, tr("Jump analysis"));

  auto* layout = new QVBoxLayout{this};
  layout->setContentsMargins(16, 8, 16, 0);
  layout->addWidget(tabs);
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
  spinBox->setProperty("preferenceSearchTerms", pathAsGenericQString(preference.path));
  spinBox->setToolTip(pathAsQString(preference.path.filename()));

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
  spinBox->setProperty("preferenceSearchTerms", pathAsGenericQString(preference.path));
  spinBox->setToolTip(pathAsQString(preference.path.filename()));

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
  checkBox->setProperty("preferenceSearchTerms", pathAsGenericQString(preference.path));

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
  button->setProperty("preferenceSearchTerms", pathAsGenericQString(preference.path));
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
  comboBox->setProperty("preferenceSearchTerms", pathAsGenericQString(preference.path));

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
