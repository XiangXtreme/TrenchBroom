/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QScopeGuard>
#include <QSettings>
#include <QSignalSpy>
#include <QSplitter>
#include <QToolButton>

#include "base/PreferenceManager.h"
#include "mdl/EntityDefinition.h"
#include "mdl/EntityDefinitionManager.h"
#include "mdl/EntityNode.h"
#include "mdl/GroupNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Entities.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/WorldNode.h"
#include "prefs/Preferences.h"
#include "ui/AppControllerFixture.h"
#include "ui/EntityPropertyModel.h"
#include "ui/EntityPropertyTable.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"
#include "ui/SelectionPropertyEditor.h"
#include "ui/SmartColorEditor.h"
#include "ui/SmartModelEditor.h"
#include "ui/TransformCard.h"
#include "ui/outliner/OutlinerEntityPropertyEditor.h"
#include "ui/outliner/OutlinerInspector.h"
#include "ui/outliner/OutlinerTreeWidget.h"

#include <array>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{
void refreshSelectionProperties()
{
  QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QApplication::processEvents();
  QApplication::processEvents();
}

QModelIndex propertyIndex(
  EntityPropertyTable& table, const QString& key, const int column)
{
  for (auto row = 0; row < table.model()->rowCount(); ++row)
    if (
      table.model()->index(row, EntityPropertyModel::ColumnKey).data().toString() == key)
      return table.model()->index(row, column);
  return {};
}
} // namespace

