/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QMimeData>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTextBrowser>
#include <QToolButton>
#include <QTreeWidget>

#include "mdl/EntityDefinition.h"
#include "mdl/EntityDefinitionManager.h"
#include "mdl/Map.h"
#include "mdl/Map_Entities.h"
#include "mdl/Map_Selection.h"
#include "ui/AppControllerFixture.h"
#include "ui/EntityBrowser.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"

#include <memory>

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
  auto* tree = browser.findChild<QTreeWidget*>("EntityBrowser_Definitions");
  auto* search = browser.findChild<QLineEdit*>("EntityBrowser_Search");
  auto* type = browser.findChild<QComboBox*>("EntityBrowser_Type");
  auto* create = browser.findChild<QPushButton*>("EntityBrowser_Create");
  REQUIRE(tree);
  REQUIRE(search);
  REQUIRE(type);
  REQUIRE(create);
  REQUIRE(tree->topLevelItemCount() == 2);
  const auto find = [&](const QString& classname) {
    const auto items = tree->findItems(classname, Qt::MatchExactly | Qt::MatchRecursive);
    return items.isEmpty() ? nullptr : items.front();
  };

  SECTION("browsing definitions never edits the map or its selection")
  {
    const auto selection = map.selection().nodes;
    const auto modified = map.modified();
    const auto undo = map.canUndoCommand();
    auto* light = find("light");
    REQUIRE(light);
    tree->setCurrentItem(light);
    CHECK(create->isEnabled());
    CHECK(browser.findChild<QTextBrowser*>()->toPlainText() == "Illuminates the room");
    CHECK(map.selection().nodes == selection);
    CHECK(map.modified() == modified);
    CHECK(map.canUndoCommand() == undo);
    auto requested = QSignalSpy{&browser, &EntityBrowser::createEntityRequested};
    create->click();
    REQUIRE(requested.count() == 1);
    CHECK(requested.front().front().toString() == "light");
    CHECK(map.selection().nodes == selection);
  }

  SECTION("filters search both classnames and descriptions and include brush types")
  {
    search->setText("moving");
    REQUIRE(tree->topLevelItemCount() == 1);
    CHECK(tree->topLevelItem(0)->text(0) == "func_door");
    search->clear();
    type->setCurrentIndex(1);
    REQUIRE(tree->topLevelItemCount() == 1);
    CHECK(tree->topLevelItem(0)->text(0) == "light");
    type->setCurrentIndex(2);
    REQUIRE(tree->topLevelItemCount() == 1);
    tree->setCurrentItem(tree->topLevelItem(0));
    CHECK(tree->topLevelItem(0)->text(0) == "func_door");
    CHECK_FALSE(create->isEnabled());
    CHECK(create->text() == "Turn selection into entity");
    CHECK(find("worldspawn") == nullptr);
  }

  SECTION("sorting and grouping preserve the chosen type")
  {
    auto* sort = browser.findChild<QComboBox*>("EntityBrowser_Sort");
    auto* group = browser.findChild<QToolButton*>("EntityBrowser_GroupToggle");
    REQUIRE(sort);
    REQUIRE(group);
    CHECK(tree->topLevelItem(0)->text(0) == "func_door");
    auto* light = find("light");
    REQUIRE(light);
    tree->setCurrentItem(light);
    map.entityDefinitionManager().definition("light")->incUsageCount();
    sort->setCurrentIndex(1);
    CHECK(tree->topLevelItem(0)->text(0) == "light");
    REQUIRE(tree->currentItem());
    CHECK(tree->currentItem()->text(0) == "light");
    group->setChecked(true);
    REQUIRE(tree->currentItem());
    CHECK(tree->currentItem()->text(0) == "light");
    group->setChecked(false);
    REQUIRE(tree->currentItem());
    CHECK(tree->currentItem()->text(0) == "light");
    map.entityDefinitionManager().definition("light")->decUsageCount();
  }

  SECTION("only point types expose the native drag payload")
  {
    auto* light = find("light");
    auto* door = find("func_door");
    REQUIRE(light);
    REQUIRE(door);
    CHECK(light->flags().testFlag(Qt::ItemIsDragEnabled));
    CHECK_FALSE(door->flags().testFlag(Qt::ItemIsDragEnabled));
    tree->setCurrentItem(light);
    const auto payload =
      std::unique_ptr<QMimeData>{tree->model()->mimeData({tree->currentIndex()})};
    REQUIRE(payload);
    CHECK(payload->text() == "entity:light");
    browser.setMapActionsEnabled(false);
    CHECK_FALSE(find("light")->flags().testFlag(Qt::ItemIsDragEnabled));
    CHECK_FALSE(create->isEnabled());
  }

  SECTION("used filter follows creation and undo")
  {
    auto* used = browser.findChild<QToolButton*>("EntityBrowser_UsedToggle");
    REQUIRE(used);
    used->setChecked(true);
    REQUIRE(tree->topLevelItemCount() == 0);
    const auto* definition = map.entityDefinitionManager().definition("light");
    REQUIRE(definition);
    REQUIRE(mdl::createPointEntity(map, *definition, {}));
    REQUIRE(tree->topLevelItemCount() == 1);
    CHECK(tree->topLevelItem(0)->text(0) == "light");
    map.undoCommand();
    CHECK(tree->topLevelItemCount() == 0);
  }

  SECTION("rebinding the same map preserves controls while real switches clear targets")
  {
    auto searchGuard = QPointer<QLineEdit>{search};
    browser.setDocument(document);
    REQUIRE(searchGuard);
    CHECK(browser.findChild<QLineEdit*>("EntityBrowser_Search") == search);
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
    CHECK(searchGuard.isNull());
    auto* rebound = browser.findChild<QTreeWidget*>("EntityBrowser_Definitions");
    REQUIRE(rebound);
    REQUIRE(rebound->topLevelItemCount() == 1);
    CHECK(rebound->topLevelItem(0)->text(0) == "other_point");
    CHECK_FALSE(browser.findChild<QPushButton*>("EntityBrowser_Create")->isEnabled());
    browser.setDocument(document);
  }

  SECTION("definition settings are owned by the bound map page")
  {
    auto* settings = browser.findChild<QToolButton*>("EntityBrowser_Settings");
    REQUIRE(settings);
    settings->click();
    auto dialog =
      QPointer<QDialog>{browser.findChild<QDialog*>("EntityBrowser_DefinitionSettings")};
    REQUIRE(dialog);
    settings->click();
    CHECK(browser.findChildren<QDialog*>("EntityBrowser_DefinitionSettings").size() == 1);
    auto secondFixture = MapDocumentFixture{};
    auto& second = secondFixture.create();
    browser.setDocument(second);
    CHECK(dialog.isNull());
    browser.setDocument(document);
  }
}
} // namespace tb::ui
