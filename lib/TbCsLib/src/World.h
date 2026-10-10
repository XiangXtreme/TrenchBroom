/*
 Copyright (C) 2026 KreedzBroom contributors

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

#include "BspTypes.h"

#include <memory>
#include <vector>

namespace tb::cs {

// An immutable compiled collision world. models[0] is worldspawn, models[n]
// is brush entity n-1. Shared between contexts via shared_ptr so the editor
// can rebuild and hot-swap it while a context is still using the old one.
struct World
{
  std::vector<std::unique_ptr<BrushModel>> models;
};

} // namespace tb::cs
