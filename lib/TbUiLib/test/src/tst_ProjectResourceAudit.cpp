/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QCheckBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
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
  auto temporary = QTemporaryDir{};
  REQUIRE(temporary.isValid());
  const auto path = pathFromQString(temporary.path()) / "audit.tbproj";
  auto session = checked(ProjectSession::create(path, "Audit", "Quake"));

  SECTION("closed maps resolve by publish path and retain concrete locations")
  {
    const auto sound = addFile(*session, "sound/ambience/test.wav");
    const auto moved = checked(session->moveAsset(sound.id, "Favorites/audio.wav"));
    CHECK(moved.publishPath == sound.publishPath);
    const auto model =
      addFile(*session, "models/custom/tree.mdl", project::AssetType::Model);
    addMap(*session, "maps/a.map", R"({"classname" "worldspawn"}
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
    checked(session->removeAsset(second.id));
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
