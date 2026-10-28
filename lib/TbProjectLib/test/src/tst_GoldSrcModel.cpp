/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom.
*/

#include "GoldSrcFixtures.h"
#include "project/GoldSrcModel.h"

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

} // namespace

TEST_CASE("GoldSrcModel")
{
  SECTION("reads skin dependencies from a studio model")
  {
    const auto model = require(readGoldSrcModelDependencies(
      test::makeModel({"crate.bmp", "models/props/metal.bmp"})));
    REQUIRE(model.skins.size() == 2);
    CHECK(model.skins[0] == "crate.bmp");
    CHECK(model.skins[1] == "models/props/metal.bmp");
  }

  SECTION("models without skins declare no dependencies")
  {
    const auto model = require(readGoldSrcModelDependencies(test::makeModel({})));
    CHECK(model.skins.empty());
  }

  SECTION("rejects payloads that are not studio models")
  {
    CHECK(readGoldSrcModelDependencies({}).is_error());
    CHECK(readGoldSrcModelDependencies(std::vector<std::uint8_t>(244, 0)).is_error());
    auto otherVersion = test::makeModel({"crate.bmp"});
    test::writeU32(otherVersion, 4, 9);
    CHECK(readGoldSrcModelDependencies(otherVersion).is_error());
    auto truncatedTable = test::makeModel({"crate.bmp"});
    truncatedTable.resize(244 + 10);
    CHECK(readGoldSrcModelDependencies(truncatedTable).is_error());
  }

  SECTION("normalizes dependency names to comparable stems")
  {
    CHECK(modelDependencyKey("crate.bmp") == "crate");
    CHECK(modelDependencyKey("models\\props\\CRATE.BMP") == "crate");
    CHECK(modelDependencyKey("models/props/Metal") == "metal");
    CHECK(modelDependencyKey("") == "");
    CHECK(modelDependencyKey("noextension") == "noextension");
  }

  SECTION("suggests a multiplayer companion name")
  {
    CHECK(modelCompanionName("player.mdl") == "playerT.mdl");
    CHECK(modelCompanionName("models/player/gignT.mdl") == "models/player/gign.mdl");
    CHECK(modelCompanionName("crate.mdl") == "crateT.mdl");
    CHECK(modelCompanionName("").empty());
  }
}

} // namespace tb::project
