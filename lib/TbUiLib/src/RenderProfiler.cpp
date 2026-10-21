/*
 Copyright (C) 2026 TrenchBroom contributors

 This file is part of TrenchBroom, licensed under the GNU General Public License,
 version 3 or (at your option) any later version. See <http://www.gnu.org/licenses/>.
 */

#include "ui/RenderProfiler.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLTimerQuery>
#include <QSize>
#include <QSurfaceFormat>

#include "render/RenderTiming.h"
#include "ui/FileLogger.h"
#include "ui/QPathUtils.h"
#include "ui/SystemPaths.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numeric>
#include <vector>

namespace tb::ui
{
namespace
{
QJsonObject summarize(std::vector<double> values)
{
  const auto count = values.size();
  if (count == 0)
  {
    return {};
  }
  const auto sum = std::accumulate(values.begin(), values.end(), 0.0);
  std::ranges::sort(values);
  const auto p95 = static_cast<size_t>(std::ceil(0.95 * static_cast<double>(count))) - 1;
  return {
    {"samples", static_cast<qint64>(count)},
    {"avg", sum / static_cast<double>(count)},
    {"p95", values[p95]},
    {"max", values.back()}};
}
} // namespace

struct RenderProfiler::Impl
{
  QFile file;
  QElapsedTimer clock;
  qint64 reportStart = 0;
  qint64 frameStart = -1;
  qint64 paintStart = -1;
  qint64 swapEnd = -1;
  qint64 renderEnd = -1;
  qint64 composeStart = -1;
  bool pendingComposition = false;
  bool metadataWritten = false;
  bool gpuInitialized = false;
  bool gpuSupported = false;
  int activeQuery = -1;
  struct Query
  {
    std::unique_ptr<QOpenGLTimerQuery> timer;
    bool pending = false;
  };
  std::array<Query, 4> queries;
  render::RenderTiming timing;
  std::map<std::string, std::vector<double>, std::less<>> samples;
  std::map<std::string, size_t, std::less<>> counters;
  size_t frames = 0;
  QString view;
  QSize pixels;
  int fpsLimit = 0;

  explicit Impl(const QString& path)
    : file{path}
  {
    timing.timeGlCalls = qEnvironmentVariableIntValue("TB_RENDER_PROFILE_GL_TIMING") != 0;
    QDir{}.mkpath(QFileInfo{path}.absolutePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append))
    {
      qWarning() << "Render profiling log could not be opened:" << path
                 << file.errorString();
    }
    clock.start();
  }

