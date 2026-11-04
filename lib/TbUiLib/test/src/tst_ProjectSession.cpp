/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QBuffer>
#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QtTest/QSignalSpy>

#include "TestEnvironment.h"
#include "gl/MaterialManager.h"
#include "gl/PerspectiveCamera.h"
#include "img/DecodeImage.h"
#include "img/GoldSrcTexture.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushNode.h"
#include "mdl/CompilationProfile.h"
#include "mdl/EditorContext.h"
#include "mdl/GameFileSystem.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "mdl/MapHeader.h"
#include "mdl/Map_CopyPaste.h"
#include "mdl/Map_Entities.h"
#include "mdl/Map_Groups.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/NodeContents.h"
#include "mdl/PasteType.h"
#include "mdl/WorldNode.h"
#include "project/AssetLibrary.h"
#include "project/ContentHash.h"
#include "project/GoldSrcWad.h"
#include "project/LibrarySync.h"
#include "ui/CatalogAssetDrop.h"
#include "ui/ClipTool.h"
#include "ui/CompilationContext.h"
#include "ui/CompilationRunner.h"
#include "ui/CompilationVariables.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"
#include "ui/ProjectResourceAudit.h"
#include "ui/ProjectSession.h"
#include "ui/QPathUtils.h"
#include "ui/TerrainRegion.h"
#include "ui/automation/AutomationAssetPreview.h"
#include "ui/automation/AutomationAssets.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace tb::ui
{
namespace
{
template <typename T>
T requireProject(Result<T> result)
{
  if (result.is_error())
    FAIL(std::get<Error>(result.error()).msg);
  return std::move(result).value();
}
void requireProject(Result<void> result)
{
  if (result.is_error())
    FAIL(std::get<Error>(result.error()).msg);
}
std::vector<std::uint8_t> readProjectFile(const std::filesystem::path& path)
{
  auto file = QFile{pathAsQString(path)};
  REQUIRE(file.open(QIODevice::ReadOnly));
  const auto bytes = file.readAll();
  REQUIRE(file.error() == QFileDevice::NoError);
  return {bytes.begin(), bytes.end()};
}
project::AssetImport testAsset()
{
  auto asset = project::AssetImport{};
  asset.type = project::AssetType::Sound;
  asset.virtualPath = "sound/test.wav";
  asset.displayName = "Test sound";
  asset.original = {1, 2, 3};
  asset.compatible = asset.original;
  return asset;
}

// Builds a real GoldSrc miptex by converting an image, so palette and mip layout match
// what the editor produces for imported textures.
std::vector<std::uint8_t> miptexFor(
  const std::string& name, const std::size_t size, const unsigned char fill)
{
  auto converted = img::convertGoldSrcTexture(
    {size, size, std::vector<unsigned char>(size * size * 4, fill)}, name);
  REQUIRE(converted.is_success());
  auto archive = project::readGoldSrcWad(converted.value().wad);
  REQUIRE(archive.is_success());
  REQUIRE(archive.value().textures.size() == 1);
  return archive.value().textures.front().miptex;
}

void writeFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
{
  std::filesystem::create_directories(path.parent_path());
  auto file = std::ofstream{path, std::ios::binary};
  file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

// Minimal but real PNG payload, so an image dependency goes through the same decode and
// GoldSrc conversion path as an imported texture.
std::vector<std::uint8_t> pngBytes(const QColor color, const int size = 8)
{
  auto image = QImage{size, size, QImage::Format_RGBA8888};
  image.fill(color);
  auto buffer = QBuffer{};
  REQUIRE(buffer.open(QIODevice::WriteOnly));
  REQUIRE(image.save(&buffer, "PNG"));
  const auto bytes = buffer.data();
  return {bytes.begin(), bytes.end()};
}

// Structurally valid version 10 studio model declaring the given sequence group count.
// GoldSrc models embed their textures, so the only external resources are the separate
// texture file and the animation groups.
std::vector<std::uint8_t> modelWithGroups(const std::uint32_t sequenceGroups)
{
  constexpr auto headerSize = std::size_t{244};
  auto model = std::vector<std::uint8_t>(headerSize, 0);
  const auto writeU32 = [&](const std::size_t offset, const std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i)
      model[offset + i] = std::uint8_t((value >> (i * 8)) & 0xff);
  };
  writeU32(0, 0x54534449); // "IDST"
  writeU32(4, 10);
  writeU32(172, sequenceGroups);
  return model;
}

const project::LibraryAsset& libraryAssetNamed(
  const std::vector<project::LibraryAsset>& assets, const std::string& name)
{
  const auto it = std::ranges::find_if(
    assets, [&](const auto& asset) { return asset.sourceMember == name; });
  REQUIRE(it != assets.end());
  return *it;
}

const project::LibraryUpdate& updateFor(
  const std::vector<project::LibraryUpdate>& updates, const std::string& projectAssetId)
{
  const auto it = std::ranges::find_if(
    updates, [&](const auto& update) { return update.projectAssetId == projectAssetId; });
  REQUIRE(it != updates.end());
  return *it;
}

/** Temporary project plus asset library folder, wired together on demand. */
struct LibrarySandbox
{
  QTemporaryDir temporary;
  std::filesystem::path root;
  std::filesystem::path projectPath;
  std::filesystem::path libraryFolder;
  std::shared_ptr<ProjectSession> session;

  LibrarySandbox()
  {
    REQUIRE(temporary.isValid());
    root = pathFromQString(temporary.path());
    projectPath = root / "snapshot.tbproj";
    libraryFolder = root / "library";
    std::filesystem::create_directories(libraryFolder);
    session =
      requireProject(ProjectSession::create(projectPath, "Snapshot", "Half-Life"));
  }

  void writeWad(
    const std::vector<std::vector<std::uint8_t>>& miptex,
    const std::string& relativePath = "custom.wad") const
  {
    writeFile(
      libraryFolder / std::filesystem::path{relativePath},
      requireProject(project::buildGoldSrcWad(miptex)));
  }

  void write(
    const std::string& relativePath, const std::vector<std::uint8_t>& bytes) const
  {
    writeFile(libraryFolder / std::filesystem::path{relativePath}, bytes);
  }

  void createLibrary(const std::string& name = "Studio") const
  {
    auto library = requireProject(project::AssetLibrary::create(libraryFolder, name));
    requireProject(library->scan());
  }

  std::vector<project::LibraryAsset> libraryAssets() const
  {
    auto library = requireProject(project::AssetLibrary::open(libraryFolder));
    return requireProject(library->assets());
  }
  std::string libraryId() const
  {
    return requireProject(project::AssetLibrary::open(libraryFolder))->info().id;
  }
  void rescanLibrary() const
  {
    requireProject(requireProject(project::AssetLibrary::open(libraryFolder))->scan());
  }
};
} // namespace

TEST_CASE("ProjectSession")
{
  auto temporary = QTemporaryDir{};
  REQUIRE(temporary.isValid());
  const auto path = pathFromQString(temporary.path()) / "test.tbproj";
  auto session = requireProject(ProjectSession::create(path, "Test", "Quake"));

  SECTION("isolates unsaved content locks the source and saves portable copies")
  {
    CHECK_FALSE(session->modified());
    CHECK(ProjectSession::open(path).is_error());
    const auto input = testAsset();
    const auto asset = requireProject(session->importAsset(input));
    CHECK(session->modified());
    CHECK(requireProject(project::ProjectStore::open(path))->assets().value().empty());
    const auto copy = path.parent_path() / "copy.tbproj";
    requireProject(session->saveCopy(copy));
    CHECK(session->modified());
    CHECK(session->saveCopy(copy).is_error());
    auto copied = requireProject(project::ProjectStore::open(copy));
    CHECK(
      requireProject(copied->readAsset(asset.id, project::ContentKind::Compatible))
      == input.compatible);
    copied.reset();
    requireProject(session->save());
    CHECK_FALSE(session->modified());
    session.reset();
    session = requireProject(ProjectSession::open(path));
    CHECK(requireProject(session->store().findAsset(asset.id)) == asset);
  }

  SECTION("cleans unused revisions only when asked and saves the smaller project")
  {
    const auto asset = requireProject(session->importAsset(testAsset()));
    auto replacement = testAsset();
    replacement.original = {1, 2, 3, 4};
    replacement.compatible = replacement.original;
    requireProject(session->replaceAsset(asset.id, replacement));
    requireProject(session->save());
    CHECK_FALSE(session->modified());

    const auto plan = requireProject(session->unreferencedRevisions());
    REQUIRE(plan.revisions.size() == 1u);
    CHECK(plan.revisions.front().revision == 1u);
    CHECK(plan.contentBytes > 0);
    // Reporting the plan never modifies the project.
    CHECK_FALSE(session->modified());

    const auto pruned = requireProject(session->pruneUnreferencedRevisions());
    CHECK(pruned.revisions.size() == 1u);
    CHECK(pruned.contentBytes > 0);
    CHECK(session->modified());
    CHECK(requireProject(session->unreferencedRevisions()).empty());

    requireProject(session->save());
    CHECK_FALSE(session->modified());
    session.reset();
    session = requireProject(ProjectSession::open(path));
    CHECK(
      requireProject(
        session->store().readAsset(asset.id, project::ContentKind::Compatible))
      == replacement.compatible);
    CHECK(session->store()
            .readAsset(asset.id, project::ContentKind::Compatible, 1)
            .is_error());
  }

  SECTION("refuses to overwrite an externally changed source")
  {
    const auto asset = requireProject(session->importAsset(testAsset()));
    {
      auto external = requireProject(project::ProjectStore::open(path));
      requireProject(external->checkpoint("External edit"));
    }
    const auto before = readProjectFile(path);
    CHECK(session->save().is_error());
    CHECK(readProjectFile(path) == before);
    CHECK(session->modified());
    CHECK(requireProject(session->store().findAsset(asset.id)) == asset);
  }

  SECTION("map comparison snapshots and copies never leak staged map changes")
  {
    auto fixture = MapDocumentFixture{};
    auto& document = fixture.create();
    auto& map = document.map();
    const auto source = path.parent_path() / "initial.map";
    requireProject(map.saveTo(source));
    auto input = project::AssetImport{};
    input.type = project::AssetType::Map;
    input.virtualPath = "maps/a.map";
    input.displayName = "A";
    input.original = readProjectFile(source);
    input.compatible = input.original;
    const auto asset = requireProject(session->importAsset(input));
    requireProject(session->registerDocument(document, asset.id));
    requireProject(session->save());
    CHECK_FALSE(map.modified());
    const auto baseline = requireProject(session->checkpoint("Before edit"));
    REQUIRE(mdl::setEntityProperty(map, "message", "Unsaved text"));
    REQUIRE(map.modified());
    CHECK(requireProject(session->compare(baseline.id)).assets.size() == 1);
    CHECK(requireProject(session->store().findAsset(asset.id)) == asset);
    const auto edited = requireProject(session->checkpoint("With edit"));
    CHECK(requireProject(session->store().findAsset(asset.id)) == asset);
    CHECK(requireProject(session->compare(edited.id)).assets.empty());
    const auto copy = path.parent_path() / "dirty-copy.tbproj";
    requireProject(session->saveCopy(copy));
    CHECK(requireProject(session->store().findAsset(asset.id)) == asset);
    CHECK(map.modified());
    CHECK(
      requireProject(project::ProjectStore::open(copy))
        ->readAsset(asset.id, project::ContentKind::Compatible)
        .value()
      != input.compatible);
    requireProject(map.save());
    CHECK_FALSE(map.modified());
    CHECK_FALSE(session->modified());
    CHECK(
      requireProject(
        session->store().readAsset(asset.id, project::ContentKind::Compatible))
      != input.compatible);
    session->unregisterDocument(document);
    CHECK_FALSE(session->hasOpenMaps());
    requireProject(session->restore(baseline.id));
    CHECK(
      requireProject(
        session->store().readAsset(asset.id, project::ContentKind::Compatible))
      == input.compatible);
  }

  SECTION("restores open maps and preserves live edits in a safety version")
  {
    auto fixture = MapDocumentFixture{};
    auto& document = fixture.create();
    const auto source = path.parent_path() / "live.map";
    requireProject(document.map().saveAs(source));
    auto input = project::AssetImport{};
    input.type = project::AssetType::Map;
    input.virtualPath = "maps/live.map";
    input.displayName = "Live";
    input.original = input.compatible = readProjectFile(source);
    const auto empty = requireProject(session->checkpoint("Without map"));
    const auto asset = requireProject(session->importAsset(input));
    requireProject(session->registerDocument(document, asset.id));
    requireProject(session->save());

    REQUIRE(mdl::setEntityProperty(document.map(), "message", "Version one"));
    const auto v1 = requireProject(session->checkpoint("v1"));
    requireProject(document.map().saveTo(source));
    const auto v1Bytes = readProjectFile(source);
    REQUIRE(mdl::setEntityProperty(document.map(), "message", "Live edits"));
    requireProject(document.map().saveTo(source));
    const auto liveBytes = readProjectFile(source);

    SECTION("missing map is rejected without changing state")
    {
      const auto before = requireProject(session->store().assets());
      const auto history = requireProject(session->store().checkpoints());
      CHECK(session->restore(empty.id).is_error());
      CHECK(requireProject(session->store().assets()) == before);
      CHECK(requireProject(session->store().checkpoints()).size() == history.size());
      CHECK(document.map().modified());
      requireProject(document.map().saveTo(source));
      CHECK(readProjectFile(source) == liveBytes);
    }
    SECTION("restored documents are clean and remain bound to project saving")
    {
      requireProject(session->restore(v1.id));
      CHECK_FALSE(document.map().modified());
      CHECK(session->hasOpenMaps());
      CHECK(session->modified());
      requireProject(document.map().saveTo(source));
      CHECK(readProjectFile(source) == v1Bytes);
      CHECK(
        requireProject(
          session->store().readAsset(asset.id, project::ContentKind::Compatible))
        == v1Bytes);
      const auto history = requireProject(session->store().checkpoints());
      const auto safety = std::ranges::find_if(
        history, [](const auto& entry) { return entry.name == "Before restore"; });
      REQUIRE(safety != history.end());
      requireProject(session->restore(safety->id));
      requireProject(document.map().saveTo(source));
      CHECK(readProjectFile(source) == liveBytes);
      CHECK_FALSE(document.map().modified());
      REQUIRE(mdl::setEntityProperty(document.map(), "message", "After restore"));
      requireProject(document.map().save());
      CHECK_FALSE(session->modified());
    }
    session->unregisterDocument(document);
  }

  SECTION("multi map restore prepares every replacement before changing live state")
  {
    auto firstFixture = MapDocumentFixture{};
    auto secondFixture = MapDocumentFixture{};
    auto& first = firstFixture.create();
    auto& second = secondFixture.create();
    const auto firstPath = path.parent_path() / "first.map";
    const auto secondPath = path.parent_path() / "second.map";
    requireProject(first.map().saveAs(firstPath));
    requireProject(second.map().saveAs(secondPath));
    auto input = project::AssetImport{};
    input.type = project::AssetType::Map;
    input.displayName = "Map";
    input.virtualPath = "maps/first.map";
    input.original = input.compatible = readProjectFile(firstPath);
    const auto firstAsset = requireProject(session->importAsset(input));
    input.virtualPath = "maps/second.map";
    const auto secondAsset = requireProject(session->importAsset(input));
    requireProject(session->registerDocument(first, firstAsset.id));
    requireProject(session->registerDocument(second, secondAsset.id));
    const auto sound = requireProject(session->importAsset(testAsset()));
    auto store = requireProject(project::ProjectStore::open(session->store().path()));
    requireProject(store->addDependency(sound.id, firstAsset.id, "map"));
    const auto valid = requireProject(session->checkpoint("Valid"));

    // The later document in session order fails after the first replacement is ready.
    const auto badId =
      std::less<MapDocument*>{}(&first, &second) ? secondAsset.id : firstAsset.id;
    auto target = valid;
    auto expectSuccess = false;
    SECTION("invalid map payload")
    {
      input.original = input.compatible = {'n', 'o', 't', ' ', 'a', ' ', 'm', 'a', 'p'};
      requireProject(store->replaceAsset(badId, input));
      target = requireProject(session->checkpoint("Invalid payload"));
    }
    SECTION("cache file cannot be written")
    {
      requireProject(store->moveAsset(badId, "maps/" + std::string(300, 'x') + ".map"));
      target = requireProject(session->checkpoint("Unwritable cache name"));
    }
    SECTION("all maps prepare successfully")
    {
      expectSuccess = true;
    }
    // Return the live catalog to valid content, with a different dependency graph.
    requireProject(store->restore(valid.id));
    requireProject(store->removeAsset(sound.id));
    store.reset();
    requireProject(session->save());
    REQUIRE(mdl::setEntityProperty(first.map(), "message", "First live edit"));
    REQUIRE(mdl::setEntityProperty(second.map(), "message", "Second live edit"));
    auto* firstMap = &first.map();
    auto* secondMap = &second.map();
    const auto firstCache = readProjectFile(firstPath);
    const auto secondCache = readProjectFile(secondPath);
    const auto before = requireProject(session->store().assets());
    const auto historySize = requireProject(session->store().checkpoints()).size();
    const auto modified = session->modified();
    auto loads = 0;
    auto connection = NotifierConnection{};
    connection += first.documentWasLoadedNotifier.connect([&]() { ++loads; });
    connection += second.documentWasLoadedNotifier.connect([&]() { ++loads; });
    const auto restored = session->restore(target.id);
    if (expectSuccess)
    {
      requireProject(restored);
      CHECK(loads == 2);
      CHECK_FALSE(first.map().modified());
      CHECK_FALSE(second.map().modified());
      CHECK(requireProject(session->store().dependencies(sound.id)).size() == 1);
      REQUIRE(mdl::setEntityProperty(first.map(), "message", "New edit"));
      requireProject(first.map().save());
      CHECK_FALSE(session->modified());
    }
    else
    {
      CHECK(restored.is_error());
      CHECK(loads == 0);
      CHECK(&first.map() == firstMap);
      CHECK(&second.map() == secondMap);
      CHECK(first.map().modified());
      CHECK(second.map().modified());
      CHECK(first.map().canUndoCommand());
      CHECK(second.map().canUndoCommand());
      CHECK(session->modified() == modified);
      CHECK(requireProject(session->store().assets()) == before);
      CHECK(requireProject(session->store().checkpoints()).size() == historySize);
      CHECK(requireProject(session->store().dependencies(sound.id)).empty());
      CHECK(readProjectFile(firstPath) == firstCache);
      CHECK(readProjectFile(secondPath) == secondCache);
    }
    session->unregisterDocument(first);
    session->unregisterDocument(second);
  }

  SECTION("runtime generations retain publish paths and remove restored content")
  {
    const auto asset = requireProject(session->importAsset(testAsset()));
    const auto baseline = requireProject(session->checkpoint("Before extra"));
    auto fixture = MapDocumentFixture{};
    auto& document = fixture.create();
    auto input = testAsset();
    input.type = project::AssetType::Map;
    input.virtualPath = "maps/test.map";
    const auto mapAsset = requireProject(session->importAsset(input));
    requireProject(session->registerDocument(document, mapAsset.id));
    const auto root = document.map().gameFileSystem().projectDirectory();
    CHECK(readProjectFile(root / "sound/test.wav") == testAsset().compatible);
    input = testAsset();
    input.compatible = {9, 8};
    requireProject(session->replaceAsset(asset.id, input));
    const auto updatedRoot = document.map().gameFileSystem().projectDirectory();
    CHECK(readProjectFile(updatedRoot / "sound/test.wav") == input.compatible);
    CHECK_FALSE(std::filesystem::exists(root));
    const auto moved =
      requireProject(session->moveAsset(asset.id, "Favorites/renamed.wav"));
    CHECK(moved.publishPath == asset.publishPath);
    CHECK(std::filesystem::exists(
      document.map().gameFileSystem().projectDirectory() / "sound/test.wav"));
    session->unregisterDocument(document);
    requireProject(session->restore(baseline.id));
    CHECK(requireProject(session->store().findAsset(asset.id)) == asset);
  }

  SECTION(
    "embedded WADs load and replace materials in multiple maps without source files")
  {
    auto config = mdl::QuakeFixtureConfig;
    config.gameInfo.gameConfig.materialConfig.extensions = {".C"};
    auto firstFixture = MapDocumentFixture{};
    auto secondFixture = MapDocumentFixture{};
    auto& first = firstFixture.create(config);
    auto& second = secondFixture.create(config);
    auto mapInput = testAsset();
    mapInput.type = project::AssetType::Map;
    mapInput.virtualPath = "maps/first.map";
    const auto firstAsset = requireProject(session->importAsset(mapInput));
    mapInput.virtualPath = "maps/second.map";
    const auto secondAsset = requireProject(session->importAsset(mapInput));
    requireProject(session->registerDocument(first, firstAsset.id));
    requireProject(session->registerDocument(second, secondAsset.id));
    auto input = project::AssetImport{};
    input.type = project::AssetType::Texture;
    input.virtualPath = "textures/portable.wad";
    input.displayName = "Portable";
    input.original = {1, 2, 3};
    input.compatible =
      requireProject(
        img::convertGoldSrcTexture(
          {16, 16, std::vector<unsigned char>(16 * 16 * 4, 255)}, "portable"))
        .wad;
    const auto texture = requireProject(session->importAsset(input));
    CHECK(first.map().materialManager().material("portable") != nullptr);
    CHECK(second.map().materialManager().material("portable") != nullptr);
    const auto oldRoot = first.map().gameFileSystem().projectDirectory();
    input.compatible =
      requireProject(
        img::convertGoldSrcTexture(
          {32, 32, std::vector<unsigned char>(32 * 32 * 4, 255)}, "portable"))
        .wad;
    requireProject(session->replaceAsset(texture.id, input));
    CHECK_FALSE(std::filesystem::exists(oldRoot));
    CHECK(first.map().materialManager().material("portable") != nullptr);
    CHECK(second.map().materialManager().material("portable") != nullptr);
    requireProject(session->moveAsset(texture.id, "Favorites/moved.wad"));
    CHECK(first.map().materialManager().material("portable") != nullptr);
    requireProject(session->save());
    session->unregisterDocument(first);
    session->unregisterDocument(second);
    session.reset();
    session = requireProject(ProjectSession::open(path));
    requireProject(session->registerDocument(first, firstAsset.id));
    CHECK(first.map().materialManager().material("portable") != nullptr);
    session->unregisterDocument(first);
  }

  SECTION("renames a published sound path and rewrites stored maps")
  {
    const auto sound = requireProject(session->importAsset(testAsset()));
    CHECK(sound.publishPath == "sound/test.wav");

    auto mapStream = std::ostringstream{};
    mdl::writeMapHeader(mapStream, "Half-Life", mdl::MapFormat::Standard, "Test");
    mapStream << R"({"classname" "worldspawn"}
      {"classname" "ambient_generic" "message" "test.wav"}
      {"classname" "env_sprite" "model" "sprites/keep.spr"})";
    const auto mapText = mapStream.str();
    auto mapInput = project::AssetImport{};
    mapInput.type = project::AssetType::Map;
    mapInput.virtualPath = "maps/test.map";
    mapInput.displayName = "test";
    mapInput.original.assign(mapText.begin(), mapText.end());
    mapInput.compatible = mapInput.original;
    const auto mapAsset = requireProject(session->importAsset(mapInput));

    const auto renamed =
      requireProject(session->renamePublishPath(sound.id, "sound/footstep.wav"));
    CHECK(renamed.asset.publishPath == "sound/footstep.wav");
    CHECK(renamed.mapsChanged == 1);
    CHECK(renamed.referencesChanged == 1);
    CHECK_FALSE(renamed.checkpointId.empty());

    const auto bytes = requireProject(
      session->store().readAsset(mapAsset.id, project::ContentKind::Compatible));
    const auto text = std::string(bytes.begin(), bytes.end());
    CHECK(text.find("\"footstep.wav\"") != std::string::npos);
    CHECK(text.find("\"test.wav\"") == std::string::npos);
    CHECK(text.find("sprites/keep.spr") != std::string::npos);

    const auto audit = requireProject(auditGoldSrcProjectResources(*session));
    const auto use = std::ranges::find_if(audit.uses, [](const auto& candidate) {
      return candidate.reference == "sound/footstep.wav";
    });
    REQUIRE(use != audit.uses.end());
    CHECK(use->status == ProjectResourceStatus::Embedded);

    // A path another asset already publishes is rejected before any map changes.
    const auto other = requireProject(session->importAsset(project::AssetImport{
      project::AssetType::Sound, "sound/other.wav", "Other", "", {1}, {1}, "", {}}));
    const auto before = requireProject(
      session->store().readAsset(mapAsset.id, project::ContentKind::Compatible));
    CHECK(session->renamePublishPath(sound.id, other.publishPath).is_error());
    CHECK(
      requireProject(
        session->store().readAsset(mapAsset.id, project::ContentKind::Compatible))
      == before);
  }

  SECTION("renames a texture and rewrites map materials")
  {
    const auto converted = requireProject(img::convertGoldSrcTexture(
      {16, 16, std::vector<unsigned char>(16 * 16 * 4, 255)}, "OLD01"));
    auto textureInput = project::AssetImport{};
    textureInput.type = project::AssetType::Texture;
    textureInput.virtualPath = "textures/OLD01.wad";
    textureInput.displayName = "OLD01";
    textureInput.original = {9, 9, 9};
    textureInput.compatible = converted.wad;
    const auto texture = requireProject(session->importAsset(textureInput));

    auto mapStream = std::ostringstream{};
    mdl::writeMapHeader(mapStream, "Half-Life", mdl::MapFormat::Standard, "Test");
    mapStream << "{\n"
                 "\"classname\" \"worldspawn\"\n"
                 "{\n"
                 "( 0 0 0 ) ( 64 0 0 ) ( 0 64 0 ) OLD01 0 0 0 1 1\n"
                 "}\n"
                 "}\n";
    const auto mapText = mapStream.str();
    auto mapInput = project::AssetImport{};
    mapInput.type = project::AssetType::Map;
    mapInput.virtualPath = "maps/faces.map";
    mapInput.displayName = "faces";
    mapInput.original.assign(mapText.begin(), mapText.end());
    mapInput.compatible = mapInput.original;
    const auto mapAsset = requireProject(session->importAsset(mapInput));

    const auto renamed =
      requireProject(session->renamePublishPath(texture.id, "textures/NEW02.wad"));
    CHECK(renamed.asset.publishPath == "textures/NEW02.wad");
    CHECK(renamed.mapsChanged == 1);
    CHECK(renamed.referencesChanged == 1);

    const auto bytes = requireProject(
      session->store().readAsset(mapAsset.id, project::ContentKind::Compatible));
    const auto text = std::string(bytes.begin(), bytes.end());
    CHECK(text.find(" OLD01 ") == std::string::npos);
    CHECK(text.find(" NEW02 ") != std::string::npos);

    const auto wadBytes = requireProject(
      session->store().readAsset(texture.id, project::ContentKind::Compatible));
    const auto archive = requireProject(project::readGoldSrcWad(wadBytes));
    REQUIRE(archive.textures.size() == 1);
    CHECK(archive.textures.front().name == "NEW02");

    const auto audit = requireProject(auditGoldSrcProjectResources(*session));
    const auto use = std::ranges::find_if(
      audit.uses, [](const auto& candidate) { return candidate.reference == "NEW02"; });
    REQUIRE(use != audit.uses.end());
    CHECK(use->status == ProjectResourceStatus::Embedded);

    // A WAD with more than one texture cannot pick which name to rename.
    const auto other = requireProject(img::convertGoldSrcTexture(
      {16, 16, std::vector<unsigned char>(16 * 16 * 4, 255)}, "OTHER"));
    const auto firstArchive = requireProject(project::readGoldSrcWad(converted.wad));
    const auto otherArchive = requireProject(project::readGoldSrcWad(other.wad));
    const auto combined = requireProject(project::buildGoldSrcWad(
      {firstArchive.textures.front().miptex, otherArchive.textures.front().miptex}));
    auto combinedInput = textureInput;
    combinedInput.virtualPath = "textures/COMBINED.wad";
    combinedInput.displayName = "COMBINED";
    combinedInput.compatible = combined;
    const auto combinedAsset = requireProject(session->importAsset(combinedInput));
    CHECK(
      session->renamePublishPath(combinedAsset.id, "textures/COMBINED2.wad").is_error());
  }
}

