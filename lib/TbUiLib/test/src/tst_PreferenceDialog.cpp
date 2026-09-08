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
#include <QColor>
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMetaObject>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableView>
#include <QTimer>
#include <QtTest/QTest>

#include "base/PreferenceManager.h"
#include "prefs/Preferences.h"
#include "ui/AppControllerFixture.h"
#include "ui/ColorsPreferencePane.h"
#include "ui/GamesPreferencePane.h"
#include "ui/KeyboardPreferencePane.h"
#include "ui/KeyboardShortcutModel.h"
#include "ui/McpSettingsWidget.h"
#include "ui/MiscPreferencePane.h"
#include "ui/MousePreferencePane.h"
#include "ui/PreferenceDialog.h"
#include "ui/UpdatePreferencePane.h"
#include "ui/ViewPreferencePane.h"

#include <memory>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("PreferenceDialog")
{
  auto fixture = AppControllerFixture{};
  auto dialog = std::make_unique<PreferenceDialog>(fixture.appController(), nullptr);

  SECTION("opens at a usable size")
  {
    CHECK(dialog->width() >= 800);
    CHECK(dialog->height() >= 300);
  }

  SECTION("uses stable preference panes")
  {
    CHECK(dialog->objectName() == "PreferenceDialog_Dialog");

    auto* navigation = dialog->findChild<QListWidget*>("PreferenceDialog_NavigationList");
    auto* navigationTitle =
      dialog->findChild<QLabel*>("PreferenceDialog_NavigationTitle");
    auto* pages = dialog->findChild<QStackedWidget*>("PreferenceDialog_Pages");
    auto* pageTitle = dialog->findChild<QLabel*>("PreferenceDialog_PageTitle");
    auto* buttonBar = dialog->findChild<QWidget*>("DialogButtonBar");
    REQUIRE(navigation != nullptr);
    REQUIRE(navigationTitle != nullptr);
    REQUIRE(pages != nullptr);
    REQUIRE(pageTitle != nullptr);
    REQUIRE(buttonBar != nullptr);
    CHECK(navigationTitle->property("regionAnchor").toBool());

    auto paneNames = QStringList{};
    for (auto i = 0; i < navigation->count(); ++i)
    {
      paneNames << navigation->item(i)->text();
      navigation->setCurrentRow(i);
      CHECK(pages->currentIndex() == i);
      CHECK(pageTitle->text() == navigation->item(i)->text());
    }

    CHECK(
      paneNames
      == QStringList{"Games", "View", "Colors", "Mouse", "Keyboard", "Misc", "Update"});

    navigation->setCurrentRow(0);
    QTest::keyClick(navigation, Qt::Key_Down);
    CHECK(navigation->currentRow() == 1);
    CHECK(pages->currentIndex() == 1);
  }

  SECTION("opens and closes through AppController")
  {
    auto dialogWasClosed = false;
    QTimer::singleShot(0, [&]() {
      auto* modalDialog =
        qobject_cast<PreferenceDialog*>(QApplication::activeModalWidget());
      if (modalDialog != nullptr)
      {
        dialogWasClosed = true;
        modalDialog->close();
      }
    });

    fixture.appController().showPreferences();

    CHECK(dialogWasClosed);
  }

  dialog.reset();
}

