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

// Port of ReHLDS engine/pmovetst.cpp, see PmTraceEngine.h. Expressions keep the original
// form and evaluation order (compile with -ffp-contract=off) so results round the
// same way; real_t is double in ReHLDS's original-compatible build, which matters
// where it is noted.
#include "PmTraceEngine.h"

#include <cstring>

namespace tb::cs::pm
{
namespace
{

constexpr int ContentsEmpty = -1;
constexpr int ContentsSolid = -2;
constexpr int ContentsWater = -3;
constexpr int ContentsTranslucent = -15;
constexpr float DistEpsilon = 0.03125f;

const BrushModel* modelOf(const physent_t* pe)
{
  return reinterpret_cast<const BrushModel*>(pe->model);
}

// _DotProduct: float arithmetic, returned as real_t (double).
double dotProduct(const float* v1, const float* v2)
{
  return static_cast<double>(v1[0] * v2[0] + v1[1] * v2[1] + v1[2] * v2[2]);
}

void copyVec(const float* in, float* out)
{
  out[0] = in[0];
  out[1] = in[1];
  out[2] = in[2];
}

bool isNan(const float f)
{
  return f != f;
}

// SV_HullPointContents
int svHullPointContents(const Hull* hull, int num, const float* p)
{
  while (num >= 0)
  {
    const ClipNode& node = hull->clipnodes[num];
    const ClipPlane& plane = hull->planes[node.planenum];
    const float d = plane.type < 3 ? p[plane.type] - plane.dist
                                   : static_cast<float>(
                                       dotProduct(plane.normal, p)
                                       - static_cast<double>(plane.dist));
    num = d < 0 ? node.children[1] : node.children[0];
  }
  return num;
}

// SV_RecursiveHullCheck, the server's (Quake style) hull trace used by PM_TraceModel.
bool svRecursiveHullCheck(
  const Hull* hull,
  const int num,
  const float p1f,
  const float p2f,
  const float* p1,
  const float* p2,
  pmtrace_t* trace)
{
  const float pdif = p2f - p1f;

  if (num < 0)
  {
    if (num != ContentsSolid)
    {
      trace->allsolid = 0;
      if (num == ContentsEmpty)
      {
        trace->inopen = 1;
      }
      else if (num != ContentsTranslucent)
      {
        trace->inwater = 1;
      }
    }
    else
    {
      trace->startsolid = 1;
    }
    return true;
  }

  const ClipNode& node = hull->clipnodes[num];
  const ClipPlane& plane = hull->planes[node.planenum];

  float t1 = 0.0f;
  float t2 = 0.0f;
  if (plane.type < 3)
  {
    t1 = p1[plane.type] - plane.dist;
    t2 = p2[plane.type] - plane.dist;
  }
  else
  {
    t1 = static_cast<float>(dotProduct(plane.normal, p1) - static_cast<double>(plane.dist));
    t2 = static_cast<float>(dotProduct(plane.normal, p2) - static_cast<double>(plane.dist));
  }

  if (t1 >= 0.0f && t2 >= 0.0f)
  {
    return svRecursiveHullCheck(hull, node.children[0], p1f, p2f, p1, p2, trace);
  }
  if (t1 < 0.0f && t2 < 0.0f)
  {
    return svRecursiveHullCheck(hull, node.children[1], p1f, p2f, p1, p2, trace);
  }

  // put the crosspoint DIST_EPSILON pixels on the near side
  float frac = t1 < 0.0f ? (t1 + DistEpsilon) / (t1 - t2) : (t1 - DistEpsilon) / (t1 - t2);
  if (frac < 0.0f)
  {
    frac = 0.0f;
  }
  else if (frac > 1.0f)
  {
    frac = 1.0f;
  }
  if (isNan(frac))
  {
    return false;
  }

  // mid = p1 + frac * (p2 - p1), with the difference stored as real_t (double)
  const auto lerp = [&](const float f, float* out) {
    for (int i = 0; i < 3; ++i)
    {
      const auto point = static_cast<double>(p2[i] - p1[i]);
      out[i] = static_cast<float>(static_cast<double>(f) * point + static_cast<double>(p1[i]));
    }
  };

  float midf = p1f + pdif * frac;
  float mid[3];
  lerp(frac, mid);

  const int side = t1 < 0.0f ? 1 : 0;

  // move up to the node
  if (!svRecursiveHullCheck(hull, node.children[side], p1f, midf, p1, mid, trace))
  {
    return false;
  }

  if (svHullPointContents(hull, node.children[side ^ 1], mid) != ContentsSolid)
  {
    // go past the node
    return svRecursiveHullCheck(hull, node.children[side ^ 1], midf, p2f, mid, p2, trace);
  }

  if (trace->allsolid)
  {
    return false; // never got out of the solid area
  }

  // the other side of the node is solid, this is the impact point
  if (!side)
  {
    copyVec(plane.normal, trace->plane.normal);
    trace->plane.dist = plane.dist;
  }
  else
  {
    trace->plane.normal[0] = -plane.normal[0];
    trace->plane.normal[1] = -plane.normal[1];
    trace->plane.normal[2] = -plane.normal[2];
    trace->plane.dist = -plane.dist;
  }

  while (svHullPointContents(hull, hull->firstclipnode, mid) == ContentsSolid)
  {
    // shouldn't really happen, but does occasionally
    frac -= 0.1f;
    if (frac < 0.0f)
    {
      trace->fraction = midf;
      copyVec(mid, trace->endpos);
      return false; // backup past 0
    }
    midf = p1f + pdif * frac;
    lerp(frac, mid);
  }

  trace->fraction = midf;
  copyVec(mid, trace->endpos);
  return false;
}

} // namespace

int hullPointContents(const Hull* hull, int num, const float* p)
{
  if (hull->firstclipnode >= hull->lastclipnode)
  {
    return ContentsEmpty;
  }

  while (num >= 0)
  {
    const ClipNode& node = hull->clipnodes[num];
    const ClipPlane& plane = hull->planes[node.planenum];

    const float d = plane.type >= 3 ? static_cast<float>(
                                        dotProduct(p, plane.normal)
                                        - static_cast<double>(plane.dist))
                                    : p[plane.type] - plane.dist;
    num = d >= 0.0f ? node.children[0] : node.children[1];
  }
  return num;
}

bool recursiveHullCheck(
  const Hull* hull,
  const int num,
  const float p1f,
  const float p2f,
  const float* p1,
  const float* p2,
  pmtrace_t* trace)
{
  if (num < 0)
  {
    if (num == ContentsSolid)
    {
      trace->startsolid = 1;
    }
    else
    {
      trace->allsolid = 0;
      if (num == ContentsEmpty)
      {
        trace->inopen = 1;
      }
      else
      {
        trace->inwater = 1;
      }
    }
    return true;
  }

  if (hull->firstclipnode >= hull->lastclipnode)
  {
    trace->allsolid = 0;
    trace->inopen = 1;
    return true;
  }

  const ClipNode& node = hull->clipnodes[num];
  const ClipPlane& plane = hull->planes[node.planenum];

  float t1 = 0.0f;
  float t2 = 0.0f;
  if (plane.type >= 3)
  {
    t1 = static_cast<float>(dotProduct(p1, plane.normal) - static_cast<double>(plane.dist));
    t2 = static_cast<float>(dotProduct(p2, plane.normal) - static_cast<double>(plane.dist));
  }
  else
  {
    t1 = p1[plane.type] - plane.dist;
    t2 = p2[plane.type] - plane.dist;
  }

  if (t1 >= 0.0f && t2 >= 0.0f)
  {
    return recursiveHullCheck(hull, node.children[0], p1f, p2f, p1, p2, trace);
  }

  // midf is the crosspoint as a fraction of p1 -> p2, frac the same in trace space
  float midf = 0.0f;
  if (t1 >= 0.0f)
  {
    midf = t1 - DistEpsilon;
  }
  else
  {
    if (t2 < 0.0f)
    {
      return recursiveHullCheck(hull, node.children[1], p1f, p2f, p1, p2, trace);
    }
    midf = t1 + DistEpsilon;
  }
  midf = midf / (t1 - t2);
  if (midf >= 0.0f)
  {
    if (midf > 1.0f)
    {
      midf = 1.0f;
    }
  }
  else
  {
    midf = 0.0f;
  }

  const float pdif = p2f - p1f;
  float frac = pdif * midf + p1f;
  float mid[3];
  mid[0] = (p2[0] - p1[0]) * midf + p1[0];
  mid[1] = (p2[1] - p1[1]) * midf + p1[1];
  mid[2] = (p2[2] - p1[2]) * midf + p1[2];

  const int side = t1 >= 0.0f ? 0 : 1;

  if (!recursiveHullCheck(hull, node.children[side], p1f, frac, p1, mid, trace))
  {
    return false;
  }

  if (hullPointContents(hull, node.children[side ^ 1], mid) != ContentsSolid)
  {
    return recursiveHullCheck(hull, node.children[side ^ 1], frac, p2f, mid, p2, trace);
  }

  if (trace->allsolid)
  {
    return false;
  }

  if (side)
  {
    // vec3_origin - normal: zero components stay +0
    trace->plane.normal[0] = 0.0f - plane.normal[0];
    trace->plane.normal[1] = 0.0f - plane.normal[1];
    trace->plane.normal[2] = 0.0f - plane.normal[2];
    trace->plane.dist = -plane.dist;
  }
  else
  {
    copyVec(plane.normal, trace->plane.normal);
    trace->plane.dist = plane.dist;
  }

  if (hullPointContents(hull, hull->firstclipnode, mid) != ContentsSolid)
  {
    trace->fraction = frac;
    copyVec(mid, trace->endpos);
    return false;
  }

  // The crosspoint is inside solid: back up in steps of 5% of the segment.
  while (true)
  {
    midf = static_cast<float>(static_cast<double>(midf) - 0.05);
    if (midf < 0.0f)
    {
      break;
    }

    frac = pdif * midf + p1f;
    mid[0] = (p2[0] - p1[0]) * midf + p1[0];
    mid[1] = (p2[1] - p1[1]) * midf + p1[1];
    mid[2] = (p2[2] - p1[2]) * midf + p1[2];
    if (hullPointContents(hull, hull->firstclipnode, mid) != ContentsSolid)
    {
      trace->fraction = frac;
      copyVec(mid, trace->endpos);
      return false;
    }
  }

  trace->fraction = frac;
  copyVec(mid, trace->endpos);
  return false; // trace backed up past 0.0
}

Hull* hullForBsp(physent_t* pe, const playermove_t* pmove, float* offset)
{
  auto* model = const_cast<BrushModel*>(modelOf(pe));
  Hull* hull = nullptr;
  switch (pmove->usehull)
  {
  case 1:
    hull = &model->hulls[3];
    break;
  case 2:
    hull = &model->hulls[0];
    break;
  case 3:
    hull = &model->hulls[2];
    break;
  default:
    hull = &model->hulls[1];
    break;
  }

  offset[0] = hull->clip_mins[0] - pmove->player_mins[pmove->usehull][0];
  offset[1] = hull->clip_mins[1] - pmove->player_mins[pmove->usehull][1];
  offset[2] = hull->clip_mins[2] - pmove->player_mins[pmove->usehull][2];
  offset[0] += pe->origin[0];
  offset[1] += pe->origin[1];
  offset[2] += pe->origin[2];
  return hull;
}

pmtrace_t playerTrace(
  playermove_t* pmove,
  const float* start,
  const float* end,
  const int traceFlags,
  const int numphysent,
  physent_t* physents,
  const int ignore_pe,
  IgnoreFn filter)
{
  pmtrace_t trace;
  std::memset(&trace, 0, sizeof(trace));
  trace.fraction = 1.0f;
  trace.ent = -1;
  copyVec(end, trace.endpos);

  for (int i = 0; i < numphysent; ++i)
  {
    physent_t* pe = &physents[i];
    if (i > 0 && (traceFlags & PM_WORLD_ONLY))
    {
      break;
    }

    if (filter)
    {
      if (filter(pe))
      {
        continue;
      }
    }
    else if (ignore_pe != -1 && i == ignore_pe)
    {
      continue;
    }

    if (
      (pe->model && !pe->solid && pe->skin)
      || ((traceFlags & PM_GLASS_IGNORE) && pe->rendermode))
    {
      continue;
    }
    if (!pe->model)
    {
      continue; // studio models and boxes never exist in the editor
    }

    float offset[3];
    const Hull* hull = hullForBsp(pe, pmove, offset);

    float startL[3];
    float endL[3];
    for (int k = 0; k < 3; ++k)
    {
      startL[k] = start[k] - offset[k];
      endL[k] = end[k] - offset[k];
    }

    pmtrace_t total;
    std::memset(&total, 0, sizeof(total));
    copyVec(end, total.endpos);
    total.fraction = 1.0f;
    total.allsolid = 1;
    recursiveHullCheck(hull, hull->firstclipnode, 0.0f, 1.0f, startL, endL, &total);
    if (total.allsolid)
    {
      total.startsolid = 1;
    }

    if (total.startsolid)
    {
      total.fraction = 0.0f;
    }

    if (total.fraction != 1.0f)
    {
      total.endpos[0] = (end[0] - start[0]) * total.fraction + start[0];
      total.endpos[1] = (end[1] - start[1]) * total.fraction + start[1];
      total.endpos[2] = (end[2] - start[2]) * total.fraction + start[2];
    }

    if (total.fraction < trace.fraction)
    {
      trace = total;
      trace.ent = i;
    }
  }
  return trace;
}

int testPlayerPosition(playermove_t* pmove, const float* pos, pmtrace_t* ptrace, IgnoreFn filter)
{
  const pmtrace_t tr = playerTrace(
    pmove, pmove->origin, pmove->origin, PM_NORMAL, pmove->numphysent, pmove->physents, -1, nullptr);
  if (ptrace)
  {
    *ptrace = tr;
  }

  for (int i = 0; i < pmove->numphysent; ++i)
  {
    physent_t* pe = &pmove->physents[i];
    if (filter && filter(pe))
    {
      continue;
    }
    if (pe->model && pe->solid == SOLID_NOT && pe->skin != 0)
    {
      continue;
    }
    if (!pe->model)
    {
      continue;
    }

    float offset[3];
    const Hull* hull = hullForBsp(pe, pmove, offset);
    const float test[3] = {pos[0] - offset[0], pos[1] - offset[1], pos[2] - offset[2]};
    if (hullPointContents(hull, hull->firstclipnode, test) == ContentsSolid)
    {
      return i;
    }
  }
  return -1;
}

int pointContents(playermove_t* pmove, const float* p, int* truecontents)
{
  if (!pmove->physents[0].model)
  {
    if (truecontents)
    {
      *truecontents = ContentsEmpty;
    }
    return ContentsSolid;
  }

  const BrushModel* world = modelOf(&pmove->physents[0]);
  int entityContents = hullPointContents(&world->hulls[0], world->hulls[0].firstclipnode, p);
  if (truecontents)
  {
    *truecontents = entityContents;
  }
  if (entityContents > CONTENTS_CURRENT_0 || entityContents < CONTENTS_CURRENT_DOWN)
  {
    if (entityContents == ContentsSolid)
    {
      return entityContents;
    }
  }
  else
  {
    entityContents = ContentsWater; // currents count as water
  }

  // PM_LinkContents: the first non-solid brush entity containing the point wins
  for (int i = 1; i < pmove->numphysent; ++i)
  {
    const physent_t* pe = &pmove->physents[i];
    if (pe->solid || !pe->model)
    {
      continue;
    }
    const BrushModel* model = modelOf(pe);
    const float test[3] = {p[0] - pe->origin[0], p[1] - pe->origin[1], p[2] - pe->origin[2]};
    if (hullPointContents(&model->hulls[0], model->hulls[0].firstclipnode, test) != ContentsEmpty)
    {
      return pe->skin;
    }
  }
  return entityContents;
}

int truePointContents(playermove_t* pmove, const float* p)
{
  if (!pmove->physents[0].model)
  {
    return ContentsEmpty;
  }
  const BrushModel* world = modelOf(&pmove->physents[0]);
  return hullPointContents(&world->hulls[0], world->hulls[0].firstclipnode, p);
}

float traceModel(
  playermove_t* pmove, physent_t* pe, const float* start, const float* end, trace_t* trace)
{
  const int saveHull = pmove->usehull;
  pmove->usehull = 2;
  float offset[3];
  const Hull* hull = hullForBsp(pe, pmove, offset);
  pmove->usehull = saveHull;

  const float startL[3] = {start[0] - offset[0], start[1] - offset[1], start[2] - offset[2]};
  const float endL[3] = {end[0] - offset[0], end[1] - offset[1], end[2] - offset[2]};

  // The engine traces into the caller's trace_t, which pm_shared leaves uninitialized
  // (PM_LadderMove); start from "nothing hit" instead of undefined values.
  pmtrace_t tr;
  std::memset(&tr, 0, sizeof(tr));
  tr.fraction = 1.0f;
  tr.allsolid = 1;
  copyVec(end, tr.endpos);
  svRecursiveHullCheck(hull, hull->firstclipnode, 0.0f, 1.0f, startL, endL, &tr);

  std::memset(trace, 0, sizeof(*trace));
  trace->allsolid = tr.allsolid;
  trace->startsolid = tr.startsolid;
  trace->inopen = tr.inopen;
  trace->inwater = tr.inwater;
  trace->fraction = tr.fraction;
  copyVec(tr.endpos, trace->endpos);
  copyVec(tr.plane.normal, trace->plane.normal);
  trace->plane.dist = tr.plane.dist;
  trace->ent = nullptr;
  return trace->fraction;
}

pmtrace_t* traceLine(
  playermove_t* pmove,
  const float* start,
  const float* end,
  const int flags,
  const int usehull,
  const int ignore_pe,
  IgnoreFn filter)
{
  static thread_local pmtrace_t tr;
  const int oldhull = pmove->usehull;
  pmove->usehull = usehull;
  if (filter)
  {
    // PM_TraceLineEx
    tr = flags ? playerTrace(
                   pmove, start, end, PM_NORMAL, pmove->numvisent, pmove->visents, -1, filter)
               : playerTrace(
                   pmove, start, end, PM_NORMAL, pmove->numphysent, pmove->physents, -1, filter);
  }
  else if (flags == 0)
  {
    tr = playerTrace(
      pmove, start, end, PM_NORMAL, pmove->numphysent, pmove->physents, ignore_pe, nullptr);
  }
  else if (flags == 1)
  {
    tr = playerTrace(
      pmove, start, end, PM_NORMAL, pmove->numvisent, pmove->visents, ignore_pe, nullptr);
  }
  pmove->usehull = oldhull;
  return &tr;
}

void stuckTouch(playermove_t* pmove, int /* hitent */, const pmtrace_t* traceresult)
{
  // PM_StuckTouch -> PM_AddToTouched (server side), which keys on the trace's entity
  if (!pmove->server)
  {
    return;
  }
  for (int i = 0; i < pmove->numtouch; ++i)
  {
    if (pmove->touchindex[i].ent == traceresult->ent)
    {
      return;
    }
  }
  if (pmove->numtouch >= MAX_PHYSENTS)
  {
    return;
  }
  pmtrace_t tr = *traceresult;
  copyVec(pmove->velocity, tr.deltavelocity);
  pmove->touchindex[pmove->numtouch++] = tr;
}

} // namespace tb::cs::pm
