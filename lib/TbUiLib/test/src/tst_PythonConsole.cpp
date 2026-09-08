/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include <QAbstractItemView>
#include <QCompleter>
#include <QFontDatabase>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QTextEdit>
#include <QToolButton>
#include <QtTest/QTest>

#include "base/PreferenceManager.h"
#include "prefs/Preferences.h"
#include "ui/PythonConsole.h"

#include <algorithm>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("PythonConsole")
{
  auto console = PythonConsole{};
  auto* actions = console.createTabBarPage(&console);
  auto* input = console.findChild<QPlainTextEdit*>("PythonConsole_Input");
  auto* prompt = console.findChild<QLabel*>("PythonConsole_Prompt");
  auto* runButton = console.findChild<QToolButton*>("PythonConsole_Run");
  auto* clearButton = console.findChild<QToolButton*>("PythonConsole_Clear");
  auto* output = console.findChild<QTextEdit*>("PythonConsole_Output");

  REQUIRE(actions != nullptr);
  REQUIRE(input != nullptr);
  REQUIRE(prompt != nullptr);
  REQUIRE(runButton != nullptr);
  REQUIRE(clearButton != nullptr);
  REQUIRE(output != nullptr);
  CHECK(actions->objectName() == QStringLiteral("PythonConsole_TabActions"));
  CHECK(runButton->parentWidget() == actions);
  CHECK(clearButton->parentWidget() == actions);
  CHECK(input->isReadOnly() == false);
  const auto previousFontFamily = pref(Preferences::PythonConsoleFontFamily);
  const auto previousFontSize = pref(Preferences::PythonConsoleFontSize);
  const auto expectedFontSize = std::clamp(
    previousFontSize,
    Preferences::MinPythonConsoleFontSize,
    Preferences::MaxPythonConsoleFontSize);
  CHECK(input->font().pointSize() == expectedFontSize);
  CHECK(prompt->font() == input->font());
  CHECK(output->font() == input->font());
  CHECK(runButton->isEnabled() == false);

  auto* splitter = console.findChild<QSplitter*>("PythonConsole_Splitter");
  REQUIRE(splitter != nullptr);
  CHECK(splitter->count() == 2);

  auto commands = std::vector<std::string>{};
  console.setCommandExecutor(
    [&](const std::string& source) { commands.push_back(source); });
  CHECK_FALSE(runButton->isEnabled());

  input->setPlainText(QStringLiteral("value = 41\nvalue + 1"));
  CHECK(runButton->isEnabled());
  runButton->click();
  REQUIRE(commands.size() == 1u);
  CHECK(commands.back() == "value = 41\nvalue + 1");
  CHECK(input->toPlainText().isEmpty());
  CHECK_FALSE(runButton->isEnabled());

  QTRY_VERIFY_WITH_TIMEOUT(
    output->toPlainText().contains(QStringLiteral(">>> value = 41")), 500);
  CHECK(output->toPlainText().contains(QStringLiteral("... value + 1")));

  clearButton->click();
  CHECK(output->toPlainText().isEmpty());

  input->setPlainText(QStringLiteral("draft"));
  QTest::keyClick(input, Qt::Key_Up);
  CHECK(input->toPlainText() == QStringLiteral("value = 41\nvalue + 1"));
  QTest::keyClick(input, Qt::Key_Down);
  CHECK(input->toPlainText() == QStringLiteral("draft"));

  input->setPlainText(QStringLiteral("first line"));
  auto lineCursor = input->textCursor();
  lineCursor.movePosition(QTextCursor::End);
  input->setTextCursor(lineCursor);
  QTest::keyClick(input, Qt::Key_Return, Qt::ShiftModifier);
  CHECK(input->toPlainText() == QStringLiteral("first line\n"));

  input->setPlainText(QStringLiteral("2 + 2"));
  QTest::keyClick(input, Qt::Key_Return);
  REQUIRE(commands.size() == 2u);
  CHECK(commands.back() == "2 + 2");

  const auto newFontSize = expectedFontSize == 16 ? 17 : 16;
  setPref(Preferences::PythonConsoleFontSize, newFontSize);
  CHECK(input->font().pointSize() == newFontSize);
  CHECK(prompt->font() == input->font());
  CHECK(output->font() == input->font());
  CHECK(output->document()->defaultFont() == input->font());
  setPref(Preferences::PythonConsoleFontSize, previousFontSize);

  auto availableFontFamily = QString{};
  for (const auto& family : QFontDatabase::families())
  {
    if (
      QFontDatabase::isFixedPitch(family)
      && family.compare(QString::fromStdString(previousFontFamily), Qt::CaseInsensitive)
           != 0)
    {
      availableFontFamily = family;
      break;
    }
  }
  REQUIRE_FALSE(availableFontFamily.isEmpty());
  setPref(Preferences::PythonConsoleFontFamily, availableFontFamily.toStdString());
  CHECK(input->font().family().compare(availableFontFamily, Qt::CaseInsensitive) == 0);
  CHECK(prompt->font() == input->font());
  CHECK(output->font() == input->font());
  CHECK(output->document()->defaultFont() == input->font());
  setPref(Preferences::PythonConsoleFontFamily, previousFontFamily);

  REQUIRE(console.completer() != nullptr);

  const auto completionLabels = [&]() {
    auto result = std::vector<QString>{};
    auto* model = console.completer()->completionModel();
    for (auto row = 0; row < model->rowCount(); ++row)
    {
      result.push_back(model->index(row, 0).data(Qt::DisplayRole).toString());
    }
    return result;
  };

  // Test dot completion context extraction and insertion
  input->setPlainText(QStringLiteral("doc.sele"));
  auto cursor = input->textCursor();
  cursor.movePosition(QTextCursor::End);
  input->setTextCursor(cursor);
  const auto [base1, prefix1] = console.completionContextUnderCursor();
  CHECK(base1 == QStringLiteral("doc"));
  CHECK(prefix1 == QStringLiteral("sele"));
  console.updateCompleter(true);
  console.insertCompletion(QStringLiteral("selection"));
  CHECK(input->toPlainText() == QStringLiteral("doc.selection"));

  // Test global shortcut context extraction and insertion
  input->setPlainText(QStringLiteral("sel.tra"));
  cursor = input->textCursor();
  cursor.movePosition(QTextCursor::End);
  input->setTextCursor(cursor);
  const auto [base2, prefix2] = console.completionContextUnderCursor();
  CHECK(base2 == QStringLiteral("sel"));
  CHECK(prefix2 == QStringLiteral("tra"));
  console.updateCompleter(true);
  console.insertCompletion(QStringLiteral("translate"));
  CHECK(input->toPlainText() == QStringLiteral("sel.translate"));

  input->setPlainText(QStringLiteral("sel.brush"));
  cursor = input->textCursor();
  cursor.movePosition(QTextCursor::End);
  input->setTextCursor(cursor);
  const auto [base3, prefix3] = console.completionContextUnderCursor();
  CHECK(base3 == QStringLiteral("sel"));
  CHECK(prefix3 == QStringLiteral("brush"));
  console.updateCompleter(true);
  console.insertCompletion(QStringLiteral("brushes"));
  CHECK(input->toPlainText() == QStringLiteral("sel.brushes"));

  // Test chained and indexed expression completion
  input->setPlainText(QStringLiteral("sel.brush.entity.prop"));
  cursor = input->textCursor();
  cursor.movePosition(QTextCursor::End);
  input->setTextCursor(cursor);
  const auto [base4, prefix4] = console.completionContextUnderCursor();
  CHECK(base4 == QStringLiteral("sel.brush.entity"));
  CHECK(prefix4 == QStringLiteral("prop"));
  console.updateCompleter(true);
  console.insertCompletion(QStringLiteral("properties"));
  CHECK(input->toPlainText() == QStringLiteral("sel.brush.entity.properties"));

  input->setPlainText(QStringLiteral("sel.brushes[0].ent"));
  cursor = input->textCursor();
  cursor.movePosition(QTextCursor::End);
  input->setTextCursor(cursor);
  const auto [base5, prefix5] = console.completionContextUnderCursor();
  CHECK(base5 == QStringLiteral("sel.brushes[0]"));
  CHECK(prefix5 == QStringLiteral("ent"));
  console.updateCompleter(true);
  console.insertCompletion(QStringLiteral("entity"));
  CHECK(input->toPlainText() == QStringLiteral("sel.brushes[0].entity"));

  // Test selection all_entities completion
  input->setPlainText(QStringLiteral("sel.all_"));
  cursor = input->textCursor();
  cursor.movePosition(QTextCursor::End);
  input->setTextCursor(cursor);
  const auto [base6, prefix6] = console.completionContextUnderCursor();
  CHECK(base6 == QStringLiteral("sel"));
  CHECK(prefix6 == QStringLiteral("all_"));
  console.updateCompleter(true);
  console.insertCompletion(QStringLiteral("all_entities"));
  CHECK(input->toPlainText() == QStringLiteral("sel.all_entities"));

  // Completion members come from the public trenchbroom API catalog.
  input->setPlainText(QStringLiteral("sel."));
  cursor = input->textCursor();
  cursor.movePosition(QTextCursor::End);
  input->setTextCursor(cursor);
  console.updateCompleter(true);
  auto labels = completionLabels();
  CHECK(std::ranges::find(labels, QStringLiteral("entity")) != labels.end());
  CHECK(std::ranges::find(labels, QStringLiteral("properties")) != labels.end());
  CHECK(std::ranges::find(labels, QStringLiteral("empty")) == labels.end());

  input->setPlainText(QStringLiteral("doc."));
  cursor = input->textCursor();
  cursor.movePosition(QTextCursor::End);
  input->setTextCursor(cursor);
  console.updateCompleter(true);
  labels = completionLabels();
  CHECK(std::ranges::find(labels, QStringLiteral("materials")) != labels.end());
  CHECK(std::ranges::find(labels, QStringLiteral("layers")) == labels.end());

  input->setPlainText(QStringLiteral("sel.brush."));
  cursor = input->textCursor();
  cursor.movePosition(QTextCursor::End);
  input->setTextCursor(cursor);
  console.updateCompleter(true);
  labels = completionLabels();
  CHECK(std::ranges::find(labels, QStringLiteral("faces")) != labels.end());
  CHECK(std::ranges::find(labels, QStringLiteral("bounds")) == labels.end());

  // Lists do not expose their element members until explicitly indexed.
  input->setPlainText(QStringLiteral("sel.brush.entity.brushes."));
  cursor = input->textCursor();
  cursor.movePosition(QTextCursor::End);
  input->setTextCursor(cursor);
  console.updateCompleter(true);
  CHECK(completionLabels().empty());
  CHECK_FALSE(console.completer()->popup()->isVisible());

  input->setPlainText(QStringLiteral("sel.brush.entity.brushes[0]."));
  cursor = input->textCursor();
  cursor.movePosition(QTextCursor::End);
  input->setTextCursor(cursor);
  console.updateCompleter(true);
  labels = completionLabels();
  CHECK(std::ranges::find(labels, QStringLiteral("entity")) != labels.end());

  input->setPlainText(QStringLiteral("sel.brushes[0]."));
  cursor = input->textCursor();
  cursor.movePosition(QTextCursor::End);
  input->setTextCursor(cursor);
  console.updateCompleter(true);
  labels = completionLabels();
  CHECK(std::ranges::find(labels, QStringLiteral("faces")) != labels.end());

  input->setPlainText(QStringLiteral("unknown_name."));
  cursor = input->textCursor();
  cursor.movePosition(QTextCursor::End);
  input->setTextCursor(cursor);
  console.updateCompleter(true);
  CHECK(completionLabels().empty());

  console.setCompletionRootProvider([](const std::string_view name) {
    if (name == "api")
    {
      return PythonCompletionRoot{true, PythonApiValueType{PythonApiType::Module}};
    }
    if (name == "x")
    {
      return PythonCompletionRoot{true, PythonApiValueType{PythonApiType::Brush}};
    }
    if (name == "e")
    {
      return PythonCompletionRoot{true, std::nullopt};
    }
    return PythonCompletionRoot{};
  });

  input->setPlainText(QStringLiteral("api.documents.current().selection."));
  cursor = input->textCursor();
  cursor.movePosition(QTextCursor::End);
  input->setTextCursor(cursor);
  console.updateCompleter(true);
  labels = completionLabels();
  CHECK(std::ranges::find(labels, QStringLiteral("brush")) != labels.end());

  input->setPlainText(QStringLiteral("x."));
  cursor = input->textCursor();
  cursor.movePosition(QTextCursor::End);
  input->setTextCursor(cursor);
  console.updateCompleter(true);
  labels = completionLabels();
  CHECK(std::ranges::find(labels, QStringLiteral("entity")) != labels.end());

  input->setPlainText(QStringLiteral("e."));
  cursor = input->textCursor();
  cursor.movePosition(QTextCursor::End);
  input->setTextCursor(cursor);
  console.updateCompleter(true);
  CHECK(completionLabels().empty());

  input->setPlainText(QStringLiteral("tren"));
  cursor = input->textCursor();
  cursor.movePosition(QTextCursor::End);
  input->setTextCursor(cursor);
  console.updateCompleter(true);
  labels = completionLabels();
  CHECK(std::ranges::find(labels, QStringLiteral("trenchbroom")) != labels.end());
  CHECK(std::ranges::find(labels, QStringLiteral("tb")) == labels.end());

  // Test deletion and auto-dismissal
  input->setPlainText(QStringLiteral(""));
  cursor = input->textCursor();
  input->setTextCursor(cursor);
  console.updateCompleter(false);
  CHECK(console.completer()->popup()->isVisible() == false);

  input->setPlainText(QStringLiteral("'sel.br"));
  cursor = input->textCursor();
  cursor.movePosition(QTextCursor::End);
  input->setTextCursor(cursor);
  const auto [stringBase, stringPrefix] = console.completionContextUnderCursor();
  CHECK(stringBase.isEmpty());
  CHECK(stringPrefix.isEmpty());

  input->setPlainText(QStringLiteral("value = 1 # sel.br"));
  cursor = input->textCursor();
  cursor.movePosition(QTextCursor::End);
  input->setTextCursor(cursor);
  const auto [commentBase, commentPrefix] = console.completionContextUnderCursor();
  CHECK(commentBase.isEmpty());
  CHECK(commentPrefix.isEmpty());

  // Test Tab key completion workflow
  input->setPlainText(QStringLiteral("doc.selecti"));
  cursor = input->textCursor();
  cursor.movePosition(QTextCursor::End);
  input->setTextCursor(cursor);
  console.updateCompleter(true);
  CHECK(console.completer()->popup()->isVisible());
  QTest::keyClick(input, Qt::Key_Tab);
  CHECK(input->toPlainText() == QStringLiteral("doc.selection"));
  CHECK(console.completer()->popup()->isVisible() == false);

  // Punctuation dismisses stale completions, and Enter executes rather than accepting.
  input->setPlainText(QStringLiteral("doc.save"));
  cursor = input->textCursor();
  cursor.movePosition(QTextCursor::End);
  input->setTextCursor(cursor);
  console.updateCompleter(true);
  REQUIRE(console.completer()->popup()->isVisible());
  QTest::keyClick(input, Qt::Key_ParenLeft);
  CHECK_FALSE(console.completer()->popup()->isVisible());
  QTest::keyClick(input, Qt::Key_ParenRight);
  QTest::keyClick(input, Qt::Key_Return);
  REQUIRE(commands.size() == 3u);
  CHECK(commands.back() == "doc.save()");

  // Test Tab key indentation (4 spaces)
  input->setPlainText(QStringLiteral(""));
  cursor = input->textCursor();
  input->setTextCursor(cursor);
  QTest::keyClick(input, Qt::Key_Tab);
  CHECK(input->toPlainText() == QStringLiteral("    "));
}

} // namespace tb::ui
