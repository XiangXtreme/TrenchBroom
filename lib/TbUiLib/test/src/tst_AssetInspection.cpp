/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QBuffer>
#include <QComboBox>
#include <QImage>
#include <QLabel>
#include <QMediaPlayer>
#include <QTemporaryDir>

#include "img/GoldSrcTexture.h"
#include "project/AssetLibrary.h"
#include "ui/AssetInspection.h"
#include "ui/AssetInspector.h"
#include "ui/AssetLibraryImport.h"
#include "ui/AssetLibrarySource.h"
#include "ui/QPathUtils.h"

#include <fstream>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{
template <typename T>
T require(Result<T> result)
{
  if (result.is_error())
    FAIL(std::get<Error>(result.error()).msg);
  return std::move(result).value();
}
void write(const std::filesystem::path& path, const inspection::Bytes& bytes)
{
  auto file = std::ofstream{path, std::ios::binary};
  file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
  REQUIRE(file.good());
}
inspection::Bytes png(const QColor color)
{
  auto image = QImage{16, 16, QImage::Format_RGBA8888};
  image.fill(color);
  auto buffer = QBuffer{};
  REQUIRE(buffer.open(QIODevice::WriteOnly));
  REQUIRE(image.save(&buffer, "PNG"));
  const auto data = buffer.data();
  return {data.begin(), data.end()};
}
void append(inspection::Bytes& bytes, const std::uint32_t value)
{
  for (auto i = 0u; i < 4; ++i)
    bytes.push_back(std::uint8_t(value >> (i * 8)));
}
} // namespace

TEST_CASE("AssetInspection")
{
  SECTION("sky look directions agree with the editor face order")
  {
    const auto colors =
      std::array{Qt::red, Qt::green, Qt::blue, Qt::yellow, Qt::cyan, Qt::magenta};
    auto faces = std::array<QImage, 6>{};
    for (auto i = size_t{}; i < faces.size(); ++i)
    {
      faces[i] = QImage{16, 16, QImage::Format_RGB32};
      faces[i].fill(colors[i]);
    }
    CHECK(
      inspection::renderSkyView(faces, 0, 0, 100, 100).pixelColor(50, 50)
      == QColor{Qt::red});
    CHECK(
      inspection::renderSkyView(faces, 1.570796, 0, 100, 100).pixelColor(50, 50)
      == QColor{Qt::green});
    CHECK(
      inspection::renderSkyView(faces, 0, 1.570796, 100, 100).pixelColor(50, 50)
      == QColor{Qt::cyan});
    CHECK(inspection::renderSkyCross(faces, 32).size() == QSize{128, 96});
  }
  SECTION("WAD palette preserves red and blue and masked transparency")
  {
    auto image = img::Image{16, 16, std::vector<unsigned char>(16 * 16 * 4, 255)};
    image.alphaDomain = img::ImageAlphaDomain::Binary;
    for (auto i = size_t{}; i < image.pixels.size(); i += 4)
    {
      image.pixels[i] = 220;
      image.pixels[i + 1] = 40;
      image.pixels[i + 2] = 12;
    }
    image.pixels[3] = 0;
    const auto converted = require(img::convertGoldSrcTexture(image, "{test"));
    const auto decoded = require(inspection::decodeImage(converted.wad));
    CHECK(decoded.pixelColor(0, 0).alpha() == 0);
    const auto color = decoded.pixelColor(1, 0);
    CHECK(color.red() > 180);
    CHECK(color.blue() < 60);
    CHECK(color.alpha() == 255);
    CHECK(inspection::decodeImage({'W', 'A', 'D', '3'}).is_error());
    CHECK(inspection::decodeImage({1, 2, 3}).is_error());
  }
  SECTION("source snapshots and host selection are independent")
  {
    auto temporary = QTemporaryDir{};
    REQUIRE(temporary.isValid());
    const auto folder = pathFromQString(temporary.path());
    const auto file = folder / "red.png";
    write(file, png(Qt::red));
    auto library = require(project::AssetLibrary::create(folder, "Library"));
    REQUIRE(library->scan().is_success());
    auto entries = require(library->assets());
    REQUIRE(entries.size() == 1);
    REQUIRE(require(convertLibraryImage(*library, entries.front().id)));
    entries = require(library->assets());
    auto source = openLocalAssetLibrary(folder);
    REQUIRE(source);
    auto snapshot = require(inspection::readLibraryAsset(*source, entries.front()));
    auto first = AssetInspector{};
    auto second = AssetInspector{};
    first.setAsset(snapshot);
    second.setAsset(snapshot);
    first.findChild<QComboBox*>("AssetInspector_ContentKind")->setCurrentIndex(1);
    CHECK(
      second.findChild<QComboBox*>("AssetInspector_ContentKind")->currentIndex() == 0);
    write(file, png(Qt::blue));
    REQUIRE(library->scan().is_success());
    CHECK(
      require(inspection::decodeImage(snapshot.original)).pixelColor(0, 0)
      == QColor{Qt::red});
    first.clear();
    CHECK_FALSE(first.asset());
    REQUIRE(second.asset());
    CHECK(second.asset()->identity == snapshot.identity);
    second.setAsset(Error{"source unavailable"});
    CHECK_FALSE(second.asset());
    CHECK(
      second.findChild<QLabel*>("AssetInspector_Status")->text() == "source unavailable");
    CHECK(second.findChild<QMediaPlayer*>()->sourceDevice() == nullptr);
  }
  SECTION("WAVE chunks are bounded and sampler loop is optional")
  {
    auto bytes = inspection::Bytes{
      'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' '};
    append(bytes, 16);
    append(bytes, 0x00010001); // PCM mono
    append(bytes, 22050);
    append(bytes, 44100);
    append(bytes, 0x00100002); // 16 bit, block alignment 2
    bytes.insert(bytes.end(), {'d', 'a', 't', 'a'});
    append(bytes, 44100);
    bytes.resize(bytes.size() + 44100);
    const auto size = std::uint32_t(bytes.size() - 8);
    for (auto i = 0u; i < 4; ++i)
      bytes[4 + i] = std::uint8_t(size >> (i * 8));
    const auto info = require(inspection::readWaveInfo(bytes));
    CHECK(info.sampleRate == 22050);
    CHECK(info.bits == 16);
    CHECK(info.channels == 1);
    CHECK(info.seconds == 1.0);
    CHECK_FALSE(info.loop);
    bytes.pop_back();
    CHECK(inspection::readWaveInfo(bytes).is_error());
  }
}
} // namespace tb::ui
