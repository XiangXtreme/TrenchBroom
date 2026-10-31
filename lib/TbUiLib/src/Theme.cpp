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

#include "ui/Theme.h"

#include <QColor>
#include <QPalette>
#include <QRegularExpression>
#include <QString>

#include "base/PreferenceManager.h"
#include "prefs/Preferences.h"
#include "ui/QColorUtils.h"

#include <array>
#include <cmath>
#include <utility>

namespace tb::ui
{
namespace
{

QColor blend(const QColor& background, const QColor& foreground, const double amount)
{
  const auto channel = [amount](const int backgroundValue, const int foregroundValue) {
    return int(std::lround(
      double(backgroundValue) * (1.0 - amount) + double(foregroundValue) * amount));
  };

  return {
    channel(background.red(), foreground.red()),
    channel(background.green(), foreground.green()),
    channel(background.blue(), foreground.blue())};
}

QString styleSheetColor(const QColor& color)
{
  if (color.alpha() == 255)
  {
    return color.name(QColor::HexRgb);
  }

  return QStringLiteral("rgba(%1, %2, %3, %4)")
    .arg(color.red())
    .arg(color.green())
    .arg(color.blue())
    .arg(color.alpha());
}

Color preferenceOrPaletteColor(
  const Preference<Color>& preference, const QColor& paletteColor)
{
  const auto& configuredColor = pref(preference);
  return configuredColor == preference.defaultValue ? fromQColor(paletteColor)
                                                    : configuredColor;
}

} // namespace

ThemeTokens makeSystemThemeTokens(const QPalette& palette)
{
  auto tokens = ThemeTokens{};

  tokens.windowBackground = palette.color(QPalette::Active, QPalette::Window);
  tokens.editorBackground = palette.color(QPalette::Active, QPalette::Base);
  tokens.sidebarBackground = tokens.windowBackground;
  tokens.panelBackground = tokens.windowBackground;
  tokens.elevatedBackground = palette.color(QPalette::Active, QPalette::Button);
  tokens.inputBackground = tokens.editorBackground;
  tokens.alternateBackground = palette.color(QPalette::Active, QPalette::AlternateBase);
  tokens.buttonBackground = palette.color(QPalette::Active, QPalette::Button);

  const auto highlight = palette.color(QPalette::Active, QPalette::Highlight);
  tokens.hoverBackground = blend(tokens.buttonBackground, highlight, 0.12);
  tokens.pressedBackground = blend(tokens.buttonBackground, highlight, 0.22);
  tokens.selectionBackground = highlight;
  tokens.inactiveSelectionBackground =
    palette.color(QPalette::Inactive, QPalette::Highlight);

  tokens.border = palette.color(QPalette::Active, QPalette::Mid);
  tokens.strongBorder = palette.color(QPalette::Active, QPalette::Dark);
  tokens.focusBorder = highlight;

  tokens.text = palette.color(QPalette::Active, QPalette::WindowText);
  tokens.secondaryText = blend(tokens.windowBackground, tokens.text, 0.68);
  tokens.disabledText = palette.color(QPalette::Disabled, QPalette::WindowText);
  tokens.inverseText = palette.color(QPalette::Active, QPalette::HighlightedText);

  tokens.accent = palette.color(QPalette::Active, QPalette::Link);
  tokens.error = QColor{196, 43, 28};
  tokens.warning = QColor{157, 93, 0};
  tokens.success = QColor{16, 124, 16};

  return tokens;
}

QPalette makeThemePalette(const ThemeTokens& tokens)
{
  auto palette = QPalette{tokens.buttonBackground};

  palette.setColor(QPalette::All, QPalette::Window, tokens.windowBackground);
  palette.setColor(QPalette::All, QPalette::Base, tokens.editorBackground);
  palette.setColor(QPalette::All, QPalette::AlternateBase, tokens.alternateBackground);
  palette.setColor(QPalette::All, QPalette::Button, tokens.buttonBackground);
  palette.setColor(QPalette::All, QPalette::ToolTipBase, tokens.elevatedBackground);

  palette.setColor(QPalette::All, QPalette::WindowText, tokens.text);
  palette.setColor(QPalette::All, QPalette::Text, tokens.text);
  palette.setColor(QPalette::All, QPalette::ButtonText, tokens.text);
  palette.setColor(QPalette::All, QPalette::ToolTipText, tokens.text);
  palette.setColor(QPalette::All, QPalette::PlaceholderText, tokens.secondaryText);
  palette.setColor(QPalette::All, QPalette::BrightText, tokens.inverseText);

  palette.setColor(QPalette::All, QPalette::Light, tokens.elevatedBackground);
  palette.setColor(QPalette::All, QPalette::Midlight, tokens.hoverBackground);
  palette.setColor(QPalette::All, QPalette::Mid, tokens.border);
  palette.setColor(QPalette::All, QPalette::Dark, tokens.strongBorder);
  palette.setColor(QPalette::All, QPalette::Shadow, tokens.strongBorder);

  palette.setColor(QPalette::Active, QPalette::Highlight, tokens.selectionBackground);
  palette.setColor(
    QPalette::Inactive, QPalette::Highlight, tokens.inactiveSelectionBackground);
  palette.setColor(QPalette::Disabled, QPalette::Highlight, tokens.pressedBackground);
  palette.setColor(QPalette::All, QPalette::HighlightedText, tokens.inverseText);

  palette.setColor(QPalette::All, QPalette::Link, tokens.accent);
  palette.setColor(QPalette::All, QPalette::LinkVisited, tokens.accent);
  palette.setColor(QPalette::All, QPalette::Accent, tokens.accent);

  palette.setColor(QPalette::Disabled, QPalette::WindowText, tokens.disabledText);
  palette.setColor(QPalette::Disabled, QPalette::Text, tokens.disabledText);
  palette.setColor(QPalette::Disabled, QPalette::ButtonText, tokens.disabledText);

  return palette;
}

Color browserBackgroundColor(const QPalette& palette)
{
  return preferenceOrPaletteColor(
    Preferences::BrowserBackgroundColor, palette.color(QPalette::Active, QPalette::Base));
}

Color browserGroupBackgroundColor(const QPalette& palette)
{
  return preferenceOrPaletteColor(
    Preferences::BrowserGroupBackgroundColor,
    palette.color(QPalette::Active, QPalette::AlternateBase));
}

Color browserTextColor(const QPalette& palette)
{
  return preferenceOrPaletteColor(
    Preferences::BrowserTextColor, palette.color(QPalette::Active, QPalette::Text));
}

Color browserCellBackgroundColor(const QPalette& palette)
{
  return fromQColor(palette.color(QPalette::Active, QPalette::AlternateBase));
}

Color browserCellHoverColor(const QPalette& palette)
{
  return fromQColor(palette.color(QPalette::Active, QPalette::Midlight));
}

Color browserCellSelectedColor(const QPalette& palette)
{
  auto color = palette.color(QPalette::Active, QPalette::Highlight);
  color.setAlpha(64);
  return fromQColor(color);
}

Color browserErrorColor(const QPalette& palette)
{
  const auto lightBackground =
    palette.color(QPalette::Active, QPalette::Base).lightnessF() > 0.5f;
  return fromQColor(lightBackground ? QColor{196, 43, 28} : QColor{244, 135, 113});
}

bool expandThemeStyleSheet(QString& styleSheet, const ThemeTokens& tokens, QString* error)
{
  const auto replacements = std::array{
    std::pair{QStringLiteral("@tb-window-background"), tokens.windowBackground},
    std::pair{QStringLiteral("@tb-editor-background"), tokens.editorBackground},
    std::pair{QStringLiteral("@tb-sidebar-background"), tokens.sidebarBackground},
    std::pair{QStringLiteral("@tb-panel-background"), tokens.panelBackground},
    std::pair{QStringLiteral("@tb-elevated-background"), tokens.elevatedBackground},
    std::pair{QStringLiteral("@tb-input-background"), tokens.inputBackground},
    std::pair{QStringLiteral("@tb-alternate-background"), tokens.alternateBackground},
    std::pair{QStringLiteral("@tb-button-background"), tokens.buttonBackground},
    std::pair{QStringLiteral("@tb-hover-background"), tokens.hoverBackground},
    std::pair{QStringLiteral("@tb-pressed-background"), tokens.pressedBackground},
    std::pair{QStringLiteral("@tb-selection-background"), tokens.selectionBackground},
    std::pair{
      QStringLiteral("@tb-inactive-selection-background"),
      tokens.inactiveSelectionBackground},
    std::pair{QStringLiteral("@tb-border"), tokens.border},
    std::pair{QStringLiteral("@tb-strong-border"), tokens.strongBorder},
    std::pair{QStringLiteral("@tb-focus-border"), tokens.focusBorder},
    std::pair{QStringLiteral("@tb-text"), tokens.text},
    std::pair{QStringLiteral("@tb-secondary-text"), tokens.secondaryText},
    std::pair{QStringLiteral("@tb-disabled-text"), tokens.disabledText},
    std::pair{QStringLiteral("@tb-inverse-text"), tokens.inverseText},
    std::pair{QStringLiteral("@tb-accent"), tokens.accent},
    std::pair{QStringLiteral("@tb-error"), tokens.error},
    std::pair{QStringLiteral("@tb-warning"), tokens.warning},
    std::pair{QStringLiteral("@tb-success"), tokens.success},
  };

  auto expanded = styleSheet;
  for (const auto& [placeholder, color] : replacements)
  {
    expanded.replace(placeholder, styleSheetColor(color));
  }

  const auto darkTheme = tokens.windowBackground.lightnessF() <= 0.5f;
  expanded.replace(
    QStringLiteral("@tb-combo-arrow-image"),
    darkTheme ? QStringLiteral("url(:/controls/chevron-down-dark)")
              : QStringLiteral("url(:/controls/chevron-down-light)"));
  expanded.replace(
    QStringLiteral("@tb-combo-arrow-disabled-image"),
    darkTheme ? QStringLiteral("url(:/controls/chevron-down-disabled-dark)")
              : QStringLiteral("url(:/controls/chevron-down-disabled-light)"));
  expanded.replace(
    QStringLiteral("@tb-spin-up-arrow-image"),
    darkTheme ? QStringLiteral("url(:/controls/chevron-up-dark)")
              : QStringLiteral("url(:/controls/chevron-up-light)"));
  expanded.replace(
    QStringLiteral("@tb-spin-up-arrow-disabled-image"),
    darkTheme ? QStringLiteral("url(:/controls/chevron-up-disabled-dark)")
              : QStringLiteral("url(:/controls/chevron-up-disabled-light)"));
  expanded.replace(
    QStringLiteral("@tb-tab-close-image"),
    darkTheme ? QStringLiteral("url(:/controls/tab-close-dark)")
              : QStringLiteral("url(:/controls/tab-close-light)"));
  expanded.replace(
    QStringLiteral("@tb-tab-close-hover-image"),
    darkTheme ? QStringLiteral("url(:/controls/tab-close-hover-dark)")
              : QStringLiteral("url(:/controls/tab-close-hover-light)"));

  const auto unknownTokenExpression =
    QRegularExpression{QStringLiteral(R"(@tb-[a-z0-9-]+)")};
  const auto match = unknownTokenExpression.match(expanded);
  if (match.hasMatch())
  {
    if (error != nullptr)
    {
      *error = QStringLiteral("Unknown theme token: %1").arg(match.captured());
    }
    return false;
  }

  styleSheet = std::move(expanded);
  if (error != nullptr)
  {
    error->clear();
  }
  return true;
}

} // namespace tb::ui
