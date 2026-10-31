/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom, licensed under GPL version 3 or later.
*/
#include "ui/ModelThumbnail.h"

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPointF>
#include <QPolygonF>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "base/Logger.h"
#include "fs/DiskFileSystem.h"
#include "gl/Material.h"
#include "mdl/EntityModel.h"
#include "mdl/GameConfig.h"
#include "mdl/LoadEntityModel.h"

namespace tb::ui
{
namespace
{
struct Vec3
{
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};
} // namespace

QImage renderModelThumbnail(
  const std::filesystem::path& folder,
  const std::filesystem::path& relativePath,
  const int size)
{
  if (size <= 0)
    return {};

  auto files = fs::DiskFileSystem{folder};
  auto logger = NullLogger{};
  const auto loaded = mdl::loadEntityModelSync(
    files,
    mdl::MaterialConfig{},
    relativePath,
    [](const std::filesystem::path&) { return gl::Material{"", nullptr}; },
    logger);
  if (loaded.is_error())
    return {};

  const auto* data = loaded.value().data();
  if (data == nullptr || data->surfaceCount() == 0)
    return {};

  const auto& surface = data->surface(0);
  const auto positions = surface.framePositions(0);
  const auto triangles = surface.frameTriangles(0);
  if (positions.size() < 3 || triangles.empty())
    return {};

  auto low = Vec3{positions.front().x(), positions.front().y(), positions.front().z()};
  auto high = low;
  for (const auto& position : positions)
  {
    const auto v = Vec3{position.x(), position.y(), position.z()};
    low = {std::min(low.x, v.x), std::min(low.y, v.y), std::min(low.z, v.z)};
    high = {std::max(high.x, v.x), std::max(high.y, v.y), std::max(high.z, v.z)};
  }
  const auto center =
    Vec3{(low.x + high.x) / 2, (low.y + high.y) / 2, (low.z + high.z) / 2};
  const auto extent = std::max({high.x - low.x, high.y - low.y, high.z - low.z, 0.001f});
  const auto scale = float(size) * 0.78f / extent;

  const auto yaw = 0.55f;
  const auto pitch = -0.38f;
  const auto cosYaw = std::cos(yaw), sinYaw = std::sin(yaw);
  const auto cosPitch = std::cos(pitch), sinPitch = std::sin(pitch);

  const auto project = [&](const vm::vec3f& position) {
    const auto x = position.x() - center.x;
    const auto y = position.y() - center.y;
    const auto z = position.z() - center.z;
    const auto rx = x * cosYaw + z * sinYaw;
    const auto rz = -x * sinYaw + z * cosYaw;
    const auto ry = y * cosPitch - rz * sinPitch;
    const auto rz2 = y * sinPitch + rz * cosPitch;
    return std::array<float, 3>{float(size) / 2 + rx * scale, float(size) / 2 - ry * scale, rz2};
  };

  struct Projected
  {
    QPolygonF polygon;
    QColor color;
    float depth = 0.0f;
  };
  auto projected = std::vector<Projected>{};
  projected.reserve(triangles.size());

  const auto light = Vec3{-0.35f, 0.55f, 0.75f};
  for (const auto& triangle : triangles)
  {
    const auto a = project(positions[triangle[0]]);
    const auto b = project(positions[triangle[1]]);
    const auto c = project(positions[triangle[2]]);

    const auto u = Vec3{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const auto v = Vec3{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    auto normal =
      Vec3{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
    const auto length = std::max(
      0.0001f, std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z));
    normal = {normal.x / length, normal.y / length, normal.z / length};
    const auto lambert =
      std::abs(normal.x * light.x + normal.y * light.y + normal.z * light.z);
    const auto shade = 0.35f + 0.65f * lambert;

    auto polygon = QPolygonF{};
    polygon << QPointF{a[0], a[1]} << QPointF{b[0], b[1]} << QPointF{c[0], c[1]};
    projected.push_back(
      {polygon,
       QColor(int(120 * shade + 30), int(132 * shade + 34), int(146 * shade + 38)),
       (a[2] + b[2] + c[2]) / 3.0f});
  }

  std::sort(
    projected.begin(), projected.end(),
    [](const auto& left, const auto& right) { return left.depth < right.depth; });

  auto image = QImage{size, size, QImage::Format_ARGB32_Premultiplied};
  image.fill(Qt::transparent);
  auto painter = QPainter{&image};
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(Qt::NoPen);
  for (const auto& item : projected)
  {
    painter.setBrush(item.color);
    painter.drawPolygon(item.polygon);
  }
  painter.end();
  return image;
}

} // namespace tb::ui
