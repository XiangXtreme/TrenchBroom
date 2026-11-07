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

#include "ui/EntityInspector.h"
#include "ui/FaceInspector.h"
#include "ui/ImageUtils.h"
#include "ui/MapInspector.h"
#include "ui/MapViewBar.h"
#include "ui/PluginInspector.h"
#include "ui/SyncHeightEventFilter.h"
#include "ui/TabBar.h"
#include "ui/TabBook.h"
#include "ui/TerrainInspector.h"
#include "ui/outliner/OutlinerInspector.h"

#include <array>

namespace tb::ui
{
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
  m_mapInspector = new MapInspector{document};
  m_terrainInspector = new TerrainInspector{appController, document, toolBox};

  m_tabBook->addPage(m_mapInspector, "Map");
  m_tabBook->addPlaceholderPage("Entity");
  m_tabBook->addPlaceholderPage("Face");
  m_tabBook->addPlaceholderPage("Outliner");
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
    InspectorPage::Map,
    tr("Map Inspector"),
    tr("Map Inspector (Ctrl+1)"),
    "Inspector_NavigationMap",
    "InspectorMap.svg");
  addNavigationButton(
    InspectorPage::Entity,
    tr("Entity Inspector"),
    tr("Entity Inspector (Ctrl+2)"),
    "Inspector_NavigationEntity",
    "InspectorEntity.svg");
  addNavigationButton(
    InspectorPage::Face,
    tr("Face Inspector"),
    tr("Face Inspector (Ctrl+3)"),
    "Inspector_NavigationFace",
    "InspectorFace.svg");
  addNavigationButton(
    InspectorPage::Outliner,
    tr("Outliner"),
    tr("Outliner"),
    "Inspector_NavigationOutliner",
    "InspectorOutliner.svg");
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
  const auto index = static_cast<int>(page);
  switch (page)
  {
  case InspectorPage::Entity:
    if (!m_entityInspector)
    {
      m_entityInspector = new EntityInspector{*m_appController, *m_document};
      m_tabBook->replacePage(index, m_entityInspector);
      emit pagePrepared(index);
    }
    break;
  case InspectorPage::Face:
    if (!m_faceInspector)
    {
      m_faceInspector = new FaceInspector{*m_appController, *m_document};
      m_tabBook->replacePage(index, m_faceInspector);
      emit pagePrepared(index);
    }
    break;
  case InspectorPage::Outliner:
    if (!m_outlinerInspector)
    {
      m_outlinerInspector = new OutlinerInspector{*m_document};
      m_tabBook->replacePage(index, m_outlinerInspector);
      emit pagePrepared(index);
    }
    break;
  case InspectorPage::Plugin:
    if (!m_pluginInspector)
    {
      m_pluginInspector = new PluginInspector{};
      m_tabBook->replacePage(index, m_pluginInspector);
      emit pagePrepared(index);
    }
    break;
  case InspectorPage::Map:
  case InspectorPage::Terrain:
    break;
  }
}

void Inspector::switchToPage(const InspectorPage page)
{
  ensurePage(page);
  m_tabBook->switchToPage(static_cast<int>(page));
}

InspectorPage Inspector::currentPage() const
{
  return static_cast<InspectorPage>(m_tabBook->currentPageIndex());
}

void Inspector::updateNavigation(const int page)
{
  if (page < 0 || page >= static_cast<int>(m_navigationButtons.size()))
  {
    return;
  }

  const auto titles = std::array{
    tr("Map"), tr("Entity"), tr("Face"), tr("Outliner"), tr("Plugins"), tr("Terrain")};
  m_pageTitle->setText(titles.at(static_cast<size_t>(page)).toUpper());
  m_navigationButtons.at(static_cast<size_t>(page))->setChecked(true);
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
