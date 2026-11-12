/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QCheckBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QtTest/QSignalSpy>

#include "ui/CatchConfig.h"
#include "ui/HltoolsCompilerOptions.h"
#include "ui/HltoolsOptionsEditor.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
TEST_CASE("HltoolsOptionsEditor")
{
  auto editor = HltoolsOptionsEditor{};
  auto changes = QSignalSpy{&editor, &HltoolsOptionsEditor::parametersChanged};
  auto parameters = QString{};
  QObject::connect(
    &editor,
    &HltoolsOptionsEditor::parametersChanged,
    [&parameters](const QString& value) { parameters = value; });
  SECTION("every visible registry entry has a named control and availability")
  {
    for (const auto& option : hltoolsCompilerOptions())
    {
      if (!option.visible)
        continue;
      INFO(option.flag.toStdString());
      auto* checkbox =
        editor.findChild<QCheckBox*>("HltoolsOption_" + option.flag.mid(1));
      REQUIRE(checkbox);
      CHECK(checkbox->text() != option.flag);
      CHECK(checkbox->isEnabled() == option.unavailableReason.isEmpty());
      if (!option.unavailableReason.isEmpty())
        CHECK(checkbox->parentWidget()->toolTip().contains(option.unavailableReason));
    }
  }
  SECTION("multi-value edits remove old components and preserve unrelated arguments")
  {
    editor.setParameters(
      R"(-ambient -0.1 0.2 0.3 -gamma 0.7 -ambient 0.4 0.5 0.6 -lights "my lights.rad")");
    auto* ambient = editor.findChild<QCheckBox*>("HltoolsOption_ambient");
    auto* red = editor.findChild<QLineEdit*>("HltoolsOption_ambientValue0");
    REQUIRE(ambient);
    REQUIRE(red);
    CHECK(ambient->isChecked());
    CHECK(red->text() == "0.4");
    red->setText("${1 / 4}");
    QMetaObject::invokeMethod(red, "editingFinished", Qt::DirectConnection);
    CHECK(parameters.count("-ambient") == 1);
    CHECK(parameters.contains("-ambient ${1 / 4} 0.5 0.6"));
    CHECK(parameters.contains(R"(-lights "my lights.rad")"));
    CHECK(parameters.contains("-gamma 0.7"));
    ambient->click();
    CHECK_FALSE(parameters.contains("-ambient"));
    CHECK_FALSE(parameters.contains("0.5"));
    CHECK_FALSE(parameters.contains("0.6"));
  }
  SECTION("ambient occlusion and shadow options round trip through controls")
  {
    editor.setParameters("-ao -aocolor 128 32 16 -pcf 2");
    auto* ao = editor.findChild<QCheckBox*>("HltoolsOption_ao");
    auto* color = editor.findChild<QCheckBox*>("HltoolsOption_aocolor");
    auto* red = editor.findChild<QLineEdit*>("HltoolsOption_aocolorValue0");
    REQUIRE(ao);
    REQUIRE(color);
    REQUIRE(red);
    CHECK(ao->isChecked());
    CHECK(color->isChecked());
    CHECK(red->text() == "128");
    red->setText("64");
    QMetaObject::invokeMethod(red, "editingFinished", Qt::DirectConnection);
    CHECK(parameters.contains("-aocolor 64 32 16"));
    CHECK(parameters.contains("-pcf 2"));
  }
  SECTION("repeatable entries preserve spaces and separate WAD values")
  {
    editor.setParameters(
      R"(-waddir "C:/first folder" -waddir "D:/second folder" -wadinclude one.wad -wadinclude two.wad -gamma 0.6)");
    auto* paths = editor.findChild<QPlainTextEdit*>("HltoolsOption_waddirValues");
    auto* wads = editor.findChild<QPlainTextEdit*>("HltoolsOption_wadincludeValues");
    REQUIRE(paths);
    REQUIRE(wads);
    CHECK(paths->toPlainText() == "C:/first folder\nD:/second folder");
    CHECK(wads->toPlainText() == "one.wad\ntwo.wad");
    paths->setPlainText("E:/new folder\nF:/other folder");
    CHECK(parameters.count("-waddir") == 2);
    CHECK(parameters.contains(R"(-waddir "E:/new folder" -waddir "F:/other folder")"));
    CHECK(parameters.contains("-wadinclude one.wad -wadinclude two.wad"));
    CHECK(parameters.contains("-gamma 0.6"));
    editor.findChild<QCheckBox*>("HltoolsOption_waddir")->click();
    CHECK_FALSE(parameters.contains("-waddir"));
    CHECK_FALSE(parameters.contains("new folder"));
  }
  SECTION("enabling an empty repeatable option leaves its editor usable")
  {
    auto* checkbox = editor.findChild<QCheckBox*>("HltoolsOption_wadinclude");
    auto* wads = editor.findChild<QPlainTextEdit*>("HltoolsOption_wadincludeValues");
    REQUIRE(checkbox);
    REQUIRE(wads);
    checkbox->click();
    CHECK(checkbox->isChecked());
    CHECK(wads->isEnabled());
    wads->setPlainText("one.wad\ntwo.wad");
    CHECK(parameters.count("-wadinclude") == 2);
  }
  SECTION("file values named like flags do not activate unrelated switches")
  {
    editor.setParameters(R"(-lights "-noskyclip" -ambient 0 0 0)");
    CHECK_FALSE(editor.findChild<QCheckBox*>("HltoolsOption_noskyclip")->isChecked());
    auto* path = editor.findChild<QLineEdit*>("HltoolsOption_lightsValue");
    REQUIRE(path);
    CHECK(path->text() == "-noskyclip");
    path->setText("C:/my lights/custom.rad");
    QMetaObject::invokeMethod(path, "editingFinished", Qt::DirectConnection);
    CHECK(parameters.contains(R"(-lights "C:/my lights/custom.rad")"));
    CHECK_FALSE(parameters.contains("-noskyclip"));
  }
  SECTION("driver aliases synchronize without changing the saved expression")
  {
    editor.setParameters("--dump-intermediates -gamma 0.55");
    CHECK(changes.empty());
    auto* dump = editor.findChild<QCheckBox*>("HltoolsOption_dumpintermediates");
    REQUIRE(dump);
    CHECK(dump->isChecked());
    dump->click();
    CHECK(parameters == "-gamma 0.55");
  }
}
} // namespace tb::ui
