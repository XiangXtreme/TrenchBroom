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

#include <QColor>
#include <QFile>
#include <QGuiApplication>
#include <QPalette>
#include <QScopeGuard>
#include <QString>
#include <QTemporaryDir>
#include <QVariant>

#include "base/PreferenceManager.h"
#include "prefs/Preferences.h"
#include "ui/QColorUtils.h"
#include "ui/QPathUtils.h"
#include "ui/Theme.h"
#include "ui/ThemeRegistry.h"

#include <algorithm>
#include <array>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("Theme")
{
  const auto builtInThemes = ThemeRegistry{QGuiApplication::palette()};

  SECTION("loads built-in themes from distributable definitions")
  {
    REQUIRE(builtInThemes.themes().size() == 4u);

    const auto* systemTheme = builtInThemes.findTheme(QStringLiteral("builtin.system"));
    const auto* lightTheme = builtInThemes.findTheme(QStringLiteral("builtin.light"));
    const auto* darkTheme = builtInThemes.findTheme(QStringLiteral("builtin.dark"));
    const auto* blenderTheme = builtInThemes.findTheme(QStringLiteral("builtin.blender"));
    REQUIRE(systemTheme != nullptr);
    REQUIRE(lightTheme != nullptr);
    REQUIRE(darkTheme != nullptr);
    REQUIRE(blenderTheme != nullptr);

    CHECK(systemTheme->name == QStringLiteral("System"));
    CHECK(systemTheme->appearance == ThemeAppearance::System);
    CHECK(lightTheme->name == QStringLiteral("Light"));
    CHECK(lightTheme->appearance == ThemeAppearance::Light);
    CHECK(darkTheme->name == QStringLiteral("Dark"));
    CHECK(darkTheme->appearance == ThemeAppearance::Dark);
    CHECK(blenderTheme->name == QStringLiteral("Blender"));
    CHECK(blenderTheme->appearance == ThemeAppearance::Dark);
    CHECK(builtInThemes.diagnostics().empty());
  }

  SECTION("loads light theme tokens")
  {
    const auto& tokens =
      builtInThemes.resolveTheme(QStringLiteral("builtin.light")).tokens;

    CHECK(tokens.windowBackground == (QColor{243, 243, 243}));
    CHECK(tokens.editorBackground == (QColor{255, 255, 255}));
    CHECK(tokens.focusBorder == (QColor{0, 120, 212}));
    CHECK(tokens.text == (QColor{31, 31, 31}));
  }

  SECTION("loads dark theme tokens")
  {
    const auto& tokens =
      builtInThemes.resolveTheme(QStringLiteral("builtin.dark")).tokens;

    CHECK(tokens.windowBackground == (QColor{24, 24, 24}));
    CHECK(tokens.editorBackground == (QColor{31, 31, 31}));
    CHECK(tokens.focusBorder == (QColor{0, 127, 212}));
    CHECK(tokens.text == (QColor{204, 204, 204}));
  }

  SECTION("resolves inherited Blender theme tokens")
  {
    const auto& tokens =
      builtInThemes.resolveTheme(QStringLiteral("builtin.blender")).tokens;

    CHECK(tokens.windowBackground == (QColor{32, 33, 36}));
    CHECK(tokens.editorBackground == (QColor{39, 40, 43}));
    CHECK(tokens.sidebarBackground == (QColor{37, 38, 41}));
    CHECK(tokens.elevatedBackground == (QColor{48, 50, 54}));
    CHECK(tokens.inputBackground == (QColor{53, 55, 60}));
    CHECK(tokens.buttonBackground == (QColor{53, 55, 60}));
    CHECK(tokens.selectionBackground == (QColor{85, 64, 50}));
    CHECK(tokens.focusBorder == (QColor{234, 131, 47}));
    CHECK(tokens.text == (QColor{212, 214, 218}));
    CHECK(tokens.secondaryText == (QColor{163, 166, 173}));
    CHECK(tokens.accent == (QColor{234, 131, 47}));
  }

  SECTION("makeThemePalette")
  {
    const auto& tokens =
      builtInThemes.resolveTheme(QStringLiteral("builtin.dark")).tokens;
    const auto palette = makeThemePalette(tokens);

    CHECK(palette.color(QPalette::Active, QPalette::Window) == tokens.windowBackground);
    CHECK(palette.color(QPalette::Active, QPalette::Base) == tokens.editorBackground);
    CHECK(
      palette.color(QPalette::Active, QPalette::Highlight) == tokens.selectionBackground);
    CHECK(
      palette.color(QPalette::Inactive, QPalette::Highlight)
      == tokens.inactiveSelectionBackground);
    CHECK(palette.color(QPalette::Disabled, QPalette::Text) == tokens.disabledText);
    CHECK(
      palette.color(QPalette::Active, QPalette::HighlightedText) == tokens.inverseText);
    CHECK(palette.color(QPalette::Inactive, QPalette::HighlightedText) == tokens.text);
    CHECK(
      palette.color(QPalette::Disabled, QPalette::HighlightedText)
      == tokens.disabledText);
  }

  SECTION("semantic colors use the installed theme rather than the saved preference")
  {
    auto* app = QCoreApplication::instance();
    REQUIRE(app);
    const auto palette = QGuiApplication::palette();
    for (const auto& [role, property] : std::array{
           std::pair{SemanticColor::Error, "tbThemeErrorColor"},
           std::pair{SemanticColor::Warning, "tbThemeWarningColor"},
           std::pair{SemanticColor::Success, "tbThemeSuccessColor"}})
    {
      const auto previous = app->property(property);
      const auto restore = qScopeGuard([&] { app->setProperty(property, previous); });
      app->setProperty(property, QVariant{});
      CHECK(themeSemanticColor(palette, role).isValid());
      const auto custom = QColor{101, 132, 163};
      app->setProperty(property, custom);
      CHECK(themeSemanticColor(palette, role) == custom);
    }
  }

  SECTION("browser colors follow the theme until they are customized")
  {
    const auto originalBackground = pref(Preferences::BrowserBackgroundColor);
    const auto originalGroupBackground = pref(Preferences::BrowserGroupBackgroundColor);
    const auto originalText = pref(Preferences::BrowserTextColor);
    const auto palette = makeThemePalette(
      builtInThemes.resolveTheme(QStringLiteral("builtin.light")).tokens);

    setPref(
      Preferences::BrowserBackgroundColor,
      Preferences::BrowserBackgroundColor.defaultValue);
    setPref(
      Preferences::BrowserGroupBackgroundColor,
      Preferences::BrowserGroupBackgroundColor.defaultValue);
    setPref(Preferences::BrowserTextColor, Preferences::BrowserTextColor.defaultValue);

    CHECK(toQColor(browserBackgroundColor(palette)) == palette.color(QPalette::Base));
    CHECK(
      toQColor(browserGroupBackgroundColor(palette))
      == palette.color(QPalette::AlternateBase));
    CHECK(toQColor(browserTextColor(palette)) == palette.color(QPalette::Text));
    CHECK(
      toQColor(browserCellBackgroundColor(palette))
      == palette.color(QPalette::AlternateBase));
    CHECK(toQColor(browserCellHoverColor(palette)) == palette.color(QPalette::Midlight));

    auto expectedSelectedColor = palette.color(QPalette::Highlight);
    expectedSelectedColor.setAlpha(64);
    CHECK(toQColor(browserCellSelectedColor(palette)) == expectedSelectedColor);
    CHECK(toQColor(browserErrorColor(palette)) == (QColor{196, 43, 28}));
    CHECK(
      toQColor(browserErrorColor(makeThemePalette(
        builtInThemes.resolveTheme(QStringLiteral("builtin.dark")).tokens)))
      == (QColor{244, 135, 113}));

    const auto customBackground = Color{RgbF{0.2f, 0.3f, 0.4f}};
    setPref(Preferences::BrowserBackgroundColor, customBackground);
    CHECK(browserBackgroundColor(palette) == customBackground);

    setPref(Preferences::BrowserBackgroundColor, originalBackground);
    setPref(Preferences::BrowserGroupBackgroundColor, originalGroupBackground);
    setPref(Preferences::BrowserTextColor, originalText);
  }

  SECTION("expandThemeStyleSheet")
  {
    const auto& tokens =
      builtInThemes.resolveTheme(QStringLiteral("builtin.dark")).tokens;
    auto styleSheet = QStringLiteral(
      "QWidget { color: @tb-text; background: @tb-window-background; "
      "image: @tb-combo-arrow-image; }");
    auto error = QString{};

    CHECK(expandThemeStyleSheet(styleSheet, tokens, &error));
    CHECK(error.isEmpty());
    CHECK(
      styleSheet
      == QStringLiteral("QWidget { color: #cccccc; background: #181818; "
                        "image: url(:/controls/chevron-down-dark); }"));
  }

  SECTION("expandThemeStyleSheet selects light control assets")
  {
    const auto& tokens =
      builtInThemes.resolveTheme(QStringLiteral("builtin.light")).tokens;
    auto styleSheet = QStringLiteral(
      "QComboBox::down-arrow { image: @tb-combo-arrow-image; } "
      "QComboBox::down-arrow:disabled { image: @tb-combo-arrow-disabled-image; } "
      "QSpinBox::up-arrow { image: @tb-spin-up-arrow-image; } "
      "QSpinBox::up-arrow:disabled { image: @tb-spin-up-arrow-disabled-image; }");
    auto error = QString{};

    CHECK(expandThemeStyleSheet(styleSheet, tokens, &error));
    CHECK(error.isEmpty());
    CHECK(
      styleSheet
      == QStringLiteral(
        "QComboBox::down-arrow { image: url(:/controls/chevron-down-light); } "
        "QComboBox::down-arrow:disabled { image: "
        "url(:/controls/chevron-down-disabled-light); } "
        "QSpinBox::up-arrow { image: url(:/controls/chevron-up-light); } "
        "QSpinBox::up-arrow:disabled { image: "
        "url(:/controls/chevron-up-disabled-light); }"));
  }

  SECTION("expandThemeStyleSheet selects tab close assets")
  {
    auto error = QString{};
    const auto& darkTokens =
      builtInThemes.resolveTheme(QStringLiteral("builtin.dark")).tokens;
    auto darkStyleSheet = QStringLiteral(
      "QTabBar::close-button { image: @tb-tab-close-image; } "
      "QTabBar::close-button:hover { image: @tb-tab-close-hover-image; }");
    CHECK(expandThemeStyleSheet(darkStyleSheet, darkTokens, &error));
    CHECK(error.isEmpty());
    CHECK(
      darkStyleSheet
      == QStringLiteral(
        "QTabBar::close-button { image: url(:/controls/tab-close-dark); } "
        "QTabBar::close-button:hover { image: "
        "url(:/controls/tab-close-hover-dark); }"));

    const auto& lightTokens =
      builtInThemes.resolveTheme(QStringLiteral("builtin.light")).tokens;
    auto lightStyleSheet = QStringLiteral(
      "QTabBar::close-button { image: @tb-tab-close-image; } "
      "QTabBar::close-button:hover { image: @tb-tab-close-hover-image; }");
    CHECK(expandThemeStyleSheet(lightStyleSheet, lightTokens, &error));
    CHECK(error.isEmpty());
    CHECK(
      lightStyleSheet
      == QStringLiteral(
        "QTabBar::close-button { image: url(:/controls/tab-close-light); } "
        "QTabBar::close-button:hover { image: "
        "url(:/controls/tab-close-hover-light); }"));
  }

  SECTION("expandThemeStyleSheet rejects unknown tokens without changing the input")
  {
    const auto& tokens =
      builtInThemes.resolveTheme(QStringLiteral("builtin.dark")).tokens;
    const auto original = QStringLiteral("QWidget { color: @tb-unknown-color; }");
    auto styleSheet = original;
    auto error = QString{};

    CHECK_FALSE(expandThemeStyleSheet(styleSheet, tokens, &error));
    CHECK(styleSheet == original);
    CHECK(error == QStringLiteral("Unknown theme token: @tb-unknown-color"));
  }

  SECTION("maps legacy built-in names and aliases to stable IDs")
  {
    CHECK(
      builtInThemes.canonicalThemeId(QStringLiteral("System"))
      == QStringLiteral("builtin.system"));
    CHECK(
      builtInThemes.canonicalThemeId(QStringLiteral("light"))
      == QStringLiteral("builtin.light"));
    CHECK(
      builtInThemes.canonicalThemeId(QStringLiteral("Dark"))
      == QStringLiteral("builtin.dark"));
    CHECK(
      builtInThemes.canonicalThemeId(QStringLiteral("blender"))
      == QStringLiteral("builtin.blender"));
    CHECK(
      builtInThemes.canonicalThemeId(QStringLiteral("publisher.custom"))
      == QStringLiteral("publisher.custom"));
  }

  SECTION("loads validated user themes with inheritance")
  {
    auto directory = QTemporaryDir{};
    REQUIRE(directory.isValid());

    const auto writeTheme = [&](const QString& fileName, const QByteArray& contents) {
      auto file = QFile{directory.filePath(fileName)};
      REQUIRE(file.open(QFile::WriteOnly));
      REQUIRE(file.write(contents) == contents.size());
    };

    writeTheme(QStringLiteral("10-midnight.tbtheme"), QByteArray{R"({
        "schemaVersion": 1,
        "id": "acme.midnight",
        "name": "Midnight",
        "author": "Acme",
        "appearance": "dark",
        "inherits": "builtin.dark",
        "colors": {
          "accent": "#123456",
          "focusBorder": "#654321"
        }
      })"});
    writeTheme(QStringLiteral("20-unknown-field.tbtheme"), QByteArray{R"({
        "schemaVersion": 1,
        "id": "acme.unsafe",
        "name": "Unsafe",
        "appearance": "dark",
        "inherits": "builtin.dark",
        "colors": {},
        "stylesheet": "QWidget { margin: 100px; }"
      })"});
    writeTheme(QStringLiteral("30-duplicate.tbtheme"), QByteArray{R"({
        "schemaVersion": 1,
        "id": "builtin.dark",
        "name": "Replacement Dark",
        "appearance": "dark",
        "inherits": "builtin.dark",
        "colors": {}
      })"});
    writeTheme(QStringLiteral("31-duplicate-first.tbtheme"), QByteArray{R"({
        "schemaVersion": 1,
        "id": "acme.duplicate",
        "name": "First Duplicate",
        "appearance": "dark",
        "inherits": "builtin.dark",
        "colors": {}
      })"});
    writeTheme(QStringLiteral("32-duplicate-second.tbtheme"), QByteArray{R"({
        "schemaVersion": 1,
        "id": "acme.duplicate",
        "name": "Second Duplicate",
        "appearance": "dark",
        "inherits": "builtin.dark",
        "colors": {}
      })"});
    writeTheme(QStringLiteral("40-missing-parent.tbtheme"), QByteArray{R"({
        "schemaVersion": 1,
        "id": "acme.orphan",
        "name": "Orphan",
        "appearance": "dark",
        "inherits": "missing.theme",
        "colors": {}
      })"});
    writeTheme(QStringLiteral("50-cycle-a.tbtheme"), QByteArray{R"({
        "schemaVersion": 1,
        "id": "acme.cycle-a",
        "name": "Cycle A",
        "appearance": "dark",
        "inherits": "acme.cycle-b",
        "colors": {}
      })"});
    writeTheme(QStringLiteral("60-cycle-b.tbtheme"), QByteArray{R"({
        "schemaVersion": 1,
        "id": "acme.cycle-b",
        "name": "Cycle B",
        "appearance": "dark",
        "inherits": "acme.cycle-a",
        "colors": {}
      })"});

    const auto registry =
      ThemeRegistry{QGuiApplication::palette(), pathFromQString(directory.path())};
    const auto* theme = registry.findTheme(QStringLiteral("acme.midnight"));
    REQUIRE(theme != nullptr);
    CHECK(theme->name == QStringLiteral("Midnight"));
    CHECK(theme->author == QStringLiteral("Acme"));
    CHECK(theme->tokens.accent == (QColor{0x12, 0x34, 0x56}));
    CHECK(theme->tokens.focusBorder == (QColor{0x65, 0x43, 0x21}));
    CHECK(theme->tokens.editorBackground == (QColor{31, 31, 31}));

    const auto* duplicate = registry.findTheme(QStringLiteral("acme.duplicate"));
    REQUIRE(duplicate != nullptr);
    CHECK(duplicate->name == QStringLiteral("First Duplicate"));

    CHECK(registry.findTheme(QStringLiteral("acme.unsafe")) == nullptr);
    CHECK(registry.findTheme(QStringLiteral("acme.orphan")) == nullptr);
    CHECK(registry.findTheme(QStringLiteral("acme.cycle-a")) == nullptr);
    CHECK(registry.findTheme(QStringLiteral("acme.cycle-b")) == nullptr);
    CHECK(
      registry.resolveTheme(QStringLiteral("builtin.dark")).name
      == QStringLiteral("Dark"));

    const auto hasDiagnostic = [&](const QString& text) {
      return std::ranges::any_of(registry.diagnostics(), [&](const auto& diagnostic) {
        return diagnostic.message.contains(text);
      });
    };
    CHECK(hasDiagnostic(QStringLiteral("Unknown field: stylesheet")));
    CHECK(
      hasDiagnostic(QStringLiteral("Theme IDs beginning with builtin. are reserved")));
    CHECK(hasDiagnostic(QStringLiteral("Duplicate theme ID ignored: acme.duplicate")));
    CHECK(hasDiagnostic(QStringLiteral("Inherited theme not found: missing.theme")));
    CHECK(hasDiagnostic(QStringLiteral("Cyclic theme inheritance")));
  }
}

} // namespace tb::ui
