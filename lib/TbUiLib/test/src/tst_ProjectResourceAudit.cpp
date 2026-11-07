/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QCheckBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTreeWidget>

#include "img/GoldSrcTexture.h"
#include "mdl/Map.h"
#include "mdl/MapHeader.h"
#include "mdl/Map_Entities.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"
#include "ui/ProjectResourceAudit.h"
#include "ui/ProjectResourceDialog.h"
#include "ui/ProjectSession.h"
#include "ui/QPathUtils.h"
#include "ui/TerrainChunks.h"
#include "ui/TerrainRegion.h"

#include <algorithm>
#include <sstream>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{
template <typename T>
T checked(Result<T> result)
{
  if (result.is_error())
    FAIL(std::get<Error>(result.error()).msg);
  return std::move(result).value();
}
void checked(Result<void> result)
{
  if (result.is_error())
    FAIL(std::get<Error>(result.error()).msg);
}
project::Asset addFile(
  ProjectSession& session,
  const std::string& path,
  const project::AssetType type = project::AssetType::Sound,
  const std::vector<std::uint8_t>& bytes = {1, 2, 3})
{
  auto input = project::AssetImport{};
  input.type = type;
  input.virtualPath = path;
  input.displayName = path;
  input.original = input.compatible = bytes;
  return checked(session.importAsset(input));
}
project::Asset addMap(
  ProjectSession& session,
  const std::string& path,
  const std::string& body,
  const mdl::MapFormat format = mdl::MapFormat::Standard)
{
  auto stream = std::ostringstream{};
  mdl::writeMapHeader(stream, "Quake", format, "Test");
  stream << body;
  const auto text = stream.str();
  return addFile(session, path, project::AssetType::Map, {text.begin(), text.end()});
}
const ProjectResourceUse& useOf(
  const ProjectResourceAudit& audit, const std::string& reference)
{
  const auto found =
    std::ranges::find(audit.uses, reference, &ProjectResourceUse::reference);
  REQUIRE(found != audit.uses.end());
  return *found;
}
std::vector<std::uint8_t> textureWad(const std::string& name)
{
  return checked(img::convertGoldSrcTexture(
                   {16, 16, std::vector<unsigned char>(16 * 16 * 4, 255)}, name))
    .wad;
}
} // namespace

