/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom, licensed under GPL version 3 or later.
*/

#include "base/Uuid.h"
#include "project/AssetLibrary.h"
#include "project/GoldSrcWad.h"
#include "ui/AssetLibraryImport.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{

struct Workspace
{
  std::filesystem::path directory =
    std::filesystem::temp_directory_path() / ("tb-library-import-" + generateUuid());
  std::filesystem::path folder = directory / "library";

  Workspace() { std::filesystem::create_directories(folder); }
  ~Workspace()
  {
    auto error = std::error_code{};
    std::filesystem::remove_all(directory, error);
  }

  std::filesystem::path write(
    const std::string& relativePath, const std::vector<std::uint8_t>& bytes) const
  {
    const auto path = directory / std::filesystem::path{relativePath};
    std::filesystem::create_directories(path.parent_path());
    auto file = std::ofstream{path, std::ios::binary};
    file.write(
      reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    file.close();
    return path;
  }
};

template <typename T>
T require(Result<T> result)
{
  if (result.is_error())
    FAIL(std::get<Error>(result.error()).msg);
  return std::move(result).value();
}

// Minimal uncompressed 32 bit TGA so the test does not need an image fixture.
std::vector<std::uint8_t> makeTga(
  const std::uint16_t width, const std::uint16_t height, const bool transparent)
{
  auto bytes = std::vector<std::uint8_t>(18 + std::size_t{width} * height * 4, 0);
  bytes[2] = 2;
  bytes[12] = std::uint8_t(width & 0xff);
  bytes[13] = std::uint8_t((width >> 8) & 0xff);
  bytes[14] = std::uint8_t(height & 0xff);
  bytes[15] = std::uint8_t((height >> 8) & 0xff);
  bytes[16] = 32;
  bytes[17] = 8;
  for (auto i = std::size_t{18}; i + 3 < bytes.size(); i += 4)
  {
    bytes[i + 0] = 0x20;
    bytes[i + 1] = 0x40;
    bytes[i + 2] = 0x80;
    bytes[i + 3] = transparent ? 0x00 : 0xff;
  }
  return bytes;
}

} // namespace

TEST_CASE("AssetLibraryImport")
{
  SECTION("maps file extensions to library subfolders")
  {
    CHECK(librarySubfolderForFile("wall.png") == "textures");
    CHECK(librarySubfolderForFile("wall.JPG") == "textures");
    CHECK(librarySubfolderForFile("wall.jpeg") == "textures");
    CHECK(librarySubfolderForFile("wall.bmp") == "textures");
    CHECK(librarySubfolderForFile("wall.tga") == "textures");
    CHECK(librarySubfolderForFile("custom.wad") == "textures");
    CHECK(librarySubfolderForFile("barrel.mdl") == "models");
    CHECK(librarySubfolderForFile("flame.spr") == "sprites");
    CHECK(librarySubfolderForFile("step.wav") == "sound");
    CHECK(librarySubfolderForFile("notes.txt").empty());
    CHECK(librarySubfolderForFile("no_extension").empty());
  }

  SECTION("normalizes file stems into GoldSrc texture names")
  {
    CHECK(goldSrcTextureNameFromStem("wall01", false) == "wall01");
    CHECK(goldSrcTextureNameFromStem("wall texture!", false) == "wall_texture_");
    CHECK(goldSrcTextureNameFromStem("", false) == "texture");
    CHECK(goldSrcTextureNameFromStem("glass", true) == "{glass");
    CHECK(goldSrcTextureNameFromStem("averylongtexturename", false) == "averylongtextur");
    CHECK(goldSrcTextureNameFromStem("averylongtexturename", true) == "{averylongtextu");
  }

  SECTION("imports files and refuses an existing target")
  {
    auto workspace = Workspace{};
    const auto source = workspace.write("source/wall01.tga", makeTga(16, 16, false));

    const auto imported =
      require(importFileIntoLibrary(workspace.folder, source, "textures/wall01.tga"));
    CHECK(imported == "textures/wall01.tga");
    CHECK(std::filesystem::exists(workspace.folder / "textures/wall01.tga"));
    CHECK(
      importFileIntoLibrary(workspace.folder, source, "textures/wall01.tga").is_error());
  }

  SECTION("failed replacement preserves the existing source and rejects escaping paths")
  {
    auto workspace = Workspace{};
    const auto source = workspace.write("source/wall.tga", makeTga(16, 16, false));
    require(importFileIntoLibrary(workspace.folder, source, "textures/wall.tga"));
    CHECK(importFileIntoLibrary(
            workspace.folder, workspace.directory / "missing", "textures/wall.tga", true)
            .is_error());
    CHECK(
      std::filesystem::file_size(workspace.folder / "textures/wall.tga")
      == makeTga(16, 16, false).size());
    const auto replacement =
      workspace.write("replacement/wall.tga", makeTga(32, 16, false));
    require(
      importFileIntoLibrary(workspace.folder, replacement, "textures/wall.tga", true));
    CHECK(
      std::filesystem::file_size(workspace.folder / "textures/wall.tga")
      == makeTga(32, 16, false).size());
    CHECK(importFileIntoLibrary(workspace.folder, source, "../escaped.tga").is_error());
    CHECK_FALSE(std::filesystem::exists(workspace.directory / "escaped.tga"));
  }

  SECTION("converts an imported image into GoldSrc texture content")
  {
    auto workspace = Workspace{};
    const auto source = workspace.write("source/wall01.tga", makeTga(16, 16, false));
    require(importFileIntoLibrary(workspace.folder, source, "textures/wall01.tga"));

    auto library = require(project::AssetLibrary::create(workspace.folder, "Test"));
    require(library->scan());

    const auto assets = require(library->assets(project::AssetType::Texture));
    REQUIRE(assets.size() == 1);
    const auto& asset = assets.front();
    CHECK(asset.virtualPath == "textures/wall01.tga");
    CHECK(asset.nativeFormat == "image");

    CHECK(require(convertLibraryImage(*library, asset.id)));
    const auto converted = require(library->findAsset(asset.id));
    CHECK(converted->nativeFormat == "goldsrc-wad");

    const auto archive =
      require(project::readGoldSrcWad(require(library->readContent(asset.id))));
    REQUIRE(archive.textures.size() == 1);
    CHECK(archive.textures.front().name == "wall01");
    CHECK_FALSE(require(convertLibraryImage(*library, asset.id)));
  }

  SECTION("masked images get the transparency prefix")
  {
    auto workspace = Workspace{};
    const auto source = workspace.write("source/glass.tga", makeTga(16, 16, true));
    require(importFileIntoLibrary(workspace.folder, source, "textures/glass.tga"));

    auto library = require(project::AssetLibrary::create(workspace.folder, "Test"));
    require(library->scan());

    const auto assets = require(library->assets(project::AssetType::Texture));
    REQUIRE(assets.size() == 1);
    CHECK(require(convertLibraryImage(*library, assets.front().id)));
    const auto archive =
      require(project::readGoldSrcWad(require(library->readContent(assets.front().id))));
    REQUIRE(archive.textures.size() == 1);
    CHECK(archive.textures.front().name == "{glass");
  }
}

} // namespace tb::ui
