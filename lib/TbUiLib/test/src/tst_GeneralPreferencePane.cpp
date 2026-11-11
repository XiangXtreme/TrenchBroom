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

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>

#include "base/PreferenceManager.h"
#include "mcp/McpBridgeConfig.h"
#include "prefs/Preferences.h"
#include "ui/AppController.h"
#include "ui/AppControllerFixture.h"
#include "ui/ExtensionsPreferencePane.h"
#include "ui/GeneralPreferencePane.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("GeneralPreferencePane")
{
  auto appControllerFixture = AppControllerFixture{};
  auto& prefs = PreferenceManager::instance();
  prefs.set(Preferences::Language, Preferences::languageChinese());
  prefs.set(Preferences::PrefixWorldspawnHeaderOnCopy, true);
  prefs.set(Preferences::Enable2DBoxSelection, true);
  prefs.set(Preferences::PythonPluginDirectories, "C:/tb/plugin");
  prefs.set(Preferences::PieMenuAction, "Menu/Edit/Undo|Menu/Edit/Redo");
  prefs.set(Preferences::PrefabDirectory, "C:/tb/prefabs");

  auto tempDir = QTemporaryDir{};
  REQUIRE(tempDir.isValid());
  const auto mcpConfigPath = QDir{tempDir.path()}.filePath("mcp-config.json");
  auto mcpConfig = mcp::defaultBridgeConfig();
  mcpConfig.mode = mcp::McpMode::Edit;
  REQUIRE(mcp::writeBridgeConfig(mcpConfig, mcpConfigPath));
  auto pane = GeneralPreferencePane{};
  auto extensions =
    ExtensionsPreferencePane{appControllerFixture.appController(), mcpConfigPath};

  SECTION("loads general preferences")
  {
    auto* prefixCheckBox = static_cast<QCheckBox*>(nullptr);
    auto* boxSelectionCheckBox = static_cast<QCheckBox*>(nullptr);
    for (auto* checkBox : pane.findChildren<QCheckBox*>())
    {
      if (checkBox->text() == QStringLiteral("Prefix worldspawn header on copy"))
      {
        prefixCheckBox = checkBox;
      }
      if (checkBox->text() == QStringLiteral("Enable 2D box selection with Ctrl+drag"))
      {
        boxSelectionCheckBox = checkBox;
      }
    }

    REQUIRE(prefixCheckBox != nullptr);
    CHECK(prefixCheckBox->isChecked());
    REQUIRE(boxSelectionCheckBox != nullptr);
    CHECK(boxSelectionCheckBox->isChecked());

    auto foundChinese = false;
    for (auto* radioButton : pane.findChildren<QRadioButton*>())
    {
      if (radioButton->text() == QString::fromStdString(Preferences::languageChinese()))
      {
        foundChinese = radioButton->isChecked();
      }
    }
    CHECK(foundChinese);
  }

  SECTION("reset only restores the selected category")
  {
    pane.resetToDefaults();

    CHECK(prefs.get(Preferences::Language) == Preferences::languageEnglish());
    CHECK_FALSE(prefs.get(Preferences::PrefixWorldspawnHeaderOnCopy));
    CHECK_FALSE(prefs.get(Preferences::Enable2DBoxSelection));
    CHECK(prefs.get(Preferences::PythonPluginDirectories) == "C:/tb/plugin");
    CHECK(prefs.get(Preferences::PieMenuAction) == "Menu/Edit/Undo|Menu/Edit/Redo");
    CHECK(prefs.get(Preferences::PrefabDirectory).empty());

    const auto unchangedMcpConfig = mcp::readBridgeConfig(mcpConfigPath);
    REQUIRE(unchangedMcpConfig);
    CHECK(unchangedMcpConfig->mode == mcp::McpMode::Edit);
    prefs.set(Preferences::Enable2DBoxSelection, true);
    extensions.resetToDefaults();
    CHECK(prefs.get(Preferences::Enable2DBoxSelection));
    const auto resetMcpConfig = mcp::readBridgeConfig(mcpConfigPath);
    REQUIRE(resetMcpConfig);
    CHECK(resetMcpConfig->mode == mcp::McpMode::Off);
  }

  SECTION("loads prefab directory preference")
  {
    auto* prefabDirectoryEdit = pane.findChild<QLineEdit*>("prefabDirectoryEdit");

    REQUIRE(prefabDirectoryEdit != nullptr);
    CHECK(prefabDirectoryEdit->text() == QStringLiteral("C:/tb/prefabs"));
  }

  SECTION("reset clears prefab directory preference")
  {
    pane.resetToDefaults();

    CHECK(prefs.get(Preferences::PrefabDirectory).empty());
  }

  SECTION("links to separate management dialogs")
  {
    auto foundPieMenuButton = false;
    auto foundPluginButton = false;

    auto buttons = pane.findChildren<QPushButton*>();
    buttons.append(extensions.findChildren<QPushButton*>());
    for (auto* button : buttons)
    {
      foundPieMenuButton =
        foundPieMenuButton || button->text() == QStringLiteral("Pie Menu Settings...");
      foundPluginButton =
        foundPluginButton || button->text() == QStringLiteral("Python Plugin Manager...");
    }

    CHECK(foundPieMenuButton);
    CHECK(foundPluginButton);
  }

  SECTION("scrolls compact content without compressing MCP rows")
  {
    extensions.resize(720, 260);
    extensions.show();
    QApplication::processEvents();

    auto* scrollArea = extensions.findChild<QScrollArea*>("PreferencePane_ScrollArea");
    auto* modeCombo = extensions.findChild<QComboBox*>("McpSettings_Mode");
    auto* statusLabel = extensions.findChild<QLabel*>("McpSettings_Status");
    auto* httpUrl = extensions.findChild<QLineEdit*>("McpSettings_HttpUrl");
    auto* copyClaudeCommand =
      extensions.findChild<QPushButton*>("McpSettings_CopyClaudeCommand");
    REQUIRE(scrollArea != nullptr);
    REQUIRE(modeCombo != nullptr);
    REQUIRE(statusLabel != nullptr);
    REQUIRE(httpUrl != nullptr);
    REQUIRE(copyClaudeCommand != nullptr);

    CHECK(scrollArea->verticalScrollBar()->maximum() > 0);
    CHECK(modeCombo->geometry().bottom() < statusLabel->geometry().top());
    CHECK(statusLabel->geometry().bottom() < httpUrl->geometry().top());
    CHECK(httpUrl->geometry().bottom() < copyClaudeCommand->geometry().top());
  }
}

} // namespace tb::ui
