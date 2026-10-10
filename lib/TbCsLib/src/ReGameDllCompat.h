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

// Stand-in for ReGameDLL's precompiled.h. ReGameDLL's pm_shared is compiled
// unmodified (thirdparty/regamedll) against this header instead of the whole game
// server: it provides only what pm_shared needs. REGAMEDLL_ADD, REGAMEDLL_FIXES and
// REGAMEDLL_API stay undefined, which selects the original CS 1.6 behaviour, and
// PLAY_GAMEDLL stays undefined, so real_t is float like in the game.
//
// TbCsLib's engine side includes it too, so both agree on playermove_t.
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Base types and macros normally provided by ReGameDLL's engine headers.
typedef int qboolean;
typedef int BOOL;
typedef unsigned char byte;
typedef int int32;
typedef unsigned int uint32;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#define BIT(n) (1 << (n))
#define DbgAssert(x) assert(x)

#define NOXREF
#define EXT_FUNC
#define __API_HOOK(fname) fname
#define LINK_HOOK_VOID_CHAIN(...)
#define LINK_HOOK_VOID_CHAIN2(...)

#define Q_sqrt sqrt
#define Q_fabs fabs
#define Q_sin sin
#define Q_cos cos
#define Q_atan2 atan2
#define Q_acos acos
#define Q_snprintf snprintf
#define Q_min std::min
#define Q_max std::max
#define Q_memcpy memcpy
#define Q_memset memset
#define Q_strcpy strcpy
#define Q_strncpy strncpy
#define Q_strlen strlen
#define Q_strcmp strcmp
#define Q_stricmp strcasecmp
#define Q_strnicmp strncasecmp
#define Q_memmove memmove

inline char* Q_strlcpy(char* dest, const char* src, size_t size)
{
  Q_strncpy(dest, src, size - 1);
  dest[size - 1] = '\0';
  return dest;
}
template <size_t size>
char* Q_strlcpy(char (&dest)[size], const char* src)
{
  return Q_strlcpy(static_cast<char*>(dest), src, size);
}

// strcpy that works correctly with overlapping src and dst buffers
inline char* Q_strcpy_s(char* dst, char* src)
{
  Q_memmove(dst, src, Q_strlen(src) + 1);
  return dst;
}

#define ARRAYSIZE(p) (sizeof(p) / sizeof(p[0]))

// engine/bspfile.h
#define CONTENTS_CURRENT_0 -9
#define CONTENTS_CURRENT_90 -10
#define CONTENTS_CURRENT_180 -11
#define CONTENTS_CURRENT_270 -12
#define CONTENTS_CURRENT_UP -13
#define CONTENTS_CURRENT_DOWN -14
#define CONTENTS_TRANSLUCENT -15

// game_shared/bot/nav.h
const float HalfHumanHeight = 36.0f;

#include "mathlib.h"
#include "vector.h"
#include "const.h"
#include "usercmd.h"
#include "pmtrace.h"
#include "pm_defs.h"
#include "pm_movevars.h"
#include "pm_shared.h"
#include "pm_math.h"
#include "pm_materials.h"
#include "pm_debug.h"

// engine/model.h: pm_shared reads hull_t::firstclipnode of the hull returned by
// PM_HullForBsp (PM_Ladder); the engine's hull layout.
typedef struct hull_s
{
  void* clipnodes;
  void* planes;
  int firstclipnode;
  int lastclipnode;
  vec3_t clip_mins, clip_maxs;
} hull_t;

typedef enum modtype_e
{
  mod_brush,
  mod_sprite,
  mod_alias,
  mod_studio,
} modtype_t;
