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
#include "fs/DkPakFileSystem.h"
#include "fs/File.h" // IWYU pragma: keep
#include "fs/TestEnvironment.h"
#include "fs/TestUtils.h"
#include "fs/TraversalMode.h"

#include "kd/result.h"

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>

#include <catch2/catch_test_macros.hpp>

namespace tb::fs
{

TEST_CASE("DkPakFileSystem")
{
  const auto fsTestPath = getFixtureRoot() / "test/fs/";

  SECTION("doReadDirectory")
  {
    SECTION("rejects malformed compressed entries")
    {
      auto env = TestEnvironment{};
      const auto checkInvalid = [&](
                                  const std::initializer_list<unsigned char> bytes,
                                  const unsigned int declaredSize) {
        auto archive = std::string{"PACK"};
        const auto appendInt = [&](const unsigned int value) {
          for (auto shift = 0u; shift < 32; shift += 8)
          {
            archive.push_back(static_cast<char>((value >> shift) & 0xff));
          }
        };
        appendInt(12u + static_cast<unsigned int>(bytes.size()));
        appendInt(72);
        for (const auto byte : bytes)
        {
          archive.push_back(static_cast<char>(byte));
        }
        archive.append("entry.bin");
        archive.resize(12 + bytes.size() + 56, '\0');
        appendInt(12);
        appendInt(declaredSize);
        appendInt(static_cast<unsigned int>(bytes.size()));
        appendInt(1);

        const auto path = env.dir() / "invalid.pak";
        {
          auto stream = std::ofstream{path, std::ios::binary};
          stream.write(archive.data(), static_cast<std::streamsize>(archive.size()));
        }
        const auto file = Disk::openFile(path) | kdl::value();
        const auto fs = createImageFileSystem<DkPakFileSystem>(file) | kdl::value();
        CHECK(fs->openFile("entry.bin").is_error());
      };

      checkInvalid({0x01, 'A', 'B', 0xff, 0}, 1);
      checkInvalid({0x41, 0xff, 0}, 1);
      checkInvalid({0x82, 'A', 0xff, 0}, 1);
      checkInvalid({0xc0, 0, 0xff, 0}, 2);
      checkInvalid({0x01, 'A', 'B', 0xc1, 0, 0xff, 0}, 5);
      checkInvalid({0xff, 0}, 1);
      checkInvalid({}, 1);
    }

    SECTION("rejects directory entries outside the archive")
    {
      auto env = TestEnvironment{};
      auto archive = std::string(84, '\0');
      archive.replace(0, 4, "PACK");
      archive[4] = 12;
      archive[8] = 72;
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
      CHECK(createImageFileSystem<DkPakFileSystem>(file).is_error());
    }

    SECTION("decompresses entries covering every RLE opcode and both loop exits")
    {
      // dkpak_rle.pak was generated to exercise DkPakFileSystem::decompress() beyond
      // what any legitimate compressed game asset does:
      // - "terminated.bin": opcode 0x01 (raw copy "AB"), 0x41 (a run of 3 zero bytes),
      //   0x82 'C' (a run of 4 'C' bytes), 0xFE (unmatched by any RLE case, must be a
      //   no-op), 0xC0 0x07 (a back-reference copying "AB" from the start of the
      //   output), then 0xFF followed by a padding byte, so the terminator is reached
      //   without also exhausting the reader
      // - "unterminated.bin": no terminator opcode at all; the read that consumes the
      //   entry's last byte lands exactly on eof(), so the loop must exit via
      //   `!reader.eof()` rather than by seeing `x == 0xFF`
      // - "plain.txt": an uncompressed entry, for a sanity check alongside the above
      // - "corrupt.bin": opcode 0x01 claims a 2 byte raw copy, but only 1 byte of
      //   entry data follows, so decompress() must turn the resulting ReaderException
      //   into an Error rather than letting it escape
      auto fs = openFS<DkPakFileSystem>(fsTestPath / "Pak/dkpak_rle.pak");

      // uses buffer().stringView() rather than readString(), since the decompressed
      // content contains embedded null bytes that readString would truncate at
      const auto terminated = fs->openFile("terminated.bin") | kdl::value();
      const auto terminatedContent = terminated->reader().buffer().stringView();
      CHECK(std::string{terminatedContent} == std::string{"AB\0\0\0CCCCAB", 11});

      const auto unterminated = fs->openFile("unterminated.bin") | kdl::value();
      auto unterminatedReader = unterminated->reader();
      CHECK(unterminatedReader.readString(unterminatedReader.size()) == "Z");

      const auto plain = fs->openFile("plain.txt") | kdl::value();
      auto plainReader = plain->reader();
      CHECK(plainReader.readString(plainReader.size()) == "hello");

      CHECK(fs->openFile("corrupt.bin").is_error());
    }

    SECTION("returns an error if the directory cannot be read")
    {
      // dkpak_truncated.pak claims a one entry directory starting right after the
      // header, but the file ends there, so reading the entry's name runs out of
      // bounds
      const auto file =
        Disk::openFile(fsTestPath / "Pak/dkpak_truncated.pak") | kdl::value();
      const auto result = createImageFileSystem<DkPakFileSystem>(file);
      CHECK(result.is_error());
    }

    SECTION("reads an entry whose name is not valid UTF-8")
    {
      // https://github.com/TrenchBroom/TrenchBroom/issues/5494
      const auto fs =
        openFS<DkPakFileSystem>(fsTestPath / "Pak/dkpak_non_utf8_entry_name.pak");

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
