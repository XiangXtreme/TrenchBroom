/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/TerrainRegion.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/EditorContext.h"
#include "mdl/GroupNode.h"
#include "mdl/Map.h"
#include "mdl/Selection.h"
#include "ui/TerrainChunks.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <numeric>
#include <optional>
#include <ranges>
#include <sstream>

namespace tb::ui
{
namespace
{
std::optional<int> integerField(
  const QJsonObject& object,
  const QString& name,
  const int defaultValue,
  const int minimum,
  const int maximum)
{
  const auto value = object.value(name);
  if (value.isUndefined())
    return defaultValue;
  if (!value.isDouble())
    return std::nullopt;
  const auto number = value.toDouble();
  if (
    !std::isfinite(number) || number < minimum || number > maximum
    || std::trunc(number) != number)
    return std::nullopt;
  return int(number);
}

Result<TerrainRegion> decodeSculptableRegion(const std::string& encoded)
{
  // The URL-safe encoding survives the map writer's property chunk boundaries.
  const auto bytes = QByteArray::fromBase64Encoding(
    QByteArray::fromStdString(encoded),
    QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
  if (!bytes)
    return Error{"Invalid terrain region data."};
  auto error = QJsonParseError{};
  const auto document = QJsonDocument::fromJson(bytes.decoded, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject())
    return Error{"Invalid terrain region data."};
  const auto object = document.object();
  const auto originValue = object.value(QStringLiteral("origin"));
  const auto material = object.value(QStringLiteral("material"));
  const auto weights = object.value(QStringLiteral("weights"));
  const auto fieldValue = object.value(QStringLiteral("heightField"));
  if (
    !originValue.isArray() || !material.isString() || !weights.isString()
    || !fieldValue.isObject())
    return Error{"Invalid terrain region data."};
  const auto origin = originValue.toArray();
  if (origin.size() != 2 || !origin[0].isDouble() || !origin[1].isDouble())
    return Error{"Invalid terrain region origin."};

  auto region = TerrainRegion{{origin[0].toDouble(), origin[1].toDouble()}};
  region.extensionFields = object;
  for (const auto* key :
       {"origin",
        "material",
        "weights",
        "maxAddedFaces",
        "maxWadKiB",
        "minFaceWidth",
        "patchDensityPercent",
        "heightField"})
    region.extensionFields.remove(QLatin1String{key});
  region.materialAssetId = material.toString().toStdString();
  const auto maxFaces =
    integerField(object, QStringLiteral("maxAddedFaces"), -1, -1, 100000);
  const auto maxWad = integerField(object, QStringLiteral("maxWadKiB"), -1, -1, 1048576);
  const auto width = integerField(object, QStringLiteral("minFaceWidth"), 0, 0, 1280);
  const auto density =
    integerField(object, QStringLiteral("patchDensityPercent"), 100, 50, 200);
  if (
    !maxFaces || !maxWad || !width || !density
    || (*density != 50 && *density != 100 && *density != 200))
    return Error{"Invalid terrain budget."};
  region.maxAddedFaces = *maxFaces;
  region.maxWadKiB = *maxWad;
  region.minFaceWidth = *width;
  region.patchDensityPercent = *density;

  const auto hex = weights.toString().toLatin1();
  const auto pixels = QByteArray::fromHex(hex);
  if (
    size_t(hex.size()) != region.weights.size() * 4
    || size_t(pixels.size()) != region.weights.size() * 2)
    return Error{"Invalid terrain paint weights."};
  for (auto i = size_t{0}; i < region.weights.size(); ++i)
  {
    const auto dirt = static_cast<unsigned char>(pixels[qsizetype(i * 2)]);
    const auto rock = static_cast<unsigned char>(pixels[qsizetype(i * 2 + 1)]);
    if (dirt + rock > 255)
      return Error{"Invalid terrain paint weights."};
    region.weights[i] = {(255 - dirt - rock) / 255.0, dirt / 255.0, rock / 255.0};
  }

  const auto height = fieldValue.toObject();
  region.heightFieldExtensionFields = height;
  for (const auto* key : {"columns", "rows", "spacing", "bottom", "heights"})
    region.heightFieldExtensionFields.remove(QLatin1String{key});
  const auto columns = integerField(height, QStringLiteral("columns"), 0, 1, 4096);
  const auto rows = integerField(height, QStringLiteral("rows"), 0, 1, 4096);
  const auto spacing = height.value(QStringLiteral("spacing"));
  const auto bottom = height.value(QStringLiteral("bottom"));
  const auto samples = height.value(QStringLiteral("heights"));
  if (
    !columns || !rows || !spacing.isDouble() || !bottom.isDouble() || !samples.isArray())
    return Error{"Invalid terrain height field."};
  auto field = TerrainHeightField{};
  field.columns = *columns;
  field.rows = *rows;
  field.spacing = spacing.toDouble();
  field.bottom = bottom.toDouble();
  const auto values = samples.toArray();
  if (size_t(values.size()) != size_t(field.columns + 1) * size_t(field.rows + 1))
    return Error{"Invalid terrain height field."};
  field.heights.reserve(size_t(values.size()));
  for (const auto value : values)
  {
    if (!value.isDouble())
      return Error{"Invalid terrain height field."};
    field.heights.push_back(value.toDouble());
  }
  if (
    !field.valid() || !std::isfinite(region.origin.x())
    || !std::isfinite(region.origin.y())
    || field.columns * field.spacing > TerrainRegion::Width * TerrainRegion::UnitsPerTexel
    || field.rows * field.spacing > TerrainRegion::Height * TerrainRegion::UnitsPerTexel)
    return Error{"Invalid terrain height field."};
  region.heightField = std::move(field);
  return region;
}
} // namespace

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
  return encodeImpl(true);
}

std::string TerrainRegion::paintCacheKey() const
{
  return encodeImpl(false);
}

std::string TerrainRegion::encodeImpl(const bool includeHeightField) const
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
  if (includeHeightField && heightField)
  {
    auto heights = QJsonArray{};
    for (const auto height : heightField->heights)
      heights.append(height);
    auto field = heightFieldExtensionFields;
    field.insert(QStringLiteral("columns"), heightField->columns);
    field.insert(QStringLiteral("rows"), heightField->rows);
    field.insert(QStringLiteral("spacing"), heightField->spacing);
    field.insert(QStringLiteral("bottom"), heightField->bottom);
    field.insert(QStringLiteral("heights"), heights);
    auto object = extensionFields;
    object.insert(QStringLiteral("origin"), QJsonArray{origin.x(), origin.y()});
    object.insert(QStringLiteral("material"), QString::fromStdString(materialAssetId));
    object.insert(QStringLiteral("weights"), QString::fromLatin1(pixels.toHex()));
    object.insert(QStringLiteral("maxAddedFaces"), maxAddedFaces);
    object.insert(QStringLiteral("maxWadKiB"), maxWadKiB);
    object.insert(QStringLiteral("minFaceWidth"), minFaceWidth);
    object.insert(QStringLiteral("patchDensityPercent"), patchDensityPercent);
    object.insert(QStringLiteral("heightField"), field);
    const auto json = QJsonDocument{object}.toJson(QJsonDocument::Compact);
    return "5 "
           + json.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)
               .toStdString();
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
  if (!(input >> version))
    return Error{"Invalid terrain region data."};

