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

// extern "C" boundary: argument checking, exception fencing, handle types.
#include "cs/scs.h"

#include "HullCompiler.h"
#include "PlayerHost.h"
#include "World.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

struct scs_world
{
  std::shared_ptr<const tb::cs::World> world;
};

struct scs_context
{
  explicit scs_context(const scs_config& config)
    : host(config)
  {
  }
  tb::cs::PlayerHost host;
};

namespace {

std::vector<tb::cs::BrushPlane> convertPlanes(const scs_brush& brush)
{
  std::vector<tb::cs::BrushPlane> planes;
  planes.reserve(brush.plane_count);
  for (uint32_t i = 0; i < brush.plane_count; ++i)
  {
    const scs_plane& p = brush.planes[i];
    planes.push_back(
      {{p.normal[0], p.normal[1], p.normal[2]}, p.dist, (p.flags & SCS_PLANE_BEVEL) != 0});
  }
  return planes;
}

std::unique_ptr<tb::cs::BrushModel> compileBrushes(
  const scs_brush* brushes,
  uint32_t count,
  bool forceSolid,
  const tb::cs::HullCompileOptions& options,
  tb::cs::HullCompileStats& stats)
{
  std::vector<std::vector<tb::cs::BrushPlane>> storage;
  std::vector<tb::cs::BrushDesc> descs;
  storage.reserve(count);
  descs.reserve(count);
  for (uint32_t i = 0; i < count; ++i)
  {
    if (brushes[i].planes == nullptr || brushes[i].plane_count < 4)
    {
      ++stats.skippedBrushes;
      continue;
    }
    storage.push_back(convertPlanes(brushes[i]));
    descs.push_back(
      {storage.back(),
       forceSolid ? tb::cs::kContentsSolid : brushes[i].contents,
       brushes[i].hull_mask != 0 ? brushes[i].hull_mask & tb::cs::kAllHulls
                                 : tb::cs::kAllHulls});
  }
  return tb::cs::compileBrushModel(descs, options, stats);
}

void fillTrace(const pmtrace_t& in, scs_trace* out)
{
  out->allsolid = in.allsolid;
  out->startsolid = in.startsolid;
  out->inopen = in.inopen;
  out->inwater = in.inwater;
  out->fraction = in.fraction;
  for (int i = 0; i < 3; ++i)
  {
    out->endpos[i] = in.endpos[i];
    out->plane_normal[i] = in.plane.normal[i];
  }
  out->plane_dist = in.plane.dist;
  out->ent = in.ent;
}

} // namespace

