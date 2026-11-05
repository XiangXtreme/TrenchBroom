/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include "base/Notifier.h"

#include <memory>

namespace tb::render
{
struct LightmapAssembly;
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
public:
  Notifier<> previewDidChangeNotifier;

private:
  class Impl;
  std::unique_ptr<Impl> m_impl;

public:
  TerrainPreviewRenderer(AppController& app, MapDocument& document);
  ~TerrainPreviewRenderer();

  void render(
    render::RenderBatch& batch,
    const TerrainInspector* inspector,
    bool visible,
    const render::LightmapAssembly* lightmap = nullptr);
};
} // namespace tb::ui
