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

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

class QSoundEffect;

namespace tb
{
namespace fs
{
class FileSystem;
}

namespace ui
{

/**
 * Plays the sounds the movement code emits (footsteps, ladders, water, landing) from the
 * game's sound directory, like the engine's client: every entity channel holds one
 * sound, so a new sound on a channel cuts off the previous one (CHAN_AUTO excepted).
 * This is why a bhop with one frame on the ground is heard as the jump step only.
 *
 * Only loose sound files found through the game path are played; without them (or
 * without Qt Multimedia) play mode is silent.
 */
class PlayModeSound
{
private:
  const fs::FileSystem& m_fileSystem;
  float m_volume = 0.8f;
  std::map<std::string, std::optional<std::string>> m_urls; // sample -> file url
  std::map<std::pair<int, std::string>, std::unique_ptr<QSoundEffect>> m_effects;
  std::map<int, QSoundEffect*> m_playing; // per channel

public:
  explicit PlayModeSound(const fs::FileSystem& fileSystem);
  ~PlayModeSound();

  /** Master volume, like the game's "volume" cvar (0 mutes). */
  void setVolume(float volume);

  /** pmove->PM_PlaySound: `sample` relative to sound/, `volume` in [0, 1]. */
  void play(int channel, const std::string& sample, float volume);

  void stopAll();

private:
  std::optional<std::string> resolve(const std::string& sample);
};

} // namespace ui
} // namespace tb
