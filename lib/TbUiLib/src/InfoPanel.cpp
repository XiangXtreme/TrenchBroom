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

#include "ui/InfoPanel.h"

#include <QByteArray>
#include <QDataStream>
#include <QIODevice>
#include <QVBoxLayout>

#include "ui/AppController.h"
#include "ui/Console.h"
#include "ui/IssueBrowser.h"
#include "ui/MapDocument.h"
#include "ui/ModelInspector.h"
#include "ui/ProjectSession.h"
#include "ui/PythonConsole.h"
#include "ui/TabBar.h"
#include "ui/TabBook.h"

namespace tb::ui
{

InfoPanel::InfoPanel(AppController& appController, MapDocument& document, QWidget* parent)
  : QWidget{parent}
{
  m_tabBook = new TabBook{};
  m_tabBook->setObjectName("InfoPanel_TabBook");
  m_tabBook->tabBar()->setObjectName("InfoPanel_TabBar");
  m_tabBook->tabBar()->setProperty("regionAnchorTabs", true);

  m_console = new Console{};
  m_console->setObjectName("InfoPanel_Console");
  m_pythonConsole = new PythonConsole{};
  m_pythonConsole->setObjectName("InfoPanel_PythonConsole");
  m_issueBrowser = new IssueBrowser{document};
  m_issueBrowser->setObjectName("InfoPanel_Issues");
  m_project = appController.projectFor(document).get();
  if (!m_project)
  {
    m_modelInspector = new ModelInspector{appController, document};
    m_modelInspector->setObjectName("InfoPanel_Assets");
  }

  m_tabBook->addPage(m_console, tr("Console").toUpper());
  m_tabBook->addPage(m_pythonConsole, tr("Python Console").toUpper());
  m_tabBook->addPage(m_issueBrowser, tr("Issues").toUpper());
  if (m_modelInspector)
    m_tabBook->addPage(m_modelInspector, tr("Assets").toUpper());

  auto* sizer = new QVBoxLayout{};
  sizer->setContentsMargins(0, 0, 0, 0);
  sizer->addWidget(m_tabBook);
  setLayout(sizer);
}

InfoPanel::~InfoPanel() = default;

Console* InfoPanel::console() const
{
  return m_console;
}

PythonConsole* InfoPanel::pythonConsole() const
{
  return m_pythonConsole;
}

void InfoPanel::switchToPage(const InfoPanelPage page)
{
  if (page == InfoPanelPage::Assets && m_project)
    emit m_project->browserFocusRequested();
  else
    m_tabBook->switchToPage(static_cast<int>(page));
}

InfoPanelPage InfoPanel::currentPage() const
{
  return static_cast<InfoPanelPage>(m_tabBook->currentPageIndex());
}

void InfoPanel::setNavigationVisible(const bool visible)
{
  // Keep the active page's controls (issue filters, Python commands) available.
  m_tabBook->tabBar()->setNavigationVisible(visible);
}

QByteArray InfoPanel::saveState() const
{
  auto result = QByteArray{};
  auto stream = QDataStream{&result, QIODevice::WriteOnly};
  stream << !isHidden();
  return result;
}

bool InfoPanel::restoreState(const QByteArray& state)
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
