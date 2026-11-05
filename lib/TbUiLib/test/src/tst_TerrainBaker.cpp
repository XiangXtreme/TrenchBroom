/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QColor>
#include <QFile>
#include <QImage>
#include <QPointF>
#include <QTemporaryDir>

#include "img/GoldSrcTexture.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushNode.h"
#include "mdl/EditorContext.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/Map_CopyPaste.h"
#include "mdl/Map_Groups.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/NodeContents.h"
#include "mdl/PasteType.h"
#include "project/GoldSrcWad.h"
#include "project/ProjectStore.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"
#include "ui/ProjectSession.h"
#include "ui/QPathUtils.h"
#include "ui/TerrainBaker.h"
#include "ui/TerrainExport.h"
#include "ui/TerrainMaterial.h"
#include "ui/TerrainRegion.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{
template <typename T>
T requireValue(Result<T> result)
{
  if (result.is_error())
    FAIL(std::get<Error>(result.error()).msg);
  return std::move(result).value();
}

void requireValue(Result<void> result)
{
  if (result.is_error())
    FAIL(std::get<Error>(result.error()).msg);
}

std::vector<std::uint8_t> readBytes(const std::filesystem::path& path)
{
  auto file = QFile{pathAsQString(path)};
  REQUIRE(file.open(QIODevice::ReadOnly));
  const auto bytes = file.readAll();
  REQUIRE(file.error() == QFileDevice::NoError);
  return {bytes.begin(), bytes.end()};
}

std::uint32_t readU32(const std::vector<std::uint8_t>& bytes, const size_t offset)
{
  return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8)
         | (std::uint32_t(bytes[offset + 2]) << 16)
         | (std::uint32_t(bytes[offset + 3]) << 24);
}

std::uint8_t sampleIndex(
  const TerrainBake& bake,
  const BakeFace& face,
  const double x,
  const double y,
  const int mip)
{
  const auto& texture = bake.textures[face.material];
  const auto size = 1 << mip;
  const auto u = int(std::floor((face.u.x() * x + face.u.y() * y + face.u.z()) / size));
  const auto v = int(std::floor((face.v.x() * x + face.v.y() * y + face.v.z()) / size));
  const auto width = int(texture.width) >> mip;
  const auto height = int(texture.height) >> mip;
  const auto wrappedU = (u % width + width) % width;
  const auto wrappedV = (v % height + height) % height;
  const auto offset = size_t(readU32(texture.miptex, size_t(24 + mip * 4)));
  return texture.miptex[offset + size_t(wrappedV * width + wrappedU)];
}
} // namespace

