/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom.
*/

#include "GoldSrcFixtures.h"
#include "project/ContentHash.h"
#include "project/GoldSrcWad.h"
#include "project/LibrarySync.h"

#include <map>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace tb::project
{
namespace
{

LibraryAsset libraryAsset(
  const std::string& id,
  const std::string& name,
  const std::string& hash,
  const std::uint64_t revision = 1,
  std::string nativeFormat = "goldsrc-miptex")
{
  auto asset = LibraryAsset{};
  asset.id = id;
  asset.type = AssetType::Texture;
  asset.displayName = name;
  asset.virtualPath = "custom.wad/" + name;
  asset.group = "custom.wad";
  asset.sourceContainer = "custom.wad";
  asset.sourceMember = name;
  asset.nativeFormat = std::move(nativeFormat);
  asset.recipe = R"({"importer":"wad-member","version":1})";
  asset.revision = revision;
  asset.contentHash = hash;
  asset.size = 128;
  return asset;
}

Asset projectAsset(
  const std::string& id, const LibraryLink& link, const std::string& name)
{
  auto asset = Asset{};
  asset.id = id;
  asset.type = AssetType::Texture;
  asset.virtualPath = "textures/" + name + ".wad";
  asset.displayName = name;
  asset.publishPath = asset.virtualPath;
  asset.revision = 1;
  asset.library = link;
  return asset;
}

LibraryLink linkFor(
  const std::string& libraryId, const std::string& assetId, const std::string& hash)
{
  auto link = LibraryLink{};
  link.libraryId = libraryId;
  link.libraryAssetId = assetId;
  link.libraryRevision = 1;
  link.libraryContentHash = hash;
  link.sourceContainer = "custom.wad";
  link.sourceMember = "WALL01";
  return link;
}

constexpr auto ProjectAssetId = "11111111-1111-1111-1111-111111111111";
constexpr auto LibraryId = "22222222-2222-2222-2222-222222222222";
constexpr auto LibraryAssetId = "33333333-3333-3333-3333-333333333333";

} // namespace

TEST_CASE("LibrarySync")
{
  SECTION("reports content that still matches the captured revision")
  {
    const auto hash = std::string(64, 'a');
    const auto assets = std::vector<Asset>{
      projectAsset(ProjectAssetId, linkFor(LibraryId, LibraryAssetId, hash), "WALL01")};
    auto state = LibraryState{LibraryId, "Library", "/library", true, {}, {}};
    state.assets.push_back(libraryAsset(LibraryAssetId, "WALL01", hash));

    const auto updates = evaluateLibraryUpdates(assets, {state});
    REQUIRE(updates.size() == 1);
    CHECK(updates.front().state == LibraryUpdateState::UpToDate);
    CHECK_FALSE(updates.front().needsAttention());
    CHECK(updates.front().available.has_value());
  }

  SECTION("detects a newer library revision without touching the project")
  {
    const auto captured = std::string(64, 'a');
    const auto changed = std::string(64, 'b');
    const auto assets = std::vector<Asset>{projectAsset(
      ProjectAssetId, linkFor(LibraryId, LibraryAssetId, captured), "WALL01")};
    auto state = LibraryState{LibraryId, "Library", "/library", true, {}, {}};
    state.assets.push_back(libraryAsset(LibraryAssetId, "WALL01", changed, 2));

    const auto updates = evaluateLibraryUpdates(assets, {state});
    REQUIRE(updates.size() == 1);
    CHECK(updates.front().state == LibraryUpdateState::NewVersion);
    CHECK(updates.front().needsAttention());
    CHECK(updates.front().available->revision == 2);
    CHECK(updates.front().link.libraryContentHash == captured);
    CHECK(updates.front().projectRevision == 1);
  }

  SECTION("distinguishes a deleted library asset from an unreadable library")
  {
    const auto captured = std::string(64, 'a');
    const auto assets = std::vector<Asset>{projectAsset(
      ProjectAssetId, linkFor(LibraryId, LibraryAssetId, captured), "WALL01")};

    auto deleted = LibraryState{LibraryId, "Library", "/library", true, {}, {}};
    const auto missingUpdates = evaluateLibraryUpdates(assets, {deleted});
    REQUIRE(missingUpdates.size() == 1);
    CHECK(missingUpdates.front().state == LibraryUpdateState::Missing);
    CHECK(missingUpdates.front().needsAttention());

    auto vanished =
      LibraryState{LibraryId, "Library", "/library", false, "not found", {}};
    auto asset = libraryAsset(LibraryAssetId, "WALL01", captured);
    asset.missing = true;
    vanished.assets.push_back(asset);
    // A library that cannot be opened hides its assets entirely.
    const auto unavailable = evaluateLibraryUpdates(assets, {vanished});
    REQUIRE(unavailable.size() == 1);
    CHECK(unavailable.front().state == LibraryUpdateState::LibraryUnavailable);

    auto inaccessible = LibraryState{LibraryId, "Library", "/library", true, {}, {}};
    inaccessible.assets.push_back(asset);
    const auto inaccessibleUpdates = evaluateLibraryUpdates(assets, {inaccessible});
    REQUIRE(inaccessibleUpdates.size() == 1);
    CHECK(inaccessibleUpdates.front().state == LibraryUpdateState::Missing);
  }

  SECTION("honours ignored library revisions")
  {
    const auto captured = std::string(64, 'a');
    const auto changed = std::string(64, 'b');
    const auto assets = std::vector<Asset>{projectAsset(
      ProjectAssetId, linkFor(LibraryId, LibraryAssetId, captured), "WALL01")};
    auto state = LibraryState{LibraryId, "Library", "/library", true, {}, {}};
    state.assets.push_back(libraryAsset(LibraryAssetId, "WALL01", changed, 2));

    const auto updates =
      evaluateLibraryUpdates(assets, {state}, {{ProjectAssetId, {changed}}});
    REQUIRE(updates.size() == 1);
    CHECK(updates.front().state == LibraryUpdateState::NewVersion);
    CHECK(updates.front().ignored);
    CHECK_FALSE(updates.front().needsAttention());

    // Ignoring a different revision does not hide the current one.
    const auto other =
      evaluateLibraryUpdates(assets, {state}, {{ProjectAssetId, {std::string(64, 'c')}}});
    CHECK_FALSE(other.front().ignored);
    CHECK(other.front().needsAttention());
  }

  SECTION("ignores assets that were imported without a library origin")
  {
    auto asset = projectAsset(ProjectAssetId, {}, "WALL01");
    CHECK(evaluateLibraryUpdates({asset}, {}).empty());
  }

  SECTION("orders updates by display name")
  {
    const auto hash = std::string(64, 'a');
    const auto first = projectAsset(
      "44444444-4444-4444-4444-444444444444",
      linkFor(LibraryId, "55555555-5555-5555-5555-555555555555", hash),
      "ZULU");
    const auto second = projectAsset(
      "66666666-6666-6666-6666-666666666666",
      linkFor(LibraryId, "77777777-7777-7777-7777-777777777777", hash),
      "ALPHA");
    auto state = LibraryState{LibraryId, "Library", "/library", true, {}, {}};
    const auto updates = evaluateLibraryUpdates({first, second}, {state});
    REQUIRE(updates.size() == 2);
    CHECK(updates[0].displayName == "ALPHA");
    CHECK(updates[1].displayName == "ZULU");
  }

  SECTION("wraps native miptex content into a mountable WAD")
  {
    const auto miptex = test::makeMiptex("WALL01", 16, 16, 4);
    const auto asset = libraryAsset(LibraryAssetId, "WALL01", contentHash(miptex));
    CHECK_FALSE(libraryAssetNeedsConversion(asset));
    const auto compatible = compatibleLibraryContent(asset, miptex);
    REQUIRE(compatible.is_success());
    const auto archive = readGoldSrcWad(compatible.value());
    REQUIRE(archive.is_success());
    REQUIRE(archive.value().textures.size() == 1);
    CHECK(archive.value().textures.front().name == "WALL01");
    CHECK(archive.value().textures.front().miptex == miptex);

    auto image = libraryAsset(LibraryAssetId, "WALL01", contentHash(miptex), 1, "image");
    CHECK(libraryAssetNeedsConversion(image));
    const auto passthrough = compatibleLibraryContent(image, miptex);
    REQUIRE(passthrough.is_success());
    CHECK(passthrough.value() == miptex);

    auto converted =
      libraryAsset(LibraryAssetId, "WALL01", contentHash(miptex), 2, "goldsrc-wad");
    CHECK_FALSE(libraryAssetNeedsConversion(converted));
    const auto published = compatibleLibraryContent(converted, miptex);
    REQUIRE(published.is_success());
    CHECK(published.value() == miptex);
  }

  SECTION("treats a converted revision as a new library version")
  {
    const auto hash = std::string(64, 'a');
    auto captured = linkFor(LibraryId, LibraryAssetId, hash);
    captured.libraryRevision = 3;
    const auto assets =
      std::vector<Asset>{projectAsset(ProjectAssetId, captured, "WALL01")};
    auto state = LibraryState{LibraryId, "Library", "/library", true, {}, {}};
    // The source hash is unchanged; only the conversion advanced the revision.
    state.assets.push_back(
      libraryAsset(LibraryAssetId, "WALL01", hash, 4, "goldsrc-wad"));

    const auto updates = evaluateLibraryUpdates(assets, {state});
    REQUIRE(updates.size() == 1);
    CHECK(updates.front().state == LibraryUpdateState::NewVersion);
    CHECK(updates.front().available->revision == 4);
  }

  SECTION("records capture provenance in the conversion recipe")
  {
    const auto asset = libraryAsset(LibraryAssetId, "WALL01", std::string(64, 'a'), 3);
    const auto recipe = captureRecipe(asset);
    CHECK(recipe.starts_with('{'));
    CHECK(recipe.ends_with('}'));
    CHECK(recipe.find(R"("capturedFrom":"asset-library")") != std::string::npos);
    CHECK(recipe.find(LibraryAssetId) != std::string::npos);
    CHECK(recipe.find(R"("libraryRevision":3)") != std::string::npos);
    CHECK(recipe.find(R"("wad-member")") != std::string::npos);

    const auto converted = captureRecipe(asset, R"({"importer":"goldsrc-texture"})");
    CHECK(converted.find(R"("importer":"goldsrc-texture")") != std::string::npos);
    CHECK(converted.find(R"("capturedFrom":"asset-library")") != std::string::npos);
  }
}

} // namespace tb::project
