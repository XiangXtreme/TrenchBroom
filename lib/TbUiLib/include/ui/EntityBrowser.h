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

#include "base/NotifierConnection.h"

#include <string>

class QLabel;
class QPushButton;
class QScrollBar;

namespace tb::mdl
{
enum class EntityDefinitionSortOrder;
}

namespace tb::ui
{
class AppController;
class EntityBrowserView;
class MapDocument;

// Entity content canvas. Navigation, search and view controls belong to ModelBrowser.
class EntityBrowser : public QWidget
{
  Q_OBJECT
private:
  MapDocument* m_document;
  AppController& m_appController;
  QWidget* m_content = nullptr;
  QLabel* m_summary = nullptr;
  QPushButton* m_create = nullptr;
  QScrollBar* m_scrollBar = nullptr;
  EntityBrowserView* m_view = nullptr;
  bool m_mapActionsEnabled = true;
  NotifierConnection m_notifierConnection;

public:
  EntityBrowser(
    AppController& appController, MapDocument& document, QWidget* parent = nullptr);
  void reload();
  void setDocument(MapDocument& document);
  void setMapActionsEnabled(bool enabled);
  EntityBrowserView& view() const;
  void showSettings();

signals:
  void contentsChanged();
  void createEntityRequested(const QString& classname);

private:
  void refreshSelection();
  void createGui();
  void connectObservers();
};
} // namespace tb::ui