TEST_CASE("Terrain baker shares terrain-aligned custom texture tiles")
{
  auto images = std::array<QImage, 3>{
    QImage{128, 128, QImage::Format_RGBA8888},
    QImage{128, 128, QImage::Format_RGBA8888},
    QImage{128, 128, QImage::Format_RGBA8888}};
  for (auto y = 0; y < 128; ++y)
    for (auto x = 0; x < 128; ++x)
    {
      images[0].setPixelColor(x, y, QColor{x * 2, y * 2, (x ^ y) * 2});
      images[1].setPixelColor(x, y, QColor{255 - y * 2, x * 2, 255 - x * 2});
      images[2].setPixelColor(x, y, QColor{y * 2, 255 - x * 2, y * 2});
    }
  auto prepared = prepareTerrainMaterial(std::move(images));
  REQUIRE(prepared.is_success());
  const auto& material = prepared.value();
  auto region = TerrainRegion{vm::vec2d{0, 0}};
  for (auto row = 0; row <= TerrainRegion::Rows; ++row)
    for (auto col = 0; col <= TerrainRegion::Columns; ++col)
    {
      const auto weight = 0.3 + 0.4 * double(row) / TerrainRegion::Rows;
      region.weights[size_t(row * (TerrainRegion::Columns + 1) + col)] = {
        1.0 - weight, weight, 0};
    }
  // Exercise shared cells beside a three-material junction and period edges.
  for (auto row = 4; row <= 6; ++row)
    for (auto col = 8; col <= 10; ++col)
      region.weights[size_t(row * (TerrainRegion::Columns + 1) + col)] = {
        0.25, 0.25, 0.5};
  auto sharedResult = runTerrainBaker(region, &material, TerrainBakeMode::Shared);
  auto localResult = runTerrainBaker(region, &material, TerrainBakeMode::Local);
  REQUIRE(sharedResult.is_success());
  REQUIRE(localResult.is_success());
  const auto& shared = sharedResult.value();
  const auto& local = localResult.value();

  auto tileMaterials = std::map<std::pair<int, int>, size_t>{};
  auto compared = size_t{0};
  for (const auto& face : shared.faces)
  {
    if (face.kind != TerrainFaceKind::Shared)
      continue;
    const auto bounds = face.polygon.boundingRect();
    const auto center = bounds.center() + QPointF{0.125, -0.125};
    const auto tileX = int(std::floor(center.x() / 128));
    const auto tileY = int(std::floor(center.y() / 128));
    CHECK(bounds.left() >= tileX * 128 + 8);
    CHECK(bounds.right() <= (tileX + 1) * 128 - 8);
    CHECK(bounds.top() >= tileY * 128 + 8);
    CHECK(bounds.bottom() <= (tileY + 1) * 128 - 8);
    tileMaterials[{tileX, tileY}] = face.material;
    const auto points = std::array{
      center,
      QPointF{bounds.left() + 0.125, bounds.top() + 0.375},
      QPointF{bounds.right() - 0.125, bounds.bottom() - 0.375},
      QPointF{bounds.left() + 0.125, bounds.bottom() - 0.375},
      QPointF{bounds.right() - 0.125, bounds.top() + 0.375}};
    for (const auto& point : points)
    {
      if (!face.polygon.containsPoint(point, Qt::OddEvenFill))
        continue;
      const auto original = std::ranges::find_if(local.faces, [&](const auto& candidate) {
        return candidate.polygon.containsPoint(point, Qt::OddEvenFill);
      });
      REQUIRE(original != local.faces.end());
      for (auto mip = 0; mip < 4; ++mip)
        CHECK(
          sampleIndex(shared, face, point.x(), point.y(), mip)
          == sampleIndex(local, *original, point.x(), point.y(), mip));
    }
    ++compared;
  }
  CHECK(compared > 0);
  auto sharedAcrossTiles = false;
  for (const auto& [firstTile, firstMaterial] : tileMaterials)
    for (const auto& [secondTile, secondMaterial] : tileMaterials)
      sharedAcrossTiles |= firstTile != secondTile && firstMaterial == secondMaterial;
  CHECK(sharedAcrossTiles);

  for (const auto& point : {QPointF{204.25, 107.75}, QPointF{121.25, 36.375}})
  {
    const auto face = std::ranges::find_if(shared.faces, [&](const auto& candidate) {
      return candidate.polygon.containsPoint(point, Qt::OddEvenFill);
    });
    REQUIRE(face != shared.faces.end());
    CHECK(face->kind == TerrainFaceKind::Local);
  }
  for (const auto density : {50, 200})
  {
    region.patchDensityPercent = density;
    const auto dense = runTerrainBaker(region, &material, TerrainBakeMode::Shared);
    REQUIRE(dense.is_success());
    CHECK(std::ranges::none_of(dense.value().faces, [](const auto& face) {
      return face.kind == TerrainFaceKind::Shared;
    }));
  }
}