  if (version == 5)
  {
    auto encoded = std::string{};
    if (!(input >> encoded) || (input >> extra))
      return Error{"Invalid terrain region data."};
    return decodeSculptableRegion(encoded);
  }
  if (!(input >> x >> y))
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
  auto* group = selection.nodes.size() == 1 && selection.groups.size() == 1
                  ? selection.groups.front()
                  : map.editorContext().currentGroup();
  if (group)
    if (auto* parent = dynamic_cast<mdl::GroupNode*>(group->parent());
        parent && isTerrainChunkGroup(*parent))
      return parent;
  return group;
}

Result<TerrainRegion> createTerrainRegion(
  const mdl::GroupNode& group, const bool allowLargeSource)
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
  if (const auto surfaces = terrainSurfaces(group, region, allowLargeSource);
      surfaces.is_error())
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

namespace
{
Result<std::vector<size_t>> validateSculptedBrushes(
  const mdl::GroupNode& group,
  const TerrainRegion& region,
  const TerrainHeightField& field,
  const vm::mat4x4d& inverse)
{
  const auto invalid = Error{
    "The terrain geometry no longer matches its height field. Undo edits made outside "
    "the terrain tools or recreate the terrain."};
  if (
    !field.valid()
    || field.columns * field.spacing > TerrainRegion::Width * TerrainRegion::UnitsPerTexel
    || field.rows * field.spacing > TerrainRegion::Height * TerrainRegion::UnitsPerTexel
    || group.children().size() != size_t(field.columns) * size_t(field.rows) * 2)
    return invalid;

  constexpr auto Tolerance = 1e-3;
  auto indices = std::vector<size_t>(
    size_t(field.columns) * size_t(field.rows) * 2, std::numeric_limits<size_t>::max());
  for (auto childIndex = size_t{0}; childIndex < group.children().size(); ++childIndex)
  {
    const auto* child = group.children()[childIndex];
    const auto* brush = dynamic_cast<const mdl::BrushNode*>(child);
    if (!brush)
      return invalid;
    const auto vertices = brush->brush().vertexPositions();
    if (vertices.size() != 6)
      return invalid;
    auto tops = std::vector<std::pair<int, int>>{};
    auto bottoms = std::vector<std::pair<int, int>>{};
    for (const auto& vertex : vertices)
    {
      const auto local = inverse * vertex;
      const auto x = (local.x() - region.origin.x()) / field.spacing;
      const auto y = (region.origin.y() - local.y()) / field.spacing;
      if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(local.z()))
        return invalid;
      const auto c = std::round(x);
      const auto r = std::round(y);
      if (
        c < 0 || c > field.columns || r < 0 || r > field.rows
        || std::abs(x - c) * field.spacing > Tolerance
        || std::abs(y - r) * field.spacing > Tolerance)
        return invalid;
      const auto corner = std::pair{int(c), int(r)};
      if (std::abs(local.z() - field.bottom) <= Tolerance)
        bottoms.push_back(corner);
      else if (
        std::abs(local.z() - field.heights[field.index(corner.first, corner.second)])
        <= Tolerance)
        tops.push_back(corner);
      else
        return invalid;
    }
    if (tops.size() != 3 || bottoms.size() != 3)
      return invalid;
    std::ranges::sort(tops);
    std::ranges::sort(bottoms);
    if (tops != bottoms || std::ranges::adjacent_find(tops) != tops.end())
      return invalid;
    const auto [minColumn, maxColumn] = std::ranges::minmax(
      tops | std::views::transform([](const auto& p) { return p.first; }));
    const auto [minRow, maxRow] = std::ranges::minmax(
      tops | std::views::transform([](const auto& p) { return p.second; }));
    if (maxColumn != minColumn + 1 || maxRow != minRow + 1)
      return invalid;
    const auto a = std::pair{minColumn, minRow};
    const auto b = std::pair{maxColumn, minRow};
    const auto c = std::pair{maxColumn, maxRow};
    const auto d = std::pair{minColumn, maxRow};
    const auto abc = std::ranges::find(tops, a) != tops.end()
                     && std::ranges::find(tops, b) != tops.end()
                     && std::ranges::find(tops, c) != tops.end();
    const auto acd = std::ranges::find(tops, a) != tops.end()
                     && std::ranges::find(tops, c) != tops.end()
                     && std::ranges::find(tops, d) != tops.end();
    if (!abc && !acd)
      return invalid;
    const auto triangle =
      (size_t(minRow) * size_t(field.columns) + size_t(minColumn)) * 2 + size_t(acd);
    if (indices[triangle] != std::numeric_limits<size_t>::max())
      return invalid;
    indices[triangle] = childIndex;
  }
  return indices;
}
} // namespace

