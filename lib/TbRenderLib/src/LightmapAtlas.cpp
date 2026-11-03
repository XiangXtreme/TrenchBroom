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

#include "render/LightmapAtlas.h"

#include "gl/GlInterface.h"
#include "gl/Texture.h"
#include "gl/TextureBuffer.h"

namespace tb::render
{

namespace
{

/**
 * The placeholder atlas: one opaque white texel. Sampling it anywhere yields white,
 * which the face shader multiplies the shaded surface with, leaving it unchanged.
 */
std::unique_ptr<gl::Texture> makePlaceholderTexture(gl::Gl& gl)
{
  auto buffer = gl::TextureBuffer{4};
  auto* data = buffer.data();
  data[0] = 255;
  data[1] = 255;
  data[2] = 255;
  data[3] = 255;

  auto texture = std::make_unique<gl::Texture>(
    1, 1, Color{RgbaF{1.0f, 1.0f, 1.0f, 1.0f}}, GL_RGBA, gl::NoEmbeddedDefaults{},
    std::move(buffer));
  texture->upload(gl);
  return texture;
}

} // namespace

LightmapAtlas::LightmapAtlas() = default;
LightmapAtlas::~LightmapAtlas() = default;

bool LightmapAtlas::activate(gl::Gl& gl)
{
  if (!m_texture)
  {
    m_texture = makePlaceholderTexture(gl);
    m_uploaded = true;
  }

  if (!m_texture->isReady())
  {
    m_texture->upload(gl);
    m_uploaded = true;
  }

  // The atlas must be sampled without mipmaps and clamped at its borders, otherwise
  // neighbouring faces bleed into each other.
  return m_texture->activate(gl, GL_NEAREST, GL_NEAREST);
}

void LightmapAtlas::deactivate(gl::Gl& gl)
{
  if (m_texture)
  {
    m_texture->deactivate(gl);
  }
}

} // namespace tb::render
