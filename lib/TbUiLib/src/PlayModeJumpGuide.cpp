/*
 Copyright (C) 2026 KreedzBroom contributors
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/PlayModeJumpGuide.h"

#include "base/PreferenceManager.h"
#include "gl/Camera.h"
#include "prefs/Preferences.h"
#include "render/RenderContext.h"
#include "render/RenderService.h"
#include "render/TextAnchor.h"

#include "vm/vec.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>

namespace tb::ui
{
namespace
{
constexpr auto Approach = 128.0f;
constexpr auto MinGap = 160.0f;
constexpr auto MaxGap = 300.0f;
constexpr auto MaxHeightDifference = 256.0f;
constexpr auto SurfaceTolerance = 0.125f;
constexpr auto HighJumpDrop = 69.5f;
constexpr auto EdgeProbeDepth = 80.0f;
constexpr auto HighJumpInset = 13.0f;
constexpr auto EdgeHalfLength = 15.0f;

float halfHeight(const scs_player_state& state)
{
  return state.flags & SCS_FL_DUCKING ? 18.0f : 36.0f;
}

float cross2(const vm::vec3f& a, const vm::vec3f& b)
{
  return a.x() * b.y() - a.y() * b.x();
}

struct Interval
{
  float start;
  float end;
  float height;
  vm::vec3f endNormal;
  bool allowTakeoff;
};

// Clip a horizontal ray against a convex face's XY footprint. This measures actual
// edges, including thin and diagonal platforms, without distance-stepping raycasts.
std::optional<Interval> intersect(
  const PlayModeJumpSurface& surface, const vm::vec3f& origin, const vm::vec3f& direction)
{
  if (surface.vertices.size() < 3)
    return std::nullopt;
  const auto& vertices = surface.vertices;
  auto area = 0.0f;
  for (auto i = size_t{0}; i < vertices.size(); ++i)
    area += cross2(
      vertices[i] - vertices.front(),
      vertices[(i + 1) % vertices.size()] - vertices.front());
  if (std::abs(area) < 0.001f)
    return std::nullopt;
  const auto winding = area > 0.0f ? 1.0f : -1.0f;
  auto from = -Approach;
  auto to = Approach + MaxGap;
  auto endNormal = vm::vec3f{};
  for (auto i = size_t{0}; i < vertices.size(); ++i)
  {
    const auto edge = vertices[(i + 1) % vertices.size()] - vertices[i];
    const auto distance = winding * cross2(edge, origin - vertices[i]);
    const auto rate = winding * cross2(edge, direction);
    if (std::abs(rate) < 0.00001f)
    {
      if (distance < 0.0f)
        return std::nullopt;
    }
    else if (rate > 0.0f)
      from = std::max(from, -distance / rate);
    else if (-distance / rate < to)
    {
      to = -distance / rate;
      endNormal = vm::normalize(vm::vec3f{edge.y(), -edge.x(), 0} * winding);
    }
    if (to < from)
      return std::nullopt;
  }
  return Interval{from, to, vertices.front().z(), endNormal, surface.allowTakeoff};
}

std::vector<Interval> intervals(
  const std::vector<PlayModeJumpSurface>& surfaces,
  const scs_player_state& state,
  const vm::vec3f& feet,
  const vm::vec3f& direction)
{
  auto result = std::vector<Interval>{};
  // Brush masks use BSP order (point=0, standing=1, large=2, ducked=3),
  // whereas scs_trace_hull uses PM order (standing=0, ducked=1, point=2).
  const auto mask = state.flags & SCS_FL_DUCKING ? 0b1000u : 0b0010u;
  for (const auto& surface : surfaces)
  {
    if (
      (surface.hullMask & mask) == 0 || surface.vertices.empty()
      || std::abs(surface.vertices.front().z() - feet.z()) > MaxHeightDifference)
      continue;
    if (const auto interval = intersect(surface, feet, direction))
      result.push_back(*interval);
  }
  // Merge every height's continuous footprint, not just the takeoff surface.
  // Otherwise a lower floor split into brushes can invent a landing edge at
  // an internal seam, even when that floor continues all the way under us.
  std::ranges::sort(result, {}, &Interval::height);
  auto merged = std::vector<Interval>{};
  merged.reserve(result.size());
  for (auto first = result.begin(); first != result.end();)
  {
    const auto height = first->height;
    const auto last = std::find_if(first, result.end(), [&](const auto& span) {
      return span.height > height + SurfaceTolerance;
    });
    std::ranges::sort(first, last, {}, &Interval::start);
    const auto groupStart = merged.size();
    for (auto current = first; current != last; ++current)
    {
      if (
        merged.size() > groupStart
        && current->start <= merged.back().end + SurfaceTolerance)
      {
        if (current->end > merged.back().end)
        {
          merged.back().end = current->end;
          merged.back().endNormal = current->endNormal;
          merged.back().allowTakeoff = current->allowTakeoff;
        }
        else if (current->end == merged.back().end)
          merged.back().allowTakeoff |= current->allowTakeoff;
      }
      else
        merged.push_back(*current);
    }
    first = last;
  }
  std::ranges::sort(merged, {}, &Interval::start);
  return merged;
}

std::optional<Interval> takeoffEdge(
  const std::vector<Interval>& spans, const float height)
{
  auto end = -std::numeric_limits<float>::infinity();
  auto result = std::optional<Interval>{};
  for (const auto& span : spans)
  {
    if (std::abs(span.height - height) > SurfaceTolerance)
      continue;
    // A grounded player's center can overhang an edge while the 32-unit hull still
    // has support. Keep that edge visible through the final grounded approach.
    if (
      ((span.start <= 0.0f && span.end >= -24.0f) || span.start <= end + SurfaceTolerance)
      && span.end > end)
    {
      end = span.end;
      result = span;
    }
  }
  if (!std::isfinite(end) || end < -24.0f || end > Approach || !result->allowTakeoff)
    return std::nullopt;
  return result;
}

struct ApproachEdge
{
  std::vector<Interval> spans;
  Interval edge;
};

std::optional<ApproachEdge> approachEdge(
  const std::vector<PlayModeJumpSurface>& surfaces,
  const scs_player_state& state,
  const vm::vec3f& feet,
  const vm::vec3f& selectionDirection)
{
  const auto selected =
    takeoffEdge(intervals(surfaces, state, feet, selectionDirection), feet.z());
  if (!selected || vm::squared_length(selected->endNormal) < 0.5f)
    return std::nullopt;
  // The view selects the platform edge. Dimensions follow that edge's outward
  // normal through the player, so turning the camera cannot skew the same gap.
  auto spans = intervals(surfaces, state, feet, selected->endNormal);
  const auto edge = takeoffEdge(spans, feet.z());
  if (!edge || vm::dot(edge->endNormal, selected->endNormal) < 0.999f)
    return std::nullopt;
  return ApproachEdge{std::move(spans), *edge};
}

std::optional<PlayModeJumpGuide::Edge> takeoffGuide(
  scs_context* context, const vm::vec3f& feet, const Interval& edge)
{
  auto position = feet + edge.endNormal * edge.end;
  position[2] = edge.height;
  // Sample just outside the physical lip. Starting on the plane can hit the
  // takeoff brush itself; use the same point hull as jumpaids' downward trace.
  const auto from =
    position + edge.endNormal * SurfaceTolerance + vm::vec3f{0, 0, SurfaceTolerance};
  const auto to = vm::vec3f{from.xy(), edge.height - EdgeProbeDepth};
  auto trace = scs_trace{};
  scs_trace_hull(context, from.v, to.v, SCS_HULL_POINT, &trace);
  if (trace.startsolid || trace.allsolid)
    return std::nullopt;
  const auto drop = edge.height - trace.endpos[2];
  if (drop <= 8.0f)
    return std::nullopt;
  const auto highJump = drop > HighJumpDrop;
  // jumpaids 1.21 (SiMen) uses a 13-unit takeoff hint for HJ. This is a
  // placement aid, not a simulation of the movement-dependent friction boundary.
  return PlayModeJumpGuide::Edge{
    position - edge.endNormal * (highJump ? HighJumpInset : 0.0f),
    edge.endNormal,
    highJump};
}

std::string number(const float value)
{
  auto text = std::ostringstream{};
  text << std::fixed << std::setprecision(2) << value;
  auto result = text.str();
  while (result.ends_with('0'))
    result.pop_back();
  if (result.ends_with('.'))
    result.pop_back();
  return result;
}
} // namespace

PlayModeJumpGuide measurePlayModeJumpGuide(
  scs_context* context,
  const std::vector<PlayModeJumpSurface>& surfaces,
  const scs_player_state& state,
  const vm::vec3f& lookDirection)
{
  auto result = PlayModeJumpGuide{};
  if (
    !context || state.movetype != SCS_MOVETYPE_WALK || state.ground_entity == -1
    || !(state.flags & SCS_FL_ONGROUND) || state.waterlevel >= 2
    || state.teleport_time != 0.0f)
    return result;
  const auto feet =
    vm::vec3f{state.origin[0], state.origin[1], state.origin[2] - halfHeight(state)};
  auto forward = vm::vec3f{lookDirection.x(), lookDirection.y(), 0};
  if (vm::squared_length(forward) < 0.0001f)
    return result;
  forward = vm::normalize(forward);
  const auto approach = approachEdge(surfaces, state, feet, forward);
  if (approach)
  {
    const auto& edge = approach->edge;
    const auto& direction = edge.endNormal;
    result.edge = takeoffGuide(context, feet, edge);
    const auto canLand = [&](const Interval& span) {
      // Raw brush faces include covered floors and narrow lips beside taller
      // blocks. A landing needs room for the player's current collision hull.
      const auto inset = std::min(SurfaceTolerance, (span.end - span.start) / 2.0f);
      auto origin = feet + direction * (span.start + inset);
      origin[2] = span.height + halfHeight(state) + SurfaceTolerance;
      const auto hull =
        state.flags & SCS_FL_DUCKING ? SCS_HULL_DUCKED : SCS_HULL_STANDING;
      return scs_position_is_free(context, origin.v, hull) != 0;
    };
    const auto firstLanding =
      std::ranges::find_if(approach->spans, [&](const auto& span) {
        return span.start > edge.end + SurfaceTolerance && span.start < edge.end + MaxGap
               && canLand(span);
      });
    if (firstLanding != approach->spans.end())
    {
      // A floor and the platform above it can share the same leading edge. Use
      // the top surface at that edge, independently of brush collection order.
      auto landing = firstLanding;
      for (auto next = std::next(firstLanding);
           next != approach->spans.end()
           && next->start <= firstLanding->start + SurfaceTolerance;
           ++next)
      {
        if (next->height > landing->height && canLand(*next))
          landing = next;
      }
      const auto& candidate = *landing;
      const auto distance = candidate.start - edge.end;
      // Only the nearest landing counts. Do not skip a short ledge or pit floor
      // and label a farther platform as if the intervening support did not exist.
      if (distance < MinGap || distance >= MaxGap)
        return result;
      auto start = feet + direction * edge.end;
      start[2] = edge.height;
      auto end = feet + direction * candidate.start;
      end[2] = candidate.height;
      // A platform hidden behind a wall/overhang is not a visible landing target.
      const auto from = start + vm::vec3f{0, 0, 1};
      auto to = end + vm::vec3f{0, 0, 1};
      if (end.z() > start.z())
        to[2] = from.z();
      auto trace = scs_trace{};
      scs_trace_hull(context, from.v, to.v, SCS_HULL_POINT, &trace);
      // An elevated landing's front face may be the first obstruction.
      if (trace.startsolid || (trace.fraction < 0.999f
            && vm::length(vm::vec3f{trace.endpos[0], trace.endpos[1], trace.endpos[2]} - to) > 1.0f))
        return result;
      result.span = PlayModeJumpSpan{start, end, distance};
    }
  }
  return result;
}

void renderPlayModeJumpGuide(
  const PlayModeJumpGuide& guide,
  render::RenderContext& renderContext,
  render::RenderBatch& renderBatch)
{
  if (!guide.span && !guide.edge)
    return;
  auto service = render::RenderService{renderContext, renderBatch};
  service.setHideOccludedObjects();
  service.setLineWidth(2.0f);
  const auto highJump = guide.edge && guide.edge->highJump;
  service.setForegroundColor(
    highJump ? Color{RgbF{1.0f, 0.2f, 0.2f}} : Color{RgbF{0.2f, 1.0f, 0.2f}});
  service.setBackgroundColor(pref(Preferences::InfoOverlayBackgroundColor));
  if (guide.edge)
  {
    const auto& edge = *guide.edge;
    const auto center = edge.center + vm::vec3f{0, 0, 0.2f};
    const auto cap = vm::vec3f{-edge.normal.y(), edge.normal.x(), 0} * EdgeHalfLength;
    service.renderLine(center - cap, center + cap);
  }
  if (!guide.span)
    return;
  constexpr auto lift = vm::vec3f{0, 0, 1};
  const auto& span = *guide.span;
  const auto start = span.start + lift;
  auto end = span.end + lift;
  end[2] = start.z();
  const auto direction = vm::normalize(end - start);
  const auto cap = vm::vec3f{-direction.y(), direction.x(), 0} * 12.0f;
  service.renderLine(start, end);
  if (!guide.edge || highJump)
    service.renderLine(start - cap, start + cap);
  service.renderLine(end - cap, end + cap);
  if (std::abs(span.end.z() - span.start.z()) > SurfaceTolerance)
    service.renderLine(end, span.end + lift);
  auto label = number(span.distance);
  const auto height = span.end.z() - span.start.z();
  if (std::abs(height) > SurfaceTolerance)
    label += "  Z " + std::string{height > 0 ? "+" : ""} + number(height);
  service.renderString(
    label,
    render::SimpleTextAnchor{
      (start + end) / 2.0f, render::TextAlignment::Center, {0, 12}});
}
} // namespace tb::ui
