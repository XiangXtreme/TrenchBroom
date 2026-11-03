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

#include <cstring>

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

std::unique_ptr<gl::Texture> makeTexture(
  gl::Gl& gl, const int width, const int height, const std::vector<std::uint8_t>& rgba)
{
  auto buffers = gl::TextureBufferList{};
  buffers.emplace_back(rgba.size());
  std::memcpy(buffers.front().data(), rgba.data(), rgba.size());

  auto texture = std::make_unique<gl::Texture>(
    static_cast<size_t>(width), static_cast<size_t>(height),
    Color{RgbaF{1.0f, 1.0f, 1.0f, 1.0f}}, GL_RGBA, gl::NoEmbeddedDefaults{},
    std::move(buffers));
  texture->upload(gl);
  return texture;
}

} // namespace

LightmapAtlas::LightmapAtlas() = default;
LightmapAtlas::~LightmapAtlas() = default;

void LightmapAtlas::setData(
  const int width, const int height, const std::vector<std::uint8_t>& rgb)
{
  const auto expected = static_cast<size_t>(width) * static_cast<size_t>(height) * 3u;
  if (width <= 0 || height <= 0 || rgb.size() < expected)
  {
    return;
  }

  m_width = width;
  m_height = height;
  m_pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
  for (auto i = size_t{0}; i < expected / 3u; ++i)
  {
    m_pixels[i * 4 + 0] = rgb[i * 3 + 0];
    m_pixels[i * 4 + 1] = rgb[i * 3 + 1];
    m_pixels[i * 4 + 2] = rgb[i * 3 + 2];
    m_pixels[i * 4 + 3] = 255;
  }

  // A texture object is immutable once uploaded, so drop it and upload the new data.
  m_texture.reset();
}

void LightmapAtlas::clear()
{
  m_width = 0;
  m_height = 0;
  m_pixels.clear();
  m_texture.reset();
}

int LightmapAtlas::width() const
{
  return m_width;
}

int LightmapAtlas::height() const
{
  return m_height;
}

bool LightmapAtlas::activate(gl::Gl& gl)
{
  if (!m_texture)
  {
    m_texture = m_pixels.empty()
                  ? makePlaceholderTexture(gl)
                  : makeTexture(gl, m_width, m_height, m_pixels);
  }

  if (!m_texture->isReady())
  {
    m_texture->upload(gl);
  }

  // Blend adjacent luxels at the base level. Mipmaps could mix separate face tiles.
  return m_texture->activate(gl, GL_LINEAR, GL_LINEAR);
}

void LightmapAtlas::deactivate(gl::Gl& gl)
{
  if (m_texture)
  {
    m_texture->deactivate(gl);
  }
}

} // namespace tb::render
