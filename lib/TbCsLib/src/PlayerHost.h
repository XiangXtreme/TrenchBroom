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

// The "engine" around pm_shared.
//
// PlayerHost plays the role of the GoldSrc server for exactly one client: it
// owns the edict-like player state, builds playermove_t the way the engine's
// SV_RunCmd does (as reconstructed by ReHLDS), calls the game's
// PM_Move(pmove, server = 1) and copies the results back. pm_shared itself is
// ReGameDLL's, compiled unmodified.
#pragma once

#include "Hlsdk.h"
#include "cs/scs.h"
#include "World.h"

#include <memory>
#include <random>

namespace tb::cs {

// CBasePlayer fall damage constants (player.h).
constexpr float kFallPunchThreshold = 250.0f;
constexpr float kMaxSafeFallSpeed = 500.0f;
constexpr float kMaxFatalFallSpeed = 1100.0f;

float fallDamage(float fallSpeed);

class PlayerHost
{
public:
  explicit PlayerHost(const scs_config& config);
  ~PlayerHost();

  PlayerHost(const PlayerHost&) = delete;
  PlayerHost& operator=(const PlayerHost&) = delete;

  void setWorld(std::shared_ptr<const World> world);

  void spawn(const float origin[3], const float angles[3]);
  void runCommand(const scs_usercmd& cmd);

  scs_player_state& state() { return ent_; }
  const scs_player_state& state() const { return ent_; }
  void setState(const scs_player_state& state);

  bool positionIsFree(const float origin[3], int usehull);
  pmtrace_t traceHull(const float start[3], const float end[3], int usehull);
  int pointContents(const float point[3]);

  // Engine callback plumbing (pm_shared calls plain C function pointers).
  playermove_t* pmove() { return pm_.get(); }
  double simTime() const { return timebase_; }
  long randomLong(long lo, long hi);
  float randomFloat(float lo, float hi);
  void playSound(int channel, const char* sample, float volume, float attenuation, int flags, int pitch);

private:
  void bindEngineFunctions();
  void runCommandChopped(usercmd_t cmd);
  void runCommandOnce(const usercmd_t& cmd);
  void postThinkFallDamage();
  void checkMovingGround(float frametime);
  void setupPMove(const usercmd_t& cmd);
  void finishPMove();
  void addPhysents(const float* center, float radius);
  void resetPersistentMoveState();

  std::unique_ptr<playermove_t> pm_;
  movevars_t movevars_{};
  scs_config config_{};
  scs_player_state ent_{};
  std::shared_ptr<const World> world_;
  double timebase_ = 0.0;
  std::mt19937 rng_{0x5c5u};
};

} // namespace tb::cs
