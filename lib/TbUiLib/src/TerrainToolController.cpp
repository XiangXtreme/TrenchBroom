/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/TerrainToolController.h"

#include "ui/GestureTracker.h"
#include "ui/InputEvent.h"
#include "ui/MapViewBase.h"
#include "ui/TerrainInspector.h"
#include "ui/TerrainTool.h"

namespace tb::ui
{
namespace
{
class TerrainGestureTracker : public GestureTracker
{
  TerrainTool& m_tool;

public:
  explicit TerrainGestureTracker(TerrainTool& tool)
    : m_tool{tool}
  {
  }
  bool update(const InputState& input) override
  {
    m_tool.update(input);
    return true;
  }
  void end(const InputState& input) override { m_tool.end(input); }
  void cancel() override { m_tool.cancelInteraction(); }
};
} // namespace
TerrainToolController::TerrainToolController(
  TerrainTool& tool, MapViewBase& view, const bool perspective)
  : m_tool{tool}
  , m_view{&view}
  , m_perspective{perspective}
{
}
TerrainToolController::~TerrainToolController()
{
  if (m_view && m_tool.interactingWith(*m_view))
    m_tool.cancelInteraction();
}
Tool& TerrainToolController::tool()
{
  return m_tool;
}
const Tool& TerrainToolController::tool() const
{
  return m_tool;
}
bool TerrainToolController::keyPress(const InputState&, const KeyEvent& event)
{
  if (event.type == KeyEvent::Type::Down && event.key == Qt::Key_Escape && m_view)
  {
    m_view->cancel();
    return true;
  }
  return false;
}
void TerrainToolController::mouseDown(const InputState& input)
{
  if (m_view)
    m_tool.begin(input, *m_view, m_perspective);
}
void TerrainToolController::mouseUp(const InputState& input)
{
  if (
    m_view && !input.anyToolDragging() && m_tool.interactingWith(*m_view)
    && input.mouseButtonsPressed(MouseButtons::Left))
    m_tool.end(input);
}
bool TerrainToolController::mouseClick(const InputState& input)
{
  // Reserve left clicks even when this view cannot edit the current terrain mode.
  return input.mouseButtonsPressed(MouseButtons::Left);
}
bool TerrainToolController::mouseDoubleClick(const InputState& input)
{
  mouseDown(input);
  mouseUp(input);
  return mouseClick(input);
}
std::unique_ptr<GestureTracker> TerrainToolController::acceptMouseDrag(
  const InputState& input)
{
  if (!input.mouseButtonsPressed(MouseButtons::Left))
    return nullptr;
  return std::make_unique<TerrainGestureTracker>(m_tool);
}
bool TerrainToolController::cancel()
{
  return m_tool.cancelInteraction();
}
void TerrainToolController::render(
  const InputState& input, render::RenderContext& context, render::RenderBatch& batch)
{
  m_tool.renderDrawPreview(context, batch);
  if (const auto* inspector = m_tool.inspector())
  {
    inspector->renderVertices(
      context, batch, input.pickResult(), QPointF{input.mouseX(), input.mouseY()});
  }
}
} // namespace tb::ui
