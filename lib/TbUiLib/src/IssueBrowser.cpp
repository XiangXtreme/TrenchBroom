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

#include "ui/IssueBrowser.h"

#include <QVBoxLayout>

#include "ui/IssueBrowserView.h"
#include "ui/MapDocument.h"

namespace tb::ui
{

IssueBrowser::IssueBrowser(MapDocument& document, QWidget* parent)
  : TabBookPage{parent}
  , m_document{document}
  , m_view{new IssueBrowserView{m_document, this}}
{
  auto* sizer = new QVBoxLayout{};
  sizer->setContentsMargins(0, 0, 0, 0);
  sizer->addWidget(m_view);
  setLayout(sizer);

  connectObservers();
}

void IssueBrowser::connectObservers()
{
  m_notifierConnection += m_document.documentWasLoadedNotifier.connect([&] {
    m_view->updateFilterFlags();
    m_view->reload();
  });
  m_notifierConnection +=
    m_document.documentWasSavedNotifier.connect([&] { m_view->reload(); });
  m_notifierConnection +=
    m_document.documentDidChangeNotifier.connect([&] { m_view->reload(); });
  m_notifierConnection +=
    m_document.materialCollectionsDidChangeNotifier.connect([&] { m_view->reload(); });
  m_notifierConnection +=
    m_document.entityDefinitionsDidChangeNotifier.connect([&] { m_view->reload(); });
}

} // namespace tb::ui
