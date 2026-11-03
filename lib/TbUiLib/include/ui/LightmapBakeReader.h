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

#include "render/LightmapData.h"

#include <filesystem>
#include <optional>
#include <string>

namespace tb::ui
{

/**
 * Reads a baked lightmap back in: the manifest says where every face landed in the
 * atlas, the image supplies the pixels.
 *
 * Returns nothing and fills in `error` when either file is missing or cannot be read.
 */
std::optional<render::LightmapBake> readLightmapBake(
  const std::filesystem::path& manifestPath,
  const std::filesystem::path& atlasPath,
  std::string& error);

} // namespace tb::ui
