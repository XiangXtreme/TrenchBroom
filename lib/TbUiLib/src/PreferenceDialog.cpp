/*
 Copyright (C) 2010 Kristian Duske

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

#include "ui/PreferenceDialog.h"

#include <QBoxLayout>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStackedWidget>

#include "base/PreferenceManager.h"
#include "ui/ColorsPreferencePane.h"
#include "ui/DialogButtonLayout.h"
#include "ui/ExtensionsPreferencePane.h"
#include "ui/GamesPreferencePane.h"
#include "ui/GeneralPreferencePane.h"
#include "ui/ImageUtils.h"
#include "ui/KeyboardPreferencePane.h"
#include "ui/KreedzBroomPreferencePane.h"
#include "ui/MousePreferencePane.h"
#include "ui/PreferencePane.h"
#include "ui/QStyleUtils.h"
#include "ui/SearchBox.h"
#include "ui/UpdatePreferencePane.h"
#include "ui/ViewPreferencePane.h"

#include <algorithm>
#include <filesystem>

namespace tb::ui
{
namespace
{

constexpr int PreferenceDialogMinWidth = 920;
constexpr int PreferenceDialogMinHeight = 560;
constexpr int PreferenceNavigationWidth = 232;

} // namespace

enum class PreferenceDialog::PrefPane
{
  General = 0,
  Games,
  View,
  Colors,
  Mouse,
  Keyboard,
  KreedzBroom,
  Extensions,
  Update
};

PreferenceDialog::PreferenceDialog(
  AppController& appController, MapDocument* document, QWidget* parent)
  : QDialog{parent}
  , m_appController{appController}
  , m_document{document}
{
  setObjectName("PreferenceDialog_Dialog");
  setWindowTitle(tr("Preferences"));
  setWindowIconTB(this);
  createGui();
  setMinimumSize(PreferenceDialogMinWidth, PreferenceDialogMinHeight);
  switchToPane(PrefPane::General);
  currentPane()->updateControls();

  const auto preferredSize = initialDialogSize();
  if (const auto* currentScreen = screen())
  {
    const auto availableSize = currentScreen->availableGeometry().size();
    setMaximumSize(availableSize);
    resize(preferredSize.boundedTo(availableSize));
  }

  connectObservers();
}

void PreferenceDialog::closeEvent(QCloseEvent* event)
{
  if (currentPane()->validate())
  {
    auto& prefs = PreferenceManager::instance();
    if (prefs.hasUnsavedChanges())
    {
      auto msgBox = QMessageBox{
        QMessageBox::Question,
        tr("Unsaved Preference Changes"),
        tr(
          "You have unsaved preference changes. Would you like to save or discard them?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        this};
      msgBox.button(QMessageBox::Save)->setText(tr("Save"));
      msgBox.button(QMessageBox::Discard)->setText(tr("Discard"));
      msgBox.button(QMessageBox::Cancel)->setText(tr("Cancel"));

      switch (msgBox.exec())
      {
      case QMessageBox::Save:
        prefs.saveChanges();
        event->accept();
        break;
      case QMessageBox::Discard:
        prefs.discardChanges();
        event->accept();
        break;
      default:
        event->ignore();
        break;
      }
    }
    else
    {
      event->accept();
    }
  }
  else
  {
    event->ignore();
  }
}

bool PreferenceDialog::eventFilter(QObject* watched, QEvent* event)
{
  if (watched == m_search && event->type() == QEvent::KeyPress)
  {
    const auto key = static_cast<QKeyEvent*>(event)->key();
    if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Down)
    {
      m_navigation->setFocus();
      return true;
    }
    if (key == Qt::Key_Escape && !m_search->text().isEmpty())
    {
      m_search->clear();
      return true;
    }
  }
  return QDialog::eventFilter(watched, event);
}

void PreferenceDialog::createGui()
{
  m_navigation = new QListWidget{};
  m_navigation->setObjectName("PreferenceDialog_NavigationList");
  m_navigation->setProperty("tbControlRole", "navigation");
  m_navigation->setIconSize(QSize{16, 16});
  m_navigation->setSpacing(2);
  m_navigation->setSelectionMode(QAbstractItemView::SingleSelection);
  m_navigation->setUniformItemSizes(true);
  m_navigation->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_navigation->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

  auto* navigationTitle = new QLabel{tr("Settings")};
  navigationTitle->setObjectName("PreferenceDialog_NavigationTitle");
  navigationTitle->setProperty("regionAnchor", true);

  auto* navigationLayout = new QVBoxLayout{};
  navigationLayout->setContentsMargins(8, 12, 8, 8);
  navigationLayout->setSpacing(8);
  navigationLayout->addWidget(navigationTitle);
  navigationLayout->addWidget(m_navigation, 1);

  auto* navigation = new QWidget{};
  navigation->setObjectName("PreferenceDialog_Navigation");
  navigation->setAttribute(Qt::WA_StyledBackground);
  navigation->setFixedWidth(PreferenceNavigationWidth);
  navigation->setLayout(navigationLayout);

  m_stackedWidget = new QStackedWidget{};
  m_stackedWidget->setObjectName("PreferenceDialog_Pages");
  const auto addPane =
    [&](PreferencePane* pane, const QString& id, const QString& title, const char* icon) {
      m_stackedWidget->addWidget(pane);
      auto* item = new QListWidgetItem{loadSVGIcon(icon), title, m_navigation};
      item->setData(Qt::UserRole, id);
      item->setToolTip(title);
    };
  addPane(
    new GeneralPreferencePane{}, "general", tr("General"), "GeneralPreferences.svg");
  addPane(
    new GamesPreferencePane{m_appController, m_document},
    "games",
    tr("Games"),
    "GeneralPreferences.svg");
  addPane(
    new ViewPreferencePane{}, "view", tr("Appearance and views"), "ViewPreferences.svg");
  addPane(new ColorsPreferencePane{}, "colors", tr("Colors"), "ColorPreferences.svg");
  addPane(new MousePreferencePane{}, "mouse", tr("Mouse"), "MousePreferences.svg");
  addPane(
    new KeyboardPreferencePane{m_appController, m_document},
    "keyboard",
    tr("Keyboard shortcuts"),
    "KeyboardPreferences.svg");
  addPane(
    new KreedzBroomPreferencePane{},
    "play",
    tr("KreedzBroom"),
    "KreedzBroomPreferences.svg");
  addPane(
    new ExtensionsPreferencePane{m_appController},
    "extensions",
    tr("Extensions and automation"),
    "GeneralPreferences.svg");
  addPane(
    new UpdatePreferencePane{m_appController},
    "updates",
    tr("Updates"),
    "UpdatePreferences.svg");

  m_buttonBox = new QDialogButtonBox{
    PreferenceManager::instance().saveInstantly()
      ? QDialogButtonBox::RestoreDefaults
      : QDialogButtonBox::RestoreDefaults | QDialogButtonBox::Ok | QDialogButtonBox::Apply
          | QDialogButtonBox::Cancel,
    this};
  m_buttonBox->setObjectName("PreferenceDialog_ButtonBox");

  auto* resetButton = m_buttonBox->button(QDialogButtonBox::RestoreDefaults);
  resetButton->setText(tr("Restore Page Defaults"));
  resetButton->setToolTip(
    tr("Restore defaults for this category, including all its tabs."));
  connect(resetButton, &QPushButton::clicked, this, &PreferenceDialog::resetToDefaults);

  if (!PreferenceManager::instance().saveInstantly())
  {
    m_buttonBox->button(QDialogButtonBox::Ok)->setText(tr("OK"));
    m_buttonBox->button(QDialogButtonBox::Apply)->setText(tr("Apply"));
    m_buttonBox->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));

    connect(
      m_buttonBox->button(QDialogButtonBox::Ok), &QPushButton::clicked, this, [&]() {
        auto& prefs = PreferenceManager::instance();
        prefs.saveChanges();
        this->close();
      });
    connect(
      m_buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [&]() {
        auto& prefs = PreferenceManager::instance();
        prefs.saveChanges();
      });
    connect(
      m_buttonBox->button(QDialogButtonBox::Cancel), &QPushButton::clicked, this, [&]() {
        auto& prefs = PreferenceManager::instance();
        prefs.discardChanges();
        this->close();
      });
  }

  m_pageTitle = new QLabel{};
  m_pageTitle->setObjectName("PreferenceDialog_PageTitle");

  m_search = createSearchBox();
  m_search->setObjectName("PreferenceDialog_Search");
  m_search->setProperty("tbControlRole", "prominent");
  m_search->setPlaceholderText(tr("Search all settings"));
  m_search->setAccessibleName(tr("Search all settings"));
  auto* searchLayout = new QHBoxLayout{};
  searchLayout->setContentsMargins(16, 8, 16, 8);
  searchLayout->addWidget(m_search);

  m_noResults = new QLabel{tr("No matching settings. Try another search.")};
  m_noResults->setObjectName("PreferenceDialog_NoResults");
  m_noResults->setAlignment(Qt::AlignCenter);
  m_noResults->setWordWrap(true);
  setInfoStyle(m_noResults);
  m_noResults->hide();

  auto* contentLayout = new QVBoxLayout{};
  contentLayout->setContentsMargins(0, 0, 0, 0);
  contentLayout->setSpacing(0);
  contentLayout->addWidget(m_pageTitle);
  contentLayout->addLayout(searchLayout);
  contentLayout->addWidget(m_noResults, 1);
  contentLayout->addWidget(m_stackedWidget, 1);
  contentLayout->addLayout(wrapDialogButtonBox(m_buttonBox));

  auto* content = new QWidget{};
  content->setObjectName("PreferenceDialog_Content");
  content->setAttribute(Qt::WA_StyledBackground);
  content->setLayout(contentLayout);

  auto* layout = new QHBoxLayout{};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(navigation);
  layout->addWidget(content, 1);
  setLayout(layout);

  connect(m_navigation, &QListWidget::currentRowChanged, this, [this](const int row) {
    if (row >= 0 && row < m_stackedWidget->count())
    {
      switchToPane(static_cast<PrefPane>(row));
    }
  });
  connect(m_search, &QLineEdit::textChanged, this, &PreferenceDialog::filterPanes);
  auto* findShortcut = new QShortcut{QKeySequence::Find, this};
  connect(findShortcut, &QShortcut::activated, this, [this]() {
    m_search->setFocus();
    m_search->selectAll();
  });
  m_search->installEventFilter(this);
}

QSize PreferenceDialog::initialDialogSize() const
{
  const auto numPanes = m_stackedWidget->count();
  contract_assert(numPanes > 0);

  const auto paneHeights =
    std::views::iota(0, numPanes) | std::views::transform([&](const auto i) {
      const auto* pane = static_cast<PreferencePane*>(m_stackedWidget->widget(i));
      return pane->contentSizeHint().height();
    });

  const auto maxPaneHeight = *std::ranges::max_element(paneHeights);
  const auto frameHeight = sizeHint().height() - m_stackedWidget->sizeHint().height();
  const auto initialWidth = std::max(sizeHint().width(), PreferenceDialogMinWidth);
  const auto initialHeight = frameHeight + maxPaneHeight;
  return {initialWidth, initialHeight};
}

void PreferenceDialog::switchToPane(const PrefPane pane)
{
  const auto paneIndex = int(pane);
  if (!currentPane()->validate())
  {
    const auto blocker = QSignalBlocker{m_navigation};
    m_navigation->setCurrentRow(m_stackedWidget->currentIndex());
    return;
  }

  m_stackedWidget->setCurrentIndex(paneIndex);
  currentPane()->updateControls();

  const auto blocker = QSignalBlocker{m_navigation};
  m_navigation->setCurrentRow(paneIndex);
  m_pageTitle->setText(m_navigation->item(paneIndex)->text());
  currentPane()->revealSearch(m_search->text());

  auto* resetButton = m_buttonBox->button(QDialogButtonBox::RestoreDefaults);
  resetButton->setEnabled(currentPane()->canResetToDefaults());
}

void PreferenceDialog::filterPanes(const QString& query)
{
  const auto words = query.simplified().split(' ', Qt::SkipEmptyParts);
  auto matches = std::vector<bool>{};
  auto firstMatch = -1;
  for (int i = 0; i < m_navigation->count(); ++i)
  {
    const auto title = m_navigation->item(i)->text();
    const auto titleMatches = std::ranges::all_of(
      words, [&](const auto& word) { return title.contains(word, Qt::CaseInsensitive); });
    const auto* pane = static_cast<PreferencePane*>(m_stackedWidget->widget(i));
    const auto match = titleMatches || pane->matchesSearch(query);
    matches.push_back(match);
    if (match && firstMatch < 0)
      firstMatch = i;
  }

  const auto current = m_stackedWidget->currentIndex();
  if (firstMatch >= 0 && !matches[static_cast<size_t>(current)])
    switchToPane(static_cast<PrefPane>(firstMatch));

  // If validation prevented leaving a page, keep it available for correction.
  const auto stayedOnInvalidPage =
    firstMatch >= 0 && !matches[static_cast<size_t>(m_stackedWidget->currentIndex())];
  for (int i = 0; i < m_navigation->count(); ++i)
    m_navigation->item(i)->setHidden(
      !matches[static_cast<size_t>(i)] && !(stayedOnInvalidPage && i == current));

  m_noResults->setVisible(firstMatch < 0);
  m_stackedWidget->setVisible(firstMatch >= 0);
  m_pageTitle->setText(
    firstMatch < 0 ? tr("Search all settings") : m_navigation->currentItem()->text());
  m_buttonBox->button(QDialogButtonBox::RestoreDefaults)
    ->setEnabled(firstMatch >= 0 && currentPane()->canResetToDefaults());
  if (firstMatch >= 0)
    currentPane()->revealSearch(query);
}

PreferencePane* PreferenceDialog::currentPane() const
{
  return static_cast<PreferencePane*>(m_stackedWidget->currentWidget());
}

void PreferenceDialog::connectObservers()
{
  auto& prefs = PreferenceManager::instance();
  m_notifierConnection += prefs.preferenceDidChangeNotifier.connect(
    [this](const auto&) { currentPane()->updateControls(); });
}

void PreferenceDialog::resetToDefaults()
{
  currentPane()->resetToDefaults();
}

} // namespace tb::ui