Result<std::vector<size_t>> terrainHeightFieldBrushIndices(
  const mdl::GroupNode& group, const TerrainRegion& region)
{
  const auto& transform = group.group().transformation();
  const auto inverse = vm::invert(transform);
  if (
    !region.heightField || !inverse || transform[2][2] <= 0
    || std::hypot(transform[2][0], transform[2][1]) > 1e-6)
    return Error{"Invalid sculptable terrain or group transformation."};
  return validateSculptedBrushes(group, region, *region.heightField, *inverse);
}

Result<std::vector<TerrainSurface>> terrainSurfaces(
  const mdl::GroupNode& group, const TerrainRegion& region, const bool allowLargeSource)
{
  const auto& transform = group.group().transformation();
  const auto inverse = vm::invert(transform);
  if (
    !inverse || transform[2][2] <= 0
    || std::hypot(transform[2][0], transform[2][1]) > 1e-6)
    return Error{"Terrain groups must keep their local Z axis vertical."};
  if (region.heightField)
  {
    const auto validation =
      validateSculptedBrushes(group, region, *region.heightField, *inverse);
    if (validation.is_error())
      return std::get<Error>(validation.error());
  }
  auto surfaces = std::vector<TerrainSurface>{};
  for (const auto* child : group.children())
  {
    const auto* brushNode = dynamic_cast<const mdl::BrushNode*>(child);
    if (!brushNode)
      return Error{"Terrain groups must contain brushes directly."};
    const auto& brush = brushNode->brush();
    auto brushSurfaces = std::vector<TerrainSurface>{};
    for (auto i = size_t{0}; i < brush.faceCount(); ++i)
    {
      if (brush.face(i).normal().z() > 1e-6)
      {
        auto outline = QPolygonF{};
        for (const auto& vertex : brush.face(i).vertexPositions())
        {
          const auto p = region.project(*inverse * vertex);
          outline.append(QPointF{p.x(), p.y()});
        }
        brushSurfaces.push_back({brushNode, i, std::move(outline)});
      }
    }
    if (brushSurfaces.empty())
      return Error{"A terrain brush is missing its top face."};
    // Every part of the brush must lie under one of its upward-facing faces.
    for (const auto& vertex : brush.vertexPositions())
    {
      const auto p = region.project(*inverse * vertex);
      if (
        p.x() < -1e-5 || p.y() < -1e-5
        || (!allowLargeSource && (p.x() > TerrainRegion::Width + 1e-5 || p.y() > TerrainRegion::Height + 1e-5)))
        return Error{"Terrain brushes exceed the 1280 by 640 unit region."};
      const auto underTop = std::ranges::any_of(brushSurfaces, [&](const auto& surface) {
        auto positive = false;
        auto negative = false;
        const auto& outline = surface.outline;
        for (auto i = qsizetype{0}; i < outline.size(); ++i)
        {
          const auto& a = outline[i];
          const auto& b = outline[(i + 1) % outline.size()];
          const auto side =
            (b.x() - a.x()) * (p.y() - a.y()) - (b.y() - a.y()) * (p.x() - a.x());
          positive |= side > 1e-5;
          negative |= side < -1e-5;
        }
        return !(positive && negative);
      });
      if (!underTop)
        return Error{"Terrain brushes must fit beneath their top faces."};
    }
    for (auto& surface : brushSurfaces)
      surfaces.push_back(std::move(surface));
  }
  // The height-field check already verifies every triangle exactly once.
  if (region.heightField)
    return surfaces;

  // Compare only outlines whose X ranges overlap. A sculpted region can contain
  // hundreds of small triangles; checking every pair makes editing needlessly slow.
  auto order = std::vector<size_t>(surfaces.size());
  std::iota(order.begin(), order.end(), size_t{0});
  std::ranges::sort(order, [&](const auto lhs, const auto rhs) {
    return surfaces[lhs].outline.boundingRect().left()
           < surfaces[rhs].outline.boundingRect().left();
  });
  auto active = std::vector<size_t>{};
  for (const auto index : order)
  {
    const auto bounds = surfaces[index].outline.boundingRect();
    std::erase_if(active, [&](const auto previous) {
      return surfaces[previous].outline.boundingRect().right() <= bounds.left();
    });
    for (const auto previous : active)
    {
      const auto& other = surfaces[previous].outline;
      if (
        bounds.intersects(other.boundingRect())
        && terrainArea(surfaces[index].outline.intersected(other)) > 1e-5)
        return Error{"Terrain top faces overlap in the region plane."};
    }
    active.push_back(index);
  }
  return surfaces;
}

