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

#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include "gl/PerspectiveCamera.h"
#include "mdl/CompilationProfile.h"
#include "mdl/CompilationTask.h"
#include "mdl/Map.h"
#include "ui/AppControllerFixture.h"
#include "ui/CatchConfig.h"
#include "ui/CompilationDialog.h"
#include "ui/CompilationProfileManager.h"
#include "ui/CompilationVariables.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"
#include "ui/TaskNotification.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

namespace
{

mdl::MapFixtureConfig fixtureConfigWithCompilationProfiles(
  std::vector<mdl::CompilationProfile> profiles)
{
  auto fixtureConfig = mdl::MapFixtureConfig{};
  fixtureConfig.gameInfo.compilationConfig.profiles = std::move(profiles);
  return fixtureConfig;
}

} // namespace

TEST_CASE("CompilationDialog")
{
  auto appControllerFixture = AppControllerFixture{};
  auto& appController = appControllerFixture.appController();


  SECTION("frozen input variables point at the exported map")
  {
    auto documentFixture = MapDocumentFixture{};
    auto& document = documentFixture.create();
    const auto snapshot = std::filesystem::path{"snapshot"} / "alpha.map";

    auto variables = CompilationVariables{document.map(), "work", snapshot};

    using namespace CompilationVariableNames;
    CHECK(variables.value(MAP_DIR_PATH).stringValue() == "snapshot");
    CHECK(variables.value(MAP_FULL_NAME).stringValue() == "alpha.map");
    CHECK(variables.value(MAP_BASE_NAME).stringValue() == "alpha");
    CHECK(variables.value(WORK_DIR_PATH).stringValue() == "work");
    // The live document path must never leak into the frozen input.
    CHECK(
      variables.value(MAP_DIR_PATH).stringValue()
      != document.map().path().parent_path().string());
  }

  SECTION("selectProfile")
  {
    auto profiles = std::vector<mdl::CompilationProfile>{
      mdl::CompilationProfile{"profile 1", "${MAP_DIR_PATH}", {}},
      mdl::CompilationProfile{"profile 2", "${MAP_DIR_PATH}", {}},
    };

    auto documentFixture = MapDocumentFixture{};
    auto& document =
      documentFixture.create(fixtureConfigWithCompilationProfiles(profiles));
    auto camera = gl::PerspectiveCamera{};

    auto dialog = CompilationDialog{appController, document, camera};

    auto* profileManager = dialog.findChild<CompilationProfileManager*>();
    auto* buttonBar = dialog.findChild<QWidget*>("DialogButtonBar");
    REQUIRE(profileManager != nullptr);
    REQUIRE(buttonBar != nullptr);

    SECTION("returns true and selects an existing profile")
    {
      const auto result = dialog.selectProfile(profiles[1]);

      CHECK(result);
      REQUIRE(profileManager->selectedProfile() != nullptr);
      CHECK(profileManager->selectedProfile()->name == "profile 2");
    }

    SECTION("returns false for an unknown profile")
    {
      const auto unknown = mdl::CompilationProfile{"unknown", "${MAP_DIR_PATH}", {}};

      const auto result = dialog.selectProfile(unknown);

      CHECK(!result);
      REQUIRE(profileManager->selectedProfile() != nullptr);
      CHECK(profileManager->selectedProfile()->name == "profile 1");
    }
  }

  SECTION("a hidden compilation continues and can be stopped from its notification")
  {
    const auto cmake = QStandardPaths::findExecutable("cmake");
    REQUIRE_FALSE(cmake.isEmpty());
    auto fixture = MapDocumentFixture{};
    auto& document = fixture.create(fixtureConfigWithCompilationProfiles(
      {{"background",
        "",
        {mdl::CompilationRunTool{true, cmake.toStdString(), "-E sleep 10", true}}}}));
    auto camera = gl::PerspectiveCamera{};
    auto host = QWidget{};
    host.resize(800, 600);
    host.show();
    auto center = TaskNotificationCenter{&host};
    auto dialog = CompilationDialog{appController, document, camera, &host, false};
    dialog.setNotificationCenter(center);
    dialog.show();
    dialog.runSelectedProfile();
    REQUIRE(dialog.isRunning());
    dialog.close();
    CHECK_FALSE(dialog.isVisible());
    CHECK(dialog.isRunning());
    auto* card =
      host.findChild<TaskNotificationCard*>("TaskNotificationCard_compilation");
    REQUIRE(card);
    CHECK_FALSE(card->isHidden());
    auto findButton = [&card](const QString& text) {
      for (auto* button : card->findChildren<QPushButton*>())
        if (button->text() == text && !button->isHidden())
          return button;
      return static_cast<QPushButton*>(nullptr);
    };
    auto* viewLog = findButton("View log");
    REQUIRE(viewLog);
    viewLog->click();
    CHECK(dialog.isVisible());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    dialog.close();
    card = host.findChild<TaskNotificationCard*>("TaskNotificationCard_compilation");
    REQUIRE(card);
    auto* stop = findButton("Stop");
    REQUIRE(stop);
    stop->click();
    REQUIRE(QTest::qWaitFor([&] { return !dialog.isRunning(); }, 5000));
  }

  SECTION("selectFirstProfile")
  {
    auto documentFixture = MapDocumentFixture{};
    auto& document = documentFixture.create(fixtureConfigWithCompilationProfiles({
      {"profile 1", "${MAP_DIR_PATH}", {}},
      {"profile 2", "${MAP_DIR_PATH}", {}},
      {"profile 3", "${MAP_DIR_PATH}", {}},
    }));
    auto camera = gl::PerspectiveCamera{};

    auto dialog = CompilationDialog{appController, document, camera};

    auto* profileManager = dialog.findChild<CompilationProfileManager*>();
    REQUIRE(profileManager != nullptr);

    const auto& profiles = document.map().gameInfo().compilationConfig.profiles;
    REQUIRE(dialog.selectProfile(profiles[2]));
    REQUIRE(profileManager->selectedProfile()->name == profiles[2].name);

    dialog.selectFirstProfile();

    REQUIRE(profileManager->selectedProfile()->name == profiles[0].name);
  }

  SECTION("inline variable diagnostics locate the input and recover when it is fixed")
  {
    auto fixture = MapDocumentFixture{};
    auto& document = fixture.create(fixtureConfigWithCompilationProfiles(
      {{"broken",
        "",
        {mdl::CompilationRunTool{true, "compiler", "${MISSING_ARGUMENT}", true}}}}));
    const auto camera = gl::PerspectiveCamera{};
    auto dialog = CompilationDialog{appController, document, camera, nullptr, false};
    auto* compile = dialog.findChild<QPushButton*>("CompilationCompile");
    auto* diagnostic = dialog.findChild<QLabel*>("CompilationTaskDiagnostic");
    REQUIRE(compile);
    REQUIRE(diagnostic);
    CHECK_FALSE(dialog.findChild<QLabel*>("CompilationTarget"));
    CHECK_FALSE(dialog.findChild<QLabel*>("CompilationPreflight"));
    CHECK_FALSE(compile->isEnabled());
    CHECK(diagnostic->toolTip().contains("MISSING_ARGUMENT"));
    CHECK_FALSE(diagnostic->text().contains("Undefined"));
    auto* locate = dialog.findChild<QPushButton*>("CompilationLocateSetting");
    REQUIRE(locate);
    CHECK_FALSE(locate->isHidden());
    locate->click();
    auto* field = qobject_cast<QLineEdit*>(dialog.focusWidget());
    REQUIRE(field);
    CHECK(field->text() == "${MISSING_ARGUMENT}");
    CHECK(diagnostic->parentWidget() == field->parentWidget());
    field->setText("-threads 2");
    QApplication::processEvents();
    CHECK(compile->isEnabled());
    CHECK(diagnostic->isHidden());
    CHECK(locate->isHidden());

    auto* work = dialog.findChild<QLineEdit*>("CompilationWorkDirectory");
    REQUIRE(work);
    work->setText("${MISSING_DIRECTORY}");
    QApplication::processEvents();
    auto* workError = dialog.findChild<QLabel*>("CompilationProfileDiagnostic");
    REQUIRE(workError);
    CHECK_FALSE(workError->isHidden());
    CHECK(diagnostic->isHidden());
    CHECK_FALSE(compile->isEnabled());
    dialog.runSelectedProfile();
    CHECK(dialog.focusWidget() == work);
    work->clear();
    QApplication::processEvents();
    CHECK(workError->isHidden());
    CHECK(compile->isEnabled());
  }

  SECTION("finished simulation keeps its identity when another profile is selected")
  {
    const auto profiles = std::vector<mdl::CompilationProfile>{
      {"first", "", {mdl::CompilationRunTool{true, "compiler", "", true}}},
      {"second", "", {mdl::CompilationRunTool{true, "compiler", "", true}}}};
    auto fixture = MapDocumentFixture{};
    auto& document = fixture.create(fixtureConfigWithCompilationProfiles(profiles));
    const auto camera = gl::PerspectiveCamera{};
    auto dialog = CompilationDialog{appController, document, camera};
    auto* simulate = dialog.findChild<QPushButton*>("CompilationSimulate");
    REQUIRE(simulate);
    simulate->click();
    CHECK_FALSE(dialog.isRunning());
    auto* identity = dialog.findChild<QLabel*>("CompilationLogIdentity");
    auto* status = dialog.findChild<QLabel*>("CompilationRunStatus");
    REQUIRE(identity);
    REQUIRE(status);
    const auto completedStatus = status->text();
    CHECK(identity->text().contains("first"));
    CHECK(completedStatus.contains("Simulation completed"));
    REQUIRE(dialog.selectProfile(profiles[1]));
    CHECK(identity->text().contains("first"));
    CHECK(status->text() == completedStatus);
    CHECK(dialog.outputText().contains("compiler"));
  }

  SECTION("runSelectedProfile")
  {
    SECTION("does nothing when no profile is selected")
    {
      auto documentFixture = MapDocumentFixture{};
      auto& document = documentFixture.create();
      auto camera = gl::PerspectiveCamera{};

      auto dialog = CompilationDialog{appController, document, camera};
      auto spy = QSignalSpy{&dialog, &CompilationDialog::compilationProfileStarted};

      dialog.runSelectedProfile();
      QApplication::processEvents();

      CHECK(spy.count() == 0);
    }

    SECTION("starts the selected profile")
    {
      auto documentFixture = MapDocumentFixture{};
      auto& document = documentFixture.create(fixtureConfigWithCompilationProfiles({
        mdl::CompilationProfile{
          "runnable profile",
          "${MAP_DIR_PATH}",
          {mdl::CompilationRunTool{true, "/does/not/exist", "", true}},
        },
      }));
      auto camera = gl::PerspectiveCamera{};

      auto dialog = CompilationDialog{appController, document, camera};
      auto spy = QSignalSpy{&dialog, &CompilationDialog::compilationProfileStarted};

      dialog.runSelectedProfile();
      QApplication::processEvents();

      CHECK(spy.count() == 1);
    }
  }
}

} // namespace tb::ui
