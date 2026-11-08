/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSpinBox>
#include <QToolButton>
#include <QTreeWidget>

#include "gl/Material.h"
#include "gl/MaterialManager.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/EntityDefinitionManager.h"
#include "mdl/EntityNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Entities.h"
#include "mdl/Map_Selection.h"
#include "mdl/Map_World.h"
#include "mdl/WorldNode.h"
#include "ui/AppControllerFixture.h"
#include "ui/CollapsibleTitledPanel.h"
#include "ui/FaceAttribsEditor.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"
#include "ui/MaterialBrowser.h"
#include "ui/MaterialPreview.h"
#include "ui/ModelBrowser.h"
#include "ui/SmartWadEditor.h"
#include "ui/SpinControl.h"
#include "ui/UvEditor.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
TEST_CASE("MapMaterialWorkflow")
{
  auto app = AppControllerFixture{};
  auto fixture = MapDocumentFixture{};
  auto& document = fixture.load("test/mapMaterialsQ2.map", mdl::Quake2FixtureConfig);
  auto& map = document.map();
  auto content = ModelBrowser{app.appController(), document};
  content.resize(1000, 440);
  content.showMapMaterials();
  auto* browser = content.findChild<MaterialBrowser*>();
  REQUIRE(browser);
  const auto* material = map.materialManager().material("e1m1/b_pv_v1a2");
  REQUIRE(material);

  SECTION("no selection changes current material without editing map geometry")
  {
    mdl::deselectAll(map);
    const auto modified = map.modified();
    auto editor = FaceAttribsEditor{app.appController(), document};
    emit browser->materialSelected(material);
    CHECK(map.currentMaterialName() == material->name());
    QApplication::processEvents();
    CHECK(
      editor.findChild<QLabel*>("FaceAttribsEditor_MaterialName")->text()
      == QString::fromStdString(material->name()));
    CHECK(map.modified() == modified);
    CHECK_FALSE(map.selection().hasAnyBrushFaces());
    emit browser->materialSelected(material);
    CHECK(map.currentMaterialName() == mdl::BrushFace::NoMaterialName);
  }

  SECTION("applying and clearing materials preserves faces and supports undo")
  {
    mdl::selectAllNodes(map);
    REQUIRE_FALSE(map.selection().brushes.empty());
    const auto face = mdl::BrushFaceHandle{*map.selection().brushes.front(), 0};
    mdl::deselectAll(map);
    mdl::selectBrushFaces(map, {face});
    const auto original = face.face().materialName();
    REQUIRE(original != material->name());
    auto editor = FaceAttribsEditor{app.appController(), document};
    emit browser->materialSelected(material);
    CHECK(face.face().materialName() == material->name());
    CHECK(map.selection().brushFaces == std::vector{face});
    CHECK(editor.findChild<MaterialPreview*>()->material() == material);
    auto* offset = editor.findChild<SpinControl*>("FaceAttribsEditor_OffsetX");
    REQUIRE(offset);
    QApplication::processEvents();
    const auto originalOffset = face.face().uvAttributes().offset.x();
    offset->setValue(double(originalOffset) + 16.0);
    CHECK(face.face().uvAttributes().offset.x() == originalOffset + 16.0f);
    map.undoCommand();
    CHECK(face.face().uvAttributes().offset.x() == originalOffset);
    emit browser->materialSelected(material);
    CHECK(face.face().materialName() == mdl::BrushFace::NoMaterialName);
    map.undoCommand();
    CHECK(face.face().materialName() == material->name());
    map.undoCommand();
    CHECK(face.face().materialName() == original);
  }

  SECTION("materials entities and files retain independent searches")
  {
    auto* search = content.findChild<QLineEdit*>("ModelBrowser_Search");
    REQUIRE(search);
    search->setText("b_pv");
    content.showEntityBrowser();
    CHECK(search->text().isEmpty());
    search->setText("light");
    auto* folders = content.findChild<QTreeWidget*>("ModelBrowser_FolderTree");
    REQUIRE(folders);
    folders->setCurrentItem(folders->topLevelItem(0));
    CHECK(content.isShowingAssets());
    search->setText("model");
    content.showMapMaterials();
    CHECK(search->text() == "b_pv");
    auto* used = content.findChild<QCheckBox*>("ModelBrowser_MaterialUsed");
    REQUIRE(used);
    used->setChecked(true);
    content.revealMaterial(material);
    CHECK(search->text().isEmpty());
    CHECK_FALSE(used->isChecked());
    CHECK(browser->selectedMaterial() == material);
    content.showEntityBrowser();
    CHECK(search->text() == "light");
    folders->setCurrentItem(folders->topLevelItem(0));
    CHECK(search->text() == "model");
  }

  SECTION("UV controls fit narrow inspectors and collapsed preview releases its height")
  {
    auto editor = FaceAttribsEditor{app.appController(), document};
    editor.setFixedWidth(300);
    editor.resize(300, 640);
    editor.show();
    QApplication::processEvents();
    auto* uv = editor.findChild<UvEditor*>();
    auto* grid = editor.findChild<QWidget*>("UvEditor_Grid");
    auto* actions = editor.findChild<QWidget*>("UvEditor_Actions");
    REQUIRE(uv);
    REQUIRE(grid);
    REQUIRE(actions);
    for (const auto* spin : grid->findChildren<QSpinBox*>())
    {
      const auto position = spin->mapTo(uv, QPoint{});
      CHECK(position.x() >= 0);
      CHECK(position.x() + spin->width() <= uv->width());
    }
    CHECK(grid->geometry().top() >= actions->geometry().bottom());
    auto* preview =
      editor.findChild<CollapsibleTitledPanel*>("FaceAttribsEditor_UvSection");
    REQUIRE(preview);
    CHECK(preview->height() > 100);
    preview->collapse();
    QApplication::processEvents();
    CHECK(preview->height() < 50);
    preview->expand();
    QApplication::processEvents();
    CHECK(preview->height() > 100);
  }
}

TEST_CASE("SmartWadEditor targets world settings while an entity is selected")
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
  editor.moveSelectedWadsUp();
  CHECK(*map.worldNode().entity().property("wad") == "b.wad;a.wad;c.wad");
  CHECK_FALSE(entity->entity().property("wad"));
  CHECK(map.selection().nodes == selection);
  map.undoCommand();
  editor.update({&map.worldNode()});
  list->setCurrentRow(1);
  editor.removeSelectedWads();
  CHECK(*map.worldNode().entity().property("wad") == "a.wad;c.wad");
  CHECK_FALSE(entity->entity().property("wad"));
  CHECK(map.selection().nodes == selection);
  map.undoCommand();
  CHECK(*map.worldNode().entity().property("wad") == "a.wad;b.wad;c.wad");
}
} // namespace tb::ui
