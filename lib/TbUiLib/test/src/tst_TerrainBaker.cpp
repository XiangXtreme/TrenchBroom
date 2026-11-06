/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QColor>
#include <QFile>
#include <QImage>
#include <QPointF>
#include <QTemporaryDir>

#include "base/Logger.h"
#include "base/SimpleParserStatus.h"
#include "img/GoldSrcTexture.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushNode.h"
#include "mdl/EditorContext.h"
#include "mdl/Entity.h"
#include "mdl/EntityDefinition.h"
#include "mdl/EntityNode.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/LockState.h"
#include "mdl/Map.h"
#include "mdl/Map_CopyPaste.h"
#include "mdl/Map_Entities.h"
#include "mdl/Map_Geometry.h"
#include "mdl/Map_Groups.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/NodeContents.h"
#include "mdl/PasteType.h"
#include "mdl/TransactionScope.h"
#include "mdl/WorldNode.h"
#include "mdl/WorldReader.h"
#include "project/GoldSrcWad.h"
#include "project/ProjectStore.h"
#include "ui/ClipTool.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"
#include "ui/ProjectSession.h"
#include "ui/QPathUtils.h"
#include "ui/TerrainBaker.h"
#include "ui/TerrainChunks.h"
#include "ui/TerrainExport.h"
#include "ui/TerrainHeightField.h"
#include "ui/TerrainMaterial.h"
#include "ui/TerrainOperations.h"
#include "ui/TerrainRegion.h"

#include "kd/task_manager.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
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

TEST_CASE("Terrain converts a planar rectangular slope without changing its surface")
{
  auto fixture = MapDocumentFixture{};
  auto config = mdl::MapFixtureConfig{.mapFormat = mdl::MapFormat::Valve};
  auto& map = fixture.create(config).map();
  mdl::selectAllNodes(map);
  mdl::removeSelectedNodes(map);
  const auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, map.worldBounds()};
  auto* slope = new mdl::BrushNode{requireValue(builder.createBrush(
    std::vector<vm::vec3d>{
      {0, 0, 0},
      {0, -64, 0},
      {128, 0, 16},
      {128, -64, 16},
      {0, 0, -64},
      {0, -64, -64},
      {128, 0, -64},
      {128, -64, -64}},
    "TERR_GRASS"))};
  mdl::addNodes(map, {{map.editorContext().currentLayer(), {slope}}});
  mdl::selectNodes(map, {slope});
  auto* group = mdl::groupSelectedNodes(map, "Slope");
  REQUIRE(group);
  const auto region = requireValue(createTerrainRegion(*group));
  const auto field = requireValue(createTerrainHeightField(*group, region, 32));
  CHECK(field.columns == 4);
  CHECK(field.rows == 2);
  CHECK(field.bottom == Catch::Approx(-64));
  for (auto row = 0; row <= field.rows; ++row)
    for (auto column = 0; column <= field.columns; ++column)
      CHECK(field.heights[field.index(column, row)] == Catch::Approx(column * 4));
  CHECK(createTerrainHeightField(*group, region, 30).is_error());
}

TEST_CASE("Terrain conversion joins aligned brushes without seams or gaps")
{
  const auto convert = [](const double rightStart, const double rightHeight) {
    auto fixture = MapDocumentFixture{};
    auto config = mdl::MapFixtureConfig{.mapFormat = mdl::MapFormat::Valve};
    auto& map = fixture.create(config).map();
    mdl::selectAllNodes(map);
    mdl::removeSelectedNodes(map);
    const auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, map.worldBounds()};
    auto* left = new mdl::BrushNode{requireValue(builder.createBrush(
      std::vector<vm::vec3d>{
        {0, 0, 0},
        {0, -640, 0},
        {640, 0, 40},
        {640, -640, 40},
        {0, 0, -64},
        {0, -640, -64},
        {640, 0, -64},
        {640, -640, -64}},
      "TERR_GRASS"))};
    auto* right = new mdl::BrushNode{requireValue(builder.createBrush(
      std::vector<vm::vec3d>{
        {rightStart, 0, rightHeight},
        {rightStart, -640, rightHeight},
        {1280, 0, rightHeight},
        {1280, -640, rightHeight},
        {rightStart, 0, -64},
        {rightStart, -640, -64},
        {1280, 0, -64},
        {1280, -640, -64}},
      "TERR_GRASS"))};
    mdl::addNodes(map, {{map.editorContext().currentLayer(), {left, right}}});
    mdl::selectNodes(map, {left, right});
    auto* group = mdl::groupSelectedNodes(map, "Terrain");
    REQUIRE(group);
    const auto region = requireValue(createTerrainRegion(*group));
    return createTerrainHeightField(*group, region, 64);
  };

  const auto result = convert(640, 40);
  REQUIRE(result.is_success());
  const auto& field = result.value();
  CHECK(field.columns == 20);
  CHECK(field.rows == 10);
  CHECK(field.heights[field.index(5, 5)] == Catch::Approx(20));
  CHECK(field.heights[field.index(10, 5)] == Catch::Approx(40));
  CHECK(field.heights[field.index(20, 5)] == Catch::Approx(40));
  CHECK(convert(640, 48).is_error());
  CHECK(convert(704, 40).is_error());
}

TEST_CASE("Terrain conversion preserves source materials and is one undo step")
{
  auto fixture = MapDocumentFixture{};
  auto config = mdl::MapFixtureConfig{.mapFormat = mdl::MapFormat::Valve};
  auto& map = fixture.create(config).map();
  mdl::selectAllNodes(map);
  mdl::removeSelectedNodes(map);
  const auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, map.worldBounds()};
  auto* left = new mdl::BrushNode{
    requireValue(builder.createCuboid(vm::bbox3d{{0, -64, -64}, {64, 0, 0}}, "LEFT"))};
  auto* right = new mdl::BrushNode{
    requireValue(builder.createCuboid(vm::bbox3d{{64, -64, -64}, {128, 0, 0}}, "RIGHT"))};
  const auto sourceTop = [](const auto& brush) {
    const auto it = std::ranges::find_if(
      brush.faces(), [](const auto& face) { return face.normal().z() > 0.9; });
    REQUIRE(it != brush.faces().end());
    return *it;
  };
  const auto leftTop = sourceTop(left->brush());
  const auto rightTop = sourceTop(right->brush());
  mdl::addNodes(map, {{map.editorContext().currentLayer(), {left, right}}});
  mdl::selectNodes(map, {left, right});
  auto* group = mdl::groupSelectedNodes(map, "Terrain");
  REQUIRE(group);
  auto region = requireValue(createTerrainRegion(*group));
  region.paint({32, 32}, 48, 1);
  const auto originalPaint = region.encode();
  auto content = group->group();
  content.setTerrainPaint(originalPaint);
  REQUIRE(mdl::updateNodeContents(
    map, "Paint", {{group, mdl::NodeContents{std::move(content)}}}));

  group->setLockState(mdl::LockState::Locked);
  CHECK(convertTerrainToHeightField(map, *group, 32).is_error());
  CHECK(group->children().size() == 2);
  CHECK(group->group().terrainPaint() == originalPaint);
  group->setLockState(mdl::LockState::Inherited);

  requireValue(convertTerrainToHeightField(map, *group, 32));
  CHECK(group->children().size() == 16);
  const auto converted =
    requireValue(TerrainRegion::decode(group->group().terrainPaint()));
  REQUIRE(converted.heightField);
  CHECK(converted.weights == requireValue(TerrainRegion::decode(originalPaint)).weights);
  REQUIRE(terrainSurfaces(*group, converted).is_success());
  for (const auto* child : group->children())
  {
    const auto& brush = static_cast<const mdl::BrushNode*>(child)->brush();
    const auto top = std::ranges::find_if(
      brush.faces(), [](const auto& face) { return face.normal().z() > 0.9; });
    const auto bottom = std::ranges::find_if(
      brush.faces(), [](const auto& face) { return face.normal().z() < -0.9; });
    REQUIRE(top != brush.faces().end());
    REQUIRE(bottom != brush.faces().end());
    const auto& source = top->center().x() < 64 ? leftTop : rightTop;
    CHECK(top->materialName() == source.materialName());
    CHECK(bottom->materialName() == source.materialName());
    CHECK(
      vm::is_equal(top->uvCoords(top->center()), source.uvCoords(top->center()), 1e-5f));
  }
  map.undoCommand();
  CHECK(group->children().size() == 2);
  CHECK(group->group().terrainPaint() == originalPaint);
  map.redoCommand();
  CHECK(group->children().size() == 16);
  const auto redone = requireValue(TerrainRegion::decode(group->group().terrainPaint()));
  CHECK(terrainSurfaces(*group, redone).is_success());
}

