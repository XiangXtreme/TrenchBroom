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

#include "ui/PythonConsole.h"

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QAbstractListModel>
#include <QCompleter>
#include <QEvent>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListView>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSizePolicy>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include "base/PreferenceManager.h"
#include "prefs/Preferences.h"
#include "ui/FixedWidthFont.h"
#include "ui/QThreadUtils.h"
#include "ui/Theme.h"
#include "ui/python/PythonApiCatalog.h"
#include "ui/python/PythonCompletionEngine.h"

#include "kd/contracts.h"

#include <algorithm>
#include <string_view>
#include <utility>
#include <vector>

namespace tb::ui
{
namespace
{
constexpr auto MaxHistorySize = size_t{100u};

enum class SuggestionKind
{
  Function,
  Method,
  Property,
  Keyword,
  Class,
  Variable,
  Module
};

struct SuggestionItem
{
  QString label;
  SuggestionKind kind;
  QString detail;
};

class SuggestionModel : public QAbstractListModel
{
private:
  std::vector<SuggestionItem> m_items;

public:
  explicit SuggestionModel(QObject* parent = nullptr)
    : QAbstractListModel{parent}
  {
  }

  int rowCount(const QModelIndex& parent = QModelIndex{}) const override
  {
    return parent.isValid() ? 0 : static_cast<int>(m_items.size());
  }

  QVariant data(const QModelIndex& index, const int role) const override
  {
    if (
      !index.isValid() || index.row() < 0
      || static_cast<size_t>(index.row()) >= m_items.size())
    {
      return {};
    }

    const auto& item = m_items[static_cast<size_t>(index.row())];
    switch (role)
    {
    case Qt::DisplayRole:
    case Qt::EditRole:
      return item.label;
    case Qt::UserRole + 1:
      return static_cast<int>(item.kind);
    case Qt::UserRole + 2:
      return item.detail;
    default:
      return {};
    }
  }

  void setItems(std::vector<SuggestionItem> items)
  {
    beginResetModel();
    m_items = std::move(items);
    std::sort(m_items.begin(), m_items.end(), [](const auto& a, const auto& b) {
      return a.label.compare(b.label, Qt::CaseInsensitive) < 0;
    });
    endResetModel();
  }
};

class SuggestionItemDelegate : public QStyledItemDelegate
{
public:
  explicit SuggestionItemDelegate(QObject* parent = nullptr)
    : QStyledItemDelegate{parent}
  {
  }

  void paint(
    QPainter* painter,
    const QStyleOptionViewItem& option,
    const QModelIndex& index) const override
  {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const auto label = index.data(Qt::DisplayRole).toString();
    const auto kind = static_cast<SuggestionKind>(index.data(Qt::UserRole + 1).toInt());
    const auto detail = index.data(Qt::UserRole + 2).toString();

    const auto isSelected = (option.state & QStyle::State_Selected) != 0;
    const auto isHovered = (option.state & QStyle::State_MouseOver) != 0;

    auto rowRect = option.rect.adjusted(2, 1, -2, -1);

    if (isSelected)
    {
      painter->setPen(Qt::NoPen);
      painter->setBrush(option.palette.highlight());
      painter->drawRoundedRect(rowRect, 4, 4);
    }
    else if (isHovered)
    {
      auto hoverColor = option.palette.highlight().color();
      hoverColor.setAlpha(35);
      painter->setPen(Qt::NoPen);
      painter->setBrush(hoverColor);
      painter->drawRoundedRect(rowRect, 4, 4);
    }

    // Kind Badge
    auto badgeBg = QColor{60, 60, 60};
    auto badgeFg = QColor{255, 255, 255};
    auto badgeText = QStringLiteral("txt");

    switch (kind)
    {
    case SuggestionKind::Method:
      badgeBg = QColor{30, 110, 190};
      badgeText = QStringLiteral("m");
      break;
    case SuggestionKind::Function:
      badgeBg = QColor{36, 120, 204};
      badgeText = QStringLiteral("fn");
      break;
    case SuggestionKind::Property:
      badgeBg = QColor{35, 134, 54};
      badgeText = QStringLiteral("prop");
      break;
    case SuggestionKind::Keyword:
      badgeBg = QColor{137, 87, 229};
      badgeText = QStringLiteral("kw");
      break;
    case SuggestionKind::Class:
      badgeBg = QColor{210, 153, 34};
      badgeText = QStringLiteral("cls");
      break;
    case SuggestionKind::Variable:
      badgeBg = QColor{56, 139, 253};
      badgeText = QStringLiteral("var");
      break;
    case SuggestionKind::Module:
      badgeBg = QColor{187, 128, 9};
      badgeText = QStringLiteral("mod");
      break;
    }

    const auto badgeRect =
      QRect{rowRect.left() + 4, rowRect.top() + (rowRect.height() - 16) / 2, 28, 16};
    painter->setPen(Qt::NoPen);
    painter->setBrush(badgeBg);
    painter->drawRoundedRect(badgeRect, 3, 3);

    auto badgeFont = option.font;
    badgeFont.setPixelSize(10);
    badgeFont.setBold(true);
    painter->setFont(badgeFont);
    painter->setPen(badgeFg);
    painter->drawText(badgeRect, Qt::AlignCenter, badgeText);

    // Label Text
    auto labelFont = option.font;
    labelFont.setPixelSize(12);
    painter->setFont(labelFont);
    painter->setPen(
      isSelected ? option.palette.highlightedText().color()
                 : option.palette.text().color());

    const auto textLeft = badgeRect.right() + 8;
    const auto detailWidth = 110;
    const auto labelWidth =
      std::max(40, rowRect.width() - (textLeft - rowRect.left()) - detailWidth);
    const auto labelRect = QRect{textLeft, rowRect.top(), labelWidth, rowRect.height()};
    painter->drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, label);

