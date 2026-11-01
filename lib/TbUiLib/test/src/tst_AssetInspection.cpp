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
#include <QTreeWidget>

#include "img/GoldSrcTexture.h"
#include "project/AssetLibrary.h"
#include "ui/AssetInspection.h"
#include "ui/AssetInspector.h"
#include "ui/AssetLibraryImport.h"
#include "ui/AssetLibrarySource.h"
#include "ui/ModelThumbnail.h"
#include "ui/QPathUtils.h"

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
  constexpr auto TriangleSize = std::size_t{8};

  const auto boneOffset = Header;
  const auto textureOffset = boneOffset + 2 * BoneSize;
  const auto bodyPartOffset = textureOffset + TextureSize;
  const auto modelOffset = bodyPartOffset + BodyPartSize;
  const auto meshOffset = modelOffset + 2 * ModelSize;
  const auto triangleOffset = meshOffset + 2 * MeshSize;
  const auto vertexInfoOffset = triangleOffset + 2 * TriangleSize;
  const auto vertexOffset = vertexInfoOffset + 2 * 3;

  auto data = inspection::Bytes(vertexOffset + 2 * 36, 0);
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
    put32(mesh + 4, std::int32_t(triangleOffset + std::size_t(variant) * TriangleSize));
    const auto triangle = std::array<std::int16_t, 4>{0, 1, 2, 0};
    std::memcpy(
      data.data() + triangleOffset + std::size_t(variant) * TriangleSize,
      triangle.data(),
      sizeof(triangle));

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
  SECTION("GoldSrc studio models preview the bind pose and body variants")
  {
    const auto bytes = studioModel();
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

    const auto image =
      renderModelPreview(scene->frames.front(), 64, 0.55f, 0.38f);
    REQUIRE_FALSE(image.isNull());
    auto painted = 0;
    for (auto y = 0; y < image.height(); ++y)
      for (auto x = 0; x < image.width(); ++x)
        painted += image.pixelColor(x, y).alpha() > 0 ? 1 : 0;
    CHECK(painted > 0);

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
    const auto valueFor = [details](const QString& key) {
      const auto items = details->findItems(key, Qt::MatchExactly, 0);
      return items.empty() ? QString{} : items.front()->text(1);
    };
    CHECK(valueFor("Bones") == "2");
    CHECK(valueFor("Skin families") == "3");
    CHECK(valueFor("Embedded textures") == "brick");
    CHECK(valueFor("Sequences") == "4 in 2 external groups");
    CHECK(summary->text().contains("bind pose"));
    body->setCurrentIndex(1);
    CHECK(summary->text().contains("bind pose"));
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
    // Models whose render pose comes from a sequence have no decoded bind pose yet; they
    // must report metadata without inventing geometry.
    if (scene->frames.empty())
      return;
    const auto image = renderModelPreview(scene->frames.front(), 256, 0.55f, 0.38f);
    REQUIRE_FALSE(image.isNull());
    auto painted = 0;
    for (auto y = 0; y < image.height(); ++y)
      for (auto x = 0; x < image.width(); ++x)
        painted += image.pixelColor(x, y).alpha() > 0 ? 1 : 0;
    CHECK(painted > 256);
    if (const auto out = std::getenv("TB_GOLDSRC_PREVIEW_OUT"); out && *out)
      CHECK(image.save(QString::fromUtf8(out)));
  }
  SECTION("prefabs report authored volumes and references")
  {
    const auto text = std::string{
      "{\n"
      "\"classname\" \"worldspawn\"\n"
      "\"skyname\" \"desert\"\n"
      "{\n"
      "( 0 0 0 ) ( 128 0 0 ) ( 0 128 0 ) WALL01 0 0 0 1 1\n"
      "( 0 0 64 ) ( 128 0 64 ) ( 0 128 64 ) WALL01 0 0 0 1 1\n"
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
    CHECK(valueFor("Faces") == "2");
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
