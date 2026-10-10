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

#include "ui/PlayModeSound.h"

#include <QUrl>
#ifdef TB_PLAY_MODE_SOUND
#include <QSoundEffect>
#endif

#include "fs/FileSystem.h"

#include "kd/result.h"

#include <algorithm>
#include <filesystem>

#ifndef TB_PLAY_MODE_SOUND
// Without Qt Multimedia there is nothing to play with.
class QSoundEffect
{
};
#endif

namespace tb::ui
{
namespace
{

[[maybe_unused]] constexpr auto ChanAuto = 0;

} // namespace

PlayModeSound::PlayModeSound(const fs::FileSystem& fileSystem)
  : m_fileSystem{fileSystem}
{
}

PlayModeSound::~PlayModeSound() = default;

void PlayModeSound::setVolume(const float volume)
{
  m_volume = std::clamp(volume, 0.0f, 1.0f);
}

void PlayModeSound::play(const int channel, const std::string& sample, const float volume)
{
#ifdef TB_PLAY_MODE_SOUND
  if (m_volume <= 0.0f)
  {
    return;
  }
  const auto url = resolve(sample);
  if (!url)
  {
    return;
  }

  auto& effect = m_effects[{channel, sample}];
  if (!effect)
  {
    effect = std::make_unique<QSoundEffect>();
    effect->setSource(QUrl{QString::fromStdString(*url)});
  }

  if (channel != ChanAuto)
  {
    // S_StartSound: a new sound on an entity channel replaces the playing one
    if (auto* playing = m_playing[channel]; playing && playing != effect.get())
    {
      playing->stop();
    }
    m_playing[channel] = effect.get();
  }
  effect->stop();
  effect->setVolume(std::clamp(volume, 0.0f, 1.0f) * m_volume);
  effect->play();
#else
  (void)channel;
  (void)sample;
  (void)volume;
#endif
}

void PlayModeSound::stopAll()
{
#ifdef TB_PLAY_MODE_SOUND
  for (auto& [key, effect] : m_effects)
  {
    effect->stop();
  }
#endif
  m_playing.clear();
}

std::optional<std::string> PlayModeSound::resolve(const std::string& sample)
{
  if (const auto it = m_urls.find(sample); it != m_urls.end())
  {
    return it->second;
  }

  // The game file system knows where the game's sound folder is on disk. Sounds that are
  // missing (e.g. no game path configured) or not loose files on disk are not played:
  // QSoundEffect only plays local files.
  auto& url = m_urls[sample];
  const auto path = std::filesystem::path{"sound"} / sample;
  if (
    const auto absolute = m_fileSystem.makeAbsolute(path) | kdl::value_or(path);
    absolute.is_absolute() && std::filesystem::is_regular_file(absolute))
  {
    url = QUrl::fromLocalFile(QString::fromStdString(absolute.string()))
            .toString()
            .toStdString();
  }
  return url;
}

} // namespace tb::ui
