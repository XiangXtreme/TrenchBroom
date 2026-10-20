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

#pragma once

#include "gl/MaterialRenderFunc.h"

#include "vm/vec.h"

#include <string_view>

namespace tb::mdl
{
class Entity;
struct GameConfig;
} // namespace tb::mdl

namespace tb::render
{
struct GoldSrcEntityRenderSettings
{
  int mode = 0;
  float alpha = 1.0f;
  vm::vec3f color{1.0f, 1.0f, 1.0f};
  float conveyorSpeed = 0.0f;

  bool operator==(const GoldSrcEntityRenderSettings&) const = default;
  bool transparent() const;
};

bool usesGoldSrcRendering(const mdl::GameConfig& config);
GoldSrcEntityRenderSettings goldSrcEntityRenderSettings(const mdl::Entity* entity);
bool isGoldSrcConveyorMaterial(std::string_view materialName);
float goldSrcConveyorOffset(
  const GoldSrcEntityRenderSettings& settings,
  std::string_view materialName,
  size_t textureWidth,
  double time);

// Applies entity overrides after activating the shared material and restores GL state
// before deactivating it, so entities sharing a texture can use different render modes.
class GoldSrcEntityMaterialRenderFunc : public gl::AlphaTestedMaterialRenderFunc
{
private:
  gl::ActiveShader& m_shader;
  GoldSrcEntityRenderSettings m_settings;

public:
  GoldSrcEntityMaterialRenderFunc(
    gl::ActiveShader& shader,
    int minFilter,
    int magFilter,
    GoldSrcEntityRenderSettings settings);
  void setSettings(GoldSrcEntityRenderSettings settings);
  void before(gl::Gl& gl, const gl::Material* material) override;
  void after(gl::Gl& gl, const gl::Material* material) override;
};
} // namespace tb::render
