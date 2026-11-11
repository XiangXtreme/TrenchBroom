/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QTemporaryDir>
#include <QTextEdit>

#include "gl/PerspectiveCamera.h"
#include "mdl/CompilationProfile.h"
#include "mdl/MapFixture.h"
#include "ui/CompilationPlan.h"
#include "ui/CompilationRun.h"
#include "ui/CompilationVariables.h"
#include "ui/QPathUtils.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
TEST_CASE("CompilationPlan")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.create();
  auto directory = QTemporaryDir{};
  REQUIRE(directory.isValid());
  const auto input = pathFromQString(directory.filePath("room.map"));
  auto profile = mdl::CompilationProfile{
    "compile", directory.path().toStdString(), {mdl::CompilationHltools{}}};

  SECTION("resolves outputs without creating artifacts")
  {
    const auto plan = buildCompilationPlan(profile, map, input);
    REQUIRE(!plan.diagnostic);
    REQUIRE(plan.outputs.size() == 1);
    CHECK(plan.outputs.front() == input.parent_path() / "compiled/room.bsp");
    CHECK(plan.enabledTaskCount == 1);
    CHECK_FALSE(std::filesystem::exists(plan.outputs.front()));
  }
  SECTION("identifies the task and field containing an unknown variable")
  {
    profile.tasks.push_back(
      mdl::CompilationRunTool{true, "compiler", "${UNKNOWN_TOOL_ARGUMENT}", true});
    const auto plan = buildCompilationPlan(profile, map, input);
    REQUIRE(plan.diagnostic);
    CHECK(plan.diagnostic->taskIndex == 1);
    CHECK(plan.diagnostic->field == CompilationField::Parameters);
    CHECK(plan.diagnostic->message.contains("UNKNOWN_TOOL_ARGUMENT"));
  }
  SECTION("disabled tasks do not participate in validation")
  {
    profile.tasks.insert(
      profile.tasks.begin(), mdl::CompilationRunTool{false, "${UNKNOWN}", "", true});
    const auto plan = buildCompilationPlan(profile, map, input);
    CHECK(!plan.diagnostic);
    CHECK(plan.enabledTaskCount == 1);
  }
  SECTION("rejects a profile with no enabled tasks")
  {
    std::get<mdl::CompilationHltools>(profile.tasks.front()).enabled = false;
    CHECK(buildCompilationPlan(profile, map, input).diagnostic.has_value());
  }
  SECTION("diagnoses working directory expressions and missing folders")
  {
    for (const auto& work :
         {std::string{"${UNKNOWN}"},
          std::string{"${MAP_DIR_PATH"},
          directory.filePath("missing").toStdString()})
    {
      profile.workDirSpec = work;
      const auto plan = buildCompilationPlan(profile, map, input);
      REQUIRE(plan.diagnostic);
      CHECK(plan.diagnostic->field == CompilationField::WorkDirectory);
      CHECK(!plan.diagnostic->taskIndex);
    }
  }
  SECTION("later tasks can use the built-in output")
  {
    profile.tasks.push_back(
      mdl::CompilationCopyFiles{true, "${BSP_PATH}", "game/maps/room.bsp"});
    CHECK(!buildCompilationPlan(profile, map, input).diagnostic);
    profile.tasks.erase(profile.tasks.begin());
    CHECK(buildCompilationPlan(profile, map, input).diagnostic.has_value());
  }
  SECTION("project outputs remain persistent while input variables describe the snapshot")
  {
    const auto project =
      ProjectCompileInfo{pathFromQString(directory.filePath("game.tbproj")), "room"};
    const auto frozen = pathFromQString(directory.filePath("snapshot/room.map"));
    profile.workDirSpec = "${OUTPUT_DIR_PATH}/..";
    const auto plan = buildCompilationPlan(profile, map, frozen, project, false);
    REQUIRE(!plan.diagnostic);
    REQUIRE(plan.outputs.size() == 1);
    CHECK(plan.outputs.front() == input.parent_path() / "compiled/room.bsp");
    CHECK(
      plan.variables.value(CompilationVariableNames::MAP_DIR_PATH).stringValue()
      == frozen.parent_path().string());
    CHECK(
      plan.variables.value(CompilationVariableNames::PROJECT_PATH).stringValue()
      == project.projectPath.string());
  }
  SECTION("validation aborts before an earlier export can change the filesystem")
  {
    const auto exported = directory.filePath("must-not-exist.map");
    profile.tasks = {
      mdl::CompilationExportMap{
        true, false, std::nullopt, std::nullopt, exported.toStdString()},
      mdl::CompilationRunTool{true, "${UNDEFINED_COMPILER}", "", true}};
    auto run = CompilationRun{};
    auto output = QTextEdit{};
    const auto camera = gl::PerspectiveCamera{};
    CHECK(run.run(profile, map, camera, &output).is_error());
    CHECK_FALSE(run.running());
    CHECK_FALSE(std::filesystem::exists(pathFromQString(exported)));
    REQUIRE(run.diagnostic());
    CHECK(run.diagnostic()->taskIndex == 1);
  }
}

} // namespace tb::ui
