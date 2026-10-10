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
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QStackedWidget>

#include "base/PreferenceManager.h"
#include "ui/ColorsPreferencePane.h"
#include "ui/DialogButtonLayout.h"
#include "ui/GamesPreferencePane.h"
#include "ui/ImageUtils.h"
#include "ui/KeyboardPreferencePane.h"
#include "ui/MiscPreferencePane.h"
#include "ui/KreedzBroomPreferencePane.h"
#include "ui/MousePreferencePane.h"
#include "ui/PreferencePane.h"
#include "ui/QStyleUtils.h"
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
constexpr int PreferenceNavigationWidth = 184;

} // namespace

enum class PreferenceDialog::PrefPane
{
  First = 0,
  Games = 0,
  View = 1,
  Colors = 2,
  Mouse = 3,
  Keyboard = 4,
  Misc = 5,
  KreedzBroom = 6,
  Update = 7,
  Last = 7
} PrefPane;


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
  switchToPane(PrefPane::First);
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

void PreferenceDialog::createGui()
{
  const auto gamesImage = loadSVGIcon("GeneralPreferences.svg");
  const auto viewImage = loadSVGIcon("ViewPreferences.svg");
  const auto colorsImage = loadSVGIcon("ColorPreferences.svg");
  const auto mouseImage = loadSVGIcon("MousePreferences.svg");
  const auto keyboardImage = loadSVGIcon("KeyboardPreferences.svg");
  const auto miscImage = loadSVGIcon("GeneralPreferences.svg");
  const auto kreedzBroomImage = loadSVGIcon("KreedzBroomPreferences.svg");
  const auto updateImage = loadSVGIcon("UpdatePreferences.svg");

  m_navigation = new QListWidget{};
  m_navigation->setObjectName("PreferenceDialog_NavigationList");
  m_navigation->setIconSize(QSize{18, 18});
  m_navigation->setSelectionMode(QAbstractItemView::SingleSelection);
  m_navigation->setUniformItemSizes(true);
  m_navigation->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_navigation->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  new QListWidgetItem{gamesImage, tr("Games"), m_navigation};
  new QListWidgetItem{viewImage, tr("View"), m_navigation};
  new QListWidgetItem{colorsImage, tr("Colors"), m_navigation};
  new QListWidgetItem{mouseImage, tr("Mouse"), m_navigation};
  new QListWidgetItem{keyboardImage, tr("Keyboard"), m_navigation};
  new QListWidgetItem{miscImage, tr("Misc"), m_navigation};
  new QListWidgetItem{kreedzBroomImage, tr("KreedzBroom"), m_navigation};
  new QListWidgetItem{updateImage, tr("Update"), m_navigation};

  auto* navigationTitle = new QLabel{tr("SETTINGS")};
  navigationTitle->setObjectName("PreferenceDialog_NavigationTitle");
  navigationTitle->setProperty("regionAnchor", true);

  auto* navigationLayout = new QVBoxLayout{};
  navigationLayout->setContentsMargins(8, 12, 8, 8);
  navigationLayout->setSpacing(6);
  navigationLayout->addWidget(navigationTitle);
  navigationLayout->addWidget(m_navigation, 1);

  auto* navigation = new QWidget{};
  navigation->setObjectName("PreferenceDialog_Navigation");
  navigation->setAttribute(Qt::WA_StyledBackground);
  navigation->setFixedWidth(PreferenceNavigationWidth);
  navigation->setLayout(navigationLayout);

  m_stackedWidget = new QStackedWidget{};
  m_stackedWidget->setObjectName("PreferenceDialog_Pages");
  m_stackedWidget->addWidget(new GamesPreferencePane{m_appController, m_document});
  m_stackedWidget->addWidget(new ViewPreferencePane{});
  m_stackedWidget->addWidget(new ColorsPreferencePane{});
  m_stackedWidget->addWidget(new MousePreferencePane{});
  m_stackedWidget->addWidget(new KeyboardPreferencePane{m_appController, m_document});
  m_stackedWidget->addWidget(new MiscPreferencePane{m_appController});
  m_stackedWidget->addWidget(new KreedzBroomPreferencePane{});
  m_stackedWidget->addWidget(new UpdatePreferencePane{m_appController});

  m_buttonBox = new QDialogButtonBox{
    PreferenceManager::instance().saveInstantly()
      ? QDialogButtonBox::RestoreDefaults
      : QDialogButtonBox::RestoreDefaults | QDialogButtonBox::Ok | QDialogButtonBox::Apply
          | QDialogButtonBox::Cancel,
    this};
  m_buttonBox->setObjectName("PreferenceDialog_ButtonBox");

  auto* resetButton = m_buttonBox->button(QDialogButtonBox::RestoreDefaults);
  resetButton->setText(tr("Restore Defaults"));
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

  auto* contentLayout = new QVBoxLayout{};
  contentLayout->setContentsMargins(0, 0, 0, 0);
  contentLayout->setSpacing(0);
  contentLayout->addWidget(m_pageTitle);
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
    if (row >= int(PrefPane::First) && row <= int(PrefPane::Last))
    {
      switchToPane(static_cast<PrefPane>(row));
    }
  });
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

  auto* resetButton = m_buttonBox->button(QDialogButtonBox::RestoreDefaults);
  resetButton->setEnabled(currentPane()->canResetToDefaults());
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
