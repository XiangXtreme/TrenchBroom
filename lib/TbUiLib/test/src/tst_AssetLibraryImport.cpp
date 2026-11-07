/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom, licensed under GPL version 3 or later.
*/

#include "base/Uuid.h"
#include "project/AssetLibrary.h"
#include "project/GoldSrcWad.h"
#include "ui/AssetLibraryImport.h"
#include "ui/AssetLibrarySource.h"

#include <algorithm>
#include <cstring>
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

  SECTION("imports own their content and replacement is atomic")
  {
    auto workspace = Workspace{};
    const auto bytes = makeTga(16, 16, false);
    const auto source = workspace.write("source/wall.tga", bytes);
    auto library = require(project::AssetLibrary::create(workspace.folder, "Test"));
    require(library->importFiles({{source, "textures/wall.tga"}}));
    const auto id = require(library->assets()).front().id;
    CHECK_FALSE(std::filesystem::exists(workspace.folder / "textures/wall.tga"));
    CHECK(library->importFiles({{source, "textures/wall.tga"}}).is_error());
    CHECK(
      library->importFiles({{workspace.directory / "missing", "textures/wall.tga"}}, true)
        .is_error());
    CHECK(require(library->readOriginal(id)) == bytes);
    CHECK(library->importFiles({{source, "../escaped.tga"}}).is_error());
    std::filesystem::remove(source);
    library = require(project::AssetLibrary::open(workspace.folder));
    CHECK(require(library->readOriginal(id)) == bytes);
    CHECK_FALSE(require(library->findAsset(id))->missing);
  }

  SECTION("converts an imported image into GoldSrc texture content")
  {
    auto workspace = Workspace{};
    const auto source = workspace.write("source/wall01.tga", makeTga(16, 16, false));
    auto library = require(project::AssetLibrary::create(workspace.folder, "Test"));
    require(library->importFiles({{source, "textures/" + source.filename().string()}}));

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

  SECTION("importing a model brings its texture and animation group siblings")
  {
    auto workspace = Workspace{};
    auto model = std::vector<std::uint8_t>(244, 0);
    const auto identifier = std::uint32_t{0x54534449}; // "IDST"
    const auto version = std::uint32_t{10};
    const auto groups = std::uint32_t{2};
    std::memcpy(model.data(), &identifier, sizeof(identifier));
    std::memcpy(model.data() + 4, &version, sizeof(version));
    std::memcpy(model.data() + 172, &groups, sizeof(groups));
    const auto modelPath = workspace.write("source/test.mdl", model);
    workspace.write("source/testT.mdl", {1, 2, 3});
    workspace.write("source/test01.mdl", {4, 5, 6});

    require(project::AssetLibrary::create(workspace.folder, "Test"));
    const auto source = openLocalAssetLibrary(workspace.folder);
    REQUIRE(source);
    require(source->importFile(modelPath, "models/test.mdl"));

    std::filesystem::remove_all(workspace.directory / "source");
    const auto library = require(project::AssetLibrary::open(workspace.folder));
    const auto assets = require(library->assets());
    REQUIRE(assets.size() == 3);
    for (const auto& asset : assets)
    {
      CHECK_FALSE(require(library->readContent(asset.id)).empty());
      CHECK_FALSE(asset.missing);
    }
    const auto main =
      std::ranges::find(assets, "models/test.mdl", &project::LibraryAsset::virtualPath);
    REQUIRE(main != assets.end());
    CHECK(require(library->dependencies(main->id)).size() == 2);
  }

  SECTION("non-model imports do not discover model siblings from their contents")
  {
    auto workspace = Workspace{};
    auto bytes = std::vector<std::uint8_t>(244, 0);
    const auto identifier = std::uint32_t{0x54534449};
    const auto version = std::uint32_t{10};
    const auto groups = std::uint32_t{2};
    std::memcpy(bytes.data(), &identifier, sizeof(identifier));
    std::memcpy(bytes.data() + 4, &version, sizeof(version));
    std::memcpy(bytes.data() + 172, &groups, sizeof(groups));
    const auto path = workspace.write("source/test.wav", bytes);
    workspace.write("source/testT.wav", {1, 2, 3});
    workspace.write("source/test01.wav", {4, 5, 6});
    require(project::AssetLibrary::create(workspace.folder, "Test"));
    const auto source = openLocalAssetLibrary(workspace.folder);
    REQUIRE(source);
    require(source->importFile(path, "sound/test.wav"));
    CHECK(require(source->allAssets()).size() == 1);
    CHECK_FALSE(std::filesystem::exists(workspace.folder / "sound/test.wav"));
    CHECK_FALSE(std::filesystem::exists(workspace.folder / "sound/testT.wav"));
    CHECK_FALSE(std::filesystem::exists(workspace.folder / "sound/test01.wav"));
  }

  SECTION("masked images get the transparency prefix")
  {
    auto workspace = Workspace{};
    const auto source = workspace.write("source/glass.tga", makeTga(16, 16, true));
    auto library = require(project::AssetLibrary::create(workspace.folder, "Test"));
    require(library->importFiles({{source, "textures/" + source.filename().string()}}));

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
