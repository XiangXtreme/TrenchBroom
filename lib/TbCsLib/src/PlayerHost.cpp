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

 The per-command player setup is ported from ReHLDS
 (https://github.com/rehlds/ReHLDS), MIT licensed, Copyright (c) 2014-2025 ReHLDS
 Contributors. Small helpers (info strings, memfgets) are ported from Xash3D FWGS
 (https://github.com/FWGS/xash3d-fwgs), licensed under the GNU GPL version 3.
 */

#include "PlayerHost.h"

#include "PmTraceEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace tb::cs {
namespace {

constexpr int kFlClient = 1 << 3;         // FL_CLIENT
constexpr int kFlBaseVelocity = 1 << 22;  // FL_BASEVELOCITY
constexpr float kViewHeight = 17.0f;      // CS VEC_VIEW
constexpr float kPhysentRadius = 256.0f;  // SV_SetupPMove gathers origin +/- 256

// pm_shared keeps global state (pmove pointer, stuck tables, texture table),
// so every entry into it is serialised. Recursive: queries may nest in steps.
std::recursive_mutex& pmMutex()
{
  static std::recursive_mutex mutex;
  return mutex;
}

thread_local PlayerHost* tActiveHost = nullptr;

// Binds a host as the target of the C callbacks for the current scope.
class ActiveHostScope
{
public:
  explicit ActiveHostScope(PlayerHost* host)
    : lock_(pmMutex())
    , previous_(tActiveHost)
  {
    tActiveHost = host;
  }
  ~ActiveHostScope() { tActiveHost = previous_; }

  ActiveHostScope(const ActiveHostScope&) = delete;
  ActiveHostScope& operator=(const ActiveHostScope&) = delete;

private:
  std::scoped_lock<std::recursive_mutex> lock_;
  PlayerHost* previous_;
};

playermove_t* activePm()
{
  return tActiveHost->pmove();
}

void copyVec(const float* in, float* out)
{
  out[0] = in[0];
  out[1] = in[1];
  out[2] = in[2];
}

// ------------------------------------------------- engine function table
// Signatures follow pm_defs.h (thirdparty/regamedll) exactly.

const char* infoValueForKey(const char* s, const char* key)
{
  // Info_ValueForKey: "\key\value\key\value". Rotating buffers like the engine.
  static thread_local char values[4][256];
  static thread_local int which = 0;
  char pkey[256];

  which = (which + 1) % 4;
  if (*s == '\\')
  {
    ++s;
  }
  for (;;)
  {
    char* o = pkey;
    while (*s != '\\')
    {
      if (!*s)
      {
        return "";
      }
      if (o < pkey + sizeof(pkey) - 1)
      {
        *o++ = *s;
      }
      ++s;
    }
    *o = 0;
    ++s;

    o = values[which];
    while (*s != '\\' && *s)
    {
      if (o < values[which] + sizeof(values[which]) - 1)
      {
        *o++ = *s;
      }
      ++s;
    }
    *o = 0;

    if (!std::strcmp(key, pkey))
    {
      return values[which];
    }
    if (!*s)
    {
      return "";
    }
    ++s;
  }
}

void particle(float*, int, float, int, int) {}

int testPlayerPosition(float* pos, pmtrace_t* ptrace)
{
  return pm::testPlayerPosition(activePm(), pos, ptrace, nullptr);
}

void conNPrintf(int, char*, ...) {}
void conDPrintf(char*, ...) {}
void conPrintf(char*, ...) {}

double sysFloatTime()
{
  // PM_CheckStuck throttles on this clock. Simulation time keeps replays
  // deterministic; on a real server it advances at the same rate.
  return tActiveHost->simTime();
}

void stuckTouch(int hitent, pmtrace_t* tr)
{
  pm::stuckTouch(activePm(), hitent, tr);
}

int pointContents(float* p, int* truecontents)
{
  return pm::pointContents(activePm(), p, truecontents);
}

int truePointContents(float* p)
{
  return pm::truePointContents(activePm(), p);
}

int hullPointContents(struct hull_s* hull, int num, float* p)
{
  return pm::hullPointContents(reinterpret_cast<const Hull*>(hull), num, p);
}

pmtrace_t playerTrace(float* start, float* end, int traceFlags, int ignore_pe)
{
  playermove_t* pm = activePm();
  return pm::playerTrace(
    pm, start, end, traceFlags, pm->numphysent, pm->physents, ignore_pe, nullptr);
}

pmtrace_t* traceLine(float* start, float* end, int flags, int usehull, int ignore_pe)
{
  return pm::traceLine(activePm(), start, end, flags, usehull, ignore_pe, nullptr);
}

int32 randomLong(int32 lo, int32 hi)
{
  return static_cast<int32>(tActiveHost->randomLong(lo, hi));
}

float randomFloat(float lo, float hi)
{
  return tActiveHost->randomFloat(lo, hi);
}

int getModelType(struct model_s*)
{
  return 0; // mod_brush: TbCsLib only ever hands out BSP models
}

void getModelBounds(struct model_s* mod, float* mins, float* maxs)
{
  const auto* model = reinterpret_cast<const BrushModel*>(mod);
  copyVec(model->mins, mins);
  copyVec(model->maxs, maxs);
}

void* hullForBsp(physent_t* pe, float* offset)
{
  return pm::hullForBsp(pe, activePm(), offset);
}

float traceModel(physent_t* pe, float* start, float* end, trace_t* trace)
{
  return pm::traceModel(activePm(), pe, start, end, trace);
}

// No filesystem: PM_InitTextureTypes sees no sound/materials.txt and every
// surface reads as concrete. Only footstep sounds depend on it.
int comFileSize(char*)
{
  return -1;
}

byte* comLoadFile(char*, int, int*)
{
  return nullptr;
}

void comFreeFile(void*) {}

char* memfgets(byte* pMemFile, int fileSize, int* pFilePos, char* pBuffer, int bufferSize)
{
  // Q_memfgets from the engine.
  if (!pMemFile || !pBuffer || *pFilePos >= fileSize)
  {
    return nullptr;
  }
  int i = *pFilePos;
  const int last = std::min(fileSize, i + bufferSize - 1);
  while (i < last)
  {
    if (pMemFile[i++] == '\n')
    {
      break;
    }
  }
  if (i == *pFilePos)
  {
    return nullptr;
  }
  const int size = i - *pFilePos;
  std::memcpy(pBuffer, pMemFile + *pFilePos, static_cast<std::size_t>(size));
  if (size < bufferSize)
  {
    pBuffer[size] = 0;
  }
  *pFilePos = i;
  return pBuffer;
}

void playSound(int channel, const char* sample, float volume, float attenuation, int fFlags, int pitch)
{
  tActiveHost->playSound(channel, sample, volume, attenuation, fFlags, pitch);
}

const char* traceTexture(int, float*, float*)
{
  return nullptr; // no texture info in clip hulls; PM_CatagorizeTextureType handles NULL
}

void playbackEventFull(int, int, unsigned short, float, float*, float*, float, float, int, int, int, int) {}

pmtrace_t playerTraceEx(float* start, float* end, int traceFlags, int (*filter)(physent_t*))
{
  playermove_t* pm = activePm();
  return pm::playerTrace(pm, start, end, traceFlags, pm->numphysent, pm->physents, -1, filter);
}

int testPlayerPositionEx(float* pos, pmtrace_t* ptrace, int (*filter)(physent_t*))
{
  return pm::testPlayerPosition(activePm(), pos, ptrace, filter);
}

pmtrace_t* traceLineEx(float* start, float* end, int flags, int usehull, int (*filter)(physent_t*))
{
  return pm::traceLine(activePm(), start, end, flags, usehull, -1, filter);
}

// CS hull bounds as returned by the game's GetHullBounds; hull 3 keeps the
// engine default because CS does not report it.
constexpr float kPlayerMins[4][3] = {{-16, -16, -36}, {-16, -16, -18}, {0, 0, 0}, {-32, -32, -32}};
constexpr float kPlayerMaxs[4][3] = {{16, 16, 36}, {16, 16, 18}, {0, 0, 0}, {32, 32, 32}};

// MSG_WriteBitAngle(16) followed by the delta reader: 360/65536 steps in [0, 360).
float quantizeAngle(float angle)
{
  constexpr unsigned kShift = 1u << 16;
  auto d = static_cast<std::int64_t>(kShift * std::fmod(static_cast<double>(angle), 360.0)) / 360;
  d &= kShift - 1;
  return static_cast<float>(d) * (360.0f / static_cast<float>(kShift));
}

// DT_SIGNED | DT_FLOAT, 12 bits, multiplier 1.0
float quantizeMove(float value)
{
  int i = static_cast<int>(static_cast<double>(value) * 1.0);
  i = std::clamp(i, -2048, 2047);
  return static_cast<float>(i);
}

std::once_flag gPmInitOnce;

} // namespace

PlayerHost::PlayerHost(const scs_config& config)
  : pm_(std::make_unique<playermove_t>())
  , config_(config)
{
  std::memset(pm_.get(), 0, sizeof(playermove_t));

  const scs_movevars& mv = config_.movevars;
  movevars_.gravity = mv.gravity;
  movevars_.stopspeed = mv.stopspeed;
  movevars_.maxspeed = mv.maxspeed;
  movevars_.spectatormaxspeed = mv.spectatormaxspeed;
  movevars_.accelerate = mv.accelerate;
  movevars_.airaccelerate = mv.airaccelerate;
  movevars_.wateraccelerate = mv.wateraccelerate;
  movevars_.friction = mv.friction;
  movevars_.edgefriction = mv.edgefriction;
  movevars_.waterfriction = mv.waterfriction;
  movevars_.entgravity = mv.entgravity;
  movevars_.bounce = mv.bounce;
  movevars_.stepsize = mv.stepsize;
  movevars_.maxvelocity = mv.maxvelocity;
  movevars_.zmax = 4096.0f;
  movevars_.footsteps = mv.footsteps;
  movevars_.rollangle = mv.rollangle;
  movevars_.rollspeed = mv.rollspeed;

  // SV_InitClientMove
  pm_->server = 1;
  pm_->movevars = &movevars_;
  pm_->runfuncs = 0;
  std::memcpy(pm_->player_mins, kPlayerMins, sizeof(kPlayerMins));
  std::memcpy(pm_->player_maxs, kPlayerMaxs, sizeof(kPlayerMaxs));
  bindEngineFunctions();

  ActiveHostScope scope(this);
  std::call_once(gPmInitOnce, [this] { PM_Init(pm_.get()); });

  const float origin[3] = {0, 0, 0};
  const float angles[3] = {0, 0, 0};
  spawn(origin, angles);
}

PlayerHost::~PlayerHost() = default;

void PlayerHost::bindEngineFunctions()
{
  playermove_t& pm = *pm_;
  pm.PM_Info_ValueForKey = infoValueForKey;
  pm.PM_Particle = particle;
  pm.PM_TestPlayerPosition = testPlayerPosition;
  pm.Con_NPrintf = conNPrintf;
  pm.Con_DPrintf = conDPrintf;
  pm.Con_Printf = conPrintf;
  pm.Sys_FloatTime = sysFloatTime;
  pm.PM_StuckTouch = stuckTouch;
  pm.PM_PointContents = tb::cs::pointContents;
  pm.PM_TruePointContents = truePointContents;
  pm.PM_HullPointContents = hullPointContents;
  pm.PM_PlayerTrace = playerTrace;
  pm.PM_TraceLine = traceLine;
  pm.RandomLong = tb::cs::randomLong;
  pm.RandomFloat = tb::cs::randomFloat;
  pm.PM_GetModelType = getModelType;
  pm.PM_GetModelBounds = getModelBounds;
  pm.PM_HullForBsp = hullForBsp;
  pm.PM_TraceModel = traceModel;
  pm.COM_FileSize = comFileSize;
  pm.COM_LoadFile = comLoadFile;
  pm.COM_FreeFile = comFreeFile;
  pm.memfgets = memfgets;
  pm.PM_PlaySound = tb::cs::playSound;
  pm.PM_TraceTexture = traceTexture;
  pm.PM_PlaybackEventFull = playbackEventFull;
  pm.PM_PlayerTraceEx = playerTraceEx;
  pm.PM_TestPlayerPositionEx = testPlayerPositionEx;
  pm.PM_TraceLineEx = traceLineEx;
}

void PlayerHost::setWorld(std::shared_ptr<const World> world)
{
  ActiveHostScope scope(this);
  world_ = std::move(world);
  pm_->numphysent = pm_->nummoveent = pm_->numvisent = 0;
}

void PlayerHost::spawn(const float origin[3], const float angles[3])
{
  // The relevant part of CBasePlayer::Spawn + PutInServer.
  ent_ = scs_player_state{};
  copyVec(origin, ent_.origin);
  ent_.origin[2] += 1.0f; // spawn points are offset 1 unit up by the game dll
  copyVec(angles, ent_.v_angle);
  ent_.v_angle[2] = 0.0f;
  ent_.view_ofs[2] = kViewHeight;
  ent_.flags = kFlClient;
  ent_.movetype = MOVETYPE_WALK;
  ent_.friction = 1.0f;
  ent_.gravity = 1.0f;
  ent_.health = 100.0f;
  ent_.maxspeed = config_.clientmaxspeed;
  ent_.watertype = CONTENTS_EMPTY;
  ent_.ground_entity = -1;
  resetPersistentMoveState();
}

void PlayerHost::setState(const scs_player_state& state)
{
  ent_ = state;
  resetPersistentMoveState();
}

void PlayerHost::resetPersistentMoveState()
{
  // The engine never reloads these from the player between commands; they persist in
  // playermove_t. A teleport (spawn or restored state) starts them from the new state.
  pm_->onground = -1;
  pm_->waterlevel = ent_.waterlevel;
  pm_->watertype = ent_.watertype;
}

void PlayerHost::runCommand(const scs_usercmd& in)
{
  usercmd_t cmd{};
  cmd.msec = in.msec;
  cmd.buttons = in.buttons;
  for (int i = 0; i < 3; ++i)
  {
    cmd.viewangles[i] = config_.quantize_usercmd ? quantizeAngle(in.viewangles[i]) : in.viewangles[i];
  }
  cmd.forwardmove = config_.quantize_usercmd ? quantizeMove(in.forwardmove) : in.forwardmove;
  cmd.sidemove = config_.quantize_usercmd ? quantizeMove(in.sidemove) : in.sidemove;
  cmd.upmove = config_.quantize_usercmd ? quantizeMove(in.upmove) : in.upmove;

  ActiveHostScope scope(this);
  runCommandChopped(cmd);
}

void PlayerHost::runCommandChopped(usercmd_t cmd)
{
  // SV_RunCmd: chop up very long commands (note: odd milliseconds are lost,
  // the engine does the same).
  if (cmd.msec > 50)
  {
    const int oldmsec = cmd.msec;
    cmd.msec = static_cast<byte>(oldmsec / 2);
    runCommandOnce(cmd);
    cmd.msec = static_cast<byte>(oldmsec / 2);
    cmd.impulse = 0;
    runCommandOnce(cmd);
    return;
  }
  runCommandOnce(cmd);
}

void PlayerHost::runCommandOnce(const usercmd_t& cmd)
{
  if (!world_ || world_->models.empty())
  {
    return;
  }

  // SV_RunCmd
  const auto frametime = static_cast<float>(cmd.msec * 0.001);
  timebase_ += static_cast<double>(frametime);

  checkMovingGround(frametime);

  copyVec(ent_.v_angle, pm_->oldangles);
  copyVec(cmd.viewangles, ent_.v_angle); // fixangle is never set in the editor

  // CBasePlayer::PreThink. Besides the fall speed nothing in it feeds pm_shared
  // (maxspeed is set through the state).
  if (!(ent_.flags & FL_ONGROUND))
  {
    ent_.server_fall_velocity = -ent_.velocity[2];
  }

  setupPMove(cmd);
  PM_Move(pm_.get(), 1);
  finishPMove();

  // Touch processing (triggers) would run here.
  pm_->numtouch = 0;

  postThinkFallDamage();
}

void PlayerHost::postThinkFallDamage()
{
  // CBasePlayer::PostThink: landing hard enough deals damage, except in water. The
  // editor is in god mode, so the damage is only recorded.
  if (!(ent_.flags & FL_ONGROUND))
  {
    return;
  }
  if (
    ent_.server_fall_velocity >= kFallPunchThreshold && ent_.watertype != CONTENTS_WATER
    && ent_.server_fall_velocity > kMaxSafeFallSpeed)
  {
    const float damage = fallDamage(ent_.server_fall_velocity);
    if (damage > 0.0f)
    {
      ent_.last_fall_damage = damage;
      ent_.last_fall_speed = ent_.server_fall_velocity;
      ++ent_.fall_count;
    }
  }
  ent_.server_fall_velocity = 0.0f;
}

float fallDamage(const float fallSpeed)
{
  // CHalfLifeMultiplay::FlPlayerFallDamage
  constexpr float kDamageForFallSpeed = 100.0f / (kMaxFatalFallSpeed - kMaxSafeFallSpeed);
  return fallSpeed > kMaxSafeFallSpeed
           ? (fallSpeed - kMaxSafeFallSpeed) * kDamageForFallSpeed * 1.25f
           : 0.0f;
}

void PlayerHost::checkMovingGround(float frametime)
{
  // SV_CheckMovingGround. No conveyors yet, so only the momentum part applies.
  if (!(ent_.flags & kFlBaseVelocity))
  {
    const float scale = frametime * 0.5f + 1.0f;
    for (int i = 0; i < 3; ++i)
    {
      ent_.velocity[i] = scale * ent_.basevelocity[i] + ent_.velocity[i];
      ent_.basevelocity[i] = 0.0f;
    }
  }
  ent_.flags &= ~kFlBaseVelocity;
}

void PlayerHost::setupPMove(const usercmd_t& cmd)
{
  // The playermove_t setup of SV_RunCmd, field for field. oldangles was set from the
  // previous view angles, and onground, waterlevel and watertype are deliberately not
  // touched: the engine keeps them from the previous command.
  playermove_t& pm = *pm_;
  pm.server = 1;
  pm.multiplayer = config_.multiplayer ? 1 : 0;
  pm.time = static_cast<float>(1000.0 * timebase_);
  pm.usehull = (ent_.flags & FL_DUCKING) == FL_DUCKING ? 1 : 0;
  pm.maxspeed = movevars_.maxspeed;
  pm.clientmaxspeed = ent_.maxspeed;
  pm.flDuckTime = ent_.duck_time;
  pm.bInDuck = ent_.in_duck;
  pm.flTimeStepSound = ent_.time_step_sound;
  pm.iStepLeft = ent_.step_left;
  pm.flFallVelocity = ent_.fall_velocity;
  pm.flSwimTime = ent_.swim_time;
  pm.oldbuttons = ent_.oldbuttons;
  pm.physinfo[0] = '\0';
  copyVec(ent_.velocity, pm.velocity);
  copyVec(ent_.movedir, pm.movedir);
  copyVec(ent_.v_angle, pm.angles);
  copyVec(ent_.basevelocity, pm.basevelocity);
  copyVec(ent_.view_ofs, pm.view_ofs);
  copyVec(ent_.punchangle, pm.punchangle);
  pm.deadflag = ent_.deadflag;
  pm.effects = ent_.effects;
  pm.gravity = ent_.gravity;
  pm.friction = ent_.friction;
  pm.spectator = 0;
  pm.waterjumptime = ent_.teleport_time;
  pm.cmd = cmd;
  pm.dead = ent_.health <= 0.0f ? 1 : 0;
  pm.movetype = ent_.movetype;
  pm.flags = ent_.flags;
  pm.player_index = 0;
  pm.iuser1 = ent_.iuser[0];
  pm.iuser2 = ent_.iuser[1];
  pm.iuser3 = ent_.iuser[2];
  pm.iuser4 = ent_.iuser[3];
  pm.fuser1 = ent_.fuser[0];
  pm.fuser2 = ent_.fuser[1];
  pm.fuser3 = ent_.fuser[2];
  pm.fuser4 = ent_.fuser[3];
  copyVec(ent_.vuser[0], pm.vuser1);
  copyVec(ent_.vuser[1], pm.vuser2);
  copyVec(ent_.vuser[2], pm.vuser3);
  copyVec(ent_.vuser[3], pm.vuser4);
  copyVec(ent_.origin, pm.origin);
  addPhysents(pm.origin, kPhysentRadius);
  pm.frametime = static_cast<float>(cmd.msec * 0.001);
  pm.runfuncs = 1;
}

void PlayerHost::finishPMove()
{
  // The copy back of SV_RunCmd, field for field.
  const playermove_t& pm = *pm_;
  ent_.deadflag = pm.deadflag;
  ent_.effects = pm.effects;
  ent_.teleport_time = pm.waterjumptime;
  ent_.waterlevel = pm.waterlevel;
  ent_.watertype = pm.watertype;
  ent_.flags = pm.flags;
  ent_.friction = pm.friction;
  ent_.movetype = pm.movetype;
  ent_.maxspeed = pm.clientmaxspeed;
  ent_.step_left = pm.iStepLeft;
  copyVec(pm.view_ofs, ent_.view_ofs);
  copyVec(pm.movedir, ent_.movedir);
  copyVec(pm.punchangle, ent_.punchangle);

  if (pm.onground == -1)
  {
    ent_.flags &= ~FL_ONGROUND;
    ent_.ground_entity = -1;
  }
  else
  {
    ent_.flags |= FL_ONGROUND;
    ent_.ground_entity = pm.physents[pm.onground].info;
  }

  copyVec(pm.origin, ent_.origin);
  copyVec(pm.velocity, ent_.velocity);
  copyVec(pm.basevelocity, ent_.basevelocity);
  copyVec(pm.angles, ent_.v_angle);
  ent_.in_duck = pm.bInDuck;
  // entvars keeps the duck and swim timers as truncated whole milliseconds
  ent_.duck_time = static_cast<float>(static_cast<int>(pm.flDuckTime));
  ent_.time_step_sound = pm.flTimeStepSound;
  ent_.fall_velocity = pm.flFallVelocity;
  ent_.swim_time = static_cast<float>(static_cast<int>(pm.flSwimTime));
  ent_.oldbuttons = pm.cmd.buttons;
  ent_.iuser[0] = pm.iuser1;
  ent_.iuser[1] = pm.iuser2;
  ent_.iuser[2] = pm.iuser3;
  ent_.iuser[3] = pm.iuser4;
  ent_.fuser[0] = pm.fuser1;
  ent_.fuser[1] = pm.fuser2;
  ent_.fuser[2] = pm.fuser3;
  ent_.fuser[3] = pm.fuser4;
  copyVec(pm.vuser1, ent_.vuser[0]);
  copyVec(pm.vuser2, ent_.vuser[1]);
  copyVec(pm.vuser3, ent_.vuser[2]);
  copyVec(pm.vuser4, ent_.vuser[3]);
  pm_->runfuncs = 0;
}

void PlayerHost::addPhysents(const float* center, float radius)
{
  // SV_AddLinksToPM: the world, then every brush entity. All of them become visents;
  // those that collide or carry contents become physents if they touch the box
  // around the player, except ladders, which only go into moveents.
  playermove_t& pm = *pm_;
  pm.numphysent = pm.nummoveent = pm.numvisent = 0;
  if (!world_ || world_->models.empty())
  {
    return;
  }

  const auto& models = world_->models;
  const auto copyModelToPhysent = [&](physent_t& pe, const std::size_t index) {
    const BrushModel& model = *models[index];
    std::memset(&pe, 0, sizeof(pe));
    std::snprintf(pe.name, sizeof(pe.name), "%s", model.name.c_str());
    pe.info = static_cast<int>(index);
    pe.model = reinterpret_cast<struct model_s*>(const_cast<BrushModel*>(&model));
    pe.rendermode = kRenderNormal;
  };

  physent_t& world = pm.physents[0];
  copyModelToPhysent(world, 0);
  world.solid = SOLID_BSP;
  world.movetype = MOVETYPE_NONE;
  world.takedamage = 1;
  pm.numphysent = 1;
  pm.visents[0] = world;
  pm.numvisent = 1;

  for (std::size_t i = 1; i < models.size(); ++i)
  {
    const BrushModel& model = *models[i];
    if (pm.numvisent >= MAX_PHYSENTS)
    {
      break;
    }
    physent_t& ve = pm.visents[pm.numvisent++];
    copyModelToPhysent(ve, i);
    ve.solid = model.solid;
    ve.skin = model.skin;
    ve.movetype = MOVETYPE_PUSH;

    if (model.solid == SOLID_NOT && model.skin == 0)
    {
      continue;
    }
    if (radius >= 0.0f)
    {
      bool touches = true;
      for (int k = 0; k < 3; ++k)
      {
        touches = touches && center[k] - radius <= model.maxs[k]
                  && center[k] + radius >= model.mins[k];
      }
      if (!touches)
      {
        continue;
      }
    }

    if (model.solid != SOLID_NOT || model.skin != CONTENTS_LADDER)
    {
      if (pm.numphysent < MAX_PHYSENTS)
      {
        pm.physents[pm.numphysent++] = ve;
      }
    }
    else if (pm.nummoveent < MAX_MOVEENTS)
    {
      pm.moveents[pm.nummoveent++] = ve;
    }
  }
}

bool PlayerHost::positionIsFree(const float origin[3], int usehull)
{
  ActiveHostScope scope(this);
  if (!world_ || world_->models.empty())
  {
    return true;
  }
  const int oldUsehull = pm_->usehull;
  pm_->usehull = std::clamp(usehull, 0, 3);
  addPhysents(origin, kPhysentRadius);
  const bool free = pm::testPlayerPosition(pm_.get(), origin, nullptr, nullptr) == -1;
  pm_->usehull = oldUsehull;
  return free;
}

pmtrace_t PlayerHost::traceHull(const float start[3], const float end[3], int usehull)
{
  ActiveHostScope scope(this);
  pmtrace_t tr{};
  tr.fraction = 1.0f;
  tr.ent = -1;
  copyVec(end, tr.endpos);
  if (!world_ || world_->models.empty())
  {
    return tr;
  }
  const int oldUsehull = pm_->usehull;
  pm_->usehull = std::clamp(usehull, 0, 3);
  addPhysents(start, -1.0f);
  tr = pm::playerTrace(pm_.get(), start, end, PM_NORMAL, pm_->numphysent, pm_->physents, -1, nullptr);
  pm_->usehull = oldUsehull;
  return tr;
}

int PlayerHost::pointContents(const float point[3])
{
  ActiveHostScope scope(this);
  if (!world_ || world_->models.empty())
  {
    return CONTENTS_EMPTY;
  }
  addPhysents(point, kPhysentRadius);
  return pm::pointContents(pm_.get(), point, nullptr);
}

long PlayerHost::randomLong(long lo, long hi)
{
  if (hi <= lo)
  {
    return lo;
  }
  return std::uniform_int_distribution<long>(lo, hi)(rng_);
}

float PlayerHost::randomFloat(float lo, float hi)
{
  if (hi <= lo)
  {
    return lo;
  }
  return std::uniform_real_distribution<float>(lo, hi)(rng_);
}

void PlayerHost::playSound(
  int channel, const char* sample, float volume, float attenuation, int flags, int pitch)
{
  if (config_.sound_fn)
  {
    config_.sound_fn(config_.sound_user, channel, sample, volume, attenuation, flags, pitch);
  }
}

} // namespace tb::cs
