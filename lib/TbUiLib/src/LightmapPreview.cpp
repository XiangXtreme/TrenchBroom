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

#include "base/Logger.h"
#include "base/PreferenceManager.h"
#include "mdl/CommandProcessor.h"
#include "mdl/Map.h"
#include "prefs/Preferences.h"
#include "render/RenderContext.h"
#include "ui/MapDocument.h"

#include <algorithm>
#include <chrono>
#include <unordered_set>

namespace tb::ui
{

LightmapPreview::LightmapPreview(MapDocument& document)
  : m_document{document}
  , m_modificationCount{m_document.map().modificationCount()}
{
  m_timer.setSingleShot(true);
  connect(&m_timer, &QTimer::timeout, this, &LightmapPreview::startBake);
  connect(
    &m_baker,
    &LightmapBaker::stageProgressChanged,
    this,
    [this](const QString& stage, const int percent) {
      m_stagePercent = percent;
      m_status = percent >= 0 ? tr("%1 · %2%").arg(stage).arg(percent) : stage;
      emit stateChanged();
    });

  m_notifierConnection += m_document.documentWasLoadedNotifier.connect([this] {
    cancel();
    m_baker.clearCache();
    m_assembly.reset();
    m_atlas.clear();
    m_displayInputs.clear();
    m_modificationCount = m_document.map().modificationCount();
    m_requested = false;
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
  auto& preferences = PreferenceManager::instance();
  m_notifierConnection +=
    preferences.preferenceDidChangeNotifier.connect([this](const auto& path) {
      if (path == Preferences::LightmapQuality.path)
      {
        invalidate();
      }
      else if (path == Preferences::ShowLightmap.path && !pref(Preferences::ShowLightmap))
      {
        cancel();
      }
      else if (path == Preferences::ShowLightmap.path && pref(Preferences::ShowLightmap))
      {
        if (m_dirty || !m_assembly)
          requestBake();
        emit stateChanged();
      }
      else if (path == Preferences::LightmapAutoUpdate.path)
      {
        if (pref(Preferences::LightmapAutoUpdate) && m_dirty)
          schedule();
        // A manual request waiting for an edit is independent of automatic updates.
        else if (m_baking || m_activity != Activity::Waiting || !m_manualRequest)
        {
          m_timer.stop();
          m_pending = m_manualPending;
          if (!m_baking && !m_manualPending && m_activity == Activity::Waiting)
            m_activity = Activity::Idle;
        }
        emit stateChanged();
      }
      else if (path == Preferences::LightmapUpdateDelay.path && m_timer.isActive())
        schedule();
    });
}

void LightmapPreview::configure(render::RenderContext& context)
{
  context.setShowLightmap(pref(Preferences::ShowLightmap));
  if (context.showLightmap() && !m_requested)
  {
    requestBake();
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

bool LightmapPreview::canCancel() const
{
  return m_baking || m_timer.isActive();
}

void LightmapPreview::setProjectInfoProvider(ProjectInfoProvider provider)
{
  m_projectInfoProvider = std::move(provider);
}

bool LightmapPreview::stale() const
{
  return m_dirty;
}
bool LightmapPreview::hasResult() const
{
  return bool(m_assembly);
}
LightmapPreview::Activity LightmapPreview::activity() const
{
  return m_activity;
}
bool LightmapPreview::manualRequest() const
{
  return m_manualRequest;
}
bool LightmapPreview::waitingForEdit() const
{
  return m_waitingForEdit;
}
bool LightmapPreview::changesQueued() const
{
  return m_pending;
}
int LightmapPreview::stagePercent() const
{
  return m_stagePercent;
}
qint64 LightmapPreview::elapsedMilliseconds() const
{
  return m_elapsedMilliseconds;
}
QString LightmapPreview::error() const
{
  return m_error;
}
QString LightmapPreview::output() const
{
  return QString::fromStdString(m_baker.output());
}
std::filesystem::path LightmapPreview::diagnosticDirectory() const
{
  return m_baker.diagnosticDirectory();
}

QString LightmapPreview::statusText() const
{
  if (!pref(Preferences::ShowLightmap))
    return tr("Lightmap preview off");
  if (m_baking)
    return m_pending ? tr("%1 · changes queued").arg(m_status) : m_status;
  if (m_timer.isActive())
    return m_waitingForEdit ? tr("Waiting for editing to finish")
                            : tr("Waiting to update");
  if (!m_error.isEmpty())
    return tr("Bake failed · %1").arg(m_error);
  if (!m_status.isEmpty() && m_status == tr("Cancelled"))
    return m_status;
  if (m_dirty)
    return pref(Preferences::LightmapAutoUpdate)
             ? tr("Lightmap out of date")
             : tr("Lightmap out of date · automatic update off");
  return m_status.isEmpty() ? tr("Lightmap up to date") : m_status;
}

void LightmapPreview::retainMatchingFaces()
{
  if (!m_assembly)
    return;
  m_displayInputs = render::matchingLightmapFaces(m_displayInputs, m_document.map());
  auto valid = std::unordered_set<const mdl::BrushFace*>{};
  for (const auto& input : m_displayInputs)
    valid.insert(input.face);
  auto assembly = std::make_shared<render::LightmapAssembly>(*m_assembly);
  std::erase_if(
    assembly->blocks, [&](const auto& block) { return !valid.contains(block.first); });
  // Terrain fragments have no stable editor face handle; do not reuse them after edits.
  assembly->terrainBlocks.clear();
  m_assembly = std::move(assembly);
}

void LightmapPreview::schedule(const bool manual)
{
  if (!pref(Preferences::ShowLightmap))
    return;
  m_requested = true;
  if (m_activity != Activity::Waiting && m_activity != Activity::Baking)
    m_manualRequest = manual;
  else
    m_manualRequest = m_manualRequest || manual;
  if (m_baking)
  {
    m_pending = true;
    m_manualPending = m_manualPending || manual;
  }
  else
  {
    m_activity = Activity::Waiting;
    m_timer.start(
      m_manualRequest ? 0
                      : std::clamp(pref(Preferences::LightmapUpdateDelay), 100, 30000));
  }
  emit stateChanged();
}

void LightmapPreview::requestBake()
{
  m_error.clear();
  if (!m_baking)
    m_status.clear();
  schedule(true);
}

void LightmapPreview::invalidate()
{
  ++m_revision;
  m_dirty = true;
  if (!m_baking)
    m_status.clear();
  retainMatchingFaces();
  if (pref(Preferences::LightmapAutoUpdate))
    schedule();
  emit stateChanged();
  if (pref(Preferences::ShowLightmap))
    emit changed();
}

void LightmapPreview::cancel()
{
  const auto hadWork = canCancel();
  m_timer.stop();
  m_baker.cancel();
  m_pending = false;
  m_manualPending = false;
  m_waitingForEdit = false;
  m_requested = true;
  m_status = hadWork ? tr("Cancelled") : QString{};
  m_activity = hadWork ? Activity::Cancelled : Activity::Idle;
  m_baking = false;
  emit stateChanged();
}

void LightmapPreview::startBake()
{
  if (!pref(Preferences::ShowLightmap))
    return;
  if (m_document.map().commandProcessor().isTransactionActive())
  {
    m_timer.start(150);
    if (!m_waitingForEdit)
    {
      m_waitingForEdit = true;
      emit stateChanged();
    }
    return;
  }
  m_waitingForEdit = false;
  m_pending = false;
  m_manualPending = false;
  m_error.clear();
  const auto revision = m_revision;
  const auto inputs = render::collectLightmapFaces(m_document.map());
  auto& logger = m_document.map().logger();
  const auto startedAt = std::chrono::steady_clock::now();
  logger.info() << "Baking a lightmap for the view.";
  m_activity = Activity::Baking;
  m_stagePercent = -1;
  m_status = tr("Starting");
  m_baking = true;
  emit stateChanged();
  m_baker.start(
    m_document.map(),
    8,
    pref(Preferences::LightmapQuality),
    [this, startedAt, revision, inputs](
      std::optional<render::LightmapBake> bake, const std::string& error) {
      m_baking = false;
      auto& logger = m_document.map().logger();
      if (!bake)
      {
        m_dirty = true;
        m_activity = Activity::Failed;
        m_error = QString::fromStdString(error);
        logger.error() << "Cannot bake a lightmap: " << error;
        if (m_pending && (m_manualPending || pref(Preferences::LightmapAutoUpdate)))
          schedule(m_manualPending);
        emit stateChanged();
        return;
      }

      auto assembly = std::make_shared<render::LightmapAssembly>();
      const auto matching = render::matchingLightmapFaces(inputs, m_document.map());
      render::buildLightmapAssembly(matching, *bake, *assembly);
      if (revision != m_revision)
        assembly->terrainBlocks.clear();
      if (assembly->blocks.empty() && assembly->terrainBlocks.empty())
      {
        m_dirty = true;
        m_activity = Activity::Failed;
        m_error = tr("The bake does not match the current map.");
        logger.warn() << "The lightmap bake does not match any face of this map.";
        if (m_pending && (m_manualPending || pref(Preferences::LightmapAutoUpdate)))
          schedule(m_manualPending);
        emit stateChanged();
        return;
      }

      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - startedAt)
                             .count();
      m_elapsedMilliseconds = elapsed;
      m_activity = Activity::Succeeded;
      const auto totalLit = assembly->blocks.size() + assembly->terrainBlocks.size();
      logger.info() << "Lightmap baked in " << elapsed << " ms: " << totalLit << " of "
                    << bake->faces.size() << " faces lit, " << assembly->width << "x"
                    << assembly->height << " atlas.";

      m_atlas.setData(assembly->width, assembly->height, assembly->pixels);
      assembly->pixels = std::vector<std::uint8_t>{};
      m_assembly = std::move(assembly);
      m_displayInputs = matching;
      m_dirty = revision != m_revision;
      m_status = m_baker.reusedGeometry() ? tr("Lighting updated · geometry reused")
                                          : tr("Lightmap up to date");
      if (m_pending && (m_manualPending || pref(Preferences::LightmapAutoUpdate)))
        schedule(m_manualPending);
      emit stateChanged();
      emit changed();
    },
    m_projectInfoProvider);
}

} // namespace tb::ui
