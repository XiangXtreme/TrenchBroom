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

// The only place TbCsLib's engine side includes game headers. They come from the
// same ReGameDLL sources that pm_shared is compiled from (thirdparty/regamedll),
// so playermove_t and friends have identical layouts on both sides.
#pragma once

#include "ReGameDllCompat.h" // pm_defs.h, pm_movevars.h, pm_shared.h (PM_Init / PM_Move)
