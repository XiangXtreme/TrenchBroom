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

#include "ui/TabBook.h"

class QWidget;

namespace tb
{
namespace gl
{
class Material;
} // namespace gl

namespace ui
{
class AppController;
class FaceAttribsEditor;
class MapDocument;

class FaceInspector : public TabBookPage
{
  Q_OBJECT
private:
  FaceAttribsEditor* m_faceAttribsEditor = nullptr;

public:
  FaceInspector(
    AppController& appController, MapDocument& document, QWidget* parent = nullptr);
  ~FaceInspector() override;

  bool cancelMouseDrag();
signals:
  void materialBrowserRequested(const gl::Material* material);
};

} // namespace ui
} // namespace tb
