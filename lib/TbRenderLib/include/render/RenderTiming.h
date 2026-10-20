/*
 Copyright (C) 2026 TrenchBroom contributors

 This file is part of TrenchBroom, licensed under the GNU General Public License,
 version 3 or (at your option) any later version. See <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <chrono>
#include <map>
#include <string>
#include <string_view>

namespace tb::render
{

/** Optional CPU timings for one frame. Nested stages are inclusive; do not add them. */
struct RenderTiming
{
  std::map<std::string, double, std::less<>> milliseconds;
  std::map<std::string, size_t, std::less<>> counters;
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
