/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QTemporaryDir>

#include "img/GoldSrcTexture.h"
#include "project/GoldSrcWad.h"
#include "ui/ProjectReference.h"
#include "ui/ProjectSession.h"
#include "ui/QPathUtils.h"

#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{
template <typename T>
T require(Result<T> value)
{
  if (value.is_error())
    FAIL(std::get<Error>(value.error()).msg);
  return std::move(value).value();
}
void require(Result<void> value)
{
  if (value.is_error())
    FAIL(std::get<Error>(value.error()).msg);
}

std::vector<std::uint8_t> textureWad(const std::string& name)
{
  return require(img::convertGoldSrcTexture(
                   {16, 16, std::vector<std::uint8_t>(16 * 16 * 4, 255)}, name))
    .wad;
}

std::vector<std::uint8_t> textureLump(const std::string& name)
{
  const auto archive = require(project::readGoldSrcWad(textureWad(name)));
  REQUIRE(archive.textures.size() == 1);
  return archive.textures.front().miptex;
}

project::Asset addAsset(
  ProjectSession& session,
  const std::string& virtualPath,
  const project::AssetType type,
  const std::vector<std::uint8_t>& bytes)
{
  auto input = project::AssetImport{};
  input.type = type;
  input.virtualPath = virtualPath;
  input.displayName = virtualPath;
  input.original = input.compatible = bytes;
  return require(session.importAsset(input));
}
} // namespace

TEST_CASE("ProjectReferenceIndex")
{
  auto temporary = QTemporaryDir{};
  REQUIRE(temporary.isValid());
  auto session = require(ProjectSession::create(
    pathFromQString(temporary.filePath("reference.tbproj")), "Reference", "Half-Life"));

  SECTION("texture names resolve case insensitively and duplicates are ambiguous")
  {
    const auto first = addAsset(
      *session, "textures/a.wad", project::AssetType::Texture, textureWad("wall"));
    const auto second = addAsset(
      *session, "textures/b.wad", project::AssetType::Texture, textureWad("wall"));
    auto index = require(ProjectReferenceIndex::build(*session));
    const auto duplicated = index.resolveTexture("WALL");
    CHECK(duplicated.status == ReferenceStatus::Ambiguous);
    CHECK(duplicated.assets.size() == 2);
    CHECK(index.resolveTexture("absEnt").status == ReferenceStatus::Missing);

    require(session->removeAsset(second.id));
    index = require(ProjectReferenceIndex::build(*session));
    const auto resolved = index.resolveTexture("Wall");
    CHECK(resolved.status == ReferenceStatus::Resolved);
    REQUIRE(resolved.assets.size() == 1);
    CHECK(resolved.assets.front() == first);
  }

  SECTION("publish paths resolve without depending on case or platform separators")
  {
    const auto sound = addAsset(
      *session, "sound/clip.wav", project::AssetType::Sound, {1, 2, 3});
    const auto index = require(ProjectReferenceIndex::build(*session));
    for (const auto* path : {"sound/clip.wav", "SOUND/CLIP.WAV", "sound\\clip.wav"})
    {
      const auto match = index.resolvePath(path);
      CHECK(match.status == ReferenceStatus::Resolved);
      REQUIRE(match.assets.size() == 1);
      CHECK(match.assets.front() == sound);
    }
    CHECK(index.resolvePath("sound/missing.wav").status == ReferenceStatus::Missing);
    CHECK(index.resolvePath("../escape.wav").status == ReferenceStatus::Missing);
    CHECK(index.resolvePath("/absolute.wav").status == ReferenceStatus::Missing);
  }

  SECTION("maps and prefabs never become references")
  {
    addAsset(*session, "maps/test.map", project::AssetType::Map, {'{', '}'});
    const auto index = require(ProjectReferenceIndex::build(*session));
    CHECK(index.resolvePath("maps/test.map").status == ReferenceStatus::Missing);
  }

  SECTION("unreadable WADs are reported instead of silently skipped")
  {
    const auto broken =
      addAsset(*session, "textures/broken.wad", project::AssetType::Texture, {1, 2, 3});
    addAsset(
      *session, "textures/good.wad", project::AssetType::Texture, textureWad("wall"));
    const auto index = require(ProjectReferenceIndex::build(*session));
    REQUIRE(index.unreadableWads().size() == 1);
    CHECK(index.unreadableWads().front().assetId == broken.id);
    CHECK(index.unreadableWads().front().virtualPath == "textures/broken.wad");
    CHECK_FALSE(index.unreadableWads().front().message.empty());
    CHECK(index.resolveTexture("wall").status == ReferenceStatus::Resolved);
  }
}

TEST_CASE("ProjectReferenceNames")
{
  auto temporary = QTemporaryDir{};
  REQUIRE(temporary.isValid());
  auto session = require(ProjectSession::create(
    pathFromQString(temporary.filePath("names.tbproj")), "Names", "Half-Life"));

  SECTION("a captured texture is named after its miptex")
  {
    const auto texture = addAsset(
      *session, "textures/wall01.wad", project::AssetType::Texture, textureWad("WALL01"));
    CHECK(require(gameName(*session, texture)) == "WALL01");
  }

  SECTION("resources without embedded members are named by their publish path")
  {
    const auto sound =
      addAsset(*session, "sound/clip.wav", project::AssetType::Sound, {1, 2, 3});
    CHECK(require(gameName(*session, sound)) == "sound/clip.wav");
  }

  SECTION("an archive embedding several textures has no single name")
  {
    const auto archive = require(
      project::buildGoldSrcWad({textureLump("first"), textureLump("second")}));
    const auto texture =
      addAsset(*session, "textures/mixed.wad", project::AssetType::Texture, archive);
    const auto name = gameName(*session, texture);
    CHECK(name.is_error());
    CHECK(std::get<Error>(name.error()).msg.find("2 textures") != std::string::npos);
  }

  SECTION("an archive without usable miptex has no name")
  {
    const auto texture = addAsset(
      *session,
      "textures/empty.wad",
      project::AssetType::Texture,
      require(project::buildGoldSrcWad({})));
    CHECK(gameName(*session, texture).is_error());
  }

  SECTION("a capture previews the name it will publish")
  {
    auto texture = project::AssetImport{};
    texture.type = project::AssetType::Texture;
    texture.virtualPath = "textures/WALL01.wad";
    CHECK(require(capturedGameName(texture)) == "WALL01");

    texture.virtualPath = "textures/{MASK.wad";
    CHECK(require(capturedGameName(texture)) == "{MASK");

    auto sound = project::AssetImport{};
    sound.type = project::AssetType::Sound;
    sound.virtualPath = "sound/clip.wav";
    CHECK(require(capturedGameName(sound)) == "sound/clip.wav");

    auto unnamed = project::AssetImport{};
    unnamed.type = project::AssetType::Texture;
    CHECK(capturedGameName(unnamed).is_error());
  }
}
} // namespace tb::ui
