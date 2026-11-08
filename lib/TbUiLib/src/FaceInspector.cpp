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

#include "ui/FaceInspector.h"

#include <QVBoxLayout>

#include "ui/FaceAttribsEditor.h"

namespace tb::ui
{
FaceInspector::FaceInspector(
  AppController& appController, MapDocument& document, QWidget* parent)
  : TabBookPage{parent}
  , m_faceAttribsEditor{new FaceAttribsEditor{appController, document}}
{
  auto* layout = new QVBoxLayout{this};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(m_faceAttribsEditor, 1);
  connect(
    m_faceAttribsEditor,
    &FaceAttribsEditor::materialBrowserRequested,
    this,
    &FaceInspector::materialBrowserRequested);
}
FaceInspector::~FaceInspector() = default;
bool FaceInspector::cancelMouseDrag()
{
  return m_faceAttribsEditor->cancelMouseDrag();
}
} // namespace tb::ui