TEST_CASE("SelectionPropertyEditor")
{
  auto app = AppControllerFixture{};
  auto fixture = MapDocumentFixture{};
  auto& document = fixture.create();
  auto& map = document.map();
  const auto wasTable = pref(Preferences::EntityPropertiesTable);
  const auto restore =
    qScopeGuard([&] { setPref(Preferences::EntityPropertiesTable, wasTable); });
  setPref(Preferences::EntityPropertiesTable, false);
  auto editor = SelectionPropertyEditor{document};
  auto* mode = editor.findChild<QComboBox*>("SelectionPropertyEditor_Presentation");
  REQUIRE(mode);
  auto* entity = new mdl::EntityNode{mdl::Entity{
    {{"classname", "light"}, {"message", "original"}, {"model", "models/test.mdl"}}}};
  mdl::addNodes(map, {{&mdl::parentForNodes(map), {entity}}});
  mdl::selectNodes(map, {entity});
  refreshSelectionProperties();

  SECTION("diagnostic property navigation uses the active presentation")
  {
    editor.focusProperty("message");
    refreshSelectionProperties();
    mode->setCurrentIndex(1);
    refreshSelectionProperties();
    editor.focusProperty("model");
    refreshSelectionProperties();
    auto* table = editor.findChild<EntityPropertyTable*>();
    REQUIRE(table);
    CHECK(
      table->currentIndex()
        .siblingAtColumn(EntityPropertyModel::ColumnKey)
        .data()
        .toString()
      == "model");
    mode->setCurrentIndex(0);
    refreshSelectionProperties();
    editor.focusProperty("message");
    refreshSelectionProperties();
    CHECK(*entity->entity().property("message") == "original");
  }

  SECTION("form and table edit the same properties with native undo")
  {
    auto* form = editor.findChild<OutlinerEntityPropertyEditor*>();
    REQUIRE(form);
    auto* transform = form->findChild<TransformCard*>();
    REQUIRE(transform);
    CHECK_FALSE(transform->expanded());
    auto* value = static_cast<QLineEdit*>(nullptr);
    for (auto* edit : form->findChildren<QLineEdit*>("outlinerPropertyValue"))
      if (edit->property("propertyKey").toString() == "message")
        value = edit;
    REQUIRE(value);
    value->setText("form edit");
    Q_EMIT value->editingFinished();
    refreshSelectionProperties();
    CHECK(*entity->entity().property("message") == "form edit");
    mode->setCurrentIndex(1);
    refreshSelectionProperties();
    auto* table = editor.findChild<EntityPropertyTable*>();
    REQUIRE(table);
    const auto index = propertyIndex(*table, "message", EntityPropertyModel::ColumnValue);
    REQUIRE(index.isValid());
    CHECK(index.data().toString() == "form edit");
    REQUIRE(table->model()->setData(index, "table edit"));
    CHECK(*entity->entity().property("message") == "table edit");
    map.undoCommand();
    refreshSelectionProperties();
    CHECK(*entity->entity().property("message") == "form edit");
    mode->setCurrentIndex(0);
    refreshSelectionProperties();
    CHECK(editor.findChild<OutlinerEntityPropertyEditor*>() == form);
  }

  SECTION("table exposes key renaming protection defaults and smart editors")
  {
    auto* group = new mdl::GroupNode{mdl::Group{"group"}};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {group}}});
    REQUIRE(mdl::reparentNodes(map, {{group, {entity}}}));
    mode->setCurrentIndex(1);
    refreshSelectionProperties();
    auto* table = editor.findChild<EntityPropertyTable*>();
    REQUIRE(table);
    CHECK_FALSE(table->isColumnHidden(EntityPropertyModel::ColumnProtected));
    auto key = propertyIndex(*table, "message", EntityPropertyModel::ColumnKey);
    REQUIRE(table->model()->setData(key, "targetname"));
    refreshSelectionProperties();
    CHECK(entity->entity().property("message") == nullptr);
    REQUIRE(entity->entity().property("targetname"));
    auto protection =
      propertyIndex(*table, "targetname", EntityPropertyModel::ColumnProtected);
    REQUIRE(protection.isValid());
    REQUIRE(table->model()->flags(protection).testFlag(Qt::ItemIsUserCheckable));
    table->model()->setData(protection, Qt::Checked, Qt::CheckStateRole);
    CHECK_FALSE(entity->entity().protectedProperties().empty());
    REQUIRE(
      editor.findChild<QToolButton*>("EntityPropertyGrid_SetDefaultPropertiesButton"));
    auto model = propertyIndex(*table, "model", EntityPropertyModel::ColumnValue);
    table->setCurrentIndex(model);
    refreshSelectionProperties();
    auto* modelEditor = editor.findChild<SmartModelEditor*>();
    REQUIRE(modelEditor);
    CHECK_FALSE(modelEditor->isHidden());
  }

  SECTION("scene and standalone surfaces share presentation and property capabilities")
  {
    auto scene = OutlinerInspector{document};
    auto entities = SelectionPropertyEditor{document};
    mode->setCurrentIndex(1);
    refreshSelectionProperties();
    for (auto* host : std::array<QWidget*, 2>{&scene, &entities})
    {
      auto* surface = qobject_cast<SelectionPropertyEditor*>(host);
      if (!surface)
        surface = host->findChild<SelectionPropertyEditor*>();
      REQUIRE(surface);
      REQUIRE(surface->findChild<EntityPropertyTable*>());
      CHECK(
        surface->findChild<QComboBox*>("SelectionPropertyEditor_Presentation")
          ->currentIndex()
        == 1);
    }
    mode->setCurrentIndex(0);
    refreshSelectionProperties();
    auto* shared = scene.findChild<SelectionPropertyEditor*>();
    REQUIRE(shared);
    auto* form = shared->findChild<OutlinerEntityPropertyEditor*>();
    REQUIRE(form);
    auto forwarded = QSignalSpy{shared, &SelectionPropertyEditor::focusMapViewRequested};
    Q_EMIT form->focusMapViewRequested();
    CHECK(forwarded.count() == 1);
  }

  SECTION("mixed selection respects read-only fields in both presentations")
  {
    map.entityDefinitionManager().setDefinitions({
      {"fixed",
       {},
       "",
       {{"color", mdl::PropertyValueTypes::String{}, "", "", true}},
       mdl::PointEntityDefinition{vm::bbox3d{16.0}, {}, {}}},
      {"editable",
       {},
       "",
       {{"color", mdl::PropertyValueTypes::String{}, "", "", false}},
       mdl::PointEntityDefinition{vm::bbox3d{16.0}, {}, {}}},
    });
    mdl::deselectAll(map);
    auto* fixed =
      mdl::createPointEntity(map, *map.entityDefinitionManager().definition("fixed"), {});
    auto* editable = mdl::createPointEntity(
      map, *map.entityDefinitionManager().definition("editable"), {});
    REQUIRE(fixed);
    REQUIRE(editable);
    mdl::deselectAll(map);
    mdl::selectNodes(map, {fixed, editable});
    mdl::setEntityProperty(map, "color", "255 128 0", false);
    refreshSelectionProperties();
    auto* form = editor.findChild<OutlinerEntityPropertyEditor*>();
    REQUIRE(form);
    auto* color = form->findChild<QToolButton*>("outlinerPropertyColorButton");
    REQUIRE(color);
    CHECK_FALSE(color->isEnabled());
    auto* addKey = form->findChild<QLineEdit*>("outlinerPropertyAddKey");
    REQUIRE(addKey);
    addKey->setText("color");
    for (auto* button : form->findChildren<QToolButton*>())
      if (button->toolTip() == "Add property")
        CHECK_FALSE(button->isEnabled());
    mode->setCurrentIndex(1);
    refreshSelectionProperties();
    auto* table = editor.findChild<EntityPropertyTable*>();
    REQUIRE(table);
    auto index = propertyIndex(*table, "color", EntityPropertyModel::ColumnValue);
    REQUIRE(index.isValid());
    CHECK_FALSE(table->model()->flags(index).testFlag(Qt::ItemIsEditable));
    table->setCurrentIndex(index);
    refreshSelectionProperties();
    auto* smart = editor.findChild<SmartColorEditor*>();
    REQUIRE(smart);
    CHECK_FALSE(smart->isEnabled());
    mdl::deselectAll(map);
    mdl::selectNodes(map, {editable});
    refreshSelectionProperties();
    CHECK(smart->isEnabled());
  }

  SECTION("changing presentation keeps documents isolated")
  {
    auto otherFixture = MapDocumentFixture{};
    auto& other = otherFixture.create();
    auto otherEditor = SelectionPropertyEditor{other};
    mode->setCurrentIndex(1);
    refreshSelectionProperties();
    auto* table = editor.findChild<EntityPropertyTable*>();
    REQUIRE(table);
    REQUIRE(table->model()->setData(
      propertyIndex(*table, "message", EntityPropertyModel::ColumnValue), "only here"));
    CHECK(*entity->entity().property("message") == "only here");
    CHECK_FALSE(other.map().worldNode().entity().property("message"));
  }

  SECTION("scene panes can be hidden independently and restore their split")
  {
    auto settings = QSettings{};
    const auto paths = QStringList{
      "Windows/OutlinerInspector/TreeVisible",
      "Windows/OutlinerInspector/PropertiesVisible",
      "Windows/OutlinerInspector_Splitter/State"};
    auto saved = QList<QVariant>{};
    for (const auto& path : paths)
    {
      saved.push_back(settings.value(path));
      settings.remove(path);
    }
    const auto restoreSettings = qScopeGuard([&] {
      for (auto i = 0; i < paths.size(); ++i)
        if (saved[i].isValid())
          settings.setValue(paths[i], saved[i]);
        else
          settings.remove(paths[i]);
    });
    auto scene = OutlinerInspector{document};
    scene.resize(340, 800);
    scene.show();
    refreshSelectionProperties();
    auto* tree = scene.findChild<OutlinerTreeWidget*>();
    auto* properties = scene.findChild<SelectionPropertyEditor*>();
    auto* split = scene.findChild<QSplitter*>("OutlinerInspector_Splitter");
    auto* toggle = scene.findChild<QToolButton*>("OutlinerInspector_PropertiesToggle");
    auto* treeVisible = scene.findChild<QAction*>("OutlinerInspector_TreeVisible");
    REQUIRE(tree);
    REQUIRE(properties);
    REQUIRE(split);
    REQUIRE(toggle);
    REQUIRE(treeVisible);
    CHECK_FALSE(tree->isHidden());
    CHECK_FALSE(properties->isHidden());
    split->setSizes({320, 440});
    const auto initialSizes = split->sizes();
    toggle->click();
    CHECK(properties->isHidden());
    treeVisible->setChecked(false);
    CHECK(tree->isHidden());
    CHECK_FALSE(properties->isHidden());
    toggle->click();
    CHECK_FALSE(tree->isHidden());
    CHECK(properties->isHidden());
    toggle->click();
    refreshSelectionProperties();
    CHECK_FALSE(tree->isHidden());
    CHECK_FALSE(properties->isHidden());
    REQUIRE(split->sizes().size() == 2);
    CHECK(split->sizes()[0] > 0);
    CHECK(split->sizes()[1] > 0);
    CHECK(std::abs(split->sizes()[0] - initialSizes[0]) <= 2);
  }
}
} // namespace tb::ui