TEST_CASE("Project terrain chooses shared tiles only within budget and with WAD savings")
{
  auto directory = QTemporaryDir{};
  REQUIRE(directory.isValid());
  const auto root = pathFromQString(directory.path());
  auto session =
    requireValue(ProjectSession::create(root / "shared.tbproj", "Shared", "Half-Life"));
  const auto colors =
    std::array{QColor{220, 40, 20}, QColor{30, 180, 40}, QColor{20, 40, 210}};
  auto textureIds = std::array<std::string, 3>{};
  for (auto slot = size_t{0}; slot < colors.size(); ++slot)
  {
    auto pixels = std::vector<unsigned char>(64 * 64 * 4, 255);
    for (auto offset = size_t{0}; offset < pixels.size(); offset += 4)
    {
      pixels[offset] = static_cast<unsigned char>(colors[slot].red());
      pixels[offset + 1] = static_cast<unsigned char>(colors[slot].green());
      pixels[offset + 2] = static_cast<unsigned char>(colors[slot].blue());
    }
    const auto name = "SHARED" + std::to_string(slot);
    const auto converted =
      requireValue(img::convertGoldSrcTexture({64, 64, pixels}, name));
    auto input = project::AssetImport{};
    input.type = project::AssetType::Texture;
    input.virtualPath = "textures/" + name + ".wad";
    input.displayName = name;
    input.original = input.compatible = converted.wad;
    textureIds[slot] = requireValue(session->importAsset(input)).id;
  }
  const auto recipe = std::string{
    R"({"format":"goldsrc-terrain-material","version":1,"slots":[{"name":"A"},{"name":"B"},{"name":"C"}]})"};
  auto materialInput = project::AssetImport{};
  materialInput.type = project::AssetType::Material;
  materialInput.virtualPath = "materials/shared.tbmat";
  materialInput.displayName = "Shared";
  materialInput.original.assign(recipe.begin(), recipe.end());
  materialInput.compatible = materialInput.original;
  const auto materialAsset = requireValue(session->importAsset(materialInput));
  {
    auto store = requireValue(project::ProjectStore::open(session->store().path()));
    for (auto slot = size_t{0}; slot < textureIds.size(); ++slot)
      requireValue(store->addDependency(
        materialAsset.id, textureIds[slot], "terrain-slot-" + std::to_string(slot)));
  }

  auto fixture = MapDocumentFixture{};
  auto config = mdl::MapFixtureConfig{.mapFormat = mdl::MapFormat::Valve};
  config.gameInfo.gameConfig.name = "Half-Life";
  auto& document = fixture.create(config);
  auto& map = document.map();
  mdl::selectAllNodes(map);
  mdl::removeSelectedNodes(map);
  const auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, map.worldBounds()};
  auto* floor = new mdl::BrushNode{requireValue(
    builder.createCuboid(vm::bbox3d{{0, -640, -64}, {1280, 0, 0}}, "SHARED0"))};
  mdl::addNodes(map, {{map.editorContext().currentLayer(), {floor}}});
  mdl::selectNodes(map, {floor});
  auto* group = mdl::groupSelectedNodes(map, "Terrain");
  REQUIRE(group);
  auto region = requireValue(createTerrainRegion(*group));
  region.materialAssetId = materialAsset.id;
  region.weights.fill({0.5, 0.5, 0});

  auto walls = std::vector<mdl::Node*>{};
  for (const auto& bounds :
       {vm::bbox3d{{-32, -672, -64}, {0, 32, 192}},
        vm::bbox3d{{1280, -672, -64}, {1312, 32, 192}},
        vm::bbox3d{{0, -672, -64}, {1280, -640, 192}},
        vm::bbox3d{{0, 0, -64}, {1280, 32, 192}},
        vm::bbox3d{{-32, -672, 192}, {1312, 32, 224}}})
    walls.push_back(
      new mdl::BrushNode{requireValue(builder.createCuboid(bounds, "SHARED0"))});
  mdl::addNodes(map, {{map.editorContext().currentLayer(), std::move(walls)}});
  REQUIRE(mdl::paste(map, R"(
    { "classname" "info_player_start" "origin" "64 -320 48" }
    { "classname" "info_player_deathmatch" "origin" "1160 -320 48" }
    { "classname" "light" "origin" "640 -320 150" "_light" "255 255 255 400" }
  )") == mdl::PasteType::Node);

  const auto source = root / "shared.map";
  const auto saveRegion = [&](const TerrainRegion& value) {
    auto content = group->group();
    content.setTerrainPaint(value.encode());
    REQUIRE(mdl::updateNodeContents(
      map, "Update terrain", {{group, mdl::NodeContents{std::move(content)}}}));
    requireValue(map.saveTo(source));
  };
  saveRegion(region);
  const auto estimate = requireValue(estimateTerrainGroup(*group, &session->store()));
  REQUIRE(std::ranges::any_of(
    estimate.cuts, [](const auto& cut) { return cut.kind == TerrainFaceKind::Shared; }));

  const auto material =
    requireValue(loadTerrainMaterial(session->store(), materialAsset.id));
  {
    auto smallGroup = mdl::GroupNode{mdl::Group{"Small terrain"}};
    smallGroup.addChild(new mdl::BrushNode{requireValue(
      builder.createCuboid(vm::bbox3d{{0, -128, -64}, {128, 0, 0}}, "SHARED0"))});
    auto smallRegion = requireValue(createTerrainRegion(smallGroup));
    smallRegion.materialAssetId = materialAsset.id;
    smallRegion.weights.fill({0.5, 0.5, 0});
    auto content = smallGroup.group();
    content.setTerrainPaint(smallRegion.encode());
    smallGroup.setGroup(std::move(content));

    const auto smallSurfaces = requireValue(terrainSurfaces(smallGroup, smallRegion));
    const auto smallShared =
      requireValue(runTerrainBaker(smallRegion, &material, TerrainBakeMode::Shared));
    const auto smallLocal =
      requireValue(runTerrainBaker(smallRegion, &material, TerrainBakeMode::Local));
    REQUIRE(std::ranges::any_of(smallShared.faces, [&](const auto& face) {
      return face.kind == TerrainFaceKind::Shared
             && terrainArea(smallSurfaces.front().outline.intersected(face.polygon))
                  > 1e-7;
    }));
    const auto wadBytes = [&](const TerrainBake& plan) {
      auto used = std::vector<bool>(plan.textures.size());
      for (const auto& face : plan.faces)
        if (terrainArea(smallSurfaces.front().outline.intersected(face.polygon)) > 1e-7)
          used[face.material] = true;
      auto lumps = std::vector<std::vector<std::uint8_t>>{};
      for (auto i = size_t{0}; i < used.size(); ++i)
        if (used[i])
          lumps.push_back(plan.textures[i].miptex);
      return requireValue(project::buildGoldSrcWad(lumps)).size();
    };
    const auto sharedBytes = wadBytes(smallShared);
    const auto localBytes = wadBytes(smallLocal);
    REQUIRE(sharedBytes >= localBytes);
    const auto smallEstimate =
      requireValue(estimateTerrainGroup(smallGroup, &session->store()));
    CHECK(smallEstimate.wadBytes == localBytes);
    CHECK(std::ranges::none_of(smallEstimate.cuts, [](const auto& cut) {
      return cut.kind == TerrainFaceKind::Shared;
    }));
  }
  const auto savedRegion = requireValue(TerrainRegion::decode(region.encode()));
  const auto local =
    requireValue(runTerrainBaker(savedRegion, &material, TerrainBakeMode::Local));
  auto used = std::vector<bool>(local.textures.size());
  for (const auto& face : local.faces)
    used[face.material] = true;
  auto localLumps = std::vector<std::vector<std::uint8_t>>{};
  for (auto i = size_t{0}; i < used.size(); ++i)
    if (used[i])
      localLumps.push_back(local.textures[i].miptex);
  const auto localBytes = requireValue(project::buildGoldSrcWad(localLumps)).size();
  CHECK(estimate.wadBytes < localBytes);

  const auto snapshot =
    requireValue(bakeTerrainSnapshot(readBytes(source), &session->store()));
  REQUIRE(snapshot);
  CHECK(snapshot->topFaces == estimate.generatedFaces);
  auto actualLumps = std::vector<std::vector<std::uint8_t>>{};
  for (const auto& [name, bytes] : snapshot->textures)
    actualLumps.push_back(bytes);
  CHECK(requireValue(project::buildGoldSrcWad(actualLumps)).size() == estimate.wadBytes);
  const auto repeated =
    requireValue(bakeTerrainSnapshot(readBytes(source), &session->store()));
  REQUIRE(repeated);
  CHECK(repeated->map == snapshot->map);
  CHECK(repeated->textures == snapshot->textures);

  auto constrained = region;
  constrained.maxAddedFaces = 0;
  saveRegion(constrained);
  const auto faceLimited = requireValue(estimateTerrainGroup(*group, &session->store()));
  CHECK(std::ranges::none_of(faceLimited.cuts, [](const auto& cut) {
    return cut.kind == TerrainFaceKind::Shared;
  }));
  CHECK(bakeTerrainSnapshot(readBytes(source), &session->store()).is_error());
  constrained.maxAddedFaces = -1;
  constrained.maxWadKiB = 0;
  saveRegion(constrained);
  const auto wadLimited = requireValue(estimateTerrainGroup(*group, &session->store()));
  CHECK(std::ranges::none_of(wadLimited.cuts, [](const auto& cut) {
    return cut.kind == TerrainFaceKind::Shared;
  }));
  CHECK(bakeTerrainSnapshot(readBytes(source), &session->store()).is_error());
  constrained.maxWadKiB = -1;
  constrained.minFaceWidth = 1280;
  saveRegion(constrained);
  const auto widthLimited = requireValue(estimateTerrainGroup(*group, &session->store()));
  CHECK(std::ranges::none_of(widthLimited.cuts, [](const auto& cut) {
    return cut.kind == TerrainFaceKind::Shared;
  }));
  CHECK(bakeTerrainSnapshot(readBytes(source), &session->store()).is_error());

  saveRegion(region);
  auto mapInput = project::AssetImport{};
  mapInput.type = project::AssetType::Map;
  mapInput.virtualPath = "maps/shared.map";
  mapInput.displayName = "Shared";
  mapInput.original = mapInput.compatible = readBytes(source);
  const auto mapAsset = requireValue(session->importAsset(mapInput));
  requireValue(session->save());
  auto cached = requireValue(session->store().terrainCompile(mapAsset.id));
  REQUIRE(cached);
  INFO(cached->error);
  REQUIRE(cached->error.empty());
  CHECK(cached->generatorVersion == TerrainGeneratorVersion);
  cached->generatorVersion = TerrainGeneratorVersion - 1;
  {
    auto store = requireValue(project::ProjectStore::open(session->store().path()));
    requireValue(store->setTerrainCompile(mapAsset.id, *cached));
  }
  CHECK(requireValue(session->terrainCompileNeedsSave(mapAsset.id)));
  requireValue(session->save());
  CHECK_FALSE(requireValue(session->terrainCompileNeedsSave(mapAsset.id)));
  if (const auto output = qEnvironmentVariable("TB_TERRAIN_SHARED_ACCEPTANCE_PROJECT");
      !output.isEmpty())
    requireValue(session->saveCopy(pathFromQString(output)));
}
} // namespace tb::ui
