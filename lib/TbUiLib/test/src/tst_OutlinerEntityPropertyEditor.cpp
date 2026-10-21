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
#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QScrollArea>
#include <QToolButton>
#include <QtTest/QTest>

#include "mdl/BrushNode.h"
#include "mdl/Entity.h"
#include "mdl/EntityDefinition.h"
#include "mdl/EntityDefinitionManager.h"
#include "mdl/EntityNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Entities.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/Node.h"
#include "mdl/TestFactory.h"
#include "mdl/WorldNode.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"
#include "ui/SmartSkyboxEditor.h"
#include "ui/outliner/OutlinerEntityPropertyEditor.h"

#include "kd/flat_map.h"

#include <map>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{
void processOutlinerUpdates()
{
  QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QApplication::processEvents();
  QApplication::processEvents();
}

std::vector<QWidget*> propertyRows(OutlinerEntityPropertyEditor& editor)
{
  auto result = std::vector<QWidget*>{};
  for (auto* widget : editor.findChildren<QWidget*>("outlinerPropertyRow"))
  {
    if (widget->parentWidget() != nullptr)
    {
      result.push_back(widget);
    }
  }
  return result;
}

QWidget* propertyRow(OutlinerEntityPropertyEditor& editor, const QString& key)
{
  for (auto* row : propertyRows(editor))
  {
    if (row->property("propertyKey").toString() == key)
    {
      return row;
    }
  }
  return nullptr;
}

bool hasInfoLabel(OutlinerEntityPropertyEditor& editor, const QString& text)
{
  for (const auto* label : editor.findChildren<QLabel*>())
  {
    if (label->text() == text)
    {
      return true;
    }
  }
  return false;
}

QLineEdit* propertyValueEdit(OutlinerEntityPropertyEditor& editor, const QString& key)
{
  if (auto* row = propertyRow(editor, key))
  {
    return row->findChild<QLineEdit*>("outlinerPropertyValue");
  }
  return nullptr;
}

std::vector<QAbstractButton*> propertyRowButtons(
  OutlinerEntityPropertyEditor& editor, const QString& key)
{
  auto result = std::vector<QAbstractButton*>{};
  if (auto* row = propertyRow(editor, key))
  {
    for (auto* button : row->findChildren<QAbstractButton*>())
    {
      if (button->property("propertyKey").toString() == key)
      {
        result.push_back(button);
      }
    }
  }
  return result;
}

mdl::EntityNodeBase& selectedEntity(mdl::Map& map)
{
  const auto& entities = map.selection().allEntities();
  REQUIRE(entities.size() == 1);
  return *entities.front();
}
} // namespace

