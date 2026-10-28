/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom.
*/

#include "GoldSrcFixtures.h"
#include "project/GoldSrcWad.h"

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace tb::project
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

std::vector<std::uint8_t> withDirectoryEntry(
  const std::vector<std::uint8_t>& wad, const std::uint8_t lumpType)
{
  auto copy = wad;
  // The first directory entry starts after the header and the first lump, which the
  // reader itself locates; rewriting the type byte tests that non-texture lumps are
  // skipped instead of reported as damaged textures.
  const auto directory = std::uint32_t{copy[8]} | (std::uint32_t{copy[9]} << 8)
                         | (std::uint32_t{copy[10]} << 16)
                         | (std::uint32_t{copy[11]} << 24);
  copy[directory + 12] = lumpType;
  return copy;
}

} // namespace

TEST_CASE("GoldSrcWad")
{
  SECTION("round trips miptex entries without changing their bytes")
  {
    const auto wall = test::makeMiptex("WALL01", 32, 16, 3);
    const auto floor = test::makeMiptex("FLOOR1", 16, 32, 9);
    const auto bytes = require(buildGoldSrcWad({wall, floor}));
    REQUIRE(isGoldSrcWad(bytes));

    const auto archive = require(readGoldSrcWad(bytes));
    CHECK(archive.signature == "WAD3");
    REQUIRE(archive.textures.size() == 2);
    CHECK(archive.textures[0].name == "WALL01");
    CHECK(archive.textures[0].width == 32);
    CHECK(archive.textures[0].height == 16);
    CHECK(archive.textures[0].valid);
    CHECK(archive.textures[0].miptex == wall);
    CHECK(archive.textures[1].name == "FLOOR1");
    CHECK(archive.textures[1].valid);
    CHECK(archive.textures[1].miptex == floor);
    CHECK(goldSrcMiptexName(wall) == "WALL01");
  }

  SECTION("rebuilds byte identical archives for identical input")
  {
    const auto wall = test::makeMiptex("WALL01", 16, 16, 1);
    CHECK(require(buildGoldSrcWad({wall})) == require(buildGoldSrcWad({wall})));
  }

  SECTION("skips non-texture lumps")
  {
    const auto wall = test::makeMiptex("WALL01", 16, 16, 1);
    const auto palette = withDirectoryEntry(require(buildGoldSrcWad({wall})), 0x40);
    const auto archive = require(readGoldSrcWad(palette));
    CHECK(archive.textures.empty());
  }

  SECTION("reports damaged lumps without failing the archive")
  {
    // A directory entry whose lump is too short to hold a miptex header keeps its bytes
    // and is reported as invalid instead of aborting the whole archive.
    const auto truncated = std::vector<std::uint8_t>(20, 0x7f);
    const auto wad = require(buildGoldSrcWad({truncated}));
    const auto archive = require(readGoldSrcWad(wad));
    REQUIRE(archive.textures.size() == 1);
    CHECK_FALSE(archive.textures[0].valid);
    CHECK(archive.textures[0].problem == "miptex header is truncated");
    CHECK(archive.textures[0].miptex == truncated);
  }

  SECTION("rejects malformed containers")
  {
    CHECK(readGoldSrcWad({}).is_error());
    CHECK(readGoldSrcWad({'W', 'A', 'D', '3'}).is_error());
    CHECK(readGoldSrcWad({'N', 'O', 'P', 'E', 0, 0, 0, 0, 12, 0, 0, 0}).is_error());
    const auto outOfRange =
      std::vector<std::uint8_t>{'W', 'A', 'D', '3', 1, 0, 0, 0, 200, 0, 0, 0};
    CHECK(readGoldSrcWad(outOfRange).is_error());
    CHECK_FALSE(isGoldSrcWad({'W', 'A', 'D'}));
  }

  SECTION("builds empty archives and refuses unknown miptex names")
  {
    const auto empty = require(buildGoldSrcWad({}));
    const auto archive = require(readGoldSrcWad(empty));
    CHECK(archive.textures.empty());
    CHECK(goldSrcMiptexName({1, 2, 3}).empty());
  }
}

} // namespace tb::project
