/*
 Copyright (C) 2026 XiangXtreme
 SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ui/MapViewAreaGeometry.h"

#include <algorithm>
#include <cmath>

namespace tb::ui::areaGeometry
{
namespace
{
constexpr auto Epsilon = 0.000001;
bool near(const double a, const double b)
{
  return std::abs(a - b) < Epsilon;
}
double low(const QRectF& r, const bool x)
{
  return x ? r.left() : r.top();
}
double high(const QRectF& r, const bool x)
{
  return x ? r.right() : r.bottom();
}
void setLow(QRectF& r, const bool x, const double value)
{
  if (x)
    r.setLeft(value);
  else
    r.setTop(value);
}
void setHigh(QRectF& r, const bool x, const double value)
{
  if (x)
    r.setRight(value);
  else
    r.setBottom(value);
}
int duplicate(Plan& plan, const int origin, const QRectF& rect)
{
  const auto id = plan.nextId++;
  plan.rects.emplace(id, rect);
  plan.copies[id] = plan.copies.contains(origin) ? plan.copies.at(origin) : origin;
  return id;
}

bool joinInto(
  Plan& plan,
  int source,
  int target,
  const QSizeF& size,
  bool closeRemainders,
  int& budget);

bool closeArea(
  Plan& plan, const int id, const int exclude, const QSizeF& size, int& budget)
{
  if (!plan.rects.contains(id))
    return true;
  const auto rect = plan.rects.at(id);
  auto best = -1;
  auto score = 0.0;
  for (const auto& [candidate, r] : plan.rects)
  {
    if (candidate == id || candidate == exclude)
      continue;
    const auto dir = direction(rect, r, size);
    if (dir < 0)
      continue;
    const auto x = dir == 1 || dir == 3;
    const auto a = high(rect, x) - low(rect, x);
    const auto b = high(r, x) - low(r, x);
    const auto alignment = std::min(a, b) / std::max(a, b);
    if (alignment > score)
    {
      score = alignment;
      best = candidate;
    }
  }
  if (best < 0)
    return false;
  // A failed recursive close must leave the retained remainder untouched.
  const auto before = plan;
  if (joinInto(plan, best, id, size, true, budget))
    return true;
  plan = before;
  return false;
}

bool joinInto(
  Plan& plan,
  const int source,
  const int target,
  const QSizeF& size,
  const bool closeRemainders,
  int& budget)
{
  if (
    --budget < 0 || source == target || !plan.rects.contains(source)
    || !plan.rects.contains(target))
    return false;
  const auto dir = direction(plan.rects.at(source), plan.rects.at(target), size);
  if (dir < 0)
    return false;
  const auto crossX = dir == 1 || dir == 3;
  const auto tolerance =
    crossX ? MinimumWidth / size.width() : HeaderHeight / size.height();
  auto a = plan.rects.at(source);
  auto b = plan.rects.at(target);
  const auto sourceOverhang = low(a, crossX) < low(b, crossX) - Epsilon
                              || high(a, crossX) > high(b, crossX) + Epsilon;
  auto remainders = std::vector<int>{};
  for (const auto lower : {true, false})
  {
    const auto edgeA = lower ? low(a, crossX) : high(a, crossX);
    const auto edgeB = lower ? low(b, crossX) : high(b, crossX);
    if (std::abs(edgeA - edgeB) + Epsilon < tolerance)
      continue;
    const auto trimA = lower ? edgeA < edgeB : edgeA > edgeB;
    auto& r = trimA ? a : b;
    const auto cut = trimA ? edgeB : edgeA;
    auto remainder = r;
    if (lower)
    {
      setHigh(remainder, crossX, cut);
      setLow(r, crossX, cut);
    }
    else
    {
      setLow(remainder, crossX, cut);
      setHigh(r, crossX, cut);
    }
    remainders.push_back(duplicate(plan, trimA ? source : target, remainder));
  }
  plan.rects[source] = a;
  plan.rects[target] = b;
  // Small misalignments move the shared screen edges, including neighboring areas.
  for (const auto lower : {true, false})
  {
    const auto edgeA = lower ? low(a, crossX) : high(a, crossX);
    const auto edgeB = lower ? low(b, crossX) : high(b, crossX);
    if (near(edgeA, edgeB))
      continue;
    for (const auto& [id, r] : plan.rects)
      if (
        id != source && id != target && high(r, crossX) - low(r, crossX) < tolerance
        && (near(low(r, crossX), std::min(edgeA, edgeB)) || near(high(r, crossX), std::max(edgeA, edgeB))))
        return false;
    const auto average = (edgeA + edgeB) / 2;
    for (auto& [id, r] : plan.rects)
    {
      if (near(low(r, crossX), edgeA) || near(low(r, crossX), edgeB))
        setLow(r, crossX, average);
      if (near(high(r, crossX), edgeA) || near(high(r, crossX), edgeB))
        setHigh(r, crossX, average);
    }
    a = plan.rects.at(source);
    b = plan.rects.at(target);
  }
  plan.rects[source] = plan.rects.at(source).united(plan.rects.at(target));
  plan.rects.erase(target);
  if (closeRemainders || sourceOverhang)
  {
    for (const auto id : remainders)
    {
      if (!plan.rects.contains(id))
        continue;
      const auto closed = plan.rects.at(id);
      if (closeArea(plan, id, source, size, budget))
        plan.closed.push_back(closed);
      // A remainder with no suitable other neighbor is retained, as in Blender.
    }
  }
  return true;
}

void finish(Plan& plan, const QSizeF& size)
{
  plan.valid = validTiling(plan.rects);
  for (const auto& [id, r] : plan.rects)
    plan.valid &= r.width() * size.width() >= MinimumWidth - 1
                  && r.height() * size.height() >= HeaderHeight - 1;
}
} // namespace

bool validTiling(const Rects& rects)
{
  if (rects.empty() || rects.size() > MaximumAreas)
    return false;
  auto area = 0.0;
  for (auto it = rects.begin(); it != rects.end(); ++it)
  {
    const auto& r = it->second;
    if (
      !std::isfinite(r.x()) || !std::isfinite(r.y()) || !std::isfinite(r.width())
      || !std::isfinite(r.height()) || r.width() <= Epsilon || r.height() <= Epsilon
      || r.left() < -Epsilon || r.top() < -Epsilon || r.right() > 1 + Epsilon
      || r.bottom() > 1 + Epsilon)
      return false;
    area += r.width() * r.height();
    for (auto next = std::next(it); next != rects.end(); ++next)
    {
      const auto overlap = r.intersected(next->second);
      if (overlap.width() > Epsilon && overlap.height() > Epsilon)
        return false;
    }
  }
  return near(area, 1);
}

int direction(const QRectF& source, const QRectF& target, const QSizeF& size)
{
  if (size.width() <= 0 || size.height() <= 0)
    return -1;
  const auto overlapX =
    std::min(source.right(), target.right()) - std::max(source.left(), target.left());
  const auto overlapY =
    std::min(source.bottom(), target.bottom()) - std::max(source.top(), target.top());
  const auto minX =
    std::min({MinimumWidth / size.width(), source.width(), target.width()});
  const auto minY =
    std::min({HeaderHeight / size.height(), source.height(), target.height()});
  if (near(source.left(), target.right()) && overlapY + Epsilon >= minY)
    return 0;
  if (near(source.top(), target.bottom()) && overlapX + Epsilon >= minX)
    return 1;
  if (near(source.right(), target.left()) && overlapY + Epsilon >= minY)
    return 2;
  if (near(source.bottom(), target.top()) && overlapX + Epsilon >= minX)
    return 3;
  return -1;
}

Plan join(const Rects& rects, const int source, const int target, const QSizeF& size)
{
  auto plan = Plan{};
  plan.rects = rects;
  plan.nextId = rects.empty() ? 0 : rects.rbegin()->first + 1;
  if (!validTiling(rects) || size.width() <= 0 || size.height() <= 0)
    return plan;
  auto budget = 128;
  if (joinInto(plan, source, target, size, false, budget))
  {
    plan.highlight = plan.rects.at(source);
    finish(plan, size);
  }
  return plan;
}

Plan dock(
  const Rects& rects,
  const int source,
  const int target,
  const Dock side,
  const double fraction,
  const QSizeF& size)
{
  auto plan = Plan{};
  plan.rects = rects;
  plan.nextId = rects.empty() ? 0 : rects.rbegin()->first + 1;
  if (
    !validTiling(rects) || size.width() <= 0 || size.height() <= 0 || source == target
    || !rects.contains(source) || !rects.contains(target) || side == Dock::None
    || !std::isfinite(fraction) || fraction <= 0 || fraction >= 1)
    return plan;
  const auto a = rects.at(source);
  const auto b = rects.at(target);
  auto slot = b;
  auto rest = b;
  if (side == Dock::Left)
  {
    slot.setWidth(b.width() * fraction);
    rest.setLeft(slot.right());
  }
  if (side == Dock::Right)
  {
    slot.setLeft(b.right() - b.width() * fraction);
    rest.setRight(slot.left());
  }
  if (side == Dock::Top)
  {
    slot.setHeight(b.height() * fraction);
    rest.setTop(slot.bottom());
  }
  if (side == Dock::Bottom)
  {
    slot.setTop(b.bottom() - b.height() * fraction);
    rest.setBottom(slot.top());
  }
  const auto aligned = (near(a.left(), b.left()) && near(a.right(), b.right()))
                       || (near(a.top(), b.top()) && near(a.bottom(), b.bottom()));
  if (aligned && direction(a, slot, size) >= 0)
  {
    if (side == Dock::Center)
      return join(rects, source, target, size);
    plan.rects[target] = rest;
    const auto slotId = duplicate(plan, target, slot);
    auto budget = 128;
    if (!joinInto(plan, source, slotId, size, false, budget))
      return plan;
  }
  else
  {
    plan.rects[source] = slot;
    if (side == Dock::Center)
      plan.rects.erase(target);
    else
      plan.rects[target] = rest;
    // Docking swaps editor contents before closing the old source location.
    const auto hole = duplicate(plan, target, a);
    auto budget = 128;
    if (!closeArea(plan, hole, -1, size, budget))
      return plan;
    plan.closed.push_back(a);
  }
  plan.highlight = plan.rects.at(source);
  finish(plan, size);
  return plan;
}

Dock dockingTarget(
  const QRectF& source,
  const QRectF& target,
  const QPointF& point,
  const QSizeF& size,
  const bool alt,
  const bool ctrl,
  double& fraction)
{
  const auto dir = direction(source, target, size);
  const auto x = (point.x() - target.left()) / target.width();
  const auto y = (point.y() - target.top()) / target.height();
  const auto width = target.width() * size.width();
  const auto height = target.height() * size.height();
  if ((dir == 0 || dir == 2) && point.y() >= source.top() && point.y() <= source.bottom())
  {
    const auto distance = (dir == 2 ? x : 1 - x) * width;
    if (width < 2 * MinimumWidth || distance < std::min(width * 0.25, 5.0 * MinimumWidth))
      return Dock::None;
  }
  if ((dir == 1 || dir == 3) && point.x() >= source.left() && point.x() <= source.right())
  {
    const auto distance = (dir == 3 ? y : 1 - y) * height;
    if (
      height < 2 * HeaderHeight || distance < std::min(height * 0.25, 5.0 * HeaderHeight))
      return Dock::None;
  }
  auto side = Dock::Center;
  if (width < 6 * MinimumWidth)
  {
    if (y <= 0.4)
      side = Dock::Top;
    else if (y >= 0.6)
      side = Dock::Bottom;
  }
  else if (height < 6 * HeaderHeight)
  {
    if (x <= 0.4)
      side = Dock::Left;
    else if (x >= 0.6)
      side = Dock::Right;
  }
  else if (!(x > 0.4 && x < 0.6 && y > 0.4 && y < 0.6))
  {
    const auto distance = std::min({x, 1 - x, y, 1 - y});
    side = distance == x       ? Dock::Left
           : distance == 1 - x ? Dock::Right
           : distance == y     ? Dock::Top
                               : Dock::Bottom;
  }
  fraction = 0.5;
  if (side != Dock::Center)
  {
    const auto horizontal = side == Dock::Left || side == Dock::Right;
    const auto distance = side == Dock::Left    ? x
                          : side == Dock::Right ? 1 - x
                          : side == Dock::Top   ? y
                                                : 1 - y;
    fraction =
      std::max(
        distance, 1.5 * (horizontal ? MinimumWidth / width : HeaderHeight / height))
      * (alt || ctrl ? 2.5 : 2.0);
    if (!alt && fraction >= 0.4375 && fraction < 0.5625)
      fraction = 0.5;
    if (ctrl)
    {
      if (fraction < 0.4375)
        fraction = std::max(0.125, std::round(fraction * 8) / 8);
      else if (fraction >= 0.5625)
        fraction = std::min(0.875, std::round(fraction * 8) / 8);
    }
  }
  return side;
}
} // namespace tb::ui::areaGeometry
