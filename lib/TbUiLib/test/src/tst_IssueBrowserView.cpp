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

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#include <QTreeView>

#include "mdl/BrushBuilder.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/EmptyPropertyValueValidator.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/Issue.h"
#include "mdl/Map.h"
#include "mdl/Map_Entities.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/WorldNode.h"
#include "ui/CatchConfig.h"
#include "ui/IssueBrowserView.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"

#include "kd/result.h"

#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{
QModelIndex findGroup(QTreeView& tree, const std::string& category)
{
  const auto title = issueTypeLabel(category);
  for (int row = 0; row < tree.model()->rowCount(); ++row)
  {
    const auto index = tree.model()->index(row, 0);
    if (index.data().toString().startsWith(title + " ("))
      return index;
  }
  return {};
}
void selectRow(QTreeView& tree, const QModelIndex& index)
{
  tree.selectionModel()->select(
    index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}
} // namespace

TEST_CASE("IssueBrowserView")
{
  auto fixture = MapDocumentFixture{};
  auto& document = fixture.create();
  auto& map = document.map();
  auto* entity = new mdl::EntityNode{mdl::Entity{{
    {"classname", "some_class"},
    {"first_empty", ""},
    {"second_empty", ""},
  }}};
  mdl::addNodes(map, {{&mdl::parentForNodes(map), {entity}}});
  auto view = IssueBrowserView{document};
  view.validate();
  auto* tree = view.findChild<QTreeView*>("IssueBrowser_Tree");
  auto* search = view.findChild<QLineEdit*>("IssueBrowser_Search");
  auto* locate = view.findChild<QPushButton*>("IssueBrowser_Locate");
  REQUIRE(tree);
  REQUIRE(search);
  REQUIRE(locate);
  auto* model = tree->model();
  const auto group = findGroup(*tree, "Empty property value");
  REQUIRE(group.isValid());
  REQUIRE(model->rowCount(group) == 2);

  SECTION("reading does not change map selection; explicit location does")
  {
    mdl::deselectAll(map);
    auto spy = QSignalSpy{&view, &IssueBrowserView::focusObjectsRequested};
    selectRow(*tree, model->index(0, 0, group));
    CHECK(map.selection().nodes.empty());
    CHECK(
      view.findChild<QLabel*>("IssueBrowser_Details")->text().contains("first_empty"));
    locate->click();
    CHECK(map.selection().nodes == std::vector<mdl::Node*>{entity});
    CHECK(spy.count() == 1);
  }
  SECTION("face diagnostics locate the face and request the face inspector")
  {
    auto* brush = new mdl::BrushNode{
      mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()}.createCube(
        64.0, mdl::BrushFace::NoMaterialName)
      | kdl::value()};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {brush}}});
    view.reload();
    view.validate();
    const auto faces = findGroup(*tree, "Placeholder texture");
    REQUIRE(faces.isValid());
    selectRow(*tree, model->index(0, 0, faces));
    auto spy = QSignalSpy{&view, &IssueBrowserView::focusObjectsRequested};
    view.locateSelection();
    CHECK(map.selection().nodes.empty());
    CHECK(map.selection().brushFaces.size() == 1);
    REQUIRE(spy.count() == 1);
    CHECK(spy.front().front().toBool());
  }
  SECTION("property navigation emits the exact key and selects its owner")
  {
    auto spy = QSignalSpy{&view, &IssueBrowserView::editPropertyRequested};
    selectRow(*tree, model->index(0, 0, group));
    view.findChild<QPushButton*>("IssueBrowser_EditProperty")->click();
    REQUIRE(spy.count() == 1);
    CHECK(spy.front().front().toString() == "first_empty");
    CHECK(map.selection().nodes == std::vector<mdl::Node*>{entity});
  }
  SECTION("Chinese presentation translates group titles and actions")
  {
    auto translator = QTranslator{};
    REQUIRE(translator.load(QDir{QFileInfo{__FILE__}.absolutePath()}.absoluteFilePath(
      "../../../../app/TrenchBroom/resources/translations/trenchbroom_zh_CN.qm")));
    QApplication::installTranslator(&translator);
    const auto restore =
      qScopeGuard([&] { QApplication::removeTranslator(&translator); });
    CHECK(issueTypeLabel("Non-integer vertices") == QString::fromUtf8("非整数顶点"));
    CHECK(IssueBrowserView::tr("Locate objects") == QString::fromUtf8("定位对象"));
  }
  SECTION("unselectable targets leave the existing map selection intact")
  {
    auto* other = new mdl::EntityNode{mdl::Entity{{{"classname", "other_class"}}}};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {other}}});
    mdl::selectNodes(map, {other});
    entity->setLockState(mdl::LockState::Locked);
    selectRow(*tree, model->index(0, 0, group));
    locate->click();
    CHECK(map.selection().nodes == std::vector<mdl::Node*>{other});
    CHECK_FALSE(view.findChild<QLabel*>("IssueBrowser_Result")->text().isEmpty());
  }
  SECTION("empty property keys remain navigable")
  {
    mdl::selectNodes(map, {entity});
    REQUIRE(mdl::setEntityProperty(map, "", "value"));
    view.reload();
    view.validate();
    const auto emptyKey = findGroup(*tree, "Empty property name");
    REQUIRE(emptyKey.isValid());
    selectRow(*tree, model->index(0, 0, emptyKey));
    auto spy = QSignalSpy{&view, &IssueBrowserView::editPropertyRequested};
    auto* edit = view.findChild<QPushButton*>("IssueBrowser_EditProperty");
    REQUIRE(edit->isEnabled());
    edit->click();
    REQUIRE(spy.count() == 1);
    CHECK(spy.front().front().toString().isEmpty());
  }
  SECTION("search filters children and does not rerun validators")
  {
    const auto issues = entity->issues(map.worldNode().registeredValidators());
    search->setText("FIRST_EMPTY");
    const auto filtered = findGroup(*tree, "Empty property value");
    REQUIRE(filtered.isValid());
    CHECK(model->rowCount(filtered) == 1);
    CHECK(tree->isExpanded(filtered));
    CHECK(entity->issues(map.worldNode().registeredValidators()) == issues);
    search->setText("no_such_property");
    CHECK(model->rowCount() == 0);
    CHECK_FALSE(locate->isEnabled());
    search->clear();
    CHECK(model->rowCount(findGroup(*tree, "Empty property value")) == 2);
  }
  SECTION("show ignored intersects type filtering")
  {
    const auto type = mdl::EmptyPropertyValueValidator{}.type();
    for (const auto* issue : entity->issues(map.worldNode().registeredValidators()))
      if (issue->type() == type)
        map.setIssueHidden(*issue, true);
    view.reload();
    view.validate();
    CHECK_FALSE(findGroup(*tree, "Empty property value").isValid());
    view.setShowHiddenIssues(true);
    CHECK(findGroup(*tree, "Empty property value").isValid());
    view.setHiddenIssueTypes(type);
    CHECK_FALSE(findGroup(*tree, "Empty property value").isValid());
    CHECK(model->rowCount() > 0);
  }
  SECTION("ignore and restore act on object issue types without selecting objects")
  {
    selectRow(*tree, group);
    view.findChild<QPushButton*>("IssueBrowser_Ignore")->click();
    CHECK_FALSE(findGroup(*tree, "Empty property value").isValid());
    CHECK(map.selection().nodes.empty());
    view.setShowHiddenIssues(true);
    const auto ignoredGroup = findGroup(*tree, "Empty property value");
    REQUIRE(ignoredGroup.isValid());
    selectRow(*tree, ignoredGroup);
    view.findChild<QPushButton*>("IssueBrowser_Restore")->click();
    view.setShowHiddenIssues(false);
    CHECK(findGroup(*tree, "Empty property value").isValid());
  }
  SECTION("reload keeps readable values but disables stale actions after deletion")
  {
    selectRow(*tree, model->index(0, 0, group));
    mdl::selectNodes(map, {entity});
    mdl::removeSelectedNodes(map);
    view.reload();
    CHECK(model->rowCount() > 0);
    CHECK(model->index(0, 0, group).data().isValid());
    CHECK_FALSE(locate->isEnabled());
    view.locateSelection();
    CHECK(map.selection().nodes.empty());
    search->setText("first_empty");
    CHECK(model->rowCount() > 0);
    view.validate();
    CHECK(model->rowCount() == 0);
  }
  SECTION("reload preserves expansion, scroll and unchanged selection")
  {
    auto nodes = std::vector<mdl::Node*>{};
    for (size_t i = 0; i < 100; ++i)
      nodes.push_back(new mdl::EntityNode{
        mdl::Entity{{{"classname", "some_class"}, {"some_key", ""}}}});
    mdl::addNodes(map, {{&mdl::parentForNodes(map), nodes}});
    view.reload();
    view.validate();
    tree->expandAll();
    view.resize(900, 280);
    view.show();
    QApplication::processEvents();
    const auto currentGroup = findGroup(*tree, "Empty property value");
    selectRow(*tree, model->index(0, 0, currentGroup));
    auto* scroll = tree->verticalScrollBar();
    REQUIRE(scroll->maximum() > 0);
    scroll->setValue(scroll->maximum() / 2);
    const auto position = scroll->value();
    view.reload();
    QApplication::processEvents();
    view.validate();
    CHECK(tree->isExpanded(findGroup(*tree, "Empty property value")));
    CHECK(scroll->value() == position);
    CHECK(tree->selectionModel()->selectedRows().size() == 1);
    CHECK(map.selection().nodes.empty());
  }
  SECTION("search temporarily expands groups and restores their disclosure state")
  {
    tree->collapse(group);
    search->setText("first_empty");
    CHECK(tree->isExpanded(findGroup(*tree, "Empty property value")));
    search->clear();
    CHECK_FALSE(tree->isExpanded(findGroup(*tree, "Empty property value")));
  }
  SECTION("batch key repair keeps property values across cache invalidation")
  {
    mdl::selectNodes(map, {entity});
    REQUIRE(mdl::setEntityProperty(map, "bad\"one", "value one"));
    REQUIRE(mdl::setEntityProperty(map, "bad\"two", "value two"));
    view.reload();
    view.validate();
    const auto badKeys = findGroup(*tree, "Invalid entity property keys");
    REQUIRE(badKeys.isValid());
    selectRow(*tree, badKeys);
    auto* menu = view.findChild<QToolButton*>("IssueBrowser_Fix")->menu();
    Q_EMIT menu->aboutToShow();
    REQUIRE(menu->actions().size() == 2);
    QTimer::singleShot(0, &view, [] {
      for (auto* widget : QApplication::topLevelWidgets())
        if (auto* question = qobject_cast<QMessageBox*>(widget))
          question->button(QMessageBox::Yes)->click();
    });
    menu->actions().back()->trigger();
    REQUIRE(entity->entity().property("bad'one"));
    REQUIRE(entity->entity().property("bad'two"));
    CHECK(*entity->entity().property("bad'one") == "value one");
    CHECK(*entity->entity().property("bad'two") == "value two");
    map.undoCommand();
    CHECK(entity->entity().hasProperty("bad\"one"));
    CHECK(entity->entity().hasProperty("bad\"two"));
  }
  SECTION("group and child selection deduplicate fixes and preserve native undo")
  {
    search->setText("empty");
    const auto filtered = findGroup(*tree, "Empty property value");
    selectRow(*tree, filtered);
    tree->selectionModel()->select(
      model->index(0, 0, filtered),
      QItemSelectionModel::Select | QItemSelectionModel::Rows);
    auto* fix = view.findChild<QToolButton*>("IssueBrowser_Fix");
    REQUIRE(fix->isEnabled());
    auto* menu = fix->menu();
    Q_EMIT menu->aboutToShow();
    REQUIRE(menu->actions().size() == 1);
    QTimer::singleShot(0, &view, [] {
      for (auto* widget : QApplication::topLevelWidgets())
        if (auto* question = qobject_cast<QMessageBox*>(widget))
          question->button(QMessageBox::Yes)->click();
    });
    menu->actions().front()->trigger();
    INFO(view.findChild<QLabel*>("IssueBrowser_Result")->text().toStdString());
    CHECK_FALSE(entity->entity().hasProperty("first_empty"));
    CHECK_FALSE(entity->entity().hasProperty("second_empty"));
    CHECK(view.findChild<QLabel*>("IssueBrowser_Result")->text().contains("2"));
    map.undoCommand();
    CHECK(entity->entity().hasProperty("first_empty"));
    CHECK(entity->entity().hasProperty("second_empty"));
  }
}
} // namespace tb::ui
