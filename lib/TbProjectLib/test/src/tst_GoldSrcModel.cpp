/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom.
*/

#include "GoldSrcFixtures.h"
#include "project/GoldSrcModel.h"

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

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
using namespace Catch::Matchers;

TEST_CASE("GoldSrcModel")
{
  SECTION("reads the external animation group count")
  {
    CHECK(require(readGoldSrcModelDependencies(test::makeModel(1))).sequenceGroups == 1);
    CHECK(require(readGoldSrcModelDependencies(test::makeModel(4))).sequenceGroups == 4);
    // A degenerate count still means "only the base model".
    CHECK(require(readGoldSrcModelDependencies(test::makeModel(0))).sequenceGroups == 1);
  }

  SECTION("derives animation group names the way the engine does")
  {
    CHECK(
      modelAnimationGroupNames("models/player/gign.mdl", 1)
      == std::vector<std::string>{});
    CHECK_THAT(
      modelAnimationGroupNames("models/player/gign.mdl", 3),
      Equals(std::vector<std::string>{
        "models/player/gign01.mdl", "models/player/gign02.mdl"}));
    // Backslash paths from a map or entity value are normalized.
    CHECK_THAT(
      modelAnimationGroupNames("models\\props\\crate.mdl", 2),
      Equals(std::vector<std::string>{"models/props/crate01.mdl"}));
    CHECK(modelAnimationGroupNames("", 3).empty());
  }

  SECTION("rejects payloads that are not studio models")
  {
    CHECK(readGoldSrcModelDependencies({}).is_error());
    CHECK(readGoldSrcModelDependencies(std::vector<std::uint8_t>(244, 0)).is_error());
    auto otherVersion = test::makeModel(2);
    test::writeU32(otherVersion, 4, 9);
    CHECK(readGoldSrcModelDependencies(otherVersion).is_error());
    auto implausible = test::makeModel(1);
    test::writeU32(implausible, 172, 100000);
    CHECK(readGoldSrcModelDependencies(implausible).is_error());
  }

  SECTION("normalizes dependency names to comparable stems")
  {
    CHECK(modelDependencyKey("crate.bmp") == "crate");
    CHECK(modelDependencyKey("models\\props\\CRATE.BMP") == "crate");
    CHECK(modelDependencyKey("models/props/Metal") == "metal");
    CHECK(modelDependencyKey("") == "");
    CHECK(modelDependencyKey("noextension") == "noextension");
  }

  SECTION("suggests a separate texture file name")
  {
    CHECK(modelCompanionName("player.mdl") == "playerT.mdl");
    CHECK(modelCompanionName("crate.mdl") == "crateT.mdl");
    // The relation is one-directional: a separate texture file never points back at its
    // base model, otherwise every capture would form a dependency cycle.
    CHECK(modelCompanionName("models/player/gignT.mdl").empty());
    CHECK(modelCompanionName("").empty());
  }
}

} // namespace tb::project
