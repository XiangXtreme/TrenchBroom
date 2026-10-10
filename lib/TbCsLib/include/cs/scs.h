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

/*
 * TbCsLib - embedded GoldSrc / Counter-Strike 1.6 player physics runtime.
 *
 * Public C interface. This header is self-contained: it does not pull in any
 * game or engine headers, so editor code (TrenchBroom) never sees engine types.
 *
 * Threading: pm_shared keeps process-global state (the `pmove` pointer, stuck
 * tables, texture tables). TbCsLib serialises every call that enters pm_shared
 * behind an internal mutex, so multiple contexts are safe but never parallel.
 *
 * Coordinate system and units are GoldSrc's: Z up, 1 unit = 1 inch, angles in
 * degrees as (pitch, yaw, roll).
 */
#ifndef SCS_SCS_H
#define SCS_SCS_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && defined(SCS_SHARED)
#  ifdef SCS_BUILDING
#    define SCS_API __declspec(dllexport)
#  else
#    define SCS_API __declspec(dllimport)
#  endif
#elif defined(SCS_SHARED)
#  define SCS_API __attribute__((visibility("default")))
#else
#  define SCS_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SCS_API_VERSION 1

typedef struct scs_world scs_world;     /* immutable, ref-counted collision world */
typedef struct scs_context scs_context; /* one simulated player */

typedef enum scs_result {
  SCS_OK = 0,
  SCS_ERR_INVALID_ARGUMENT = -1,
  SCS_ERR_HULL_LIMIT = -2, /* runaway hull size (sanity cap of 2^24 clipnodes) */
  SCS_ERR_OUT_OF_MEMORY = -3,
  SCS_ERR_INTERNAL = -4
} scs_result;

/* ------------------------------------------------------------------------ */
/* Constants (values are identical to the GoldSrc ones)                     */
/* ------------------------------------------------------------------------ */

/* BSP contents */
#define SCS_CONTENTS_EMPTY (-1)
#define SCS_CONTENTS_SOLID (-2)
#define SCS_CONTENTS_WATER (-3)
#define SCS_CONTENTS_SLIME (-4)
#define SCS_CONTENTS_LAVA (-5)
#define SCS_CONTENTS_CLIP (-8) /* solid for player hulls 1-3 only (CLIP texture) */
#define SCS_CONTENTS_LADDER (-16)

/* usercmd_t::buttons */
#define SCS_IN_ATTACK (1 << 0)
#define SCS_IN_JUMP (1 << 1)
#define SCS_IN_DUCK (1 << 2)
#define SCS_IN_FORWARD (1 << 3)
#define SCS_IN_BACK (1 << 4)
#define SCS_IN_USE (1 << 5)
#define SCS_IN_MOVELEFT (1 << 9)
#define SCS_IN_MOVERIGHT (1 << 10)
#define SCS_IN_RUN (1 << 12)

/* entvars_t::flags bits that matter to movement */
#define SCS_FL_ONGROUND (1 << 9)
#define SCS_FL_DUCKING (1 << 14)

/* movetype */
#define SCS_MOVETYPE_NONE 0
#define SCS_MOVETYPE_WALK 3
#define SCS_MOVETYPE_FLY 5
#define SCS_MOVETYPE_NOCLIP 8

/* player hull indices (pmove->usehull) */
#define SCS_HULL_STANDING 0
#define SCS_HULL_DUCKED 1
#define SCS_HULL_POINT 2
#define SCS_HULL_LARGE 3

/* ------------------------------------------------------------------------ */
/* In-memory collision world                                                */
/* ------------------------------------------------------------------------ */

/* A brush face plane. Points p with dot(normal, p) <= dist are inside the
 * brush, i.e. normals point OUT of the brush (TrenchBroom's convention).
 * Normals must be unit length. Doubles, because that is what the editor has. */
typedef struct scs_plane {
  double normal[3];
  double dist;
  uint32_t flags; /* SCS_PLANE_* */
} scs_plane;

/* hlcsg BEVEL / CLIPBEVEL face: not pushed out when expanding clip hulls. */
#define SCS_PLANE_BEVEL (1u << 0)

/* A convex brush: the intersection of the back half-spaces of its planes. */
typedef struct scs_brush {
  const scs_plane* planes;
  uint32_t plane_count;
  /* SCS_CONTENTS_SOLID, _CLIP, _WATER, _SLIME or _LAVA. Ignored (forced to
   * SOLID) for brushes of a brush entity, see scs_brush_entity. */
  int32_t contents;
  /* Bit h set: the brush exists in BSP hull h (before the contents rules).
   * 0 means all hulls. CLIPHULLn brushes use 1 << n, NOCLIP brushes 1 << 0. */
  uint32_t hull_mask;
} scs_brush;

/* A brush entity (func_wall, func_ladder, func_water, ...). It becomes its own
 * BSP model with its own four hulls, exactly like hlcsg/hlbsp would emit. */
