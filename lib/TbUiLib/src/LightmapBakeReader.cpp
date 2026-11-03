/*
 Copyright (C) 2026 XiangXtreme

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

#include "ui/LightmapBakeReader.h"

#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <cstring>

namespace tb::ui
{

namespace
{

QString toQString(const std::filesystem::path& path)
{
  return QString::fromStdString(path.string());
}

double numberAt(const QJsonArray& array, const int index, const double fallback = 0.0)
{
  const auto value = array.at(index);
  return value.isDouble() ? value.toDouble() : fallback;
}

vm::vec2i readVec2i(const QJsonArray& array)
{
  return {static_cast<int>(numberAt(array, 0)), static_cast<int>(numberAt(array, 1))};
}

vm::vec4f readVec4f(const QJsonArray& array)
{
  return {
    static_cast<float>(numberAt(array, 0)), static_cast<float>(numberAt(array, 1)),
    static_cast<float>(numberAt(array, 2)), static_cast<float>(numberAt(array, 3))};
}

std::vector<vm::vec2f> readPoly(const QJsonArray& array)
{
  auto result = std::vector<vm::vec2f>{};
  result.reserve(static_cast<size_t>(array.size()));
  for (const auto& value : array)
  {
    const auto point = value.toArray();
    result.emplace_back(
      static_cast<float>(numberAt(point, 0)), static_cast<float>(numberAt(point, 1)));
  }
  return result;
}

bool readAtlasImage(
  const std::filesystem::path& path, render::LightmapBake& bake, std::string& error)
{
  auto image = QImage{toQString(path)};
  if (image.isNull())
  {
    error = "could not read the lightmap atlas";
    return false;
  }

  image = image.convertToFormat(QImage::Format_RGB888);
  if (image.isNull() || image.width() <= 0 || image.height() <= 0)
  {
    error = "the lightmap atlas has no pixels";
    return false;
  }

  bake.width = image.width();
  bake.height = image.height();
  bake.pixels.assign(
    static_cast<size_t>(bake.width) * static_cast<size_t>(bake.height) * 3u, 0);

  for (auto row = 0; row < bake.height; ++row)
  {
    std::memcpy(
      bake.pixels.data() + static_cast<size_t>(row) * static_cast<size_t>(bake.width) * 3u,
      image.constScanLine(row), static_cast<size_t>(bake.width) * 3u);
  }
  return true;
}

} // namespace

std::optional<render::LightmapBake> readLightmapBake(
  const std::filesystem::path& manifestPath,
  const std::filesystem::path& atlasPath,
  std::string& error)
{
  auto file = QFile{toQString(manifestPath)};
  if (!file.open(QIODevice::ReadOnly))
  {
    error = "could not read the lightmap manifest";
    return std::nullopt;
  }

  auto parseError = QJsonParseError{};
  const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject())
  {
    error = "the lightmap manifest is not valid json";
    return std::nullopt;
  }

  auto bake = render::LightmapBake{};
  if (!readAtlasImage(atlasPath, bake, error))
  {
    return std::nullopt;
  }

  const auto root = document.object();
  const auto faces = root.value("faces").toArray();
  bake.faces.reserve(static_cast<size_t>(faces.size()));
  for (const auto& value : faces)
  {
    const auto object = value.toObject();

    auto record = render::LightmapFaceRecord{};
    const auto plane = object.value("plane").toArray();
    record.normal = vm::vec3f{
      static_cast<float>(numberAt(plane, 0)), static_cast<float>(numberAt(plane, 1)),
      static_cast<float>(numberAt(plane, 2))};
    record.dist = static_cast<float>(object.value("dist").toDouble());

    const auto vecs = object.value("vecs").toArray();
    record.vecs[0] = readVec4f(vecs.at(0).toArray());
    record.vecs[1] = readVec4f(vecs.at(1).toArray());

    record.mins = readVec2i(object.value("mins").toArray());
    record.size = readVec2i(object.value("size").toArray());
    record.tile = readVec2i(object.value("tile").toArray());
    record.poly = readPoly(object.value("poly").toArray());

    if (record.size.x() <= 0 || record.size.y() <= 0 || record.poly.size() < 3)
    {
      continue;
    }
    bake.faces.push_back(std::move(record));
  }

  if (bake.faces.empty())
  {
    error = "the lightmap manifest has no faces";
    return std::nullopt;
  }
  return bake;
}

} // namespace tb::ui
