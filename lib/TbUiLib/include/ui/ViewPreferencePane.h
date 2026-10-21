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
 along with TrenchBroom.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "ui/PreferencePane.h"

#include <optional>

class QCheckBox;
class QComboBox;
class QSpinBox;

namespace tb::ui
{
class SliderWithLabel;

class ViewPreferencePane : public PreferencePane
{
  Q_OBJECT
private:
  QComboBox* m_layoutCombo = nullptr;
  QComboBox* m_default2DToolCombo = nullptr;
  QCheckBox* m_link2dCameras = nullptr;
  SliderWithLabel* m_brightnessSlider = nullptr;
  SliderWithLabel* m_gridAlphaSlider = nullptr;
  SliderWithLabel* m_fovSlider = nullptr;
  QCheckBox* m_showAxes = nullptr;
  QComboBox* m_filterModeCombo = nullptr;
  QCheckBox* m_enableMsaa = nullptr;
  QSpinBox* m_fpsLimitSpin = nullptr;
  QComboBox* m_themeCombo = nullptr;
  QComboBox* m_materialBrowserIconSizeCombo = nullptr;
  QComboBox* m_rendererFontSizeCombo = nullptr;
  QComboBox* m_pythonConsoleFontFamilyCombo = nullptr;
  QSpinBox* m_pythonConsoleFontSizeSpin = nullptr;

public:
  explicit ViewPreferencePane(QWidget* parent = nullptr);

private:
  void createGui();
  QWidget* createViewPreferences();

  void bindEvents();

  bool canResetToDefaults() override;
  void doResetToDefaults() override;
  void updateControls() override;
  bool validate() override;

  int findThemeIndex(const QString& theme) const;
private slots:
  void layoutChanged(int index);
  void link2dCamerasChanged(int state);
  void brightnessChanged(int value);
  void gridAlphaChanged(int value);
  void fovChanged(int value);
  void showAxesChanged(int state);
  void enableMsaaChanged(int state);
  void fpsLimitChanged(int value);
  void filterModeChanged(int index);
  void themeChanged(int index);
  void materialBrowserIconSizeChanged(int index);
  void rendererFontSizeChanged(const QString& text);
  void pythonConsoleFontFamilyChanged(int index);
  void pythonConsoleFontSizeChanged(int value);
};

} // namespace tb::ui