TEST_CASE("PreferenceDialog.preferencePanes")
{
  auto fixture = AppControllerFixture{};

  SECTION("Games")
  {
    auto pane = std::make_unique<GamesPreferencePane>(fixture.appController(), nullptr);
    pane.reset();
  }

  SECTION("View")
  {
    auto pane = std::make_unique<ViewPreferencePane>();

    auto* themeCombo =
      pane->findChild<QComboBox*>(QStringLiteral("ViewPreference_ThemeCombo"));
    REQUIRE(themeCombo != nullptr);
    CHECK(themeCombo->count() >= 4);
    CHECK(themeCombo->itemText(0) == QStringLiteral("System"));
    CHECK(themeCombo->itemText(1) == QStringLiteral("Light"));
    CHECK(themeCombo->itemText(2) == QStringLiteral("Dark"));
    CHECK(themeCombo->itemText(3) == QStringLiteral("Blender"));
    CHECK(themeCombo->itemData(0).toString().toStdString() == Preferences::SystemTheme);
    CHECK(themeCombo->itemData(1).toString().toStdString() == Preferences::LightTheme);
    CHECK(themeCombo->itemData(2).toString().toStdString() == Preferences::DarkTheme);
    CHECK(themeCombo->itemData(3).toString().toStdString() == Preferences::BlenderTheme);

    auto* materialBrowserIconSizeCombo = pane->findChild<QComboBox*>(
      QStringLiteral("ViewPreference_MaterialBrowserIconSizeCombo"));
    REQUIRE(materialBrowserIconSizeCombo != nullptr);
    CHECK(materialBrowserIconSizeCombo->count() == 9);
    CHECK(materialBrowserIconSizeCombo->itemText(0) == QStringLiteral("100%"));
    CHECK(materialBrowserIconSizeCombo->itemData(0).toFloat() == 1.0f);
    CHECK(materialBrowserIconSizeCombo->itemText(8) == QStringLiteral("500%"));
    CHECK(materialBrowserIconSizeCombo->itemData(8).toFloat() == 5.0f);

    auto* pythonConsoleFontFamilyCombo = pane->findChild<QComboBox*>(
      QStringLiteral("ViewPreference_PythonConsoleFontFamilyCombo"));
    REQUIRE(pythonConsoleFontFamilyCombo != nullptr);
    CHECK(pythonConsoleFontFamilyCombo->count() > 1);
    CHECK(pythonConsoleFontFamilyCombo->itemData(0).toString().isEmpty());
    CHECK(pythonConsoleFontFamilyCombo->maxVisibleItems() == 10);
    auto* pythonConsoleFontList =
      qobject_cast<QListView*>(pythonConsoleFontFamilyCombo->view());
    REQUIRE(pythonConsoleFontList != nullptr);
    CHECK(pythonConsoleFontList->uniformItemSizes());
    CHECK(pythonConsoleFontList->maximumHeight() == 320);
    CHECK(
      (pythonConsoleFontList->verticalScrollBarPolicy() == Qt::ScrollBarAsNeeded
       || pythonConsoleFontList->verticalScrollBarPolicy() == Qt::ScrollBarAlwaysOff));

    pane->resize(800, 600);
    pane->show();
    pythonConsoleFontFamilyCombo->showPopup();
    QTRY_VERIFY_WITH_TIMEOUT(pythonConsoleFontList->isVisible(), 500);
    CHECK(pythonConsoleFontList->window()->height() <= 336);
    pythonConsoleFontFamilyCombo->hidePopup();
    pane->hide();

    auto* pythonConsoleFontSizeSpin = pane->findChild<QSpinBox*>(
      QStringLiteral("ViewPreference_PythonConsoleFontSizeSpin"));
    REQUIRE(pythonConsoleFontSizeSpin != nullptr);
    CHECK(pythonConsoleFontSizeSpin->minimum() == Preferences::MinPythonConsoleFontSize);
    CHECK(pythonConsoleFontSizeSpin->maximum() == Preferences::MaxPythonConsoleFontSize);

    auto& prefs = PreferenceManager::instance();
    const auto previousTheme = prefs.get(Preferences::Theme);
    const auto previousMaterialBrowserIconSize =
      prefs.get(Preferences::MaterialBrowserIconSize);
    const auto previousPythonConsoleFontSize =
      prefs.get(Preferences::PythonConsoleFontSize);
    const auto previousPythonConsoleFontFamily =
      prefs.get(Preferences::PythonConsoleFontFamily);
    themeCombo->setCurrentIndex(3);
    REQUIRE(QMetaObject::invokeMethod(
      themeCombo, "activated", Qt::DirectConnection, Q_ARG(int, 3)));
    CHECK(prefs.getPendingValue(Preferences::Theme) == Preferences::BlenderTheme);
    prefs.set(Preferences::Theme, previousTheme);

    materialBrowserIconSizeCombo->setCurrentIndex(8);
    CHECK(prefs.getPendingValue(Preferences::MaterialBrowserIconSize) == 5.0f);
    prefs.set(Preferences::MaterialBrowserIconSize, previousMaterialBrowserIconSize);

    pythonConsoleFontFamilyCombo->setCurrentIndex(1);
    CHECK(
      prefs.getPendingValue(Preferences::PythonConsoleFontFamily)
      == pythonConsoleFontFamilyCombo->itemData(1).toString().toStdString());
    prefs.set(Preferences::PythonConsoleFontFamily, previousPythonConsoleFontFamily);

    const auto newPythonConsoleFontSize = previousPythonConsoleFontSize == 16 ? 17 : 16;
    pythonConsoleFontSizeSpin->setValue(newPythonConsoleFontSize);
    CHECK(
      prefs.getPendingValue(Preferences::PythonConsoleFontSize)
      == newPythonConsoleFontSize);
    prefs.set(Preferences::PythonConsoleFontSize, previousPythonConsoleFontSize);

    pane.reset();
  }

  SECTION("Colors")
  {
    auto pane = std::make_unique<ColorsPreferencePane>();

    auto* table = pane->findChild<QTableView*>(QStringLiteral("ColorsPreference_Table"));
    REQUIRE(table != nullptr);
    REQUIRE(table->model() != nullptr);
    REQUIRE(table->model()->rowCount() > 0);
    CHECK(table->verticalHeader()->isHidden());

    const auto swatch = table->model()
                          ->data(table->model()->index(0, 0), Qt::DecorationRole)
                          .value<QColor>();
    CHECK(swatch.isValid());
    CHECK(table->iconSize() == QSize{48, 12});

    pane.reset();
  }

  SECTION("Mouse")
  {
    auto pane = std::make_unique<MousePreferencePane>();
    pane.reset();
  }

  SECTION("Keyboard")
  {
    auto pane =
      std::make_unique<KeyboardPreferencePane>(fixture.appController(), nullptr);

    auto* table =
      pane->findChild<QTableView*>(QStringLiteral("KeyboardPreference_Table"));
    REQUIRE(table != nullptr);
    auto* header = table->horizontalHeader();
    REQUIRE(header != nullptr);

    CHECK(table->verticalHeader()->isHidden());
    CHECK_FALSE(header->sectionsMovable());
    CHECK(
      header->sectionResizeMode(KeyboardShortcutModel::DescriptionColumn)
      == QHeaderView::Stretch);
    CHECK(
      header->sectionResizeMode(KeyboardShortcutModel::ContextColumn)
      == QHeaderView::Fixed);
    CHECK(header->sectionSize(KeyboardShortcutModel::ContextColumn) == 190);
    CHECK(
      header->sectionResizeMode(KeyboardShortcutModel::ShortcutColumn)
      == QHeaderView::Fixed);
    CHECK(header->sectionSize(KeyboardShortcutModel::ShortcutColumn) == 104);
    CHECK(
      header->sectionResizeMode(KeyboardShortcutModel::AlternativeColumn)
      == QHeaderView::Fixed);
    CHECK(header->sectionSize(KeyboardShortcutModel::AlternativeColumn) == 116);

    auto* proxy = qobject_cast<QSortFilterProxyModel*>(table->model());
    REQUIRE(proxy != nullptr);
    CHECK(proxy->filterKeyColumn() == -1);
    CHECK(
      proxy->headerData(KeyboardShortcutModel::DescriptionColumn, Qt::Horizontal)
        .toString()
      == QStringLiteral("Description"));
    CHECK(
      proxy->headerData(KeyboardShortcutModel::ContextColumn, Qt::Horizontal).toString()
      == QStringLiteral("Context"));
    CHECK(
      proxy->headerData(KeyboardShortcutModel::ShortcutColumn, Qt::Horizontal).toString()
      == QStringLiteral("Shortcut"));
    CHECK(
      proxy->headerData(KeyboardShortcutModel::AlternativeColumn, Qt::Horizontal)
        .toString()
      == QStringLiteral("Alternative"));
    REQUIRE(proxy->rowCount() > 0);
    CHECK_FALSE(proxy->data(proxy->index(0, KeyboardShortcutModel::DescriptionColumn))
                  .toString()
                  .isEmpty());

    pane.reset();
  }

  SECTION("Misc")
  {
    auto pane = std::make_unique<MiscPreferencePane>(fixture.appController());

    auto* mcpSettings = pane->findChild<McpSettingsWidget*>("McpSettings_Group");
    auto* modeCombo = pane->findChild<QComboBox*>("McpSettings_Mode");
    auto* httpUrl = pane->findChild<QLineEdit*>("McpSettings_HttpUrl");
    auto* copyUrl = pane->findChild<QPushButton*>("McpSettings_CopyUrl");
    auto* copyClaudeCommand =
      pane->findChild<QPushButton*>("McpSettings_CopyClaudeCommand");
    REQUIRE(mcpSettings != nullptr);
    REQUIRE(modeCombo != nullptr);
    REQUIRE(httpUrl != nullptr);
    REQUIRE(copyUrl != nullptr);
    REQUIRE(copyClaudeCommand != nullptr);
    CHECK(modeCombo->count() == 3);
    CHECK(httpUrl->isReadOnly());
    CHECK(httpUrl->text().startsWith("http://127.0.0.1:"));
    CHECK(copyUrl->text() == "Copy URL");
    CHECK(copyClaudeCommand->text() == "Copy Setup Command");
    CHECK(mcpSettings->findChildren<QLineEdit*>().size() == 1);
    CHECK(mcpSettings->findChildren<QPushButton*>().size() == 2);
    pane.reset();
  }

  SECTION("Update")
  {
    auto pane = std::make_unique<UpdatePreferencePane>(fixture.appController());
    pane.reset();
  }
}

} // namespace tb::ui
