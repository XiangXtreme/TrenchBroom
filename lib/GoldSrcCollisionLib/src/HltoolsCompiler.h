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
#include <string>
#include <string_view>
#include <vector>

namespace tb::cs
{

struct HltoolsCompileResult
{
  std::vector<std::unique_ptr<BrushModel>> models;
  std::vector<std::string> warnings;
};

HltoolsCompileResult compileHltoolsWorld(std::string_view mapText, int clipType);

} // namespace tb::cs