TEST_CASE("ProjectResourceAudit")
{
  SECTION("browser usage is scoped and snapshot identities remain separate")
  {
    auto first = BrowserAsset{BrowserCellType::Sound, "sound/test.wav"};
    first.source = BrowserSource::Project;
    first.assetId = "first";
    auto second = first;
    second.assetId = "second";
    auto game = BrowserAsset{BrowserCellType::Sound, "sound/test.wav"};
    auto library = first;
    library.source = BrowserSource::AssetLibrary;
    auto captured = library;
    captured.projectAssetId = "first";
    auto audit = ProjectResourceAudit{};
    audit.uses.push_back(
      {"a",
       "maps/a.map",
       1,
       "ambient_generic.message",
       "SOUND/TEST.WAV",
       ProjectResourceStatus::Embedded,
       {"first"},
       {}});
    const auto used =
      browserAssetsUsedInMap({first, second, library, captured, game}, audit, "a");
    REQUIRE(used.size() == 3);
    CHECK(used.front().assetId == "first");
    CHECK(used[1].projectAssetId == "first");
    CHECK(used.back().source == BrowserSource::GameFiles);
    CHECK(browserAssetsUsedInMap({first, second, game}, audit, "b").empty());
  }
  auto temporary = QTemporaryDir{};
  REQUIRE(temporary.isValid());
  const auto path = pathFromQString(temporary.path()) / "audit.tbproj";
  auto session = checked(ProjectSession::create(path, "Audit", "Quake"));

  SECTION("cleanup protects custom property paths and transitive dependencies")
  {
    const auto model = addFile(*session, "models/tree.mdl", project::AssetType::Model);
    const auto texture = addFile(*session, "models/treeT.mdl", project::AssetType::Model);
    const auto animation =
      addFile(*session, "models/tree01.mdl", project::AssetType::Model);
    const auto unused = addFile(*session, "sound/unused.wav");
    {
      auto store = checked(project::ProjectStore::open(session->store().path()));
      checked(store->addDependency(model.id, texture.id, "texture"));
      checked(store->addDependency(texture.id, animation.id, "animation"));
    }
    const auto map = addMap(*session, "maps/custom.map", R"({"classname" "worldspawn"}
      {"classname" "custom_entity" "custom_model" "MODELS/TREE.MDL"})");
    const auto references = checked(projectAssetMapReferences(*session));
    for (const auto& id : {model.id, texture.id, animation.id})
    {
      CHECK(references.at(id) == std::vector{map.virtualPath});
      CHECK(session->removeAsset(id).is_error());
      CHECK(checked(session->store().findAsset(id)).has_value());
    }
    CHECK_FALSE(references.contains(unused.id));
    checked(session->removeAsset(unused.id));
    CHECK_FALSE(checked(session->store().findAsset(unused.id)).has_value());
  }

  SECTION("chunk containers retain child terrain material references")
  {
    const auto material =
      addFile(*session, "materials/terrain.tbmat", project::AssetType::Material);
    auto region = TerrainRegion{{0, 0}};
    region.materialAssetId = material.id;
    const auto body = std::string{R"({"classname" "worldspawn"}
      {"classname" "func_group" "_tb_terrain_paint" ")"}
                      + terrainChunkGroupMarker() + R"("}
      {"classname" "func_group" "_tb_terrain_paint" ")"
                      + region.encode() + R"("})";
    const auto map = addMap(*session, "maps/chunks.map", body);
    const auto audit = checked(auditGoldSrcProjectResources(*session));
    checked(validateGoldSrcCompileResources(audit, map.id));
    const auto references = checked(projectAssetMapReferences(*session));
    CHECK(references.at(material.id) == std::vector{map.virtualPath});
  }

  SECTION("a broken map can be removed while other resource deletion stays guarded")
  {
    const auto broken = addMap(*session, "maps/broken.map", "broken");
    const auto other = addMap(*session, "maps/other-broken.map", "broken");
    const auto unused = addFile(*session, "sound/unused.wav");
    CHECK(session->removeAsset(unused.id).is_error());
    checked(session->removeAsset(broken.id));
    CHECK(session->removeAsset(unused.id).is_error());
    checked(session->removeAsset(other.id));
    CHECK_FALSE(checked(session->store().findAsset(broken.id)).has_value());
    checked(session->removeAsset(unused.id));
  }

  SECTION("cleanup uses unsaved map contents and permits removal after unbinding")
  {
    const auto model = addFile(*session, "models/live.mdl", project::AssetType::Model);
    const auto asset = addMap(*session, "maps/live.map", R"({"classname" "worldspawn"})");
    auto fixture = MapDocumentFixture{};
    auto& document = fixture.create();
    checked(session->registerDocument(document, asset.id));
    const auto cleanup = qScopeGuard([&]() { session->unregisterDocument(document); });
    REQUIRE(mdl::setEntityProperty(document.map(), "custom_model", "models/live.mdl"));
    CHECK(checked(projectAssetMapReferences(*session)).contains(model.id));
    CHECK(session->removeAsset(model.id).is_error());
    REQUIRE(mdl::setEntityProperty(document.map(), "custom_model", ""));
    CHECK_FALSE(checked(projectAssetMapReferences(*session)).contains(model.id));
    checked(session->removeAsset(model.id));
    CHECK(document.map().modified());
  }

  SECTION("cleanup retains unreadable WADs while allowing unrelated unused resources")
  {
    const auto wad =
      addFile(*session, "textures/broken.wad", project::AssetType::Texture);
    const auto model = addFile(*session, "models/unused.mdl", project::AssetType::Model);
    CHECK(checked(projectAssetMapReferences(*session)).empty());
    const auto map = addMap(*session, "maps/empty.map", R"({"classname" "worldspawn"})");
    CHECK(checked(projectAssetMapReferences(*session)).contains(wad.id));
    checked(session->removeAsset(model.id));
    CHECK(session->removeAsset(wad.id).is_error());
    checked(session->removeAsset(map.id));
    checked(session->removeAsset(wad.id));
  }

  SECTION("cleanup does not guess sentence catalog dependencies")
  {
    const auto sound = addFile(*session, "sound/sentence.wav");
    addMap(*session, "maps/sentence.map", R"({"classname" "worldspawn"}
      {"classname" "ambient_generic" "message" "!SENTENCE"})");
    CHECK(projectAssetMapReferences(*session).is_error());
    CHECK(session->removeAsset(sound.id).is_error());
  }

  SECTION("cleanup refuses incomplete map scans")
  {
    const auto unused = addFile(*session, "sound/unused.wav");
    addMap(*session, "maps/broken.map", "broken");
    CHECK(projectAssetMapReferences(*session).is_error());
    CHECK(session->removeAsset(unused.id).is_error());
    CHECK(checked(session->store().findAsset(unused.id)).has_value());
  }

  SECTION("closed maps resolve by publish path and retain concrete locations")
  {
    const auto sound = addFile(*session, "sound/ambience/test.wav");
    const auto moved = checked(session->moveAsset(sound.id, "Favorites/audio.wav"));
    CHECK(moved.publishPath == sound.publishPath);
    const auto model =
      addFile(*session, "models/custom/tree.mdl", project::AssetType::Model);
    const auto firstMap = addMap(*session, "maps/a.map", R"({"classname" "worldspawn"}
      {"classname" "ambient_generic" "message" "AMBIENCE\TEST.WAV"}
      {"classname" "cycler" "model" "models/custom/tree.mdl"}
      {"classname" "env_sprite" "model" "sprites/missing.spr"})");
    addMap(*session, "maps/b.map", R"({"classname" "worldspawn"}
      {"classname" "env_message" "messagesound" "ambience/test.wav"})");
    checked(session->save());
    const auto before = checked(session->store().assets());
    const auto audit = checked(auditGoldSrcProjectResources(*session));
    CHECK(audit.mapsScanned == 2);
    REQUIRE(audit.uses.size() == 4);
    const auto& use = useOf(audit, "sound/AMBIENCE\\TEST.WAV");
    CHECK(use.status == ProjectResourceStatus::Embedded);
    CHECK(use.assetIds == std::vector{sound.id});
    CHECK(use.mapPath == "maps/a.map");
    CHECK(use.location == "ambient_generic.message");
    CHECK(use.line > 1);
    CHECK(useOf(audit, "models/custom/tree.mdl").assetIds == std::vector{model.id});
    CHECK(useOf(audit, "sprites/missing.spr").status == ProjectResourceStatus::Missing);
    CHECK_FALSE(session->modified());
    CHECK(checked(session->store().assets()) == before);
    const auto scoped = checked(auditGoldSrcProjectResources(*session, firstMap.id));
    CHECK(scoped.mapsScanned == 1);
    REQUIRE(scoped.uses.size() == 3);
    for (const auto& scopedUse : scoped.uses)
      CHECK(scopedUse.mapId == firstMap.id);
  }

  SECTION("sky group identity resolves from worldspawn face references")
  {
    const auto sky = addFile(*session, "skyboxes/live.tbsky", project::AssetType::Skybox);
    const auto map =
      addMap(*session, "maps/sky.map", R"({"classname" "worldspawn" "skyname" "live"})");
    const auto audit = checked(auditGoldSrcProjectResources(*session, map.id));
    auto entry = BrowserAsset{BrowserCellType::Texture, "skyboxes/live.tbsky"};
    entry.source = BrowserSource::Project;
    entry.assetId = sky.id;
    entry.skybox = true;
    CHECK(browserAssetsUsedInMap({entry}, audit, map.id).size() == 1);
  }

  SECTION("only explicit runtime fields count and indirect catalogs remain visible")
  {
    addMap(
      *session,
      "maps/fields.map",
      R"({"classname" "worldspawn" "message" "title.wav" "wad" "C:\external.wad"}
      {"classname" "info_player_deathmatch" "model" "models/editor.mdl"}
      {"classname" "game_text" "message" "not-a-sound.wav"}
      {"classname" "ambient_generic" "message" "!SENTENCE"}
      {"classname" "infodecal" "texture" "{bullet"}
      {"classname" "env_laser" "texture" "sprites/beam.spr" "EndSprite" "sprites/end.spr"}
      {"classname" "func_breakable" "gibmodel" "models/gib.mdl"}
      {"classname" "env_shooter" "shootmodel" "models/can.mdl"})");
    const auto audit = checked(auditGoldSrcProjectResources(*session));
    REQUIRE(audit.uses.size() == 6);
    CHECK(useOf(audit, "!SENTENCE").status == ProjectResourceStatus::Unsupported);
    CHECK(useOf(audit, "{bullet").status == ProjectResourceStatus::Unsupported);
    CHECK(useOf(audit, "sprites/end.spr").location == "env_laser.EndSprite");
  }

  SECTION("relative paths reject traversal absolute paths and empty components")
  {
    addMap(*session, "maps/paths.map", R"({"classname" "worldspawn"}
      {"classname" "env_sprite" "model" "../escape.spr"}
      {"classname" "env_sprite" "model" "C:\absolute.spr"}
      {"classname" "env_sprite" "model" "\\server\file.spr"}
      {"classname" "env_sprite" "model" "/absolute.spr"}
      {"classname" "env_sprite" "model" "sprites//empty.spr"}
      {"classname" "env_sprite" "model" "sprites/./dot.spr"}
      {"classname" "ambient_generic" "message" "../escape.wav"})");
    const auto audit = checked(auditGoldSrcProjectResources(*session));
    REQUIRE(audit.uses.size() == 7);
    for (const auto& use : audit.uses)
      CHECK(use.status == ProjectResourceStatus::InvalidPath);
  }

  SECTION("WAD members resolve case insensitively and duplicates are ambiguous")
  {
    const auto first = addFile(
      *session, "textures/a.wad", project::AssetType::Texture, textureWad("wall"));
    const auto second = addFile(
      *session, "textures/b.wad", project::AssetType::Texture, textureWad("wall"));
    addMap(*session, "maps/standard.map", R"({"classname" "worldspawn"
      {
        ( 0 0 0 ) ( 1 0 0 ) ( 0 1 0 ) WALL 0 0 0 1 1
        ( 0 0 0 ) ( 1 0 0 ) ( 0 1 0 ) absent 0 0 0 1 1
      }})");
    addMap(
      *session,
      "maps/valve.map",
      R"({"classname" "worldspawn"
      {( 0 0 0 ) ( 1 0 0 ) ( 0 1 0 ) WALL [ 1 0 0 0 ] [ 0 1 0 0 ] 0 1 1 } })",
      mdl::MapFormat::Valve);
    auto audit = checked(auditGoldSrcProjectResources(*session));
    REQUIRE(audit.mapsScanned == 2);
    REQUIRE(audit.uses.size() == 3);
    const auto& use = useOf(audit, "WALL");
    CHECK(use.status == ProjectResourceStatus::Ambiguous);
    CHECK(use.assetIds.size() == 2);
    CHECK(useOf(audit, "absent").status == ProjectResourceStatus::Missing);
    CHECK(use.location == "worldspawn brush face");
    CHECK(use.line > 1);
    CHECK(session->removeAsset(second.id).is_error());
    {
      auto store = checked(project::ProjectStore::open(session->store().path()));
      checked(store->removeAsset(second.id));
    }
    audit = checked(auditGoldSrcProjectResources(*session));
    CHECK(useOf(audit, "WALL").status == ProjectResourceStatus::Embedded);
    CHECK(useOf(audit, "WALL").assetIds == std::vector{first.id});
    auto replacement = project::AssetImport{};
    replacement.type = project::AssetType::Texture;
    replacement.original = replacement.compatible = textureWad("changed");
    checked(session->replaceAsset(first.id, replacement));
    audit = checked(auditGoldSrcProjectResources(*session));
    CHECK(useOf(audit, "WALL").status == ProjectResourceStatus::Missing);
  }

  SECTION("live map edits are scanned without changing revisions dirty state or disk")
  {
    auto fixture = MapDocumentFixture{};
    auto& document = fixture.create();
    auto& map = document.map();
    const auto asset = addMap(*session, "maps/live.map", R"({"classname" "worldspawn"})");
    checked(session->registerDocument(document, asset.id));
    checked(session->save());
    REQUIRE(mdl::setEntityProperty(map, "skyname", "live"));
    auto file = QFile{pathAsQString(path)};
    REQUIRE(file.open(QIODevice::ReadOnly));
    const auto diskBefore = file.readAll();
    file.close();
    const auto audit = checked(auditGoldSrcProjectResources(*session));
    CHECK(audit.mapsScanned == 1);
    REQUIRE(audit.uses.size() == 6);
    CHECK(useOf(audit, "gfx/env/liveup.tga").status == ProjectResourceStatus::Missing);
    CHECK(map.modified());
    CHECK(session->modified());
    CHECK(checked(session->store().findAsset(asset.id)) == asset);
    REQUIRE(file.open(QIODevice::ReadOnly));
    CHECK(file.readAll() == diskBefore);
    file.close();
    REQUIRE(mdl::setEntityProperty(map, "skyname", "changed"));
    const auto next = checked(auditGoldSrcProjectResources(*session));
    REQUIRE(next.uses.size() == 6);
    CHECK(useOf(next, "gfx/env/changedup.tga").status == ProjectResourceStatus::Missing);
    session->unregisterDocument(document);
  }

  SECTION("broken maps and WADs do not conceal incomplete scans or stop other maps")
  {
    addMap(*session, "maps/broken.map", R"({"classname" "worldspawn")");
    addMap(*session, "maps/empty.map", "");
    addMap(
      *session,
      "maps/unknown.map",
      R"({"classname" "worldspawn"})",
      mdl::MapFormat::Quake2);
    addFile(*session, "textures/broken.wad", project::AssetType::Texture);
    addMap(*session, "maps/good.map", R"({"classname" "worldspawn"}
      {"classname" "env_sprite" "model" "sprites/missing.spr"})");
    const auto audit = checked(auditGoldSrcProjectResources(*session));
    CHECK(audit.mapsScanned == 1);
    CHECK(
      std::ranges::count(
        audit.uses, ProjectResourceStatus::ScanError, &ProjectResourceUse::status)
      == 4);
    CHECK(useOf(audit, "sprites/missing.spr").status == ProjectResourceStatus::Missing);
  }

  SECTION("dialog filters current uses and preserves incomplete scan diagnostics")
  {
    const auto sound = addFile(*session, "sound/test.wav");
    addMap(*session, "maps/a.map", R"({"classname" "worldspawn"}
      {"classname" "ambient_generic" "message" "test.wav"})");
    const auto broken = addMap(*session, "maps/broken.map", "");
    checked(session->save());
    auto opened = std::string{};
    auto dialog =
      ProjectResourceDialog{*session, sound.id, [&](const auto& id) { opened = id; }};
    auto* tree = dialog.findChild<QTreeWidget*>("ProjectResourceUses");
    auto* search = dialog.findChild<QLineEdit*>();
    auto* issues = dialog.findChild<QCheckBox*>();
    REQUIRE(tree);
    REQUIRE(search);
    REQUIRE(issues);
    CHECK(tree->topLevelItemCount() == 2);
    issues->setChecked(true);
    CHECK(tree->topLevelItemCount() == 1);
    CHECK(
      tree->topLevelItem(0)->data(0, Qt::UserRole).toString().toStdString() == broken.id);
    issues->setChecked(false);
    search->setText("sound/test.wav");
    CHECK(tree->topLevelItemCount() == 1);
    search->clear();
    const auto unused = addFile(*session, "sound/unused.wav");
    auto unusedDialog = ProjectResourceDialog{*session, unused.id, [](const auto&) {}};
    CHECK(unusedDialog.findChild<QTreeWidget*>()->topLevelItemCount() == 1);
    REQUIRE(unusedDialog.findChild<QLabel*>("ProjectResourceSummary"));
    CHECK(unusedDialog.findChild<QLabel*>("ProjectResourceSummary")
            ->text()
            .contains("1 failed scans"));
    auto* row = tree->topLevelItem(0);
    tree->setCurrentItem(row);
    for (auto* button : dialog.findChildren<QPushButton*>())
      if (button->text() == "Open Map")
        button->click();
    CHECK(opened == row->data(0, Qt::UserRole).toString().toStdString());
  }
}
} // namespace tb::ui
