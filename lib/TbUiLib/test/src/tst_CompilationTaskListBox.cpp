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
#include <QtTest/QSignalSpy>

#include "mdl/CompilationProfile.h"
#include "mdl/CompilationTask.h"
#include "mdl/Entity.h"
#include "mdl/EntityProperties.h"
#include "ui/CatchConfig.h"
#include "ui/CollapsibleTitledPanel.h"
#include "ui/CompilationTaskListBox.h"
#include "ui/ControlListBox.h"
#include "ui/HltoolsOptionsEditor.h"
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
  CHECK(output->text() == "${MAP_BASE_NAME}.bsp");
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
  SECTION("visual options and the saved parameter expression stay synchronized")
  {
    auto* sky = editor.findChild<QCheckBox*>("HltoolsOption_noskyclip");
    auto* wad = editor.findChild<QCheckBox*>("HltoolsOption_wadtextures");
    auto* bounce = editor.findChild<QCheckBox*>("HltoolsOption_bounce");
    auto* bounceValue = editor.findChild<QComboBox*>("HltoolsOption_bounceValue");
    REQUIRE(sky);
    REQUIRE(wad);
    REQUIRE(bounce);
    REQUIRE(bounceValue);
    const auto original = QString{
      R"(-lights "C:\my lights\custom.rad" -threads ${CPU_COUNT} ${'literal -noskyclip'})"};
    parameters->setText(original);
    CHECK_FALSE(sky->isChecked());
    sky->click();
    CHECK(parameters->text() == original + " -noskyclip");
    CHECK(
      std::get<mdl::CompilationHltools>(task).parameterSpec
      == parameters->text().toStdString());
    sky->click();
    CHECK(parameters->text() == original);
    parameters->setText("-wadtextures -noskyclip -noskyclip -bounce ${CPU_COUNT}");
    CHECK(wad->isChecked());
    CHECK(sky->isChecked());
    CHECK(bounce->isChecked());
    CHECK(bounceValue->currentText() == "${CPU_COUNT}");
    sky->click();
    CHECK_FALSE(parameters->text().contains("-noskyclip"));
    CHECK(parameters->text().contains("-bounce ${CPU_COUNT}"));
    bounce->click();
    CHECK_FALSE(parameters->text().contains("-bounce"));
    CHECK_FALSE(parameters->text().contains("CPU_COUNT"));
    CHECK(parameters->text().contains("-wadtextures"));
  }
  SECTION("quoted flags and the opaque alias reflect effective options")
  {
    auto* sky = editor.findChild<QCheckBox*>("HltoolsOption_noskyclip");
    auto* opaque = editor.findChild<QCheckBox*>("HltoolsOption_nopaque");
    REQUIRE(sky);
    REQUIRE(opaque);
    parameters->setText("\"-noskyclip\" -noopaque");
    CHECK(sky->isChecked());
    CHECK(opaque->isChecked());
    sky->click();
    CHECK_FALSE(parameters->text().contains("noskyclip"));
    opaque->click();
    CHECK(parameters->text().isEmpty());
  }
  SECTION("value selectors preserve expressions and remove stale duplicate overrides")
  {
    auto* clip = editor.findChild<QCheckBox*>("HltoolsOption_cliptype");
    auto* clipValue = editor.findChild<QComboBox*>("HltoolsOption_cliptypeValue");
    auto* bounce = editor.findChild<QCheckBox*>("HltoolsOption_bounce");
    auto* bounceValue = editor.findChild<QComboBox*>("HltoolsOption_bounceValue");
    REQUIRE(clip);
    REQUIRE(clipValue);
    REQUIRE(bounce);
    REQUIRE(bounceValue);
    parameters->setText(
      "-bounce -1 -cliptype simple -cliptype precise -lights \"custom lights.rad\"");
    CHECK(clip->isChecked());
    CHECK(clipValue->currentText() == "precise");
    bounce->click();
    CHECK_FALSE(parameters->text().contains("-1"));
    clipValue->setCurrentIndex(clipValue->findText("normalized"));
    QMetaObject::invokeMethod(
      clipValue,
      "activated",
      Qt::DirectConnection,
      Q_ARG(int, clipValue->currentIndex()));
    CHECK(parameters->text().count("-cliptype") == 1);
    CHECK(parameters->text().contains("-cliptype normalized"));
    CHECK(parameters->text().contains("-lights \"custom lights.rad\""));
    bounce->click();
    bounceValue->setEditText("${CPU_COUNT}");
    QMetaObject::invokeMethod(
      bounceValue->lineEdit(), "editingFinished", Qt::DirectConnection);
    CHECK(parameters->text().contains("-bounce ${CPU_COUNT}"));
  }
  SECTION("option search and modified filter do not edit task parameters")
  {
    auto* panel = editor.findChild<CollapsibleTitledPanel*>("HltoolsOptionsPanel");
    auto* sky = editor.findChild<QCheckBox*>("HltoolsOption_noskyclip");
    auto* wad = editor.findChild<QCheckBox*>("HltoolsOption_wadtextures");
    auto* filter = editor.findChild<QLineEdit*>("HltoolsOptionFilter");
    auto* modified = editor.findChild<QCheckBox*>("HltoolsModifiedOnly");
    REQUIRE(panel);
    REQUIRE(sky);
    REQUIRE(wad);
    REQUIRE(filter);
    REQUIRE(modified);
    CHECK_FALSE(panel->expanded());
    parameters->setText("-noskyclip");
    modified->setChecked(true);
    CHECK_FALSE(sky->parentWidget()->isHidden());
    CHECK(wad->parentWidget()->isHidden());
    filter->setText("wad");
    CHECK(sky->parentWidget()->isHidden());
    CHECK(parameters->text() == "-noskyclip");
    modified->setChecked(false);
    CHECK_FALSE(wad->parentWidget()->isHidden());
    CHECK(std::get<mdl::CompilationHltools>(task).parameterSpec == "-noskyclip");
    filter->clear();
    auto* stage = editor.findChild<QComboBox*>("HltoolsOptionStage");
    REQUIRE(stage);
    stage->setCurrentIndex(stage->findData("RAD"));
    CHECK(sky->parentWidget()->isHidden());
    CHECK_FALSE(
      editor.findChild<QCheckBox*>("HltoolsOption_bounce")->parentWidget()->isHidden());
  }
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
