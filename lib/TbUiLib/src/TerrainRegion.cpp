/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/TerrainRegion.h"

#include <QByteArray>

#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/EditorContext.h"
#include "mdl/GroupNode.h"
#include "mdl/Map.h"
#include "mdl/Selection.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace tb::ui
{
TerrainRegion::TerrainRegion(const vm::vec2d origin_, std::string materialAssetId_)
  : origin{origin_}
  , materialAssetId{std::move(materialAssetId_)}
{
  weights.fill({1, 0, 0});
}

vm::vec2d TerrainRegion::project(const vm::vec3d& local) const
{
  return vm::vec2d{local.x() - origin.x(), origin.y() - local.y()} / UnitsPerTexel;
}

vm::vec3d TerrainRegion::unproject(const QPointF& pixel, const double z) const
{
  return vm::vec3d{
    origin.x() + pixel.x() * UnitsPerTexel, origin.y() - pixel.y() * UnitsPerTexel, z};
}

void TerrainRegion::paint(
  const vm::vec2d& point,
  const double radius,
  const size_t material,
  const double strength,
  const double hardness)
{
  if (radius <= 0 || material >= 3)
    return;
  const auto left = std::max(0, int(std::ceil((point.x() - radius) / Step)));
  const auto right = std::min(Columns, int(std::floor((point.x() + radius) / Step)));
  const auto top = std::max(0, int(std::ceil((point.y() - radius) / Step)));
  const auto bottom = std::min(Rows, int(std::floor((point.y() + radius) / Step)));
  for (auto row = top; row <= bottom; ++row)
  {
    for (auto col = left; col <= right; ++col)
    {
      const auto distance = vm::length(point - vm::vec2d{col * Step, row * Step});
      const auto edge = radius * std::max(1e-6, 1.0 - std::clamp(hardness, 0.0, 1.0));
      const auto t = std::clamp((radius - distance) / edge, 0.0, 1.0);
      const auto amount = std::clamp(strength, 0.0, 1.0) * t * t * (3 - 2 * t);
      auto& w = weights[size_t(row * (Columns + 1) + col)];
      for (auto k = size_t{0}; k < w.size(); ++k)
        w[k] = w[k] * (1 - amount) + (k == material ? amount : 0);
    }
  }
}

const TerrainRegion::Weight& TerrainRegion::weight(const int col, const int row) const
{
  return weights[size_t(row * (Columns + 1) + col)];
}

std::string TerrainRegion::encode() const
{
  auto pixels = QByteArray{};
  pixels.reserve(qsizetype(weights.size() * 2));
  for (const auto& w : weights)
  {
    const auto dirt = std::clamp(int(std::lround(w[1] * 255)), 0, 255);
    const auto rock =
      std::clamp(int(std::lround((w[1] + w[2]) * 255)) - dirt, 0, 255 - dirt);
    pixels.append(char(dirt));
    pixels.append(char(rock));
  }
  auto output = std::ostringstream{};
  output.imbue(std::locale::classic());
  if (minFaceWidth > 0 || patchDensityPercent != 100)
  {
    output << "4 " << std::setprecision(17) << origin.x() << ' ' << origin.y() << ' '
           << (materialAssetId.empty() ? "-" : materialAssetId) << ' ' << maxAddedFaces
           << ' ' << maxWadKiB << ' ' << minFaceWidth << ' ' << patchDensityPercent << ' '
           << pixels.toHex().toStdString();
  }
  else if (maxAddedFaces >= 0 || maxWadKiB >= 0)
  {
    output << "3 " << std::setprecision(17) << origin.x() << ' ' << origin.y() << ' '
           << (materialAssetId.empty() ? "-" : materialAssetId) << ' ' << maxAddedFaces
           << ' ' << maxWadKiB << ' ' << pixels.toHex().toStdString();
  }
  else if (materialAssetId.empty())
  {
    output << "1 " << std::setprecision(17) << origin.x() << ' ' << origin.y() << ' '
           << pixels.toHex().toStdString();
  }
  else
  {
    output << "2 " << std::setprecision(17) << origin.x() << ' ' << origin.y() << ' '
           << materialAssetId << ' ' << pixels.toHex().toStdString();
  }
  return output.str();
}

Result<TerrainRegion> TerrainRegion::decode(const std::string_view text)
{
  auto input = std::istringstream{std::string{text}};
  input.imbue(std::locale::classic());
  auto version = 0;
  auto x = 0.0;
  auto y = 0.0;
  auto materialAssetId = std::string{};
  auto maxAddedFaces = -1;
  auto maxWadKiB = -1;
  auto minFaceWidth = 0;
  auto patchDensityPercent = 100;
  auto hex = std::string{};
  auto extra = std::string{};
  if (!(input >> version >> x >> y))
    return Error{"Invalid terrain region data."};

  if (version == 1)
  {
    if (!(input >> hex) || (input >> extra))
      return Error{"Invalid terrain region data."};
  }
  else if (version == 2 || version == 3 || version == 4)
  {
    if (!(input >> materialAssetId))
      return Error{"Invalid terrain region data."};
    if (
      version >= 3
      && (!(input >> maxAddedFaces >> maxWadKiB) || maxAddedFaces < -1 || maxAddedFaces > 100000 || maxWadKiB < -1 || maxWadKiB > 1048576))
      return Error{"Invalid terrain budget."};
    if (
      version == 4
      && (!(input >> minFaceWidth >> patchDensityPercent) || minFaceWidth < 0 || minFaceWidth > 1280 || (patchDensityPercent != 50 && patchDensityPercent != 100 && patchDensityPercent != 200)))
      return Error{"Invalid terrain width or patch density."};
    if (!(input >> hex) || (input >> extra))
      return Error{"Invalid terrain region data."};
    if (materialAssetId == "-")
      materialAssetId.clear();
  }
  else
  {
    return Error{"Invalid terrain region data."};
  }

  auto result = TerrainRegion{vm::vec2d{0, 0}};
  if (!std::isfinite(x) || !std::isfinite(y) || hex.size() != result.weights.size() * 4)
  {
    return Error{"Invalid terrain region data."};
  }
  const auto pixels = QByteArray::fromHex(QByteArray::fromStdString(hex));
  if (size_t(pixels.size()) != result.weights.size() * 2)
    return Error{"Invalid terrain paint weights."};
  result.origin = vm::vec2d{x, y};
  result.materialAssetId = std::move(materialAssetId);
  result.maxAddedFaces = maxAddedFaces;
  result.maxWadKiB = maxWadKiB;
  result.minFaceWidth = minFaceWidth;
  result.patchDensityPercent = patchDensityPercent;
  for (auto i = size_t{0}; i < result.weights.size(); ++i)
  {
    const auto dirt = static_cast<unsigned char>(pixels[qsizetype(i * 2)]);
    const auto rock = static_cast<unsigned char>(pixels[qsizetype(i * 2 + 1)]);
    if (dirt + rock > 255)
      return Error{"Invalid terrain paint weights."};
    result.weights[i] = {(255 - dirt - rock) / 255.0, dirt / 255.0, rock / 255.0};
  }
  return result;
}

mdl::GroupNode* selectedTerrainGroup(const mdl::Map& map)
{
  const auto& selection = map.selection();
  if (selection.nodes.size() == 1 && selection.groups.size() == 1)
    return selection.groups.front();
  return map.editorContext().currentGroup();
}

Result<TerrainRegion> createTerrainRegion(const mdl::GroupNode& group)
{
  const auto inverse = vm::invert(group.group().transformation());
  if (!inverse || group.children().empty())
    return Error{"Select a nonempty group of terrain brushes."};
  auto left = std::numeric_limits<double>::max();
  auto top = std::numeric_limits<double>::lowest();
  for (const auto* child : group.children())
  {
    const auto* brush = dynamic_cast<const mdl::BrushNode*>(child);
    if (!brush)
      return Error{"Terrain groups must contain brushes directly."};
    for (const auto& vertex : brush->brush().vertexPositions())
    {
      const auto local = *inverse * vertex;
      left = std::min(left, local.x());
      top = std::max(top, local.y());
    }
  }
  auto region = TerrainRegion{vm::vec2d{left, top}};
  if (const auto surfaces = terrainSurfaces(group, region); surfaces.is_error())
    return std::get<Error>(surfaces.error());
  return region;
}

double terrainArea(const QPolygonF& polygon)
{
  auto sum = 0.0;
  for (auto i = qsizetype{0}; i < polygon.size(); ++i)
  {
    const auto& a = polygon[i];
    const auto& b = polygon[(i + 1) % polygon.size()];
    sum += a.x() * b.y() - a.y() * b.x();
  }
  return std::abs(sum) / 2.0;
}

Result<std::vector<TerrainSurface>> terrainSurfaces(
  const mdl::GroupNode& group, const TerrainRegion& region)
{
  const auto& transform = group.group().transformation();
  const auto inverse = vm::invert(transform);
  if (
    !inverse || transform[2][2] <= 0
    || std::hypot(transform[2][0], transform[2][1]) > 1e-6)
    return Error{"Terrain groups must keep their local Z axis vertical."};
  auto surfaces = std::vector<TerrainSurface>{};
  for (const auto* child : group.children())
  {
    const auto* brushNode = dynamic_cast<const mdl::BrushNode*>(child);
    if (!brushNode)
      return Error{"Terrain groups must contain brushes directly."};
    const auto& brush = brushNode->brush();
    auto top = std::optional<size_t>{};
    for (auto i = size_t{0}; i < brush.faceCount(); ++i)
    {
      if (brush.face(i).normal().z() > 1e-6)
      {
        if (top)
          return Error{"Each terrain brush must have one upward-facing plane."};
        top = i;
      }
    }
    if (!top)
      return Error{"A terrain brush is missing its top face."};
    auto outline = QPolygonF{};
    for (const auto& vertex : brush.face(*top).vertexPositions())
    {
      const auto p = region.project(*inverse * vertex);
      outline.append(QPointF{p.x(), p.y()});
    }
    // A brush with an overhang would lose volume when split by its top footprint.
    for (const auto& vertex : brush.vertexPositions())
    {
      const auto p = region.project(*inverse * vertex);
      if (
        p.x() < -1e-5 || p.x() > TerrainRegion::Width + 1e-5 || p.y() < -1e-5
        || p.y() > TerrainRegion::Height + 1e-5)
        return Error{"Terrain brushes exceed the 1280 by 640 unit region."};
      auto positive = false;
      auto negative = false;
      for (auto i = qsizetype{0}; i < outline.size(); ++i)
      {
        const auto& a = outline[i];
        const auto& b = outline[(i + 1) % outline.size()];
        const auto side =
          (b.x() - a.x()) * (p.y() - a.y()) - (b.y() - a.y()) * (p.x() - a.x());
        positive |= side > 1e-5;
        negative |= side < -1e-5;
      }
      if (positive && negative)
        return Error{"Terrain brushes must fit beneath their top faces."};
    }
    for (const auto& other : surfaces)
    {
      if (terrainArea(outline.intersected(other.outline)) > 1e-5)
        return Error{"Terrain top faces overlap in the region plane."};
    }
    surfaces.push_back({brushNode, *top, std::move(outline)});
  }
  return surfaces;
}
} // namespace tb::ui
