/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/TerrainTool.h"

#include <QWidget>

#include "gl/Camera.h"
#include "ui/InputState.h"
#include "ui/TerrainInspector.h"

#include <cmath>

namespace tb::ui
{
TerrainTool::TerrainTool()
  : Tool{false}
{
}
TerrainTool::~TerrainTool()
{
  cancelInteraction();
}
TerrainToolMode TerrainTool::mode() const
{
  return m_mode;
}
TerrainToolMode TerrainTool::activationMode() const
{
  return canActivate(m_lastMode) ? m_lastMode : TerrainToolMode::Draw;
}
bool TerrainTool::canActivate(const TerrainToolMode mode) const
{
  return m_inspector && m_inspector->canActivate(mode);
}
void TerrainTool::setMode(const TerrainToolMode mode)
{
  m_mode = mode;
  if (mode != TerrainToolMode::None)
    m_lastMode = mode;
}
void TerrainTool::setInspector(TerrainInspector* inspector)
{
  m_inspector = inspector;
}
TerrainInspector* TerrainTool::inspector() const
{
  return m_inspector.data();
}
bool TerrainTool::ownsSelection() const
{
  return true;
}
bool TerrainTool::interacting() const
{
  return m_view
         || (m_inspector && (m_inspector->stroking() || m_inspector->vertexInteracting()));
}
bool TerrainTool::interactingWith(const QWidget& view) const
{
  return m_view == &view;
}
bool TerrainTool::doActivate()
{
  if (canActivate(m_mode))
    return true;
  m_mode = TerrainToolMode::None;
  return false;
}
bool TerrainTool::doDeactivate()
{
  m_mode = TerrainToolMode::None;
  cancelInteraction();
  return true;
}
bool TerrainTool::begin(const InputState& input, QWidget& view, const bool perspective)
{
  if (
    !active() || !m_inspector || interacting()
    || !input.mouseButtonsPressed(MouseButtons::Left)
    || input.modifierKeysDown(ModifierKeys::Alt))
    return false;
  const auto point = QPointF{input.mouseX(), input.mouseY()};
  if (m_mode == TerrainToolMode::Vertices)
  {
    m_inspector->beginVertexInteraction(
      input.camera(),
      view,
      point,
      input.modifierKeys() != ModifierKeys::None,
      input.pickResult());
    if (!m_inspector->vertexInteracting())
      return false;
  }
  else if (input.modifierKeys() != ModifierKeys::None)
    return false;
  else if (m_mode == TerrainToolMode::Draw)
  {
    if (perspective || std::abs(input.camera().direction().z()) <= 0.9f)
      return false;
    const auto& ray = input.pickRay();
    m_origin = {ray.origin.x(), ray.origin.y()};
    m_current = m_origin;
    m_drawing = true;
  }
  else
  {
    if (!perspective)
      return false;
    m_inspector->beginStroke(input.pickResult());
    if (!m_inspector->stroking())
      return false;
  }
  m_view = &view;
  refreshViews();
  return true;
}
void TerrainTool::update(const InputState& input)
{
  if (!m_view || !m_inspector)
    return;
  const auto point = QPointF{input.mouseX(), input.mouseY()};
  if (m_drawing)
    m_current = {input.pickRay().origin.x(), input.pickRay().origin.y()};
  else if (m_inspector->vertexInteracting())
    m_inspector->moveVertexInteraction(point);
  else if (m_inspector->stroking())
    m_inspector->continueStroke(input.pickResult());
  refreshViews();
}
void TerrainTool::end(const InputState& input)
{
  if (!m_view || !m_inspector)
    return;
  m_view.clear();
  if (m_drawing)
  {
    m_drawing = false;
    const auto& ray = input.pickRay();
    m_inspector->createDrawnTerrain(m_origin, {ray.origin.x(), ray.origin.y()});
  }
  else if (m_inspector->vertexInteracting())
    m_inspector->endVertexInteraction(
      input.camera(), {input.mouseX(), input.mouseY()}, true);
  else
    m_inspector->endStroke(true);
  refreshViews();
}
std::optional<vm::bbox3d> TerrainTool::drawPreviewBounds() const
{
  return m_drawing && m_inspector ? m_inspector->drawnTerrainBounds(m_origin, m_current)
                                  : std::nullopt;
}
void TerrainTool::renderDrawPreview(
  render::RenderContext& context, render::RenderBatch& batch) const
{
  if (const auto bounds = drawPreviewBounds())
    m_inspector->renderDrawnTerrain(context, batch, *bounds);
}

bool TerrainTool::cancelInteraction()
{
  const auto hadInteraction = interacting();
  m_view.clear();
  m_drawing = false;
  if (m_inspector)
  {
    m_inspector->cancelVertexInteraction();
    m_inspector->endStroke(false);
  }
  if (hadInteraction)
    refreshViews();
  return hadInteraction;
}
} // namespace tb::ui
