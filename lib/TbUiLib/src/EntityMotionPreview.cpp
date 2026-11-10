/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/EntityMotionPreview.h"

#include "base/PreferenceManager.h"
#include "mdl/BrushNode.h"
#include "mdl/EntityNode.h"
#include "mdl/GameInfo.h"
#include "mdl/Map.h"
#include "mdl/Selection.h"
#include "prefs/Preferences.h"
#include "render/GoldSrcEntityRenderer.h"
#include "render/RenderContext.h"
#include "render/RenderService.h"
#include "ui/MapDocument.h"

#include <algorithm>
#include <cmath>

namespace tb::ui
{
EntityMotionPreview::EntityMotionPreview(MapDocument& document)
  : m_document{document}
{
  m_notifierConnection +=
    document.selectionDidChangeNotifier.connect([this](const auto&) { refresh(); });
  m_notifierConnection +=
    document.nodesDidChangeNotifier.connect([this](const auto&) { refresh(); });
  m_notifierConnection +=
    document.nodesWereAddedNotifier.connect([this](const auto&) { refresh(); });
  m_notifierConnection +=
    document.nodesWillBeRemovedNotifier.connect([this](const auto&) { clear(); });
  m_notifierConnection +=
    document.nodesWereRemovedNotifier.connect([this](const auto&) { refresh(); });
  m_notifierConnection += document.documentWasLoadedNotifier.connect([this] {
    clear();
    refresh();
  });
  m_notifierConnection += document.documentWillBeDestroyedNotifier.connect([this] {
    clear();
    m_notifierConnection.disconnect();
  });
  refresh();
}

const std::optional<GoldSrcEntityMotion>& EntityMotionPreview::motion() const
{
  return m_motion;
}
double EntityMotionPreview::progress() const
{
  return m_progress;
}
bool EntityMotionPreview::opposite() const
{
  return m_opposite;
}
bool EntityMotionPreview::active() const
{
  return m_active && !m_suspended && m_motion && m_motion->problem == MotionProblem::None;
}

void EntityMotionPreview::clear()
{
  m_owner = nullptr;
  m_motion.reset();
  reset();
}

void EntityMotionPreview::reset()
{
  m_progress = 0;
  m_opposite = false;
  m_active = false;
  emit changed();
}

void EntityMotionPreview::setSuspended(const bool suspended)
{
  m_suspended = suspended;
  reset();
}

void EntityMotionPreview::setProgress(const double progress)
{
  if (
    m_suspended || !m_motion || m_motion->problem != MotionProblem::None
    || !std::isfinite(progress))
    return;
  m_progress = std::clamp(progress, 0.0, 1.0);
  m_active = true;
  emit changed();
}

void EntityMotionPreview::setOpposite(const bool opposite)
{
  if (m_suspended || !m_motion || !m_motion->bidirectional)
    return;
  m_opposite = opposite;
  emit changed();
}

void EntityMotionPreview::refresh()
{
  const auto& map = m_document.map();
  const auto& selection = map.selection();
  const auto* owner = static_cast<const mdl::EntityNode*>(nullptr);
  auto compatible = true;
  const auto include = [&](const mdl::EntityNodeBase* candidate) {
    const auto* entity = dynamic_cast<const mdl::EntityNode*>(candidate);
    if (!entity || (owner && entity != owner))
      compatible = false;
    else
      owner = entity;
  };
  for (const auto* node : selection.nodes)
  {
    if (const auto* brush = dynamic_cast<const mdl::BrushNode*>(node))
      include(brush->entity());
    else if (const auto* entity = dynamic_cast<const mdl::EntityNode*>(node))
      include(entity);
    else
      compatible = false;
  }
  for (const auto& face : selection.brushFaces)
    include(face.node().entity());
  if (!compatible)
    owner = nullptr;
  if (!render::usesGoldSrcRendering(map.gameInfo().gameConfig))
    owner = nullptr;
  if (owner != m_owner)
  {
    m_progress = 0;
    m_opposite = false;
    m_active = false;
  }
  m_owner = owner;
  m_motion = owner ? goldSrcEntityMotion(*owner) : std::nullopt;
  if (!m_motion || m_motion->problem != MotionProblem::None)
    m_active = false;
  if (!m_motion || !m_motion->bidirectional)
    m_opposite = false;
  emit changed();
}

void EntityMotionPreview::render(
  render::RenderContext& context, render::RenderBatch& batch) const
{
  if (!active())
    return;
  const auto transform = m_motion->transform(m_progress, m_opposite);
  // Suppress a coincident ghost at the authored pose.
  if (vm::is_equal(transform, vm::mat4x4d::identity(), 1e-6))
    return;
  auto service = render::RenderService{context, batch};
  service.setShowOccludedObjectsTransparent();
  service.setShowBackfaces();
  service.setLineWidth(1.5f);
  const auto color = pref(Preferences::SelectedEdgeColor).to<RgbF>();
  for (const auto& face : m_motion->faces)
  {
    auto vertices = std::vector<vm::vec3f>{};
    vertices.reserve(face.size());
    for (const auto& vertex : face)
      vertices.emplace_back(transform * vertex);
    if (context.render3D())
    {
      service.setForegroundColor(RgbaF{color, 0.14f});
      service.renderFilledPolygon(vertices);
    }
    service.setForegroundColor(RgbaF{color, 0.85f});
    service.renderPolygonOutline(vertices);
  }
}
} // namespace tb::ui
