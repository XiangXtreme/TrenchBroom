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

class QComboBox;
class QLabel;
class QPushButton;
class QTextBrowser;
class QTreeWidget;
class QLineEdit;
class QScrollBar;
class QToolButton;

namespace tb::ui
{
class AppController;
class EntityBrowserView;
class MapDocument;

class EntityBrowser : public QWidget
{
  Q_OBJECT
private:
  MapDocument* m_document;
  AppController& m_appController;
  QWidget* m_content = nullptr;
  QTreeWidget* m_definitions = nullptr;
  QComboBox* m_typeChoice = nullptr;
  QTextBrowser* m_description = nullptr;
  QPushButton* m_create = nullptr;
  QLabel* m_summary = nullptr;
  bool m_mapActionsEnabled = true;
  QComboBox* m_sortOrderChoice = nullptr;
  QToolButton* m_groupButton = nullptr;
  QToolButton* m_usedButton = nullptr;
  QLineEdit* m_filterBox = nullptr;
  QScrollBar* m_scrollBar = nullptr;
  EntityBrowserView* m_view = nullptr;

  NotifierConnection m_notifierConnection;

public:
  EntityBrowser(
    AppController& appController, MapDocument& document, QWidget* parent = nullptr);

  void reload();
  void setDocument(MapDocument& document);
  void setMapActionsEnabled(bool enabled);
  void focusSearch();

signals:
  void createEntityRequested(const QString& classname);

private:
  void refreshDefinitions();
  void refreshSelection();
  void updateCreationAction();
  void showSettings();

private:
  void createGui(AppController& appController);

  void connectObservers();
};

} // namespace tb::ui
