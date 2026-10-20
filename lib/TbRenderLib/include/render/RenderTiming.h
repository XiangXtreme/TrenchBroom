/*
 Copyright (C) 2026 TrenchBroom contributors

 This file is part of TrenchBroom, licensed under the GNU General Public License,
 version 3 or (at your option) any later version. See <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <array>
#include <chrono>
#include <map>
#include <string>
#include <string_view>

namespace tb::render
{

enum class GlCounter : size_t
{
  DrawCalls,
  DrawRanges,
  SubmittedVertices,
  TextureBinds,
  TextureParameters,
  Uniforms,
  BufferBinds,
  AttributeChanges,
  Count
};

using GlCounters = std::array<size_t, static_cast<size_t>(GlCounter::Count)>;

/** Optional CPU timings for one frame. Nested stages are inclusive; do not add them. */
struct RenderTiming
{
  std::map<std::string, double, std::less<>> milliseconds;
  std::map<std::string, size_t, std::less<>> counters;
  GlCounters glCounters{};
  static inline thread_local RenderTiming* current = nullptr;

  class Capture
  {
    RenderTiming* m_previous;

  public:
    explicit Capture(RenderTiming& timing)
      : m_previous{current}
    {
      current = &timing;
    }
    ~Capture() { current = m_previous; }
    Capture(const Capture&) = delete;
    Capture& operator=(const Capture&) = delete;
  };

  static void count(const std::string_view name, const size_t value)
  {
    if (current)
    {
      auto& counters = current->counters;
      auto it = counters.find(name);
      if (it == counters.end())
      {
        it = counters.emplace(std::string{name}, size_t{0}).first;
      }
      it->second += value;
    }
  }

  // GL calls are frequent: accumulate fixed counters without per-call map lookups.
  static void countGl(const GlCounter counter, const size_t value = 1)
  {
    if (current)
    {
      current->glCounters[static_cast<size_t>(counter)] += value;
    }
  }
};

/** Inclusive GL workload for a pass. Counts API submissions, not visible geometry. */
class RenderGlScope
{
  RenderTiming* m_timing = RenderTiming::current;
  std::string_view m_prefix;
  GlCounters m_start{};

public:
  explicit RenderGlScope(const std::string_view prefix)
    : m_prefix{prefix}
  {
    if (m_timing)
    {
      m_start = m_timing->glCounters;
    }
  }

  ~RenderGlScope()
  {
    if (m_timing)
    {
      constexpr auto names = std::array{
        "draw_calls",
        "draw_ranges",
        "submitted_vertices",
        "texture_binds",
        "texture_parameters",
        "uniforms",
        "buffer_binds",
        "attribute_changes"};
      static_assert(names.size() == std::tuple_size_v<GlCounters>);
      for (size_t i = 0; i < names.size(); ++i)
      {
        m_timing->counters[std::string{m_prefix} + "." + names[i]] +=
          m_timing->glCounters[i] - m_start[i];
      }
    }
  }
  RenderGlScope(const RenderGlScope&) = delete;
  RenderGlScope& operator=(const RenderGlScope&) = delete;
};

/** When profiling is disabled this only checks a thread-local pointer. */
class RenderTimingScope
{
  using Clock = std::chrono::steady_clock;
  RenderTiming* m_timing = RenderTiming::current;
  std::string_view m_name;
  Clock::time_point m_start;

public:
  explicit RenderTimingScope(const std::string_view name)
    : m_name{name}
  {
    if (m_timing)
    {
      m_start = Clock::now();
    }
  }

  ~RenderTimingScope()
  {
    if (m_timing)
    {
      m_timing->milliseconds[std::string{m_name}] +=
        std::chrono::duration<double, std::milli>{Clock::now() - m_start}.count();
    }
  }
  RenderTimingScope(const RenderTimingScope&) = delete;
  RenderTimingScope& operator=(const RenderTimingScope&) = delete;
};

} // namespace tb::render
