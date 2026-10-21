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
#include <QHeaderView>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTreeWidgetItem>
#include <QtTest/QTest>

#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/WorldNode.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"
#include "ui/outliner/OutlinerTreeWidget.h"

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{
void processOutlinerTreeUpdates()
{
  QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QApplication::processEvents();
  QApplication::processEvents();
}

std::vector<QTreeWidgetItem*> allItems(QTreeWidget& tree)
{
  auto result = std::vector<QTreeWidgetItem*>{};
  auto stack = std::vector<QTreeWidgetItem*>{};

  for (int i = 0; i < tree.topLevelItemCount(); ++i)
  {
    if (auto* item = tree.topLevelItem(i))
    {
      stack.push_back(item);
    }
  }

  while (!stack.empty())
  {
    auto* item = stack.back();
    stack.pop_back();
    result.push_back(item);

    for (int i = 0; i < item->childCount(); ++i)
    {
      if (auto* child = item->child(i))
      {
        stack.push_back(child);
      }
    }
  }

  return result;
}

QTreeWidgetItem* itemForNode(QTreeWidget& tree, const mdl::Node* node)
{
  for (auto* item : allItems(tree))
  {
    if (item->data(0, Qt::UserRole).value<mdl::Node*>() == node)
    {
      return item;
    }
  }
  return nullptr;
}

QTreeWidgetItem* itemWithText(QTreeWidget& tree, const QString& text)
{
  for (auto* item : allItems(tree))
  {
    if (item->text(0) == text)
    {
      return item;
    }
  }
  return nullptr;
}

std::vector<QString> childNames(const QTreeWidgetItem& item)
{
  auto result = std::vector<QString>{};
  for (int i = 0; i < item.childCount(); ++i)
  {
    result.push_back(item.child(i)->text(0));
  }
  return result;
}

struct OutlinerTreeFixture
{
  MapDocumentFixture fixture;
  MapDocument& document;
  mdl::Map& map;
  mdl::LayerNode* defaultLayer;
  mdl::EntityNode* lightEntity;
  mdl::EntityNode* infoEntity;
  mdl::GroupNode* group;
  mdl::EntityNode* groupedEntity;

  OutlinerTreeFixture()
    : document{fixture.create()}
    , map{document.map()}
    , defaultLayer{map.worldNode().defaultLayer()}
    , lightEntity{new mdl::EntityNode{mdl::Entity{{{"classname", "z_light"}}}}}
    , infoEntity{new mdl::EntityNode{mdl::Entity{{{"classname", "a_info_player_start"}}}}}
    , group{new mdl::GroupNode{mdl::Group{"Detail group"}}}
    , groupedEntity{new mdl::EntityNode{mdl::Entity{{{"classname", "path_corner"}}}}}
  {
    group->addChild(groupedEntity);
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {lightEntity, infoEntity, group}}});
  }
};

class TestOutlinerTreeWidget : public OutlinerTreeWidget
{
public:
  using OutlinerTreeWidget::dropSelectedItemsOnItem;
  using OutlinerTreeWidget::OutlinerTreeWidget;
};
} // namespace

