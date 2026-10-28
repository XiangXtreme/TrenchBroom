/*
 Copyright (C) 2026 XiangXtreme

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

#include <QImage>
#include <QRgb>

#include "fs/TestEnvironment.h"
#include "img/GoldSrcTexture.h"
#include "mdl/GameFileSystem.h"
#include "ui/AssetPreviewProvider.h"
#include "ui/PrefabAsset.h"
#include "ui/QPathUtils.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{

void appendI32(std::vector<std::uint8_t>& data, const std::int32_t value)
{
  const auto unsignedValue = std::uint32_t(value);
  data.push_back(std::uint8_t(unsignedValue & 0xFFu));
  data.push_back(std::uint8_t((unsignedValue >> 8u) & 0xFFu));
  data.push_back(std::uint8_t((unsignedValue >> 16u) & 0xFFu));
  data.push_back(std::uint8_t((unsignedValue >> 24u) & 0xFFu));
}

void appendU16(std::vector<std::uint8_t>& data, const std::uint16_t value)
{
  data.push_back(std::uint8_t(value & 0xFFu));
  data.push_back(std::uint8_t((value >> 8u) & 0xFFu));
}

void appendF32(std::vector<std::uint8_t>& data, const float value)
{
  auto raw = std::uint32_t{};
  std::memcpy(&raw, &value, sizeof(raw));
  data.push_back(std::uint8_t(raw & 0xFFu));
  data.push_back(std::uint8_t((raw >> 8u) & 0xFFu));
  data.push_back(std::uint8_t((raw >> 16u) & 0xFFu));
  data.push_back(std::uint8_t((raw >> 24u) & 0xFFu));
}

std::vector<std::uint8_t> makeSingleFrameSprite()
{
  auto data = std::vector<std::uint8_t>{};
  data.insert(std::end(data), {'I', 'D', 'S', 'P'});
  appendI32(data, 2);    // version
  appendI32(data, 2);    // VP_PARALLEL
  appendI32(data, 0);    // normal alpha
  appendF32(data, 0.0f); // radius
  appendI32(data, 2);    // width
  appendI32(data, 1);    // height
  appendI32(data, 1);    // frame count
  appendF32(data, 0.0f); // beam length
  appendI32(data, 0);    // synchronized
  appendU16(data, 256);

  for (auto i = 0; i < 256; ++i)
  {
    data.push_back(std::uint8_t(i));
    data.push_back(std::uint8_t(255 - i));
    data.push_back(std::uint8_t((i * 2) & 0xFF));
  }

  appendI32(data, 0); // single frame
  appendI32(data, -1);
  appendI32(data, 0);
  appendI32(data, 2);
  appendI32(data, 1);
  data.push_back(1);
  data.push_back(2);
  return data;
}

void writeBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
{
  auto stream = std::ofstream{path, std::ios::out | std::ios::binary};
  stream.write(
    reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

} // namespace

TEST_CASE("AssetPreviewProvider")
{
  auto env = fs::TestEnvironment{};
  auto gameFileSystem = mdl::GameFileSystem{};
  auto provider = AssetPreviewProvider{gameFileSystem};

  SECTION("project WAD preview reads back converted colors and transparency")
  {
    auto image = img::Image{16, 16, std::vector<unsigned char>(16 * 16 * 4, 255)};
    image.pixels[3] = 0;
    auto converted = img::convertGoldSrcTexture(image, "{brick");
    REQUIRE(converted.is_success());
    const auto path = env.dir() / "brick.wad";
    writeBytes(path, converted.value().wad);
    const auto preview = provider.preview(
      BrowserAsset{BrowserCellType::Texture, "textures/brick.wad", path});
    REQUIRE(preview.status == AssetPreviewStatus::Ready);
    REQUIRE(preview.sprite);
    CHECK(preview.sprite->width == 16u);
    CHECK(preview.sprite->height == 16u);
    CHECK(preview.sprite->rgba == converted.value().preview.pixels);
    writeBytes(path, {'b', 'a', 'd'});
    CHECK(
      provider.preview(BrowserAsset{BrowserCellType::Texture, path, path}).status
      == AssetPreviewStatus::Error);
  }

  SECTION("valid SPR returns ready sprite preview")
  {
    const auto path = env.dir() / "valid.spr";
    writeBytes(path, makeSingleFrameSprite());

    const auto preview =
      provider.preview(BrowserAsset{BrowserCellType::Sprite, "sprites/valid.spr", path});

    REQUIRE(preview.status == AssetPreviewStatus::Ready);
    REQUIRE(preview.sprite.has_value());
    CHECK(preview.sprite->width == 2u);
    CHECK(preview.sprite->height == 1u);
  }

  SECTION("invalid SPR returns error")
  {
    const auto path = env.dir() / "invalid.spr";
    writeBytes(path, {'N', 'O', 'P', 'E'});

    const auto preview = provider.preview(
      BrowserAsset{BrowserCellType::Sprite, "sprites/invalid.spr", path});

    CHECK(preview.status == AssetPreviewStatus::Error);
    CHECK_FALSE(preview.sprite.has_value());
  }

  SECTION("missing SPR returns missing")
  {
    const auto preview = provider.preview(BrowserAsset{
      BrowserCellType::Sprite, "sprites/missing.spr", env.dir() / "missing.spr"});

    CHECK(preview.status == AssetPreviewStatus::Missing);
    CHECK_FALSE(preview.sprite.has_value());
  }

  SECTION("valid WAV returns ready sound preview")
  {
    const auto path = env.dir() / "hum.wav";
    writeBytes(path, {'R', 'I', 'F', 'F'});

    const auto preview = provider.preview(
      BrowserAsset{BrowserCellType::Sound, "sound/ambience/hum.wav", path});

    CHECK(preview.status == AssetPreviewStatus::Ready);
    CHECK(preview.soundPath == path);
    CHECK_FALSE(preview.sprite.has_value());

    const auto previews = loadAssetPreviews(
      provider,
      {
        BrowserAsset{BrowserCellType::Sound, "sound/ambience/hum.wav", path},
        BrowserAsset{
          BrowserCellType::Sprite, "sprites/missing.spr", env.dir() / "missing.spr"},
      });

    CHECK(previews.contains("sound/ambience/hum.wav"));
    CHECK(previews.contains("sprites/missing.spr"));
  }

  SECTION("loads only requested previews")
  {
    const auto soundPath = env.dir() / "hum.wav";
    writeBytes(soundPath, {'R', 'I', 'F', 'F'});

    const auto previews = loadAssetPreviews(
      provider,
      {
        BrowserAsset{BrowserCellType::Sound, "sound/ambience/hum.wav", soundPath},
        BrowserAsset{
          BrowserCellType::Sprite, "sprites/missing.spr", env.dir() / "missing.spr"},
      },
      {std::filesystem::path{"sound/ambience/hum.wav"}});

    CHECK(previews.contains("sound/ambience/hum.wav"));
    CHECK_FALSE(previews.contains("sprites/missing.spr"));
  }

  SECTION("missing WAV returns missing")
  {
    const auto preview = provider.preview(BrowserAsset{
      BrowserCellType::Sound, "sound/ambience/missing.wav", env.dir() / "missing.wav"});

    CHECK(preview.status == AssetPreviewStatus::Missing);
    CHECK(preview.soundPath.empty());
  }

  SECTION("valid prefab thumbnail returns ready preview")
  {
    const auto path = env.dir() / "crate.tbprefab";
    writeBytes(path, {'p', 'r', 'e', 'f', 'a', 'b'});

    auto image = QImage{2, 1, QImage::Format_RGBA8888};
    image.setPixel(0, 0, qRgba(255, 0, 0, 255));
    image.setPixel(1, 0, qRgba(0, 255, 0, 128));
    REQUIRE(image.save(pathAsQString(prefabThumbnailPath(path))));

    const auto preview = provider.preview(
      BrowserAsset{BrowserCellType::Prefab, "prefabs/crate.tbprefab", path});

    REQUIRE(preview.status == AssetPreviewStatus::Ready);
    REQUIRE(preview.sprite.has_value());
    CHECK(preview.sprite->width == 2u);
    CHECK(preview.sprite->height == 1u);
    CHECK(preview.sprite->rgba.size() == 8u);
  }

  SECTION("missing prefab thumbnail returns missing preview")
  {
    const auto preview = provider.preview(BrowserAsset{
      BrowserCellType::Prefab,
      "prefabs/missing.tbprefab",
      env.dir() / "missing.tbprefab"});

    CHECK(preview.status == AssetPreviewStatus::Missing);
    CHECK_FALSE(preview.sprite.has_value());
  }
}

} // namespace tb::ui