    // Detail Text on Right
    if (!detail.isEmpty())
    {
      auto detailFont = option.font;
      detailFont.setPixelSize(11);
      painter->setFont(detailFont);

      auto detailColor = isSelected ? option.palette.highlightedText().color()
                                    : option.palette.placeholderText().color();
      if (!isSelected)
      {
        detailColor.setAlpha(170);
      }
      painter->setPen(detailColor);

      const auto detailRect = QRect{
        rowRect.right() - detailWidth, rowRect.top(), detailWidth - 6, rowRect.height()};
      painter->drawText(detailRect, Qt::AlignRight | Qt::AlignVCenter, detail);
    }

    painter->restore();
  }

  QSize sizeHint(
    const QStyleOptionViewItem& option, const QModelIndex& index) const override
  {
    Q_UNUSED(option);
    Q_UNUSED(index);
    return QSize{320, 24};
  }
};

QFont consoleFont()
{
  auto font = Fonts::fixedWidthFont();
  const auto requestedFamily =
    QString::fromStdString(pref(Preferences::PythonConsoleFontFamily)).trimmed();
  for (const auto& availableFamily : QFontDatabase::families())
  {
    if (
      availableFamily.compare(requestedFamily, Qt::CaseInsensitive) == 0
      && QFontDatabase::isFixedPitch(availableFamily))
    {
      font.setFamily(availableFamily);
      break;
    }
  }
  font.setStyleHint(QFont::TypeWriter);
  font.setFixedPitch(true);
  font.setPointSize(std::clamp(
    pref(Preferences::PythonConsoleFontSize),
    Preferences::MinPythonConsoleFontSize,
    Preferences::MaxPythonConsoleFontSize));
  return font;
}

std::string utf8String(const QString& string)
{
  const auto utf8 = string.toUtf8();
  return {utf8.constData(), static_cast<size_t>(utf8.size())};
}

QString formatPrompt(const QString& source)
{
  const auto lines = source.split('\n', Qt::KeepEmptyParts);
  auto result = QStringLiteral(">>> ") + lines.front();
  for (auto i = 1; i < lines.size(); ++i)
  {
    result += QStringLiteral("\n... ") + lines[i];
  }
  return result;
}

QString apiString(const std::string_view value)
{
  return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

SuggestionKind suggestionKind(const PythonApiSymbolKind kind)
{
  switch (kind)
  {
  case PythonApiSymbolKind::Class:
    return SuggestionKind::Class;
  case PythonApiSymbolKind::Function:
    return SuggestionKind::Function;
  case PythonApiSymbolKind::Property:
    return SuggestionKind::Property;
  case PythonApiSymbolKind::Method:
    return SuggestionKind::Method;
  }
  return SuggestionKind::Property;
}

std::vector<SuggestionItem> suggestionItems(const PythonApiType type)
{
  auto result = std::vector<SuggestionItem>{};
  const auto symbols = pythonApiSymbols(type);
  result.reserve(symbols.size());
  for (const auto& symbol : symbols)
  {
    result.push_back(SuggestionItem{
      apiString(symbol.name), suggestionKind(symbol.kind), apiString(symbol.detail)});
  }
  return result;
}

std::vector<SuggestionItem> globalSuggestionItems()
{
  auto result = std::vector<SuggestionItem>{
    {QStringLiteral("doc"), SuggestionKind::Variable, QStringLiteral("Document")},
    {QStringLiteral("sel"), SuggestionKind::Variable, QStringLiteral("Selection")},
    {QStringLiteral("trenchbroom"), SuggestionKind::Module, QStringLiteral("module")},
  };

  const auto moduleSymbols = pythonApiSymbols(PythonApiType::Module);
  for (const auto helperName : pythonConsoleHelperNames())
  {
    const auto symbol = std::find_if(
      moduleSymbols.begin(), moduleSymbols.end(), [&](const auto& candidate) {
        return candidate.name == helperName;
      });
    contract_assert(symbol != moduleSymbols.end());
    result.push_back(SuggestionItem{
      apiString(symbol->name), suggestionKind(symbol->kind), apiString(symbol->detail)});
  }

  const auto builtins = std::vector<SuggestionItem>{
    {QStringLiteral("for"), SuggestionKind::Keyword, QStringLiteral("keyword")},
    {QStringLiteral("in"), SuggestionKind::Keyword, QStringLiteral("keyword")},
    {QStringLiteral("range"), SuggestionKind::Function, QStringLiteral("(stop) -> list")},
    {QStringLiteral("with"), SuggestionKind::Keyword, QStringLiteral("keyword")},
    {QStringLiteral("def"), SuggestionKind::Keyword, QStringLiteral("keyword")},
    {QStringLiteral("if"), SuggestionKind::Keyword, QStringLiteral("keyword")},
    {QStringLiteral("else"), SuggestionKind::Keyword, QStringLiteral("keyword")},
    {QStringLiteral("elif"), SuggestionKind::Keyword, QStringLiteral("keyword")},
    {QStringLiteral("return"), SuggestionKind::Keyword, QStringLiteral("keyword")},
    {QStringLiteral("import"), SuggestionKind::Keyword, QStringLiteral("keyword")},
    {QStringLiteral("as"), SuggestionKind::Keyword, QStringLiteral("keyword")},
    {QStringLiteral("while"), SuggestionKind::Keyword, QStringLiteral("keyword")},
    {QStringLiteral("break"), SuggestionKind::Keyword, QStringLiteral("keyword")},
    {QStringLiteral("continue"), SuggestionKind::Keyword, QStringLiteral("keyword")},
    {QStringLiteral("pass"), SuggestionKind::Keyword, QStringLiteral("keyword")},
    {QStringLiteral("True"), SuggestionKind::Keyword, QStringLiteral("bool")},
    {QStringLiteral("False"), SuggestionKind::Keyword, QStringLiteral("bool")},
    {QStringLiteral("None"), SuggestionKind::Keyword, QStringLiteral("NoneType")},
    {QStringLiteral("print"), SuggestionKind::Function, QStringLiteral("(*values)")},
    {QStringLiteral("len"), SuggestionKind::Function, QStringLiteral("(obj) -> int")},
    {QStringLiteral("enumerate"), SuggestionKind::Function, QStringLiteral("(iterable)")},
    {QStringLiteral("zip"), SuggestionKind::Function, QStringLiteral("(*iterables)")},
    {QStringLiteral("list"), SuggestionKind::Class, QStringLiteral("type")},
    {QStringLiteral("dict"), SuggestionKind::Class, QStringLiteral("type")},
    {QStringLiteral("set"), SuggestionKind::Class, QStringLiteral("type")},
    {QStringLiteral("int"), SuggestionKind::Class, QStringLiteral("type")},
    {QStringLiteral("float"), SuggestionKind::Class, QStringLiteral("type")},
    {QStringLiteral("str"), SuggestionKind::Class, QStringLiteral("type")},
    {QStringLiteral("bool"), SuggestionKind::Class, QStringLiteral("type")},
  };
  result.insert(result.end(), builtins.begin(), builtins.end());
  return result;
}

bool isPythonCodeAtCursor(const QString& text, const qsizetype cursorPosition)
{
  enum class StringState
  {
    None,
    SingleQuoted,
    DoubleQuoted,
    TripleSingleQuoted,
    TripleDoubleQuoted,
  };

  auto stringState = StringState::None;
  auto inComment = false;
  auto escaped = false;
  for (auto i = qsizetype{0}; i < cursorPosition; ++i)
  {
    const auto ch = text[i];
    if (inComment)
    {
      if (ch == '\n')
      {
        inComment = false;
      }
      continue;
    }

    if (stringState != StringState::None)
    {
      const auto quote = stringState == StringState::SingleQuoted
                             || stringState == StringState::TripleSingleQuoted
                           ? '\''
                           : '"';
      const auto triple = stringState == StringState::TripleSingleQuoted
                          || stringState == StringState::TripleDoubleQuoted;
      if (escaped)
      {
        escaped = false;
      }
      else if (ch == '\\')
      {
        escaped = true;
      }
      else if (
        ch == quote
        && (!triple || (i + 2 < cursorPosition && text[i + 1] == quote && text[i + 2] == quote)))
      {
        stringState = StringState::None;
        if (triple)
        {
          i += 2;
        }
      }
      continue;
    }

    if (ch == '#')
    {
      inComment = true;
    }
    else if (ch == '\'' || ch == '"')
    {
      const auto triple =
        i + 2 < cursorPosition && text[i + 1] == ch && text[i + 2] == ch;
      if (ch == '\'')
      {
        stringState =
          triple ? StringState::TripleSingleQuoted : StringState::SingleQuoted;
      }
      else
      {
        stringState =
          triple ? StringState::TripleDoubleQuoted : StringState::DoubleQuoted;
      }
      if (triple)
      {
        i += 2;
      }
    }
  }

  return !inComment && stringState == StringState::None;
}
} // namespace

PythonConsole::PythonConsole(QWidget* parent)
  : Console{parent}
  , m_input{new QPlainTextEdit{this}}
  , m_runButton{new QToolButton{this}}
  , m_clearButton{new QToolButton{this}}
{
  textView()->setObjectName("PythonConsole_Output");

  if (auto* baseLayout = layout())
  {
    baseLayout->removeWidget(textView());
  }

  m_splitter = new QSplitter{Qt::Horizontal, this};
  m_splitter->setObjectName("PythonConsole_Splitter");

  auto* leftPane = new QWidget{m_splitter};
  leftPane->setObjectName("PythonConsole_LeftPane");
  auto* leftLayout = new QVBoxLayout{leftPane};
  leftLayout->setContentsMargins(0, 0, 0, 0);
  leftLayout->setSpacing(0);
  leftLayout->addWidget(textView());

  auto* rightPane = new QWidget{m_splitter};
  rightPane->setObjectName("PythonConsole_RightPane");
  auto* rightLayout = new QVBoxLayout{rightPane};
  rightLayout->setContentsMargins(8, 4, 8, 8);
  rightLayout->setSpacing(4);

  m_prompt =
    new QLabel{tr("Script Editor (Enter to run, Shift+Enter for newline)"), rightPane};
  m_prompt->setObjectName("PythonConsole_Prompt");
  m_prompt->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

  m_input->setObjectName("PythonConsole_Input");
  m_input->setAccessibleName(tr("Python command input"));
  m_input->setLineWrapMode(QPlainTextEdit::NoWrap);
  m_input->setTabChangesFocus(false);
  m_input->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  m_input->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  m_input->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  m_input->installEventFilter(this);

  setupCompleter();

  m_runButton->setObjectName("PythonConsole_Run");
  m_runButton->setText(tr("Run"));
  m_runButton->setToolTip(tr("Run current script (Enter)"));
  m_runButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
  m_runButton->setAutoRaise(true);
  m_runButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  m_runButton->setEnabled(false);
  connect(m_runButton, &QToolButton::clicked, this, &PythonConsole::executeCurrentInput);

  m_clearButton->setObjectName("PythonConsole_Clear");
  m_clearButton->setText(tr("Clear"));
  m_clearButton->setToolTip(tr("Clear console output"));
  m_clearButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
  m_clearButton->setAutoRaise(true);
  m_clearButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  connect(m_clearButton, &QToolButton::clicked, this, &Console::clear);

  connect(
    m_input, &QPlainTextEdit::textChanged, this, &PythonConsole::updateRunButtonEnabled);

  connect(m_input, &QPlainTextEdit::cursorPositionChanged, this, [this]() {
    if (m_completer && m_completer->popup() && m_completer->popup()->isVisible())
    {
      const auto [baseQualifier, prefix] = completionContextUnderCursor();
      if (prefix.isEmpty() && baseQualifier.isEmpty())
      {
        m_completer->popup()->hide();
      }
    }
  });

  rightLayout->addWidget(m_prompt);
  rightLayout->addWidget(m_input, 1);

  m_splitter->addWidget(leftPane);
  m_splitter->addWidget(rightPane);
  m_splitter->setStretchFactor(0, 1);
  m_splitter->setStretchFactor(1, 1);
  m_splitter->setSizes({420, 380});

  layout()->addWidget(m_splitter);

  auto& prefs = PreferenceManager::instance();
  m_notifierConnection +=
    prefs.preferenceDidChangeNotifier.connect([this](const auto& path) {
      if (
        path == Preferences::PythonConsoleFontFamily.path
        || path == Preferences::PythonConsoleFontSize.path)
      {
        updateFont();
      }
    });
  updateFont();
}

QWidget* PythonConsole::createTabBarPage(QWidget* parent)
{
  auto* actions = new QWidget{parent};
  actions->setObjectName("PythonConsole_TabActions");

  auto* actionsLayout = new QHBoxLayout{actions};
  actionsLayout->setContentsMargins(0, 0, 0, 0);
  actionsLayout->setSpacing(2);
  actionsLayout->addStretch(1);
  actionsLayout->addWidget(m_runButton, 0, Qt::AlignVCenter);
  actionsLayout->addWidget(m_clearButton, 0, Qt::AlignVCenter);

  return actions;
}

void PythonConsole::setCommandExecutor(std::function<void(const std::string&)> executor)
{
  m_commandExecutor = std::move(executor);
  updateRunButtonEnabled();
}

void PythonConsole::setCompletionRootProvider(PythonCompletionRootProvider provider)
{
  m_completionRootProvider = std::move(provider);
}

void PythonConsole::executeCurrentInput()
{
  const auto sourceText = m_input->toPlainText();
  if (sourceText.trimmed().isEmpty() || !m_commandExecutor)
  {
    return;
  }

  const auto source = utf8String(sourceText);
  info() << utf8String(formatPrompt(sourceText));

  if (m_history.empty() || m_history.back() != source)
  {
    m_history.push_back(source);
    if (m_history.size() > MaxHistorySize)
    {
      m_history.erase(m_history.begin());
    }
  }
  m_historyIndex = m_history.size();
  m_historyDraft.clear();
  m_input->clear();

  m_commandExecutor(source);
  m_input->setFocus();
}

QCompleter* PythonConsole::completer() const
{
  return m_completer;
}

void PythonConsole::setupCompleter()
{
  m_completer = new QCompleter{this};
  m_completer->setWidget(m_input);
  m_completer->setCompletionMode(QCompleter::PopupCompletion);
  m_completer->setCaseSensitivity(Qt::CaseInsensitive);
  m_completer->setFilterMode(Qt::MatchStartsWith);

  auto* suggestModel = new SuggestionModel{m_completer};
  m_completionModel = suggestModel;
  m_completer->setModel(m_completionModel);

  auto* popup = m_completer->popup();
  popup->setProperty("tbControlRole", "suggest-popup");
  popup->setItemDelegate(new SuggestionItemDelegate{popup});
  if (auto* listView = qobject_cast<QListView*>(popup))
  {
    listView->setUniformItemSizes(true);
    listView->setLayoutMode(QListView::Batched);
  }
  popup->setSelectionBehavior(QAbstractItemView::SelectRows);
  popup->setSelectionMode(QAbstractItemView::SingleSelection);
  popup->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  popup->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  popup->installEventFilter(this);

  connect(
    m_completer,
    QOverload<const QString&>::of(&QCompleter::activated),
    this,
    &PythonConsole::insertCompletion);
}

std::pair<QString, QString> PythonConsole::completionContextUnderCursor() const
{
  const auto cursor = m_input->textCursor();
  if (!isPythonCodeAtCursor(m_input->toPlainText(), cursor.position()))
  {
    return {};
  }
  const auto blockText = cursor.block().text();
  const auto posInBlock = cursor.positionInBlock();
  const auto lineToCursor = blockText.left(posInBlock);

  auto prefixStart = posInBlock;
  while (
    prefixStart > 0
    && (lineToCursor[prefixStart - 1].isLetterOrNumber() || lineToCursor[prefixStart - 1] == '_'))
  {
    --prefixStart;
  }
  const auto prefix = lineToCursor.mid(prefixStart);

  auto beforePrefix = lineToCursor.left(prefixStart).trimmed();
  if (beforePrefix.endsWith('.'))
  {
    beforePrefix.chop(1);
    beforePrefix = beforePrefix.trimmed();

    auto baseStart = beforePrefix.length();
    auto bracketDepth = 0;
    auto parenDepth = 0;
    while (baseStart > 0)
    {
      const auto ch = beforePrefix[baseStart - 1];
      if (ch == ']')
      {
        ++bracketDepth;
        --baseStart;
      }
      else if (ch == '[')
      {
        if (bracketDepth > 0)
        {
          --bracketDepth;
        }
        --baseStart;
      }
      else if (ch == ')')
      {
        ++parenDepth;
        --baseStart;
      }
      else if (ch == '(')
      {
        if (parenDepth > 0)
        {
          --parenDepth;
        }
        --baseStart;
      }
      else if (bracketDepth > 0 || parenDepth > 0)
      {
        --baseStart;
      }
      else if (ch.isLetterOrNumber() || ch == '_' || ch == '.')
      {
        --baseStart;
      }
      else
      {
        break;
      }
    }
    const auto baseQualifier = beforePrefix.mid(baseStart);
    return {baseQualifier, prefix};
  }

  return {QString{}, prefix};
}

void PythonConsole::updateCompleter(const bool explicitTrigger)
{
  if (!m_completer || !m_completionModel)
  {
    return;
  }

  const auto [baseQualifier, prefix] = completionContextUnderCursor();

  if (!explicitTrigger && prefix.isEmpty() && baseQualifier.isEmpty())
  {
    if (m_completer->popup() && m_completer->popup()->isVisible())
    {
      m_completer->popup()->hide();
    }
    return;
  }

  auto* suggestModel = static_cast<SuggestionModel*>(m_completionModel);
  auto items = std::vector<SuggestionItem>{};
  if (baseQualifier.isEmpty())
  {
    items = globalSuggestionItems();
  }
  else if (const auto type = pythonCompletionTypeForExpression(
             utf8String(baseQualifier), m_completionRootProvider);
           type && type->sequenceDepth == 0u)
  {
    items = suggestionItems(type->type);
  }
  suggestModel->setItems(std::move(items));
  m_completer->setCompletionPrefix(prefix);

  if (m_completer->completionCount() == 0)
  {
    if (m_completer->popup() && m_completer->popup()->isVisible())
    {
      m_completer->popup()->hide();
    }
    return;
  }

  auto cr = m_input->cursorRect();
  cr.setWidth(320);
  m_completer->complete(cr);

  // Pre-select the first row so Tab immediately inserts the top candidate.
  if (auto* popup = m_completer->popup())
  {
    const auto firstIndex = m_completer->completionModel()->index(0, 0);
    if (firstIndex.isValid())
    {
      popup->setCurrentIndex(firstIndex);
    }
  }
}

void PythonConsole::insertActiveCompletion()
{
  if (!m_completer)
  {
    return;
  }

  auto completionText = QString{};
  if (auto* popup = m_completer->popup())
  {
    const auto currentIndex = popup->currentIndex();
    if (currentIndex.isValid())
    {
      completionText = currentIndex.data(Qt::DisplayRole).toString();
    }
  }

  if (
    completionText.isEmpty() && m_completer->completionModel()
    && m_completer->completionModel()->rowCount() > 0)
  {
    completionText =
      m_completer->completionModel()->index(0, 0).data(Qt::DisplayRole).toString();
  }

  if (completionText.isEmpty())
  {
    completionText = m_completer->currentCompletion();
  }

  if (!completionText.isEmpty())
  {
    insertCompletion(completionText);
  }

  if (m_completer->popup() && m_completer->popup()->isVisible())
  {
    m_completer->popup()->hide();
  }
}

void PythonConsole::insertCompletion(const QString& completion)
{
  if (completion.isEmpty())
  {
    return;
  }

  const auto [baseQualifier, prefix] = completionContextUnderCursor();
  auto cursor = m_input->textCursor();
  cursor.movePosition(
    QTextCursor::Left, QTextCursor::KeepAnchor, static_cast<int>(prefix.length()));
  cursor.insertText(completion);
  m_input->setTextCursor(cursor);
}

bool PythonConsole::eventFilter(QObject* watched, QEvent* event)
{
  if (event->type() != QEvent::KeyPress)
  {
    return Console::eventFilter(watched, event);
  }

  auto* keyEvent = static_cast<QKeyEvent*>(event);
  const auto key = keyEvent->key();
  const auto modifiers = keyEvent->modifiers();
  const auto isReturn = (key == Qt::Key_Return || key == Qt::Key_Enter);
  const auto popupVisible =
    m_completer && m_completer->popup() && m_completer->popup()->isVisible();

  // Tab accepts a completion. Enter always retains its console execution behavior.
  if (popupVisible)
  {
    if (key == Qt::Key_Tab || key == Qt::Key_Backtab)
    {
      insertActiveCompletion();
      return true;
    }

    if (key == Qt::Key_Escape)
    {
      m_completer->popup()->hide();
      return true;
    }

    if (isReturn && modifiers.testFlag(Qt::ShiftModifier))
    {
      m_completer->popup()->hide();
      m_input->insertPlainText(QStringLiteral("\n"));
      return true;
    }

    if (
      key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_PageUp
      || key == Qt::Key_PageDown)
    {
      if (watched == m_input)
      {
        QCoreApplication::sendEvent(m_completer->popup(), event);
        return true;
      }
      return false;
    }
  }

  // If event arrived on popup but was not handled above, refocus input and forward
  if (m_completer && watched == m_completer->popup())
  {
    m_input->setFocus();
    QCoreApplication::sendEvent(m_input, event);
    return true;
  }

  if (watched != m_input)
  {
    return Console::eventFilter(watched, event);
  }

  // 1. Shift+Enter inserts newline in the editor
  if (isReturn && modifiers.testFlag(Qt::ShiftModifier))
  {
    if (popupVisible)
    {
      m_completer->popup()->hide();
    }
    m_input->insertPlainText(QStringLiteral("\n"));
    return true;
  }

  // 2. Enter (or Ctrl+Enter) executes current script
  if (
    isReturn && (modifiers == Qt::NoModifier || modifiers.testFlag(Qt::ControlModifier)))
  {
    if (popupVisible)
    {
      m_completer->popup()->hide();
    }
    executeCurrentInput();
    return true;
  }

  // 3. Tab or Ctrl+Space when popup is NOT visible
  if (
    (key == Qt::Key_Space && modifiers.testFlag(Qt::ControlModifier))
    || (key == Qt::Key_Tab && modifiers == Qt::NoModifier))
  {
    const auto [baseQualifier, prefix] = completionContextUnderCursor();
    if (!baseQualifier.isEmpty() || !prefix.isEmpty())
    {
      updateCompleter(true);
      return true;
    }
    if (key == Qt::Key_Tab)
    {
      m_input->insertPlainText(QStringLiteral("    "));
      return true;
    }
    return Console::eventFilter(watched, event);
  }

  // 4. History navigation (when completer is not visible)
  if (
    modifiers == Qt::NoModifier && key == Qt::Key_Up
    && m_input->textCursor().blockNumber() == 0)
  {
    showPreviousHistoryEntry();
    return true;
  }

  const auto cursor = m_input->textCursor();
  if (
    modifiers == Qt::NoModifier && key == Qt::Key_Down
    && cursor.blockNumber() == m_input->document()->blockCount() - 1)
  {
    showNextHistoryEntry();
    return true;
  }

  // 5. Reactive completion for typing, Backspace, Delete, and Dot
  const auto text = keyEvent->text();
  const auto isEditKey =
    (key == Qt::Key_Backspace || key == Qt::Key_Delete
     || (!text.isEmpty() && (text[0].isLetterOrNumber() || text[0] == '_' || text[0] == '.')));

  if (isEditKey)
  {
    QTimer::singleShot(0, this, [this]() { updateCompleter(false); });
  }
  else if (!text.isEmpty() && popupVisible)
  {
    m_completer->popup()->hide();
  }
  else if (
    key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Home
    || key == Qt::Key_End)
  {
    if (popupVisible)
    {
      m_completer->popup()->hide();
    }
  }

  return Console::eventFilter(watched, event);
}

void PythonConsole::updateRunButtonEnabled()
{
  m_runButton->setEnabled(
    m_commandExecutor && !m_input->toPlainText().trimmed().isEmpty());
}

void PythonConsole::updateFont()
{
  const auto font = consoleFont();
  textView()->setFont(font);
  textView()->document()->setDefaultFont(font);
  m_prompt->setFont(font);
  m_input->setFont(font);
  m_input->document()->setDefaultFont(font);
  const auto spaceWidth = QFontMetricsF{font}.horizontalAdvance(' ');
  m_input->setTabStopDistance(spaceWidth * 4.0);
}

void PythonConsole::showPreviousHistoryEntry()
{
  if (m_history.empty() || m_historyIndex == 0u)
  {
    return;
  }
  if (m_historyIndex == m_history.size())
  {
    m_historyDraft = utf8String(m_input->toPlainText());
  }
  --m_historyIndex;
  showHistoryEntry(m_history[m_historyIndex]);
}

void PythonConsole::showNextHistoryEntry()
{
  if (m_historyIndex >= m_history.size())
  {
    return;
  }
  ++m_historyIndex;
  showHistoryEntry(
    m_historyIndex < m_history.size() ? m_history[m_historyIndex] : m_historyDraft);
}

void PythonConsole::showHistoryEntry(const std::string& command)
{
  m_input->setPlainText(QString::fromUtf8(command));
  auto cursor = m_input->textCursor();
  cursor.movePosition(QTextCursor::End);
  m_input->setTextCursor(cursor);
}

void PythonConsole::logToConsole(const LogLevel level, const std::string& message)
{
  contract_pre(isMainThread());

  auto* edit = textView();
  if (edit == nullptr)
  {
    return;
  }

  const auto& colors = edit->palette();
  const auto promptColor = colors.color(QPalette::Active, QPalette::Accent);
  const auto subPromptColor = colors.color(QPalette::Active, QPalette::PlaceholderText);
  const auto resultPrefixColor = themeSemanticColor(colors, SemanticColor::Success);
  const auto normalTextColor = colors.color(QPalette::Active, QPalette::Text);
  const auto resultTextColor = normalTextColor;
  const auto errorColor = themeSemanticColor(colors, SemanticColor::Error);
  const auto warnColor = themeSemanticColor(colors, SemanticColor::Warning);

  auto cursor = QTextCursor{edit->document()};
  cursor.movePosition(QTextCursor::End);

  const auto qMsg = QString::fromStdString(message);
  const auto lines = qMsg.split('\n');

  for (auto i = 0; i < lines.size(); ++i)
  {
    const auto& line = lines[i];

    if (level == LogLevel::Error)
    {
      auto fmt = QTextCharFormat{};
      fmt.setForeground(errorColor);
      cursor.insertText(line, fmt);
      cursor.insertText(QStringLiteral("\n"));
    }
    else if (level == LogLevel::Warn)
    {
      auto fmt = QTextCharFormat{};
      fmt.setForeground(warnColor);
      cursor.insertText(line, fmt);
      cursor.insertText(QStringLiteral("\n"));
    }
    else if (line.startsWith(QStringLiteral(">>> ")))
    {
      if (!cursor.atStart())
      {
        auto prevBlock = cursor.block();
        if (!prevBlock.text().trimmed().isEmpty())
        {
          cursor.insertText(QStringLiteral("\n"));
        }
      }

      auto promptFmt = QTextCharFormat{};
      promptFmt.setForeground(promptColor);
      promptFmt.setFontWeight(QFont::DemiBold);
      cursor.insertText(QStringLiteral(">>> "), promptFmt);

      auto codeFmt = QTextCharFormat{};
      codeFmt.setForeground(normalTextColor);
      cursor.insertText(line.mid(4), codeFmt);
      cursor.insertText(QStringLiteral("\n"));
    }
    else if (line.startsWith(QStringLiteral("... ")))
    {
      auto promptFmt = QTextCharFormat{};
      promptFmt.setForeground(subPromptColor);
      cursor.insertText(QStringLiteral("... "), promptFmt);

      auto codeFmt = QTextCharFormat{};
      codeFmt.setForeground(normalTextColor);
      cursor.insertText(line.mid(4), codeFmt);
      cursor.insertText(QStringLiteral("\n"));
    }
    else if (line.startsWith(QStringLiteral("=> ")))
    {
      auto resPromptFmt = QTextCharFormat{};
      resPromptFmt.setForeground(resultPrefixColor);
      resPromptFmt.setFontWeight(QFont::DemiBold);
      cursor.insertText(QStringLiteral("=> "), resPromptFmt);

      auto resTextFmt = QTextCharFormat{};
      resTextFmt.setForeground(resultTextColor);
      cursor.insertText(line.mid(3), resTextFmt);
      cursor.insertText(QStringLiteral("\n"));
    }
    else
    {
      auto fmt = QTextCharFormat{};
      fmt.setForeground(normalTextColor);
      cursor.insertText(line, fmt);
      cursor.insertText(QStringLiteral("\n"));
    }
  }

  edit->moveCursor(QTextCursor::End);
}

} // namespace tb::ui
