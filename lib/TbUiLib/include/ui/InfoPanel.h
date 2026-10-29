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

namespace tb
{
class Logger;

namespace ui
{
class Console;
class AppController;
class IssueBrowser;
class MapDocument;
class ModelInspector;
class PythonConsole;
class ProjectSession;
class TabBook;

enum class InfoPanelPage
{
  Console,
  PythonConsole,
  Issues,
  Assets,
};

class InfoPanel : public QWidget
{
  Q_OBJECT
private:
  ProjectSession* m_project = nullptr;
  TabBook* m_tabBook = nullptr;
  Console* m_console = nullptr;
  PythonConsole* m_pythonConsole = nullptr;
  IssueBrowser* m_issueBrowser = nullptr;
  ModelInspector* m_modelInspector = nullptr;

public:
  InfoPanel(
    AppController& appController, MapDocument& document, QWidget* parent = nullptr);
  ~InfoPanel() override;

  Console* console() const;
  PythonConsole* pythonConsole() const;
  void switchToPage(InfoPanelPage page);
  InfoPanelPage currentPage() const;

  QByteArray saveState() const;
  bool restoreState(const QByteArray& state);
};

} // namespace ui
} // namespace tb
