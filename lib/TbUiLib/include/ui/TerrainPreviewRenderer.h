/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <memory>

class QWidget;

namespace tb::render
{
class RenderBatch;
} // namespace tb::render
namespace tb::ui
{
class AppController;
class MapDocument;
class TerrainInspector;

/** Draws a transient material preview over the editable terrain brush faces. */
class TerrainPreviewRenderer
{
  class Impl;
  std::unique_ptr<Impl> m_impl;

public:
  TerrainPreviewRenderer(AppController& app, MapDocument& document, QWidget& view);
  ~TerrainPreviewRenderer();

  void render(render::RenderBatch& batch, const TerrainInspector* inspector);
};
} // namespace tb::ui
