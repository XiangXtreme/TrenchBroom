/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QApplication>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QToolButton>

#include "mdl/Map.h"
#include "mdl/Map_World.h"
#include "ui/AppControllerFixture.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"
#include "ui/MapSettingsDialog.h"
#include "ui/ModEditor.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
TEST_CASE("MapSettingsDialog")
{
  auto app = AppControllerFixture{};
  auto fixture = MapDocumentFixture{};
  auto& document = fixture.create();
  auto& map = document.map();
  // Simulate opening settings after a map was loaded, rather than at startup.
  mdl::setEnabledMods(map, {"first", "second"});
  auto dialog = MapSettingsDialog{document};
  dialog.show();
  QApplication::processEvents();

  SECTION("bounds support custom vectors, invalid input, defaults and undo")
  {
    auto* custom = dialog.findChild<QRadioButton*>("MapBounds_Custom");
    auto* defaults = dialog.findChild<QRadioButton*>("MapBounds_Game");
    auto* disabled = dialog.findChild<QRadioButton*>("MapBounds_Disabled");
    auto* minimum = dialog.findChild<QLineEdit*>("MapBounds_Min");
    auto* maximum = dialog.findChild<QLineEdit*>("MapBounds_Max");
    REQUIRE(custom);
    REQUIRE(defaults);
    REQUIRE(disabled);
    REQUIRE(minimum);
    REQUIRE(maximum);
    const auto original = mdl::softMapBounds(map);
    custom->click();
    minimum->setText("-128 -256 -512");
    maximum->setText("128 256 512");
    Q_EMIT maximum->editingFinished();
    REQUIRE(mdl::softMapBounds(map).bounds);
    CHECK(mdl::softMapBounds(map).bounds->min == vm::vec3d{-128, -256, -512});
    CHECK(mdl::softMapBounds(map).bounds->max == vm::vec3d{128, 256, 512});
    map.undoCommand();
    CHECK(mdl::softMapBounds(map) == original);
    map.redoCommand();
    CHECK(custom->isChecked());
    CHECK(minimum->text() == "-128 -256 -512");
    CHECK(maximum->text() == "128 256 512");
    const auto valid = mdl::softMapBounds(map);
    minimum->setText("invalid");
    Q_EMIT minimum->editingFinished();
    CHECK(mdl::softMapBounds(map) == valid);
    disabled->click();
    CHECK_FALSE(mdl::softMapBounds(map).bounds);
    CHECK(mdl::softMapBounds(map).source == mdl::SoftMapBoundsType::Map);
    defaults->click();
    CHECK(mdl::softMapBounds(map).source == mdl::SoftMapBoundsType::Game);
    CHECK_FALSE(minimum->isEnabled());
    CHECK_FALSE(maximum->isEnabled());
  }

  SECTION("mods populate on lazy opening and preserve reorder, disable and undo")
  {
    auto* editor = dialog.findChild<ModEditor*>();
    REQUIRE(editor);
    auto* enabled = editor->findChild<QListWidget*>("ModEditor_Enabled");
    auto* moveDown = editor->findChild<QToolButton*>("ModEditor_MoveDown");
    auto* remove = editor->findChild<QToolButton*>("ModEditor_Remove");
    REQUIRE(enabled);
    REQUIRE(moveDown);
    REQUIRE(remove);
    REQUIRE(enabled->count() == 2);
    CHECK(enabled->item(0)->text() == "first");
    enabled->setCurrentRow(0);
    REQUIRE(moveDown->isEnabled());
    moveDown->click();
    CHECK(mdl::enabledMods(map) == std::vector<std::string>{"second", "first"});
    CHECK(enabled->item(0)->text() == "second");
    map.undoCommand();
    CHECK(enabled->item(0)->text() == "first");
    enabled->setCurrentRow(0);
    REQUIRE(remove->isEnabled());
    remove->click();
    CHECK(mdl::enabledMods(map) == std::vector<std::string>{"second"});
    map.undoCommand();
    CHECK(enabled->count() == 2);
    auto* buttons = dialog.findChild<QDialogButtonBox*>();
    REQUIRE(buttons);
    buttons->button(QDialogButtonBox::Close)->click();
    CHECK_FALSE(dialog.isVisible());
    CHECK(mdl::enabledMods(map) == std::vector<std::string>{"first", "second"});
  }
}
} // namespace tb::ui
