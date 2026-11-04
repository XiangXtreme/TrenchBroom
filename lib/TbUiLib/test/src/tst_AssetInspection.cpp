/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFileDialog>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMediaPlayer>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalSpy>
#include <QSlider>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>

#include "img/GoldSrcTexture.h"
#include "project/AssetLibrary.h"
#include "project/GoldSrcWad.h"
#include "ui/AssetInspection.h"
#include "ui/AssetInspector.h"
#include "ui/AssetLibraryImport.h"
#include "ui/AssetLibraryItemDelegate.h"
#include "ui/AssetLibrarySource.h"
#include "ui/AssetLibraryWindow.h"
#include "ui/ModelThumbnail.h"
#include "ui/QPathUtils.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

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

/**
 * Minimal GoldSrc studio model: two bones, one body part with two variants and one
 * triangle per variant. Variant 1 skips the child bone, so a body switch moves geometry.
 */
inspection::Bytes studioModel()
{
  constexpr auto Header = std::size_t{244};
  constexpr auto BoneSize = std::size_t{112};
  constexpr auto TextureSize = std::size_t{80};
  constexpr auto BodyPartSize = std::size_t{76};
  constexpr auto ModelSize = std::size_t{112};
  constexpr auto MeshSize = std::size_t{20};
  constexpr auto VertexSize = std::size_t{8};
  // Each variant stores one triangle strip: a count, three 4-short vertices, a zero.
  constexpr auto StripSize = std::size_t{2} + 3 * VertexSize + 2;

  const auto boneOffset = Header;
  const auto textureOffset = boneOffset + 2 * BoneSize;
  const auto bodyPartOffset = textureOffset + TextureSize;
  const auto modelOffset = bodyPartOffset + BodyPartSize;
  const auto meshOffset = modelOffset + 2 * ModelSize;
  const auto triangleOffset = meshOffset + 2 * MeshSize;
  const auto vertexInfoOffset = triangleOffset + 2 * StripSize;
  const auto vertexOffset = vertexInfoOffset + 2 * 3;
  constexpr auto SequenceSize = std::size_t{176};
  const auto sequenceOffset = vertexOffset + 2 * 36;
  // A 64x64 texture whose left half is palette entry 0 and right half entry 1, so a
  // rendered triangle must show both colours.
  constexpr auto TextureWidth = std::size_t{64};
  constexpr auto TextureHeight = std::size_t{64};
  const auto textureDataOffset = sequenceOffset + 4 * SequenceSize;
  const auto skinOffset = textureDataOffset + TextureWidth * TextureHeight + 768;
  // One skin reference per family, three families in the fixture.
  constexpr auto SkinBytes = std::size_t{3 * 2};

  auto data = inspection::Bytes(skinOffset + SkinBytes, 0);
  const auto put32 = [&](const std::size_t offset, const std::int32_t value) {
    std::memcpy(data.data() + offset, &value, 4);
  };
  const auto putFloat = [&](const std::size_t offset, const float value) {
    std::memcpy(data.data() + offset, &value, 4);
  };
  const auto putText =
    [&](const std::size_t offset, const char* text, const std::size_t size) {
      auto length = std::size_t{0};
      while (length + 1 < size && text[length] != '\0')
        ++length;
      std::memcpy(data.data() + offset, text, length);
    };

  put32(0, 0x54534449); // "IDST"
  put32(4, 10);
  putText(8, "ui_studio", 64);
  putFloat(88, -8.0f);
  putFloat(92, -8.0f);
  putFloat(96, -8.0f);
  putFloat(100, 16.0f);
  putFloat(104, 8.0f);
  putFloat(108, 8.0f);
  put32(140, 2); // bones
  put32(144, std::int32_t(boneOffset));
  put32(164, 4); // sequences
  put32(168, std::int32_t(sequenceOffset));
  put32(172, 2); // external groups
  put32(180, 1); // textures
  put32(184, std::int32_t(textureOffset));
  put32(192, 1);
  put32(196, 3); // skin families
  put32(204, 1); // body parts
  put32(208, std::int32_t(bodyPartOffset));

  putText(boneOffset, "root", 32);
  put32(boneOffset + 32, -1);
  putText(boneOffset + BoneSize, "child", 32);
  put32(boneOffset + BoneSize + 32, 0);
  putFloat(boneOffset + BoneSize + 64, 10.0f);

  putText(textureOffset, "brick", 64);
  put32(textureOffset + 68, 64);
  put32(textureOffset + 72, 64);

  putText(bodyPartOffset, "body", 64);
  put32(bodyPartOffset + 64, 2);
  put32(bodyPartOffset + 68, 0);
  put32(bodyPartOffset + 72, std::int32_t(modelOffset));

  for (auto variant = 0; variant < 2; ++variant)
  {
    const auto model = modelOffset + std::size_t(variant) * ModelSize;
    putText(model, variant == 0 ? "body_a" : "body_b", 64);
    put32(model + 72, 1);
    put32(model + 76, std::int32_t(meshOffset + std::size_t(variant) * MeshSize));
    put32(model + 80, 3);
    put32(model + 84, std::int32_t(vertexInfoOffset + std::size_t(variant) * 3));
    put32(model + 88, std::int32_t(vertexOffset + std::size_t(variant) * 36));

    const auto mesh = meshOffset + std::size_t(variant) * MeshSize;
    put32(mesh, 1);
    const auto strip = triangleOffset + std::size_t(variant) * StripSize;
    put32(mesh + 4, std::int32_t(strip));
    const auto count = std::int16_t{3};
    std::memcpy(data.data() + strip, &count, sizeof(count));
    // Corners sample the left, right and centre of the texture.
    const auto coords = std::array<std::array<std::int16_t, 2>, 3>{
      std::array<std::int16_t, 2>{6, 32},
      std::array<std::int16_t, 2>{58, 32},
      std::array<std::int16_t, 2>{32, 32}};
    for (auto corner = std::size_t{0}; corner < 3; ++corner)
    {
      const auto vertex = std::array<std::int16_t, 4>{
        std::int16_t(corner), 0, coords[corner][0], coords[corner][1]};
      std::memcpy(
        data.data() + strip + 2 + corner * VertexSize, vertex.data(), sizeof(vertex));
    }
    const auto terminator = std::int16_t{0};
    std::memcpy(
      data.data() + strip + 2 + 3 * VertexSize, &terminator, sizeof(terminator));

    const auto spread = variant == 0 ? 12.0f : 6.0f;
    const auto points = std::array<std::array<float, 3>, 3>{
      std::array<float, 3>{variant == 0 ? 1.0f : 2.0f, 0.0f, 0.0f},
      std::array<float, 3>{spread, spread, 0.0f},
      std::array<float, 3>{-spread, spread, 0.0f}};
    for (auto vertex = std::size_t{0}; vertex < 3; ++vertex)
    {
      data[vertexInfoOffset + std::size_t(variant) * 3 + vertex] =
        std::uint8_t(variant == 0 ? 1 : 0);
      std::memcpy(
        data.data() + vertexOffset + std::size_t(variant) * 36 + vertex * 12,
        points[vertex].data(),
        12);
    }
  }

  // Four sequences without animation data: metadata is reported, the pose falls back to
  // the bone rest values.
  for (auto index = std::size_t{0}; index < 4; ++index)
  {
    const auto offset = sequenceOffset + index * SequenceSize;
    const auto label = "idle" + std::to_string(index);
    putText(offset, label.c_str(), 32);
    putFloat(offset + 32, 15.0f);
    put32(offset + 36, 1);
    put32(offset + 56, 2);
    put32(offset + 120, 1);
    put32(offset + 124, 0);
    put32(offset + 156, index % 2);
  }

  // Indexed pixels and their palette; skin family 0 points at this texture.
  for (auto y = std::size_t{0}; y < TextureHeight; ++y)
    for (auto x = std::size_t{0}; x < TextureWidth; ++x)
      data[textureDataOffset + y * TextureWidth + x] = x < TextureWidth / 2 ? 0 : 1;
  data[textureDataOffset + TextureWidth * TextureHeight + 0] = 255;
  data[textureDataOffset + TextureWidth * TextureHeight + 1] = 0;
  data[textureDataOffset + TextureWidth * TextureHeight + 2] = 0;
  data[textureDataOffset + TextureWidth * TextureHeight + 3] = 0;
  data[textureDataOffset + TextureWidth * TextureHeight + 4] = 255;
  data[textureDataOffset + TextureWidth * TextureHeight + 5] = 0;
  for (auto index = std::size_t{0}; index < SkinBytes; ++index)
    data[skinOffset + index] = 0;
  put32(textureOffset + 68, int32_t(TextureWidth));
  put32(textureOffset + 72, int32_t(TextureHeight));
  put32(textureOffset + 76, int32_t(textureDataOffset));
  put32(188, int32_t(textureDataOffset));
  put32(200, int32_t(skinOffset));

  return data;
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
    const auto archive = require(project::readGoldSrcWad(converted.wad));
    const auto& miptex = archive.textures.front().miptex;
    CHECK(require(inspection::decodeImage(miptex, "goldsrc-miptex")) == decoded);
    CHECK(require(inspection::decodeImage(converted.wad, "goldsrc-miptex")) == decoded);
    CHECK(
      inspection::decodeImage(inspection::Bytes{1, 2, 3}, "goldsrc-miptex").is_error());
    CHECK(inspection::decodeImage(
            inspection::Bytes{miptex.begin(), miptex.end() - 4}, "goldsrc-miptex")
            .is_error());
    // A texture name can start with the WAD signature without being an archive.
    auto named = miptex;
    std::copy_n("WAD3", 4, named.begin());
    CHECK(
      require(inspection::decodeImage(named, "goldsrc-miptex")).size() == decoded.size());
    CHECK(inspection::decodeImage(inspection::Bytes{'W', 'A', 'D', '3'}).is_error());
    CHECK(inspection::decodeImage(inspection::Bytes{1, 2, 3}).is_error());
  }
  SECTION("WAD members have library thumbnails and original and compatible previews")
  {
    auto temporary = QTemporaryDir{};
    REQUIRE(temporary.isValid());
    const auto folder = pathFromQString(temporary.path());
    auto image = img::Image{16, 16, std::vector<unsigned char>(16 * 16 * 4, 255)};
    for (auto i = size_t{}; i < image.pixels.size(); i += 4)
    {
      image.pixels[i + 1] = 0;
      image.pixels[i + 2] = 0;
    }
    const auto converted = require(img::convertGoldSrcTexture(image, "WALL01"));
    write(folder / "liquids.wad", converted.wad);
    auto library = require(project::AssetLibrary::create(folder, "Textures"));
    REQUIRE(library->scan().is_success());
    const auto entries = require(library->assets());
    REQUIRE(entries.size() == 1);
    REQUIRE(entries.front().nativeFormat == "goldsrc-miptex");
    auto source = openLocalAssetLibrary(folder);
    REQUIRE(source);
    auto snapshot = require(inspection::readLibraryAsset(*source, entries.front()));
    CHECK(snapshot.nativeFormat == "goldsrc-miptex");
    CHECK(
      require(inspection::decodeImage(snapshot.original, snapshot.nativeFormat))
        .pixelColor(0, 0)
      == QColor{Qt::red});
    auto inspector = AssetInspector{};
    inspector.setAsset(snapshot);
    auto* summary = inspector.findChild<QLabel*>("AssetInspector_Summary");
    REQUIRE(summary);
    CHECK(summary->text().contains("16 × 16"));
    inspector.findChild<QComboBox*>("AssetInspector_ContentKind")->setCurrentIndex(1);
    CHECK(summary->text().contains("16 × 16"));
    // Captured project members retain the raw original and a complete compatible WAD.
    snapshot.compatible = converted.wad;
    inspector.setAsset(snapshot);
    CHECK(summary->text().contains("16 × 16"));
    inspector.findChild<QComboBox*>("AssetInspector_ContentKind")->setCurrentIndex(1);
    CHECK(summary->text().contains("16 × 16"));
    auto browser = AssetLibraryWindow{temporary.path(), false};
    auto* grid = browser.findChild<QListWidget*>("AssetLibraryGrid");
    REQUIRE(grid);
    REQUIRE(grid->count() == 1);
    const auto thumbnail = grid->item(0)
                             ->data(AssetLibraryItemDelegate::ThumbnailRole)
                             .value<QPixmap>()
                             .toImage();
    REQUIRE_FALSE(thumbnail.isNull());
    CHECK(thumbnail.pixelColor(0, 0) == QColor{Qt::red});
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
  SECTION(
    "library cards load thumbnails on demand and retain them across metadata reloads")
  {
    auto temporary = QTemporaryDir{};
    REQUIRE(temporary.isValid());
    const auto folder = pathFromQString(temporary.path());
    const auto red = png(Qt::red);
    for (auto i = 0; i < 80; ++i)
      write(folder / ("color" + std::to_string(i) + ".png"), red);
    auto library = require(project::AssetLibrary::create(folder, "Library"));
    REQUIRE(library->scan().is_success());
    auto browser = AssetLibraryWindow{temporary.path(), false};
    auto* grid = browser.findChild<QListWidget*>("AssetLibraryGrid");
    auto* search = browser.findChild<QLineEdit*>("AssetLibrarySearch");
    REQUIRE(grid);
    REQUIRE(search);
    REQUIRE(grid->count() == 80);
    const auto thumbnail = [grid](const int row) {
      return grid->item(row)
        ->data(AssetLibraryItemDelegate::ThumbnailRole)
        .value<QPixmap>()
        .toImage();
    };
    CHECK(thumbnail(0).pixelColor(0, 0) == QColor{Qt::red});
    const auto entries = require(library->assets());
    // Update the store without a UI notification. An unrequested card must still read
    // its first image lazily, while the requested card retains its cached image.
    const auto blue = png(Qt::blue);
    for (const auto& entry : entries)
      write(folder / entry.sourceContainer, blue);
    REQUIRE(library->scan().is_success());
    search->setText("color");
    CHECK(thumbnail(0).pixelColor(0, 0) == QColor{Qt::red});
    CHECK(thumbnail(79).pixelColor(0, 0) == QColor{Qt::blue});

    auto source = openLocalAssetLibrary(folder);
    REQUIRE(source);
    REQUIRE(source->rescan().is_success());
    QTRY_COMPARE(thumbnail(0).pixelColor(0, 0), QColor{Qt::blue});
    // Metadata edits preserve content and keep the cached thumbnail valid.
    const auto id = entries.front().id;
    REQUIRE(source->setFavorite(id, true).is_success());
    QTest::qWait(10);
    CHECK(thumbnail(0).pixelColor(0, 0) == QColor{Qt::blue});
  }

  SECTION("batch conversion refreshes the library grid once")
  {
    auto temporary = QTemporaryDir{};
    REQUIRE(temporary.isValid());
    const auto folder = pathFromQString(temporary.path());
    for (const auto* name : {"one.png", "two.png", "three.png"})
      write(folder / name, png(Qt::red));
    auto library = require(project::AssetLibrary::create(folder, "Library"));
    REQUIRE(library->scan().is_success());
    auto browser = AssetLibraryWindow{temporary.path(), false};
    auto* grid = browser.findChild<QListWidget*>("AssetLibraryGrid");
    auto* convert = browser.findChild<QPushButton*>("AssetLibraryConvert");
    REQUIRE(grid);
    REQUIRE(convert);
    auto resets = QSignalSpy{grid->model(), &QAbstractItemModel::modelReset};
    convert->click();
    const auto converted = [&]() {
      const auto entries = require(library->assets());
      return std::ranges::all_of(
        entries, [](const auto& entry) { return entry.nativeFormat == "goldsrc-wad"; });
    };
    QTRY_VERIFY(converted());
    CHECK(resets.count() == 1);
    CHECK_FALSE(convert->isEnabled());
  }

  SECTION("switching libraries clears failed conversion retries")
  {
    auto first = QTemporaryDir{};
    auto second = QTemporaryDir{};
    REQUIRE(first.isValid());
    REQUIRE(second.isValid());
    const auto folder = pathFromQString(first.path());
    for (auto i = 0; i < 20; ++i)
      write(folder / ("broken" + std::to_string(i) + ".png"), {1, 2, 3});
    auto library = require(project::AssetLibrary::create(folder, "First"));
    REQUIRE(library->scan().is_success());
    auto other =
      require(project::AssetLibrary::create(pathFromQString(second.path()), "Second"));
    auto browser = AssetLibraryWindow{first.path(), false};
    auto* convert = browser.findChild<QPushButton*>("AssetLibraryConvert");
    auto* retry = browser.findChild<QPushButton*>("AssetLibraryRetryFailed");
    REQUIRE(convert);
    REQUIRE(retry);
    auto closeReport = QTimer{};
    QObject::connect(&closeReport, &QTimer::timeout, &browser, [&]() {
      for (auto* dialog : browser.findChildren<QDialog*>())
        if (dialog->windowTitle() == "Asset operation results")
          dialog->reject();
    });
    closeReport.start(10);
    convert->click();
    QTRY_VERIFY(!retry->isHidden());
    closeReport.stop();

    QTimer::singleShot(0, &browser, [&]() {
      auto* dialog = browser.findChild<QFileDialog*>();
      REQUIRE(dialog);
      dialog->setDirectory(second.path());
      QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
    });
    auto* change = static_cast<QPushButton*>(nullptr);
    for (auto* button : browser.findChildren<QPushButton*>())
      if (button->text() == "Change Library…")
        change = button;
    REQUIRE(change);
    retry->click();
    auto* timer = browser.findChild<QTimer*>("AssetLibraryConvertTimer");
    REQUIRE(timer);
    REQUIRE(timer->isActive());
    change->click();
    CHECK_FALSE(timer->isActive());
    CHECK(browser.libraryId() == other->info().id);
    CHECK(retry->isHidden());
    CHECK_FALSE(convert->isEnabled());
  }

  SECTION("library model inspection resolves dependency-backed siblings")
  {
    auto temporary = QTemporaryDir{};
    REQUIRE(temporary.isValid());
    const auto folder = pathFromQString(temporary.path());
    auto library = require(project::AssetLibrary::create(folder, "Library"));
    std::filesystem::create_directories(folder / "models");
    const auto bytes = studioModel();
    write(folder / "models/test.mdl", bytes);
    write(folder / "models/testT.mdl", bytes);
    REQUIRE(library->scan().is_success());

    const auto entries = require(library->assets());
    const auto entry = std::ranges::find_if(entries, [](const auto& candidate) {
      return candidate.sourceContainer == "models/test.mdl";
    });
    REQUIRE(entry != entries.end());

    const auto source = openLocalAssetLibrary(folder);
    REQUIRE(source);
    const auto asset = require(inspection::readLibraryAsset(*source, *entry));
    CHECK_FALSE(asset.studioExternal.textureHeader.empty());
  }

  SECTION("skybox faces stay hidden behind their logical group")
  {
    auto temporary = QTemporaryDir{};
    REQUIRE(temporary.isValid());
    const auto folder = pathFromQString(temporary.path());
    auto library = require(project::AssetLibrary::create(folder, "Library"));
    std::filesystem::create_directories(folder / "env");
    for (const auto face : {"bk", "rt", "lf", "ft", "up", "dn"})
      write(folder / "env" / (std::string{"desert"} + face + ".png"), png(Qt::blue));
    REQUIRE(library->scan().is_success());

    const auto source = openLocalAssetLibrary(folder);
    REQUIRE(source);
    const auto entries = require(source->assets());
    REQUIRE(entries.size() == 1);
    CHECK(entries.front().type == project::AssetType::Skybox);
    CHECK(require(source->allAssets()).size() == 7);

    const auto sky = require(inspection::readLibraryAsset(*source, entries.front()));
    CHECK(sky.resource.skybox);
    for (const auto& face : sky.skyOriginal)
      CHECK_FALSE(face.isNull());
    const auto keys = require(assetPreviewKeys(*source));
    const auto thumbnail = inspection::skyThumbnail(sky);
    REQUIRE_FALSE(thumbnail.isNull());
    CHECK(thumbnail.pixelColor(128, 128) == QColor{Qt::blue});
    REQUIRE(source->setFavorite(entries.front().id, true).is_success());
    CHECK(require(assetPreviewKeys(*source)) == keys);

    auto window = AssetLibraryWindow{pathAsQString(folder), false};
    window.resize(900, 600);
    window.show();
    QTest::qWait(200);
    const auto* grid = window.findChild<QListView*>("AssetLibraryGrid");
    REQUIRE(grid != nullptr);
    REQUIRE(grid->model()->rowCount() == 1);
    const auto pixmap = qvariant_cast<QPixmap>(grid->model()->data(
      grid->model()->index(0, 0), AssetLibraryItemDelegate::ThumbnailRole));
    CHECK_FALSE(pixmap.isNull());
    CHECK(pixmap.toImage() == thumbnail);
    // Faces change independently of the manifest revision in both browser hosts.
    for (const auto face : {"bk", "rt", "lf", "ft", "up", "dn"})
      write(folder / "env" / (std::string{"desert"} + face + ".png"), png(Qt::red));
    REQUIRE(source->rescan().is_success());
    CHECK(require(source->assets()).front().revision == entries.front().revision);
    CHECK(
      require(assetPreviewKeys(*source)).at(entries.front().id)
      != keys.at(entries.front().id));
    QTRY_COMPARE(
      grid->model()
        ->data(grid->model()->index(0, 0), AssetLibraryItemDelegate::ThumbnailRole)
        .value<QPixmap>()
        .toImage()
        .pixelColor(128, 128),
      QColor{Qt::red});
  }

  SECTION("GoldSrc studio models preview the bind pose and body variants")
  {
    auto bytes = studioModel();
    auto sequenceOffset = std::uint32_t{};
    std::memcpy(&sequenceOffset, bytes.data() + 168, sizeof(sequenceOffset));
    const auto twoBlends = int32_t{2};
    std::memcpy(bytes.data() + sequenceOffset + 120, &twoBlends, sizeof(twoBlends));
    const auto scene = loadGoldSrcStudioPreviewScene(bytes);
    REQUIRE(scene);
    REQUIRE(scene->studioModel);
    CHECK(scene->studioModel->bones == 2);
    CHECK(scene->studioModel->sequences == 4);
    CHECK(scene->studioModel->sequenceGroups == 2);
    CHECK(scene->studioModel->skinFamilies == 3);
    CHECK(scene->studioModel->textures == std::vector<std::string>{"brick"});
    REQUIRE(scene->frames.size() == 1);
    CHECK(scene->frames.front().triangles.size() == 1);
    CHECK(scene->frames.front().positions.front() == vm::vec3f{11.0f, 0.0f, 0.0f});
    CHECK_FALSE(loadGoldSrcStudioPreviewScene({1, 2, 3}));

    const auto image = renderModelPreview(scene->frames.front(), 64, 0.55f, 0.38f);
    REQUIRE_FALSE(image.isNull());
    auto painted = 0;
    for (auto y = 0; y < image.height(); ++y)
      for (auto x = 0; x < image.width(); ++x)
        painted += image.pixelColor(x, y).alpha() > 0 ? 1 : 0;
    CHECK(painted > 0);

    // The fixture texture is red on the left half and green on the right half, so a
    // correctly sampled triangle shows both.
    auto red = 0;
    auto green = 0;
    for (auto y = 0; y < image.height(); ++y)
      for (auto x = 0; x < image.width(); ++x)
      {
        const auto color = image.pixelColor(x, y);
        red += color.red() > color.green() + 20 ? 1 : 0;
        green += color.green() > color.red() + 20 ? 1 : 0;
      }
    CHECK(red > 0);
    CHECK(green > 0);

    const auto geometry = loadGoldSrcStudioFrame(bytes, 0, 1, {}, false);
    REQUIRE(geometry);
    CHECK(geometry->texCoords == scene->frames.front().texCoords);
    REQUIRE(geometry->textures.size() == 1);
    CHECK(geometry->textures.front().pixels.empty());

    auto edge = scene->frames.front();
    std::fill(edge.texCoords.begin(), edge.texCoords.end(), std::array{0.0f, 0.0f});
    const auto edgeImage = renderModelPreview(edge, 64);
    for (auto y = 0; y < edgeImage.height(); ++y)
      for (auto x = 0; x < edgeImage.width(); ++x)
        if (const auto color = edgeImage.pixelColor(x, y); color.alpha() > 0)
          CHECK(color.green() == 0);

    auto asset = inspection::Asset{};
    asset.resource.type = BrowserCellType::Model;
    asset.resource.path = "models/ui_studio.mdl";
    asset.name = "ui_studio";
    asset.original = bytes;
    asset.compatible = bytes;
    auto inspector = AssetInspector{};
    inspector.setAsset(asset);
    auto* body = inspector.findChild<QComboBox*>("AssetInspector_ModelBody");
    auto* details = inspector.findChild<QTreeWidget*>("AssetInspector_Details");
    auto* summary = inspector.findChild<QLabel*>("AssetInspector_Summary");
    REQUIRE(body != nullptr);
    REQUIRE(details != nullptr);
    REQUIRE(summary != nullptr);
    CHECK_FALSE(body->isHidden());
    CHECK(body->count() == 2);
    CHECK(body->itemText(0).contains("body_a"));

    auto* sequence = inspector.findChild<QComboBox*>("AssetInspector_ModelSequence");
    auto* skin = inspector.findChild<QComboBox*>("AssetInspector_ModelSkin");
    auto* blend = inspector.findChild<QComboBox*>("AssetInspector_ModelBlend");
    auto* frame = inspector.findChild<QSlider*>("AssetInspector_ModelFrame");
    auto* play = inspector.findChild<QPushButton*>("AssetInspector_ModelPlay");
    REQUIRE(sequence != nullptr);
    REQUIRE(skin != nullptr);
    REQUIRE(blend != nullptr);
    REQUIRE(frame != nullptr);
    REQUIRE(play != nullptr);
    inspector.show();
    CHECK_FALSE(skin->isHidden());
    CHECK(skin->count() == 3);
    skin->setCurrentIndex(1);
    CHECK(skin->currentIndex() == 1);
    CHECK_FALSE(blend->isHidden());
    CHECK(blend->count() == 2);
    blend->setCurrentIndex(1);
    CHECK(blend->currentIndex() == 1);
    CHECK_FALSE(sequence->isHidden());
    CHECK(sequence->count() == 4);
    CHECK(frame->maximum() == 1);
    CHECK(frame->value() == 0);
    play->setChecked(true);
    CHECK(play->isChecked());
    QTRY_VERIFY(frame->value() == 1);
    play->setChecked(false);
    CHECK(frame->value() == 1);
    const auto valueFor = [details](const QString& key) {
      const auto items = details->findItems(key, Qt::MatchExactly, 0);
      return items.empty() ? QString{} : items.front()->text(1);
    };
    CHECK(valueFor("Bones") == "2");
    CHECK(valueFor("Skin families") == "3");
    CHECK(valueFor("Textures") == "brick");
    CHECK(valueFor("Sequences") == "4 in 2 external groups (0 loaded)");
    CHECK(summary->text().contains("idle0 frame 1"));
    body->setCurrentIndex(1);
    CHECK(summary->text().contains("idle0 frame 1"));
    play->setChecked(true);
    sequence->setCurrentIndex(1);
    CHECK_FALSE(play->isChecked());

    // A short, narrow details panel scrolls its content instead of letting the layout
    // run out of height, which used to paint the preview over the controls.
    inspector.setFixedSize(420, 320);
    QApplication::processEvents();
    auto* scroll = inspector.findChild<QScrollArea*>("AssetInspector_Scroll");
    auto* preview = inspector.findChild<QWidget*>("AssetInspector_Image");
    REQUIRE(scroll != nullptr);
    REQUIRE(scroll->widget() != nullptr);
    REQUIRE(preview != nullptr);
    const auto contentY = [content = scroll->widget()](QWidget* widget) {
      return widget->mapTo(content, QPoint{0, 0}).y();
    };
    CHECK(contentY(sequence) >= contentY(preview) + preview->height());
    CHECK(scroll->widget()->height() > scroll->viewport()->height());

    // Hiding, clearing and selecting another resource must stop the studio timer.
    play->setChecked(true);
    inspector.hide();
    CHECK_FALSE(play->isChecked());
    inspector.show();
    play->setChecked(true);
    inspector.clear();
    CHECK_FALSE(play->isChecked());
    for (auto* timer : inspector.findChildren<QTimer*>())
      CHECK_FALSE(timer->isActive());
    // A stale control event after clearing must not dereference missing metadata.
    play->setChecked(true);
    CHECK_FALSE(play->isChecked());
  }
  SECTION("masked studio textures are alpha tested in the preview")
  {
    auto frame = ModelPreviewFrame{};
    auto texture = mdl::GoldSrcStudioTexture{};
    texture.width = 2;
    texture.height = 1;
    texture.flags = 0x40;
    // Left texel is transparent palette entry 255, right texel is opaque red.
    texture.pixels = {0x00000000u, 0xFFFF0000u};
    frame.textures.push_back(texture);
    frame.positions = {
      vm::vec3f{-1.0f, -1.0f, 0.0f},
      vm::vec3f{1.0f, -1.0f, 0.0f},
      vm::vec3f{0.0f, 1.0f, 0.0f}};
    frame.texCoords = {
      std::array{0.0f, 0.0f}, std::array{1.0f, 0.0f}, std::array{0.5f, 1.0f}};
    frame.triangles = {{0, 1, 2}};
    frame.triangleTextures = {0};

    const auto image = renderModelPreview(frame, 64);
    auto transparent = 0;
    auto opaque = 0;
    for (auto y = 0; y < image.height(); ++y)
      for (auto x = 0; x < image.width(); ++x)
      {
        const auto alpha = image.pixelColor(x, y).alpha();
        transparent += alpha == 0 ? 1 : 0;
        opaque += alpha == 255 ? 1 : 0;
      }
    CHECK(transparent > 0);
    CHECK(opaque > 0);
  }

  SECTION("chrome and additive texture flags render")
  {
    auto frame = ModelPreviewFrame{};
    auto texture = mdl::GoldSrcStudioTexture{};
    texture.width = 1;
    texture.height = 1;
    texture.flags = 0x0008;
    texture.pixels = {0xFF808080u};
    frame.textures.push_back(texture);
    frame.positions = {
      vm::vec3f{-1.0f, -1.0f, 0.0f},
      vm::vec3f{1.0f, -1.0f, 0.0f},
      vm::vec3f{0.0f, 1.0f, 0.0f}};
    frame.texCoords = {
      std::array{0.0f, 0.0f}, std::array{1.0f, 0.0f}, std::array{0.5f, 1.0f}};
    frame.triangles = {{0, 1, 2}};
    frame.triangleTextures = {0};

    const auto chrome = renderModelPreview(frame, 64);
    auto chromePainted = 0;
    for (auto y = 0; y < chrome.height(); ++y)
      for (auto x = 0; x < chrome.width(); ++x)
        chromePainted += chrome.pixelColor(x, y).alpha() > 0 ? 1 : 0;
    CHECK(chromePainted > 0);

    frame.textures.front().flags = 0x0020;
    const auto additive = renderModelPreview(frame, 64);
    auto additivePainted = 0;
    for (auto y = 0; y < additive.height(); ++y)
      for (auto x = 0; x < additive.width(); ++x)
        if (const auto color = additive.pixelColor(x, y); color.alpha() > 0)
        {
          ++additivePainted;
          CHECK(color.alpha() == 255);
        }
    CHECK(additivePainted > 0);
  }

  SECTION("model thumbnails resolve a sibling texture header")
  {
    auto main = studioModel();
    const auto zero = int32_t{0};
    std::memcpy(main.data() + 180, &zero, sizeof(zero)); // numtextures
    std::memcpy(main.data() + 184, &zero, sizeof(zero)); // textureindex
    std::memcpy(main.data() + 192, &zero, sizeof(zero)); // numskinref
    std::memcpy(main.data() + 196, &zero, sizeof(zero)); // numskinfamilies
    std::memcpy(main.data() + 200, &zero, sizeof(zero)); // skinindex

    auto textures = studioModel();
    std::memcpy(textures.data() + 140, &zero, sizeof(zero)); // numbones
    std::memcpy(textures.data() + 144, &zero, sizeof(zero)); // boneindex
    std::memcpy(textures.data() + 164, &zero, sizeof(zero)); // numseq
    std::memcpy(textures.data() + 168, &zero, sizeof(zero)); // seqindex
    std::memcpy(textures.data() + 172, &zero, sizeof(zero)); // numseqgroups
    std::memcpy(textures.data() + 176, &zero, sizeof(zero)); // seqgroupindex
    std::memcpy(textures.data() + 204, &zero, sizeof(zero)); // numbodyparts
    std::memcpy(textures.data() + 208, &zero, sizeof(zero)); // bodypartindex

    auto directory = QTemporaryDir{};
    REQUIRE(directory.isValid());
    const auto write =
      [](const std::filesystem::path& path, const inspection::Bytes& bytes) {
        auto stream = std::ofstream{path, std::ios::binary};
        stream.write(
          reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
      };
    write(directory.filePath("ui_studio.mdl").toStdString(), main);
    write(directory.filePath("ui_studioT.mdl").toStdString(), textures);

    const auto scene = loadModelPreviewScene(
      std::filesystem::path{directory.path().toStdString()}, "ui_studio.mdl");
    REQUIRE(scene);
    REQUIRE(scene->studioModel);
    CHECK(scene->studioModel->externalTextures);
    CHECK(scene->studioModel->textures == std::vector<std::string>{"brick"});
    REQUIRE_FALSE(scene->frames.empty());
    REQUIRE_FALSE(scene->frames.front().textures.empty());
    CHECK_FALSE(scene->frames.front().textures.front().pixels.empty());

    const auto image = renderModelThumbnail(
      std::filesystem::path{directory.path().toStdString()}, "ui_studio.mdl", 64);
    REQUIRE_FALSE(image.isNull());
    auto painted = 0;
    for (auto y = 0; y < image.height(); ++y)
      for (auto x = 0; x < image.width(); ++x)
        painted += image.pixelColor(x, y).alpha() > 0 ? 1 : 0;
    CHECK(painted > 0);
  }

  SECTION("non-looping studio sequences stop on their final frame")
  {
    auto bytes = studioModel();
    auto sequenceOffset = std::uint32_t{};
    std::memcpy(&sequenceOffset, bytes.data() + 168, sizeof(sequenceOffset));
    std::fill(
      bytes.begin() + sequenceOffset + 36,
      bytes.begin() + sequenceOffset + 40,
      std::uint8_t{0});
    auto asset = inspection::Asset{};
    asset.resource.type = BrowserCellType::Model;
    asset.original = asset.compatible = bytes;
    auto inspector = AssetInspector{};
    inspector.setAsset(asset);
    inspector.show();
    auto* play = inspector.findChild<QPushButton*>("AssetInspector_ModelPlay");
    auto* frame = inspector.findChild<QSlider*>("AssetInspector_ModelFrame");
    REQUIRE(play);
    REQUIRE(frame);
    play->setChecked(true);
    QTRY_COMPARE(frame->value(), 1);
    QTRY_VERIFY(!play->isChecked());
    CHECK(frame->value() == 1);
  }
  SECTION("studio metadata without a readable mesh keeps preview controls safe")
  {
    auto bytes = studioModel();
    // A valid metadata header with no body parts cannot supply a preview frame.
    std::fill(bytes.begin() + 204, bytes.begin() + 208, std::uint8_t{0});
    const auto scene = loadGoldSrcStudioPreviewScene(bytes);
    REQUIRE(scene);
    REQUIRE(scene->frames.empty());
    auto asset = inspection::Asset{};
    asset.resource.type = BrowserCellType::Model;
    asset.compatible = bytes;
    asset.original = bytes;
    auto inspector = AssetInspector{};
    inspector.setAsset(asset);
    for (auto* control : inspector.findChildren<QCheckBox*>())
      control->setChecked(true);
    CHECK(
      inspector.findChild<QLabel*>("AssetInspector_Summary")->text()
      == "The studio model has no readable bind pose.");
  }
  SECTION("sky preview separates source and published faces")
  {
    auto asset = inspection::Asset{};
    asset.resource.type = BrowserCellType::Texture;
    asset.resource.skybox = true;
    asset.name = "night";
    asset.resource.path = "gfx/env/night";
    for (auto i = size_t{}; i < 6; ++i)
    {
      asset.skyOriginal[i] = QImage{8, 8, QImage::Format_RGB32};
      asset.skyOriginal[i].fill(Qt::blue);
      asset.skyCompatible[i] = QImage{4, 4, QImage::Format_RGB32};
      asset.skyCompatible[i].fill(Qt::green);
    }
    auto inspector = AssetInspector{};
    inspector.setAsset(asset);
    auto* summary = inspector.findChild<QLabel*>("AssetInspector_Summary");
    auto* mode = inspector.findChild<QComboBox*>("AssetInspector_ContentKind");
    REQUIRE(summary != nullptr);
    REQUIRE(mode != nullptr);
    CHECK(summary->text().contains("Published"));
    CHECK(summary->text().contains("source 8 × 8"));
    CHECK(summary->text().contains("published 4 × 4"));
    mode->setCurrentIndex(1);
    CHECK(summary->text().contains("Source"));
  }
  SECTION("real GoldSrc studio models render their bind pose")
  {
    // Opt-in like the TbMdlLib test: the repository ships no game assets.
    const auto path = std::getenv("TB_GOLDSRC_MODEL");
    if (!path || !*path)
      SKIP("TB_GOLDSRC_MODEL is not set");
    auto file = std::ifstream{std::filesystem::path{path}, std::ios::binary};
    REQUIRE(file);
    const auto bytes = inspection::Bytes{
      std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    const auto scene = loadGoldSrcStudioPreviewScene(bytes);
    REQUIRE(scene);
    REQUIRE(scene->studioModel);
    CHECK(scene->studioModel->bones > 0);
    CHECK_FALSE(scene->studioModel->sequenceList.empty());

    // When the retail model splits textures or animation into sibling files, resolve them
    // from the model path exactly as the model browser does.
    auto externalFiles = GoldSrcStudioExternalFiles{};
    const auto external =
      loadGoldSrcStudioExternalData(bytes, std::filesystem::path{path}, externalFiles);
    auto previewScene = scene;
    if (!externalFiles.textureHeader.empty())
    {
      auto textured = loadGoldSrcStudioPreviewScene(bytes, external);
      REQUIRE(textured);
      CHECK_FALSE(textured->studioModel->textures.empty());
      REQUIRE_FALSE(textured->frames.empty());
      REQUIRE_FALSE(textured->frames.front().textures.empty());
      CHECK_FALSE(textured->frames.front().textures.front().pixels.empty());
      previewScene = std::move(textured);
    }
    for (auto index = std::size_t{0}; index < scene->studioModel->sequenceList.size();
         ++index)
    {
      const auto& sequence = scene->studioModel->sequenceList[index];
      if (
        sequence.group == 0 || !external.sequenceGroups
        || sequence.group > external.sequenceGroups->size()
        || (*external.sequenceGroups)[sequence.group - 1].empty())
        continue;
      const auto frame = loadGoldSrcStudioFrame(bytes, index, 0, {}, true, external);
      CHECK(frame);
      break;
    }
    if (
      !scene->studioModel->sequenceList.empty()
      && scene->studioModel->sequenceList.front().frameCount > 1
      && !scene->frames.empty())
    {
      const auto next = loadGoldSrcStudioFrame(bytes, 0, 1);
      REQUIRE(next);
      // Static sequences legitimately repeat the same pose; only the shape contract is
      // universal across arbitrary retail models.
      CHECK(next->positions.size() == scene->frames.front().positions.size());
      CHECK(next->texCoords.size() == next->positions.size());
    }
    REQUIRE_FALSE(scene->frames.empty());
    REQUIRE_FALSE(previewScene->frames.empty());
    const auto image =
      renderModelPreview(previewScene->frames.front(), 256, 0.55f, 0.38f);
    REQUIRE_FALSE(image.isNull());
    auto painted = 0;
    for (auto y = 0; y < image.height(); ++y)
      for (auto x = 0; x < image.width(); ++x)
        painted += image.pixelColor(x, y).alpha() > 0 ? 1 : 0;
    CHECK(painted > 256);
    const auto out = std::getenv("TB_GOLDSRC_PREVIEW_OUT");
    if (out && *out)
      CHECK(image.save(QString::fromUtf8(out)));

    // The inspector must expose the authored sequences and a frame control.
    auto asset = inspection::Asset{};
    asset.resource.type = BrowserCellType::Model;
    asset.resource.path = "models/player/gordon.mdl";
    asset.name = "gordon";
    asset.original = bytes;
    asset.compatible = bytes;
    auto inspector = AssetInspector{};
    inspector.resize(420, 620);
    inspector.setAsset(asset);
    auto* sequence = inspector.findChild<QComboBox*>("AssetInspector_ModelSequence");
    auto* frame = inspector.findChild<QSlider*>("AssetInspector_ModelFrame");
    auto* play = inspector.findChild<QPushButton*>("AssetInspector_ModelPlay");
    REQUIRE(sequence);
    REQUIRE(frame);
    REQUIRE(play);
    CHECK_FALSE(sequence->isHidden());
    CHECK(sequence->count() == int(scene->studioModel->sequenceList.size()));
    CHECK(frame->maximum() > 0);
    if (out && *out)
    {
      inspector.show();
      QTest::qWait(120);
      CHECK(inspector.grab().save(QString::fromUtf8(out) + ".panel.png"));
      inspector.hide();
    }
  }
  SECTION("prefabs report authored volumes and references")
  {
    const auto text = std::string{
      "{\n"
      "\"classname\" \"worldspawn\"\n"
      "\"skyname\" \"desert\"\n"
      "{\n"
      "( 0 0 0 ) ( 0 1 0 ) ( 0 0 1 ) WALL01 0 0 0 1 1\n"
      "( 0 0 0 ) ( 0 0 1 ) ( 1 0 0 ) WALL01 0 0 0 1 1\n"
      "( 0 0 0 ) ( 1 0 0 ) ( 0 1 0 ) WALL01 0 0 0 1 1\n"
      "( 128 128 64 ) ( 128 129 64 ) ( 129 128 64 ) WALL01 0 0 0 1 1\n"
      "( 128 128 64 ) ( 129 128 64 ) ( 128 128 65 ) WALL01 0 0 0 1 1\n"
      "( 128 128 64 ) ( 128 128 65 ) ( 128 129 64 ) WALL01 0 0 0 1 1\n"
      "}\n"
      "}\n"
      "{\n"
      "\"classname\" \"cycler\"\n"
      "\"model\" \"models/barrel.mdl\"\n"
      "}\n"};
    auto asset = inspection::Asset{};
    asset.resource.type = BrowserCellType::Prefab;
    asset.resource.path = "prefabs/crate.tbprefab";
    asset.name = "crate";
    asset.original.assign(text.begin(), text.end());
    asset.compatible = asset.original;
    asset.prefab = require(readPrefabContents(text));
    asset.prefabIssues << "Open the prefab from a project to verify references.";

    auto inspector = AssetInspector{};
    inspector.setAsset(asset);
    auto* summary = inspector.findChild<QLabel*>("AssetInspector_Summary");
    auto* details = inspector.findChild<QTreeWidget*>("AssetInspector_Details");
    REQUIRE(summary != nullptr);
    REQUIRE(details != nullptr);
    CHECK(summary->text().contains("1 brushes"));
    CHECK(summary->text().contains("128.0 × 128.0 × 64.0"));
    CHECK(summary->text().contains("Open the prefab from a project"));
    const auto valueFor = [details](const QString& key) {
      const auto items = details->findItems(key, Qt::MatchExactly, 0);
      return items.empty() ? QString{} : items.front()->text(1);
    };
    CHECK(valueFor("Brushes") == "1");
    CHECK(valueFor("Entities") == "1");
    CHECK(valueFor("Faces") == "6");
    CHECK(valueFor("Materials") == "WALL01");
    CHECK(valueFor("Models and sprites") == "models/barrel.mdl");
    CHECK(valueFor("Sky") == "desert");
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
