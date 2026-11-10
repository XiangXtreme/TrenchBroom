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
#include <limits>
#include <sstream>

namespace tb::ui
{
namespace
{
constexpr auto Approach = 128.0f;
constexpr auto MaxGap = 512.0f;
constexpr auto MaxHeightDifference = 256.0f;
constexpr auto SurfaceTolerance = 0.125f;

float halfHeight(const scs_player_state& state)
{
  return state.flags & SCS_FL_DUCKING ? 18.0f : 36.0f;
}

int hull(const scs_player_state& state)
{
  return state.flags & SCS_FL_DUCKING ? SCS_HULL_DUCKED : SCS_HULL_STANDING;
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
  return Interval{from, to, vertices.front().z(), endNormal};
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
  std::ranges::sort(result, {}, &Interval::start);
  return result;
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
  if (!std::isfinite(end) || end < -24.0f || end > Approach)
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

bool playModeEdgeFriction(scs_context* context, const scs_player_state& state)
{
  // Keep this probe equivalent to PM_Friction in the vendored pm_shared.cpp.
  // https://github.com/ValveSoftware/halflife/blob/master/pm_shared/pm_shared.c
  const auto speed = std::sqrt(
    state.velocity[0] * state.velocity[0] + state.velocity[1] * state.velocity[1]
    + state.velocity[2] * state.velocity[2]);
  if (
    !context || state.ground_entity == -1 || !(state.flags & SCS_FL_ONGROUND)
    || state.movetype != SCS_MOVETYPE_WALK || state.teleport_time != 0.0f || speed < 0.1f)
    return false;
  const float start[] = {
    state.origin[0] + state.velocity[0] / speed * 16.0f,
    state.origin[1] + state.velocity[1] / speed * 16.0f,
    state.origin[2] - halfHeight(state)};
  const float end[] = {start[0], start[1], start[2] - 34.0f};
  auto trace = scs_trace{};
  scs_trace_hull(context, start, end, hull(state), &trace);
  return trace.fraction == 1.0f;
}

PlayModeJumpGuide measurePlayModeJumpGuide(
  scs_context* context,
  const std::vector<PlayModeJumpSurface>& surfaces,
  const scs_player_state& state,
  const vm::vec3f& lookDirection,
  const bool measureSpan,
  const bool measureEdge)
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
  if (measureSpan && approach)
  {
    const auto& edge = approach->edge;
    const auto& direction = edge.endNormal;
    for (const auto& candidate : approach->spans)
    {
      if (
        candidate.start <= edge.end + SurfaceTolerance
        || candidate.start - edge.end > MaxGap)
        continue;
      const auto start = feet + direction * edge.end;
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
        break;
      result.span = PlayModeJumpSpan{start, end, candidate.start - edge.end};
      break;
    }
  }
  if (measureEdge)
  {
    auto direction = vm::vec3f{state.velocity[0], state.velocity[1], 0};
    const auto moving = vm::squared_length(direction) >= 0.01f;
    direction = moving ? vm::normalize(direction) : forward;
    const auto edgeApproach = moving ? approachEdge(surfaces, state, feet, direction)
                                     : std::optional<ApproachEdge>{};
    const auto* support = moving ? (edgeApproach ? &*edgeApproach : nullptr)
                                 : (approach ? &*approach : nullptr);
    if (support)
    {
      const auto& normal = support->edge.endNormal;
      const auto edge = support->edge.end;
      if (!moving)
        direction = normal;
      // Locate the support-probe transition for a hypothetical player moving along
      // this approach. In particular, do not draw a fixed 16-unit inset: player hull
      // expansion, shallow support and stance all affect where the probe misses.
      const auto probe = [&](const float offset) {
        auto predicted = state;
        for (auto i = size_t{0}; i < 3; ++i)
        {
          predicted.origin[i] += normal[i] * offset;
          predicted.velocity[i] = direction[i] * 250.0f;
        }
        return playModeEdgeFriction(context, predicted);
      };
      auto low = edge - 40.0f;
      auto high = edge + 32.0f;
      if (!probe(low) && probe(high))
      {
        for (auto i = 0; i < 14; ++i)
        {
          const auto middle = (low + high) / 2.0f;
          (probe(middle) ? high : low) = middle;
        }
        // Draw the rear edge of the player's footprint at the transition, on the
        // platform side, rather than projecting the player's center into the gap.
        const auto halfWidth = 16.0f * (std::abs(normal.x()) + std::abs(normal.y()));
        result.edge = PlayModeEdgeGuide{
          feet + normal * (high - halfWidth),
          normal,
          moving && playModeEdgeFriction(context, state)};
      }
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
  service.setForegroundColor(pref(Preferences::InfoOverlayTextColor));
  service.setBackgroundColor(pref(Preferences::InfoOverlayBackgroundColor));
  constexpr auto lift = vm::vec3f{0, 0, 1};
  if (guide.span)
  {
    const auto& span = *guide.span;
    const auto start = span.start + lift;
    auto end = span.end + lift;
    end[2] = start.z();
    const auto direction = vm::normalize(end - start);
    const auto cap = vm::vec3f{-direction.y(), direction.x(), 0} * 12.0f;
    service.renderLine(start, end);
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
  if (guide.edge)
  {
    const auto& edge = *guide.edge;
    service.setForegroundColor(
      pref(edge.active ? Preferences::XAxisColor : Preferences::ZAxisColor));
    service.setLineWidth(edge.active ? 3.0f : 2.0f);
    const auto cap = vm::vec3f{-edge.direction.y(), edge.direction.x(), 0} * 24.0f;
    const auto position = edge.position + lift * 2.0f;
    service.renderLine(position - cap, position + cap);
    service.renderString(
      "HJ",
      render::SimpleTextAnchor{position + cap, render::TextAlignment::Left, {6, 8}});
  }
}
} // namespace tb::ui
