/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QAbstractButton>
#include <QApplication>
#include <QListWidget>
#include <QScrollBar>

#include "mdl/EntityDefinitionManager.h"
#include "mdl/EntityNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Entities.h"
#include "mdl/Map_Selection.h"
#include "mdl/Map_World.h"
#include "mdl/WorldNode.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"
#include "ui/SmartWadEditor.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
TEST_CASE("SmartWadEditor")
{
  auto fixture = MapDocumentFixture{};
  auto& document = fixture.create();
  auto& map = document.map();
  map.entityDefinitionManager().setDefinitions(
    {{"light", {}, "", {}, mdl::PointEntityDefinition{vm::bbox3d{8.0}, {}, {}}}});
  const auto* definition = map.entityDefinitionManager().definition("light");
  REQUIRE(definition);
  REQUIRE(mdl::createPointEntity(map, *definition, {}));
  const auto selection = map.selection().nodes;
  REQUIRE(selection.size() == 1);
  auto* entity = dynamic_cast<mdl::EntityNode*>(selection.front());
  REQUIRE(entity);
  mdl::setWorldProperty(map, "wad", "a.wad;b.wad;c.wad");
  auto editor = SmartWadEditor{document};
  editor.activate("wad");
  editor.update({&map.worldNode()});
  auto* list = editor.findChild<QListWidget*>();
  REQUIRE(list);
  list->setCurrentRow(1);

  SECTION("shows filenames while retaining distinct full paths and update state")
  {
    const auto paths = std::string{
      "D:/CS/Mapper/Shared_Assets/Wad_Common/halflife.wad;"
      "D:\\CS\\Mapper\\Map_Assets\\halflife.wad;"
      "D:/CS/Mapper/Shared_Assets/Wad_Common/sdhlt.wad"};
    mdl::setWorldProperty(map, "wad", paths);
    editor.update({&map.worldNode()});
    CHECK(list->item(0)->text() == "halflife.wad");
    CHECK(list->item(1)->text() == "halflife.wad");
    CHECK(list->item(2)->text() == "sdhlt.wad");
    CHECK(
      list->item(0)->toolTip() == "D:/CS/Mapper/Shared_Assets/Wad_Common/halflife.wad");
    CHECK(list->item(1)->toolTip() == "D:\\CS\\Mapper\\Map_Assets\\halflife.wad");
    CHECK(list->item(1)->data(Qt::UserRole).toString() == list->item(1)->toolTip());
    editor.resize(320, 180);
    editor.show();
    QApplication::processEvents();
    CHECK(list->viewport()->height() >= 4 * list->sizeHintForRow(0));
    CHECK(list->horizontalScrollBar()->maximum() == 0);
    list->setCurrentRow(1);
    auto* retained = list->item(1);
    editor.update({&map.worldNode()});
    CHECK(list->item(1) == retained);
    CHECK(list->currentRow() == 1);
    editor.removeSelectedWads();
    CHECK(
      *map.worldNode().entity().property("wad")
      == "D:/CS/Mapper/Shared_Assets/Wad_Common/halflife.wad;"
         "D:/CS/Mapper/Shared_Assets/Wad_Common/sdhlt.wad");
    CHECK_FALSE(entity->entity().property("wad"));
    CHECK(map.selection().nodes == selection);
    map.undoCommand();
    CHECK(*map.worldNode().entity().property("wad") == paths);
    return;
  }

  SECTION("refreshes command states after populating or clearing the list")
  {
    auto* reload = static_cast<QAbstractButton*>(nullptr);
    for (auto* button : editor.findChildren<QAbstractButton*>())
      if (button->toolTip() == "Reload WAD files")
        reload = button;
    REQUIRE(reload != nullptr);
    CHECK(reload->isEnabled());
    mdl::setWorldProperty(map, "wad", "");
    editor.update({&map.worldNode()});
    CHECK(list->count() == 0);
    CHECK_FALSE(reload->isEnabled());
    CHECK_FALSE(editor.canMoveWadsDown());
    CHECK_FALSE(editor.canRemoveWads());
    return;
  }

  SECTION("moving WADs up changes world settings while an entity is selected")
  {
    editor.moveSelectedWadsUp();
    CHECK(*map.worldNode().entity().property("wad") == "b.wad;a.wad;c.wad");
  }
  SECTION("moving WADs down changes world settings while an entity is selected")
  {
    editor.moveSelectedWadsDown();
    CHECK(*map.worldNode().entity().property("wad") == "a.wad;c.wad;b.wad");
  }
  SECTION("removing WADs changes world settings while an entity is selected")
  {
    editor.removeSelectedWads();
    CHECK(*map.worldNode().entity().property("wad") == "a.wad;c.wad");
  }
  CHECK_FALSE(entity->entity().property("wad"));
  CHECK(map.selection().nodes == selection);
  map.undoCommand();
  CHECK(*map.worldNode().entity().property("wad") == "a.wad;b.wad;c.wad");
}
} // namespace tb::ui
