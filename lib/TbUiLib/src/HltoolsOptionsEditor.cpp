/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/HltoolsOptionsEditor.h"

#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "ui/FixedWidthFont.h"
#include "ui/PanelLayout.h"
#include "ui/QStyleUtils.h"

#include <algorithm>
#include <utility>

namespace tb::ui
{
namespace
{
struct Token
{
  qsizetype start;
  qsizetype length;
  QString text;
};

bool matchesFlag(const QString& token, const QString& flag)
{
  return token == flag || (flag == "-nopaque" && token == "-noopaque");
}

// Preserve every untouched byte, including quoted paths and EL expressions with
// spaces. Only literal option tokens are editable through the visual controls.
std::vector<Token> tokens(const QString& text)
{
  auto result = std::vector<Token>{};
  for (qsizetype i = 0; i < text.size();)
  {
    if (text[i].isSpace())
    {
      ++i;
      continue;
    }
    const auto start = i;
    auto quote = QChar{};
    auto depth = 0;
    auto escaped = false;
    while (i < text.size())
    {
      const auto c = text[i];
      if (!escaped)
      {
        if (!quote.isNull())
        {
          if (c == quote)
            quote = {};
        }
        else if (c == '"' || (depth > 0 && c == '\''))
          quote = c;
        else if (c == '{' && (depth > 0 || (i > start && text[i - 1] == '$')))
          ++depth;
        else if (c == '}' && depth > 0)
          --depth;
        else if (c.isSpace() && depth == 0)
          break;
      }
      escaped = c == '\\' && !escaped;
      ++i;
    }
    auto value = text.mid(start, i - start);
    if (
      value.size() >= 2 && value.startsWith('"') && value.endsWith('"')
      && !value.contains("${"))
      value = value.mid(1, value.size() - 2);
    result.push_back({start, i - start, std::move(value)});
  }
  return result;
}

QString optionValue(const std::vector<Token>& arguments, const QString& flag)
{
  auto result = QString{};
  for (size_t i = 0; i < arguments.size(); ++i)
    if (matchesFlag(arguments[i].text, flag))
      result = i + 1 < arguments.size() ? arguments[i + 1].text : QString{};
  return result;
}
} // namespace

HltoolsOptionsEditor::HltoolsOptionsEditor(QWidget* parent)
  : QWidget{parent}
{
  auto* layout = createPanelColumn(this, PanelInsets::None);
  auto* tools = createPanelActions();
  m_stage = new QComboBox{};
  m_stage->setObjectName("HltoolsOptionStage");
  m_stage->addItem(tr("All stages"), "");
  for (const auto* stage : {"CSG", "BSP", "VIS", "RAD"})
    m_stage->addItem(stage, stage);
  m_filter = new QLineEdit{};
  m_filter->setObjectName("HltoolsOptionFilter");
  m_filter->setPlaceholderText(tr("Filter options or flags"));
  m_filter->setClearButtonEnabled(true);
  m_modifiedOnly = new QCheckBox{tr("Modified only")};
  m_modifiedOnly->setObjectName("HltoolsModifiedOnly");
  tools->addWidget(m_stage);
  tools->addWidget(m_filter, 1);
  tools->addWidget(m_modifiedOnly);
  layout->addLayout(tools);
  auto* content = new QWidget{};
  auto* rows = createPanelColumn(content, PanelInsets::None);
  rows->setSpacing(4);

  auto add = [&](
               const char* stage,
               const char* flag,
               const QString& label,
               const QString& help,
               const QStringList& values = QStringList{}) {
    auto* row = new QWidget{};
    row->setToolTip(help);
    auto* rowLayout = createPanelActions(row);
    rowLayout->setContentsMargins(4, 0, 4, 0);
    auto* checkbox = new QCheckBox{label};
    checkbox->setObjectName("HltoolsOption_" + QString::fromLatin1(flag + 1));
    rowLayout->addWidget(checkbox);
    QComboBox* value = nullptr;
    if (!values.empty())
    {
      value = new QComboBox{};
      value->setObjectName(checkbox->objectName() + "Value");
      value->addItems(values);
      value->setEditable(QString::fromLatin1(flag) != "-cliptype");
      value->setInsertPolicy(QComboBox::NoInsert);
      value->setMinimumContentsLength(10);
      rowLayout->addWidget(value);
    }
    rowLayout->addStretch();
    auto* flagLabel = new QLabel{flag};
    flagLabel->setFont(Fonts::fixedWidthFont());
    setInfoStyle(flagLabel);
    rowLayout->addWidget(flagLabel);
    auto* stageLabel =
      new QLabel{QString{stage}.isEmpty() ? tr("Shared") : QString{stage}};
    setInfoStyle(stageLabel);
    rowLayout->addWidget(stageLabel);
    rows->addWidget(row);
    const auto entry =
      Entry{flag, stage, row, checkbox, value, value ? value->count() : 0};
    m_entries.push_back(entry);
    connect(checkbox, &QCheckBox::clicked, this, [this, entry](bool checked) {
      changeOption(
        entry,
        checked ? (entry.value ? entry.value->currentText() : entry.flag) : QString{});
    });
    if (value)
    {
      const auto changed = [this, entry] {
        if (entry.checkbox->isChecked())
          changeOption(entry, entry.value->currentText());
      };
      connect(value, &QComboBox::activated, this, changed);
      if (value->isEditable())
        connect(value->lineEdit(), &QLineEdit::editingFinished, this, changed);
    }
  };

  // Verified against the pinned hltools common/cmdline.cpp and stage parsers.
  add(
    "CSG",
    "-cliptype",
    tr("Collision hull type"),
    tr("Override the default simple collision hull construction."),
    {"simple", "precise", "normalized", "smallest", "legacy", "legacytrim"});
  add(
    "CSG",
    "-noskyclip",
    tr("Disable sky clipping"),
    tr("Do not generate collision clipping for sky brushes."));
  add(
    "CSG / BSP",
    "-noclip",
    tr("Disable collision hulls"),
    tr("Do not build player collision hulls. Also affects BSP."));
  add(
    "CSG",
    "-clipmerge",
    tr("Merge legacy collision hulls"),
    tr("Merge clipping generated by the legacy hull algorithm."));
  add(
    "CSG",
    "-wadtextures",
    tr("Keep textures in external WADs"),
    tr("Store texture references instead of embedding every used texture. The game needs "
       "the WAD files."));
  add(
    "CSG",
    "-nowadautodetect",
    tr("Disable WAD auto detection"),
    tr("Keep the supplied WAD list. Only useful with external WAD textures."));
  add(
    "CSG",
    "-nolightopt",
    tr("Keep redundant light entities"),
    tr("Disable the light entity optimization pass."));
  add(
    "CSG",
    "-nullifytrigger",
    tr("Strip trigger textures"),
    tr("Replace trigger textures with NULL."));
  add(
    "BSP",
    "-nofill",
    tr("Disable outside filling"),
    tr("Keep outside space. Intended for debugging map leaks."));
  add(
    "BSP",
    "-noinsidefill",
    tr("Disable inside filling"),
    tr("Keep enclosed unused space."));
  add(
    "BSP",
    "-notjunc",
    tr("Disable T-junction repair"),
    tr("Skip T-junction fixes; cracks may remain between faces."));
  add("BSP", "-nobrink", tr("Disable brink repair"), tr("Skip collision edge repairs."));
  add(
    "BSP",
    "-noopt",
    tr("Disable BSP optimization"),
    tr("Skip the BSP tree optimization pass."));
  add(
    "BSP",
    "-noclipnodemerge",
    tr("Disable clipnode merging"),
    tr("Keep clipnodes separate."));
  add(
    "BSP",
    "-deepclip",
    tr("Use deep collision clipping"),
    tr("Enable deep clipping in BSP construction."));
  add(
    "BSP",
    "-allleaks",
    tr("Find all leaks"),
    tr("Survey every leak and put their paths in the pointfile."));
  add(
    "BSP",
    "-nonulltex",
    tr("Keep NULL textures"),
    tr("Do not remove NULL textures during BSP generation."));
  add(
    "BSP",
    "-nohull2",
    tr("Skip large collision hull"),
    tr("Do not build hull 2 for large monsters."));
  add(
    "VIS",
    "-nofull",
    tr("Disable full visibility"),
    tr("Use normal visibility instead of the default full pass. Fast quality still takes "
       "precedence."));
  add(
    "VIS",
    "-nofixprt",
    tr("Disable portal repair"),
    tr("Skip automatic portal corrections."));
  add(
    "RAD",
    "-bounce",
    tr("Light bounces"),
    tr("Override bounce count. The compiler defaults to 8, or 12 with Extra quality. "
       "Variables are allowed."),
    {"8", "0", "2", "4", "12", "16"});
  add(
    "RAD",
    "-nolerp",
    tr("Disable light interpolation"),
    tr("Do not interpolate lighting between patches."));
  add(
    "RAD",
    "-notexscale",
    tr("Ignore texture scale for patches"),
    tr("Do not scale lighting patches with texture scale."));
  add(
    "RAD",
    "-nosubdivide",
    tr("Disable patch subdivision"),
    tr("Keep lighting patches unsplit."));
  add(
    "RAD",
    "-noskyfix",
    tr("Disable sky lighting fix"),
    tr("Use the legacy sky lighting behavior."));
  add(
    "RAD",
    "-nospread",
    tr("Disable light spreading"),
    tr("Disable texture light spreading."));
  add(
    "RAD",
    "-nopaque",
    tr("Disable opaque brush shadows"),
    tr("Ignore opaque brush blockers during lighting."));
  add(
    "RAD",
    "-nostudioshadow",
    tr("Disable model shadows"),
    tr("Skip shadows from studio models."));
  add(
    "RAD",
    "-rgbtransfers",
    tr("Use RGB light transfers"),
    tr("Keep color in bounced light transfers."));
  add(
    "RAD",
    "-customshadowwithbounce",
    tr("Apply custom shadows to bounced light"),
    tr("Include custom shadow behavior in indirect lighting."));
  add(
    "RAD",
    "-notextures",
    tr("Ignore texture reflectivity"),
    tr("Do not use texture pixels when estimating bounced light reflectivity."));
  add(
    "RAD",
    "-nobleedfix",
    tr("Disable light bleed repair"),
    tr("Disable the lighting bleed correction pass."));
  add(
    "RAD",
    "-circus",
    tr("Color lighting patches for debugging"),
    tr("Use diagnostic patch colors instead of normal lighting."));
  add(
    "RAD",
    "-drawoverload",
    tr("Highlight lighting overloads"),
    tr("Mark overloaded lighting samples for debugging."));
  add(
    "RAD",
    "-pre25",
    tr("Use pre-anniversary brightness limit"),
    tr("Clamp lighting to the fullbright threshold from before the Half-Life 25th "
       "anniversary update."));
  add(
    "",
    "-threads",
    tr("Worker threads"),
    tr("Override automatic worker count. Variables such as ${CPU_COUNT} are allowed."),
    {"4", "1", "2", "8", "16", "${CPU_COUNT}"});
  add(
    "",
    "-nochart",
    tr("Hide BSP usage chart"),
    tr("Skip the final BSP resource usage chart."));

  m_empty = new QLabel{tr("No matching options")};
  setInfoStyle(m_empty);
  rows->addWidget(m_empty);
  rows->addStretch();
  auto* scroll = createPanelScrollArea(content);
  scroll->setMinimumHeight(160);
  scroll->setMaximumHeight(200);
  layout->addWidget(scroll);
  connect(m_filter, &QLineEdit::textChanged, this, &HltoolsOptionsEditor::filterEntries);
  connect(
    m_stage, &QComboBox::currentIndexChanged, this, &HltoolsOptionsEditor::filterEntries);
  connect(
    m_modifiedOnly, &QCheckBox::toggled, this, &HltoolsOptionsEditor::filterEntries);
  setParameters({});
}

void HltoolsOptionsEditor::setParameters(const QString& parameters)
{
  m_parameters = parameters;
  const auto arguments = tokens(parameters);
  for (const auto& entry : m_entries)
  {
    const auto found = std::ranges::any_of(
      arguments, [&](const auto& token) { return matchesFlag(token.text, entry.flag); });
    const auto blocker = QSignalBlocker{entry.checkbox};
    entry.checkbox->setChecked(found);
    if (entry.value)
    {
      const auto valueBlocker = QSignalBlocker{entry.value};
      const auto value = optionValue(arguments, entry.flag);
      if (found)
      {
        while (entry.value->count() > entry.presetCount)
          entry.value->removeItem(entry.value->count() - 1);
        auto index = entry.value->findText(value);
        if (index < 0)
        {
          entry.value->addItem(value);
          index = entry.value->count() - 1;
        }
        entry.value->setCurrentIndex(index);
      }
      entry.value->setEnabled(found);
    }
  }
  filterEntries();
}

void HltoolsOptionsEditor::filterEntries()
{
  const auto filter = m_filter->text().trimmed();
  const auto stage = m_stage->currentData().toString();
  auto visible = 0;
  for (const auto& entry : m_entries)
  {
    const auto matches =
      (stage.isEmpty() || entry.stage.isEmpty() || entry.stage.contains(stage))
      && (!m_modifiedOnly->isChecked() || entry.checkbox->isChecked())
      && (filter.isEmpty() || (entry.checkbox->text() + " " + entry.flag + " " + entry.row->toolTip()).contains(filter, Qt::CaseInsensitive));
    entry.row->setVisible(matches);
    visible += matches ? 1 : 0;
  }
  m_empty->setVisible(visible == 0);
}

void HltoolsOptionsEditor::changeOption(const Entry& entry, const QString& value)
{
  auto text = m_parameters;
  const auto arguments = tokens(text);
  for (size_t i = arguments.size(); i > 0; --i)
  {
    const auto& token = arguments[i - 1];
    if (matchesFlag(token.text, entry.flag))
    {
      auto end = token.start + token.length;
      if (entry.value && i < arguments.size())
      {
        auto numeric = false;
        arguments[i].text.toDouble(&numeric);
        if (!arguments[i].text.startsWith('-') || numeric)
          end = arguments[i].start + arguments[i].length;
      }
      text.remove(token.start, end - token.start);
    }
  }
  text = text.trimmed();
  if (!value.isEmpty())
  {
    if (!text.isEmpty())
      text += ' ';
    text += entry.flag;
    if (entry.value)
      text += ' ' + value;
  }
  setParameters(text);
  emit parametersChanged(text);
}
} // namespace tb::ui
