/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>
#include <QTreeWidget>

#include "mdl/EntityDefinition.h"
#include "mdl/EntityDefinitionManager.h"
#include "mdl/EntityDefinitionUtils.h"
#include "mdl/Map.h"
#include "mdl/Map_Entities.h"
#include "mdl/Map_Selection.h"
#include "ui/AppControllerFixture.h"
#include "ui/EntityBrowser.h"
#include "ui/EntityBrowserView.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"
#include "ui/ModelBrowser.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
TEST_CASE("EntityBrowser")
{
  auto app = AppControllerFixture{};
  auto fixture = MapDocumentFixture{};
  auto& document = fixture.create();
  auto& map = document.map();
  map.entityDefinitionManager().setDefinitions({
    {"light",
     {},
     "Illuminates the room",
     {},
     mdl::PointEntityDefinition{vm::bbox3d{16.0}, {}, {}}},
    {"func_door", {}, "A moving brush door", {}},
    {"worldspawn", {}, "World", {}},
  });
  auto browser = EntityBrowser{app.appController(), document};
  auto* create = browser.findChild<QPushButton*>("EntityBrowser_Create");
  REQUIRE(create);
  REQUIRE(browser.view().filteredDefinitions().size() == 2);

  SECTION("browsing and selecting grid cells never edits the map selection")
  {
    const auto selection = map.selection().nodes;
    const auto modified = map.modified();
    const auto undo = map.canUndoCommand();
    REQUIRE(browser.view().selectDefinition("light"));
    CHECK(create->isEnabled());
    CHECK(browser.findChild<QLabel*>()->text().contains("Illuminates the room"));
    CHECK(map.selection().nodes == selection);
    CHECK(map.modified() == modified);
    CHECK(map.canUndoCommand() == undo);
    auto requested = QSignalSpy{&browser, &EntityBrowser::createEntityRequested};
    create->click();
    REQUIRE(requested.count() == 1);
    CHECK(requested.front().front().toString() == "light");
    CHECK(map.selection().nodes == selection);
  }
  SECTION("shared search and folders filter the grid and clear hidden selections")
  {
    REQUIRE(browser.view().selectDefinition("light"));
    browser.view().setFilterText("moving");
    auto definitions = browser.view().filteredDefinitions();
    REQUIRE(definitions.size() == 1);
    CHECK(definitions.front()->name == "func_door");
    CHECK_FALSE(create->isEnabled());
    browser.view().setFilterText("");
    browser.view().setFolder(EntityBrowserFolder::Point);
    definitions = browser.view().filteredDefinitions();
    REQUIRE(definitions.size() == 1);
    CHECK(definitions.front()->name == "light");
    browser.view().setFolder(EntityBrowserFolder::Brush);
    definitions = browser.view().filteredDefinitions();
    REQUIRE(definitions.size() == 1);
    CHECK(definitions.front()->name == "func_door");
    REQUIRE(browser.view().selectDefinition("func_door"));
    CHECK_FALSE(create->isEnabled());
    CHECK(create->text() == "Turn selection into entity");
    CHECK_FALSE(browser.view().selectDefinition("worldspawn"));
  }
  SECTION("sorting and grouping preserve selection")
  {
    CHECK(browser.view().filteredDefinitions().front()->name == "func_door");
    REQUIRE(browser.view().selectDefinition("light"));
    map.entityDefinitionManager().definition("light")->incUsageCount();
    browser.view().setSortOrder(mdl::EntityDefinitionSortOrder::Usage);
    CHECK(browser.view().filteredDefinitions().front()->name == "light");
    browser.view().setGroup(true);
    CHECK(browser.view().selectedDefinitionName() == "light");
    browser.view().setGroup(false);
    CHECK(browser.view().selectedDefinitionName() == "light");
    map.entityDefinitionManager().definition("light")->decUsageCount();
  }
  SECTION(
    "only point types expose the native drag payload and no-map browsing stays enabled")
  {
    CHECK(browser.view().dragPayload("light") == "entity:light");
    CHECK(browser.view().dragPayload("func_door").isEmpty());
    CHECK(browser.view().dragPayload("missing").isEmpty());
    REQUIRE(browser.view().selectDefinition("light"));
    browser.setMapActionsEnabled(false);
    CHECK(browser.view().dragPayload("light").isEmpty());
    CHECK_FALSE(create->isEnabled());
    CHECK(browser.view().isEnabled());
    browser.showSettings();
    CHECK(browser.findChild<QDialog*>() == nullptr);
  }
  SECTION("used filter follows creation and undo")
  {
    browser.view().setHideUnused(true);
    REQUIRE(browser.view().filteredDefinitions().empty());
    const auto* definition = map.entityDefinitionManager().definition("light");
    REQUIRE(definition);
    REQUIRE(mdl::createPointEntity(map, *definition, {}));
    REQUIRE(browser.view().filteredDefinitions().size() == 1);
    CHECK(browser.view().filteredDefinitions().front()->name == "light");
    map.undoCommand();
    CHECK(browser.view().filteredDefinitions().empty());
  }
  SECTION("entity folders use shared controls and retain file navigation")
  {
    auto content = ModelBrowser{app.appController(), document};
    content.resize(1000, 440);
    content.show();
    auto* search = content.findChild<QLineEdit*>("ModelBrowser_Search");
    auto* folders = content.findChild<QTreeWidget*>("ModelBrowser_FolderTree");
    auto* back = content.findChild<QToolButton*>("ModelBrowser_Back");
    auto* forward = content.findChild<QToolButton*>("ModelBrowser_Forward");
    REQUIRE(search);
    REQUIRE(folders);
    REQUIRE(back);
    REQUIRE(forward);
    search->setText("file search");
    content.showEntityBrowser();
    QApplication::processEvents();
    REQUIRE(folders->topLevelItemCount() >= 2);
    CHECK(content.isShowingEntities());
    CHECK(search->text().isEmpty());
    CHECK(content.findChild<QWidget*>("ModelBrowser_Controls")->isVisible());
    CHECK(content.findChildren<QLineEdit*>("EntityBrowser_Search").empty());
    auto* grid = content.findChild<EntityBrowserView*>();
    REQUIRE(grid);
    CHECK_FALSE(grid->grabFramebuffer().isNull());
    // The first cell is a brush definition: rendering and clicking it uses the same grid.
    QTest::mouseClick(grid, Qt::LeftButton, Qt::NoModifier, QPoint{40, 40});
    CHECK(grid->selectedDefinitionName() == "func_door");
    search->setText("room");
    REQUIRE(grid->filteredDefinitions().size() == 1);
    CHECK(grid->filteredDefinitions().front()->name == "light");
    back->click();
    CHECK_FALSE(content.isShowingEntities());
    CHECK(search->text() == "file search");
    REQUIRE(forward->isEnabled());
    forward->click();
    CHECK(content.isShowingEntities());
    CHECK(search->text() == "room");
    // Virtual folder navigation participates in the same back/forward history.
    search->clear();
    auto* root = folders->currentItem();
    REQUIRE(root);
    REQUIRE(root->childCount() == 2);
    folders->setCurrentItem(root->child(1));
    CHECK(grid->folder() == EntityBrowserFolder::Brush);
    REQUIRE(grid->filteredDefinitions().size() == 1);
    CHECK(grid->filteredDefinitions().front()->name == "func_door");
    back->click();
    CHECK(grid->folder() == EntityBrowserFolder::All);
    REQUIRE(forward->isEnabled());
    forward->click();
    CHECK(grid->folder() == EntityBrowserFolder::Brush);
    // File-only filters do not leak into the entity listing.
    auto* used = content.findChild<QCheckBox*>("ModelBrowser_EntityUsed");
    REQUIRE(used);
    used->setChecked(true);
    CHECK(grid->filteredDefinitions().empty());
    content.setMapActionsEnabled(false);
    CHECK(grid->isEnabled());
    CHECK_FALSE(content.findChild<QToolButton*>("EntityBrowser_Settings")->isEnabled());
  }
  SECTION("same map preserves canvas and real switches release old targets")
  {
    auto canvas = QPointer<EntityBrowserView>{&browser.view()};
    browser.setDocument(document);
    REQUIRE(canvas);
    CHECK(&browser.view() == canvas);
    browser.showSettings();
    auto dialog =
      QPointer<QDialog>{browser.findChild<QDialog*>("EntityBrowser_DefinitionSettings")};
    REQUIRE(dialog);
    browser.showSettings();
    CHECK(browser.findChildren<QDialog*>("EntityBrowser_DefinitionSettings").size() == 1);
    auto secondFixture = MapDocumentFixture{};
    auto& second = secondFixture.create();
    second.map().entityDefinitionManager().setDefinitions({
      {"other_point",
       {},
       "Other map",
       {},
       mdl::PointEntityDefinition{vm::bbox3d{8.0}, {}, {}}},
    });
    browser.setDocument(second);
    CHECK(canvas.isNull());
    CHECK(dialog.isNull());
    const auto definitions = browser.view().filteredDefinitions();
    REQUIRE(definitions.size() == 1);
    CHECK(definitions.front()->name == "other_point");
    CHECK_FALSE(browser.findChild<QPushButton*>("EntityBrowser_Create")->isEnabled());
    browser.setDocument(document);
  }
}
} // namespace tb::ui
