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

#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>

#include "mdl/CompilationProfile.h"
#include "mdl/CompilationTask.h"
#include "mdl/Entity.h"
#include "mdl/EntityProperties.h"
#include "ui/CatchConfig.h"
#include "ui/CompilationTaskListBox.h"
#include "ui/ControlListBox.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{

QLineEdit* findLineEditByPlaceholder(const QWidget& widget, const QString& placeholder)
{
  for (auto* lineEdit : widget.findChildren<QLineEdit*>())
  {
    if (lineEdit->placeholderText() == placeholder)
    {
      return lineEdit;
    }
  }
  return nullptr;
}

} // namespace

TEST_CASE("CompilationHltoolsTaskEditor")
{
  auto fixture = MapDocumentFixture{};
  auto& document = fixture.create();
  auto profile = mdl::CompilationProfile{"Built-in", "", {}};
  auto task = mdl::CompilationTask{mdl::CompilationHltools{}};
  auto editor = CompilationHltoolsTaskEditor{document, profile, task};
  editor.updateItem();
  auto* output = editor.findChild<QLineEdit*>("HltoolsOutput");
  auto* quality = editor.findChild<QComboBox*>("HltoolsQuality");
  auto* gpu = editor.findChild<QCheckBox*>("HltoolsGpu");
  auto* parameters = editor.findChild<QLineEdit*>("HltoolsParameters");
  REQUIRE(output);
  REQUIRE(quality);
  REQUIRE(gpu);
  REQUIRE(parameters);
  CHECK(quality->currentData().toString() == "normal");
  CHECK(gpu->isChecked());
  output->setText("custom.bsp");
  quality->setCurrentIndex(quality->findData("extra"));
  gpu->setChecked(false);
  parameters->setText("-bounce 8");
  CHECK(
    std::get<mdl::CompilationHltools>(task)
    == mdl::CompilationHltools{true, "custom.bsp", "extra", false, "-bounce 8"});
  std::get<mdl::CompilationHltools>(task) = mdl::CompilationHltools{};
  editor.updateItem();
  CHECK(quality->currentData().toString() == "normal");
  CHECK(gpu->isChecked());
  CHECK(parameters->text().isEmpty());
}

TEST_CASE("CompilationExportMapTaskEditor")
{
  auto documentFixture = MapDocumentFixture{};
  auto& document = documentFixture.create();

  auto profile = mdl::CompilationProfile{"profile", "", {}};

  SECTION("updateItem")
  {
    SECTION("syncs the drop entity classname from the task")
    {
      auto task = mdl::CompilationTask{mdl::CompilationExportMap{
        true,
        false,
        std::nullopt,
        mdl::Entity{{{mdl::EntityPropertyKeys::Classname, "info_player_start"}}},
        "target",
      }};

      auto editor = CompilationExportMapTaskEditor{document, profile, task};
      editor.updateItem();

      auto* dropEntity = findLineEditByPlaceholder(editor, "enter classname");
      REQUIRE(dropEntity != nullptr);
      CHECK(dropEntity->text() == "info_player_start");
    }

    SECTION("clears the drop entity classname when the task has none")
    {
      auto task = mdl::CompilationTask{mdl::CompilationExportMap{
        true,
        false,
        std::nullopt,
        std::nullopt,
        "target",
      }};

      auto editor = CompilationExportMapTaskEditor{document, profile, task};
      editor.updateItem();

      auto* dropEntity = findLineEditByPlaceholder(editor, "enter classname");
      REQUIRE(dropEntity != nullptr);
      CHECK(dropEntity->text().isEmpty());
    }
  }
}

} // namespace tb::ui
