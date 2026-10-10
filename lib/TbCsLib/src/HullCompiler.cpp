/*
 Copyright (C) 2026 KreedzBroom contributors

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

#include "HullCompiler.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace tb::cs {
namespace {

// Tolerances follow the original compilers.
constexpr double kOnEpsilon = 0.01;          // hlbsp ON_EPSILON
constexpr double kNormalEpsilon = 0.00001;   // hlcsg NORMAL_EPSILON
constexpr double kDirEpsilon = 0.0001;       // hlcsg DIR_EPSILON (plane merging)
constexpr double kPlaneDistEpsilon = 0.04;   // hlcsg FindIntPlane DIST_EPSILON
constexpr double kColinearEpsilon = 0.04;    // hlcsg ON_EPSILON
// Per compiler: VectorCompare EQUAL_EPSILON and FLOOR_Z (cliptype precise).
constexpr double kVhltEqualEpsilon = 0.004;
constexpr double kZhltEqualEpsilon = 0.001;
constexpr double kVhltFloorZ = 0.7;
constexpr double kZhltFloorZ = 0.5;
constexpr double kBogusRange = 131072.0;     // > any legal coordinate
constexpr double kWorldLimit = 65536.0;      // open brushes run past this
constexpr std::size_t kSpatialSplitThreshold = 12;
constexpr std::size_t kMaxSplitCandidates = 48;

// ---------------------------------------------------------------- vector math

Vec3d operator+(const Vec3d& a, const Vec3d& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3d operator-(const Vec3d& a, const Vec3d& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3d operator-(const Vec3d& a) { return {-a.x, -a.y, -a.z}; }
Vec3d operator*(const Vec3d& a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(const Vec3d& a, const Vec3d& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3d cross(const Vec3d& a, const Vec3d& b)
{
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double length(const Vec3d& a) { return std::sqrt(dot(a, a)); }

Vec3d axisVector(int axis, double sign)
{
  Vec3d v;
  v[axis] = sign;
  return v;
}

// hlcsg FindIntPlane: unit normal, made exactly axial when within DIR_EPSILON
// of an axis (PlaneTypeForNormal), distance kept through a point on the plane.
BrushPlane snapPlane(BrushPlane p)
{
  const double len = length(p.normal);
  if (len > 0.0)
  {
    p.normal = p.normal * (1.0 / len);
    p.dist /= len;
  }
  for (int i = 0; i < 3; ++i)
  {
    if (
      std::fabs(p.normal[i]) > 1.0 - kDirEpsilon
      && std::fabs(p.normal[(i + 1) % 3]) < kDirEpsilon
      && std::fabs(p.normal[(i + 2) % 3]) < kDirEpsilon)
    {
      const Vec3d origin = p.normal * p.dist;
      p.normal = axisVector(i, p.normal[i] > 0.0 ? 1.0 : -1.0);
      p.dist = dot(origin, p.normal);
      break;
    }
  }
  return p;
}

// FindIntPlane's merge test: coplanar faces of different brushes share a plane.
bool samePlane(const BrushPlane& a, const BrushPlane& b)
{
  return std::fabs(a.normal.x - b.normal.x) < kDirEpsilon
         && std::fabs(a.normal.y - b.normal.y) < kDirEpsilon
         && std::fabs(a.normal.z - b.normal.z) < kDirEpsilon
         && std::fabs(a.dist - b.dist) < kPlaneDistEpsilon;
}

int axialType(const Vec3d& n)
{
  for (int i = 0; i < 3; ++i)
  {
    if (n[i] == 1.0 || n[i] == -1.0)
    {
      return i;
    }
  }
  return -1;
}

// ------------------------------------------------------------------ windings

using Winding = std::vector<Vec3d>;

// Quake's BaseWindingForPlane: a huge quad lying on the plane.
Winding baseWinding(const BrushPlane& p)
{
  int major = 0;
  double best = -1.0;
  for (int i = 0; i < 3; ++i)
  {
    if (std::fabs(p.normal[i]) > best)
    {
      best = std::fabs(p.normal[i]);
      major = i;
    }
  }
  Vec3d up = major == 2 ? Vec3d{1, 0, 0} : Vec3d{0, 0, 1};
  up = up - p.normal * dot(up, p.normal);
  up = up * (1.0 / length(up));
  const Vec3d org = p.normal * p.dist;
  const Vec3d right = cross(up, p.normal) * kBogusRange;
  up = up * kBogusRange;
  return {org - right + up, org + right + up, org + right - up, org - right - up};
}

// Keeps the part of `w` behind `p` (Sutherland-Hodgman).
void clipWindingBack(Winding& w, const BrushPlane& p)
{
  if (w.empty())
  {
    return;
  }
  const std::size_t n = w.size();
  std::vector<double> dists(n);
  bool anyFront = false;
  bool anyBack = false;
  for (std::size_t i = 0; i < n; ++i)
  {
    dists[i] = dot(w[i], p.normal) - p.dist;
    anyFront |= dists[i] > kOnEpsilon;
    anyBack |= dists[i] < -kOnEpsilon;
  }
  if (!anyFront)
  {
    return; // entirely behind or on
  }
  if (!anyBack)
  {
    w.clear();
    return;
  }

  Winding out;
  out.reserve(n + 2);
  for (std::size_t i = 0; i < n; ++i)
  {
    const Vec3d& a = w[i];
    const Vec3d& b = w[(i + 1) % n];
    const double da = dists[i];
    const double db = dists[(i + 1) % n];
    if (da <= kOnEpsilon)
    {
      out.push_back(a);
    }
    if ((da > kOnEpsilon && db < -kOnEpsilon) || (da < -kOnEpsilon && db > kOnEpsilon))
    {
      const double t = da / (da - db);
      Vec3d mid = a + (b - a) * t;
      // keep exact coordinates on axial planes
      for (int k = 0; k < 3; ++k)
      {
        if (p.normal[k] == 1.0)
        {
          mid[k] = p.dist;
        }
        else if (p.normal[k] == -1.0)
        {
          mid[k] = -p.dist;
        }
      }
      out.push_back(mid);
    }
  }
  w = out.size() >= 3 ? std::move(out) : Winding{};
}

// hlcsg Winding::RemoveColinearPoints: drops the middle point of any three
// consecutive points forming a triangle thinner than `epsilon`.
void removeColinearPoints(Winding& w, double epsilon)
{
  for (std::size_t i = 0; i < w.size();)
  {
    const std::size_t n = w.size();
    const Vec3d v1 = w[i] - w[(i + n - 1) % n];
    const Vec3d v2 = w[(i + 1) % n] - w[i];
    const double d = dot(v1, v2);
    if (
      d * d >= dot(v1, v1) * dot(v2, v2)
                 - epsilon * epsilon * (dot(v1, v1) + dot(v2, v2) + epsilon * epsilon))
    {
      w.erase(w.begin() + static_cast<std::ptrdiff_t>(i));
      i = 0;
      continue;
    }
    ++i;
  }
}

bool sameVertex(const Vec3d& a, const Vec3d& b, double epsilon)
{
  return std::fabs(a.x - b.x) <= epsilon && std::fabs(a.y - b.y) <= epsilon
         && std::fabs(a.z - b.z) <= epsilon;
}

// ----------------------------------------------------------------- polytopes

struct Polytope
{
  std::vector<BrushPlane> faces; // non-redundant planes only
  std::vector<Winding> windings; // one per face
  std::vector<Vec3d> verts;
  Vec3d mins, maxs;
};

std::optional<Polytope> buildPolytope(std::span<const BrushPlane> planes)
{
  Polytope poly;
  poly.mins = {kBogusRange, kBogusRange, kBogusRange};
  poly.maxs = {-kBogusRange, -kBogusRange, -kBogusRange};

  for (std::size_t i = 0; i < planes.size(); ++i)
  {
    bool duplicate = false;
    for (std::size_t j = 0; j < i && !duplicate; ++j)
    {
      duplicate = samePlane(planes[i], planes[j]);
    }
    if (duplicate)
    {
      continue;
    }

    Winding w = baseWinding(planes[i]);
    for (std::size_t j = 0; j < planes.size() && !w.empty(); ++j)
    {
      if (j != i && !samePlane(planes[i], planes[j]))
      {
        clipWindingBack(w, planes[j]);
      }
    }
    removeColinearPoints(w, kColinearEpsilon);
    if (w.size() < 3)
    {
      continue; // redundant plane, it does not touch the brush
    }

    poly.faces.push_back(planes[i]);
    for (std::size_t k = 0; k < w.size(); ++k)
    {
      poly.verts.push_back(w[k]);
      for (int a = 0; a < 3; ++a)
      {
        poly.mins[a] = std::min(poly.mins[a], w[k][a]);
        poly.maxs[a] = std::max(poly.maxs[a], w[k][a]);
      }
    }
    poly.windings.push_back(std::move(w));
  }

  if (poly.faces.size() < 4)
  {
    return std::nullopt;
  }
  for (int a = 0; a < 3; ++a)
  {
    if (poly.maxs[a] - poly.mins[a] < kOnEpsilon || poly.mins[a] < -kWorldLimit
        || poly.maxs[a] > kWorldLimit)
    {
      return std::nullopt; // flat or open (unbounded) brush
    }
  }
  return poly;
}

// How hlcsg pushes a plane out by the hull box (mins, maxs).
enum class Offset
{
  Corner,       // by the box corner the normal points at (simple, smallest)
  NormalScaled, // each axis scaled by the normal again (legacy, normalized)
  Vertical,     // straight up by the box height (precise floors)
};

double offsetDistance(const Vec3d& n, Offset offset, const Vec3d& mins, const Vec3d& maxs)
{
  Vec3d shift;
  for (int i = 0; i < 3; ++i)
  {
    switch (offset)
    {
    case Offset::Corner:
      shift[i] = n[i] > 0.0 ? maxs[i] : mins[i];
      break;
    case Offset::NormalScaled:
      shift[i] = n[i] != 0.0 ? n[i] * (n[i] > 0.0 ? maxs[i] : -mins[i]) : 0.0;
      break;
    case Offset::Vertical:
      shift[i] = i == 2 ? maxs[i] : 0.0;
      break;
    }
  }
  return dot(shift, n);
}

bool hasPlane(const std::vector<BrushPlane>& planes, const BrushPlane& p)
{
  return std::any_of(
    planes.begin(), planes.end(), [&](const BrushPlane& q) { return samePlane(p, q); });
}

// The face other than `face` whose winding holds both ends of an edge.
std::optional<std::size_t> edgeNeighbour(
  const Polytope& poly,
  std::size_t face,
  const Vec3d& start,
  const Vec3d& end,
  double epsilon)
{
  for (std::size_t other = 0; other < poly.faces.size(); ++other)
  {
    if (other == face)
    {
      continue;
    }
    const Winding& w = poly.windings[other];
    const auto has = [&](const Vec3d& v) {
      return std::any_of(
        w.begin(), w.end(), [&](const Vec3d& p) { return sameVertex(p, v, epsilon); });
    };
    if (has(start) && has(end))
    {
      return other;
    }
  }
  return std::nullopt;
}

// hlcsg ExpandBrush (brush.cpp): the planes of one brush grown by a hull box,
// with every -cliptype, as VHLT or ZHLT do it. The result is not the exact
// Minkowski sum; the differences are the cliptype artifacts mappers know
// (legacy slopes etc.). The two compilers differ in simple, normalized and
// precise: ZHLT's precise gives floors (normal z > 0.5) the legacy offset
// where VHLT's (z > 0.7) moves them straight up, and ZHLT only bevels the
// edges of faces whose normal has no zero component.
std::vector<BrushPlane> expandBrush(
  const Polytope& poly,
  Compiler compiler,
  ClipType clipType,
  const Vec3d& mins,
  const Vec3d& maxs)
{
  const bool zhlt = compiler == Compiler::Zhlt;
  const double floorZ = zhlt ? kZhltFloorZ : kVhltFloorZ;
  const bool normalScaled = clipType == ClipType::Legacy || clipType == ClipType::Normalized;
  const auto offsetFor = [&](const Vec3d& n, bool edge) {
    if (clipType == ClipType::Precise && n.z > floorZ)
    {
      return zhlt ? Offset::NormalScaled : Offset::Vertical;
    }
    // legacy never adds edge bevels, normalized uses its rule for them too
    return (edge ? clipType == ClipType::Normalized : normalScaled) ? Offset::NormalScaled
                                                                    : Offset::Corner;
  };

  std::vector<BrushPlane> planes;
  bool axialBevel[3][2] = {};

  // step 1: player box corner against brush face. Axial faces are left to the
  // bounding box of step 3; BEVEL faces stay where they are.
  for (const BrushPlane& face : poly.faces)
  {
    if (const int axis = axialType(face.normal); axis >= 0)
    {
      if (face.bevel)
      {
        axialBevel[axis][face.normal[axis] > 0.0 ? 1 : 0] = true;
      }
      continue;
    }
    BrushPlane plane{face.normal, face.dist};
    if (!face.bevel)
    {
      plane.dist += offsetDistance(face.normal, offsetFor(face.normal, false), mins, maxs);
    }
    planes.push_back(plane);
  }

  // step 2: player box edge against brush edge. Edges where the two face
  // normals change sign along an axis get a bevel plane for that axis.
  if (
    clipType == ClipType::Simple || clipType == ClipType::Precise
    || clipType == ClipType::Normalized)
  {
    for (std::size_t f = 0; f < poly.faces.size(); ++f)
    {
      const Vec3d& normal = poly.faces[f].normal;
      if (zhlt && (normal.x == 0.0 || normal.y == 0.0 || normal.z == 0.0))
      {
        continue; // ZHLT: only completely non-axial faces
      }
      const Winding& w = poly.windings[f];
      for (std::size_t k = 0; k < w.size(); ++k)
      {
        const Vec3d& start = w[k];
        const Vec3d& end = w[(k + 1) % w.size()];
        const auto other = edgeNeighbour(
          poly, f, start, end, zhlt ? kZhltEqualEpsilon : kVhltEqualEpsilon);
        if (!other)
        {
          continue; // illegal brush, hlcsg warns and skips the edge
        }
        const Vec3d& otherNormal = poly.faces[*other].normal;
        for (int dir = 0; dir < 3; ++dir)
        {
          if (normal[dir] * otherNormal[dir] >= (zhlt ? 0.0 : -kNormalEpsilon))
          {
            continue;
          }
          Vec3d n = cross(end - start, axisVector(dir, normal[dir] > 0.0 ? -1.0 : 1.0));
          const double len = length(n);
          if (len < kNormalEpsilon)
          {
            continue;
          }
          n = n * (1.0 / len);
          if (zhlt ? axialType(n) >= 0
                   : std::fabs(n[(dir + 1) % 3]) <= kNormalEpsilon
                       || std::fabs(n[(dir + 2) % 3]) <= kNormalEpsilon)
          {
            continue; // coincides with an axial plane (ZHLT: exactly axial)
          }
          const BrushPlane bevel = snapPlane(
            {n, dot(start, n) + offsetDistance(n, offsetFor(n, true), mins, maxs)});
          if (!hasPlane(planes, bevel))
          {
            planes.push_back(bevel);
          }
        }
      }
    }
  }

  // step 3: player box face against brush vertex, i.e. the expanded bounding
  // box. Axial BEVEL faces keep their side of the box unexpanded.
  for (int axis = 0; axis < 3; ++axis)
  {
    const double lo = axialBevel[axis][0] ? poly.mins[axis] : poly.mins[axis] + mins[axis];
    planes.push_back({axisVector(axis, -1.0), -lo});
  }
  for (int axis = 0; axis < 3; ++axis)
  {
    const double hi = axialBevel[axis][1] ? poly.maxs[axis] : poly.maxs[axis] + maxs[axis];
    planes.push_back({axisVector(axis, 1.0), hi});
  }
  return planes;
}

// ---------------------------------------------------------------- plane table

// Planes are stored in pairs (2k, 2k+1) with 2k+1 the flipped copy; nodes only
// reference the even, "positive" one (axial planes face +axis, like hlbsp).
class PlaneTable
{
public:
  int findOrAdd(BrushPlane p)
  {
    p = snapPlane(p);
    bool flipped = false;
    const int type = axialType(p.normal);
    const int major = type >= 0 ? type : majorAxis(p.normal);
    if (p.normal[major] < 0.0)
    {
      p.normal = -p.normal;
      p.dist = -p.dist;
      flipped = true;
    }

    const auto bucket = static_cast<std::int64_t>(std::floor(p.dist));
    for (std::int64_t b = bucket - 1; b <= bucket + 1; ++b)
    {
      const auto [first, last] = buckets_.equal_range(b);
      for (auto it = first; it != last; ++it)
      {
        if (samePlane(planes_[static_cast<std::size_t>(it->second)], p))
        {
          return it->second + (flipped ? 1 : 0);
        }
      }
    }

    const int index = static_cast<int>(planes_.size());
    planes_.push_back(p);
    planes_.push_back({-p.normal, -p.dist});
    buckets_.emplace(bucket, index);
    return index + (flipped ? 1 : 0);
  }

  const BrushPlane& operator[](int index) const
  {
    return planes_[static_cast<std::size_t>(index)];
  }

  std::vector<ClipPlane> toClipPlanes() const
  {
    std::vector<ClipPlane> out;
    out.reserve(planes_.size());
    for (const BrushPlane& p : planes_)
    {
      ClipPlane cp{};
      for (int i = 0; i < 3; ++i)
      {
        cp.normal[i] = static_cast<float>(p.normal[i]);
        if (cp.normal[i] < 0.0f)
        {
          cp.signbits = static_cast<std::uint8_t>(cp.signbits | (1u << i));
        }
      }
      cp.dist = static_cast<float>(p.dist);
      const int type = axialType(p.normal);
      cp.type = static_cast<std::uint8_t>(
        type >= 0 ? type : kPlaneAnyX + majorAxis(p.normal));
      out.push_back(cp);
    }
    return out;
  }

private:
  static int majorAxis(const Vec3d& n)
  {
    int major = 0;
    for (int i = 1; i < 3; ++i)
    {
      if (std::fabs(n[i]) > std::fabs(n[major]))
      {
        major = i;
      }
    }
    return major;
  }

  std::vector<BrushPlane> planes_;
  std::unordered_multimap<std::int64_t, int> buckets_;
};

// ------------------------------------------------------------ contents rules

int contentsRank(int contents)
{
  // Same ordering the engine uses (RankForContents): solid wins over liquids.
  switch (contents)
  {
  case kContentsEmpty:
    return 0;
  case kContentsWater:
    return 1;
  case kContentsSlime:
    return 9;
  case kContentsLava:
    return 10;
  case kContentsSolid:
    return 12;
  default:
    return 13;
  }
}

int normalizeContents(int contents)
{
  switch (contents)
  {
  case kContentsSolid:
  case kContentsClip:
  case kContentsWater:
  case kContentsSlime:
  case kContentsLava:
    return contents;
  default:
    return kContentsSolid;
  }
}

// Contents a brush contributes to BSP hull `hull`, or EMPTY when absent.
// hlcsg: CLIP brushes exist only in hulls 1-3, liquids only in hull 0.
int hullContents(int brushContents, int hull)
{
  switch (brushContents)
  {
  case kContentsSolid:
    return kContentsSolid;
  case kContentsClip:
    return hull == 0 ? kContentsEmpty : kContentsSolid;
  default:
    return hull == 0 ? brushContents : kContentsEmpty;
  }
}

// ---------------------------------------------------------------- BSP build

struct Volume
{
  std::vector<int> planes; // oriented plane indices
  std::vector<Vec3d> verts;
  Vec3d mins, maxs;
  int contents = kContentsSolid;
  int rank = 0;
};

struct Item
{
  const Volume* volume;
  std::vector<int> remaining; // planes not yet applied on the path to here
};

enum Side : unsigned
{
  kFront = 1,
  kBack = 2,
  kBoth = 3
};

class TreeBuilder
{
public:
  TreeBuilder(PlaneTable& planes, int hullIndex)
    : planes_(planes)
    , hullIndex_(hullIndex)
  {
  }

  int build(std::vector<Item> items) { return buildRecursive(std::move(items), kContentsEmpty); }

  std::vector<ClipNode>& nodes() { return nodes_; }

private:
  // Which side(s) of canonical plane `plane` the item's region lies on.
  Side classify(const Item& item, int plane) const
  {
    for (const int p : item.remaining)
    {
      if (p == plane)
      {
        return kBack; // it is one of the volume's own faces
      }
      if (p == (plane ^ 1))
      {
        return kFront;
      }
    }

    const BrushPlane& p = planes_[plane];
    const int axis = axialType(p.normal);
    double dmin = 0.0;
    double dmax = 0.0;
    if (axis >= 0)
    {
      dmin = item.volume->mins[axis] - p.dist;
      dmax = item.volume->maxs[axis] - p.dist;
    }
    else
    {
      dmin = std::numeric_limits<double>::max();
      dmax = std::numeric_limits<double>::lowest();
      for (const Vec3d& v : item.volume->verts)
      {
        const double d = dot(p.normal, v) - p.dist;
        dmin = std::min(dmin, d);
        dmax = std::max(dmax, d);
      }
    }
    if (dmax <= kOnEpsilon)
    {
      return kBack;
    }
    if (dmin >= -kOnEpsilon)
    {
      return kFront;
    }
    return kBoth;
  }

  struct Score
  {
    std::size_t front = 0, back = 0, both = 0;
  };

  Score score(const std::vector<Item>& items, int plane) const
  {
    Score s;
    for (const Item& item : items)
    {
      switch (classify(item, plane))
      {
      case kFront:
        ++s.front;
        break;
      case kBack:
        ++s.back;
        break;
      case kBoth:
        ++s.both;
        break;
      }
    }
    return s;
  }

  int chooseSplit(const std::vector<Item>& items)
  {
    const std::size_t n = items.size();

    // Large sets: an axial median cut (kd-tree style) to shrink the problem
    // quickly. Splits in empty space do not change trace results.
    if (n >= kSpatialSplitThreshold)
    {
      Vec3d mins{kBogusRange, kBogusRange, kBogusRange};
      Vec3d maxs{-kBogusRange, -kBogusRange, -kBogusRange};
      for (const Item& item : items)
      {
        for (int a = 0; a < 3; ++a)
        {
          mins[a] = std::min(mins[a], item.volume->mins[a]);
          maxs[a] = std::max(maxs[a], item.volume->maxs[a]);
        }
      }
      int axis = 0;
      for (int a = 1; a < 3; ++a)
      {
        if (maxs[a] - mins[a] > maxs[axis] - mins[axis])
        {
          axis = a;
        }
      }
      std::vector<double> centers;
      centers.reserve(n);
      for (const Item& item : items)
      {
        centers.push_back(0.5 * (item.volume->mins[axis] + item.volume->maxs[axis]));
      }
      std::nth_element(centers.begin(), centers.begin() + static_cast<std::ptrdiff_t>(n / 2), centers.end());
      const double cut = std::floor(centers[n / 2] + 0.5);
      const int plane = planes_.findOrAdd({axisVector(axis, 1.0), cut});
      const Score s = score(items, plane & ~1);
      if (s.front + s.both < n && s.back + s.both < n)
      {
        return plane & ~1;
      }
    }

    // Otherwise pick one of the remaining brush planes, axial ones first.
    std::vector<int> candidates;
    for (const bool wantAxial : {true, false})
    {
      for (const Item& item : items)
      {
        for (const int p : item.remaining)
        {
          const int canonical = p & ~1;
          if ((axialType(planes_[canonical].normal) >= 0) != wantAxial)
          {
            continue;
          }
          if (std::find(candidates.begin(), candidates.end(), canonical) == candidates.end())
          {
            candidates.push_back(canonical);
            if (candidates.size() >= kMaxSplitCandidates)
            {
              break;
            }
          }
        }
        if (candidates.size() >= kMaxSplitCandidates)
        {
          break;
        }
      }
    }

    int best = candidates.front();
    std::size_t bestValue = std::numeric_limits<std::size_t>::max();
    for (const int candidate : candidates)
    {
      const Score s = score(items, candidate);
      const std::size_t imbalance =
        s.front > s.back ? s.front - s.back : s.back - s.front;
      const std::size_t value = s.both * 8 + imbalance;
      if (value < bestValue)
      {
        bestValue = value;
        best = candidate;
      }
    }
    return best;
  }

  int buildRecursive(std::vector<Item> items, int contents)
  {
    // A volume whose every plane is applied contains this whole region.
    int rank = contentsRank(contents);
    for (const Item& item : items)
    {
      if (item.remaining.empty() && item.volume->rank > rank)
      {
        rank = item.volume->rank;
        contents = item.volume->contents;
      }
    }
    // Volumes that cannot override the contents are irrelevant below here.
    std::erase_if(items, [&](const Item& item) { return item.volume->rank <= rank; });
    if (items.empty())
    {
      return contents;
    }

    const int plane = chooseSplit(items);
    std::vector<Item> front;
    std::vector<Item> back;
    for (Item& item : items)
    {
      const Side side = classify(item, plane);
      if (side == kBoth)
      {
        front.push_back(item);
        back.push_back(std::move(item));
        continue;
      }
      std::erase_if(item.remaining, [&](int p) { return (p & ~1) == plane; });
      (side == kFront ? front : back).push_back(std::move(item));
    }

    const std::size_t index = nodes_.size();
    if (index >= static_cast<std::size_t>(kMaxClipNodes))
    {
      throw HullLimitError(
        "hull " + std::to_string(hullIndex_) + " exceeds "
        + std::to_string(kMaxClipNodes) + " clipnodes");
    }
    nodes_.push_back({plane, {0, 0}});
    const int frontChild = buildRecursive(std::move(front), contents);
    const int backChild = buildRecursive(std::move(back), contents);
    nodes_[index].children[0] = frontChild;
    nodes_[index].children[1] = backChild;
    return static_cast<int>(index);
  }

  PlaneTable& planes_;
  int hullIndex_;
  std::vector<ClipNode> nodes_;
};

struct PreparedBrush
{
  Polytope poly;
  int contents;
  unsigned hulls;
};

} // namespace

std::unique_ptr<BrushModel> compileBrushModel(
  std::span<const BrushDesc> brushes,
  const HullCompileOptions& options,
  HullCompileStats& stats)
{
  std::vector<PreparedBrush> prepared;
  prepared.reserve(brushes.size());
  for (const BrushDesc& brush : brushes)
  {
    std::vector<BrushPlane> planes;
    planes.reserve(brush.planes.size());
    for (const BrushPlane& p : brush.planes)
    {
      planes.push_back(snapPlane(p));
    }
    if (auto poly = buildPolytope(planes))
    {
      prepared.push_back({std::move(*poly), normalizeContents(brush.contents), brush.hulls});
    }
    else
    {
      ++stats.skippedBrushes;
    }
  }

  auto model = std::make_unique<BrushModel>();
  for (std::size_t i = 0; i < prepared.size(); ++i)
  {
    for (int a = 0; a < 3; ++a)
    {
      const auto lo = static_cast<float>(prepared[i].poly.mins[a]);
      const auto hi = static_cast<float>(prepared[i].poly.maxs[a]);
      model->mins[a] = i == 0 ? lo : std::min(model->mins[a], lo);
      model->maxs[a] = i == 0 ? hi : std::max(model->maxs[a], hi);
    }
  }

  for (int hull = 0; hull < kNumHulls; ++hull)
  {
    PlaneTable planeTable;
    std::vector<Volume> volumes;
    volumes.reserve(prepared.size());

    for (const PreparedBrush& brush : prepared)
    {
      const int contents = hullContents(brush.contents, hull);
      if (contents == kContentsEmpty || (brush.hulls & (1u << hull)) == 0)
      {
        continue;
      }

      std::optional<Polytope> shape;
      if (hull == 0)
      {
        shape = brush.poly;
      }
      else
      {
        shape = buildPolytope(expandBrush(
          brush.poly,
          options.compiler,
          options.clipType,
          options.hullMins[static_cast<std::size_t>(hull)],
          options.hullMaxs[static_cast<std::size_t>(hull)]));
      }
      if (!shape)
      {
        continue;
      }

      Volume volume;
      for (const BrushPlane& face : shape->faces)
      {
        const int index = planeTable.findOrAdd(face);
        if (std::find(volume.planes.begin(), volume.planes.end(), index) == volume.planes.end())
        {
          volume.planes.push_back(index);
        }
      }
      volume.verts = std::move(shape->verts);
      volume.mins = shape->mins;
      volume.maxs = shape->maxs;
      volume.contents = contents;
      volume.rank = contentsRank(contents);
      volumes.push_back(std::move(volume));
    }

    std::vector<Item> items;
    items.reserve(volumes.size());
    for (const Volume& volume : volumes)
    {
      items.push_back({&volume, volume.planes});
    }

    TreeBuilder builder(planeTable, hull);
    const int root = builder.build(std::move(items));
    std::vector<ClipNode>& nodes = builder.nodes();
    if (root < 0)
    {
      // Uniform hull (e.g. no brushes): a single node with both children leaves.
      const int plane = planeTable.findOrAdd({{0, 0, 1}, 0.0}) & ~1;
      nodes.push_back({plane, {root, root}});
    }
    if (nodes.size() == 1)
    {
      // The engine treats firstclipnode >= lastclipnode as an empty hull.
      nodes.push_back(nodes.front());
    }

    stats.clipnodes[static_cast<std::size_t>(hull)] = nodes.size();
    model->clipnodes[static_cast<std::size_t>(hull)] = std::move(nodes);
    model->planes[static_cast<std::size_t>(hull)] = planeTable.toClipPlanes();

    Hull& h = model->hulls[static_cast<std::size_t>(hull)];
    h.clipnodes = model->clipnodes[static_cast<std::size_t>(hull)].data();
    h.planes = model->planes[static_cast<std::size_t>(hull)].data();
    h.firstclipnode = 0;
    h.lastclipnode = static_cast<int>(model->clipnodes[static_cast<std::size_t>(hull)].size()) - 1;
    for (int a = 0; a < 3; ++a)
    {
      h.clip_mins[a] = static_cast<float>(options.hullMins[static_cast<std::size_t>(hull)][a]);
      h.clip_maxs[a] = static_cast<float>(options.hullMaxs[static_cast<std::size_t>(hull)][a]);
    }
  }

  return model;
}

} // namespace tb::cs