typedef struct scs_brush_entity {
  const scs_brush* brushes;
  uint32_t brush_count;
  /* 0                    -> solid (func_wall, func_illusionary excluded)
   * SCS_CONTENTS_LADDER  -> func_ladder (SOLID_NOT, skin = CONTENTS_LADDER)
   * SCS_CONTENTS_WATER.. -> func_water  (SOLID_NOT, skin = contents)     */
  int32_t entity_contents;
} scs_brush_entity;

/* hlcsg -cliptype: how brushes grow into the player clip hulls. The values
 * are VHLT's, also used by the info_compile_parameters "cliptype" key. */
#define SCS_CLIPTYPE_SMALLEST 0
#define SCS_CLIPTYPE_NORMALIZED 1
#define SCS_CLIPTYPE_SIMPLE 2  /* VHLT default */
#define SCS_CLIPTYPE_PRECISE 3
#define SCS_CLIPTYPE_LEGACY 4  /* ZHLT default, what most CS 1.6 maps use */

/* Whose hlcsg to imitate. They differ in simple, normalized and precise. */
#define SCS_COMPILER_VHLT 0
#define SCS_COMPILER_ZHLT 1

typedef struct scs_world_desc {
  const scs_brush* world_brushes;
  uint32_t world_brush_count;
  const scs_brush_entity* entities;
  uint32_t entity_count;
  int32_t cliptype; /* SCS_CLIPTYPE_* */
  int32_t compiler; /* SCS_COMPILER_* */
} scs_world_desc;

typedef struct scs_world_info {
  /* Per BSP hull: 0 point, 1 stand, 2 large, 3 duck. TbCsLib has no 16-bit
   * limit, but a real .bsp may hold at most 32767 clipnodes in total; use this
   * as an early warning only, hlbsp's tree differs from TbCsLib's. */
  uint32_t world_clipnodes[4];
  uint32_t skipped_brushes;    /* degenerate / open brushes that were ignored */
  double build_milliseconds;
} scs_world_info;

/* Compile brushes into GoldSrc clip hulls entirely in memory (no .map/.bsp).
 * On success *out_world holds one reference. `info` may be NULL. */
SCS_API scs_result scs_world_build(const scs_world_desc* desc,
                                   scs_world** out_world,
                                   scs_world_info* info);
SCS_API void scs_world_release(scs_world* world);

/* ------------------------------------------------------------------------ */
/* Physics context                                                          */
/* ------------------------------------------------------------------------ */

/* movevars_t subset that pm_shared reads. */
typedef struct scs_movevars {
  float gravity, stopspeed, maxspeed, spectatormaxspeed;
  float accelerate, airaccelerate, wateraccelerate;
  float friction, edgefriction, waterfriction;
  float entgravity, bounce, stepsize, maxvelocity;
  float rollangle, rollspeed;
  int32_t footsteps;
} scs_movevars;

typedef void (*scs_sound_fn)(void* user, int channel, const char* sample,
                             float volume, float attenuation, int flags, int pitch);

typedef struct scs_config {
  scs_movevars movevars;
  /* pev->maxspeed set by the held weapon: 250 knife/pistols, 260 scout... */
  float clientmaxspeed;
  /* svs.maxclients > 1. CS always runs multiplayer; when 0, pm_shared resets
   * friction to 1 after every move and onground is not reset per command. */
  int32_t multiplayer;
  /* Round-trip usercmd through the GoldSrc delta encoding (16-bit angles,
   * 12-bit signed integer move values) like a real client->server hop. */
  int32_t quantize_usercmd;
  /* Optional footstep / jump sound sink (runs on the stepping thread). */
  scs_sound_fn sound_fn;
  void* sound_user;
} scs_config;

/* The exact CS 1.6 server defaults (ReGameDLL forces accel/friction/stopspeed). */
SCS_API void scs_config_defaults(scs_config* config);

/* One GoldSrc usercmd_t, minus the fields pm_shared never reads. */
typedef struct scs_usercmd {
  uint8_t msec; /* command duration; commands > 50 ms are split like SV_RunCmd */
  float viewangles[3];
  float forwardmove, sidemove, upmove;
  uint16_t buttons; /* SCS_IN_* */
} scs_usercmd;

/* The player edict fields that the engine round-trips through playermove_t.
 * This is the complete movement state: snapshot it to resume play exactly. */
