/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "img/GoldSrcTexture.h"
#include "project/GoldSrcWad.h"
#include "ui/CatalogAssetDrop.h"
#include "ui/ProjectReference.h"
#include "ui/ProjectSession.h"
#include "ui/QPathUtils.h"

#include <limits>

#include <catch2/catch_test_macros.hpp>
namespace tb::ui
{
namespace
{
template <typename T>
T require(Result<T> value)
{
  if (value.is_error())
    FAIL(std::get<Error>(value.error()).msg);
  return std::move(value).value();
}
void require(Result<void> value)
{
  if (value.is_error())
    FAIL(std::get<Error>(value.error()).msg);
}
void write(const QString& file, const QByteArray& bytes)
{
  auto output = QFile{file};
  REQUIRE(output.open(QIODevice::WriteOnly));
  REQUIRE(output.write(bytes) == bytes.size());
}
QByteArray textureWad(const std::string& name)
{
  auto converted = img::convertGoldSrcTexture(
    {16, 16, std::vector<unsigned char>(16 * 16 * 4, 255)}, name);
  if (converted.is_error())
    FAIL(std::get<Error>(converted.error()).msg);
  const auto& wad = std::move(converted).value().wad;
  return QByteArray{reinterpret_cast<const char*>(wad.data()), int(wad.size())};
}
std::vector<std::uint8_t> textureLump(const std::string& name)
{
  const auto wad = textureWad(name);
  const auto archive =
    project::readGoldSrcWad({wad.begin(), wad.end()});
  if (archive.is_error())
    FAIL(std::get<Error>(archive.error()).msg);
  REQUIRE(archive.value().textures.size() == 1);
  return archive.value().textures.front().miptex;
}
} // namespace
TEST_CASE("CatalogAssetDrop")
{
  auto temporary = QTemporaryDir{};
  REQUIRE(temporary.isValid());
  const auto folder = temporary.filePath("library");
  REQUIRE(QDir{}.mkdir(folder));
  const auto source = folder + "/clip.wav";
  write(source, "RIFF original");
  auto library =
    require(project::AssetLibrary::create(pathFromQString(folder), "Library"));
  require(library->scan());
  const auto entries = require(library->assets());
  REQUIRE(entries.size() == 1);
  const auto& entry = entries.front();
  auto session = require(ProjectSession::create(
    pathFromQString(temporary.filePath("project.tbproj")), "Project", "Half-Life"));
  require(session->attachLibrary(pathFromQString(folder)));
  require(session->save());
  auto asset = BrowserAsset{BrowserCellType::Sound, "clip.wav"};
  asset.source = BrowserSource::AssetLibrary;
  asset.libraryId = library->info().id;
  asset.libraryAssetId = entry.id;
  asset.libraryRevision = entry.revision;
  const auto text = catalogAssetDragData(asset, session->store().info().id);
  REQUIRE(isCatalogAssetDrag(text));

  SECTION("preparation and cancellation never capture resources")
  {
    auto pending = require(PreparedAssetDrop::prepare(session, text));
    CHECK(pending->nativePayload() == "sound:sound/clip.wav");
    CHECK(pending->matches(text));
    CHECK_FALSE(pending->matches(text + " "));
    CHECK(require(session->store().assets()).empty());
    CHECK_FALSE(session->modified());
    pending.reset();
    CHECK(require(session->store().assets()).empty());
    CHECK_FALSE(session->modified());
  }
  SECTION("commit captures the selected revision exactly once")
  {
    auto pending = require(PreparedAssetDrop::prepare(session, text));
    write(source, "RIFF changed");
    require(library->scan());
    require(pending->commit());
    const auto captured = require(session->store().assets());
    REQUIRE(captured.size() == 1);
    CHECK(captured.front().library.libraryRevision == entry.revision);
    const auto bytes = require(
      session->store().readAsset(captured.front().id, project::ContentKind::Compatible));
    CHECK(std::string(bytes.begin(), bytes.end()) == "RIFF original");
    CHECK(session->modified());
    CHECK(pending->commit().is_error());
    CHECK(require(session->store().assets()).size() == 1);
  }
  SECTION("stale library revisions and cross-project drags are rejected")
  {
    const auto foreign = catalogAssetDragData(asset, "another-project");
    CHECK(PreparedAssetDrop::prepare(session, foreign).is_error());
    write(source, "RIFF changed");
    require(library->scan());
    CHECK(PreparedAssetDrop::prepare(session, text).is_error());
    CHECK(require(session->store().assets()).empty());
    CHECK_FALSE(session->modified());
  }
  SECTION("conflicting captures leave the entire project unchanged")
  {
    auto pending = require(PreparedAssetDrop::prepare(session, text));
    auto input = project::AssetImport{};
    input.type = project::AssetType::Sound;
    input.virtualPath = "sound/clip.wav";
    input.displayName = "Existing";
    input.original = {'x'};
    input.compatible = {'x'};
    const auto existing = require(session->importAsset(input));
    require(session->save());
    CHECK(pending->commit().is_error());
    const auto assets = require(session->store().assets());
    REQUIRE(assets.size() == 1);
    CHECK(assets.front() == existing);
    CHECK_FALSE(session->modified());
  }
  SECTION("project resource drags reject changes made while dragging")
  {
    const auto captured =
      require(session->captureLibraryAsset(asset.libraryId, entry.id));
    require(session->save());
    asset.source = BrowserSource::Project;
    asset.assetId = captured.id;
    asset.revision = captured.revision;
    const auto payload = catalogAssetDragData(asset, session->store().info().id);
    auto pending = require(PreparedAssetDrop::prepare(session, payload));
    CHECK(pending->nativePayload() == "sound:sound/clip.wav");
    require(session->renameAsset(captured.id, "Renamed"));
    require(session->save());
    CHECK(pending->commit().is_error());
    CHECK_FALSE(session->modified());
    CHECK(require(session->store().assets()).size() == 1);
  }
  SECTION("payloads reject malformed identities and keep 64-bit revisions exact")
  {
    CHECK(PreparedAssetDrop::prepare(session, "tb-asset:{}").is_error());
    CHECK(PreparedAssetDrop::prepare(session, "tb-asset:invalid").is_error());
    CHECK(PreparedAssetDrop::prepare(nullptr, text).is_error());
    asset.libraryRevision = std::numeric_limits<std::uint64_t>::max();
    const auto large = catalogAssetDragData(asset, session->store().info().id);
    const auto object =
      QJsonDocument::fromJson(large.mid(QString{"tb-asset:"}.size()).toUtf8()).object();
    CHECK(object.value("revision").toString() == "18446744073709551615");
    CHECK(PreparedAssetDrop::prepare(session, large).is_error());
  }
  SECTION("texture drops carry the miptex name the map will use")
  {
    const auto textureFolder = temporary.filePath("texture-library");
    REQUIRE(QDir{}.mkdir(textureFolder));
    write(textureFolder + "/custom.wad", textureWad("WALL01"));
    auto textureLibrary =
      require(project::AssetLibrary::create(pathFromQString(textureFolder), "Textures"));
    require(textureLibrary->scan());
    const auto textureEntries = require(textureLibrary->assets());
    REQUIRE(textureEntries.size() == 1);
    auto textureSession = require(ProjectSession::create(
      pathFromQString(temporary.filePath("texture.tbproj")), "Textures", "Half-Life"));
    require(textureSession->attachLibrary(pathFromQString(textureFolder)));
    require(textureSession->save());

    auto libraryTexture = BrowserAsset{BrowserCellType::Texture, {}};
    libraryTexture.source = BrowserSource::AssetLibrary;
    libraryTexture.libraryId = textureLibrary->info().id;
    libraryTexture.libraryAssetId = textureEntries.front().id;
    libraryTexture.libraryRevision = textureEntries.front().revision;
    const auto libraryPayload =
      catalogAssetDragData(libraryTexture, textureSession->store().info().id);
    REQUIRE(isCatalogAssetDrag(libraryPayload));
    auto pending = require(PreparedAssetDrop::prepare(textureSession, libraryPayload));
    CHECK(pending->nativePayload() == "texture:WALL01");
    CHECK(require(textureSession->store().assets()).empty());
    require(pending->commit());

    const auto captured = require(textureSession->store().assets());
    REQUIRE(captured.size() == 1);
    CHECK(captured.front().publishPath == "textures/WALL01.wad");
    CHECK(require(gameName(*textureSession, captured.front())) == "WALL01");

    auto projectTexture = BrowserAsset{BrowserCellType::Texture, {}};
    projectTexture.source = BrowserSource::Project;
    projectTexture.assetId = captured.front().id;
    projectTexture.revision = captured.front().revision;
    auto projectPending = require(PreparedAssetDrop::prepare(
      textureSession,
      catalogAssetDragData(projectTexture, textureSession->store().info().id)));
    CHECK(projectPending->nativePayload() == "texture:WALL01");

    auto mixed = project::AssetImport{};
    mixed.type = project::AssetType::Texture;
    mixed.virtualPath = "textures/mixed.wad";
    mixed.displayName = "Mixed";
    mixed.original = mixed.compatible =
      require(project::buildGoldSrcWad({textureLump("first"), textureLump("second")}));
    const auto mixedAsset = require(textureSession->importAsset(mixed));
    require(textureSession->save());
    auto mixedDrag = BrowserAsset{BrowserCellType::Texture, {}};
    mixedDrag.source = BrowserSource::Project;
    mixedDrag.assetId = mixedAsset.id;
    mixedDrag.revision = mixedAsset.revision;
    const auto mixedPending = PreparedAssetDrop::prepare(
      textureSession,
      catalogAssetDragData(mixedDrag, textureSession->store().info().id));
    CHECK(mixedPending.is_error());
  }
}
} // namespace tb::ui
