/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QBuffer>
#include <QColor>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include "gl/MaterialManager.h"
#include "img/GoldSrcTexture.h"
#include "mdl/GameFileSystem.h"
#include "mdl/Map.h"
#include "mdl/MapHeader.h"
#include "mdl/Map_Entities.h"
#include "project/AssetLibrary.h"
#include "project/GoldSrcWad.h"
#include "project/LibrarySync.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"
#include "ui/ProjectSession.h"
#include "ui/QPathUtils.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include <catch2/catch_test_macros.hpp>

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
