/*
 Copyright (C) 2010 Kristian Duske
 Copyright (C) 2026 XiangXtreme

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

#include "ui/Inspector.h"

#include <QByteArray>
#include <QDataStream>
#include <QHBoxLayout>
#include <QIODevice>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

#include "ui/ActionBuilder.h"
#include "ui/ActionManager.h"
#include "ui/AppController.h"
#include "ui/FaceInspector.h"
#include "ui/ImageUtils.h"
#include "ui/MapSettingsDialog.h"
#include "ui/MapViewBar.h"
#include "ui/PluginInspector.h"
#include "ui/SyncHeightEventFilter.h"
#include "ui/TabBar.h"
#include "ui/TabBook.h"
#include "ui/TerrainInspector.h"
#include "ui/outliner/OutlinerInspector.h"

#include <algorithm>
#include <array>

namespace tb::ui
{
namespace
{
// InspectorPage values also identify legacy commands. Keep their identities independent
// of the order shown in the rail and stacked pages.
constexpr auto PageOrder = std::array{
  InspectorPage::Outliner,
  InspectorPage::Face,
  InspectorPage::Plugin,
  InspectorPage::Terrain};
int pageIndex(const InspectorPage page)
{
  return static_cast<int>(std::ranges::find(PageOrder, page) - PageOrder.begin());
}
} // namespace

Inspector::Inspector(
  AppController& appController,
  MapDocument& document,
  MapViewToolBox& toolBox,
  QWidget* parent)
  : QWidget{parent}
  , m_appController{&appController}
  , m_document{&document}
{
  m_tabBook = new TabBook{};
  m_tabBook->setObjectName("Inspector_TabBook");

  // Keep the terrain parameter/operation provider alive for the tool session.
  // Other pages are prepared lazily.
  m_terrainInspector = new TerrainInspector{appController, document, toolBox};

  m_tabBook->addPlaceholderPage("Outliner");
  m_tabBook->addPlaceholderPage("Face");
  m_tabBook->addPlaceholderPage("Plugin");
  m_tabBook->addPage(m_terrainInspector, "Terrain");

  auto* legacyTabBar = m_tabBook->tabBar();
  legacyTabBar->setObjectName("Inspector_LegacyTabBar");
  legacyTabBar->setNavigationVisible(false);

  m_pageTitle = new QLabel{};
  m_pageTitle->setObjectName("Inspector_PageTitle");
  m_pageTitle->setProperty("regionAnchor", true);
  m_pageTitle->setFixedHeight(34);

  auto* navigationRail = new QWidget{};
  navigationRail->setObjectName("Inspector_NavigationRail");
  navigationRail->setAttribute(Qt::WA_StyledBackground);
  navigationRail->setFixedWidth(40);

  auto* navigationLayout = new QVBoxLayout{};
  navigationLayout->setContentsMargins(4, 6, 4, 6);
  navigationLayout->setSpacing(4);

  const auto addNavigationButton = [this, navigationRail, navigationLayout](
                                     const InspectorPage page,
                                     const QString& title,
                                     const QString& tooltip,
                                     const char* objectName,
                                     const char* iconPath) {
    auto* button = new QToolButton{navigationRail};
    button->setObjectName(QString::fromLatin1(objectName));
    button->setProperty("tbControlRole", "navigation");
    button->setToolTip(tooltip);
    button->setAccessibleName(title);
    button->setCheckable(true);
    button->setAutoExclusive(true);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::StrongFocus);
    button->setIcon(loadSVGIcon(iconPath));
    button->setIconSize(QSize{16, 16});
    button->setFixedSize(QSize{32, 32});

    connect(button, &QToolButton::clicked, this, [this, page]() { switchToPage(page); });

    m_navigationButtons.push_back(button);
    navigationLayout->addWidget(button);
  };

  addNavigationButton(
    InspectorPage::Outliner,
    tr("Scene"),
    tr("Scene tree and properties"),
    "Inspector_NavigationOutliner",
    "InspectorOutliner.svg");
  addNavigationButton(
    InspectorPage::Face,
    tr("Face Inspector"),
    tr("Face Inspector"),
    "Inspector_NavigationFace",
    "InspectorFace.svg");
  addNavigationButton(
    InspectorPage::Plugin,
    tr("Plugins"),
    tr("Plugins"),
    "Inspector_NavigationPlugin",
    "InspectorPlugin.svg");
  addNavigationButton(
    InspectorPage::Terrain,
    tr("Terrain"),
    tr("Terrain"),
    "Inspector_NavigationTerrain",
    "InspectorTerrain.svg");
  navigationLayout->addStretch(1);
  navigationRail->setLayout(navigationLayout);

  auto* pageContainer = new QWidget{};
  pageContainer->setObjectName("Inspector_PageContainer");
  pageContainer->setAttribute(Qt::WA_StyledBackground);

  auto* pageLayout = new QHBoxLayout{};
  pageLayout->setContentsMargins(0, 0, 0, 0);
  pageLayout->setSpacing(0);
  pageLayout->addWidget(navigationRail);
  auto* inspectorContent = new QVBoxLayout{};
  inspectorContent->setContentsMargins(0, 0, 0, 0);
  inspectorContent->setSpacing(0);
  inspectorContent->addWidget(m_pageTitle);
  inspectorContent->addWidget(m_tabBook, 1);
  pageLayout->addLayout(inspectorContent, 1);
  pageContainer->setLayout(pageLayout);

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(pageContainer, 1);
  setLayout(layout);

  connect(m_tabBook, &TabBook::pageChanged, this, &Inspector::updateNavigation);
  updateNavigation(m_tabBook->currentPageIndex());
  updateShortcuts();
}

Inspector::~Inspector() = default;

void Inspector::connectTopWidgets(MapViewBar* mapViewBar)
{
  if (m_syncHeaderEventFilter)
  {
    delete std::exchange(m_syncHeaderEventFilter, nullptr);
  }

  m_syncHeaderEventFilter = new SyncHeightEventFilter{mapViewBar, m_pageTitle, this};
}

void Inspector::ensurePage(const InspectorPage page)
{
  const auto index = pageIndex(page);
  switch (page)
  {
  case InspectorPage::Face:
    if (!m_faceInspector)
    {
      m_faceInspector = new FaceInspector{*m_appController, *m_document};
      m_tabBook->replacePage(index, m_faceInspector);
      emit pagePrepared(static_cast<int>(page));
    }
    break;
  case InspectorPage::Outliner:
    if (!m_outlinerInspector)
    {
      m_outlinerInspector = new OutlinerInspector{*m_document};
      m_tabBook->replacePage(index, m_outlinerInspector);
      connect(
        m_outlinerInspector,
        &OutlinerInspector::mapSettingsRequested,
        this,
        &Inspector::showMapSettings);
      emit pagePrepared(static_cast<int>(page));
    }
    break;
  case InspectorPage::Plugin:
    if (!m_pluginInspector)
    {
      m_pluginInspector = new PluginInspector{};
      m_tabBook->replacePage(index, m_pluginInspector);
      emit pagePrepared(static_cast<int>(page));
    }
    break;
  case InspectorPage::Entity:
  case InspectorPage::Map:
  case InspectorPage::Terrain:
    break;
  }
  updateShortcuts();
}

void Inspector::switchToPage(const InspectorPage page)
{
  const auto destination = page == InspectorPage::Entity || page == InspectorPage::Map
                             ? InspectorPage::Outliner
                             : page;
  ensurePage(destination);
  if (page == InspectorPage::Entity)
    m_outlinerInspector->showProperties();
  m_tabBook->switchToPage(pageIndex(destination));
}

InspectorPage Inspector::currentPage() const
{
  return PageOrder.at(static_cast<size_t>(m_tabBook->currentPageIndex()));
}

void Inspector::updateNavigation(const int page)
{
  if (page < 0 || page >= static_cast<int>(m_navigationButtons.size()))
  {
    return;
  }

  const auto titles = std::array{tr("Scene"), tr("Face"), tr("Plugins"), tr("Terrain")};
  m_pageTitle->setText(titles.at(static_cast<size_t>(page)).toUpper());
  m_navigationButtons.at(static_cast<size_t>(page))->setChecked(true);
}

void Inspector::updateShortcuts()
{
  const auto& actions = m_appController->actionManager().actionsMap();
  constexpr auto paths = std::array{
    "Menu/View/Switch to Entity Inspector",
    "Menu/View/Switch to Face Inspector",
    "Menu/View/Switch to Plugin Inspector",
    "Menu/View/Switch to Terrain Inspector"};
  for (auto i = size_t{0}; i < paths.size(); ++i)
    m_navigationButtons[i]->setToolTip(actionToolTip(actions.at(paths[i])));
  if (m_outlinerInspector)
    if (
      auto* button =
        m_outlinerInspector->findChild<QToolButton*>("OutlinerInspector_MapSettings"))
      button->setToolTip(actionToolTip(actions.at("Menu/View/Switch to Map Inspector")));
}

void Inspector::showMapSettings()
{
  if (m_mapSettingsDialog && !m_mapSettingsDialog->isVisible())
    delete m_mapSettingsDialog.data();
  if (!m_mapSettingsDialog)
  {
    m_mapSettingsDialog = new MapSettingsDialog{*m_document, this};
    m_mapSettingsDialog->setAttribute(Qt::WA_DeleteOnClose);
  }
  m_mapSettingsDialog->show();
  m_mapSettingsDialog->raise();
  m_mapSettingsDialog->activateWindow();
}

void Inspector::closeMapSettings()
{
  if (m_mapSettingsDialog)
    m_mapSettingsDialog->close();
}

bool Inspector::cancelMouseDrag()
{
  // Don't build a page just to answer an Escape key press.
  return m_faceInspector && m_faceInspector->cancelMouseDrag();
}

FaceInspector* Inspector::faceInspector()
{
  ensurePage(InspectorPage::Face);
  return m_faceInspector;
}

PluginInspector* Inspector::pluginInspector()
{
  ensurePage(InspectorPage::Plugin);
  return m_pluginInspector;
}

QByteArray Inspector::saveState() const
{
  auto result = QByteArray{};
  auto stream = QDataStream{&result, QIODevice::WriteOnly};
  stream << !isHidden();
  return result;
}

bool Inspector::restoreState(const QByteArray& state)
{
  auto stream = QDataStream{state};
  bool visible;
  stream >> visible;

  if (stream.status() == QDataStream::Ok)
  {
    setVisible(visible);
    return true;
  }

  return false;
}

} // namespace tb::ui
