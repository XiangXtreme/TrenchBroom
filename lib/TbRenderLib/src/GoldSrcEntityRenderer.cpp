/*
 Copyright (C) 2026 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "render/GoldSrcEntityRenderer.h"

#include "gl/ActiveShader.h"
#include "gl/GlInterface.h"
#include "mdl/Entity.h"
#include "mdl/GameConfig.h"

#include "kd/string_compare.h"
#include "kd/string_utils.h"

#include "vm/vec_io.h"

#include <algorithm>
#include <cmath>

namespace tb::render
{
namespace
{
float number(const mdl::Entity& entity, const std::string& key, const float fallback)
{
  const auto* value = entity.property(key);
  const auto parsed = value ? kdl::str_to_float(*value) : std::nullopt;
  return parsed && std::isfinite(*parsed) ? *parsed : fallback;
}
} // namespace

bool GoldSrcEntityRenderSettings::transparent() const
{
  return mode == 1 || mode == 2 || mode == 3 || mode == 5;
}

bool usesGoldSrcRendering(const mdl::GameConfig& config)
{
  // WAD lump type 0x43 (".C") identifies GoldSrc's mip textures, including custom mods.
  return std::ranges::any_of(config.materialConfig.extensions, [](const auto& extension) {
    return kdl::ci::str_is_equal(extension.string(), ".C");
  });
}

GoldSrcEntityRenderSettings goldSrcEntityRenderSettings(const mdl::Entity* entity)
{
  auto result = GoldSrcEntityRenderSettings{};
  if (!entity)
  {
    return result;
  }
  const auto mode = number(*entity, "rendermode", 0.0f);
  if (mode >= 0.0f && mode <= 5.0f)
  {
    result.mode = static_cast<int>(mode);
  }
  if (result.transparent())
  {
    result.alpha =
      std::clamp(number(*entity, "renderamt", 255.0f), 0.0f, 255.0f) / 255.0f;
  }
  if (const auto* color = entity->property("rendercolor"))
  {
    if (const auto parsed = vm::parse<float, 3>(*color))
    {
      for (size_t i = 0; i < 3; ++i)
      {
        result.color[i] = std::isfinite((*parsed)[i])
                            ? std::clamp((*parsed)[i], 0.0f, 255.0f) / 255.0f
                            : 1.0f;
      }
    }
  }
  if (entity->classname() == "func_conveyor")
  {
    // Valve's CFuncConveyor::Spawn defaults zero speed to 100. The engine scrolls
    // along texture S, independently of the entity's movement direction (angles).
    result.conveyorSpeed = number(*entity, "speed", 100.0f);
    if (result.conveyorSpeed == 0.0f)
    {
      result.conveyorSpeed = 100.0f;
    }
  }
  return result;
}

bool isGoldSrcConveyorMaterial(const std::string_view materialName)
{
  return kdl::ci::str_is_prefix(materialName, "scroll")
         || kdl::ci::str_is_prefix(materialName, "{scroll");
}

float goldSrcConveyorOffset(
  const GoldSrcEntityRenderSettings& settings,
  const std::string_view materialName,
  const size_t textureWidth,
  const double time)
{
  if (
    textureWidth == 0 || !std::isfinite(time) || !isGoldSrcConveyorMaterial(materialName))
  {
    return 0.0f;
  }
  const auto offset = -time * static_cast<double>(settings.conveyorSpeed)
                      / static_cast<double>(textureWidth);
  return static_cast<float>(offset - std::floor(offset));
}

GoldSrcEntityMaterialRenderFunc::GoldSrcEntityMaterialRenderFunc(
  gl::ActiveShader& shader,
  const int minFilter,
  const int magFilter,
  GoldSrcEntityRenderSettings settings)
  : gl::AlphaTestedMaterialRenderFunc{shader, minFilter, magFilter}
  , m_shader{shader}
  , m_settings{std::move(settings)}
{
}

void GoldSrcEntityMaterialRenderFunc::before(gl::Gl& gl, const gl::Material* material)
{
  gl::AlphaTestedMaterialRenderFunc::before(gl, material);
  m_shader.set("EntityRenderMode", m_settings.mode);
  m_shader.set("EntityAlpha", m_settings.alpha);
  m_shader.set("EntityColor", m_settings.color);
  if (m_settings.mode != 0)
  {
    gl.pushAttrib(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (m_settings.transparent())
    {
      gl.enable(GL_BLEND);
      gl.blendFunc(
        GL_SRC_ALPHA,
        m_settings.mode == 3 || m_settings.mode == 5 ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
      gl.depthMask(GL_FALSE);
    }
    else
    {
      gl.disable(GL_BLEND);
      gl.depthMask(GL_TRUE);
    }
  }
}

void GoldSrcEntityMaterialRenderFunc::setSettings(GoldSrcEntityRenderSettings settings)
{
  m_settings = std::move(settings);
}

void GoldSrcEntityMaterialRenderFunc::after(gl::Gl& gl, const gl::Material* material)
{
  if (m_settings.mode != 0)
  {
    gl.popAttrib();
  }
  gl::AlphaTestedMaterialRenderFunc::after(gl, material);
}
} // namespace tb::render
