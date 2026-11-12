/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/HltoolsCompilerOptions.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
TEST_CASE("HltoolsCompilerOptions")
{
  SECTION("validates vectors, repeated inputs and negative numbers using registry arity")
  {
    CHECK(validateHltoolsArguments({"-BOUNCE", "2", "-NOOPAQUE"}).isEmpty());
    CHECK(validateHltoolsArguments({"-ambient",
                                    "0.1",
                                    "0.2",
                                    "0.3",
                                    "-bounce",
                                    "-1",
                                    "-waddir",
                                    "first folder",
                                    "-waddir",
                                    "second folder",
                                    "-noopaque"})
            .isEmpty());
    CHECK_FALSE(
      validateHltoolsArguments({"-ambient", "0.1", "0.2", "-gamma", "0.6"}).isEmpty());
    CHECK_FALSE(validateHltoolsArguments({"-drawsample", "0", "0", "0"}).isEmpty());
    CHECK_FALSE(validateHltoolsArguments({"-gamma"}).isEmpty());
    CHECK_FALSE(validateHltoolsArguments({"-not-a-compiler-option"}).isEmpty());
  }
  SECTION("unavailable operations are rejected consistently including aliases")
  {
    for (const auto& arguments :
         {QStringList{"-onlyents"},
          {"-maxdistance", "100"},
          {"-embedsource"},
          {"--embed-source"},
          {"--project", "game.tbproj"}})
      CHECK_FALSE(validateHltoolsArguments(arguments).isEmpty());
  }
  SECTION("SDHLT lighting options have the correct arity")
  {
    CHECK(validateHltoolsArguments({"-ao",
                                    "-aoscale",
                                    "64",
                                    "-aocolor",
                                    "128",
                                    "32",
                                    "16",
                                    "-aomode",
                                    "normal",
                                    "-aostudio",
                                    "fast",
                                    "-pcf",
                                    "2",
                                    "-blurclamp",
                                    "0.75",
                                    "-lightcull"})
            .isEmpty());
    CHECK_FALSE(validateHltoolsArguments({"-aocolor", "128", "32", "-ao"}).isEmpty());
    CHECK_FALSE(validateHltoolsArguments({"-pcf"}).isEmpty());
  }
}
} // namespace tb::ui
