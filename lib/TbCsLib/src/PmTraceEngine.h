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

 Ported from ReHLDS (https://github.com/rehlds/ReHLDS), MIT licensed,
 Copyright (c) 2014-2025 ReHLDS Contributors.
 */

// Engine-side collision services for pm_shared, ported from ReHLDS
// engine/pmovetst.cpp (and SV_RecursiveHullCheck from engine/world.cpp), i.e. the
// behaviour of the original GoldSrc engine without REHLDS_FIXES. Only BSP physents
// exist in the editor (the world and brush entities), so the studio model and box
// hull paths are left out, as is rotation (brush entities have no angles).
#pragma once

#include "BspTypes.h"
#include "Hlsdk.h"

namespace tb::cs::pm
{

using IgnoreFn = int (*)(physent_t* pe);

// PM_HullPointContents
int hullPointContents(const Hull* hull, int num, const float* p);

// PM_RecursiveHullCheck
bool recursiveHullCheck(
  const Hull* hull,
  int num,
  float p1f,
  float p2f,
  const float* p1,
  const float* p2,
  pmtrace_t* trace);

// PM_HullForBsp
Hull* hullForBsp(physent_t* pe, const playermove_t* pmove, float* offset);

// _PM_PlayerTrace
pmtrace_t playerTrace(
  playermove_t* pmove,
  const float* start,
  const float* end,
  int traceFlags,
  int numphysent,
  physent_t* physents,
  int ignore_pe,
  IgnoreFn filter);

// _PM_TestPlayerPosition
int testPlayerPosition(
  playermove_t* pmove, const float* pos, pmtrace_t* ptrace, IgnoreFn filter);

// PM_PointContents (currents are reported as water), PM_TruePointContents
int pointContents(playermove_t* pmove, const float* p, int* truecontents);
int truePointContents(playermove_t* pmove, const float* p);

// PM_TraceModel
float traceModel(
  playermove_t* pmove,
  physent_t* pe,
  const float* start,
  const float* end,
  trace_t* trace);

// PM_TraceLine (filter == nullptr) and PM_TraceLineEx
pmtrace_t* traceLine(
  playermove_t* pmove,
  const float* start,
  const float* end,
  int flags,
  int usehull,
  int ignore_pe,
  IgnoreFn filter);

// PM_StuckTouch
void stuckTouch(playermove_t* pmove, int hitent, const pmtrace_t* traceresult);

} // namespace tb::cs::pm
