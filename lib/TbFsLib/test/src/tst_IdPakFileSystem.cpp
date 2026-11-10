/*
 Copyright (C) 2026 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Matchers.h"
#include "TestEnvironment.h"
#include "fs/DiskIO.h"
#include "fs/File.h"
#include "fs/IdPakFileSystem.h"
#include "fs/TestEnvironment.h"
#include "fs/TestUtils.h"
#include "fs/TraversalMode.h"

#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

namespace tb::fs
{

TEST_CASE("IdPakFileSystem")
{
  const auto fsTestPath = getFixtureRoot() / "test/fs/Pak/";

  SECTION("doReadDirectory")
  {
    SECTION("rejects an entry outside the archive")
    {
      auto env = TestEnvironment{};
      auto archive = std::string(76, '\0');
      archive.replace(0, 4, "PACK");
      archive[4] = 12;
      archive[8] = 64;
      archive.replace(12, 9, "entry.bin");
      archive[68] = static_cast<char>(0xff);
      archive[69] = static_cast<char>(0xff);
      archive[70] = static_cast<char>(0xff);
      archive[71] = static_cast<char>(0xff);
      archive[72] = 1;
      const auto path = env.dir() / "invalid.pak";
      {
        auto stream = std::ofstream{path, std::ios::binary};
        stream.write(archive.data(), static_cast<std::streamsize>(archive.size()));
      }
      const auto file = Disk::openFile(path) | kdl::value();
      CHECK(createImageFileSystem<IdPakFileSystem>(file).is_error());
    }

    SECTION("reads an entry whose name is not valid UTF-8")
    {
      // https://github.com/TrenchBroom/TrenchBroom/issues/5494
      const auto fs =
        openFS<IdPakFileSystem>(fsTestPath / "idpak_non_utf8_entry_name.pak");

#ifdef _WIN32
      const auto expectedPath = std::filesystem::path{L"fen\u00EAtre.txt"};
#else
      const auto expectedPath = std::filesystem::path{"fen\xEAtre.txt"};
#endif
      CHECK_THAT(fs->find("", TraversalMode::Flat), MatchesPathsResult({expectedPath}));
    }
  }
}

} // namespace tb::fs