Result<TerrainHeightField> createTerrainHeightField(
  const mdl::GroupNode& group,
  const TerrainRegion& region,
  const double spacing,
  const bool allowLargeSource)
{
  const auto unsupported = Error{
    "The source must be continuous rectangular brushes with planar tops, a shared flat "
    "bottom, and edges aligned to the terrain grid."};
  if (!std::isfinite(spacing) || spacing <= 0.0 || region.heightField)
    return unsupported;
  const auto surfaces = terrainSurfaces(group, region, allowLargeSource);
  if (surfaces.is_error())
    return std::get<Error>(surfaces.error());
  if (group.children().empty() || surfaces.value().size() != group.children().size())
    return unsupported;
  const auto& transform = group.group().transformation();
  const auto inverse = *vm::invert(transform); // Already checked by terrainSurfaces.
  const auto vertical = transform * vm::vec3d{0, 0, 1} - transform * vm::vec3d{};
  struct Footprint
  {
    const TerrainSurface* surface;
    double left;
    double right;
    double top;
    double bottomY;
    double bottom;
  };
  auto footprints = std::vector<Footprint>{};
  auto left = std::numeric_limits<double>::max();
  auto right = std::numeric_limits<double>::lowest();
  auto top = std::numeric_limits<double>::lowest();
  auto bottomY = std::numeric_limits<double>::max();
  auto bottom = std::numeric_limits<double>::max();
  for (const auto& surface : surfaces.value())
  {
    const auto& brush = surface.brush->brush();
    const auto vertices = brush.vertexPositions();
    if (vertices.size() != 8 || brush.face(surface.top).vertexPositions().size() != 4)
      return unsupported;
    auto footprint = Footprint{
      &surface,
      std::numeric_limits<double>::max(),
      std::numeric_limits<double>::lowest(),
      std::numeric_limits<double>::lowest(),
      std::numeric_limits<double>::max(),
      std::numeric_limits<double>::max()};
    for (const auto& vertex : vertices)
    {
      const auto local = inverse * vertex;
      footprint.left = std::min(footprint.left, local.x());
      footprint.right = std::max(footprint.right, local.x());
      footprint.top = std::max(footprint.top, local.y());
      footprint.bottomY = std::min(footprint.bottomY, local.y());
      footprint.bottom = std::min(footprint.bottom, local.z());
    }
    left = std::min(left, footprint.left);
    right = std::max(right, footprint.right);
    top = std::max(top, footprint.top);
    bottomY = std::min(bottomY, footprint.bottomY);
    bottom = std::min(bottom, footprint.bottom);
    footprints.push_back(footprint);
  }
  constexpr auto Tolerance = 1e-4;
  const auto columnCount = (right - left) / spacing;
  const auto rowCount = (top - bottomY) / spacing;
  if (
    !std::isfinite(columnCount) || !std::isfinite(rowCount) || columnCount < 1
    || rowCount < 1 || columnCount > 4096 || rowCount > 4096
    || std::abs(columnCount - std::round(columnCount)) * spacing > Tolerance
    || std::abs(rowCount - std::round(rowCount)) * spacing > Tolerance
    || std::abs(region.origin.x() - left) > Tolerance
    || std::abs(region.origin.y() - top) > Tolerance)
    return unsupported;

  if (std::round(columnCount) * std::round(rowCount) * 2.0 > MaxTerrainBrushes)
    return Error{"Terrain would create too many brushes; increase the grid spacing."};

  auto result = TerrainHeightField::create(
    int(std::round(columnCount)),
    int(std::round(rowCount)),
    spacing,
    bottom,
    bottom + 1.0);
  if (result.is_error())
    return std::get<Error>(result.error());
  auto field = std::move(result).value();
  auto assigned = std::vector<bool>(field.heights.size());
  auto area = 0.0;
  for (const auto& footprint : footprints)
  {
    if (std::abs(footprint.bottom - bottom) > Tolerance)
      return unsupported;
    const auto& brush = footprint.surface->brush->brush();
    const auto& plane = brush.face(footprint.surface->top).boundary();
    const auto denominator = vm::dot(plane.normal, vertical);
    if (denominator <= 1e-9)
      return unsupported;
    const auto heightAt = [&](const double x, const double y) {
      return -plane.point_distance(transform * vm::vec3d{x, y, 0}) / denominator;
    };
    auto topCorners = 0;
    auto bottomCorners = 0;
    for (const auto& vertex : brush.vertexPositions())
    {
      const auto local = inverse * vertex;
      const auto column = std::abs(local.x() - footprint.left) <= Tolerance ? 0 : 1;
      const auto row = std::abs(local.y() - footprint.top) <= Tolerance ? 0 : 1;
      if (
        std::abs(local.x() - (column == 0 ? footprint.left : footprint.right)) > Tolerance
        || std::abs(local.y() - (row == 0 ? footprint.top : footprint.bottomY))
             > Tolerance)
        return unsupported;
      const auto bit = 1 << (row * 2 + column);
      auto* corners = static_cast<int*>(nullptr);
      if (std::abs(local.z() - bottom) <= Tolerance)
        corners = &bottomCorners;
      else if (std::abs(local.z() - heightAt(local.x(), local.y())) <= Tolerance)
        corners = &topCorners;
      else
        return unsupported;
      if (*corners & bit)
        return unsupported;
      *corners |= bit;
    }
    if (topCorners != 15 || bottomCorners != 15)
      return unsupported;
    const auto c0 = (footprint.left - left) / spacing;
    const auto c1 = (footprint.right - left) / spacing;
    const auto r0 = (top - footprint.top) / spacing;
    const auto r1 = (top - footprint.bottomY) / spacing;
    const auto aligned = [&](const double value) {
      return std::abs(value - std::round(value)) * spacing <= Tolerance;
    };
    if (!aligned(c0) || !aligned(c1) || !aligned(r0) || !aligned(r1))
      return unsupported;
    for (auto row = int(std::round(r0)); row <= int(std::round(r1)); ++row)
      for (auto column = int(std::round(c0)); column <= int(std::round(c1)); ++column)
      {
        const auto index = field.index(column, row);
        const auto height = heightAt(left + column * spacing, top - row * spacing);
        if (assigned[index] && std::abs(field.heights[index] - height) > Tolerance)
          return Error{
            "Adjacent terrain brushes have different heights at their boundary."};
        field.heights[index] = height;
        assigned[index] = true;
      }
    area += (footprint.right - footprint.left) * (footprint.top - footprint.bottomY);
  }
  const auto fullArea = (right - left) * (top - bottomY);
  if (
    std::abs(area - fullArea) > std::max(Tolerance, fullArea * 1e-9)
    || !std::ranges::all_of(assigned, [](const bool value) { return value; }))
    return unsupported;
  if (!field.valid())
    return Error{"The source terrain is too thin to sculpt."};
  return field;
}
} // namespace tb::ui