extern "C" {

scs_result scs_world_build(const scs_world_desc* desc, scs_world** out_world, scs_world_info* info)
{
  if (!desc || !out_world || (desc->world_brush_count && !desc->world_brushes)
      || (desc->entity_count && !desc->entities)
      || desc->cliptype < SCS_CLIPTYPE_SMALLEST || desc->cliptype > SCS_CLIPTYPE_LEGACY
      || desc->compiler < SCS_COMPILER_VHLT || desc->compiler > SCS_COMPILER_ZHLT)
  {
    return SCS_ERR_INVALID_ARGUMENT;
  }
  *out_world = nullptr;

  const auto start = std::chrono::steady_clock::now();
  try
  {
    tb::cs::HullCompileOptions options;
    options.clipType = static_cast<tb::cs::ClipType>(desc->cliptype);
    options.compiler = static_cast<tb::cs::Compiler>(desc->compiler);

    auto world = std::make_shared<tb::cs::World>();
    tb::cs::HullCompileStats worldStats;
    world->models.push_back(compileBrushes(
      desc->world_brushes, desc->world_brush_count, false, options, worldStats));
    world->models.back()->name = "world";

    std::size_t skipped = worldStats.skippedBrushes;
    for (uint32_t i = 0; i < desc->entity_count; ++i)
    {
      const scs_brush_entity& ent = desc->entities[i];
      tb::cs::HullCompileStats stats;
      auto model = compileBrushes(ent.brushes, ent.brush_count, true, options, stats);
      skipped += stats.skippedBrushes;
      if (ent.entity_contents == 0)
      {
        model->solid = 4; // SOLID_BSP
        model->skin = 0;
      }
      else
      {
        model->solid = 0; // SOLID_NOT, contents carried in skin
        model->skin = ent.entity_contents;
      }
      model->name = "*" + std::to_string(i + 1);
      world->models.push_back(std::move(model));
    }

    if (info)
    {
      for (int h = 0; h < tb::cs::kNumHulls; ++h)
      {
        info->world_clipnodes[h] = static_cast<uint32_t>(worldStats.clipnodes[static_cast<std::size_t>(h)]);
      }
      info->skipped_brushes = static_cast<uint32_t>(skipped);
      info->build_milliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }

    *out_world = new scs_world{std::move(world)};
    return SCS_OK;
  }
  catch (const tb::cs::HullLimitError&)
  {
    return SCS_ERR_HULL_LIMIT;
  }
  catch (const std::bad_alloc&)
  {
    return SCS_ERR_OUT_OF_MEMORY;
  }
  catch (...)
  {
    return SCS_ERR_INTERNAL;
  }
}

void scs_world_release(scs_world* world)
{
  delete world;
}

void scs_config_defaults(scs_config* config)
{
  if (!config)
  {
    return;
  }
  *config = scs_config{};
  scs_movevars& mv = config->movevars;
  mv.gravity = 800.0f;          // sv_gravity
  mv.stopspeed = 75.0f;         // forced by CS (engine default 100)
  mv.maxspeed = 320.0f;         // sv_maxspeed
  mv.spectatormaxspeed = 500.0f;
  mv.accelerate = 5.0f;         // forced by CS
  mv.airaccelerate = 10.0f;     // sv_airaccelerate
  mv.wateraccelerate = 10.0f;
  mv.friction = 4.0f;           // forced by CS
  mv.edgefriction = 2.0f;
  mv.waterfriction = 1.0f;
  mv.entgravity = 1.0f;
  mv.bounce = 1.0f;
  mv.stepsize = 18.0f;
  mv.maxvelocity = 2000.0f;
  mv.rollangle = 0.0f;
  mv.rollspeed = 200.0f;
  mv.footsteps = 1;
  config->clientmaxspeed = 250.0f; // knife
  config->multiplayer = 1;
  config->quantize_usercmd = 1;
}

scs_context* scs_init(const scs_config* config)
{
  scs_config defaults;
  scs_config_defaults(&defaults);
  try
  {
    return new scs_context(config ? *config : defaults);
  }
  catch (...)
  {
    return nullptr;
  }
}

void scs_shutdown(scs_context* ctx)
{
  delete ctx;
}

scs_result scs_set_world(scs_context* ctx, scs_world* world)
{
  if (!ctx)
  {
    return SCS_ERR_INVALID_ARGUMENT;
  }
  ctx->host.setWorld(world ? world->world : nullptr);
  return SCS_OK;
}

void scs_spawn(scs_context* ctx, const float origin[3], const float angles[3])
{
  if (!ctx || !origin)
  {
    return;
  }
  const float zero[3] = {0, 0, 0};
  ctx->host.spawn(origin, angles ? angles : zero);
}

void scs_step(scs_context* ctx, const scs_usercmd* cmd)
{
  if (ctx && cmd)
  {
    ctx->host.runCommand(*cmd);
  }
}

void scs_set_position(scs_context* ctx, const float origin[3])
{
  if (ctx && origin)
  {
    std::memcpy(ctx->host.state().origin, origin, sizeof(float) * 3);
  }
}

void scs_get_position(const scs_context* ctx, float origin_out[3])
{
  if (ctx && origin_out)
  {
    std::memcpy(origin_out, ctx->host.state().origin, sizeof(float) * 3);
  }
}

void scs_get_state(const scs_context* ctx, scs_player_state* out)
{
  if (ctx && out)
  {
    *out = ctx->host.state();
  }
}

void scs_set_state(scs_context* ctx, const scs_player_state* state)
{
  if (ctx && state)
  {
    ctx->host.setState(*state);
  }
}

int scs_position_is_free(scs_context* ctx, const float origin[3], int usehull)
{
  return ctx && origin && ctx->host.positionIsFree(origin, usehull) ? 1 : 0;
}

void scs_trace_hull(scs_context* ctx, const float start[3], const float end[3], int usehull, scs_trace* out)
{
  if (!ctx || !start || !end || !out)
  {
    return;
  }
  fillTrace(ctx->host.traceHull(start, end, usehull), out);
}

int scs_point_contents(scs_context* ctx, const float point[3])
{
  return ctx && point ? ctx->host.pointContents(point) : SCS_CONTENTS_EMPTY;
}

float scs_fall_damage(float fall_speed)
{
  return tb::cs::fallDamage(fall_speed);
}

// ------------------------------------------------------------- client side
// HLSDK cl_dll/input.cpp semantics.

void scs_key_down(scs_kbutton* b, int keycode)
{
  if (!b)
  {
    return;
  }
  if (keycode == b->down[0] || keycode == b->down[1])
  {
    return; // repeating key
  }
  if (!b->down[0])
  {
    b->down[0] = keycode;
  }
  else if (!b->down[1])
  {
    b->down[1] = keycode;
  }
  else
  {
    return; // three keys down for one button
  }
  if (b->state & 1)
  {
    return; // still down
  }
  b->state |= 1 + 2; // down + impulse down
}

void scs_key_up(scs_kbutton* b, int keycode)
{
  if (!b)
  {
    return;
  }
  if (keycode == -1)
  {
    b->down[0] = b->down[1] = 0;
    b->state = 4; // impulse up
    return;
  }
  if (b->down[0] == keycode)
  {
    b->down[0] = 0;
  }
  else if (b->down[1] == keycode)
  {
    b->down[1] = 0;
  }
  else
  {
    return; // key up without corresponding down
  }
  if (b->down[0] || b->down[1])
  {
    return; // some other key is still holding it down
  }
  if (!(b->state & 1))
  {
    return; // still up
  }
  b->state &= ~1;
  b->state |= 4; // impulse up
}

float scs_key_state(scs_kbutton* key)
{
  if (!key)
  {
    return 0.0f;
  }
  const bool impulsedown = (key->state & 2) != 0;
  const bool impulseup = (key->state & 4) != 0;
  const bool down = (key->state & 1) != 0;
  float val = 0.0f;

  if (impulsedown && !impulseup)
  {
    val = down ? 0.5f : 0.0f; // pressed and held this frame
  }
  if (impulseup && !impulsedown)
  {
    val = 0.0f; // released this frame
  }
  if (!impulsedown && !impulseup)
  {
    val = down ? 1.0f : 0.0f; // held the entire frame
  }
  if (impulsedown && impulseup)
  {
    val = down ? 0.75f : 0.25f; // pressed and released this frame
  }
  key->state &= 1; // clear impulses
  return val;
}

void scs_client_input_defaults(scs_client_input* in)
{
  if (!in)
  {
    return;
  }
  *in = scs_client_input{};
  in->forwardspeed = 400.0f; // CS cl_forwardspeed
  in->backspeed = 400.0f;
  in->sidespeed = 400.0f;
  in->upspeed = 320.0f;
  in->movespeedkey = 0.52f; // CS +speed (walk)
  in->yawspeed = 210.0f;
  in->anglespeedkey = 0.67f;
}

void scs_build_usercmd(scs_client_input* in, float clientmaxspeed, uint8_t msec, scs_usercmd* cmd)
{
  if (!in || !cmd)
  {
    return;
  }
  *cmd = scs_usercmd{};
  cmd->msec = msec;

  // CL_AdjustAngles (yaw only, the client has no +lookup / +lookdown bindings here)
  const float frametime = static_cast<float>(msec) * 0.001f;
  const float speed = (in->speed.state & 1) ? frametime * in->anglespeedkey : frametime;
  in->viewangles[1] -= speed * in->yawspeed * scs_key_state(&in->right);
  in->viewangles[1] += speed * in->yawspeed * scs_key_state(&in->left);
  // anglemod (pm_math.c)
  in->viewangles[1] = static_cast<float>(
    (360.0 / 65536) * (static_cast<int>(in->viewangles[1] * (65536 / 360.0)) & 65535));

  // CL_CreateMove
  cmd->sidemove += in->sidespeed * scs_key_state(&in->moveright);
  cmd->sidemove -= in->sidespeed * scs_key_state(&in->moveleft);
  cmd->upmove += in->upspeed * scs_key_state(&in->moveup);
  cmd->upmove -= in->upspeed * scs_key_state(&in->movedown);
  cmd->forwardmove += in->forwardspeed * scs_key_state(&in->forward);
  cmd->forwardmove -= in->backspeed * scs_key_state(&in->back);

  // clip to maxspeed
  if (clientmaxspeed != 0.0f)
  {
    const float fmov = std::sqrt(
      cmd->forwardmove * cmd->forwardmove + cmd->sidemove * cmd->sidemove
      + cmd->upmove * cmd->upmove);
    if (fmov > clientmaxspeed)
    {
      const float fratio = clientmaxspeed / fmov;
      cmd->forwardmove *= fratio;
      cmd->sidemove *= fratio;
      cmd->upmove *= fratio;
    }
  }

  // +speed (walk) scales the already clipped move: CS walks at 0.52 of the weapon's
  // speed (130 with a knife), unlike Half-Life, which scales before clipping.
  if (in->speed.state & 1)
  {
    cmd->forwardmove *= in->movespeedkey;
    cmd->sidemove *= in->movespeedkey;
    cmd->upmove *= in->movespeedkey;
  }

  // CL_ButtonBits(1): "state & 3" sees presses that were released within the
  // same frame, which is why +jump/-jump on the mouse wheel works.
  uint16_t bits = 0;
  const auto button = [&](scs_kbutton& b, uint16_t bit) {
    if (b.state & 3)
    {
      bits = static_cast<uint16_t>(bits | bit);
    }
    b.state &= ~2;
  };
  button(in->attack, SCS_IN_ATTACK);
  button(in->duck, SCS_IN_DUCK);
  button(in->jump, SCS_IN_JUMP);
  button(in->forward, SCS_IN_FORWARD);
  button(in->back, SCS_IN_BACK);
  button(in->use, SCS_IN_USE);
  button(in->moveleft, SCS_IN_MOVELEFT);
  button(in->moveright, SCS_IN_MOVERIGHT);
  button(in->speed, SCS_IN_RUN);
  cmd->buttons = bits;

  std::memcpy(cmd->viewangles, in->viewangles, sizeof(cmd->viewangles));
}

uint32_t scs_tick_clock_advance(scs_tick_clock* clock, double dt_seconds, uint8_t* msec_out)
{
  if (!clock)
  {
    return 0;
  }
  const uint32_t hz = clock->tick_hz ? clock->tick_hz : 100u;
  const auto msec = static_cast<uint8_t>(std::max(1u, std::min(255u, 1000u / hz)));
  const double step = static_cast<double>(msec) / 1000.0;
  if (msec_out)
  {
    *msec_out = msec;
  }

  clock->accumulator += std::max(0.0, dt_seconds);
  auto ticks = static_cast<uint32_t>(clock->accumulator / step);
  clock->accumulator -= ticks * step;
  constexpr uint32_t kMaxCatchUp = 25; // avoid a spiral of death after a stall
  if (ticks > kMaxCatchUp)
  {
    ticks = kMaxCatchUp;
    clock->accumulator = 0.0;
  }
  return ticks;
}

} // extern "C"