TEST_CASE("OutlinerTreeWidget")
{
  auto fixture = OutlinerTreeFixture{};
  auto& document = fixture.document;
  auto& map = fixture.map;

  auto tree = TestOutlinerTreeWidget{document};
  processOutlinerTreeUpdates();

  SECTION("uses compact workbench metrics")
  {
    CHECK(tree.objectName() == "OutlinerTreeWidget");
    CHECK(tree.indentation() == 14);
    CHECK(tree.iconSize() == QSize{16, 16});
    CHECK(tree.header()->minimumSectionSize() == 28);
    CHECK(tree.header()->sectionSize(2) == 28);
    CHECK(tree.header()->sectionSize(3) == 28);
    CHECK(tree.headerItem()->text(2).isEmpty());
    CHECK(tree.headerItem()->text(3).isEmpty());
    CHECK(tree.headerItem()->toolTip(2) == "Lock state");
    CHECK(tree.headerItem()->toolTip(3) == "Visibility state");
    CHECK(!tree.headerItem()->icon(2).isNull());
    CHECK(!tree.headerItem()->icon(3).isNull());
  }

  SECTION("builds layer tree with sorted node contents")
  {
    REQUIRE(tree.topLevelItemCount() == 1);

    auto* layerItem = itemForNode(tree, fixture.defaultLayer);
    REQUIRE(layerItem != nullptr);
    CHECK(layerItem->text(0) == "Default Layer");

    const auto names = childNames(*layerItem);
    REQUIRE(names.size() == 3);
    CHECK(names[0] == "Detail group");
    CHECK(names[1] == "a_info_player_start");
    CHECK(names[2] == "z_light");

    auto* groupedItem = itemForNode(tree, fixture.groupedEntity);
    REQUIRE(groupedItem != nullptr);
    CHECK(groupedItem->parent() == itemForNode(tree, fixture.group));
  }

  SECTION("filters by text and type")
  {
    tree.setFilterText("a_info");
    processOutlinerTreeUpdates();

    REQUIRE(itemForNode(tree, fixture.infoEntity) != nullptr);
    REQUIRE(itemForNode(tree, fixture.lightEntity) != nullptr);
    CHECK(!itemForNode(tree, fixture.infoEntity)->isHidden());
    CHECK(itemForNode(tree, fixture.lightEntity)->isHidden());

    tree.setFilterText("type:group");
    processOutlinerTreeUpdates();

    CHECK(!itemForNode(tree, fixture.group)->isHidden());
    CHECK(itemForNode(tree, fixture.infoEntity)->isHidden());

    tree.setFilterText("");
    processOutlinerTreeUpdates();

    CHECK(!itemForNode(tree, fixture.infoEntity)->isHidden());
    CHECK(!itemForNode(tree, fixture.lightEntity)->isHidden());
  }

  SECTION("updates the selected filter when document selection changes")
  {
    tree.setFilterText("selected");
    processOutlinerTreeUpdates();

    CHECK(itemForNode(tree, fixture.infoEntity)->isHidden());
    CHECK(itemForNode(tree, fixture.lightEntity)->isHidden());

    mdl::selectNodes(map, {fixture.infoEntity});
    processOutlinerTreeUpdates();

    CHECK(!itemForNode(tree, fixture.infoEntity)->isHidden());
    CHECK(itemForNode(tree, fixture.lightEntity)->isHidden());
  }

  SECTION("syncs document selection into the tree")
  {
    mdl::selectNodes(map, {fixture.infoEntity});
    processOutlinerTreeUpdates();

    auto* infoItem = itemForNode(tree, fixture.infoEntity);
    REQUIRE(infoItem != nullptr);
    CHECK(infoItem->isSelected());
  }

  SECTION("syncs tree selection into the document")
  {
    auto* lightItem = itemForNode(tree, fixture.lightEntity);
    REQUIRE(lightItem != nullptr);

    tree.setCurrentItem(lightItem);
    lightItem->setSelected(true);
    processOutlinerTreeUpdates();

    CHECK(map.selection().hasOnlyEntities());
    REQUIRE(map.selection().entities.size() == 1);
    CHECK(map.selection().entities.front() == fixture.lightEntity);
  }

  SECTION("batches document selection across disjoint rows and parents")
  {
    auto* secondGroupedEntity =
      new mdl::EntityNode{mdl::Entity{{{"classname", "path_track"}}}};
    mdl::addNodes(map, {{fixture.group, {secondGroupedEntity}}});
    processOutlinerTreeUpdates();
    auto changes =
      QSignalSpy{tree.selectionModel(), &QItemSelectionModel::selectionChanged};
    mdl::selectNodes(map, {fixture.infoEntity, fixture.groupedEntity});
    processOutlinerTreeUpdates();

    REQUIRE(tree.selectedItems().size() == 2);
    CHECK(itemForNode(tree, fixture.infoEntity)->isSelected());
    CHECK(itemForNode(tree, fixture.groupedEntity)->isSelected());
    CHECK_FALSE(itemForNode(tree, secondGroupedEntity)->isSelected());
    CHECK_FALSE(itemForNode(tree, fixture.lightEntity)->isSelected());
    CHECK(changes.count() == 1);

    mdl::selectAllNodes(map);
    processOutlinerTreeUpdates();
    CHECK(itemForNode(tree, fixture.infoEntity)->isSelected());
    CHECK(itemForNode(tree, fixture.lightEntity)->isSelected());
    CHECK(itemForNode(tree, fixture.group)->isSelected());
    CHECK_FALSE(itemForNode(tree, fixture.groupedEntity)->isSelected());
    CHECK_FALSE(itemForNode(tree, secondGroupedEntity)->isSelected());

    mdl::deselectAll(map);
    processOutlinerTreeUpdates();
    CHECK(tree.selectedItems().empty());
  }

  SECTION("keeps lock and visibility columns interactive")
  {
    tree.resize(600, 400);
    tree.show();
    processOutlinerTreeUpdates();

    auto* layerItem = itemForNode(tree, fixture.defaultLayer);
    REQUIRE(layerItem != nullptr);
    const auto itemRect = tree.visualItemRect(layerItem);
    REQUIRE(!itemRect.isEmpty());

    const auto clickColumn = [&](const int column) {
      const auto x = tree.header()->sectionViewportPosition(column)
                     + tree.header()->sectionSize(column) / 2;
      QTest::mouseClick(
        tree.viewport(),
        Qt::LeftButton,
        Qt::NoModifier,
        QPoint{x, itemRect.center().y()});
      processOutlinerTreeUpdates();
    };

    CHECK_FALSE(fixture.defaultLayer->locked());
    clickColumn(2);
    CHECK(fixture.defaultLayer->locked());

    CHECK(fixture.defaultLayer->visible());
    clickColumn(3);
    CHECK_FALSE(fixture.defaultLayer->visible());
  }

  SECTION("escape clears tree and document selection")
  {
    mdl::selectNodes(map, {fixture.infoEntity});
    processOutlinerTreeUpdates();
    REQUIRE(map.selection().hasAny());

    tree.setFocus();
    QTest::keyClick(&tree, Qt::Key_Escape);
    processOutlinerTreeUpdates();

    CHECK(!map.selection().hasAny());
    CHECK(tree.selectedItems().empty());
  }

  SECTION("supports keyboard row navigation")
  {
    tree.resize(600, 400);
    tree.show();
    processOutlinerTreeUpdates();

    auto* infoItem = itemForNode(tree, fixture.infoEntity);
    auto* lightItem = itemForNode(tree, fixture.lightEntity);
    REQUIRE(infoItem != nullptr);
    REQUIRE(lightItem != nullptr);

    tree.setCurrentItem(infoItem);
    tree.setFocus();
    QTest::keyClick(&tree, Qt::Key_Down);
    CHECK(tree.currentItem() == lightItem);

    QTest::keyClick(&tree, Qt::Key_Up);
    CHECK(tree.currentItem() == infoItem);
  }

  SECTION("keeps rename actions outside inline tree editing")
  {
    for (auto* item : allItems(tree))
    {
      CHECK_FALSE(item->flags().testFlag(Qt::ItemIsEditable));
    }
  }

  SECTION("moves selected objects to another layer through the drop path")
  {
    auto* targetLayer = new mdl::LayerNode{mdl::Layer{"Target"}};
    mdl::addNodes(map, {{&map.worldNode(), {targetLayer}}});
    processOutlinerTreeUpdates();

    mdl::selectNodes(map, {fixture.infoEntity});
    processOutlinerTreeUpdates();

    auto* targetItem = itemForNode(tree, targetLayer);
    REQUIRE(targetItem != nullptr);
    REQUIRE(tree.dropSelectedItemsOnItem(targetItem));
    processOutlinerTreeUpdates();

    CHECK(fixture.infoEntity->parent() == targetLayer);
    REQUIRE(map.selection().entities.size() == 1);
    CHECK(map.selection().entities.front() == fixture.infoEntity);

    auto* movedItem = itemForNode(tree, fixture.infoEntity);
    REQUIRE(movedItem != nullptr);
    CHECK(movedItem->parent() == itemForNode(tree, targetLayer));
  }

  SECTION("locally refreshes a layer when map nodes are added")
  {
    auto* layerItemBefore = itemForNode(tree, fixture.defaultLayer);
    REQUIRE(layerItemBefore != nullptr);

    auto* addedEntity = new mdl::EntityNode{mdl::Entity{{{"classname", "trigger_once"}}}};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {addedEntity}}});
    processOutlinerTreeUpdates();

    auto* addedItem = itemWithText(tree, "trigger_once");
    REQUIRE(addedItem != nullptr);
    CHECK(addedItem->data(0, Qt::UserRole).value<mdl::Node*>() == addedEntity);
    CHECK(itemForNode(tree, fixture.defaultLayer) == layerItemBefore);
  }

  SECTION("reveals a newly added layer")
  {
    tree.resize(600, 100);
    tree.show();

    auto layers = std::vector<mdl::Node*>{};
    for (auto i = 0; i < 12; ++i)
    {
      layers.push_back(new mdl::LayerNode{mdl::Layer{"Layer " + std::to_string(i)}});
    }
    auto* targetLayer = new mdl::LayerNode{mdl::Layer{"zz Target"}};
    layers.push_back(targetLayer);

    mdl::addNodes(map, {{&map.worldNode(), layers}});
    processOutlinerTreeUpdates();

    auto* targetItem = itemForNode(tree, targetLayer);
    REQUIRE(targetItem != nullptr);
    CHECK_FALSE(tree.viewport()->rect().intersects(tree.visualItemRect(targetItem)));

    tree.revealNode(targetLayer);
    processOutlinerTreeUpdates();

    targetItem = itemForNode(tree, targetLayer);
    REQUIRE(targetItem != nullptr);
    CHECK(tree.viewport()->rect().intersects(tree.visualItemRect(targetItem)));
    CHECK(tree.verticalScrollBar()->value() > 0);
  }

  SECTION("locally refreshes a group when nested nodes change")
  {
    auto* layerItemBefore = itemForNode(tree, fixture.defaultLayer);
    auto* groupItemBefore = itemForNode(tree, fixture.group);
    REQUIRE(layerItemBefore != nullptr);
    REQUIRE(groupItemBefore != nullptr);

    auto* addedEntity =
      new mdl::EntityNode{mdl::Entity{{{"classname", "trigger_multiple"}}}};
    mdl::addNodes(map, {{fixture.group, {addedEntity}}});
    processOutlinerTreeUpdates();

    CHECK(itemForNode(tree, fixture.defaultLayer) == layerItemBefore);
    CHECK(itemForNode(tree, fixture.group) == groupItemBefore);
    CHECK(itemForNode(tree, fixture.groupedEntity) != nullptr);
    CHECK(itemForNode(tree, addedEntity)->parent() == groupItemBefore);
  }

  SECTION("locally refreshes a group when nested nodes are removed")
  {
    auto* retainedEntity = new mdl::EntityNode{mdl::Entity{{{"classname", "info_null"}}}};
    mdl::addNodes(map, {{fixture.group, {retainedEntity}}});
    processOutlinerTreeUpdates();

    auto* layerItemBefore = itemForNode(tree, fixture.defaultLayer);
    auto* groupItemBefore = itemForNode(tree, fixture.group);
    REQUIRE(layerItemBefore != nullptr);
    REQUIRE(groupItemBefore != nullptr);

    mdl::removeNodes(map, {fixture.groupedEntity});
    processOutlinerTreeUpdates();

    CHECK(itemForNode(tree, fixture.defaultLayer) == layerItemBefore);
    CHECK(itemForNode(tree, fixture.group) == groupItemBefore);
    CHECK(itemForNode(tree, fixture.groupedEntity) == nullptr);
    CHECK(itemForNode(tree, retainedEntity)->parent() == groupItemBefore);
    CHECK(groupItemBefore->childCount() == 1);
  }

  SECTION("removes an empty group after its last child is removed")
  {
    mdl::removeNodes(map, {fixture.groupedEntity});
    processOutlinerTreeUpdates();

    CHECK(itemForNode(tree, fixture.groupedEntity) == nullptr);
    CHECK(itemForNode(tree, fixture.group) == nullptr);
    CHECK(itemForNode(tree, fixture.defaultLayer) != nullptr);
  }

  SECTION("applies sorting to local structural updates")
  {
    tree.setSortMode(OutlinerTreeWidget::SortMode::FileOrder);
    processOutlinerTreeUpdates();

    auto* addedEntity =
      new mdl::EntityNode{mdl::Entity{{{"classname", "0_first_by_name"}}}};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {addedEntity}}});
    processOutlinerTreeUpdates();

    auto* layerItem = itemForNode(tree, fixture.defaultLayer);
    REQUIRE(layerItem != nullptr);
    const auto names = childNames(*layerItem);
    REQUIRE(names.size() == 4);
    CHECK(names.back() == "0_first_by_name");
  }
}

} // namespace tb::ui
