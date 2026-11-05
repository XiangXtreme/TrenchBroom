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

#pragma once

#include <QObject>
#include <QTimer>

#include "base/NotifierConnection.h"
#include "render/LightmapAtlas.h"
#include "ui/CompilationRun.h"
#include "ui/LightmapBaker.h"

#include <memory>

namespace tb::render
{
class RenderContext;
}

namespace tb::ui
{
class MapDocument;

/** Shares one bake and atlas across the views of a map editor. */
class LightmapPreview : public QObject
{
  Q_OBJECT
public:
  explicit LightmapPreview(MapDocument& document);

  void configure(render::RenderContext& context);
  bool baking() const;
  int remainingSeconds() const;

  using ProjectInfoProvider = std::function<std::optional<ProjectCompileInfo>()>;
  void setProjectInfoProvider(ProjectInfoProvider provider);

signals:
  void changed();
  void progressChanged(bool baking, int remainingSeconds);

private:
  void invalidate();
  void cancel();
  void startBake();
  void setProgress(bool baking, int remainingSeconds);

  MapDocument& m_document;
  render::LightmapAtlas m_atlas;
  std::shared_ptr<const render::LightmapAssembly> m_assembly;
  LightmapBaker m_baker;
  QTimer m_timer;
  size_t m_modificationCount;
  bool m_requested = false;
  bool m_baking = false;
  int m_remainingSeconds = -1;
  NotifierConnection m_notifierConnection;
  ProjectInfoProvider m_projectInfoProvider;
};

} // namespace tb::ui