TEST_CASE("Terrain conversion groups selected ground as one undo step")
{
  auto fixture = MapDocumentFixture{};
  auto config = mdl::MapFixtureConfig{.mapFormat = mdl::MapFormat::Valve};
  auto& map = fixture.create(config).map();
  mdl::selectAllNodes(map);
  mdl::removeSelectedNodes(map);
  const auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, map.worldBounds()};
  auto* source = new mdl::BrushNode{
    requireValue(builder.createCuboid(vm::bbox3d{{0, -64, -64}, {64, 0, 0}}, "GROUND"))};
  auto* layer = map.editorContext().currentLayer();
  mdl::addNodes(map, {{layer, {source}}});
  mdl::selectNodes(map, {source});

  CHECK(convertSelectedTerrainToHeightField(map, 30).is_error());
  CHECK(source->parent() == layer);
  REQUIRE(map.selection().brushes.size() == 1);
  CHECK(map.selection().brushes.front() == source);

  requireValue(convertSelectedTerrainToHeightField(map, 32));
  REQUIRE(map.selection().groups.size() == 1);
  auto* group = map.selection().groups.front();
  CHECK(group->children().size() == 8);
  CHECK(requireValue(TerrainRegion::decode(group->group().terrainPaint())).heightField);
  map.undoCommand();
  CHECK(source->parent() == layer);
  REQUIRE(map.selection().brushes.size() == 1);
  CHECK(map.selection().brushes.front() == source);
  map.redoCommand();
  REQUIRE(map.selection().groups.size() == 1);
  CHECK(map.selection().groups.front()->children().size() == 8);
}

TEST_CASE("Large existing ground converts into shared terrain chunks")
{
  auto fixture = MapDocumentFixture{};
  auto config = mdl::MapFixtureConfig{.mapFormat = mdl::MapFormat::Valve};
  auto& map = fixture.create(config).map();
  mdl::selectAllNodes(map);
  mdl::removeSelectedNodes(map);
  const auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, map.worldBounds()};
  auto* source = new mdl::BrushNode{requireValue(builder.createBrush(
    std::vector<vm::vec3d>{
      {0, 0, 0},
      {0, -128, 0},
      {1408, 0, 88},
      {1408, -128, 88},
      {0, 0, -64},
      {0, -128, -64},
      {1408, 0, -64},
      {1408, -128, -64}},
    "TERR_GRASS"))};
  auto* layer = map.editorContext().currentLayer();
  mdl::addNodes(map, {{layer, {source}}});
  mdl::selectNodes(map, {source});
  CHECK(convertSelectedTerrainToHeightField(map, 30).is_error());
  CHECK(source->parent() == layer);
  REQUIRE(map.selection().brushes.size() == 1);

  requireValue(convertSelectedTerrainToHeightField(map, 64));
  REQUIRE(map.selection().groups.size() == 1);
  auto* root = map.selection().groups.front();
  REQUIRE(isTerrainChunkGroup(*root));
  const auto chunks = requireValue(readTerrainChunks(*root));
  REQUIRE(chunks.chunks.size() == 2);
  REQUIRE(chunks.combined->heightField);
  CHECK(chunks.combined->heightField->columns == 22);
  CHECK(chunks.combined->heightField->rows == 2);
  CHECK(
    chunks.combined->heightField->heights[chunks.combined->heightField->index(20, 1)]
    == Catch::Approx(80));
  for (const auto& chunk : chunks.chunks)
  {
    const auto* child =
      static_cast<const mdl::GroupNode*>(root->children()[chunk.childIndex]);
    const auto surfaces = requireValue(terrainSurfaces(*child, chunk.region));
    for (const auto& surface : surfaces)
      CHECK(surface.brush->brush().face(surface.top).materialName() == "TERR_GRASS");
  }
  map.undoCommand();
  CHECK(source->parent() == layer);
  REQUIRE(map.selection().brushes.size() == 1);
  CHECK(map.selection().brushes.front() == source);
  map.redoCommand();
  REQUIRE(map.selection().groups.size() == 1);
  CHECK(readTerrainChunks(*map.selection().groups.front()).is_success());
}

TEST_CASE("Terrain chunks share one continuous height grid")
{
  auto rootData = mdl::Group{"Terrain"};
  rootData.setTerrainPaint(terrainChunkGroupMarker());
  auto root = mdl::GroupNode{std::move(rootData)};
  const auto makeChunk = [](const vm::vec2d& origin) {
    auto region = TerrainRegion{origin};
    region.heightField = requireValue(TerrainHeightField::create(2, 2, 32, -64, 0));
    auto data = mdl::Group{"Chunk"};
    data.setTerrainPaint(region.encode());
    return new mdl::GroupNode{std::move(data)};
  };
  auto* left = makeChunk({0, 0});
  auto* right = makeChunk({64, 0});
  root.addChild(left);
  root.addChild(right);
  auto chunks = requireValue(readTerrainChunks(root));
  REQUIRE(chunks.combined->heightField);
  CHECK(chunks.combined->heightField->columns == 4);
  CHECK(chunks.combined->heightField->rows == 2);
  CHECK(chunks.chunks[0].column == 0);
  CHECK(chunks.chunks[1].column == 2);

  auto inconsistent = requireValue(TerrainRegion::decode(right->group().terrainPaint()));
  inconsistent.heightField->heights[inconsistent.heightField->index(0, 1)] = 16;
  auto content = right->group();
  content.setTerrainPaint(inconsistent.encode());
  right->setGroup(std::move(content));
  CHECK(readTerrainChunks(root).is_error());

  inconsistent.heightField->heights[inconsistent.heightField->index(0, 1)] = 0;
  inconsistent.origin = {96, 0};
  content = right->group();
  content.setTerrainPaint(inconsistent.encode());
  right->setGroup(std::move(content));
  CHECK(readTerrainChunks(root).is_error());

  inconsistent.origin = {32, 0};
  content = right->group();
  content.setTerrainPaint(inconsistent.encode());
  right->setGroup(std::move(content));
  CHECK(readTerrainChunks(root).is_error());

  inconsistent.origin = {64, 0};
  inconsistent.weights[size_t(TerrainRegion::Columns + 1)] = {0, 1, 0};
  content = right->group();
  content.setTerrainPaint(inconsistent.encode());
  right->setGroup(std::move(content));
  CHECK(readTerrainChunks(root).is_error());
}

TEST_CASE("Terrain rectangle creation is atomic and undoable")
{
  auto fixture = MapDocumentFixture{};
  auto config = mdl::MapFixtureConfig{.mapFormat = mdl::MapFormat::Valve};
  auto& map = fixture.create(config).map();
  mdl::selectAllNodes(map);
  mdl::removeSelectedNodes(map);
  const auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, map.worldBounds()};
  auto* existing = new mdl::BrushNode{requireValue(
    builder.createCuboid(vm::bbox3d{{512, 512, -64}, {576, 576, 0}}, "GROUND"))};
  auto* layer = map.editorContext().currentLayer();
  mdl::addNodes(map, {{layer, {existing}}});
  mdl::selectNodes(map, {existing});
  CHECK(createTerrainRectangle(map, {0, 0}, {130, 64}, 16, 64, 32).is_error());
  CHECK(createTerrainRectangle(map, {0, 0}, {2048, 64}, 16, 64, 32).is_error());
  CHECK(map.selection().brushes.front() == existing);

  requireValue(createTerrainRectangle(map, {128, -64}, {0, 0}, 16, 64, 32));
  REQUIRE(map.selection().groups.size() == 1);
  auto* terrain = map.selection().groups.front();
  const auto region =
    requireValue(TerrainRegion::decode(terrain->group().terrainPaint()));
  REQUIRE(region.heightField);
  CHECK(region.origin == vm::vec2d{0, 0});
  CHECK(region.heightField->columns == 4);
  CHECK(region.heightField->rows == 2);
  CHECK(region.heightField->bottom == -48);
  CHECK(terrain->children().size() == 16);
  CHECK(terrainSurfaces(*terrain, region).is_success());
  map.undoCommand();
  REQUIRE(map.selection().brushes.size() == 1);
  CHECK(map.selection().brushes.front() == existing);
  CHECK(layer->children().size() == 1);
  map.redoCommand();
  REQUIRE(map.selection().groups.size() == 1);
  CHECK(map.selection().groups.front()->children().size() == 16);
}