TEST_CASE("Project terrain paint survives clipping and compile snapshots")
{
  auto temporary = QTemporaryDir{};
  REQUIRE(temporary.isValid());
  const auto root = pathFromQString(temporary.path());
  const auto path = root / "terrain.tbproj";
  auto session = requireProject(ProjectSession::create(path, "Terrain", "Half-Life"));
  auto fixture = MapDocumentFixture{};
  auto config = mdl::MapFixtureConfig{.mapFormat = mdl::MapFormat::Valve};
  config.gameInfo.gameConfig.name = "Half-Life";
  auto& document = fixture.create(config);
  auto& map = document.map();
  mdl::selectAllNodes(map);
  mdl::removeSelectedNodes(map);
  const auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, map.worldBounds()};
  auto* ramp = new mdl::BrushNode{requireProject(builder.createBrush(
    std::vector<vm::vec3d>{
      {0, 0, 0},
      {0, -640, 0},
      {1280, 0, 80},
      {1280, -640, 80},
      {0, 0, -64},
      {0, -640, -64},
      {1280, 0, -64},
      {1280, -640, -64}},
    "terrainbase"))};
  mdl::addNodes(map, {{map.editorContext().currentLayer(), {ramp}}});
  mdl::selectNodes(map, {ramp});
  auto* group = mdl::groupSelectedNodes(map, "Slope");
  REQUIRE(group);
  const auto groupId = group->persistentId();
  auto region = requireProject(createTerrainRegion(*group));
  for (auto x = 390; x <= 624; x += 6)
  {
    region.paint(vm::vec2d{double(x), 266 + (x - 390) / 4.0}, 45, 1);
    region.paint(vm::vec2d{double(x), 218.0}, 30, 2);
  }
  const auto painted = region.encode();
  auto content = group->group();
  content.setTerrainPaint(painted);
  REQUIRE(mdl::updateNodeContents(
    map, "Paint Terrain", {{group, mdl::NodeContents{std::move(content)}}}));
  map.undoCommand();
  CHECK(group->group().terrainPaint().empty());
  map.redoCommand();
  CHECK(group->group().terrainPaint() == painted);

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
  REQUIRE(requireProject(terrainSurfaces(*group, region)).size() == 2);
  map.undoCommand();
  CHECK(group->childCount() == 1);
  CHECK(group->group().terrainPaint() == painted);
  map.redoCommand();
  CHECK(group->childCount() == 2);
  CHECK(group->persistentId() == groupId);
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
  CHECK(group->childCount() == 2);
  CHECK(group->logicalBounds().max.z() == 40);
  CHECK(group->group().terrainPaint() == painted);
  map.undoCommand();
  CHECK(group->logicalBounds().max.z() == 80);
  map.redoCommand();
  CHECK(group->logicalBounds().max.z() == 40);
  mdl::closeGroup(map);

  auto walls = std::vector<mdl::Node*>{};
  for (const auto& bounds :
       {vm::bbox3d{{-32, -672, -64}, {0, 32, 224}},
        vm::bbox3d{{1280, -672, -64}, {1312, 32, 224}},
        vm::bbox3d{{0, -672, -64}, {1280, -640, 224}},
        vm::bbox3d{{0, 0, -64}, {1280, 32, 224}},
        vm::bbox3d{{-32, -672, 224}, {1312, 32, 256}}})
    walls.push_back(
      new mdl::BrushNode{requireProject(builder.createCuboid(bounds, "terrainbase"))});
  mdl::addNodes(map, {{map.editorContext().currentLayer(), std::move(walls)}});
  REQUIRE(mdl::paste(map, R"(
    { "classname" "info_player_start" "origin" "64 -300 48" "angle" "0" }
    { "classname" "info_player_deathmatch" "origin" "1160 -420 112" "angle" "180" }
    { "classname" "light" "origin" "160 -320 190" "_light" "255 240 215 420" }
    { "classname" "light" "origin" "560 -320 190" "_light" "255 240 215 420" }
    { "classname" "light" "origin" "960 -320 190" "_light" "255 240 215 420" }
  )") == mdl::PasteType::Node);

  const auto source = root / "source.map";
  requireProject(map.saveTo(source));
  auto input = project::AssetImport{};
  input.type = project::AssetType::Map;
  input.virtualPath = "maps/terrain.map";
  input.displayName = "Terrain";
  input.original = readProjectFile(source);
  input.compatible = input.original;
  const auto asset = requireProject(session->importAsset(input));
  auto texture = project::AssetImport{};
  texture.type = project::AssetType::Texture;
  texture.virtualPath = "textures/terrainbase.wad";
  texture.displayName = "Terrain base";
  texture.compatible =
    requireProject(project::buildGoldSrcWad({miptexFor("terrainbase", 16, 128)}));
  texture.original = texture.compatible;
  requireProject(session->importAsset(texture));
  requireProject(session->registerDocument(document, asset.id));
  requireProject(session->save());
  requireProject(session->exportCompileSnapshot(asset.id, root / "live"));
  CHECK(group->childCount() == 2);
  CHECK(group->group().terrainPaint() == painted);
  session->unregisterDocument(document);
  session.reset();

  session = requireProject(ProjectSession::open(path));
  const auto saved = requireProject(session->prepareMap(asset.id));
  auto reopened = MapDocumentFixture{};
  auto& loaded = reopened.load(saved, config);
  const auto& children = loaded.map().worldNode().defaultLayer()->children();
  const auto savedNode = std::ranges::find_if(children, [](const auto* child) {
    return dynamic_cast<const mdl::GroupNode*>(child) != nullptr;
  });
  REQUIRE(savedNode != children.end());
  auto* savedGroup = static_cast<mdl::GroupNode*>(*savedNode);
  REQUIRE(savedGroup);
  CHECK(savedGroup->persistentId() == groupId);
  CHECK(savedGroup->group().terrainPaint() == painted);
  CHECK(savedGroup->childCount() == 2);

  requireProject(session->exportCompileSnapshot(asset.id, root / "reopened"));
  const auto compiledMap = readProjectFile(root / "live/map.map");
  const auto wad = readProjectFile(root / "live/textures.wad");
  CHECK(compiledMap == readProjectFile(root / "reopened/map.map"));
  CHECK(wad == readProjectFile(root / "reopened/textures.wad"));
  const auto mapText = std::string{compiledMap.begin(), compiledMap.end()};
  CHECK(mapText.find("_tb_terrain_paint") == std::string::npos);
  CHECK(mapText.find("\"mapversion\" \"220\"") != std::string::npos);
  CHECK(mapText.find("tbt0_") != std::string::npos);
  const auto archive = requireProject(project::readGoldSrcWad(wad));
  CHECK(std::ranges::any_of(archive.textures, [](const auto& entry) {
    return entry.valid && entry.name.starts_with("tbt0_") && entry.width > 64;
  }));
}