TEST_CASE("OutlinerEntityPropertyEditor")
{
  auto fixture = MapDocumentFixture{};
  auto& document = fixture.create();
  auto& map = document.map();

  auto editor = OutlinerEntityPropertyEditor{document};
  processOutlinerUpdates();

  SECTION("shows worldspawn properties without explicit entity selection")
  {
    CHECK(!hasInfoLabel(editor, "No entity selected"));

    const auto* summary = editor.findChild<QLabel*>("outlinerPropertySelectionSummary");
    REQUIRE(summary != nullptr);
    CHECK(summary->text() == "worldspawn");
    CHECK(summary->isHidden());

    auto* valueEdit = propertyValueEdit(editor, "classname");
    REQUIRE(valueEdit != nullptr);
    CHECK(valueEdit->text() == "worldspawn");
  }

  SECTION("summarizes same-type and mixed entity selections")
  {
    auto* firstLight = new mdl::EntityNode{mdl::Entity{{{"classname", "light"}}}};
    auto* secondLight = new mdl::EntityNode{mdl::Entity{{{"classname", "light"}}}};
    auto* playerStart =
      new mdl::EntityNode{mdl::Entity{{{"classname", "info_player_start"}}}};
    mdl::addNodes(
      map, {{&mdl::parentForNodes(map), {firstLight, secondLight, playerStart}}});

    mdl::selectNodes(map, {firstLight, secondLight});
    processOutlinerUpdates();

    auto* summary = editor.findChild<QLabel*>("outlinerPropertySelectionSummary");
    REQUIRE(summary != nullptr);
    CHECK(summary->text() == "light (2 entities)");
    CHECK(!summary->isHidden());

    mdl::deselectAll(map);
    mdl::selectNodes(map, {firstLight, playerStart});
    processOutlinerUpdates();

    summary = editor.findChild<QLabel*>("outlinerPropertySelectionSummary");
    REQUIRE(summary != nullptr);
    CHECK(summary->text() == "Mixed selection (2 entities)");
    CHECK(!summary->isHidden());
    CHECK(hasInfoLabel(editor, "Different entity types selected"));
  }

  SECTION("keeps add controls outside the scrolling property list")
  {
    const auto* scrollArea = editor.findChild<QScrollArea*>("outlinerPropertyScrollArea");
    const auto* addKey = editor.findChild<QLineEdit*>("outlinerPropertyAddKey");
    REQUIRE(scrollArea != nullptr);
    REQUIRE(addKey != nullptr);
    CHECK(!scrollArea->isAncestorOf(addKey));
  }

  SECTION("uses the disabled text palette for inactive properties")
  {
    map.entityDefinitionManager().setDefinitions({{
      "inactive_test",
      {},
      "",
      {
        {"optional", mdl::PropertyValueTypes::String{}, "", ""},
        {"optional_choice",
         mdl::PropertyValueTypes::Choice{{{"0", "Off"}, {"1", "On"}}},
         "",
         ""},
      },
      mdl::PointEntityDefinition{vm::bbox3d{16.0}, {}, {}},
    }});
    const auto* definition = map.entityDefinitionManager().definition("inactive_test");
    REQUIRE(definition != nullptr);

    auto* entityNode = mdl::createPointEntity(map, *definition, {0, 0, 0});
    REQUIRE(entityNode != nullptr);
    mdl::selectNodes(map, {entityNode});
    processOutlinerUpdates();

    auto* row = propertyRow(editor, "optional");
    REQUIRE(row != nullptr);

    auto* keyLabel = row->findChild<QWidget*>("outlinerPropertyKey");
    auto* valueEdit = row->findChild<QLineEdit*>("outlinerPropertyValue");
    REQUIRE(keyLabel != nullptr);
    REQUIRE(valueEdit != nullptr);
    CHECK(keyLabel->isEnabled());
    CHECK(!valueEdit->isEnabled());
    CHECK(valueEdit->placeholderText() == "<inactive>");
    CHECK(
      valueEdit->palette().color(QPalette::Disabled, QPalette::PlaceholderText)
      == valueEdit->palette().color(QPalette::Disabled, QPalette::Text));

    auto* choiceRow = propertyRow(editor, "optional_choice");
    REQUIRE(choiceRow != nullptr);
    auto* valueCombo = choiceRow->findChild<QComboBox*>("outlinerPropertyValue");
    REQUIRE(valueCombo != nullptr);
    REQUIRE(valueCombo->lineEdit() != nullptr);
    CHECK(!valueCombo->isEnabled());
    CHECK(valueCombo->currentIndex() == -1);
    CHECK(valueCombo->currentText().isEmpty());
    CHECK(valueCombo->lineEdit()->placeholderText() == "<inactive>");

    mdl::setEntityProperty(map, "active", "changed", false);
    const auto changedNodes = std::vector<mdl::Node*>{entityNode};
    map.notifyNodesDidChange(changedNodes);
    processOutlinerUpdates();

    CHECK(valueEdit->placeholderText() == "<inactive>");
    CHECK(valueCombo->currentIndex() == -1);
    CHECK(valueCombo->currentText().isEmpty());
    CHECK(valueCombo->lineEdit()->placeholderText() == "<inactive>");
  }

  SECTION("allows property keys to be selected for copying")
  {
    editor.show();
    editor.resize(800, 600);
    processOutlinerUpdates();

    auto* row = propertyRow(editor, "classname");
    REQUIRE(row != nullptr);
    auto* keyWidget = row->findChild<QWidget*>("outlinerPropertyKey");
    REQUIRE(keyWidget != nullptr);
    auto* keyLabel = keyWidget->findChild<QLabel*>();
    REQUIRE(keyLabel != nullptr);
    CHECK(keyLabel->textInteractionFlags().testFlag(Qt::TextSelectableByMouse));

    QTest::mouseDClick(keyLabel, Qt::LeftButton);
    CHECK(keyLabel->selectedText() == "classname");
  }

  SECTION("adapts property rows to narrow and wide panels")
  {
    editor.show();
    editor.resize(320, 600);
    processOutlinerUpdates();

    auto* row = propertyRow(editor, "classname");
    REQUIRE(row != nullptr);
    CHECK(row->property("compact").toBool());

    editor.resize(800, 600);
    processOutlinerUpdates();
    CHECK(!row->property("compact").toBool());
  }

  SECTION("shows skybox editor control for worldspawn skyname")
  {
    mdl::selectNodes(map, std::vector<mdl::Node*>{&map.worldNode()});
    mdl::setEntityProperty(map, "skyname", "2namek", false);
    processOutlinerUpdates();

    auto* skynameEdit = propertyValueEdit(editor, "skyname");
    REQUIRE(skynameEdit != nullptr);

    auto* skyboxButton = static_cast<QAbstractButton*>(nullptr);
    for (auto* button : propertyRowButtons(editor, "skyname"))
    {
      if (button->toolTip() == "Show skybox editor")
      {
        skyboxButton = button;
        break;
      }
    }

    REQUIRE(skyboxButton != nullptr);
    QTest::mouseClick(skyboxButton, Qt::LeftButton);
    processOutlinerUpdates();

    CHECK(editor.findChild<QWidget*>("outlinerEmbeddedSkyboxEditor") != nullptr);
    auto* skyboxEditor = editor.findChild<SmartSkyboxEditor*>();
    REQUIRE(skyboxEditor != nullptr);

    mdl::setEntityProperty(map, "skyname", "morning", false);
    const auto changedNodes = std::vector<mdl::Node*>{&map.worldNode()};
    map.notifyNodesDidChange(changedNodes);
    processOutlinerUpdates();

    skynameEdit = propertyValueEdit(editor, "skyname");
    REQUIRE(skynameEdit != nullptr);
    CHECK(skynameEdit->text() == "morning");
  }

  SECTION("aligns value editors across rows with different actions")
  {
    mdl::selectNodes(map, std::vector<mdl::Node*>{&map.worldNode()});
    mdl::setEntityProperty(map, "skyname", "2namek", false);
    mdl::setEntityProperty(map, "_color", "1 0 0", false);
    editor.show();
    editor.resize(800, 600);
    processOutlinerUpdates();
    QTRY_VERIFY_WITH_TIMEOUT(propertyRow(editor, "skyname") != nullptr, 1000);

    auto* classnameRow = propertyRow(editor, "classname");
    auto* skynameRow = propertyRow(editor, "skyname");
    auto* colorRow = propertyRow(editor, "_color");
    auto* classnameEdit = propertyValueEdit(editor, "classname");
    auto* skynameEdit = propertyValueEdit(editor, "skyname");
    auto* colorEdit = propertyValueEdit(editor, "_color");
    REQUIRE(classnameRow != nullptr);
    REQUIRE(skynameRow != nullptr);
    REQUIRE(colorRow != nullptr);
    REQUIRE(classnameEdit != nullptr);
    REQUIRE(skynameEdit != nullptr);
    REQUIRE(colorEdit != nullptr);

    const auto* classnameActions =
      classnameRow->findChild<QWidget*>("outlinerPropertyActions");
    const auto* skynameActions =
      skynameRow->findChild<QWidget*>("outlinerPropertyActions");
    const auto* colorActions = colorRow->findChild<QWidget*>("outlinerPropertyActions");
    REQUIRE(classnameActions != nullptr);
    REQUIRE(skynameActions != nullptr);
    REQUIRE(colorActions != nullptr);
    CHECK(classnameActions->width() == skynameActions->width());
    CHECK(classnameActions->width() == colorActions->width());
    CHECK(colorActions->width() == 50);
    CHECK(classnameEdit->geometry().right() == skynameEdit->geometry().right());
    CHECK(classnameEdit->geometry().right() == colorEdit->geometry().right());

    const auto* colorButton =
      colorRow->findChild<QToolButton*>("outlinerPropertyColorButton");
    REQUIRE(colorButton != nullptr);
    CHECK(colorButton->size() == QSize{24, 24});
  }

  SECTION("refreshes skyname when the selected worldspawn property changes externally")
  {
    mdl::selectNodes(map, std::vector<mdl::Node*>{&map.worldNode()});
    mdl::setEntityProperty(map, "skyname", "2namek", false);
    processOutlinerUpdates();

    auto* skynameEdit = propertyValueEdit(editor, "skyname");
    REQUIRE(skynameEdit != nullptr);
    CHECK(skynameEdit->text() == "2namek");

    mdl::setEntityProperty(map, "skyname", "morning", false);
    const auto changedNodes = std::vector<mdl::Node*>{&map.worldNode()};
    map.notifyNodesDidChange(changedNodes);
    processOutlinerUpdates();

    skynameEdit = propertyValueEdit(editor, "skyname");
    REQUIRE(skynameEdit != nullptr);
    CHECK(skynameEdit->text() == "morning");
  }

  SECTION(
    "keeps expanded skybox editor available when selection still resolves to worldspawn")
  {
    mdl::selectNodes(map, std::vector<mdl::Node*>{&map.worldNode()});
    mdl::setEntityProperty(map, "skyname", "2namek", false);
    auto* brushNode = mdl::createBrushNode(map);
    mdl::addNodes(
      map,
      kdl::flat_map<mdl::Node*, std::vector<mdl::Node*>>{
        {&mdl::parentForNodes(map), {static_cast<mdl::Node*>(brushNode)}}});
    processOutlinerUpdates();

    auto* skyboxButton = static_cast<QAbstractButton*>(nullptr);
    for (auto* button : propertyRowButtons(editor, "skyname"))
    {
      if (button->toolTip() == "Show skybox editor")
      {
        skyboxButton = button;
        break;
      }
    }
    REQUIRE(skyboxButton != nullptr);
    QTest::mouseClick(skyboxButton, Qt::LeftButton);
    processOutlinerUpdates();

    auto* skyboxEditor = editor.findChild<SmartSkyboxEditor*>();
    REQUIRE(skyboxEditor != nullptr);

    mdl::selectNodes(map, std::vector<mdl::Node*>{static_cast<mdl::Node*>(brushNode)});
    processOutlinerUpdates();
    auto* selectedBrushSkyboxEditor = editor.findChild<SmartSkyboxEditor*>();
    REQUIRE(selectedBrushSkyboxEditor != nullptr);

    mdl::deselectAll(map);
    processOutlinerUpdates();
    auto* deselectedSkyboxEditor = editor.findChild<SmartSkyboxEditor*>();
    REQUIRE(deselectedSkyboxEditor != nullptr);
  }

  SECTION("updates rows from MapDocument selection notifications")
  {
    auto* entityNode = new mdl::EntityNode{mdl::Entity{{
      {"classname", "light"},
      {"targetname", "light_1"},
    }}};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {entityNode}}});
    mdl::selectNodes(map, {entityNode});
    processOutlinerUpdates();

    auto* valueEdit = propertyValueEdit(editor, "targetname");
    REQUIRE(valueEdit != nullptr);
    CHECK(valueEdit->text() == "light_1");

    valueEdit->setText("renamed_light");
    emit valueEdit->editingFinished();
    processOutlinerUpdates();

    auto& entity = selectedEntity(map).entity();
    REQUIRE(entity.property("targetname") != nullptr);
    CHECK(*entity.property("targetname") == "renamed_light");
  }

  SECTION("stores choice values instead of display labels")
  {
    map.entityDefinitionManager().setDefinitions({{
      "choice_test",
      {},
      "",
      {{"mode",
        mdl::PropertyValueTypes::Choice{{{"0", "Invisible"}, {"1", "Textured"}}},
        "",
        ""}},
      mdl::PointEntityDefinition{vm::bbox3d{16.0}, {}, {}},
    }});
    const auto* definition = map.entityDefinitionManager().definition("choice_test");
    REQUIRE(definition != nullptr);

    auto* entityNode = mdl::createPointEntity(map, *definition, {0, 0, 0});
    REQUIRE(entityNode != nullptr);
    mdl::setEntityProperty(map, "mode", "1", false);
    processOutlinerUpdates();

    auto* row = propertyRow(editor, "mode");
    REQUIRE(row != nullptr);
    auto* valueCombo = row->findChild<QComboBox*>("outlinerPropertyValue");
    REQUIRE(valueCombo != nullptr);
    REQUIRE(valueCombo->lineEdit() != nullptr);

    valueCombo->lineEdit()->setText("0 : Invisible");
    emit valueCombo->lineEdit()->editingFinished();
    processOutlinerUpdates();

    const auto& selectedAfterChoice = selectedEntity(map).entity();
    REQUIRE(selectedAfterChoice.property("mode") != nullptr);
    CHECK(*selectedAfterChoice.property("mode") == "0");

    row = propertyRow(editor, "mode");
    REQUIRE(row != nullptr);
    valueCombo = row->findChild<QComboBox*>("outlinerPropertyValue");
    REQUIRE(valueCombo != nullptr);
    REQUIRE(valueCombo->lineEdit() != nullptr);
    valueCombo->lineEdit()->setText("custom");
    emit valueCombo->lineEdit()->editingFinished();
    processOutlinerUpdates();

    const auto& selectedAfterCustomValue = selectedEntity(map).entity();
    REQUIRE(selectedAfterCustomValue.property("mode") != nullptr);
    CHECK(*selectedAfterCustomValue.property("mode") == "custom");
  }

  SECTION("adds and removes selected entity properties")
  {
    auto* entityNode = new mdl::EntityNode{mdl::Entity{{
      {"classname", "light"},
      {"targetname", "light_1"},
    }}};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {entityNode}}});
    mdl::selectNodes(map, {entityNode});
    processOutlinerUpdates();

    auto* addKey = editor.findChild<QLineEdit*>("outlinerPropertyAddKey");
    auto* addValue = editor.findChild<QLineEdit*>("outlinerPropertyAddValue");
    REQUIRE(addKey != nullptr);
    REQUIRE(addValue != nullptr);

    addKey->setText("message");
    addValue->setText("hello");

    auto* addButton = static_cast<QAbstractButton*>(nullptr);
    for (auto* button : editor.findChildren<QAbstractButton*>())
    {
      if (button->toolTip() == "Add property")
      {
        addButton = button;
        break;
      }
    }
    REQUIRE(addButton != nullptr);

    QTest::mouseClick(addButton, Qt::LeftButton);
    processOutlinerUpdates();

    auto& addedEntity = selectedEntity(map).entity();
    REQUIRE(addedEntity.property("message") != nullptr);
    CHECK(*addedEntity.property("message") == "hello");

    addKey = editor.findChild<QLineEdit*>("outlinerPropertyAddKey");
    addValue = editor.findChild<QLineEdit*>("outlinerPropertyAddValue");
    REQUIRE(addKey != nullptr);
    REQUIRE(addValue != nullptr);
    CHECK(addKey->text().isEmpty());
    CHECK(addValue->text().isEmpty());

    auto* removeButton = static_cast<QAbstractButton*>(nullptr);
    for (auto* button : propertyRowButtons(editor, "message"))
    {
      if (!button->isHidden())
      {
        removeButton = button;
      }
    }
    REQUIRE(removeButton != nullptr);
    QTest::mouseClick(removeButton, Qt::LeftButton);
    processOutlinerUpdates();

    CHECK(selectedEntity(map).entity().property("message") == nullptr);
    CHECK(propertyRow(editor, "message") == nullptr);
  }
}

} // namespace tb::ui
