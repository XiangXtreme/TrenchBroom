/*
 Copyright (C) 2026 XiangXtreme

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

#include "ui/LightmapPreview.h"

#include <QStandardPaths>

#include "base/Logger.h"
#include "base/PreferenceManager.h"
#include "mdl/Map.h"
#include "prefs/Preferences.h"
#include "render/RenderContext.h"
#include "ui/MapDocument.h"
#include "ui/QPathUtils.h"

#include <chrono>

namespace tb::ui
{

LightmapPreview::LightmapPreview(MapDocument& document)
  : m_document{document}
  , m_modificationCount{m_document.map().modificationCount()}
{
  m_timer.setSingleShot(true);
  connect(&m_timer, &QTimer::timeout, this, &LightmapPreview::startBake);
  connect(
    &m_baker, &LightmapBaker::remainingSecondsChanged, this, [this](const int seconds) {
      setProgress(true, seconds);
    });

  m_notifierConnection += m_document.documentWasLoadedNotifier.connect([this] {
    m_modificationCount = m_document.map().modificationCount();
    invalidate();
  });
  m_notifierConnection += m_document.modificationStateDidChangeNotifier.connect([this] {
    const auto count = m_document.map().modificationCount();
    // Observe each transition, including undo, even if the view never draws that state.
    if (count != m_modificationCount)
    {
      m_modificationCount = count;
      invalidate();
    }
  });
  m_notifierConnection +=
    PreferenceManager::instance().preferenceDidChangeNotifier.connect(
      [this](const std::filesystem::path& path) {
        if (
          path == Preferences::LightmapQuality.path
          || path == Preferences::LightmapToolPath.path)
        {
          invalidate();
        }
        else if (
          path == Preferences::ShowLightmap.path && !pref(Preferences::ShowLightmap))
        {
          cancel();
          m_requested = bool(m_assembly);
        }
      });
}

void LightmapPreview::configure(render::RenderContext& context)
{
  context.setShowLightmap(pref(Preferences::ShowLightmap));
  if (context.showLightmap() && !m_requested)
  {
    m_requested = true;
    m_timer.start(1500);
  }

  // The map renderer shares its brush buffers with the 2D views. Keep the UVs
  // identical across views and let the render context decide whether to sample them.
  context.setLightmapAssembly(m_assembly);
  context.setLightmapAtlas(m_assembly ? &m_atlas : nullptr);
}

bool LightmapPreview::baking() const
{
  return m_baking;
}

int LightmapPreview::remainingSeconds() const
{
  return m_remainingSeconds;
}

void LightmapPreview::setProjectInfoProvider(ProjectInfoProvider provider)
{
  m_projectInfoProvider = std::move(provider);
}

void LightmapPreview::invalidate()
{
  cancel();
  m_requested = false;
  if (m_assembly)
  {
    m_assembly.reset();
    m_atlas.clear();
  }
  if (pref(Preferences::ShowLightmap))
    emit changed();
}

void LightmapPreview::cancel()
{
  m_timer.stop();
  m_baker.cancel();
  setProgress(false, -1);
}

void LightmapPreview::setProgress(const bool baking, const int remainingSeconds)
{
  if (m_baking != baking || m_remainingSeconds != remainingSeconds)
  {
    m_baking = baking;
    m_remainingSeconds = remainingSeconds;
    emit progressChanged(baking, remainingSeconds);
  }
}

void LightmapPreview::startBake()
{
  auto& logger = m_document.map().logger();
  auto executable = pref(Preferences::LightmapToolPath);
  if (executable.empty())
    executable =
      pathFromQString(QStandardPaths::findExecutable(QStringLiteral("hltools")));
  if (executable.empty())
  {
    logger.error() << "Cannot bake a lightmap: no map toolchain is configured.";
    return;
  }

  auto projectInfo = std::optional<ProjectCompileInfo>{};
  if (m_projectInfoProvider)
    projectInfo = m_projectInfoProvider();

  const auto startedAt = std::chrono::steady_clock::now();
  logger.info() << "Baking a lightmap for the view.";
  setProgress(true, -1);
  m_baker.start(
    m_document.map(),
    executable,
    8,
    pref(Preferences::LightmapQuality),
    [this, startedAt](
      std::optional<render::LightmapBake> bake, const std::string& error) {
      setProgress(false, -1);
      auto& logger = m_document.map().logger();
      if (!bake)
      {
        logger.error() << "Cannot bake a lightmap: " << error;
        return;
      }

      auto assembly = std::make_shared<render::LightmapAssembly>();
      render::buildLightmapAssembly(m_document.map(), *bake, *assembly);
      if (assembly->blocks.empty() && assembly->terrainBlocks.empty())
      {
        logger.warn() << "The lightmap bake does not match any face of this map.";
        return;
      }

      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - startedAt)
                             .count();
      const auto totalLit = assembly->blocks.size() + assembly->terrainBlocks.size();
      logger.info() << "Lightmap baked in " << elapsed
                    << " ms: " << totalLit << " of " << bake->faces.size()
                    << " faces lit, " << assembly->width << "x" << assembly->height
                    << " atlas.";

      m_atlas.setData(assembly->width, assembly->height, assembly->pixels);
      assembly->pixels = std::vector<std::uint8_t>{};
      m_assembly = std::move(assembly);
      emit changed();
    },
    std::move(projectInfo));
}

} // namespace tb::ui
