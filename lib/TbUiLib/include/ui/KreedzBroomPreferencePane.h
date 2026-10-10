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

#include <QPushButton>
#include <QString>

#include "base/Preference.h"
#include "ui/PreferencePane.h"

#include <functional>
#include <utility>
#include <vector>

class QFocusEvent;
class QKeyEvent;

namespace tb::ui
{
class FormWithSectionsLayout;

/**
 * Records a single key, including bare modifiers such as Ctrl or Shift which
 * QKeySequenceEdit cannot record. Click it, then press the key; Escape cancels.
 */
class PlayModeKeyButton : public QPushButton
{
private:
  int m_key = 0;
  bool m_recording = false;
  std::function<void(int)> m_keyChanged;

public:
  explicit PlayModeKeyButton(
    std::function<void(int)> keyChanged, QWidget* parent = nullptr);

  int key() const;
  void setKey(int key);

protected:
  void keyPressEvent(QKeyEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;

private:
  void startRecording();
  void stopRecording();
};

/** Returns a readable name for a Qt key code, e.g. "Ctrl", "Space" or "W". */
QString playModeKeyName(int key);

/**
 * Settings of KreedzBroom's play mode (embedded Counter-Strike 1.6 movement): mouse,
 * keys, server movement variables, player and view options.
 */
class KreedzBroomPreferencePane : public PreferencePane
{
private:
  struct Binding
  {
    std::function<void()> updateControl;
    std::function<void()> resetToDefault;
  };

  std::vector<Binding> m_bindings;
  std::vector<PlayModeKeyButton*> m_keyButtons;

public:
  explicit KreedzBroomPreferencePane(QWidget* parent = nullptr);

private:
  void createGui();

  void addFloat(
    FormWithSectionsLayout& layout,
    const QString& label,
    Preference<float>& preference,
    double minimum,
    double maximum,
    int decimals,
    double step,
    const QString& suffix = {});
  void addInt(
    FormWithSectionsLayout& layout,
    const QString& label,
    Preference<int>& preference,
    int minimum,
    int maximum,
    const QString& suffix = {});
  void addBool(
    FormWithSectionsLayout& layout, const QString& label, Preference<bool>& preference);
  void addKey(
    FormWithSectionsLayout& layout, const QString& label, Preference<int>& preference);
  void addChoice(
    FormWithSectionsLayout& layout,
    const QString& label,
    Preference<int>& preference,
    const std::vector<std::pair<QString, int>>& choices);

  bool canResetToDefaults() override;
  void doResetToDefaults() override;
  void updateControls() override;
  bool validate() override;
};

} // namespace tb::ui