typedef struct scs_player_state {
  float origin[3];
  float velocity[3];
  float basevelocity[3];
  float view_ofs[3];
  float v_angle[3];
  float punchangle[3];
  float movedir[3];
  int32_t flags;
  int32_t movetype;
  int32_t waterlevel;
  int32_t watertype;
  int32_t oldbuttons;
  int32_t in_duck;          /* bInDuck */
  int32_t time_step_sound;  /* flTimeStepSound */
  int32_t step_left;        /* iStepLeft */
  int32_t deadflag;
  int32_t effects;
  float duck_time;          /* flDuckTime */
  float fall_velocity;      /* flFallVelocity */
  float swim_time;          /* flSwimTime */
  float teleport_time;      /* waterjumptime */
  float friction;
  float gravity;
  float maxspeed;           /* clientmaxspeed */
  float health;
  int32_t iuser[4];
  float fuser[4];           /* fuser[1] = CS jump stamina (fuser2) */
  float vuser[4][3];
  int32_t ground_entity;    /* -1 none, 0 world, n = brush entity n-1 */
  /* Fall damage, as CBasePlayer tracks it in PlayerPreThink / PlayerPostThink. The
   * editor never takes health (no deaths); it only reports what a landing deals. */
  float server_fall_velocity; /* m_flFallVelocity */
  float last_fall_damage;     /* damage of the most recent damaging landing */
  float last_fall_speed;      /* its fall speed */
  uint32_t fall_count;        /* incremented by every damaging landing */
} scs_player_state;

/* FlPlayerFallDamage (multiplayer rules): damage of landing at `fall_speed`, 0 if
 * the fall is safe (500 u/s or less). */
SCS_API float scs_fall_damage(float fall_speed);

typedef struct scs_trace {
  int32_t allsolid, startsolid, inopen, inwater;
  float fraction;
  float endpos[3];
  float plane_normal[3];
  float plane_dist;
  int32_t ent; /* physent index, -1 = nothing hit */
} scs_trace;

/* Create / destroy a headless physics context. `config` may be NULL. */
SCS_API scs_context* scs_init(const scs_config* config);
SCS_API void scs_shutdown(scs_context* ctx);

/* Swap the collision world (hot reload while playing). The context keeps
 * its own reference; NULL clears it. */
SCS_API scs_result scs_set_world(scs_context* ctx, scs_world* world);

/* Reset the player like CBasePlayer::Spawn and place it at `origin`. */
SCS_API void scs_spawn(scs_context* ctx, const float origin[3], const float angles[3]);

/* Run one usercmd exactly like SV_RunCmd -> PM_Move(pmove, server=1). */
SCS_API void scs_step(scs_context* ctx, const scs_usercmd* cmd);

SCS_API void scs_set_position(scs_context* ctx, const float origin[3]);
SCS_API void scs_get_position(const scs_context* ctx, float origin_out[3]);
SCS_API void scs_get_state(const scs_context* ctx, scs_player_state* out);
SCS_API void scs_set_state(scs_context* ctx, const scs_player_state* state);

/* Returns 1 when the current hull fits at `origin` (PM_TestPlayerPosition). */
SCS_API int scs_position_is_free(scs_context* ctx, const float origin[3], int usehull);

/* Hull trace against the world using player hull `usehull` (SCS_HULL_*). */
SCS_API void scs_trace_hull(scs_context* ctx, const float start[3], const float end[3],
                            int usehull, scs_trace* out);
SCS_API int scs_point_contents(scs_context* ctx, const float point[3]);

/* ------------------------------------------------------------------------ */
/* Client-side helpers: building usercmds like CL_CreateMove does           */
/* ------------------------------------------------------------------------ */

/* kbutton_t: bit 0 = held, bit 1 = pressed this frame, bit 2 = released. */
typedef struct scs_kbutton {
  int32_t down[2];
  int32_t state;
} scs_kbutton;

SCS_API void scs_key_down(scs_kbutton* b, int keycode);
SCS_API void scs_key_up(scs_kbutton* b, int keycode);
/* CL_KeyState: 0.5 on the press frame, 1.0 while held; clears edge bits. */
SCS_API float scs_key_state(scs_kbutton* b);

typedef struct scs_client_input {
  scs_kbutton forward, back, moveleft, moveright, moveup, movedown, speed;
  scs_kbutton jump, duck, use, attack;
  scs_kbutton left, right; /* +left / +right: turn at cl_yawspeed */
  float viewangles[3];
  float forwardspeed, backspeed, sidespeed, upspeed, movespeedkey; /* cl_* cvars */
  float yawspeed, anglespeedkey;                                   /* cl_* cvars */
} scs_client_input;

SCS_API void scs_client_input_defaults(scs_client_input* in);

/* Fill `cmd` from key states (consumes press/release edges). Like CL_CreateMove,
 * +left / +right first turn in->viewangles by cl_yawspeed for msec. */
SCS_API void scs_build_usercmd(scs_client_input* in, float clientmaxspeed,
                               uint8_t msec, scs_usercmd* cmd);

/* Fixed-rate tick generator: returns how many commands of `*msec_out` ms to
 * run for `dt_seconds` of wall time (fps_max style, default 100 Hz = 10 ms). */
typedef struct scs_tick_clock {
  double accumulator;
  uint32_t tick_hz;
} scs_tick_clock;

SCS_API uint32_t scs_tick_clock_advance(scs_tick_clock* clock, double dt_seconds,
                                        uint8_t* msec_out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SCS_SCS_H */