TEST_CASE("Large terrain rectangle creates one undoable chunk group")
{
  auto fixture = MapDocumentFixture{};
  auto config = mdl::MapFixtureConfig{.mapFormat = mdl::MapFormat::Valve};
  auto& map = fixture.create(config).map();
  mdl::selectAllNodes(map);
  mdl::removeSelectedNodes(map);
  auto* layer = map.editorContext().currentLayer();
  const auto before = layer->children().size();
  CHECK(createTerrainChunkRectangle(map, {0, -64}, {1408, 0}, 0, 64, 64).is_success());
  REQUIRE(map.selection().groups.size() == 1);
  auto* root = map.selection().groups.front();
  REQUIRE(isTerrainChunkGroup(*root));
  REQUIRE(root->children().size() == 2);
  const auto chunks = requireValue(readTerrainChunks(*root));
  REQUIRE(chunks.combined->heightField);
  CHECK(chunks.combined->heightField->columns == 22);
  CHECK(chunks.combined->heightField->rows == 1);
  CHECK(chunks.combined->heightField->heights.size() == 46);
  for (const auto& chunk : chunks.chunks)
  {
    const auto* group =
      static_cast<const mdl::GroupNode*>(root->children()[chunk.childIndex]);
    CHECK(terrainSurfaces(*group, chunk.region).is_success());
  }
  auto height = *chunks.combined->heightField;
  const auto seam = height.index(20, 1);
  height.heights[seam] = 16;
  requireValue(updateTerrainHeightField(map, *root, height));
  auto edited = requireValue(readTerrainChunks(*root));
  CHECK(edited.combined->heightField->heights[seam] == 16);
  for (const auto& chunk : edited.chunks)
  {
    const auto* group =
      static_cast<const mdl::GroupNode*>(root->children()[chunk.childIndex]);
    CHECK(terrainSurfaces(*group, chunk.region).is_success());
  }
  auto invalid = height;
  invalid.heights[seam] = map.worldBounds().max.z() * 2;
  CHECK(updateTerrainHeightField(map, *root, invalid).is_error());
  CHECK(
    requireValue(readTerrainChunks(*root)).combined->heightField->heights[seam] == 16);

  auto directory = QTemporaryDir{};
  REQUIRE(directory.isValid());
  const auto path = pathFromQString(directory.filePath("large-terrain.map"));
  requireValue(map.saveTo(path));
  const auto bytes = readBytes(path);
  const auto snapshot = requireValue(bakeTerrainSnapshot(bytes));
  REQUIRE(snapshot);
  CHECK(snapshot->topFaces >= 44);
  const auto snapshotText = std::string{snapshot->map.begin(), snapshot->map.end()};
  CHECK(snapshotText.find(terrainChunkGroupMarker()) == std::string::npos);
  auto logger = NullLogger{};
  auto status = SimpleParserStatus{logger};
  auto tasks = kdl::task_manager{2};
  const auto source =
    std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
  auto world = requireValue(mdl::WorldReader{source, mdl::MapFormat::Valve, {}}.read(
    map.worldBounds(), status, tasks));
  const auto findRoot =
    [&](const auto& self, const mdl::Node& node) -> const mdl::GroupNode* {
    if (const auto* group = dynamic_cast<const mdl::GroupNode*>(&node);
        group && isTerrainChunkGroup(*group))
      return group;
    for (const auto* child : node.children())
      if (const auto* group = self(self, *child))
        return group;
    return nullptr;
  };
  const auto* loaded = findRoot(findRoot, *world);
  REQUIRE(loaded);
  CHECK(
    requireValue(readTerrainChunks(*loaded)).combined->heightField->heights[seam] == 16);

  map.undoCommand();
  CHECK(requireValue(readTerrainChunks(*root)).combined->heightField->heights[seam] == 0);
  map.redoCommand();
  CHECK(
    requireValue(readTerrainChunks(*root)).combined->heightField->heights[seam] == 16);
  map.undoCommand();
  map.undoCommand();
  CHECK(layer->children().size() == before);
  map.redoCommand();
  REQUIRE(map.selection().groups.size() == 1);
  CHECK(readTerrainChunks(*map.selection().groups.front()).is_success());
}

TEST_CASE("Ordinary brush editing protects sculptable terrain")
{
  auto fixture = MapDocumentFixture{};
  auto config = mdl::MapFixtureConfig{.mapFormat = mdl::MapFormat::Valve};
  auto& map = fixture.create(config).map();
  mdl::selectAllNodes(map);
  mdl::removeSelectedNodes(map);
  requireValue(createTerrainChunkRectangle(map, {0, -128}, {1408, 0}, 0, 64, 64));
  REQUIRE(map.selection().groups.size() == 1);
  auto* root = map.selection().groups.front();
  auto* chunk = static_cast<mdl::GroupNode*>(root->children().front());
  auto* brush = static_cast<mdl::BrushNode*>(chunk->children().front());
  const auto vertex = brush->brush().vertexPositions().front();
  const auto shift = vm::translation_matrix(vm::vec3d{0, 0, 16});
  CHECK_FALSE(mdl::transformVertices(map, {vertex}, shift).success);
  CHECK_FALSE(mdl::transformEdges(map, {}, shift));
  CHECK_FALSE(mdl::transformFaces(map, {}, shift));
  CHECK_FALSE(mdl::addVertex(map, vertex));
  CHECK_FALSE(mdl::removeVertices(map, "Remove vertices", {vertex}));
  CHECK_FALSE(mdl::chamferVertices(map, "Chamfer", {vertex}, 1));
  CHECK_FALSE(mdl::chamferEdges(map, "Chamfer", {}, 1));
  CHECK_FALSE(mdl::snapVertices(map, 16));
  CHECK_FALSE(mdl::extrudeBrushes(map, {}, {0, 0, 16}));
  CHECK_FALSE(mdl::transformNodes(map, {brush}, "Move brush", shift));
  CHECK_FALSE(mdl::transformNodes(map, {chunk}, "Move chunk", shift));
  CHECK(brush->brush().hasVertex(vertex));
  REQUIRE(readTerrainChunks(*root).is_success());

  // Moving the complete terrain and editing its height field remain supported.
  REQUIRE(mdl::transformNodes(map, {root}, "Move terrain", shift));
  const auto moved = requireValue(readTerrainChunks(*root));
  CHECK(terrainGroupSurfaces(*root, *moved.combined).is_success());
  auto heights = *moved.combined->heightField;
  heights.heights[heights.index(20, 1)] = 8;
  requireValue(updateTerrainHeightField(map, *root, heights));
  CHECK(readTerrainChunks(*root).is_success());

  // A non-terrain brush remains editable through the ordinary tools.
  mdl::deselectAll(map);
  const auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, map.worldBounds()};
  auto* ordinary = new mdl::BrushNode{requireValue(
    builder.createCuboid(vm::bbox3d{{-128, -128, -64}, {-64, -64, 0}}, "GROUND"))};
  mdl::addNodes(map, {{map.editorContext().currentLayer(), {ordinary}}});
  mdl::selectNodes(map, {ordinary});
  CHECK(mdl::transformVertices(map, {{-64, -64, 0}}, shift).success);
}

TEST_CASE("Terrain conversion does not desynchronize linked groups")
{
  auto fixture = MapDocumentFixture{};
  auto config = mdl::MapFixtureConfig{.mapFormat = mdl::MapFormat::Valve};
  auto& map = fixture.create(config).map();
  mdl::selectAllNodes(map);
  mdl::removeSelectedNodes(map);
  const auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, map.worldBounds()};
  auto* source = new mdl::BrushNode{requireValue(
    builder.createCuboid(vm::bbox3d{{0, -64, -64}, {64, 0, 0}}, "TERR_GRASS"))};
  mdl::addNodes(map, {{map.editorContext().currentLayer(), {source}}});
  mdl::selectNodes(map, {source});
  auto* group = mdl::groupSelectedNodes(map, "Terrain");
  REQUIRE(group);
  auto* linked = mdl::createLinkedDuplicate(map);
  REQUIRE(linked);
  mdl::deselectAll(map);
  mdl::selectNodes(map, {group});
  const auto result = convertTerrainToHeightField(map, *group, 32);
  REQUIRE(result.is_error());
  CHECK(std::get<Error>(result.error()).msg.find("Unlink") != std::string::npos);
  CHECK(group->children().size() == 1);
  CHECK(linked->children().size() == 1);
  CHECK(group->group().terrainPaint().empty());
  CHECK(linked->group().terrainPaint().empty());
}

TEST_CASE("Terrain sculpt updates only affected brushes and supports undo")
{
  auto fixture = MapDocumentFixture{};
  auto config = mdl::MapFixtureConfig{.mapFormat = mdl::MapFormat::Valve};
  auto& map = fixture.create(config).map();
  mdl::selectAllNodes(map);
  mdl::removeSelectedNodes(map);
  const auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, map.worldBounds()};
  auto* source = new mdl::BrushNode{requireValue(
    builder.createCuboid(vm::bbox3d{{0, -64, -64}, {64, 0, 0}}, "TERR_GRASS"))};
  mdl::addNodes(map, {{map.editorContext().currentLayer(), {source}}});
  mdl::selectNodes(map, {source});
  auto* group = mdl::groupSelectedNodes(map, "Terrain");
  REQUIRE(group);
  requireValue(convertTerrainToHeightField(map, *group, 32));
  const auto initial = group->group().terrainPaint();
  const auto region = requireValue(TerrainRegion::decode(initial));
  REQUIRE(region.heightField);
  const auto order = requireValue(terrainHeightFieldBrushIndices(*group, region));
  auto oldVertices = std::vector<std::vector<vm::vec3d>>{};
  for (const auto* child : group->children())
    oldVertices.push_back(
      static_cast<const mdl::BrushNode*>(child)->brush().vertexPositions());

  auto height = *region.heightField;
  height.heights[height.index(1, 1)] = 16;
  requireValue(updateTerrainHeightField(map, *group, height));
  auto changedCount = size_t{0};
  for (auto i = size_t{0}; i < group->children().size(); ++i)
    if (
      static_cast<const mdl::BrushNode*>(group->children()[i])->brush().vertexPositions()
      != oldVertices[i])
      ++changedCount;
  CHECK(changedCount == 6);
  const auto sculpted = group->group().terrainPaint();
  const auto updated = requireValue(TerrainRegion::decode(sculpted));
  REQUIRE(updated.heightField);
  CHECK(updated.heightField->heights == height.heights);
  CHECK(updated.weights == region.weights);
  CHECK(terrainSurfaces(*group, updated).is_success());
  for (const auto index : order)
  {
    const auto& brush =
      static_cast<const mdl::BrushNode*>(group->children()[index])->brush();
    const auto top = std::ranges::find_if(
      brush.faces(), [](const auto& face) { return face.normal().z() > 1e-6; });
    REQUIRE(top != brush.faces().end());
    CHECK(top->materialName() == "TERR_GRASS");
  }
  map.undoCommand();
  CHECK(group->group().terrainPaint() == initial);
  map.redoCommand();
  CHECK(group->group().terrainPaint() == sculpted);

  auto outside = height;
  outside.heights[outside.index(1, 1)] = map.worldBounds().max.z() * 2;
  CHECK(updateTerrainHeightField(map, *group, outside).is_error());
  CHECK(group->group().terrainPaint() == sculpted);
  CHECK(terrainSurfaces(*group, updated).is_success());

  map.startTransaction("Sculpt stroke", mdl::TransactionScope::LongRunning);
  auto first = height;
  first.heights[first.index(1, 1)] = 20;
  requireValue(updateTerrainHeightField(map, *group, first));
  first.heights[first.index(1, 1)] = 24;
  requireValue(updateTerrainHeightField(map, *group, first));
  CHECK(map.commitTransaction());
  map.undoCommand();
  CHECK(group->group().terrainPaint() == sculpted);
  CHECK(terrainSurfaces(*group, updated).is_success());
  map.startTransaction("Cancel sculpt stroke", mdl::TransactionScope::LongRunning);
  requireValue(updateTerrainHeightField(map, *group, first));
  map.cancelTransaction();
  CHECK(group->group().terrainPaint() == sculpted);
  CHECK(terrainSurfaces(*group, updated).is_success());
}

