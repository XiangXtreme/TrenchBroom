/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <QPointF>
#include <QPointer>

#include "ui/Tool.h"

#include "vm/vec.h"

class QWidget;
class QRubberBand;

namespace tb::ui
{
class InputState;
class TerrainInspector;

enum class TerrainToolMode
{
  None,
  Draw,
  Paint,
  Sculpt,
  Vertices
};

// Owns the viewport editing session. The inspector supplies parameters and terrain
// operations, but neither its visibility nor its buttons own tool activation.
class TerrainTool : public Tool
{
private:
  TerrainToolMode m_mode = TerrainToolMode::None;
  TerrainToolMode m_lastMode = TerrainToolMode::Draw;
  QPointer<TerrainInspector> m_inspector;
  QPointer<QWidget> m_view;
  QPointer<QRubberBand> m_outline;
  QPointF m_start;
  vm::vec2d m_origin;

public:
  TerrainTool();
  ~TerrainTool() override;
  TerrainToolMode mode() const;
  TerrainToolMode activationMode() const;
  bool canActivate(TerrainToolMode mode) const;
  void setInspector(TerrainInspector* inspector);
  TerrainInspector* inspector() const;
  bool ownsSelection() const override;
  bool interacting() const;
  bool interactingWith(const QWidget& view) const;
  bool begin(const InputState& input, QWidget& view, bool perspective);
  void update(const InputState& input);
  void end(const InputState& input);
  bool cancelInteraction();

private:
  friend class MapViewToolBox;
  void setMode(TerrainToolMode mode);
  bool doActivate() override;
  bool doDeactivate() override;
};
} // namespace tb::ui
