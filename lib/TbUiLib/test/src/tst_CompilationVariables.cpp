/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom, licensed under GPL version 3 or later.
*/

#include "ui/CompilationVariables.h"

#include "el/Interpolate.h"
#include "el/VariableStore.h"
#include "mdl/MapFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace tb::ui
{
namespace
{
std::string expand(const el::VariableStore& variables, const std::string& text)
{
  const auto result = el::interpolate(variables, text);
  REQUIRE(result.is_success());
  return result.value();
}
} // namespace

TEST_CASE("CompilationVariables")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.create();

  SECTION("map variables describe the frozen map path")
  {
    const auto variables = CompilationVariables{map, "/work", "/maps/alpha.map"};
    CHECK(expand(variables, "${MAP_BASE_NAME}") == "alpha");
    CHECK(expand(variables, "${MAP_FULL_NAME}") == "alpha.map");
    CHECK(expand(variables, "${WORK_DIR_PATH}") == "/work");
  }

  SECTION("project variables name the project file and map")
  {
    auto variables = CompilationVariables{map, "/work", "/maps/alpha.map"};
    variables.setProject("/projects/game.tbproj", "alpha");

    CHECK(expand(variables, "${PROJECT_PATH}") == "/projects/game.tbproj");
    CHECK(expand(variables, "${MAP_NAME}") == "alpha");
    // a profile can mix both: the project is the input, the export is the work area
    CHECK(
      expand(variables, "compile --project ${PROJECT_PATH} --map ${MAP_NAME}")
      == "compile --project /projects/game.tbproj --map alpha");
  }
}

} // namespace tb::ui
