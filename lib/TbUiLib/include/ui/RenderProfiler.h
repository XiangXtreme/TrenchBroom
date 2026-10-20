/*
 Copyright (C) 2026 TrenchBroom contributors

 This file is part of TrenchBroom, licensed under the GNU General Public License,
 version 3 or (at your option) any later version. See <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <memory>

class QOpenGLContext;
class QSize;
class QString;

namespace tb::render
{
struct RenderTiming;
}

namespace tb::ui
{

/** Per-view diagnostic capture, enabled with TB_RENDER_PROFILE=1 or a JSONL path.
 * GPU results are read only when ready. Never forces CPU/GPU synchronization.
 */
class RenderProfiler
{
  struct Impl;
  std::unique_ptr<Impl> m_impl;

public:
  static std::unique_ptr<RenderProfiler> fromEnvironment();
  explicit RenderProfiler(const QString& path);
  ~RenderProfiler();

  render::RenderTiming& beginFrame(
    QOpenGLContext& context, const QString& view, const QSize& pixels, int fpsLimit);
  void endFrame();
  void aboutToCompose();
  void frameSwapped();
  // Called with the owning GL context current, including before context recreation.
  void releaseGpuQueries();
};

} // namespace tb::ui