  void write(QJsonObject object)
  {
    object.insert("view", view);
    object.insert("view_id", QString::number(reinterpret_cast<quintptr>(this), 16));
    object.insert("utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    // Each view owns a file handle; refresh its position after another view appends.
    file.seek(file.size());
    file.write(QJsonDocument{object}.toJson(QJsonDocument::Compact) + '\n');
    file.flush();
  }

  void report(const bool force = false)
  {
    const auto now = clock.nsecsElapsed();
    if (frames == 0 || (!force && now - reportStart < 1000000000LL))
    {
      return;
    }
    auto times = QJsonObject{};
    for (const auto& [name, values] : samples)
    {
      auto summary = summarize(values);
      if (name.starts_with("cpu."))
      {
        summary.insert(
          "per_frame_avg",
          std::accumulate(values.begin(), values.end(), 0.0)
            / static_cast<double>(frames));
      }
      times.insert(QString::fromStdString(name), summary);
    }
    auto counts = QJsonObject{};
    for (const auto& [name, value] : counters)
    {
      counts.insert(QString::fromStdString(name), static_cast<qint64>(value));
    }
    write(
      {{"kind", "window"},
       {"elapsed_ms", static_cast<double>(now - reportStart) / 1e6},
       {"frames", static_cast<qint64>(frames)},
       {"fps",
        static_cast<double>(frames) * 1e9 / static_cast<double>(now - reportStart)},
       {"width_px", pixels.width()},
       {"height_px", pixels.height()},
       {"fps_limit", fpsLimit},
       {"timings_ms", times},
       {"counters", counts}});
    samples.clear();
    counters.clear();
    frames = 0;
    reportStart = now;
  }

  void startGpu(QOpenGLContext& context)
  {
    if (!gpuInitialized)
    {
      gpuInitialized = true;
      gpuSupported = context.format().version() >= std::pair{3, 3}
                     || context.hasExtension(QByteArrayLiteral("GL_ARB_timer_query"));
      if (gpuSupported)
      {
        for (auto& query : queries)
        {
          query.timer = std::make_unique<QOpenGLTimerQuery>();
          if (!query.timer->create())
          {
            gpuSupported = false;
            break;
          }
        }
      }
    }
    if (!gpuSupported)
    {
      return;
    }
    for (auto& query : queries)
    {
      if (query.pending && query.timer->isResultAvailable())
      {
        samples["gpu.frame"].push_back(
          static_cast<double>(query.timer->waitForResult()) / 1e6);
        query.pending = false;
      }
    }
    for (size_t i = 0; i < queries.size(); ++i)
    {
      if (!queries[i].pending)
      {
        activeQuery = static_cast<int>(i);
        queries[i].timer->begin();
        return;
      }
    }
    ++counters["gpu.skipped_queries"];
  }
};

std::unique_ptr<RenderProfiler> RenderProfiler::fromEnvironment()
{
  const auto option = qEnvironmentVariable("TB_RENDER_PROFILE").trimmed();
  if (option.isEmpty() || option == "0")
  {
    return nullptr;
  }
  const auto path =
    option == "1" ? pathAsQString(SystemPaths::logFilePath()) + ".render.jsonl" : option;
  auto profiler = std::make_unique<RenderProfiler>(path);
  return profiler->m_impl->file.isOpen() ? std::move(profiler) : nullptr;
}

RenderProfiler::RenderProfiler(const QString& path)
  : m_impl{std::make_unique<Impl>(path)}
{
}

RenderProfiler::~RenderProfiler()
{
  m_impl->report(true);
}

void RenderProfiler::beginPaint()
{
  auto& p = *m_impl;
  p.paintStart = p.clock.nsecsElapsed();
  if (p.swapEnd >= 0)
  {
    p.samples["present.swap_to_paint"].push_back(
      static_cast<double>(p.paintStart - p.swapEnd) / 1e6);
    p.swapEnd = -1;
  }
}

void RenderProfiler::endPaint()
{
  auto& p = *m_impl;
  const auto now = p.clock.nsecsElapsed();
  if (p.paintStart >= 0)
  {
    p.samples["cpu.paint_event"].push_back(static_cast<double>(now - p.paintStart) / 1e6);
    if (p.renderEnd >= p.paintStart)
    {
      p.samples["present.render_to_paint_end"].push_back(
        static_cast<double>(now - p.renderEnd) / 1e6);
    }
    p.paintStart = -1;
  }
}

render::RenderTiming& RenderProfiler::beginFrame(
  QOpenGLContext& context, const QString& view, const QSize& pixels, const int fpsLimit)
{
  auto& p = *m_impl;
  const auto now = p.clock.nsecsElapsed();
  if (p.paintStart >= 0)
  {
    p.samples["present.paint_to_render"].push_back(
      static_cast<double>(now - p.paintStart) / 1e6);
  }
  if (p.frameStart >= 0)
  {
    p.samples["frame.interval"].push_back(static_cast<double>(now - p.frameStart) / 1e6);
  }
  if (p.pendingComposition)
  {
    ++p.counters["present.replaced_frames"];
  }
  p.frameStart = now;
  p.pendingComposition = false;
  p.composeStart = -1;
  p.view = view;
  p.pixels = pixels;
  p.fpsLimit = fpsLimit;
  p.timing.milliseconds.clear();
  p.timing.counters.clear();
  p.timing.glCounters.fill(0);
  p.timing.glMilliseconds.fill(0.0);
  p.startGpu(context);
  p.samples["cpu.profile.begin_frame"].push_back(
    static_cast<double>(p.clock.nsecsElapsed() - now) / 1e6);
  if (!p.metadataWritten)
  {
    auto framebufferSamples = GLint{};
    context.functions()->glGetIntegerv(GL_SAMPLES, &framebufferSamples);
    const auto glString = [&](const GLenum name) {
      const auto* value = context.functions()->glGetString(name);
      return value ? QString::fromLatin1(reinterpret_cast<const char*>(value))
                   : QString{};
    };
    p.write(
      {{"kind", "metadata"},
       {"vendor", glString(GL_VENDOR)},
       {"renderer", glString(GL_RENDERER)},
       {"gl_version", glString(GL_VERSION)},
       {"swap_interval_requested", context.format().swapInterval()},
       {"default_swap_interval_requested",
        QSurfaceFormat::defaultFormat().swapInterval()},
       {"context_samples", context.format().samples()},
       {"framebuffer_samples", framebufferSamples},
       {"surface_depth_bits", context.format().depthBufferSize()},
       {"default_samples_requested", QSurfaceFormat::defaultFormat().samples()},
       {"gpu_supported", p.gpuSupported},
       {"gl_call_timing", p.timing.timeGlCalls},
       {"cpu_stages", "inclusive wall time; includes any driver stalls"},
       {"presentation",
        "Qt composition and swap return, not display scanout or input latency"}});
    p.metadataWritten = true;
    FileLogger::instance().info()
      << "Render profiling: " << p.file.fileName().toStdString();
  }
  return p.timing;
}

void RenderProfiler::endFrame()
{
  auto& p = *m_impl;
  if (p.activeQuery >= 0)
  {
    auto& query = p.queries[static_cast<size_t>(p.activeQuery)];
    query.timer->end();
    query.pending = true;
    p.activeQuery = -1;
  }
  p.renderEnd = p.clock.nsecsElapsed();
  p.pendingComposition = true;
  for (const auto& [name, value] : p.timing.milliseconds)
  {
    p.samples[name].push_back(value);
  }
  for (const auto& [name, value] : p.timing.counters)
  {
    p.counters[name] += value;
  }
  ++p.frames;
  p.report();
}

void RenderProfiler::aboutToCompose()
{
  if (m_impl->pendingComposition && m_impl->composeStart < 0)
  {
    m_impl->composeStart = m_impl->clock.nsecsElapsed();
  }
}

void RenderProfiler::frameSwapped()
{
  auto& p = *m_impl;
  if (p.pendingComposition && p.composeStart >= 0)
  {
    const auto now = p.clock.nsecsElapsed();
    p.swapEnd = now;
    p.samples["present.render_to_compose"].push_back(
      static_cast<double>(p.composeStart - p.renderEnd) / 1e6);
    p.samples["present.compose_swap"].push_back(
      static_cast<double>(now - p.composeStart) / 1e6);
    p.samples["present.frame_to_swap"].push_back(
      static_cast<double>(now - p.frameStart) / 1e6);
    p.pendingComposition = false;
  }
}

void RenderProfiler::releaseGpuQueries()
{
  auto& p = *m_impl;
  for (auto& query : p.queries)
  {
    query.timer.reset();
    query.pending = false;
  }
  p.gpuInitialized = false;
  p.activeQuery = -1;
}

} // namespace tb::ui
