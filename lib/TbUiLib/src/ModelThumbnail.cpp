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
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include <fmt/format.h>

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

mdl::GoldSrcStudioExternalData loadGoldSrcStudioExternalData(
  const std::vector<std::uint8_t>& bytes,
  const std::filesystem::path& modelPath,
  GoldSrcStudioExternalFiles& storage)
{
  storage = {};
  if (modelPath.empty())
    return {};

  const auto folder = modelPath.parent_path();
  const auto stem = modelPath.stem().string();
  if (stem.empty())
    return {};

  const auto readBytes = [](const std::filesystem::path& path) {
    auto stream = std::ifstream{path, std::ios::binary};
    if (!stream)
      return std::vector<std::uint8_t>{};
    return std::vector<std::uint8_t>{
      std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
  };

  storage.textureHeader = readBytes(folder / (stem + "T.mdl"));

  auto info = mdl::readGoldSrcStudioModelInfo(bytes);
  const auto groupCount = info.is_success() ? info.value().sequenceGroups : std::size_t{1};
  if (groupCount > 1)
  {
    storage.sequenceGroups.resize(groupCount - 1);
    for (auto group = std::size_t{1}; group < groupCount; ++group)
    {
      auto name = group < info.value().sequenceGroupFiles.size()
                    ? info.value().sequenceGroupFiles[group]
                    : std::string{};
      std::replace(name.begin(), name.end(), '\\', '/');
      auto file = std::filesystem::path{name}.filename();
      if (file.empty())
        file = fmt::format("{}{:02}.mdl", stem, group);
      storage.sequenceGroups[group - 1] = readBytes(folder / file);
    }
  }

  auto external = mdl::GoldSrcStudioExternalData{};
  if (!storage.textureHeader.empty())
    external.textureHeader = &storage.textureHeader;
  if (!storage.sequenceGroups.empty())
    external.sequenceGroups = &storage.sequenceGroups;
  return external;
}

std::optional<ModelPreviewScene> loadGoldSrcStudioPreviewScene(
  const std::vector<std::uint8_t>& bytes,
  const mdl::GoldSrcStudioExternalData& external)
{
  if (!mdl::isGoldSrcStudioModel(bytes))
    return std::nullopt;
  auto info = mdl::readGoldSrcStudioModelInfo(bytes, external);
  if (info.is_error())
    return std::nullopt;

  auto scene = ModelPreviewScene{};
  scene.studioModel = std::move(info).value();
  if (auto frame = loadGoldSrcStudioFrame(bytes, 0, 0, {}, true, external); frame)
    scene.frames.push_back(std::move(*frame));
  return scene;
}

std::optional<ModelPreviewFrame> loadGoldSrcStudioFrame(
  const std::vector<std::uint8_t>& bytes,
  const std::size_t sequence,
  const std::size_t frame,
  const std::vector<std::size_t>& variants,
  const bool decodeTexturePixels,
  const mdl::GoldSrcStudioExternalData& external,
  const std::size_t skinFamily,
  const std::size_t blend)
{
  auto mesh = mdl::buildGoldSrcStudioMesh(
    bytes, variants, sequence, frame, decodeTexturePixels, external, skinFamily, blend);
  if (mesh.is_error() || mesh.value().triangles.empty())
    return std::nullopt;

  auto value = std::move(mesh).value();
  auto preview = ModelPreviewFrame{};
  preview.name = "Sequence " + std::to_string(sequence) + " frame " + std::to_string(frame);
  preview.positions = std::move(value.positions);
  preview.texCoords = std::move(value.texCoords);
  preview.triangles = std::move(value.triangles);
  preview.triangleTextures = std::move(value.triangleTextures);
  preview.textures = std::move(value.textures);
  return preview;
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
      auto externalFiles = GoldSrcStudioExternalFiles{};
      const auto external =
        loadGoldSrcStudioExternalData(bytes, folder / relativePath, externalFiles);
      if (auto studio = loadGoldSrcStudioPreviewScene(bytes, external))
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
    std::array<float, 2> screen[3];
    std::array<float, 2> uv[3];
    const ModelPreviewTexture* texture = nullptr;
    QColor color;
    float shade = 1.0f;
    float depth = 0.0f;
    bool chrome = false;
    float chromeU = 0.5f;
    float chromeV = 0.5f;
  };
  auto projected = std::vector<Projected>{};
  projected.reserve(triangles.size());

  const auto textured = mesh.texCoords.size() == positions.size();
  const auto light = Vec3{-0.35f, 0.55f, 0.75f};
  for (auto index = std::size_t{0}; index < triangles.size(); ++index)
  {
    const auto& triangle = triangles[index];
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

    const ModelPreviewTexture* texture = nullptr;
    if (index < mesh.triangleTextures.size())
    {
      const auto textureIndex = mesh.triangleTextures[index];
      if (textureIndex < mesh.textures.size())
      {
        const auto& candidate = mesh.textures[textureIndex];
        if (
          candidate.width > 0 && candidate.height > 0
          && candidate.pixels.size() == candidate.width * candidate.height)
          texture = &candidate;
      }
    }

    auto item = Projected{};
    item.screen[0] = {a[0], a[1]};
    item.screen[1] = {b[0], b[1]};
    item.screen[2] = {c[0], c[1]};
    if (textured)
      for (auto corner = 0; corner < 3; ++corner)
        item.uv[corner] = mesh.texCoords[triangle[std::size_t(corner)]];
    item.texture = texture;
    item.color = QColor(
      int(120 * shade + 30), int(132 * shade + 34), int(146 * shade + 38));
    item.shade = shade;
    item.depth = (a[2] + b[2] + c[2]) / 3.0f;
    if (texture && (texture->flags & 0x0008u) != 0)
    {
      // The engine sphere-maps chrome from the per-vertex normal. The preview has no
      // vertex normals, so reflect the face normal through the current view instead.
      const auto& p0 = positions[triangle[0]];
      const auto& p1 = positions[triangle[1]];
      const auto& p2 = positions[triangle[2]];
      const auto e1 = Vec3{p1.x() - p0.x(), p1.y() - p0.y(), p1.z() - p0.z()};
      const auto e2 = Vec3{p2.x() - p0.x(), p2.y() - p0.y(), p2.z() - p0.z()};
      auto n = Vec3{
        e1.y * e2.z - e1.z * e2.y,
        e1.z * e2.x - e1.x * e2.z,
        e1.x * e2.y - e1.y * e2.x};
      const auto nLength = std::max(
        0.0001f, std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z));
      n = {n.x / nLength, n.y / nLength, n.z / nLength};
      const auto vx = n.x * cosYaw - n.y * sinYaw;
      const auto vz = n.x * sinYaw + n.y * cosYaw;
      const auto vy = n.z * cosPitch - vz * sinPitch;
      item.chrome = true;
      item.chromeU = 0.5f + 0.5f * vx;
      item.chromeV = 0.5f - 0.5f * vy;
    }
    projected.push_back(item);
  }

  std::sort(projected.begin(), projected.end(), [](const auto& left, const auto& right) {
    return left.depth < right.depth;
  });

  auto image = QImage{size, size, QImage::Format_ARGB32};
  image.fill(Qt::transparent);
  for (const auto& item : projected)
  {
    const auto& a = item.screen[0];
    const auto& b = item.screen[1];
    const auto& c = item.screen[2];
    const auto den =
      (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1]);
    if (std::abs(den) < 1e-6f)
      continue;

    const auto minX = std::max(0, int(std::floor(std::min({a[0], b[0], c[0]}))));
    const auto maxX = std::min(size - 1, int(std::ceil(std::max({a[0], b[0], c[0]}))));
    const auto minY = std::max(0, int(std::floor(std::min({a[1], b[1], c[1]}))));
    const auto maxY = std::min(size - 1, int(std::ceil(std::max({a[1], b[1], c[1]}))));
    for (auto y = minY; y <= maxY; ++y)
      for (auto x = minX; x <= maxX; ++x)
      {
        const auto px = float(x) + 0.5f;
        const auto py = float(y) + 0.5f;
        const auto w0 =
          ((b[1] - c[1]) * (px - c[0]) + (c[0] - b[0]) * (py - c[1])) / den;
        const auto w1 =
          ((c[1] - a[1]) * (px - c[0]) + (a[0] - c[0]) * (py - c[1])) / den;
        const auto w2 = 1.0f - w0 - w1;
        if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
          continue;

        auto color = item.color;
        if (item.texture)
        {
          const auto u = item.chrome
                           ? item.chromeU
                           : w0 * item.uv[0][0] + w1 * item.uv[1][0]
                               + w2 * item.uv[2][0];
          const auto v = item.chrome
                           ? item.chromeV
                           : w0 * item.uv[0][1] + w1 * item.uv[1][1]
                               + w2 * item.uv[2][1];
          // Bilinear sampling keeps the small thumbnail readable instead of speckled.
          const auto width = int(item.texture->width);
          const auto height = int(item.texture->height);
          const auto fx = u * float(width) - 0.5f;
          const auto fy = v * float(height) - 0.5f;
          const auto rawX = int(std::floor(fx));
          const auto rawY = int(std::floor(fy));
          const auto x0 = std::clamp(rawX, 0, width - 1);
          const auto y0 = std::clamp(rawY, 0, height - 1);
          const auto x1 = std::clamp(rawX + 1, 0, width - 1);
          const auto y1 = std::clamp(rawY + 1, 0, height - 1);
          const auto tx = std::clamp(fx - std::floor(fx), 0.0f, 1.0f);
          const auto ty = std::clamp(fy - std::floor(fy), 0.0f, 1.0f);
          const auto sample = [&](const int x, const int y) {
            return item.texture->pixels[std::size_t(y) * item.texture->width
                                        + std::size_t(x)];
          };
          const auto c00 = sample(x0, y0);
          const auto c10 = sample(x1, y0);
          const auto c01 = sample(x0, y1);
          const auto c11 = sample(x1, y1);
          // Masked textures use palette entry 255 as transparent. Alpha-test the
          // interpolated sample the way the engine does instead of blending it.
          const auto topAlpha = float((c00 >> 24) & 0xFF) * (1.0f - tx)
                                + float((c10 >> 24) & 0xFF) * tx;
          const auto bottomAlpha = float((c01 >> 24) & 0xFF) * (1.0f - tx)
                                   + float((c11 >> 24) & 0xFF) * tx;
          const auto alpha = topAlpha * (1.0f - ty) + bottomAlpha * ty;
          if ((item.texture->flags & 0x0040u) != 0 && alpha < 128.0f)
            continue;
          const auto blend = [&](const int shift) {
            const auto top = float((c00 >> shift) & 0xFF) * (1.0f - tx)
                             + float((c10 >> shift) & 0xFF) * tx;
            const auto bottom = float((c01 >> shift) & 0xFF) * (1.0f - tx)
                                + float((c11 >> shift) & 0xFF) * tx;
            return std::clamp(
              int((top * (1.0f - ty) + bottom * ty) * item.shade), 0, 255);
          };
          color = QColor(blend(16), blend(8), blend(0));
        }
        if (item.texture && (item.texture->flags & 0x0020u) != 0)
        {
          const auto destination = QColor::fromRgba(image.pixel(x, y));
          const auto add = [](const int base, const int source) {
            return std::min(255, base + source);
          };
          image.setPixel(
            x,
            y,
            qRgba(
              add(destination.red(), color.red()),
              add(destination.green(), color.green()),
              add(destination.blue(), color.blue()),
              255));
        }
        else
          image.setPixel(x, y, color.rgb());
      }
  }

  auto painter = QPainter{&image};
  painter.setRenderHint(QPainter::Antialiasing, true);
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
