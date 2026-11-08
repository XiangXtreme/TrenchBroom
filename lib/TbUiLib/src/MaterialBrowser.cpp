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

#include "ui/MaterialBrowser.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtGlobal>

#include "base/PreferenceManager.h"
#include "gl/Material.h"
#include "gl/MaterialManager.h"
#include "mdl/GameInfo.h"
#include "mdl/Map.h"
#include "prefs/Preferences.h"
#include "ui/MapDocument.h"
#include "ui/MaterialBrowserIconSize.h"
#include "ui/MaterialBrowserView.h"
#include "ui/SearchBox.h"
#include "ui/ViewConstants.h"

#include <optional>

// for use in QVariant
Q_DECLARE_METATYPE(tb::ui::MaterialSortOrder)

namespace tb::ui
{
namespace
{

std::optional<float> iconSizeForComboIndex(QComboBox* combo, const int index)
{
  if (!combo || index < 0)
  {
    return std::nullopt;
  }

  auto ok = false;
  const auto value = combo->itemData(index).toFloat(&ok);
  return ok ? std::optional<float>{value} : std::nullopt;
}

} // namespace

MaterialBrowser::MaterialBrowser(
  AppController& appController, MapDocument& document, QWidget* parent)
  : QWidget{parent}
  , m_document{document}
{
  createGui(appController);
  bindEvents();
  connectObservers();
  reload();
}

const gl::Material* MaterialBrowser::selectedMaterial() const
{
  return m_view->selectedMaterial();
}

void MaterialBrowser::setSelectedMaterial(const gl::Material* selectedMaterial)
{
  m_view->setSelectedMaterial(selectedMaterial);
}

void MaterialBrowser::revealMaterial(const gl::Material* material)
{
  setFilterText("");
  m_view->revealMaterial(material);
}

void MaterialBrowser::setSortOrder(const MaterialSortOrder sortOrder)
{
  m_view->setSortOrder(sortOrder);
  switch (sortOrder)
  {
  case MaterialSortOrder::Name:
    m_sortOrderChoice->setCurrentIndex(0);
    break;
  case MaterialSortOrder::Usage:
    m_sortOrderChoice->setCurrentIndex(1);
    break;
    switchDefault();
  }
}

void MaterialBrowser::setGroup(const bool group)
{
  m_view->setGroup(group);
  m_groupButton->setChecked(group);
}

void MaterialBrowser::setHideUnused(const bool hideUnused)
{
  m_view->setHideUnused(hideUnused);
  m_usedButton->setChecked(hideUnused);
}

void MaterialBrowser::setFilterText(const std::string& filterText)
{
  m_view->setFilterText(filterText);
  m_filterBox->setText(QString::fromStdString(filterText));
}

/**
 * See EntityBrowser::createGui
 */
void MaterialBrowser::createGui(AppController& appController)
{
  setObjectName(QStringLiteral("MaterialBrowser"));
  auto* browserPanel = new QWidget{};
  m_scrollBar = new QScrollBar{Qt::Vertical};

  m_view = new MaterialBrowserView{appController, m_scrollBar, m_document};

  auto* browserPanelSizer = new QHBoxLayout{};
  browserPanelSizer->setContentsMargins(0, 0, 0, 0);
  browserPanelSizer->setSpacing(0);
  browserPanelSizer->addWidget(m_view, 1);
  browserPanelSizer->addWidget(m_scrollBar, 0);
  browserPanel->setLayout(browserPanelSizer);

  m_sortOrderChoice = new QComboBox{};
  m_sortOrderChoice->addItem(tr("Name"), QVariant::fromValue(MaterialSortOrder::Name));
  m_sortOrderChoice->addItem(tr("Usage"), QVariant::fromValue(MaterialSortOrder::Usage));
  m_sortOrderChoice->setCurrentIndex(0);
  m_sortOrderChoice->setObjectName(QStringLiteral("MaterialBrowser_Sort"));
  m_sortOrderChoice->setToolTip(tr("Select ordering criterion"));
  connect(
    m_sortOrderChoice, QOverload<int>::of(&QComboBox::activated), this, [&](int index) {
      auto sortOrder =
        static_cast<MaterialSortOrder>(m_sortOrderChoice->itemData(index).toInt());
      m_view->setSortOrder(sortOrder);
    });

  m_iconSizeChoice = new QComboBox{};
  m_iconSizeChoice->setObjectName(QStringLiteral("MaterialBrowser_IconSize"));
  m_iconSizeChoice->setMinimumWidth(58);
  m_iconSizeChoice->setMaximumWidth(66);
  m_iconSizeChoice->setToolTip(tr("Material thumbnail size"));
  m_iconSizeChoice->setAccessibleName(tr("Material thumbnail size"));
  for (const auto size : MaterialBrowserIconSizes)
  {
    m_iconSizeChoice->addItem(QStringLiteral("%1%").arg(qRound(size * 100.0f)), size);
  }
  updateIconSizeChoice();
  connect(
    m_iconSizeChoice,
    QOverload<int>::of(&QComboBox::activated),
    this,
    [&](const int index) {
      if (const auto size = iconSizeForComboIndex(m_iconSizeChoice, index))
      {
        setPref(Preferences::MaterialBrowserIconSize, *size);
      }
    });

  m_groupButton = new QToolButton{};
  m_groupButton->setText(tr("Group"));
  m_groupButton->setObjectName(QStringLiteral("MaterialBrowser_GroupToggle"));
  m_groupButton->setProperty("browserFilterToggle", true);
  m_groupButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
  m_groupButton->setToolTip(tr("Group materials by material collection"));
  m_groupButton->setCheckable(true);
  connect(m_groupButton, &QAbstractButton::clicked, this, [&]() {
    m_view->setGroup(m_groupButton->isChecked());
  });

  m_usedButton = new QToolButton{};
  m_usedButton->setText(tr("Used"));
  m_usedButton->setObjectName(QStringLiteral("MaterialBrowser_UsedToggle"));
  m_usedButton->setProperty("browserFilterToggle", true);
  m_usedButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
  m_usedButton->setToolTip(tr("Only show materials currently in use"));
  m_usedButton->setCheckable(true);
  connect(m_usedButton, &QAbstractButton::clicked, this, [&]() {
    m_view->setHideUnused(m_usedButton->isChecked());
  });

  m_filterBox = createSearchBox();
  m_filterBox->setObjectName(QStringLiteral("MaterialBrowser_Search"));
  connect(m_filterBox, &QLineEdit::textEdited, this, [&]() {
    m_view->setFilterText(m_filterBox->text().toStdString());
  });

  auto* filterRowLayout = new QHBoxLayout{};
  filterRowLayout->setContentsMargins(0, 0, 0, 0);
  filterRowLayout->setSpacing(LayoutConstants::NarrowHMargin);
  filterRowLayout->addWidget(m_sortOrderChoice, 1);
  filterRowLayout->addWidget(m_groupButton, 0);
  filterRowLayout->addWidget(m_usedButton, 0);
  filterRowLayout->addWidget(m_iconSizeChoice, 0);

  auto* filterRow = new QWidget{};
  filterRow->setObjectName(QStringLiteral("MaterialBrowser_FilterRow"));
  filterRow->setLayout(filterRowLayout);

  auto* controlLayout = new QVBoxLayout{};
  controlLayout->setContentsMargins(
    LayoutConstants::NarrowHMargin,
    LayoutConstants::NarrowVMargin,
    LayoutConstants::NarrowHMargin,
    LayoutConstants::NarrowVMargin);
  controlLayout->setSpacing(LayoutConstants::NarrowVMargin);
  controlLayout->addWidget(m_filterBox, 0);
  controlLayout->addWidget(filterRow, 0);

  auto* controls = new QWidget{};
  controls->setObjectName(QStringLiteral("MaterialBrowser_Controls"));
  controls->setAttribute(Qt::WA_StyledBackground);
  controls->setLayout(controlLayout);

  auto* outerLayout = new QVBoxLayout{};
  outerLayout->setContentsMargins(0, 0, 0, 0);
  outerLayout->setSpacing(0);
  outerLayout->addWidget(controls, 0);
  outerLayout->addWidget(browserPanel, 1);

  setLayout(outerLayout);
}

void MaterialBrowser::bindEvents()
{
  connect(
    m_view,
    &MaterialBrowserView::materialSelected,
    this,
    &MaterialBrowser::materialSelected);
}

void MaterialBrowser::connectObservers()
{
  m_notifierConnection += m_document.documentWasLoadedNotifier.connect([&] { reload(); });
  m_notifierConnection += m_document.documentDidChangeNotifier.connect([&] { reload(); });
  m_notifierConnection +=
    m_document.materialCollectionsDidChangeNotifier.connect([&] { reload(); });
  m_notifierConnection += m_document.currentMaterialNameDidChangeNotifier.connect(
    [&] { updateSelectedMaterial(); });

  auto& prefs = PreferenceManager::instance();
  m_notifierConnection +=
    prefs.preferenceDidChangeNotifier.connect([&](const auto& path) {
      if (
        path == pref(m_document.map().gameInfo().gamePathPreference)
        || path == Preferences::MaterialBrowserIconSize.path)
      {
        if (path == Preferences::MaterialBrowserIconSize.path)
        {
          updateIconSizeChoice();
        }
        reload();
      }
      else
      {
        m_view->update();
      }
    });
}

void MaterialBrowser::reload()
{
  if (m_view)
  {
    updateSelectedMaterial();
    m_view->invalidate();
    m_view->update();
  }
}

void MaterialBrowser::updateIconSizeChoice()
{
  if (!m_iconSizeChoice)
  {
    return;
  }

  const auto currentSize = pref(Preferences::MaterialBrowserIconSize);
  auto currentIndex = 0;
  for (auto i = 0; i < m_iconSizeChoice->count(); ++i)
  {
    if (iconSizeForComboIndex(m_iconSizeChoice, i) == currentSize)
    {
      currentIndex = i;
      break;
    }
  }

  const auto blocker = QSignalBlocker{m_iconSizeChoice};
  m_iconSizeChoice->setCurrentIndex(currentIndex);
}

void MaterialBrowser::updateSelectedMaterial()
{
  auto& map = m_document.map();

  const auto& materialName = map.currentMaterialName();
  const auto* material = map.materialManager().material(materialName);
  m_view->setSelectedMaterial(material);
}

} // namespace tb::ui