TEST_CASE("Terrain brush entities retain paint, sculpting and compiled ownership")
{
  auto fixture = MapDocumentFixture{};
  auto config = mdl::MapFixtureConfig{.mapFormat = mdl::MapFormat::Valve};
  auto& map = fixture.create(config).map();
  mdl::selectAllNodes(map);
  mdl::removeSelectedNodes(map);
  const auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, map.worldBounds()};
  auto* source = new mdl::BrushNode{requireValue(
    builder.createCuboid(vm::bbox3d{{0, -64, -64}, {64, 0, 0}}, "TERR_GRASS"))};
  mdl::addNodes(map, {{map.editorContext().currentLayer(), {source}}});
  mdl::selectNodes(map, {source});
  auto* group = mdl::groupSelectedNodes(map, "Terrain");
  REQUIRE(group);
  requireValue(convertTerrainToHeightField(map, *group, 32));
  auto region = requireValue(TerrainRegion::decode(group->group().terrainPaint()));
  region.paint({20, 20}, 30, 1);
  auto content = group->group();
  content.setTerrainPaint(region.encode());
  REQUIRE(mdl::updateNodeContents(map, "Paint", {{group, mdl::NodeContents{content}}}));
  region = requireValue(TerrainRegion::decode(group->group().terrainPaint()));

  auto brushes = requireValue(terrainBrushNodes(*group));
  REQUIRE(brushes.size() == 8);
  const auto definition = mdl::EntityDefinition{
    "func_detail", RgbB{255, 255, 255}, "Detail brushes", {}, std::nullopt};
  mdl::openGroup(map, *group);
  mdl::selectNodes(map, {brushes[0], brushes[1]});
  auto* first = mdl::createBrushEntity(map, definition);
  REQUIRE(first);
  REQUIRE(mdl::setEntityProperty(map, "zhlt_detaillevel", "1"));
  mdl::deselectAll(map);
  mdl::selectNodes(map, {brushes[5]});
  auto* second = mdl::createBrushEntity(map, definition);
  REQUIRE(second);
  REQUIRE(first != second);
  CHECK(terrainSurfaces(*group, region).is_success());
  map.undoCommand();
  CHECK(terrainSurfaces(*group, region).is_success());
  map.redoCommand();
  second = dynamic_cast<mdl::EntityNode*>(brushes[5]->parent());
  REQUIRE(second);

  auto height = *region.heightField;
  height.heights[height.index(1, 1)] = 16;
  requireValue(updateTerrainHeightField(map, *group, height));
  const auto sculpted =
    requireValue(TerrainRegion::decode(group->group().terrainPaint()));
  CHECK(sculpted.weights == region.weights);
  REQUIRE(terrainSurfaces(*group, sculpted).is_success());
  CHECK(brushes[0]->parent() == first);
  CHECK(brushes[5]->parent() == second);
  map.undoCommand();
  CHECK(terrainSurfaces(*group, region).is_success());
  map.redoCommand();
  CHECK(terrainSurfaces(*group, sculpted).is_success());

  auto repainted = sculpted;
  repainted.paint({25, 25}, 30, 2);
  content = group->group();
  content.setTerrainPaint(repainted.encode());
  REQUIRE(mdl::updateNodeContents(map, "Repaint", {{group, mdl::NodeContents{content}}}));
  repainted = requireValue(TerrainRegion::decode(group->group().terrainPaint()));
  CHECK(repainted.weights != region.weights);
  mdl::closeGroup(map);

  auto directory = QTemporaryDir{};
  REQUIRE(directory.isValid());
  const auto path = pathFromQString(directory.filePath("entity-terrain.map"));
  requireValue(map.saveTo(path));
  const auto sourceBytes = readBytes(path);
  auto logger = NullLogger{};
  auto status = SimpleParserStatus{logger};
  auto tasks = kdl::task_manager{2};
  const auto parse = [&](const std::vector<std::uint8_t>& bytes) {
    const auto text =
      std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    return requireValue(mdl::WorldReader{text, mdl::MapFormat::Valve, {}}.read(
      map.worldBounds(), status, tasks));
  };
  auto restoredWorld = parse(sourceBytes);
  const auto findTerrain = [&](const auto& self, mdl::Node& node) -> mdl::GroupNode* {
    if (auto* candidate = dynamic_cast<mdl::GroupNode*>(&node);
        candidate && !candidate->group().terrainPaint().empty())
      return candidate;
    for (auto* child : node.children())
      if (auto* candidate = self(self, *child))
        return candidate;
    return nullptr;
  };
  auto* restoredGroup = findTerrain(findTerrain, *restoredWorld);
  REQUIRE(restoredGroup);
  const auto restored =
    requireValue(TerrainRegion::decode(restoredGroup->group().terrainPaint()));
  CHECK(restored.weights == repainted.weights);
  CHECK(restored.heightField->heights == height.heights);
  CHECK(terrainSurfaces(*restoredGroup, restored).is_success());

  const auto snapshot = requireValue(bakeTerrainSnapshot(sourceBytes));
  REQUIRE(snapshot);
  CHECK_FALSE(snapshot->textures.empty());
  auto compiledWorld = parse(snapshot->map);
  auto entities = std::vector<const mdl::EntityNode*>{};
  auto worldBrushes = size_t{0};
  auto totalBrushes = size_t{0};
  const auto collect = [&](const auto& self, const mdl::Node& node) -> void {
    if (const auto* entity = dynamic_cast<const mdl::EntityNode*>(&node);
        entity && entity->entity().hasProperty("classname", "func_detail"))
      entities.push_back(entity);
    if (const auto* brush = dynamic_cast<const mdl::BrushNode*>(&node))
    {
      ++totalBrushes;
      if (!dynamic_cast<const mdl::EntityNode*>(brush->parent()))
        ++worldBrushes;
      const auto top = std::ranges::find_if(brush->brush().faces(), [](const auto& face) {
        return face.normal().z() > 1e-6;
      });
      REQUIRE(top != brush->brush().faces().end());
      CHECK(snapshot->textures.contains(top->materialName()));
    }
    for (const auto* child : node.children())
      self(self, *child);
  };
  collect(collect, *compiledWorld);
  REQUIRE(entities.size() == 2);
  CHECK(std::ranges::any_of(entities, [](const auto* entity) {
    return entity->entity().hasProperty("zhlt_detaillevel", "1");
  }));
  CHECK(std::ranges::all_of(
    entities, [](const auto* entity) { return entity->hasChildren(); }));
  CHECK(worldBrushes > 0);
  CHECK(snapshot->topFaces == totalBrushes);
}

