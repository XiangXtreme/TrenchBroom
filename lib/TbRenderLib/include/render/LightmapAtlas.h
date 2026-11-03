/*
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

#pragma once

#include "base/Macros.h"
#include "gl/GlUtils.h"

#include <memory>

namespace tb
{
namespace gl
{
class Gl;
class Texture;
} // namespace gl

namespace render
{

/**
 * Holds the texture that brush faces sample their lightmap from and hands it to the
 * face shader.
 *
 * The atlas is bound to the texture unit that is active when activate() is called. As
 * long as no baked lightmap has been supplied, the atlas contains a single white texel,
 * so that sampling it leaves the shaded surface unchanged.
 */
class LightmapAtlas
{
private:
  std::unique_ptr<gl::Texture> m_texture;
  bool m_uploaded = false;

public:
  LightmapAtlas();
  ~LightmapAtlas();

  deleteCopyAndMove(LightmapAtlas);

  /**
   * Binds the atlas to the currently active texture unit, uploading the placeholder
   * texel on first use. Returns false if the atlas cannot be used, in which case the
   * caller must leave lightmapping disabled.
   */
  bool activate(gl::Gl& gl);

  /**
   * Unbinds the atlas from the currently active texture unit.
   */
  void deactivate(gl::Gl& gl);
};

} // namespace render
} // namespace tb
