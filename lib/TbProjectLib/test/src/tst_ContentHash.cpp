/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom.
*/

#include "project/ContentHash.h"

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace tb::project
{

TEST_CASE("ContentHash")
{
  SECTION("hashes empty and known payloads with SHA-256")
  {
    CHECK(
      contentHash(std::vector<std::uint8_t>{})
      == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(
      contentHash(std::string_view{"abc"})
      == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    // Every digest is 64 lowercase hex characters, including payloads spanning many
    // SHA-256 blocks.
    const auto longInput = std::string(1000, 'a');
    const auto digest = contentHash(std::string_view{longInput});
    CHECK(digest.size() == 64);
    CHECK(digest.find_first_not_of("0123456789abcdef") == std::string::npos);
    CHECK(digest != contentHash(std::string_view{std::string(999, 'a')}));
  }

  SECTION("separates single changed bytes from unchanged payloads")
  {
    const auto before = std::vector<std::uint8_t>{1, 2, 3, 4};
    const auto after = std::vector<std::uint8_t>{1, 2, 3, 5};
    CHECK(contentHash(before) == contentHash(std::vector<std::uint8_t>{1, 2, 3, 4}));
    CHECK(contentHash(before) != contentHash(after));
  }

  SECTION("derives stable ID shaped digests from natural keys")
  {
    const auto first = stableId("tbasset|library|wads/custom.wad|WALL01");
    const auto second = stableId("tbasset|library|wads/custom.wad|WALL01");
    const auto other = stableId("tbasset|library|wads/custom.wad|WALL02");
    CHECK(first == second);
    CHECK(first != other);
    CHECK(first.size() == 36);
    CHECK(first[8] == '-');
    CHECK(first[13] == '-');
    CHECK(first[18] == '-');
    CHECK(first[23] == '-');
  }
}

} // namespace tb::project