TEST_CASE("Terrain converts existing brush entities without losing their ownership")
{
  auto fixture = MapDocumentFixture{};
  auto config = mdl::MapFixtureConfig{.mapFormat = mdl::MapFormat::Valve};
  auto& map = fixture.create(config).map();
  mdl::selectAllNodes(map);
  mdl::removeSelectedNodes(map);
  const auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, map.worldBounds()};
  auto* left = new mdl::BrushNode{
    requireValue(builder.createCuboid(vm::bbox3d{{0, -64, -64}, {64, 0, 0}}, "LEFT"))};
  auto* right = new mdl::BrushNode{
    requireValue(builder.createCuboid(vm::bbox3d{{64, -64, -64}, {128, 0, 0}}, "RIGHT"))};
  mdl::addNodes(map, {{map.editorContext().currentLayer(), {left, right}}});
  mdl::selectNodes(map, {left, right});
  auto* group = mdl::groupSelectedNodes(map, "Terrain");
  REQUIRE(group);
  mdl::openGroup(map, *group);
  mdl::selectNodes(map, {left});
  const auto definition = mdl::EntityDefinition{
    "func_detail", RgbB{255, 255, 255}, "Detail brushes", {}, std::nullopt};
  auto* entity = mdl::createBrushEntity(map, definition);
  REQUIRE(entity);

  const auto region = requireValue(createTerrainRegion(*group));
  CHECK(createTerrainHeightField(*group, region, 32).is_success());
  requireValue(convertTerrainToHeightField(map, *group, 32));
  REQUIRE(entity->children().size() == 8);
  REQUIRE(requireValue(terrainBrushNodes(*group)).size() == 16);
  const auto converted =
    requireValue(TerrainRegion::decode(group->group().terrainPaint()));
  CHECK(terrainSurfaces(*group, converted).is_success());
  for (const auto* child : entity->children())
  {
    const auto* brush = dynamic_cast<const mdl::BrushNode*>(child);
    REQUIRE(brush);
    const auto top = std::ranges::find_if(
      brush->brush().faces(), [](const auto& face) { return face.normal().z() > 1e-6; });
    REQUIRE(top != brush->brush().faces().end());
    CHECK(top->materialName() == "LEFT");
  }
  map.undoCommand();
  REQUIRE(entity->children().size() == 1);
  CHECK(entity->children().front() == left);
  CHECK(right->parent() == group);
  map.redoCommand();
  CHECK(entity->children().size() == 8);
  CHECK(terrainSurfaces(*group, converted).is_success());
}

TEST_CASE("Chunked terrain supports brush entities across sculpt and bake")
{
  auto fixture = MapDocumentFixture{};
  auto config = mdl::MapFixtureConfig{.mapFormat = mdl::MapFormat::Valve};
  auto& map = fixture.create(config).map();
  mdl::selectAllNodes(map);
  mdl::removeSelectedNodes(map);
  requireValue(createTerrainChunkRectangle(map, {0, -64}, {1408, 0}, 0, 64, 64));
  REQUIRE(map.selection().groups.size() == 1);
  auto* root = map.selection().groups.front();
  REQUIRE(root->children().size() == 2);
  auto* chunk = dynamic_cast<mdl::GroupNode*>(root->children().front());
  REQUIRE(chunk);
  mdl::openGroup(map, *root);
  mdl::openGroup(map, *chunk);
  mdl::selectNodes(map, {chunk->children().back()});
  const auto definition = mdl::EntityDefinition{
    "func_detail", RgbB{255, 255, 255}, "Detail brushes", {}, std::nullopt};
  auto* entity = mdl::createBrushEntity(map, definition);
  REQUIRE(entity);
  mdl::closeGroup(map);
  mdl::closeGroup(map);

  auto chunks = requireValue(readTerrainChunks(*root));
  auto height = *chunks.combined->heightField;
  const auto seam = height.index(20, 1);
  height.heights[seam] = 16;
  requireValue(updateTerrainHeightField(map, *root, height));
  chunks = requireValue(readTerrainChunks(*root));
  CHECK(chunks.combined->heightField->heights[seam] == 16);
  CHECK(entity->hasChildren());
  for (const auto& part : chunks.chunks)
  {
    const auto* child =
      static_cast<const mdl::GroupNode*>(root->children()[part.childIndex]);
    CHECK(terrainSurfaces(*child, part.region).is_success());
  }

  auto directory = QTemporaryDir{};
  REQUIRE(directory.isValid());
  const auto path = pathFromQString(directory.filePath("chunked-entity-terrain.map"));
  requireValue(map.saveTo(path));
  const auto snapshot = requireValue(bakeTerrainSnapshot(readBytes(path)));
  REQUIRE(snapshot);
  CHECK_FALSE(snapshot->textures.empty());
  const auto text = std::string{snapshot->map.begin(), snapshot->map.end()};
  CHECK(text.find("\"classname\" \"func_detail\"") != std::string::npos);
}

TEST_CASE("Sculptable terrain survives map round trip and compiles its height field")
{
  auto fixture = MapDocumentFixture{};
  auto config = mdl::MapFixtureConfig{.mapFormat = mdl::MapFormat::Valve};
  auto& map = fixture.create(config).map();
  mdl::selectAllNodes(map);
  mdl::removeSelectedNodes(map);
  auto height = requireValue(TerrainHeightField::create(2, 2, 64, -64, 0));
  height.heights[height.index(1, 1)] = 24;
  auto brushes = requireValue(createTerrainBrushes(
    height,
    {0, 0},
    vm::mat4x4d::identity(),
    mdl::MapFormat::Valve,
    map.worldBounds(),
    "TERR_GRASS"));
  auto nodes = std::vector<mdl::Node*>{};
  for (auto& brush : brushes)
    nodes.push_back(new mdl::BrushNode{std::move(brush)});
  mdl::addNodes(map, {{map.editorContext().currentLayer(), nodes}});
  mdl::selectNodes(map, nodes);
  auto* group = mdl::groupSelectedNodes(map, "Sculpted terrain");
  REQUIRE(group);
  auto region = requireValue(createTerrainRegion(*group));
  region.heightField = height;
  region.paint({32, 32}, 64, 1, 1, 0.5);
  REQUIRE(terrainSurfaces(*group, region).is_success());
  auto content = group->group();
  content.setTerrainPaint(region.encode());
  REQUIRE(mdl::updateNodeContents(
    map, "Set sculpted terrain", {{group, mdl::NodeContents{std::move(content)}}}));

  auto directory = QTemporaryDir{};
  REQUIRE(directory.isValid());
  const auto path = pathFromQString(directory.filePath("sculpted.map"));
  requireValue(map.saveTo(path));
  const auto bytes = readBytes(path);
  auto logger = NullLogger{};
  auto status = SimpleParserStatus{logger};
  auto tasks = kdl::task_manager{2};
  const auto text =
    std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
  auto world = requireValue(mdl::WorldReader{text, mdl::MapFormat::Valve, {}}.read(
    map.worldBounds(), status, tasks));
  auto* loadedGroup = static_cast<mdl::GroupNode*>(nullptr);
  const auto collect = [&](const auto& self, mdl::Node& node) -> void {
    if (auto* candidate = dynamic_cast<mdl::GroupNode*>(&node);
        candidate && !candidate->group().terrainPaint().empty())
      loadedGroup = candidate;
    for (auto* child : node.children())
      self(self, *child);
  };
  collect(collect, *world);
  REQUIRE(loadedGroup);
  const auto restored =
    requireValue(TerrainRegion::decode(loadedGroup->group().terrainPaint()));
  REQUIRE(restored.heightField);
  CHECK(restored.heightField->heights == height.heights);
  CHECK(restored.encode() == region.encode());
  REQUIRE(terrainSurfaces(*loadedGroup, restored).is_success());
  const auto snapshot = requireValue(bakeTerrainSnapshot(bytes));
  REQUIRE(snapshot);
  CHECK(snapshot->topFaces >= brushes.size());
  CHECK_FALSE(snapshot->textures.empty());

  auto inconsistent = restored;
  inconsistent.heightField->heights[inconsistent.heightField->index(1, 1)] += 1;
  CHECK(terrainSurfaces(*loadedGroup, inconsistent).is_error());
}

TEST_CASE("Terrain supports multiple upward faces without duplicating volume")
{
  auto fixture = MapDocumentFixture{};
  auto config = mdl::MapFixtureConfig{.mapFormat = mdl::MapFormat::Valve};
  auto& map = fixture.create(config).map();
  mdl::selectAllNodes(map);
  mdl::removeSelectedNodes(map);
  const auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, map.worldBounds()};
  auto* brush = new mdl::BrushNode{requireValue(builder.createBrush(
    std::vector<vm::vec3d>{
      {0, 0, 0},
      {256, 0, 0},
      {256, -256, 0},
      {0, -256, 64},
      {0, 0, -64},
      {256, 0, -64},
      {256, -256, -64},
      {0, -256, -64}},
    "TERR_GRASS"))};
  mdl::addNodes(map, {{map.editorContext().currentLayer(), {brush}}});
  mdl::selectNodes(map, {brush});
  auto* group = mdl::groupSelectedNodes(map, "Terrain");
  REQUIRE(group);

  auto region = requireValue(createTerrainRegion(*group));
  const auto surfaces = requireValue(terrainSurfaces(*group, region));
  REQUIRE(surfaces.size() == 2);
  region.paint({96, 96}, 64, 1);
  auto content = group->group();
  content.setTerrainPaint(region.encode());
  REQUIRE(mdl::updateNodeContents(
    map, "Paint terrain", {{group, mdl::NodeContents{std::move(content)}}}));

  auto directory = QTemporaryDir{};
  REQUIRE(directory.isValid());
  const auto path = pathFromQString(directory.filePath("multi-top.map"));
  requireValue(map.saveTo(path));
  const auto snapshot = requireValue(bakeTerrainSnapshot(readBytes(path)));
  REQUIRE(snapshot);

  auto logger = NullLogger{};
  auto status = SimpleParserStatus{logger};
  auto tasks = kdl::task_manager{2};
  const auto text = std::string_view{
    reinterpret_cast<const char*>(snapshot->map.data()), snapshot->map.size()};
  auto world = requireValue(mdl::WorldReader{text, mdl::MapFormat::Valve, {}}.read(
    map.worldBounds(), status, tasks));
  auto output = std::vector<const mdl::BrushNode*>{};
  const auto collect = [&](const auto& self, const mdl::Node& node) -> void {
    if (const auto* found = dynamic_cast<const mdl::BrushNode*>(&node))
      output.push_back(found);
    for (const auto* child : node.children())
      self(self, *child);
  };
  collect(collect, *world);
  REQUIRE(output.size() >= 2);

  auto topOutlines = std::vector<QPolygonF>{};
  auto area = 0.0;
  for (const auto* piece : output)
  {
    for (const auto& vertex : piece->brush().vertexPositions())
      for (const auto& face : brush->brush().faces())
        CHECK(face.boundary().point_distance(vertex) < 1e-4);
    for (const auto& face : piece->brush().faces())
    {
      if (face.normal().z() <= 1e-6)
        continue;
      auto outline = QPolygonF{};
      for (const auto& vertex : face.vertexPositions())
      {
        const auto p = region.project(vertex);
        outline.append(QPointF{p.x(), p.y()});
      }
      for (const auto& other : topOutlines)
        if (outline.boundingRect().intersects(other.boundingRect()))
          CHECK(terrainArea(outline.intersected(other)) < 1e-4);
      area += terrainArea(outline);
      topOutlines.push_back(std::move(outline));
    }
  }
  auto sourceArea = 0.0;
  for (const auto& surface : surfaces)
    sourceArea += terrainArea(surface.outline);
  CHECK(std::abs(area - sourceArea) < 1e-3);
}

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
  CHECK(std::ranges::none_of(shared.textures, [](const auto& texture) {
    return texture.name == "tm_grass_dirt" || texture.name == "tm_grass_rock"
           || texture.name == "tm_dirt_rock";
  }));

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

