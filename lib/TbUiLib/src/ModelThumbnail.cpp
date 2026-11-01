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

#include "base/Logger.h"
#include "fs/DiskFileSystem.h"
#include "gl/Material.h"
#include "mdl/EntityModel.h"
#include "mdl/GameConfig.h"
#include "mdl/LoadEntityModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iterator>
#include <vector>

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

std::optional<ModelPreviewScene> loadGoldSrcStudioPreviewScene(
  const std::vector<std::uint8_t>& bytes)
{
  if (!mdl::isGoldSrcStudioModel(bytes))
    return std::nullopt;
  auto info = mdl::readGoldSrcStudioModelInfo(bytes);
  if (info.is_error())
    return std::nullopt;

  auto scene = ModelPreviewScene{};
  scene.studioModel = std::move(info).value();
  if (const auto mesh = mdl::buildGoldSrcStudioMesh(bytes); mesh.is_success())
  {
    auto frame = ModelPreviewFrame{};
    frame.name = "Bind pose";
    frame.positions = mesh.value().positions;
    frame.triangles = mesh.value().triangles;
    scene.frames.push_back(std::move(frame));
  }
  return scene;
}

std::optional<ModelPreviewScene> loadModelPreviewScene(
  const std::filesystem::path& folder,
  const std::filesystem::path& relativePath,
  const bool firstOnly)
{
  {
    auto stream = std::ifstream{folder / relativePath, std::ios::binary};
    if (stream)
    {
      auto bytes = std::vector<std::uint8_t>{
        std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
      if (auto studio = loadGoldSrcStudioPreviewScene(bytes))
        return studio;
    }
  }

  auto files = fs::DiskFileSystem{folder};
  auto logger = NullLogger{};
  const auto loaded = mdl::loadEntityModelSync(
    files,
    mdl::MaterialConfig{},
    relativePath,
    [](const std::filesystem::path&) { return gl::Material{"", nullptr}; },
    logger);
  if (loaded.is_error() || !loaded.value().data())
    return std::nullopt;
  const auto& data = *loaded.value().data();
  auto scene = ModelPreviewScene{};
  auto bytes = size_t{};
  for (auto frameIndex = size_t{};
       frameIndex < data.frameCount() && (!firstOnly || frameIndex == 0);
       ++frameIndex)
  {
    auto mesh = ModelPreviewFrame{};
    if (const auto* frame = data.frame(frameIndex))
      mesh.name = frame->name();
    for (auto surfaceIndex = size_t{}; surfaceIndex < data.surfaceCount(); ++surfaceIndex)
    {
      const auto& surface = data.surface(surfaceIndex);
      const auto positions = surface.framePositions(frameIndex);
      const auto triangles = surface.frameTriangles(frameIndex);
      const auto base = mesh.positions.size();
      mesh.positions.insert(mesh.positions.end(), positions.begin(), positions.end());
      for (const auto& triangle : triangles)
        mesh.triangles.push_back(
          {base + triangle[0], base + triangle[1], base + triangle[2]});
    }
    bytes += mesh.positions.size() * sizeof(vm::vec3f)
             + mesh.triangles.size() * sizeof(std::array<size_t, 3>);
    if (bytes > 64 * 1024 * 1024 || scene.frames.size() >= 256)
      return std::nullopt;
    scene.frames.push_back(std::move(mesh));
  }
  return scene.frames.empty() ? std::nullopt : std::optional{std::move(scene)};
}

QImage renderModelPreview(
  const ModelPreviewFrame& mesh,
  const int size,
  const float yaw,
  const float pitch,
  const bool bounds,
  const bool origin)
{
  const auto& positions = mesh.positions;
  const auto& triangles = mesh.triangles;
  if (size <= 0 || size > 2048 || positions.size() < 3 || triangles.empty())
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

  const auto cosYaw = std::cos(yaw), sinYaw = std::sin(yaw);
  const auto cosPitch = std::cos(pitch), sinPitch = std::sin(pitch);

  const auto project = [&](const vm::vec3f& position) {
    const auto x = position.x() - center.x;
    const auto y = position.y() - center.y;
    const auto z = position.z() - center.z;
    const auto rx = x * cosYaw - y * sinYaw;
    const auto rz = x * sinYaw + y * cosYaw;
    const auto ry = z * cosPitch - rz * sinPitch;
    const auto rz2 = z * sinPitch + rz * cosPitch;
    return std::array<float, 3>{
      float(size) / 2 + rx * scale, float(size) / 2 - ry * scale, rz2};
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
      0.0001f,
      std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z));
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

  std::sort(projected.begin(), projected.end(), [](const auto& left, const auto& right) {
    return left.depth < right.depth;
  });

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
  if (bounds)
  {
    painter.setPen(QColor{220, 180, 80});
    const auto corners = std::array{
      vm::vec3f{low.x, low.y, low.z},
      vm::vec3f{high.x, low.y, low.z},
      vm::vec3f{high.x, high.y, low.z},
      vm::vec3f{low.x, high.y, low.z},
      vm::vec3f{low.x, low.y, high.z},
      vm::vec3f{high.x, low.y, high.z},
      vm::vec3f{high.x, high.y, high.z},
      vm::vec3f{low.x, high.y, high.z}};
    const auto edge = [&](const size_t a, const size_t b) {
      const auto start = project(corners[a]), end = project(corners[b]);
      painter.drawLine(QPointF{start[0], start[1]}, QPointF{end[0], end[1]});
    };
    for (auto i = size_t{}; i < 4; ++i)
    {
      edge(i, (i + 1) % 4);
      edge(i + 4, (i + 1) % 4 + 4);
      edge(i, i + 4);
    }
  }
  if (origin)
  {
    const auto zero = project(vm::vec3f{0, 0, 0});
    const auto axes = std::array{
      vm::vec3f{extent / 4, 0, 0},
      vm::vec3f{0, extent / 4, 0},
      vm::vec3f{0, 0, extent / 4}};
    const auto colors =
      std::array{QColor{240, 80, 80}, QColor{80, 220, 100}, QColor{80, 150, 255}};
    for (auto i = size_t{}; i < axes.size(); ++i)
    {
      const auto end = project(axes[i]);
      painter.setPen(QPen{colors[i], 2});
      painter.drawLine(QPointF{zero[0], zero[1]}, QPointF{end[0], end[1]});
    }
  }
  painter.end();
  return image;
}

QImage renderModelThumbnail(
  const std::filesystem::path& folder,
  const std::filesystem::path& relativePath,
  const int size)
{
  const auto scene = loadModelPreviewScene(folder, relativePath, true);
  return scene && !scene->frames.empty()
           ? renderModelPreview(scene->frames.front(), size, 0.55f, 0.38f)
           : QImage{};
}
} // namespace tb::ui
