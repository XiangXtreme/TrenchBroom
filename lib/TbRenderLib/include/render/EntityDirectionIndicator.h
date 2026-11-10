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

#include "vm/vec.h"

#include <optional>
#include <vector>

namespace tb::gl
{
class Camera;
}

namespace tb::render
{

enum class EntityDirectionIndicatorKind
{
  Arrow,
  TowardsViewer,
  AwayFromViewer,
};

struct EntityDirectionIndicator
{
  EntityDirectionIndicatorKind kind;
  // Camera-facing triangles. Fill and outline do not overlap, so depth
  // testing and the occluded pass work independently of color batching order.
  std::vector<vm::vec3f> fill;
  std::vector<vm::vec3f> outline;
};

/** Builds a bounded logical-pixel indicator anchored at the entity's center. */
std::optional<EntityDirectionIndicator> makeEntityDirectionIndicator(
  const gl::Camera& camera, const vm::vec3f& center, const vm::vec3f& direction);

} // namespace tb::render
