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

#pragma once

#include <QWidget>

#include <vector>

class QLabel;
class QToolButton;

namespace tb::ui
{
class AppController;
class FaceInspector;
class MapDocument;
class MapViewToolBox;
class MapInspector;
class MapViewBar;
class OutlinerInspector;
class PluginInspector;
class SyncHeightEventFilter;
class TabBook;
class TerrainInspector;

enum class InspectorPage
{
  Map = 0,
  Entity = 1, // Legacy settings/commands redirect to Outliner.
  Face = 2,
  Outliner = 3,
  Plugin = 4,
  Terrain = 5
};

class Inspector : public QWidget
{
  Q_OBJECT
private:
  AppController* m_appController = nullptr;
  MapDocument* m_document = nullptr;
  TabBook* m_tabBook = nullptr;
  MapInspector* m_mapInspector = nullptr;
  FaceInspector* m_faceInspector = nullptr;
  OutlinerInspector* m_outlinerInspector = nullptr;
  PluginInspector* m_pluginInspector = nullptr;
  TerrainInspector* m_terrainInspector = nullptr;

  QLabel* m_pageTitle = nullptr;
  std::vector<QToolButton*> m_navigationButtons;
  SyncHeightEventFilter* m_syncHeaderEventFilter = nullptr;

  void updateNavigation(int page);
  void ensurePage(InspectorPage page);

public:
  Inspector(
    AppController& appController,
    MapDocument& document,
    MapViewToolBox& toolBox,
    QWidget* parent = nullptr);
  ~Inspector() override;

  void connectTopWidgets(MapViewBar* mapViewBar);
  void switchToPage(InspectorPage page);
  InspectorPage currentPage() const;
  bool cancelMouseDrag();

  FaceInspector* faceInspector();
  PluginInspector* pluginInspector();

  QByteArray saveState() const;
  bool restoreState(const QByteArray& state);

signals:
  /** Emitted right after a lazily created inspector page is installed. */
  void pagePrepared(int page);
};

} // namespace tb::ui
