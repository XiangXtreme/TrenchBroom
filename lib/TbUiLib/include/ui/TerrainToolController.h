/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <QPointer>

#include "ui/ToolController.h"

namespace tb::ui
{
class TerrainTool;
class MapViewBase;
class TerrainToolController : public ToolController
{
private:
  TerrainTool& m_tool;
  QPointer<MapViewBase> m_view;
  bool m_perspective;

public:
  TerrainToolController(TerrainTool& tool, MapViewBase& view, bool perspective);
  ~TerrainToolController() override;
  Tool& tool() override;
  const Tool& tool() const override;
  bool keyPress(const InputState& input, const KeyEvent& event) override;
  void mouseDown(const InputState& input) override;
  void mouseUp(const InputState& input) override;
  bool mouseClick(const InputState& input) override;
  bool mouseDoubleClick(const InputState& input) override;
  std::unique_ptr<GestureTracker> acceptMouseDrag(const InputState& input) override;
  bool cancel() override;
  void render(
    const InputState& input,
    render::RenderContext& context,
    render::RenderBatch& batch) override;
};
} // namespace tb::ui
