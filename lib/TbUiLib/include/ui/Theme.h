/*
 Copyright (C) 2026 Kristian Duske

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

#include <QColor>
#include <QPalette>
#include <QString>

#include "base/Color.h"

namespace tb::ui
{

struct ThemeTokens
{
  QColor windowBackground;
  QColor editorBackground;
  QColor sidebarBackground;
  QColor panelBackground;
  QColor elevatedBackground;
  QColor inputBackground;
  QColor alternateBackground;
  QColor buttonBackground;

  QColor hoverBackground;
  QColor pressedBackground;
  QColor selectionBackground;
  QColor inactiveSelectionBackground;

  QColor border;
  QColor strongBorder;
  QColor focusBorder;

  QColor text;
  QColor secondaryText;
  QColor disabledText;
  QColor inverseText;

  QColor accent;
  QColor error;
  QColor warning;
  QColor success;
};

ThemeTokens makeSystemThemeTokens(const QPalette& palette);

QPalette makeThemePalette(const ThemeTokens& tokens);

enum class SemanticColor
{
  Error,
  Warning,
  Success
};

/** Semantic ink for domain renderers, including command-line theme overrides. */
QColor themeSemanticColor(const QPalette& palette, SemanticColor role);

Color browserBackgroundColor(const QPalette& palette);
Color browserGroupBackgroundColor(const QPalette& palette);
Color browserTextColor(const QPalette& palette);
Color browserCellBackgroundColor(const QPalette& palette);
Color browserCellHoverColor(const QPalette& palette);
Color browserCellSelectedColor(const QPalette& palette);
Color browserErrorColor(const QPalette& palette);

/**
 * Expands @tb-* placeholders without changing styleSheet if an unknown token remains.
 */
bool expandThemeStyleSheet(
  QString& styleSheet, const ThemeTokens& tokens, QString* error = nullptr);

} // namespace tb::ui