TEST_CASE("ProjectSessionAssetLibrary")
{
  auto sandbox = LibrarySandbox{};
  const auto wall = miptexFor("WALL01", 16, 255);
  const auto floor = miptexFor("FLOOR01", 32, 128);
  sandbox.writeWad({wall, floor});
  sandbox.createLibrary();
  const auto libraryId = sandbox.libraryId();
  const auto wallLibraryId = libraryAssetNamed(sandbox.libraryAssets(), "WALL01").id;
  const auto floorLibraryId = libraryAssetNamed(sandbox.libraryAssets(), "FLOOR01").id;

  SECTION("captures the current revision immediately and survives library loss")
  {
    requireProject(sandbox.session->attachLibrary(sandbox.libraryFolder));
    const auto captured =
      requireProject(sandbox.session->captureLibraryAsset(libraryId, wallLibraryId));
    CHECK(captured.revision == 1);
    CHECK(captured.library.libraryAssetId == wallLibraryId);
    CHECK(captured.library.libraryRevision == 1);
    CHECK(captured.publishPath == "textures/WALL01.wad");
    const auto snapshot = requireProject(
      sandbox.session->store().readAsset(captured.id, project::ContentKind::Compatible));

    // A library that is gone must not affect the project in any way.
    std::filesystem::remove_all(sandbox.libraryFolder);
    requireProject(sandbox.session->save());
    sandbox.session.reset();
    sandbox.session = requireProject(ProjectSession::open(sandbox.projectPath));
    CHECK(requireProject(sandbox.session->store().findAsset(captured.id)) == captured);
    CHECK(
      requireProject(
        sandbox.session->store().readAsset(captured.id, project::ContentKind::Compatible))
      == snapshot);
    CHECK(
      requireProject(sandbox.session->assetFile(captured)).filename() == "WALL01.wad");

    const auto libraries = requireProject(sandbox.session->libraries());
    REQUIRE(libraries.size() == 1);
    CHECK_FALSE(libraries.front().available);
    const auto updates = requireProject(sandbox.session->checkLibraryUpdates());
    REQUIRE(updates.size() == 1);
    CHECK(updates.front().state == project::LibraryUpdateState::LibraryUnavailable);
  }

  SECTION("automation catalog finds project and library assets by stable identity")
  {
    requireProject(sandbox.session->attachLibrary(sandbox.libraryFolder));
    const auto captured =
      requireProject(sandbox.session->captureLibraryAsset(libraryId, wallLibraryId));

    auto query = AutomationAssetQuery{};
    query.query = "wall";
    const auto projectHits =
      requireProject(searchAutomationProjectAssets(*sandbox.session, query));
    REQUIRE(projectHits.size() == 1u);
    CHECK(projectHits.front().scope == "project");
    CHECK(projectHits.front().id == captured.id);
    CHECK(projectHits.front().type == "texture");
    CHECK(projectHits.front().assetType == "texture");
    CHECK(projectHits.front().path == captured.virtualPath);
    CHECK(projectHits.front().publishPath == "textures/WALL01.wad");
    CHECK(projectHits.front().revision == 1u);
    CHECK_FALSE(projectHits.front().missing);

    const auto libraryHits =
      requireProject(searchAutomationLibraryAssets(*sandbox.session, query));
    REQUIRE(libraryHits.size() == 1u);
    CHECK(libraryHits.front().scope == "library");
    CHECK(libraryHits.front().id == wallLibraryId);
    CHECK(libraryHits.front().libraryId == libraryId);
    CHECK(libraryHits.front().revision == 1u);

    const auto described =
      requireProject(findAutomationProjectAsset(*sandbox.session, captured.id));
    REQUIRE(described);
    CHECK(described->displayName == "WALL01");

    const auto describedByPath =
      requireProject(findAutomationProjectAsset(*sandbox.session, captured.virtualPath));
    REQUIRE(describedByPath);
    CHECK(describedByPath->id == captured.id);

    const auto libraryByQualifiedId = requireProject(
      findAutomationLibraryAsset(*sandbox.session, libraryId + "/" + wallLibraryId));
    REQUIRE(libraryByQualifiedId);
    CHECK(libraryByQualifiedId->libraryId == libraryId);

    // A unique library asset id needs no library argument to be captured.
    const auto recaptured =
      requireProject(captureAutomationLibraryAsset(*sandbox.session, {}, wallLibraryId));
    CHECK(recaptured.id == captured.id);

    // Previewing a texture writes a PNG that a browser or an agent can open.
    const auto previewFolder = sandbox.root / "previews";
    const auto preview = previewAutomationAsset(
      sandbox.session.get(), projectHits.front(), 64, previewFolder);
    CHECK(preview.status == "image");
    CHECK(preview.source == "texture");
    CHECK(preview.width > 0);
    CHECK(preview.height > 0);
    CHECK(QFileInfo::exists(pathAsQString(preview.path)));
    const auto previewImage = QImage{pathAsQString(preview.path)};
    CHECK(previewImage.width() == preview.width);
    CHECK(previewImage.height() == preview.height);

    // Library assets preview from the session cache without capturing them.
    const auto libraryPreview = previewAutomationAsset(
      sandbox.session.get(), libraryHits.front(), 64, previewFolder);
    CHECK(libraryPreview.status == "image");
    CHECK(libraryPreview.width > 0);

    // A contact sheet lays the same previews out with labels and a cell mapping.
    const auto sheetSet = renderAutomationAssetSheets(
      sandbox.session.get(),
      {projectHits.front(), libraryHits.front()},
      48,
      2,
      1,
      previewFolder);
    CHECK(sheetSet.count == 2u);
    CHECK(sheetSet.columns == 2);
    CHECK(sheetSet.rows == 1);
    REQUIRE(sheetSet.sheets.size() == 1u);
    const auto& sheet = sheetSet.sheets.front();
    CHECK(sheet.cells.size() == 2u);
    CHECK(sheet.cells.front().index == 0u);
    CHECK(sheet.cells.front().scope == "project");
    CHECK(sheet.cells.back().index == 1u);
    CHECK(sheet.cells.back().scope == "library");
    CHECK(sheet.cells.back().x == 12 + 48 + 12);
    CHECK(sheet.width == 12 + 2 * (48 + 12));
    CHECK(QFileInfo::exists(pathAsQString(sheet.path)));
    const auto sheetImage = QImage{pathAsQString(sheet.path)};
    CHECK(sheetImage.width() == sheet.width);
    CHECK(sheetImage.height() == sheet.height);

    // Importing a file reuses the project window's naming and folder rules.
    const auto importPath = sandbox.root / "extra.mdl";
    writeFile(importPath, modelWithGroups(0));
    const auto imported = requireProject(
      importAutomationAssetFile(*sandbox.session, importPath, "Extra model"));
    CHECK(imported.scope == "project");
    CHECK(imported.type == "model");
    CHECK(imported.displayName == "Extra model");
    CHECK(imported.publishPath == "models/extra.mdl");
    CHECK(imported.revision == 1u);
    const auto stored = requireProject(sandbox.session->store().findAsset(imported.id));
    REQUIRE(stored);
    CHECK(stored->virtualPath == "models/extra.mdl");

    // Reopening the saved project must not change what the catalog reports.
    requireProject(sandbox.session->save());
    sandbox.session.reset();
    sandbox.session = requireProject(ProjectSession::open(sandbox.projectPath));
    const auto reopened =
      requireProject(searchAutomationProjectAssets(*sandbox.session, query));
    REQUIRE(reopened.size() == 1u);
    CHECK(reopened.front().id == captured.id);
  }

  SECTION("saving an open project map persists assets captured since the last save")
  {
    requireProject(sandbox.session->attachLibrary(sandbox.libraryFolder));

    auto fixture = MapDocumentFixture{};
    auto& document = fixture.create();
    const auto source = sandbox.root / "agent.map";
    requireProject(document.map().saveAs(source));

    auto mapImport = project::AssetImport{};
    mapImport.type = project::AssetType::Map;
    mapImport.virtualPath = "maps/agent.map";
    mapImport.displayName = "Agent";
    mapImport.original = mapImport.compatible = readProjectFile(source);
    const auto mapAsset = requireProject(sandbox.session->importAsset(mapImport));
    requireProject(sandbox.session->registerDocument(document, mapAsset.id));
    requireProject(sandbox.session->save());

    // The documented agent flow: capture through automation, then save the open map.
    // Map::save delegates to the project session, so the captured asset is written.
    const auto captured =
      requireProject(sandbox.session->captureLibraryAsset(libraryId, wallLibraryId));
    requireProject(document.map().save());

    sandbox.session->unregisterDocument(document);
    sandbox.session.reset();
    sandbox.session = requireProject(ProjectSession::open(sandbox.projectPath));
    CHECK(requireProject(sandbox.session->store().findAsset(captured.id)));
    const auto found =
      requireProject(findAutomationProjectAsset(*sandbox.session, captured.id));
    REQUIRE(found);
    CHECK(found->publishPath == "textures/WALL01.wad");
  }

  SECTION("automation previews skyboxes and filters them apart from textures")
  {
    for (const auto* face : {"bk", "rt", "lf", "ft", "up", "dn"})
    {
      sandbox.write(
        std::string{"env/desert"} + face + ".png", pngBytes(QColor{31, 93, 177}, 16));
    }
    sandbox.rescanLibrary();
    requireProject(sandbox.session->attachLibrary(sandbox.libraryFolder));

    auto skyQuery = AutomationAssetQuery{};
    skyQuery.type = BrowserCellType::Texture;
    skyQuery.assetType = "skybox";
    const auto skies =
      requireProject(searchAutomationLibraryAssets(*sandbox.session, skyQuery));
    REQUIRE(skies.size() == 1u);
    CHECK(skies.front().assetType == "skybox");

    // The precise filter separates the group from its six faces.
    auto textureQuery = AutomationAssetQuery{};
    textureQuery.type = BrowserCellType::Texture;
    textureQuery.assetType = "texture";
    const auto textures =
      requireProject(searchAutomationLibraryAssets(*sandbox.session, textureQuery));
    CHECK(std::ranges::none_of(
      textures, [](const auto& asset) { return asset.assetType == "skybox"; }));

    const auto previewFolder = sandbox.root / "previews";
    const auto libraryPreview =
      previewAutomationAsset(sandbox.session.get(), skies.front(), 64, previewFolder);
    CHECK(libraryPreview.status == "image");
    CHECK(libraryPreview.source == "skybox");
    CHECK(libraryPreview.width == 64);
    CHECK(QFileInfo::exists(pathAsQString(libraryPreview.path)));

    // The captured project skybox renders through the project store as well.
    const auto captured =
      requireProject(sandbox.session->captureLibraryAsset(libraryId, skies.front().id));
    const auto projectSky =
      requireProject(findAutomationProjectAsset(*sandbox.session, captured.id));
    REQUIRE(projectSky);
    CHECK(projectSky->assetType == "skybox");
    const auto projectPreview =
      previewAutomationAsset(sandbox.session.get(), *projectSky, 64, previewFolder);
    CHECK(projectPreview.status == "image");
    CHECK(projectPreview.source == "skybox");
  }

  SECTION("publishes a converted image and captures it without reconverting")
  {
    const auto encode = [](QImage picture) {
      auto buffer = QBuffer{};
      REQUIRE(buffer.open(QIODevice::WriteOnly));
      REQUIRE(picture.save(&buffer, "PNG"));
      return std::vector<std::uint8_t>{buffer.data().begin(), buffer.data().end()};
    };

    auto image = QImage{4, 4, QImage::Format_RGB32};
    image.fill(QColor{200, 10, 10});
    sandbox.write("textures/wall.png", encode(image));
    sandbox.rescanLibrary();

    const auto imageAsset = [&]() {
      for (const auto& asset : sandbox.libraryAssets())
        if (asset.virtualPath == "textures/wall.png")
          return asset;
      FAIL("library image missing");
      return project::LibraryAsset{};
    }();

    requireProject(sandbox.session->attachLibrary(sandbox.libraryFolder));
    const auto first =
      requireProject(sandbox.session->captureLibraryAsset(libraryId, imageAsset.id));
    const auto firstSnapshot = requireProject(
      sandbox.session->store().readAsset(first.id, project::ContentKind::Compatible));
    CHECK(first.library.libraryRevision == imageAsset.revision);
    CHECK(first.publishPath == "textures/wall.wad");
    CHECK(first.conversionRecipe.find("goldsrc-texture") != std::string::npos);
    CHECK_FALSE(firstSnapshot.empty());

    // Publish a distinctly different conversion. The source file and its hash stay
    // untouched, so only the library revision moves.
    auto replacement = QImage{8, 8, QImage::Format_RGB32};
    replacement.fill(QColor{10, 200, 10});
    const auto replacementBytes = encode(replacement);
    const auto published = requireProject(img::convertGoldSrcTexture(
      requireProject(img::decodeImage(replacementBytes.data(), replacementBytes.size())),
      "BLUEWALL"));
    {
      auto library = requireProject(project::AssetLibrary::open(sandbox.libraryFolder));
      requireProject(library->publishCompatibleContent(
        imageAsset.id,
        published.wad,
        R"({"importer":"goldsrc-texture","version":1,"name":"BLUEWALL"})"));
    }

    const auto updates = requireProject(sandbox.session->checkLibraryUpdates());
    const auto& update = updateFor(updates, first.id);
    CHECK(update.state == project::LibraryUpdateState::NewVersion);
    CHECK(update.available->revision == imageAsset.revision + 1);

    const auto second =
      requireProject(sandbox.session->captureLibraryAsset(libraryId, imageAsset.id));
    const auto secondSnapshot = requireProject(
      sandbox.session->store().readAsset(second.id, project::ContentKind::Compatible));
    CHECK(second.library.libraryRevision == imageAsset.revision + 1);
    const auto updatedWad = requireProject(project::readGoldSrcWad(secondSnapshot));
    REQUIRE(updatedWad.textures.size() == 1);
    CHECK(updatedWad.textures.front().name == "wall");
    CHECK(updatedWad.textures.front().width == 16);
    auto expectedMiptex =
      requireProject(project::readGoldSrcWad(published.wad)).textures.front().miptex;
    std::fill(expectedMiptex.begin(), expectedMiptex.begin() + 16, std::uint8_t{0});
    std::copy_n("wall", 4, expectedMiptex.begin());
    CHECK(updatedWad.textures.front().miptex == expectedMiptex);
    CHECK(secondSnapshot != firstSnapshot);
  }

  SECTION("renamed texture updates survive moving the project away from its sources")
  {
    requireProject(sandbox.session->attachLibrary(sandbox.libraryFolder));
    const auto captured =
      requireProject(sandbox.session->captureLibraryAsset(libraryId, wallLibraryId));
    const auto renamed = requireProject(
      sandbox.session->renamePublishPath(captured.id, "textures/RENAMED.wad"));
    requireProject(sandbox.session->renameAsset(captured.id, "Renamed wall"));
    requireProject(sandbox.session->moveAsset(captured.id, "organized/wall.wad"));

    auto stream = std::ostringstream{};
    mdl::writeMapHeader(stream, "Half-Life", mdl::MapFormat::Standard, "Portable");
    stream << "{\n\"classname\" \"worldspawn\"\n{\n"
              "( 0 0 0 ) ( 64 0 0 ) ( 0 64 0 ) RENAMED 0 0 0 1 1\n"
              "}\n}\n";
    const auto mapText = stream.str();
    auto input = project::AssetImport{};
    input.type = project::AssetType::Map;
    input.virtualPath = "maps/portable.map";
    input.displayName = "Portable";
    input.original = input.compatible = {mapText.begin(), mapText.end()};
    const auto map = requireProject(sandbox.session->importAsset(input));

    sandbox.writeWad({miptexFor("WALL01", 32, 100), floor});
    sandbox.rescanLibrary();
    const auto updated =
      requireProject(sandbox.session->updateLibraryAssets({captured.id}));
    REQUIRE(updated.size() == 1);
    CHECK(updated.front().id == captured.id);
    CHECK(updated.front().publishPath == renamed.asset.publishPath);
    CHECK(updated.front().virtualPath == "organized/wall.wad");
    CHECK(updated.front().displayName == "Renamed wall");
    const auto bytes = requireProject(
      sandbox.session->store().readAsset(captured.id, project::ContentKind::Compatible));
    const auto wad = requireProject(project::readGoldSrcWad(bytes));
    REQUIRE(wad.textures.size() == 1);
    CHECK(wad.textures.front().name == "RENAMED");
    CHECK(wad.textures.front().width == 32);

    const auto copy = sandbox.root / "portable.tbproj";
    requireProject(sandbox.session->saveCopy(copy));
    sandbox.session.reset();
    const auto relocated = sandbox.root / "another machine" / "portable.tbproj";
    std::filesystem::create_directories(relocated.parent_path());
    std::filesystem::rename(copy, relocated);
    std::filesystem::remove_all(sandbox.libraryFolder);
    std::filesystem::remove(sandbox.projectPath);
    sandbox.session = requireProject(ProjectSession::open(relocated));
    CHECK_FALSE(sandbox.session->modified());
    const auto restored = requireProject(sandbox.session->store().findAsset(captured.id));
    REQUIRE(restored);
    CHECK(*restored == updated.front());
    CHECK(
      readProjectFile(requireProject(sandbox.session->assetFile(*restored))) == bytes);
    const auto audit = requireProject(auditGoldSrcProjectResources(*sandbox.session));
    REQUIRE(audit.uses.size() == 1);
    CHECK(audit.uses.front().status == ProjectResourceStatus::Embedded);
    CHECK(sandbox.session->updateLibraryAssets({captured.id}).is_error());
    CHECK(requireProject(sandbox.session->store().findAsset(captured.id)) == restored);
    CHECK_FALSE(sandbox.session->modified());
    const auto destination = sandbox.root / "published";
    requireProject(sandbox.session->exportCompileSnapshot(map.id, destination));
    CHECK(readProjectFile(destination / "map.map") == input.compatible);
    CHECK(readProjectFile(destination / "textures.wad") == bytes);
    CHECK_FALSE(sandbox.session->modified());
  }

  SECTION("relinks a moved library and keeps captured snapshots independent")
  {
    requireProject(sandbox.session->attachLibrary(sandbox.libraryFolder));
    const auto captured =
      requireProject(sandbox.session->captureLibraryAsset(libraryId, wallLibraryId));
    const auto snapshot = requireProject(
      sandbox.session->store().readAsset(captured.id, project::ContentKind::Compatible));

    const auto moved = sandbox.root / "moved-library";
    std::filesystem::rename(sandbox.libraryFolder, moved);
    const auto unavailable = requireProject(sandbox.session->libraries());
    REQUIRE(unavailable.size() == 1);
    CHECK_FALSE(unavailable.front().available);
    CHECK(
      requireProject(sandbox.session->checkLibraryUpdates()).front().state
      == project::LibraryUpdateState::LibraryUnavailable);

    // Only the same library identity may take over the existing project link.
    const auto otherFolder = sandbox.root / "other-library";
    std::filesystem::create_directories(otherFolder);
    requireProject(project::AssetLibrary::create(otherFolder, "Other"));
    CHECK(sandbox.session->relocateLibrary(libraryId, otherFolder).is_error());

    requireProject(sandbox.session->relocateLibrary(libraryId, moved));
    const auto relinked = requireProject(sandbox.session->libraries());
    REQUIRE(relinked.size() == 1);
    CHECK(relinked.front().available);
    CHECK(
      requireProject(sandbox.session->checkLibraryUpdates()).front().state
      == project::LibraryUpdateState::UpToDate);
    CHECK(
      requireProject(
        sandbox.session->store().readAsset(captured.id, project::ContentKind::Compatible))
      == snapshot);

    // The new location is part of the saved project.
    requireProject(sandbox.session->save());
    sandbox.session.reset();
    sandbox.session = requireProject(ProjectSession::open(sandbox.projectPath));
    const auto reopened = requireProject(sandbox.session->libraries());
    REQUIRE(reopened.size() == 1);
    CHECK(reopened.front().available);
  }

  SECTION("reports same-named captures as conflicts and replaces on demand")
  {
    requireProject(sandbox.session->attachLibrary(sandbox.libraryFolder));
    const auto first =
      requireProject(sandbox.session->captureLibraryAsset(libraryId, wallLibraryId));

    // A second library publishes the same texture name with different content.
    const auto otherFolder = sandbox.root / "other-library";
    std::filesystem::create_directories(otherFolder);
    writeFile(
      otherFolder / "custom.wad",
      requireProject(project::buildGoldSrcWad({miptexFor("WALL01", 16, 64)})));
    auto other = requireProject(project::AssetLibrary::create(otherFolder, "Other"));
    requireProject(other->scan());
    const auto otherLibraryId = other->info().id;
    const auto otherWall = [&]() {
      const auto assets = requireProject(other->assets());
      const auto it = std::ranges::find_if(
        assets, [](const auto& asset) { return asset.displayName == "WALL01"; });
      REQUIRE(it != assets.end());
      return *it;
    }();
    requireProject(sandbox.session->attachLibrary(otherFolder));

    // The plain capture refuses instead of letting the store fail with a generic error.
    const auto refused =
      sandbox.session->captureLibraryAsset(otherLibraryId, otherWall.id);
    REQUIRE(refused.is_error());
    CHECK(
      std::get<Error>(refused.error()).msg.find("already publishes")
      != std::string::npos);

    // Cancelling leaves the existing snapshot untouched.
    const auto before = requireProject(sandbox.session->store().findAsset(first.id));
    const auto cancelled = requireProject(sandbox.session->captureLibraryAssetResolving(
      otherLibraryId, otherWall.id, [](const CaptureConflict&) {
        return CaptureConflictChoice::Cancel;
      }));
    CHECK_FALSE(cancelled.has_value());
    CHECK(requireProject(sandbox.session->store().findAsset(first.id)) == before);

    auto asked = 0;
    const auto replaced = requireProject(sandbox.session->captureLibraryAssetResolving(
      otherLibraryId, otherWall.id, [&](const CaptureConflict& conflict) {
        ++asked;
        REQUIRE(conflict.existing.has_value());
        CHECK(conflict.existing->id == first.id);
        CHECK(conflict.path == first.virtualPath);
        return CaptureConflictChoice::Replace;
      }));
    REQUIRE(replaced.has_value());
    CHECK(asked == 1);
    CHECK(replaced->id == first.id);
    CHECK(replaced->virtualPath == first.virtualPath);
    CHECK(replaced->library.libraryId == otherLibraryId);
  }

  SECTION("captures map references from the attached library")
  {
    // The map uses a sound and a sprite that the project does not embed yet.
    auto mapStream = std::ostringstream{};
    mdl::writeMapHeader(mapStream, "Half-Life", mdl::MapFormat::Standard, "Test");
    mapStream << R"({"classname" "worldspawn"}
      {"classname" "ambient_generic" "message" "step.wav"}
      {"classname" "env_sprite" "model" "sprites/flare.spr"})";
    const auto mapText = mapStream.str();
    auto mapInput = project::AssetImport{};
    mapInput.type = project::AssetType::Map;
    mapInput.virtualPath = "maps/test.map";
    mapInput.displayName = "test";
    mapInput.original.assign(mapText.begin(), mapText.end());
    mapInput.compatible = mapInput.original;
    const auto mapAsset = requireProject(sandbox.session->importAsset(mapInput));

    // The library provides the sound but not the sprite.
    sandbox.write("sound/step.wav", {'R', 'I', 'F', 'F'});
    sandbox.rescanLibrary();
    requireProject(sandbox.session->attachLibrary(sandbox.libraryFolder));

    const auto captured = requireProject(sandbox.session->captureMapReferences(
      mapAsset.id,
      [](const CaptureConflict&) { return CaptureConflictChoice::Replace; }));
    REQUIRE(captured.captured.size() == 1);
    CHECK(captured.captured.front().publishPath == "sound/step.wav");
    REQUIRE(captured.unresolved.size() == 1);
    CHECK(captured.unresolved.front() == "sprites/flare.spr");

    const auto audit = requireProject(auditGoldSrcProjectResources(*sandbox.session));
    for (const auto& use : audit.uses)
      if (use.reference == "sound/step.wav")
        CHECK(use.status == ProjectResourceStatus::Embedded);

    // The second run has nothing left to capture.
    const auto again = requireProject(sandbox.session->captureMapReferences(
      mapAsset.id,
      [](const CaptureConflict&) { return CaptureConflictChoice::Replace; }));
    CHECK(again.captured.empty());
  }

  SECTION("keeps the old snapshot while the library moves ahead")
  {
    requireProject(sandbox.session->attachLibrary(sandbox.libraryFolder));
    const auto captured =
      requireProject(sandbox.session->captureLibraryAsset(libraryId, wallLibraryId));
    const auto floorCaptured =
      requireProject(sandbox.session->captureLibraryAsset(libraryId, floorLibraryId));
    const auto capturedContent = requireProject(
      sandbox.session->store().readAsset(captured.id, project::ContentKind::Compatible));

    // Only WALL01 changes; FLOOR01 keeps its bytes so the WAD still has one asset per
    // texture.
    sandbox.writeWad({miptexFor("WALL01", 16, 32), floor});
    sandbox.rescanLibrary();

    CHECK(requireProject(sandbox.session->store().findAsset(captured.id)) == captured);
    CHECK(
      requireProject(
        sandbox.session->store().readAsset(captured.id, project::ContentKind::Compatible))
      == capturedContent);

    const auto updates = requireProject(sandbox.session->checkLibraryUpdates());
    REQUIRE(updates.size() == 2);
    CHECK(
      updateFor(updates, captured.id).state == project::LibraryUpdateState::NewVersion);
    CHECK(updateFor(updates, captured.id).needsAttention());
    // A single changed WAD member must not mark its siblings as outdated.
    CHECK(
      updateFor(updates, floorCaptured.id).state
      == project::LibraryUpdateState::UpToDate);
  }

  SECTION("updates selected resources and keeps the previous revision")
  {
    requireProject(sandbox.session->attachLibrary(sandbox.libraryFolder));
    const auto captured =
      requireProject(sandbox.session->captureLibraryAsset(libraryId, wallLibraryId));
    const auto firstRevision = requireProject(
      sandbox.session->store().readAsset(captured.id, project::ContentKind::Compatible));
    sandbox.writeWad({miptexFor("WALL01", 16, 32), floor});
    sandbox.rescanLibrary();

    const auto updated =
      requireProject(sandbox.session->updateLibraryAssets({captured.id}));
    REQUIRE(updated.size() == 1);
    CHECK(updated.front().id == captured.id);
    CHECK(updated.front().revision == 2);
    CHECK(updated.front().library.libraryRevision == 2);
    CHECK(sandbox.session->modified());
    CHECK(
      requireProject(
        sandbox.session->store().readAsset(captured.id, project::ContentKind::Compatible))
      != firstRevision);
    CHECK(
      requireProject(sandbox.session->store().readAsset(
        captured.id, project::ContentKind::Compatible, 1))
      == firstRevision);
    CHECK(
      updateFor(requireProject(sandbox.session->checkLibraryUpdates()), captured.id).state
      == project::LibraryUpdateState::UpToDate);

    // The update is reversible: selecting the old revision brings the prompt back.
    const auto restored =
      requireProject(sandbox.session->restoreAssetRevision(captured.id, 1));
    CHECK(restored.revision == 1);
    CHECK(
      requireProject(
        sandbox.session->store().readAsset(captured.id, project::ContentKind::Compatible))
      == firstRevision);
    CHECK(
      updateFor(requireProject(sandbox.session->checkLibraryUpdates()), captured.id).state
      == project::LibraryUpdateState::NewVersion);
  }

  SECTION("an update that cannot be completed leaves every asset untouched")
  {
    requireProject(sandbox.session->attachLibrary(sandbox.libraryFolder));
    const auto captured =
      requireProject(sandbox.session->captureLibraryAsset(libraryId, wallLibraryId));
    const auto unrelated = requireProject(sandbox.session->importAsset(testAsset()));
    sandbox.writeWad({miptexFor("WALL01", 16, 32), floor});
    sandbox.rescanLibrary();

    // The batch contains an asset without library origin, so nothing may be written.
    CHECK(sandbox.session->updateLibraryAssets({captured.id, unrelated.id}).is_error());
    CHECK(requireProject(sandbox.session->store().findAsset(captured.id)) == captured);

    // A missing library source also aborts without touching the snapshot.
    std::filesystem::remove(sandbox.libraryFolder / "custom.wad");
    sandbox.rescanLibrary();
    CHECK(sandbox.session->updateLibraryAssets({captured.id}).is_error());
    CHECK(requireProject(sandbox.session->store().findAsset(captured.id)) == captured);
    CHECK(
      updateFor(requireProject(sandbox.session->checkLibraryUpdates()), captured.id).state
      == project::LibraryUpdateState::Missing);
    // Ignoring is project state and survives a save/reopen.
    const auto missingUpdate =
      updateFor(requireProject(sandbox.session->checkLibraryUpdates()), captured.id);
    CHECK_FALSE(missingUpdate.ignored);
    requireProject(sandbox.session->save());
    sandbox.session.reset();
    sandbox.session = requireProject(ProjectSession::open(sandbox.projectPath));
    CHECK(requireProject(sandbox.session->store().findAsset(captured.id)) == captured);
    CHECK(requireProject(sandbox.session->checkLibraryUpdates()).size() == 1);
  }

  SECTION("ignored and deferred updates stay quiet until asked again")
  {
    requireProject(sandbox.session->attachLibrary(sandbox.libraryFolder));
    const auto captured =
      requireProject(sandbox.session->captureLibraryAsset(libraryId, wallLibraryId));
    sandbox.writeWad({miptexFor("WALL01", 16, 32), floor});
    sandbox.rescanLibrary();

    const auto updates = requireProject(sandbox.session->checkLibraryUpdates());
    const auto& update = updateFor(updates, captured.id);
    REQUIRE(update.available);
    requireProject(
      sandbox.session->ignoreLibraryUpdate(captured.id, update.available->contentHash));
    const auto ignored = requireProject(sandbox.session->checkLibraryUpdates());
    CHECK(updateFor(ignored, captured.id).ignored);
    CHECK_FALSE(updateFor(ignored, captured.id).needsAttention());

    CHECK_FALSE(sandbox.session->libraryUpdatesDeferred());
    sandbox.session->deferLibraryUpdates();
    CHECK(sandbox.session->libraryUpdatesDeferred());
  }

  SECTION("attaches a plain folder by creating the library catalog")
  {
    const auto plain = sandbox.root / "plain-library";
    writeFile(
      plain / "custom.wad",
      requireProject(project::buildGoldSrcWad({miptexFor("WALL01", 16, 255)})));

    // Without permission nothing is written: attaching must not turn an arbitrary folder
    // into a library by accident.
    const auto refused = sandbox.session->attachLibrary(plain);
    REQUIRE(refused.is_error());
    CHECK_FALSE(project::AssetLibrary::isLibraryFolder(plain));

    const auto attached = requireProject(sandbox.session->attachLibrary(plain, true));
    CHECK(attached.created);
    CHECK(attached.assets == 1);
    CHECK(attached.groups == 1);
    CHECK(project::AssetLibrary::isLibraryFolder(plain));
    const auto libraries = requireProject(sandbox.session->libraries());
    REQUIRE(libraries.size() == 1);
    CHECK(libraries.front().available);
    CHECK(libraries.front().info.id == attached.libraryId);

    // Attaching again opens the same library instead of creating a second one.
    const auto reopened = requireProject(sandbox.session->attachLibrary(plain));
    CHECK_FALSE(reopened.created);
    CHECK(reopened.libraryId == attached.libraryId);
    CHECK(requireProject(sandbox.session->libraries()).size() == 1);
    CHECK(reopened.assets == 1);
  }
  SECTION("browses library resources without adding them to the project")
  {
    requireProject(sandbox.session->attachLibrary(sandbox.libraryFolder));
    requireProject(sandbox.session->save());
    CHECK_FALSE(sandbox.session->modified());
    // Merely looking at a library resource must not create a project dependency.
    CHECK_FALSE(requireProject(
      sandbox.session->projectAssetForLibraryAsset(libraryId, wallLibraryId)));
    const auto preview =
      requireProject(sandbox.session->libraryAssetFile(libraryId, wallLibraryId));
    CHECK(std::filesystem::exists(preview));
    const auto bytes = readProjectFile(preview);
    REQUIRE(bytes.size() > 4);
    // The preview is the GoldSrc compatible result, so the browser shows what a capture
    // would actually embed.
    CHECK(std::string(reinterpret_cast<const char*>(bytes.data()), 4) == "WAD3");
    CHECK(requireProject(sandbox.session->store().assets()).empty());
    CHECK_FALSE(sandbox.session->modified());

    const auto captured =
      requireProject(sandbox.session->captureLibraryAsset(libraryId, wallLibraryId));
    const auto linked = requireProject(
      sandbox.session->projectAssetForLibraryAsset(libraryId, wallLibraryId));
    REQUIRE(linked);
    CHECK(linked->id == captured.id);
    CHECK(linked->library.libraryRevision == 1);
  }

  SECTION("model previews include companions and change when only a companion changes")
  {
    sandbox.write("models/crate.mdl", modelWithGroups(2));
    sandbox.write("models/crate01.mdl", modelWithGroups(1));
    sandbox.write("models/crateT.mdl", modelWithGroups(1));
    sandbox.rescanLibrary();
    requireProject(sandbox.session->attachLibrary(sandbox.libraryFolder));
    const auto assets = sandbox.libraryAssets();
    const auto model = std::ranges::find_if(
      assets, [](const auto& a) { return a.virtualPath == "models/crate.mdl"; });
    REQUIRE(model != assets.end());
    requireProject(sandbox.session->save());
    const auto first =
      requireProject(sandbox.session->libraryAssetFile(libraryId, model->id));
    CHECK(std::filesystem::exists(first.parent_path() / "crateT.mdl"));
    CHECK(std::filesystem::exists(first.parent_path() / "crate01.mdl"));
    auto changed = modelWithGroups(1);
    changed.push_back(42);
    sandbox.write("models/crateT.mdl", changed);
    sandbox.rescanLibrary();
    const auto second =
      requireProject(sandbox.session->libraryAssetFile(libraryId, model->id));
    CHECK(first != second);
    CHECK(readProjectFile(second.parent_path() / "crateT.mdl") == changed);
    CHECK(requireProject(sandbox.session->store().assets()).empty());
    CHECK_FALSE(sandbox.session->modified());
    const auto captured =
      requireProject(sandbox.session->captureLibraryAsset(libraryId, model->id));
    requireProject(sandbox.session->save());
    const auto snapshot = requireProject(sandbox.session->assetFile(captured));
    CHECK(readProjectFile(snapshot.parent_path() / "crateT.mdl") == changed);
    CHECK(std::filesystem::exists(snapshot.parent_path() / "crate01.mdl"));
    changed.push_back(43);
    sandbox.write("models/crateT.mdl", changed);
    sandbox.rescanLibrary();
    CHECK(requireProject(sandbox.session->assetFile(captured)) == snapshot);
    CHECK(readProjectFile(snapshot.parent_path() / "crateT.mdl") != changed);
    const auto latest =
      requireProject(sandbox.session->libraryAssetFile(libraryId, model->id));
    CHECK(readProjectFile(latest.parent_path() / "crateT.mdl") == changed);
    CHECK_FALSE(sandbox.session->modified());
  }

  SECTION("prepared model drags guard existing dependency snapshots")
  {
    sandbox.write("models/crate.mdl", modelWithGroups(1));
    sandbox.write("models/crateT.mdl", modelWithGroups(1));
    sandbox.rescanLibrary();
    requireProject(sandbox.session->attachLibrary(sandbox.libraryFolder));
    const auto assets = sandbox.libraryAssets();
    const auto root = std::ranges::find_if(
      assets, [](const auto& a) { return a.virtualPath == "models/crate.mdl"; });
    const auto companion = std::ranges::find_if(
      assets, [](const auto& a) { return a.virtualPath == "models/crateT.mdl"; });
    REQUIRE(root != assets.end());
    REQUIRE(companion != assets.end());
    const auto captured =
      requireProject(sandbox.session->captureLibraryAsset(libraryId, companion->id));
    requireProject(sandbox.session->save());
    auto asset = BrowserAsset{BrowserCellType::Model, "models/crate.mdl"};
    asset.source = BrowserSource::AssetLibrary;
    asset.libraryId = libraryId;
    asset.libraryAssetId = root->id;
    asset.libraryRevision = root->revision;
    const auto payload = catalogAssetDragData(asset, sandbox.session->store().info().id);
    auto pending = requireProject(PreparedAssetDrop::prepare(sandbox.session, payload));
    requireProject(sandbox.session->renameAsset(captured.id, "Changed while dragging"));
    requireProject(sandbox.session->save());
    CHECK(pending->commit().is_error());
    CHECK(requireProject(sandbox.session->store().assets()).size() == 1);
    CHECK_FALSE(sandbox.session->modified());
  }

  SECTION("capturing a model embeds its dependency closure in one transaction")
  {
    sandbox.write("models/crate.mdl", modelWithGroups(3));
    sandbox.write("models/crate01.mdl", modelWithGroups(1));
    sandbox.write("models/crate02.mdl", modelWithGroups(1));
    sandbox.write("models/crateT.mdl", modelWithGroups(1));
    // A texture that shares the model's name is not a model dependency: GoldSrc models
    // embed their textures.
    sandbox.write("models/crate.png", pngBytes(QColor{200, 120, 40}));
    sandbox.rescanLibrary();
    requireProject(sandbox.session->attachLibrary(sandbox.libraryFolder));

    const auto assets = sandbox.libraryAssets();
    const auto model = std::ranges::find_if(
      assets, [](const auto& asset) { return asset.virtualPath == "models/crate.mdl"; });
    REQUIRE(model != assets.end());

    const auto captured =
      requireProject(sandbox.session->captureLibraryAsset(libraryId, model->id));
    CHECK(captured.type == project::AssetType::Model);
    CHECK(captured.publishPath == "models/crate.mdl");

    // The model is unusable without its separate texture file and animation groups, so
    // the capture commits the whole closure with it.
    const auto projectAssets = requireProject(sandbox.session->store().assets());
    REQUIRE(projectAssets.size() == 4);
    const auto dependencies =
      requireProject(sandbox.session->store().dependencies(captured.id));
    REQUIRE(dependencies.size() == 3);
    auto kinds = std::vector<std::string>{};
    for (const auto& dependency : dependencies)
    {
      kinds.push_back(dependency.kind);
      const auto asset =
        requireProject(sandbox.session->store().findAsset(dependency.dependencyId));
      REQUIRE(asset);
      CHECK(asset->type == project::AssetType::Model);
      CHECK(requireProject(sandbox.session->store().dependents(asset->id)).size() == 1);
    }
    std::ranges::sort(kinds);
    CHECK(
      kinds
      == std::vector<std::string>{
        "model-animation", "model-animation", "model-companion"});
    CHECK_FALSE(std::ranges::any_of(projectAssets, [](const auto& asset) {
      return asset.type == project::AssetType::Texture;
    }));

    // The closure is snapshotted, so losing the library keeps every resource usable.
    std::filesystem::remove_all(sandbox.libraryFolder);
    requireProject(sandbox.session->save());
    sandbox.session.reset();
    sandbox.session = requireProject(ProjectSession::open(sandbox.projectPath));
    CHECK(requireProject(sandbox.session->store().findAsset(captured.id)) == captured);
    CHECK(
      requireProject(
        sandbox.session->store().readAsset(
          dependencies.front().dependencyId, project::ContentKind::Compatible))
        .size()
      > 0);
  }

  SECTION("a dependency the library added after capture reports an update")
  {
    sandbox.write("models/crate.mdl", modelWithGroups(2));
    sandbox.write("models/crate01.mdl", modelWithGroups(1));
    sandbox.write("models/crateT.mdl", modelWithGroups(1));
    sandbox.write("models/crate02.mdl", modelWithGroups(1));
    sandbox.rescanLibrary();
    requireProject(sandbox.session->attachLibrary(sandbox.libraryFolder));
    const auto assets = sandbox.libraryAssets();
    const auto model = std::ranges::find_if(
      assets, [](const auto& asset) { return asset.virtualPath == "models/crate.mdl"; });
    REQUIRE(model != assets.end());
    const auto captured =
      requireProject(sandbox.session->captureLibraryAsset(libraryId, model->id));
    REQUIRE(
      requireProject(sandbox.session->store().dependencies(captured.id)).size() == 2);

    // The library starts declaring one more file for the same model. The captured content
    // is unchanged, so only the graph can reveal that the snapshot is now incomplete.
    const auto extra = std::ranges::find_if(assets, [](const auto& asset) {
      return asset.virtualPath == "models/crate02.mdl";
    });
    REQUIRE(extra != assets.end());
    {
      auto library = requireProject(project::AssetLibrary::open(sandbox.libraryFolder));
      requireProject(library->addDependency(model->id, extra->id, "model-animation"));
    }
    requireProject(sandbox.session->save());

    const auto updates = requireProject(sandbox.session->checkLibraryUpdates());
    const auto update = std::ranges::find_if(updates, [&](const auto& candidate) {
      return candidate.projectAssetId == captured.id;
    });
    REQUIRE(update != updates.end());
    CHECK(update->state == project::LibraryUpdateState::DependenciesChanged);
    CHECK(update->needsAttention());

    requireProject(sandbox.session->updateLibraryAssets({captured.id}));
    // Updating captures the missing dependency and rewrites the graph; the model's own
    // revision stays because its content did not change.
    const auto edges = requireProject(sandbox.session->store().dependencies(captured.id));
    REQUIRE(edges.size() == 3);
    CHECK(std::ranges::any_of(edges, [&](const auto& edge) {
      const auto asset =
        requireProject(sandbox.session->store().findAsset(edge.dependencyId));
      return asset && asset->library.libraryAssetId == extra->id;
    }));
    CHECK(
      requireProject(sandbox.session->store().findAsset(captured.id))->revision
      == captured.revision);
  }
  SECTION("a failed dependency keeps the project untouched")
  {
    // A skybox whose face cannot be decoded: the group itself converts fine, so only the
    // dependency fails. The capture must not leave the group behind.
    for (const auto* face : {"bk", "rt", "lf", "ft", "up", "dn"})
      sandbox.write(
        std::string{"env/desert"} + face + ".png",
        std::string{face} == "dn" ? std::vector<std::uint8_t>{1, 2, 3}
                                  : pngBytes(QColor{90, 140, 200}));
    sandbox.rescanLibrary();
    requireProject(sandbox.session->attachLibrary(sandbox.libraryFolder));
    requireProject(sandbox.session->save());
    CHECK_FALSE(sandbox.session->modified());
    const auto assets = sandbox.libraryAssets();
    const auto skybox = std::ranges::find_if(
      assets, [](const auto& asset) { return asset.type == project::AssetType::Skybox; });
    REQUIRE(skybox != assets.end());

    const auto captured = sandbox.session->captureLibraryAsset(libraryId, skybox->id);
    REQUIRE(captured.is_error());
    // Nothing was committed, and the project is not left marked as modified by a failure.
    CHECK(requireProject(sandbox.session->store().assets()).empty());
    CHECK_FALSE(sandbox.session->modified());
  }
  SECTION("skybox snapshot publishes six source images independently of the library")
  {
    for (const auto* face : {"bk", "rt", "lf", "ft", "up", "dn"})
      sandbox.write(
        std::string{"env/desert"} + face + ".png", pngBytes(QColor{31, 93, 177}, 16));
    auto image = QImage{16, 16, QImage::Format_RGBA8888};
    image.fill(QColor{31, 93, 177});
    image.setPixelColor(0, 0, QColor{255, 0, 0});
    image.setPixelColor(15, 15, QColor{0, 255, 0});
    auto buffer = QBuffer{};
    REQUIRE(buffer.open(QIODevice::WriteOnly));
    REQUIRE(image.save(&buffer, "PNG"));
    const auto png = buffer.data();
    sandbox.write("env/desertbk.png", {png.begin(), png.end()});
    sandbox.rescanLibrary();
    requireProject(sandbox.session->attachLibrary(sandbox.libraryFolder));
    const auto libraryAssets = sandbox.libraryAssets();
    const auto sky = std::ranges::find_if(libraryAssets, [](const auto& asset) {
      return asset.type == project::AssetType::Skybox;
    });
    REQUIRE(sky != libraryAssets.end());
    const auto group =
      requireProject(sandbox.session->captureLibraryAsset(libraryId, sky->id));
    auto stream = std::ostringstream{};
    mdl::writeMapHeader(stream, "Half-Life", mdl::MapFormat::Valve, "Sky");
    stream << R"({
"classname" "worldspawn"
"skyname" "desert"
}
)";
    const auto mapText = stream.str();
    auto input = project::AssetImport{};
    input.type = project::AssetType::Map;
    input.displayName = "Sky";
    input.virtualPath = "maps/sky.map";
    input.original = input.compatible = {mapText.begin(), mapText.end()};
    const auto map = requireProject(sandbox.session->importAsset(input));
    const auto folder = sandbox.root / "sky-snapshot";
    const auto edges = requireProject(sandbox.session->store().dependencies(group.id));
    REQUIRE(edges.size() == 6);

    SECTION("one copy task publishes a portable mixed resource snapshot")
    {
      const auto modelBytes =
        readProjectFile(getFixtureRoot() / "test/publish-model.mdl");
      const auto spriteBytes =
        readProjectFile(getFixtureRoot() / "test/publish-sprite.spr");
      auto soundBytes = std::vector<std::uint8_t>{};
      const auto text = [&](const std::string& value) {
        soundBytes.insert(soundBytes.end(), value.begin(), value.end());
      };
      const auto integer = [&](const std::uint32_t value, const int count) {
        for (auto byte = 0; byte < count; ++byte)
          soundBytes.push_back(std::uint8_t((value >> (byte * 8)) & 255));
      };
      // Eight samples of mono unsigned 8-bit PCM at 11025 Hz.
      text("RIFF");
      integer(44, 4);
      text("WAVEfmt ");
      integer(16, 4);
      integer(1, 2);
      integer(1, 2);
      integer(11025, 4);
      integer(11025, 4);
      integer(1, 2);
      integer(8, 2);
      text("data");
      integer(8, 4);
      soundBytes.insert(soundBytes.end(), 8, 128);
      const auto import = [&](
                            const auto type, const std::string& path, const auto& bytes) {
        auto asset = project::AssetImport{};
        asset.type = type;
        asset.virtualPath = path;
        asset.displayName = path;
        asset.original = asset.compatible = bytes;
        return requireProject(sandbox.session->importAsset(asset));
      };
      const auto model =
        import(project::AssetType::Model, "models/props/crate.mdl", modelBytes);
      const auto sprite =
        import(project::AssetType::Sprite, "sprites/effects/glow.spr", spriteBytes);
      const auto sound =
        import(project::AssetType::Sound, "sound/ambience/wind.wav", soundBytes);
      for (const auto& resource : {model, sprite, sound})
      {
        const auto preview = requireProject(sandbox.session->assetFile(resource));
        const auto bytes = readProjectFile(preview);
        requireProject(sandbox.session->renameAsset(resource.id, "Organized resource"));
        const auto moved = requireProject(sandbox.session->moveAsset(
          resource.id,
          "organized/"
            + std::filesystem::path{resource.virtualPath}.filename().generic_string()));
        CHECK(moved.publishPath == resource.publishPath);
        auto reimport = project::AssetImport{};
        reimport.type = resource.type;
        reimport.sourceName = "reimported source";
        reimport.original = reimport.compatible = bytes;
        const auto replaced =
          requireProject(sandbox.session->replaceAsset(resource.id, reimport));
        CHECK(replaced.id == resource.id);
        CHECK(replaced.publishPath == resource.publishPath);
        CHECK(replaced.virtualPath == moved.virtualPath);
        CHECK(replaced.revision > resource.revision);
        CHECK(
          readProjectFile(requireProject(sandbox.session->assetFile(replaced))) == bytes);
      }
      auto mixedMap = input;
      const auto entities = mapText + R"({
"classname" "cycler"
"model" "models/props/crate.mdl"
}
{
"classname" "env_sprite"
"model" "sprites/effects/glow.spr"
}
{
"classname" "ambient_generic"
"message" "ambience/wind.wav"
}
)";
      mixedMap.original = mixedMap.compatible = {entities.begin(), entities.end()};
      requireProject(sandbox.session->replaceAsset(map.id, mixedMap));
      const auto copy = sandbox.root / "portable-mixed.tbproj";
      requireProject(sandbox.session->saveCopy(copy));
      sandbox.session.reset();
      const auto relocated = sandbox.root / "moved project" / "mixed.tbproj";
      std::filesystem::create_directories(relocated.parent_path());
      std::filesystem::rename(copy, relocated);
      std::filesystem::remove_all(sandbox.libraryFolder);
      std::filesystem::remove(sandbox.projectPath);
      sandbox.session = requireProject(ProjectSession::open(relocated));
      requireProject(sandbox.session->exportCompileSnapshot(map.id, folder));
      // Updating the live project cannot change the already exported publication input.
      auto changedSound = project::AssetImport{};
      changedSound.type = project::AssetType::Sound;
      changedSound.original = changedSound.compatible = soundBytes;
      changedSound.original.back() = changedSound.compatible.back() = 129;
      requireProject(sandbox.session->replaceAsset(sound.id, changedSound));

      auto fixture = MapDocumentFixture{};
      auto& document = fixture.create();
      auto camera = gl::PerspectiveCamera{};
      auto output = QTextEdit{};
      const auto variables = CompilationVariables{document.map(), folder.string()};
      auto context = CompilationContext{
        document.map(), camera, variables, TextOutputAdapter{&output}, false};
      const auto published = sandbox.root / "game with spaces" / "cstrike";
      const auto profile = mdl::CompilationProfile{
        "Publish",
        folder.string(),
        {mdl::CompilationCopyFiles{true, "resources", published.string()}}};
      auto runner = CompilationRunner{std::move(context), profile};
      auto ended = QSignalSpy{&runner, &CompilationRunner::compilationEnded};
      runner.execute();
      REQUIRE(ended.count() == 1);
      INFO(output.toPlainText().toStdString());
      CHECK_FALSE(output.toPlainText().contains("failed", Qt::CaseInsensitive));
      CHECK(readProjectFile(published / "models/props/crate.mdl") == modelBytes);
      CHECK(readProjectFile(published / "sprites/effects/glow.spr") == spriteBytes);
      CHECK(readProjectFile(published / "sound/ambience/wind.wav") == soundBytes);
      for (const auto& entry :
           std::filesystem::recursive_directory_iterator{folder / "resources"})
        if (entry.is_regular_file())
          CHECK(
            readProjectFile(
              published / entry.path().lexically_relative(folder / "resources"))
            == readProjectFile(entry.path()));
      CHECK_FALSE(std::filesystem::exists(published / "resources"));
      CHECK_FALSE(std::filesystem::exists(published / "snapshot.txt"));
      CHECK_FALSE(std::filesystem::exists(published / "map.map"));
    }
    SECTION("missing face blocks the snapshot")
    {
      auto store =
        requireProject(project::ProjectStore::open(sandbox.session->store().path()));
      const auto groupBytes =
        requireProject(store->readAsset(group.id, project::ContentKind::Compatible));
      requireProject(store->removeAsset(group.id));
      auto groupImport = project::AssetImport{};
      groupImport.type = project::AssetType::Skybox;
      groupImport.displayName = group.displayName;
      groupImport.virtualPath = group.virtualPath;
      groupImport.original = groupImport.compatible = groupBytes;
      const auto replacement = requireProject(store->importAsset(groupImport));
      for (auto index = std::size_t{0}; index < 5; ++index)
        requireProject(
          store->addDependency(replacement.id, edges[index].dependencyId, "skybox-face"));
      CHECK(sandbox.session->exportCompileSnapshot(map.id, folder).is_error());
      CHECK_FALSE(std::filesystem::exists(folder));
    }
    SECTION("duplicate face names block the snapshot")
    {
      auto store =
        requireProject(project::ProjectStore::open(sandbox.session->store().path()));
      const auto first = requireProject(store->findAsset(edges[0].dependencyId));
      const auto second = requireProject(store->findAsset(edges[1].dependencyId));
      REQUIRE(first);
      REQUIRE(second);
      auto replacement = project::AssetImport{};
      replacement.type = first->type;
      replacement.sourceName = second->sourceName;
      replacement.original =
        requireProject(store->readAsset(first->id, project::ContentKind::Original));
      replacement.compatible =
        requireProject(store->readAsset(first->id, project::ContentKind::Compatible));
      requireProject(store->replaceAsset(first->id, replacement));
      CHECK(sandbox.session->exportCompileSnapshot(map.id, folder).is_error());
      CHECK_FALSE(std::filesystem::exists(folder));
    }
    SECTION("ambiguous skybox groups block the snapshot")
    {
      auto otherGroup = project::AssetImport{};
      otherGroup.type = project::AssetType::Skybox;
      otherGroup.virtualPath = "other/desert";
      otherGroup.displayName = "Other desert";
      otherGroup.original = otherGroup.compatible = {'s', 'k', 'y'};
      requireProject(sandbox.session->importAsset(otherGroup));
      CHECK(sandbox.session->exportCompileSnapshot(map.id, folder).is_error());
      CHECK_FALSE(std::filesystem::exists(folder));
    }
    SECTION("damaged original image blocks publishing even when its WAD is valid")
    {
      auto store =
        requireProject(project::ProjectStore::open(sandbox.session->store().path()));
      const auto face = requireProject(store->findAsset(edges.front().dependencyId));
      REQUIRE(face);
      auto replacement = project::AssetImport{};
      replacement.type = face->type;
      replacement.sourceName = face->sourceName;
      replacement.original = {1, 2, 3};
      replacement.compatible =
        requireProject(store->readAsset(face->id, project::ContentKind::Compatible));
      requireProject(store->replaceAsset(face->id, replacement));
      CHECK(sandbox.session->exportCompileSnapshot(map.id, folder).is_error());
      CHECK_FALSE(std::filesystem::exists(folder));
    }
    SECTION("mismatched face dimensions block the snapshot")
    {
      sandbox.write("env/desertdn.png", pngBytes(QColor{31, 93, 177}, 32));
      sandbox.rescanLibrary();
      const auto updated = sandbox.libraryAssets();
      const auto face = std::ranges::find_if(updated, [](const auto& asset) {
        return asset.virtualPath == "env/desertdn.png";
      });
      REQUIRE(face != updated.end());
      requireProject(sandbox.session->captureLibraryAsset(libraryId, face->id));
      CHECK(sandbox.session->exportCompileSnapshot(map.id, folder).is_error());
      CHECK_FALSE(std::filesystem::exists(folder));
    }
    SECTION("updates affect only new snapshots and originals survive library loss")
    {
      requireProject(sandbox.session->exportCompileSnapshot(map.id, folder));
      const auto before = readProjectFile(folder / "resources/gfx/env/desertbk.tga");
      const auto decoded = requireProject(img::decodeImage(before.data(), before.size()));
      CHECK(decoded.width == 256);
      CHECK(decoded.height == 256);
      REQUIRE(before.size() == 18 + 256 * 256 * 3);
      CHECK(before[2] == 2);
      CHECK(before[16] == 24);
      CHECK(before[17] == 0);
      CHECK(decoded.pixels[0] == 255);
      CHECK(decoded.pixels[1] == 0);
      CHECK(decoded.pixels[(256 * 256 - 1) * 4 + 1] == 255);
      // The first TGA pixel is the source bottom-left, stored in BGR order.
      CHECK(before[18] == 177);
      CHECK(before[19] == 93);
      CHECK(before[20] == 31);
      const auto metadata = readProjectFile(folder / "snapshot.txt");
      const auto text = std::string{metadata.begin(), metadata.end()};
      CHECK(text.find("external gfx/env/") == std::string::npos);
      CHECK(text.find("hash=" + project::contentHash(before)) != std::string::npos);
      CHECK(
        text.find(
          "sourceHash="
          + project::contentHash(std::vector<std::uint8_t>{png.begin(), png.end()}))
        != std::string::npos);
      sandbox.write("env/desertbk.png", pngBytes(QColor{1, 2, 3}, 16));
      sandbox.rescanLibrary();
      const auto updated = sandbox.libraryAssets();
      const auto face = std::ranges::find_if(updated, [](const auto& asset) {
        return asset.virtualPath == "env/desertbk.png";
      });
      REQUIRE(face != updated.end());
      requireProject(sandbox.session->captureLibraryAsset(libraryId, face->id));
      CHECK(readProjectFile(folder / "resources/gfx/env/desertbk.tga") == before);
      requireProject(sandbox.session->save());
      sandbox.session.reset();
      std::filesystem::remove_all(sandbox.libraryFolder);
      sandbox.session = requireProject(ProjectSession::open(sandbox.projectPath));
      const auto next = sandbox.root / "sky-next";
      requireProject(sandbox.session->exportCompileSnapshot(map.id, next));
      CHECK(readProjectFile(next / "resources/gfx/env/desertbk.tga") != before);
      for (const auto* suffix : {"bk", "rt", "lf", "ft", "up", "dn"})
        CHECK(std::filesystem::exists(
          next / (std::string{"resources/gfx/env/desert"} + suffix + ".tga")));
    }
    SECTION("native and high resolution faces publish at the stock engine size")
    {
      const auto sourceSize = GENERATE(256, 512);
      auto source = QImage{sourceSize, sourceSize, QImage::Format_RGBA8888};
      source.fill(QColor{31, 93, 177});
      // Large corner blocks survive filtering and expose flips or swapped RGB channels.
      for (auto y = 0; y < sourceSize / 4; ++y)
        for (auto x = 0; x < sourceSize / 4; ++x)
        {
          source.setPixelColor(x, y, QColor{255, 0, 0});
          source.setPixelColor(sourceSize - 1 - x, sourceSize - 1 - y, QColor{0, 255, 0});
        }
      auto encoded = QByteArray{};
      auto output = QBuffer{&encoded};
      REQUIRE(output.open(QIODevice::WriteOnly));
      REQUIRE(source.save(&output, "PNG"));
      for (const auto* suffix : {"bk", "rt", "lf", "ft", "up", "dn"})
        sandbox.write(
          std::string{"env/desert"} + suffix + ".png", {encoded.begin(), encoded.end()});
      sandbox.rescanLibrary();
      for (const auto& face : sandbox.libraryAssets())
        if (
          face.virtualPath.starts_with("env/desert")
          && face.virtualPath.ends_with(".png"))
          requireProject(sandbox.session->captureLibraryAsset(libraryId, face.id));
      requireProject(sandbox.session->exportCompileSnapshot(map.id, folder));
      for (const auto* suffix : {"bk", "rt", "lf", "ft", "up", "dn"})
      {
        const auto bytes = readProjectFile(
          folder / (std::string{"resources/gfx/env/desert"} + suffix + ".tga"));
        REQUIRE(bytes.size() == 18 + 256 * 256 * 3);
        const auto decoded = requireProject(img::decodeImage(bytes.data(), bytes.size()));
        CHECK(decoded.width == 256);
        CHECK(decoded.height == 256);
        CHECK(decoded.pixels[0] == 255);
        CHECK(decoded.pixels[1] == 0);
        CHECK(decoded.pixels[2] == 0);
        CHECK(decoded.pixels[(256 * 256 - 1) * 4 + 1] == 255);
        CHECK(decoded.pixels[(128 * 256 + 128) * 4] == 31);
        CHECK(decoded.pixels[(128 * 256 + 128) * 4 + 1] == 93);
        CHECK(decoded.pixels[(128 * 256 + 128) * 4 + 2] == 177);
      }
      const auto originalFace =
        requireProject(sandbox.session->store().findAsset(edges.front().dependencyId));
      REQUIRE(originalFace);
      const auto original = requireProject(sandbox.session->store().readAsset(
        originalFace->id, project::ContentKind::Original));
      CHECK(original == std::vector<std::uint8_t>{encoded.begin(), encoded.end()});
    }
  }

  SECTION("exports a fixed compile snapshot that no longer touches the library")
  {
    requireProject(sandbox.session->attachLibrary(sandbox.libraryFolder));
    requireProject(sandbox.session->captureLibraryAsset(libraryId, wallLibraryId));

    auto stream = std::ostringstream{};
    mdl::writeMapHeader(stream, "Half-Life", mdl::MapFormat::Valve, "Snapshot");
    // A GoldSrc map is Valve 220; the compiler reads the version from the entity. Braces
    // stay on their own line because that is what TrenchBroom writes and what external
    // tools expect to see.
    stream << R"({
"classname" "worldspawn"
"mapversion" "220"
{
( 0 0 0 ) ( 128 0 0 ) ( 0 128 0 ) WALL01 [ 1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1
( 0 0 128 ) ( 0 128 128 ) ( 128 0 128 ) WALL01 [ 1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1
( 0 0 0 ) ( 0 128 0 ) ( 0 0 128 ) WALL01 [ 1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1
( 128 0 0 ) ( 128 0 128 ) ( 128 128 0 ) WALL01 [ 1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1
( 0 0 0 ) ( 0 0 128 ) ( 128 0 0 ) WALL01 [ 1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1
( 0 128 0 ) ( 128 128 0 ) ( 0 128 128 ) WALL01 [ 1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1
}
}
)";
    const auto mapText = stream.str();
    auto mapImport = project::AssetImport{};
    mapImport.type = project::AssetType::Map;
    mapImport.virtualPath = "maps/compile.map";
    mapImport.displayName = "Compile";
    mapImport.original = {mapText.begin(), mapText.end()};
    mapImport.compatible = mapImport.original;
    const auto map = requireProject(sandbox.session->importAsset(mapImport));

    const auto folder = sandbox.root / "snapshot";
    requireProject(sandbox.session->exportCompileSnapshot(map.id, folder));
    CHECK(readProjectFile(folder / "map.map") == mapImport.compatible);

    // Only the textures this map references are exported, and they arrive as GoldSrc
    // miptex entries the compiler can embed.
    const auto wad = project::readGoldSrcWad(readProjectFile(folder / "textures.wad"));
    REQUIRE(wad.is_success());
    REQUIRE(wad.value().textures.size() == 1);
    CHECK(wad.value().textures.front().name == "WALL01");
    CHECK(wad.value().textures.front().valid);

    const auto config = readProjectFile(folder / "wads.cfg");
    CHECK(std::string(config.begin(), config.end()) == "textures.wad\n");
    const auto metadata = readProjectFile(folder / "snapshot.txt");
    const auto metadataText = std::string(metadata.begin(), metadata.end());
    CHECK(metadataText.find("texture WALL01") != std::string::npos);
    CHECK(metadataText.find(map.id) != std::string::npos);
    // No library path may leak into the fixed input.
    CHECK(metadataText.find(sandbox.libraryFolder.generic_string()) == std::string::npos);

    // Removing the library must not change what a compile of this project would see.
    std::filesystem::remove_all(sandbox.libraryFolder);
    const auto secondFolder = sandbox.root / "snapshot-after-loss";
    requireProject(sandbox.session->exportCompileSnapshot(map.id, secondFolder));
    CHECK(
      readProjectFile(secondFolder / "textures.wad")
      == readProjectFile(folder / "textures.wad"));
    CHECK(
      readProjectFile(secondFolder / "map.map") == readProjectFile(folder / "map.map"));

    // A compilation exports the map under its own name so profiles that derive their
    // outputs from ${MAP_BASE_NAME} keep the artifact names, and a later project change
    // can never rewrite an input another run is compiling.
    const auto namedFolder = sandbox.root / "snapshot-named";
    requireProject(
      sandbox.session->exportCompileSnapshot(map.id, namedFolder, "compile.map"));
    CHECK(readProjectFile(namedFolder / "compile.map") == mapImport.compatible);
    CHECK_FALSE(std::filesystem::exists(namedFolder / "map.map"));
    const auto namedMetadata = readProjectFile(namedFolder / "snapshot.txt");
    CHECK(
      std::string(namedMetadata.begin(), namedMetadata.end()).find("mapFile=compile.map")
      != std::string::npos);
    CHECK(sandbox.session
            ->exportCompileSnapshot(map.id, sandbox.root / "rejected", "sub/escape.map")
            .is_error());

    auto changedMap = mapImport;
    changedMap.original = changedMap.compatible = {'{', '}'};
    requireProject(sandbox.session->replaceAsset(map.id, changedMap));
    CHECK(readProjectFile(namedFolder / "compile.map") == mapImport.compatible);
  }

  SECTION("exports only the referenced texture and its dependency closure")
  {
    // One project asset can be a WAD with many textures; only the one a face uses may be
    // exported.
    const auto wallMiptex = miptexFor("WALL01", 16, 255);
    const auto floorMiptex = miptexFor("FLOOR01", 32, 128);
    auto packImport = project::AssetImport{};
    packImport.type = project::AssetType::Texture;
    packImport.virtualPath = "textures/pack.wad";
    packImport.displayName = "Pack";
    packImport.original =
      requireProject(project::buildGoldSrcWad({wallMiptex, floorMiptex}));
    packImport.compatible = packImport.original;
    requireProject(sandbox.session->importAsset(packImport));

    auto stream = std::ostringstream{};
    mdl::writeMapHeader(stream, "Half-Life", mdl::MapFormat::Valve, "Snapshot");
    stream << R"({
"classname" "worldspawn"
"mapversion" "220"
"skyname" "desert"
{
( 0 0 0 ) ( 128 0 0 ) ( 0 128 0 ) WALL01 [ 1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1
( 0 0 128 ) ( 0 128 128 ) ( 128 0 128 ) clip [ 1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1
}
}
)";
    const auto mapText = stream.str();
    auto mapImport = project::AssetImport{};
    mapImport.type = project::AssetType::Map;
    mapImport.virtualPath = "maps/partial.map";
    mapImport.displayName = "Partial";
    mapImport.original = {mapText.begin(), mapText.end()};
    mapImport.compatible = mapImport.original;
    const auto map = requireProject(sandbox.session->importAsset(mapImport));

    const auto folder = sandbox.root / "partial-snapshot";
    requireProject(sandbox.session->exportCompileSnapshot(map.id, folder));
    const auto wad = project::readGoldSrcWad(readProjectFile(folder / "textures.wad"));
    REQUIRE(wad.is_success());
    REQUIRE(wad.value().textures.size() == 1);
    CHECK(wad.value().textures.front().name == "WALL01");
    // Tool textures and skybox faces are supplied by the compiler and the game, so they
    // are recorded as external instead of silently dropped.
    const auto metadata = readProjectFile(folder / "snapshot.txt");
    const auto text = std::string(metadata.begin(), metadata.end());
    INFO(text);
    CHECK(text.find("external gfx/env/desertbk.tga") != std::string::npos);

    // A genuinely absent texture blocks the export instead of producing a broken BSP.
    auto brokenStream = std::ostringstream{};
    mdl::writeMapHeader(brokenStream, "Half-Life", mdl::MapFormat::Valve, "Snapshot");
    brokenStream << R"({
"classname" "worldspawn"
"mapversion" "220"
{
( 0 0 0 ) ( 1 0 0 ) ( 0 1 0 ) ABSENT [ 1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1
}
}
)";
    const auto brokenText = brokenStream.str();
    auto brokenImport = project::AssetImport{};
    brokenImport.type = project::AssetType::Map;
    brokenImport.virtualPath = "maps/broken.map";
    brokenImport.displayName = "Broken";
    brokenImport.original = {brokenText.begin(), brokenText.end()};
    brokenImport.compatible = brokenImport.original;
    const auto broken = requireProject(sandbox.session->importAsset(brokenImport));
    const auto rejected =
      sandbox.session->exportCompileSnapshot(broken.id, sandbox.root / "broken-snapshot");
    REQUIRE(rejected.is_error());
    CHECK(std::get<Error>(rejected.error()).msg.find("ABSENT") != std::string::npos);
    CHECK_FALSE(
      std::filesystem::exists(sandbox.root / "broken-snapshot" / "textures.wad"));
  }

  SECTION("exports the captured dependency closure with the map resources")
  {
    sandbox.write("models/crate.mdl", modelWithGroups(2));
    sandbox.write("models/crate01.mdl", modelWithGroups(1));
    sandbox.write("models/crateT.mdl", modelWithGroups(1));
    sandbox.rescanLibrary();
    requireProject(sandbox.session->attachLibrary(sandbox.libraryFolder));
    const auto assets = sandbox.libraryAssets();
    const auto model = std::ranges::find_if(
      assets, [](const auto& asset) { return asset.virtualPath == "models/crate.mdl"; });
    REQUIRE(model != assets.end());
    const auto captured =
      requireProject(sandbox.session->captureLibraryAsset(libraryId, model->id));

    auto stream = std::ostringstream{};
    mdl::writeMapHeader(stream, "Half-Life", mdl::MapFormat::Valve, "Snapshot");
    stream << R"({
"classname" "worldspawn"
"mapversion" "220"
}
{
"classname" "cycler"
"model" "models/crate.mdl"
"origin" "0 0 0"
}
)";
    const auto mapText = stream.str();
    auto mapImport = project::AssetImport{};
    mapImport.type = project::AssetType::Map;
    mapImport.virtualPath = "maps/model.map";
    mapImport.displayName = "Model";
    mapImport.original = {mapText.begin(), mapText.end()};
    mapImport.compatible = mapImport.original;
    const auto map = requireProject(sandbox.session->importAsset(mapImport));

    const auto folder = sandbox.root / "model-snapshot";
    requireProject(sandbox.session->exportCompileSnapshot(map.id, folder));
    // The model and everything it depends on ship with the snapshot.
    CHECK(std::filesystem::exists(folder / "resources" / "models" / "crate.mdl"));
    CHECK(std::filesystem::exists(folder / "resources" / "models" / "crate01.mdl"));
    CHECK(std::filesystem::exists(folder / "resources" / "models" / "crateT.mdl"));
    const auto metadata = readProjectFile(folder / "snapshot.txt");
    const auto text = std::string(metadata.begin(), metadata.end());
    CHECK(text.find("id=" + captured.id) != std::string::npos);
    CHECK(text.find(" dependency") != std::string::npos);
  }
  SECTION("a newer library revision reaches every open map at once")
  {
    auto config = mdl::QuakeFixtureConfig;
    config.gameInfo.gameConfig.materialConfig.extensions = {".C"};
    auto firstFixture = MapDocumentFixture{};
    auto secondFixture = MapDocumentFixture{};
    auto& first = firstFixture.create(config);
    auto& second = secondFixture.create(config);

    auto mapImport = project::AssetImport{};
    mapImport.type = project::AssetType::Map;
    mapImport.virtualPath = "maps/first.map";
    mapImport.displayName = "First";
    mapImport.compatible = {'{', '\n', '}', '\n'};
    mapImport.original = mapImport.compatible;
    const auto firstMap = requireProject(sandbox.session->importAsset(mapImport));
    mapImport.virtualPath = "maps/second.map";
    const auto secondMap = requireProject(sandbox.session->importAsset(mapImport));
    requireProject(sandbox.session->registerDocument(first, firstMap.id));
    requireProject(sandbox.session->registerDocument(second, secondMap.id));
    requireProject(sandbox.session->attachLibrary(sandbox.libraryFolder));

    const auto captured =
      requireProject(sandbox.session->captureLibraryAsset(libraryId, wallLibraryId));
    CHECK(first.map().materialManager().material("WALL01") != nullptr);
    CHECK(second.map().materialManager().material("wall01") != nullptr);
    const auto before = readProjectFile(
      first.map().gameFileSystem().projectDirectory() / "textures/WALL01.wad");

    sandbox.writeWad({miptexFor("WALL01", 16, 32), floor});
    sandbox.rescanLibrary();
    requireProject(sandbox.session->updateLibraryAssets({captured.id}));

    // Both maps are served from the same project asset revision, so a single update is
    // visible in every open document.
    const auto firstRoot = first.map().gameFileSystem().projectDirectory();
    const auto secondRoot = second.map().gameFileSystem().projectDirectory();
    CHECK(firstRoot == secondRoot);
    const auto after = readProjectFile(firstRoot / "textures/WALL01.wad");
    CHECK(after != before);
    CHECK(readProjectFile(secondRoot / "textures/WALL01.wad") == after);
    CHECK(first.map().materialManager().material("WALL01") != nullptr);
    CHECK(second.map().materialManager().material("WALL01") != nullptr);
    sandbox.session->unregisterDocument(first);
    sandbox.session->unregisterDocument(second);
  }

  SECTION("same named library resources cannot silently replace each other")
  {
    sandbox.writeWad({wall}, "a/duplicate.wad");
    sandbox.writeWad({miptexFor("WALL01", 16, 32)}, "b/duplicate.wad");
    sandbox.rescanLibrary();
    requireProject(sandbox.session->attachLibrary(sandbox.libraryFolder));
    const auto assets = sandbox.libraryAssets();
    const auto first = std::ranges::find_if(assets, [](const auto& asset) {
      return asset.sourceContainer == "a/duplicate.wad";
    });
    const auto second = std::ranges::find_if(assets, [](const auto& asset) {
      return asset.sourceContainer == "b/duplicate.wad";
    });
    REQUIRE(first != assets.end());
    REQUIRE(second != assets.end());
    CHECK(first->displayName == second->displayName);
    CHECK(first->contentHash != second->contentHash);
    const auto captured =
      requireProject(sandbox.session->captureLibraryAsset(libraryId, first->id));
    CHECK(captured.publishPath == "textures/WALL01.wad");
    // Different content with the same engine texture name is a conflict, not a rename.
    CHECK(sandbox.session->captureLibraryAsset(libraryId, second->id).is_error());
    CHECK(requireProject(sandbox.session->store().assets()).size() == 1);
  }
}
} // namespace tb::ui