TEST_CASE("Terrain baker uses packaged transitions for uniform sources")
{
  auto images = std::array<QImage, 3>{
    QImage{128, 128, QImage::Format_RGBA8888},
    QImage{128, 128, QImage::Format_RGBA8888},
    QImage{128, 128, QImage::Format_RGBA8888}};
  images[0].fill(QColor{220, 40, 20});
  images[1].fill(QColor{30, 180, 40});
  images[2].fill(QColor{20, 40, 210});
  const auto prepared = requireValue(prepareTerrainMaterial(images));
  const auto packaged =
    decodeTerrainMaterialPackage(encodeTerrainMaterialPackage(prepared), prepared.images);
  REQUIRE(packaged);
  const auto pairs = std::array{
    std::pair{std::array{0, 1}, "tm_grass_dirt"},
    std::pair{std::array{0, 2}, "tm_grass_rock"},
    std::pair{std::array{1, 2}, "tm_dirt_rock"}};

  for (auto pair = size_t{0}; pair < pairs.size(); ++pair)
  {
    const auto [slotPair, name] = pairs[pair];
    auto region = TerrainRegion{vm::vec2d{0, 0}};
    for (auto row = 0; row <= TerrainRegion::Rows; ++row)
      for (auto col = 0; col <= TerrainRegion::Columns; ++col)
      {
        const auto weight = std::clamp((double(row) - 2.0) * 0.3, 0.0, 1.0);
        auto weights = TerrainRegion::Weight{};
        weights[size_t(slotPair[0])] = 1.0 - weight;
        weights[size_t(slotPair[1])] = weight;
        region.weights[size_t(row * (TerrainRegion::Columns + 1) + col)] = weights;
      }

    const auto shared =
      requireValue(runTerrainBaker(region, &*packaged, TerrainBakeMode::Shared));
    const auto local =
      requireValue(runTerrainBaker(region, &*packaged, TerrainBakeMode::Local));
    const auto texture = std::ranges::find_if(
      shared.textures, [&](const auto& candidate) { return candidate.name == name; });
    REQUIRE(texture != shared.textures.end());
    for (auto mip = size_t{0}; mip < 4; ++mip)
    {
      const auto offset = size_t(readU32(texture->miptex, 24 + mip * 4));
      const auto& expected = packaged->pairLevels[pair][mip];
      REQUIRE(offset + expected.size() <= texture->miptex.size());
      CHECK(std::equal(
        expected.begin(),
        expected.end(),
        texture->miptex.begin() + std::ptrdiff_t(offset)));
    }

    const auto materialIndex = size_t(std::distance(shared.textures.begin(), texture));
    auto compared = size_t{0};
    for (const auto& face : shared.faces)
    {
      if (face.kind != TerrainFaceKind::Shared || face.material != materialIndex)
        continue;
      const auto bounds = face.polygon.boundingRect();
      const auto points = std::array{
        bounds.center() + QPointF{0.125, -0.125},
        QPointF{bounds.left() + 0.125, bounds.top() + 0.375},
        QPointF{bounds.right() - 0.125, bounds.bottom() - 0.375},
        QPointF{bounds.left() + 0.125, bounds.bottom() - 0.375},
        QPointF{bounds.right() - 0.125, bounds.top() + 0.375}};
      for (const auto& point : points)
      {
        if (!face.polygon.containsPoint(point, Qt::OddEvenFill))
          continue;
        const auto original =
          std::ranges::find_if(local.faces, [&](const auto& candidate) {
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

    // The blend ceases to be affine past row 5. Nearby faces must not use the
    // fixed ramp because their MIP filter can reach that change in gradient.
    const auto kink = QPointF{300.125, 108.375};
    const auto kinkFace = std::ranges::find_if(shared.faces, [&](const auto& candidate) {
      return candidate.polygon.containsPoint(kink, Qt::OddEvenFill);
    });
    REQUIRE(kinkFace != shared.faces.end());
    CHECK(kinkFace->material != materialIndex);

    auto withoutLevels = *packaged;
    withoutLevels.baseLevels = {};
    withoutLevels.pairLevels = {};
    const auto generated =
      requireValue(runTerrainBaker(region, &withoutLevels, TerrainBakeMode::Shared));
    REQUIRE(generated.textures.size() == shared.textures.size());
    for (auto index = size_t{0}; index < shared.textures.size(); ++index)
      CHECK(generated.textures[index].miptex == shared.textures[index].miptex);
  }

  auto changed = *packaged;
  changed.images[0].setPixelColor(0, 0, Qt::white);
  auto withoutLevels = changed;
  withoutLevels.baseLevels = {};
  withoutLevels.pairLevels = {};
  const auto region = TerrainRegion{vm::vec2d{0, 0}};
  const auto rebuilt = requireValue(runTerrainBaker(region, &changed));
  const auto generated = requireValue(runTerrainBaker(region, &withoutLevels));
  REQUIRE(rebuilt.textures.size() == generated.textures.size());
  for (auto index = size_t{0}; index < rebuilt.textures.size(); ++index)
    CHECK(rebuilt.textures[index].miptex == generated.textures[index].miptex);

  auto repaletted = *packaged;
  repaletted.palette.fill(0);
  withoutLevels = repaletted;
  withoutLevels.baseLevels = {};
  withoutLevels.pairLevels = {};
  const auto rebuiltPalette = requireValue(runTerrainBaker(region, &repaletted));
  const auto generatedPalette = requireValue(runTerrainBaker(region, &withoutLevels));
  REQUIRE(rebuiltPalette.textures.size() == generatedPalette.textures.size());
  for (auto index = size_t{0}; index < rebuiltPalette.textures.size(); ++index)
    CHECK(
      rebuiltPalette.textures[index].miptex == generatedPalette.textures[index].miptex);
}

TEST_CASE("Terrain baker packs only patches covered by terrain")
{
  auto region = TerrainRegion{vm::vec2d{0, 0}};
  region.patchDensityPercent = 50;
  region.weights.fill({0.3, 0.3, 0.4});
  auto outline = QPolygonF{};
  outline << QPointF{0, 0} << QPointF{96, 0} << QPointF{96, 96} << QPointF{0, 96};
  const auto coverage = std::vector<QPolygonF>{outline};

  const auto full = requireValue(runTerrainBaker(region));
  const auto clipped =
    requireValue(runTerrainBaker(region, nullptr, TerrainBakeMode::Shared, &coverage));
  const auto bytes = [](const auto& bake) {
    auto result = size_t{0};
    for (const auto& texture : bake.textures)
      result += texture.miptex.size();
    return result;
  };
  CHECK(bytes(clipped) < bytes(full));
  REQUIRE(std::ranges::all_of(clipped.faces, [&](const auto& face) {
    return face.material < clipped.textures.size();
  }));

  auto compared = size_t{0};
  for (auto y = 2.25; y < 96; y += 7)
    for (auto x = 2.25; x < 96; x += 7)
    {
      const auto point = QPointF{x, y};
      const auto findFace = [&](const auto& bake) {
        return std::ranges::find_if(bake.faces, [&](const auto& face) {
          return face.polygon.containsPoint(point, Qt::OddEvenFill);
        });
      };
      const auto original = findFace(full);
      const auto packed = findFace(clipped);
      REQUIRE(original != full.faces.end());
      REQUIRE(packed != clipped.faces.end());
      for (auto mip = 0; mip < 4; ++mip)
        CHECK(
          sampleIndex(full, *original, x, y, mip)
          == sampleIndex(clipped, *packed, x, y, mip));
      ++compared;
    }
  CHECK(compared > 0);
}

TEST_CASE("Project terrain reuses identical textures across groups")
{
  auto directory = QTemporaryDir{};
  REQUIRE(directory.isValid());
  auto fixture = MapDocumentFixture{};
  auto config = mdl::MapFixtureConfig{.mapFormat = mdl::MapFormat::Valve};
  config.gameInfo.gameConfig.name = "Half-Life";
  auto& map = fixture.create(config).map();
  mdl::selectAllNodes(map);
  mdl::removeSelectedNodes(map);
  const auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, map.worldBounds()};
  auto groups = std::vector<mdl::GroupNode*>{};
  for (auto i = 0; i < 2; ++i)
  {
    const auto x = double(i * 256);
    auto* brush = new mdl::BrushNode{requireValue(
      builder.createCuboid(vm::bbox3d{{x, -128, -64}, {x + 128, 0, 0}}, "TEST"))};
    mdl::addNodes(map, {{map.editorContext().currentLayer(), {brush}}});
    mdl::deselectAll(map);
    mdl::selectNodes(map, {brush});
    auto* group = mdl::groupSelectedNodes(map, "Terrain " + std::to_string(i));
    REQUIRE(group);
    auto region = requireValue(createTerrainRegion(*group));
    region.weights.fill({0.3, 0.3, 0.4});
    auto content = group->group();
    content.setTerrainPaint(region.encode());
    REQUIRE(mdl::updateNodeContents(
      map, "Paint terrain", {{group, mdl::NodeContents{std::move(content)}}}));
    groups.push_back(group);
  }

  const auto path = pathFromQString(directory.path()) / "groups.map";
  requireValue(map.saveTo(path));
  const auto estimate = requireValue(estimateTerrainGroup(*groups[0]));
  const auto snapshot = requireValue(bakeTerrainSnapshot(readBytes(path)));
  REQUIRE(snapshot);
  REQUIRE(estimate.textures == 1);
  CHECK(snapshot->textures.size() == estimate.textures);
  CHECK(snapshot->topFaces == estimate.generatedFaces * 2);
  const auto text = std::string{snapshot->map.begin(), snapshot->map.end()};
  CHECK(text.find(" tbt0_") != std::string::npos);
  CHECK(text.find(" tbt1_") == std::string::npos);

  auto changed = requireValue(TerrainRegion::decode(groups[1]->group().terrainPaint()));
  changed.weights.fill({0.2, 0.3, 0.5});
  auto content = groups[1]->group();
  content.setTerrainPaint(changed.encode());
  REQUIRE(mdl::updateNodeContents(
    map, "Change terrain", {{groups[1], mdl::NodeContents{std::move(content)}}}));
  requireValue(map.saveTo(path));
  const auto different = requireValue(bakeTerrainSnapshot(readBytes(path)));
  REQUIRE(different);
  CHECK(different->textures.size() == 2);
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
  constexpr auto third = 1.0 / 3.0;
  constexpr auto rowWeights = std::array{
    0.0, 0.0, 0.0,   third,     2 * third, 1.0, 1.0, 1.0,       2 * third, third, 0.0,
    0.0, 0.0, third, 2 * third, 1.0,       1.0, 1.0, 2 * third, third,     0.0};
  for (auto row = 0; row <= TerrainRegion::Rows; ++row)
    for (auto col = 0; col <= TerrainRegion::Columns; ++col)
    {
      const auto weight = rowWeights[size_t(row)];
      region.weights[size_t(row * (TerrainRegion::Columns + 1) + col)] = {
        1.0 - weight, weight, 0};
    }

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
    const auto smallCoverage = std::vector<QPolygonF>{smallSurfaces.front().outline};
    const auto smallShared = requireValue(
      runTerrainBaker(smallRegion, &material, TerrainBakeMode::Shared, &smallCoverage));
    const auto smallLocal = requireValue(
      runTerrainBaker(smallRegion, &material, TerrainBakeMode::Local, &smallCoverage));
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

  auto bands = region;
  for (auto row = 0; row <= TerrainRegion::Rows; ++row)
    for (auto col = 0; col <= TerrainRegion::Columns; ++col)
    {
      const auto weight = rowWeights[size_t(row)];
      bands.weights[size_t(row * (TerrainRegion::Columns + 1) + col)] = {
        1.0 - weight, weight, 0};
    }
  saveRegion(bands);
  const auto encodedBands = requireValue(TerrainRegion::decode(bands.encode()));
  const auto bandPlan =
    requireValue(runTerrainBaker(encodedBands, &material, TerrainBakeMode::Shared));
  REQUIRE(std::ranges::any_of(bandPlan.textures, [](const auto& texture) {
    return texture.name == "tm_grass_dirt";
  }));
  const auto bandEstimate = requireValue(estimateTerrainGroup(*group, &session->store()));
  const auto bandSnapshot =
    requireValue(bakeTerrainSnapshot(readBytes(source), &session->store()));
  REQUIRE(bandSnapshot);
  CHECK(bandSnapshot->topFaces == bandEstimate.generatedFaces);
  const auto packaged =
    decodeTerrainMaterialPackage(encodeTerrainMaterialPackage(material), material.images);
  REQUIRE(packaged);
  CHECK(std::ranges::any_of(bandSnapshot->textures, [&](const auto& item) {
    const auto& bytes = item.second;
    if (bytes.size() < 40)
      return false;
    for (auto mip = size_t{0}; mip < 4; ++mip)
    {
      const auto offset = size_t(readU32(bytes, 24 + mip * 4));
      const auto& expected = packaged->pairLevels[0][mip];
      if (offset > bytes.size() || expected.size() > bytes.size() - offset)
        return false;
      if (!std::equal(
            expected.begin(), expected.end(), bytes.begin() + std::ptrdiff_t(offset)))
        return false;
    }
    return true;
  }));
  if (const auto output = qEnvironmentVariable("TB_TERRAIN_PAIR_ACCEPTANCE_PROJECT");
      !output.isEmpty())
  {
    auto input = project::AssetImport{};
    input.type = project::AssetType::Map;
    input.virtualPath = "maps/bands.map";
    input.displayName = "Bands";
    input.original = input.compatible = readBytes(source);
    requireValue(session->importAsset(input));
    requireValue(session->save());
    requireValue(session->saveCopy(pathFromQString(output)));
  }

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

TEST_CASE("Project terrain textured slope survives clipping and compile snapshots")
{
  auto directory = QTemporaryDir{};
  REQUIRE(directory.isValid());
  const auto root = pathFromQString(directory.path());
  const auto projectPath = root / "terrain-slope.tbproj";
  auto session =
    requireValue(ProjectSession::create(projectPath, "Terrain Slope", "Half-Life"));

  // Procedural, tileable source images keep the fixture self-contained while exposing
  // the alignment and mipmapping problems that uniform-color sources cannot reveal.
  const auto names = std::array{"TERR_GRASS", "TERR_DIRT", "TERR_ROCK", "TERR_WALL"};
  const auto sizes = std::array{64, 128, 256, 64};
  const auto colors = std::array{
    std::array{68, 91, 43},
    std::array{119, 88, 58},
    std::array{112, 108, 98},
    std::array{116, 114, 106}};
  auto textureIds = std::array<std::string, 3>{};
  constexpr auto pi = 3.14159265358979323846;
  for (auto slot = size_t{0}; slot < names.size(); ++slot)
  {
    const auto size = sizes[slot];
    auto pixels = std::vector<unsigned char>(size_t(size * size * 4), 255);
    for (auto y = 0; y < size; ++y)
      for (auto x = 0; x < size; ++x)
      {
        const auto u = double(x) / size;
        const auto v = double(y) / size;
        const auto noise = std::uint32_t(x) * 73856093u ^ std::uint32_t(y) * 19349663u
                           ^ std::uint32_t(slot) * 83492791u;
        const auto grain = int((noise ^ (noise >> 13)) % 17u) - 8;
        const auto shade = int(std::lround(
                             12 * std::sin(2 * pi * (3 * u + 2 * v))
                             + 7 * std::sin(2 * pi * (7 * u - 5 * v))
                             + 4 * std::sin(2 * pi * (13 * u + 11 * v))))
                           + grain;
        const auto offset = size_t((y * size + x) * 4);
        for (auto channel = size_t{0}; channel < 3; ++channel)
          pixels[offset + channel] =
            static_cast<unsigned char>(std::clamp(colors[slot][channel] + shade, 0, 255));
      }
    const auto converted = requireValue(
      img::convertGoldSrcTexture({size_t(size), size_t(size), pixels}, names[slot]));
    auto input = project::AssetImport{};
    input.type = project::AssetType::Texture;
    input.virtualPath = "textures/" + std::string{names[slot]} + ".wad";
    input.displayName = names[slot];
    input.original = input.compatible = converted.wad;
    const auto texture = requireValue(session->importAsset(input));
    if (slot < textureIds.size())
      textureIds[slot] = texture.id;
  }
  const auto recipe = std::string{
    R"({"format":"goldsrc-terrain-material","version":1,"slots":[{"name":"Grass"},{"name":"Dirt"},{"name":"Rock"}]})"};
  auto materialInput = project::AssetImport{};
  materialInput.type = project::AssetType::Material;
  materialInput.virtualPath = "materials/terrain-slope.tbmat";
  materialInput.displayName = "Grass, dirt and rock";
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
  auto* ramp = new mdl::BrushNode{requireValue(builder.createBrush(
    std::vector<vm::vec3d>{
      {0, 0, 0},
      {0, -640, 0},
      {1280, 0, 80},
      {1280, -640, 80},
      {0, 0, -64},
      {0, -640, -64},
      {1280, 0, -64},
      {1280, -640, -64}},
    "TERR_GRASS"))};
  mdl::addNodes(map, {{map.editorContext().currentLayer(), {ramp}}});
  mdl::selectNodes(map, {ramp});
  auto* group = mdl::groupSelectedNodes(map, "Terrain");
  REQUIRE(group);
  auto region = requireValue(createTerrainRegion(*group));
  region.materialAssetId = materialAsset.id;
  for (auto x = 96; x <= 864; x += 6)
    region.paint(vm::vec2d{double(x), 240 + 30 * std::sin(pi * x / 480.0)}, 70, 1, 0.65);
  for (auto x = 360; x <= 600; x += 6)
    region.paint(vm::vec2d{double(x), 190}, 52, 2, 0.38);
  for (auto row = 8; row <= 10; ++row)
    for (auto col = 19; col <= 21; ++col)
      region.weights[size_t(row * (TerrainRegion::Columns + 1) + col)] = {
        0.35, 0.4, 0.25};
  REQUIRE(std::ranges::any_of(region.weights, [](const auto& weight) {
    return weight[0] > 0.1 && weight[1] > 0.1 && weight[2] > 0.1;
  }));
  const auto painted = region.encode();
  auto content = group->group();
  content.setTerrainPaint(painted);
  REQUIRE(mdl::updateNodeContents(
    map, "Paint terrain", {{group, mdl::NodeContents{std::move(content)}}}));

  auto walls = std::vector<mdl::Node*>{};
  for (const auto& bounds :
       {vm::bbox3d{{-32, -672, -64}, {0, 32, 224}},
        vm::bbox3d{{1280, -672, -64}, {1312, 32, 224}},
        vm::bbox3d{{0, -672, -64}, {1280, -640, 224}},
        vm::bbox3d{{0, 0, -64}, {1280, 32, 224}},
        vm::bbox3d{{-32, -672, 224}, {1312, 32, 256}}})
    walls.push_back(
      new mdl::BrushNode{requireValue(builder.createCuboid(bounds, "TERR_WALL"))});
  mdl::addNodes(map, {{map.editorContext().currentLayer(), std::move(walls)}});
  REQUIRE(mdl::paste(map, R"(
    { "classname" "info_player_start" "origin" "64 -300 48" "angle" "0" }
    { "classname" "info_player_deathmatch" "origin" "1160 -420 112" "angle" "180" }
    { "classname" "light" "origin" "160 -320 190" "_light" "255 240 215 420" }
    { "classname" "light" "origin" "560 -320 190" "_light" "255 240 215 420" }
    { "classname" "light" "origin" "960 -320 190" "_light" "255 240 215 420" }
  )") == mdl::PasteType::Node);

  auto assets = std::vector<std::pair<std::string, std::string>>{};
  const auto saveMap = [&](const std::string& name, const TerrainRegion& settings) {
    auto updated = group->group();
    updated.setTerrainPaint(settings.encode());
    REQUIRE(mdl::updateNodeContents(
      map, "Set terrain density", {{group, mdl::NodeContents{std::move(updated)}}}));
    const auto source = root / (name + ".map");
    requireValue(map.saveTo(source));
    const auto estimate = requireValue(estimateTerrainGroup(*group, &session->store()));
    const auto surfaces = requireValue(terrainSurfaces(*group, settings));
    CHECK(estimate.sourceFaces == surfaces.size());
    CHECK(estimate.generatedFaces > estimate.sourceFaces);
    CHECK(std::ranges::any_of(
      estimate.cuts, [](const auto& cut) { return cut.kind == TerrainFaceKind::Local; }));
    for (auto surface = size_t{0}; surface < surfaces.size(); ++surface)
    {
      auto area = 0.0;
      for (const auto& cut : estimate.cuts)
        if (cut.surfaceIndex == surface)
          area += terrainArea(cut.polygon);
      CHECK(std::abs(area - terrainArea(surfaces[surface].outline)) < 1e-4);
    }
    auto input = project::AssetImport{};
    input.type = project::AssetType::Map;
    input.virtualPath = "maps/" + name + ".map";
    input.displayName = name;
    input.original = input.compatible = readBytes(source);
    if (name == "Terrain100")
    {
      const auto first =
        requireValue(bakeTerrainSnapshot(input.original, &session->store()));
      const auto second =
        requireValue(bakeTerrainSnapshot(input.original, &session->store()));
      REQUIRE(first);
      REQUIRE(second);
      CHECK(first->topFaces == estimate.generatedFaces);
      CHECK(first->map == second->map);
      CHECK(first->textures == second->textures);
    }
    const auto asset = requireValue(session->importAsset(input));
    assets.emplace_back(name, asset.id);
  };
  saveMap("TerrainBefore", region);
  REQUIRE(group->childCount() == 1);

  mdl::openGroup(map, *group);
  mdl::selectNodes(map, {ramp});
  auto clip = ClipTool{document};
  REQUIRE(clip.activate());
  clip.addPoint(vm::vec3d{640, 0, 40}, {});
  clip.addPoint(vm::vec3d{640, -640, 40}, {});
  clip.addPoint(vm::vec3d{640, -640, -32}, {});
  REQUIRE(clip.canClip());
  clip.toggleSide();
  clip.performClip();
  clip.deactivate();
  REQUIRE(group->childCount() == 2);
  CHECK(group->group().terrainPaint() == painted);
  map.undoCommand();
  CHECK(group->childCount() == 1);
  map.redoCommand();
  REQUIRE(group->childCount() == 2);
  const auto upper = std::ranges::find_if(group->children(), [](const auto* child) {
    return child->logicalBounds().min.x() >= 640;
  });
  REQUIRE(upper != group->children().end());
  mdl::deselectAll(map);
  mdl::selectNodes(map, {*upper});
  auto flatten = ClipTool{document};
  REQUIRE(flatten.activate());
  flatten.addPoint(vm::vec3d{640, 0, 40}, {});
  flatten.addPoint(vm::vec3d{1280, -640, 40}, {});
  flatten.addPoint(vm::vec3d{640, -640, 40}, {});
  REQUIRE(flatten.canClip());
  flatten.performClip();
  flatten.deactivate();
  REQUIRE(group->childCount() == 2);
  CHECK(group->logicalBounds().max.z() == 40);
  CHECK(group->group().terrainPaint() == painted);
  map.undoCommand();
  CHECK(group->logicalBounds().max.z() == 80);
  map.redoCommand();
  CHECK(group->logicalBounds().max.z() == 40);
  mdl::closeGroup(map);

  for (const auto density : {50, 100, 200})
  {
    auto settings = region;
    settings.patchDensityPercent = density;
    saveMap("Terrain" + std::to_string(density), settings);
  }
  requireValue(session->save());
  auto wadSizes = std::array<size_t, 3>{};
  for (const auto& [name, id] : assets)
  {
    const auto cached = requireValue(session->store().terrainCompile(id));
    REQUIRE(cached);
    INFO(name << ": " << cached->error);
    REQUIRE(cached->error.empty());
    if (name == "Terrain50")
      wadSizes[0] = cached->wad.size();
    else if (name == "Terrain100")
      wadSizes[1] = cached->wad.size();
    else if (name == "Terrain200")
      wadSizes[2] = cached->wad.size();
    requireValue(session->exportCompileSnapshot(id, root / "saved" / name));
  }
  CHECK(wadSizes[0] < wadSizes[1]);
  CHECK(wadSizes[1] < wadSizes[2]);
  session.reset();
  session = requireValue(ProjectSession::open(projectPath));
  for (const auto& [name, id] : assets)
  {
    CHECK_FALSE(requireValue(session->terrainCompileNeedsSave(id)));
    requireValue(session->exportCompileSnapshot(id, root / "reopened" / name));
    CHECK(
      readBytes(root / "saved" / name / "map.map")
      == readBytes(root / "reopened" / name / "map.map"));
    CHECK(
      readBytes(root / "saved" / name / "textures.wad")
      == readBytes(root / "reopened" / name / "textures.wad"));
  }

  if (const auto output = qEnvironmentVariable("TB_TERRAIN_SLOPE_ACCEPTANCE_DIR");
      !output.isEmpty())
  {
    const auto destination = pathFromQString(output);
    REQUIRE_FALSE(std::filesystem::exists(destination));
    REQUIRE(std::filesystem::create_directories(destination));
    requireValue(session->saveCopy(destination / "terrain-slope.tbproj"));
    for (const auto& [name, id] : assets)
      requireValue(session->exportCompileSnapshot(id, destination / name));
  }
}
} // namespace tb::ui
