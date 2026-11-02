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

#include "ui/PrefabAsset.h"

#include <QColor>
#include <QPainter>
#include <QPen>

#include "base/Error.h"
#include "base/Logger.h"
#include "base/PreferenceManager.h"
#include "base/SimpleParserStatus.h"
#include "fs/DiskIO.h"
#include "fs/File.h"
#include "fs/PathInfo.h"
#include "fs/Reader.h"
#include "mdl/Brush.h"
#include "mdl/BrushFace.h"
#include "mdl/EntityProperties.h"
#include "mdl/MapFormat.h"
#include "mdl/StandardMapParser.h"
#include "prefs/Preferences.h"
#include "ui/SystemPaths.h"

#include "kd/filesystem_utils.h"
#include "kd/path_utils.h"
#include "kd/vector_utils.h"

#include <fmt/format.h>
#include <fmt/std.h>

#include <algorithm>
#include <cmath>
#include <sstream>

namespace tb::ui
{
namespace
{

constexpr auto ThumbnailCropPaddingRatio = 0.12;
constexpr auto ThumbnailBackgroundTolerance = 10;
constexpr auto MaterialCollectionMetadataPrefix = "// tb-prefab-material-collection: ";
constexpr auto WadMetadataPrefix = "// tb-prefab-wad: ";

bool differsFromBackground(const QColor& color, const QColor& background)
{
  return std::abs(color.red() - background.red()) > ThumbnailBackgroundTolerance
         || std::abs(color.green() - background.green()) > ThumbnailBackgroundTolerance
         || std::abs(color.blue() - background.blue()) > ThumbnailBackgroundTolerance
         || std::abs(color.alpha() - background.alpha()) > ThumbnailBackgroundTolerance;
}

template <typename T, typename ToString>
std::string appendPrefabMetadata(
  const std::string& prefabText,
  std::vector<T> values,
  const std::string_view prefix,
  ToString toString)
{
  values.erase(
    std::remove_if(
      values.begin(), values.end(), [](const auto& value) { return value.empty(); }),
    values.end());
  values = kdl::vec_sort_and_remove_duplicates(std::move(values));

  auto result = std::string{};
  for (const auto& value : values)
  {
    result += prefix;
    result += toString(value);
    result += '\n';
  }
  result += prefabText;
  return result;
}

template <typename T, typename FromString>
std::vector<T> prefabMetadata(
  const std::string& prefabText, const std::string_view prefix, FromString fromString)
{
  auto result = std::vector<T>{};
  auto stream = std::istringstream{prefabText};
  auto line = std::string{};
  while (std::getline(stream, line))
  {
    if (line.starts_with(prefix))
    {
      result.push_back(fromString(line.substr(prefix.size())));
    }
  }
  return kdl::vec_sort_and_remove_duplicates(std::move(result));
}

/** Collects authored volumes and the explicit GoldSrc references of a prefab. */
class PrefabContentsParser : public mdl::StandardMapParser
{
  PrefabContents m_contents;
  std::string m_classname;
  bool m_sawWorld = false;
  std::optional<vm::vec3d> m_low;
  std::optional<vm::vec3d> m_high;
  mdl::MapFormat m_format;
  std::vector<mdl::BrushFace> m_brushFaces;
  bool m_brushValid = true;

public:
  PrefabContentsParser(const std::string& text, const mdl::MapFormat format)
    : StandardMapParser{text, format, format}
    , m_format{format}
  {
  }

  Result<PrefabContents> read()
  {
    auto logger = NullLogger{};
    auto status = SimpleParserStatus{logger};
    return parseEntities(status) | kdl::and_then([&]() -> Result<PrefabContents> {
             if (!m_sawWorld)
               return Error{"Prefab text has no worldspawn entity."};
             if (m_low && m_high)
               m_contents.bounds = vm::bbox3d{*m_low, *m_high};
             m_contents.materials =
               kdl::vec_sort_and_remove_duplicates(std::move(m_contents.materials));
             m_contents.models =
               kdl::vec_sort_and_remove_duplicates(std::move(m_contents.models));
             m_contents.sounds =
               kdl::vec_sort_and_remove_duplicates(std::move(m_contents.sounds));
             m_contents.skies =
               kdl::vec_sort_and_remove_duplicates(std::move(m_contents.skies));
             return std::move(m_contents);
           });
  }

private:
  static void extend(
    std::optional<vm::vec3d>& low, std::optional<vm::vec3d>& high, const vm::vec3d& point)
  {
    if (!low || !high)
    {
      low = point;
      high = point;
      return;
    }
    for (auto axis = 0u; axis < 3u; ++axis)
    {
      (*low)[axis] = std::min((*low)[axis], point[axis]);
      (*high)[axis] = std::max((*high)[axis], point[axis]);
    }
  }

  void addFace(
    const vm::vec3d& point1,
    const vm::vec3d& point2,
    const vm::vec3d& point3,
    std::string material)
  {
    ++m_contents.faces;
    if (!material.empty())
      m_contents.materials.push_back(std::move(material));
    auto face = mdl::BrushFace::create(point1, point2, point3, "", {}, {}, m_format);
    if (face.is_error())
      m_brushValid = false;
    else
      m_brushFaces.push_back(std::move(face).value());
  }

  void onBeginEntity(
    const FileLocation&,
    std::vector<mdl::EntityProperty> properties,
    ParserStatus&) override
  {
    m_classname = mdl::findEntityPropertyOrDefault(properties, "classname");
    m_sawWorld |= m_classname == "worldspawn";
    if (m_classname != "worldspawn")
      ++m_contents.entities;

    for (const auto& property : properties)
    {
      const auto& key = property.key();
      const auto& value = property.value();
      if (value.empty())
        continue;
      if (m_classname == "worldspawn" && key == "skyname")
      {
        m_contents.skies.push_back(value);
        continue;
      }
      const auto sound = (m_classname == "ambient_generic" && key == "message")
                         || (m_classname == "env_message" && key == "messagesound");
      if (sound)
      {
        if (value.front() != '!')
          m_contents.sounds.push_back("sound/" + value);
        continue;
      }
      // Same class/key pairs as the project resource audit: editor preview models are
      // not runtime dependencies.
      const auto model = key == "model"
                         && (m_classname == "cycler" || m_classname == "cycler_sprite"
                             || m_classname == "cycler_wreckage"
                             || m_classname == "env_sprite" || m_classname == "env_glow"
                             || m_classname == "hostage_entity");
      const auto sprite =
        (m_classname == "env_beam" && key == "texture")
        || (m_classname == "env_laser" && (key == "texture" || key == "EndSprite"));
      const auto gib = (m_classname == "func_breakable" || m_classname == "func_pushable")
                       && key == "gibmodel";
      const auto shooter = m_classname == "env_shooter" && key == "shootmodel";
      if (model || sprite || gib || shooter)
        m_contents.models.push_back(value);
    }
  }
  void onEndEntity(const FileLocation&, ParserStatus&) override {}
  void onBeginBrush(const FileLocation&, ParserStatus&) override
  {
    ++m_contents.brushes;
    m_brushFaces.clear();
    m_brushValid = true;
  }
  void onEndBrush(const FileLocation&, ParserStatus&) override
  {
    if (!m_brushValid || m_brushFaces.size() < 4)
      return;
    // Use the editor's plane intersection and validation, with room for GoldSrc maps.
    const auto brush =
      mdl::Brush::create(vm::bbox3d{16777216.0}, std::move(m_brushFaces));
    if (brush.is_error() || !brush.value().fullySpecified())
      return;
    const auto& bounds = brush.value().bounds();
    m_contents.brushBounds.push_back(bounds);
    extend(m_low, m_high, bounds.min);
    extend(m_low, m_high, bounds.max);
  }
  void onStandardBrushFace(
    const FileLocation&,
    mdl::MapFormat,
    const vm::vec3d& point1,
    const vm::vec3d& point2,
    const vm::vec3d& point3,
    std::string materialName,
    const mdl::UvAttributes&,
    const mdl::SurfaceAttributes&,
    ParserStatus&) override
  {
    addFace(point1, point2, point3, std::move(materialName));
  }
  void onValveBrushFace(
    const FileLocation&,
    mdl::MapFormat,
    const vm::vec3d& point1,
    const vm::vec3d& point2,
    const vm::vec3d& point3,
    std::string materialName,
    const mdl::UvAttributes&,
    const mdl::SurfaceAttributes&,
    const vm::vec3d&,
    const vm::vec3d&,
    ParserStatus&) override
  {
    addFace(point1, point2, point3, std::move(materialName));
  }
  void onPatch(
    const FileLocation&,
    const FileLocation&,
    mdl::MapFormat,
    size_t,
    size_t,
    std::vector<vm::vec<double, 5>>,
    std::string,
    ParserStatus&) override
  {
    // Patches are not common in GoldSrc prefabs and have no brush volume to report.
  }
};

Result<PrefabContents> parsePrefabContents(
  const std::string& text, const mdl::MapFormat format)
{
  auto parser = PrefabContentsParser{text, format};
  return parser.read();
}

} // namespace

Result<PrefabContents> readPrefabContents(const std::string& text)
{
  // Prefabs inherit the map format they were saved from. Valve is attempted first because
  // GoldSrc maps dominate this project; Standard text fails the Valve face grammar.
  auto valve = parsePrefabContents(text, mdl::MapFormat::Valve);
  if (valve.is_success())
    return valve;
  auto standard = parsePrefabContents(text, mdl::MapFormat::Standard);
  if (standard.is_success())
    return standard;
  return valve;
}

QImage renderPrefabThumbnail(const PrefabContents& contents, const int size)
{
  if (size <= 0 || size > 512 || contents.brushBounds.empty())
    return {};

  const auto& bounds = contents.bounds;
  const auto extentX = bounds.max.x() - bounds.min.x();
  const auto extentY = bounds.max.y() - bounds.min.y();
  const auto extent = std::max({extentX, extentY, 1.0});
  const auto scale = double(size) * 0.8 / extent;
  const auto offsetX = (double(size) - extentX * scale) / 2.0 - bounds.min.x() * scale;
  const auto offsetY = (double(size) - extentY * scale) / 2.0 - bounds.min.y() * scale;

  auto image = QImage{size, size, QImage::Format_ARGB32_Premultiplied};
  image.fill(Qt::transparent);
  auto painter = QPainter{&image};
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(QPen{QColor{132, 148, 172, 220}, 1.5});
  painter.setBrush(QColor{132, 148, 172, 90});
  for (const auto& brush : contents.brushBounds)
  {
    const auto x = brush.min.x() * scale + offsetX;
    const auto y = brush.min.y() * scale + offsetY;
    const auto width = std::max(1.0, (brush.max.x() - brush.min.x()) * scale);
    const auto height = std::max(1.0, (brush.max.y() - brush.min.y()) * scale);
    painter.drawRect(QRectF{x, y, width, height});
  }
  painter.end();
  return image;
}

bool isPrefabAssetPath(const std::filesystem::path& path)
{
  return kdl::path_to_lower(path.extension()) == ".tbprefab";
}

std::filesystem::path defaultPrefabDirectory()
{
  return SystemPaths::userDataDirectory() / "prefabs";
}

std::filesystem::path configuredPrefabDirectory()
{
  const auto& directory = pref(Preferences::PrefabDirectory);
  if (directory.empty() || fs::Disk::pathInfo(directory) != fs::PathInfo::Directory)
  {
    return defaultPrefabDirectory();
  }
  return directory;
}

std::filesystem::path prefabPathForName(
  const std::filesystem::path& directory, const std::string& name)
{
  auto path = directory / std::filesystem::path{name}.filename();
  if (path.extension().empty())
  {
    path.replace_extension(".tbprefab");
  }
  return path;
}

std::filesystem::path prefabThumbnailPath(const std::filesystem::path& prefabPath)
{
  auto path = prefabPath;
  path.replace_extension(".png");
  return path;
}

std::string appendPrefabMaterialCollections(
  const std::string& prefabText,
  const std::vector<std::filesystem::path>& materialCollections)
{
  return appendPrefabMetadata(
    prefabText,
    materialCollections,
    MaterialCollectionMetadataPrefix,
    [](const auto& path) { return path.generic_string(); });
}

std::vector<std::filesystem::path> prefabMaterialCollections(
  const std::string& prefabText)
{
  return prefabMetadata<std::filesystem::path>(
    prefabText, MaterialCollectionMetadataPrefix, [](const auto& str) {
      return std::filesystem::path{str};
    });
}

std::string appendPrefabWadPaths(
  const std::string& prefabText, const std::vector<std::string>& wadPaths)
{
  return appendPrefabMetadata(
    prefabText, wadPaths, WadMetadataPrefix, [](const auto& str) { return str; });
}

std::vector<std::string> prefabWadPaths(const std::string& prefabText)
{
  return prefabMetadata<std::string>(
    prefabText, WadMetadataPrefix, [](const auto& str) { return str; });
}

QImage cropPrefabThumbnailImage(const QImage& image)
{
  if (image.isNull() || image.width() < 2 || image.height() < 2)
  {
    return image;
  }

  const auto background = image.pixelColor(0, 0);
  auto left = image.width();
  auto top = image.height();
  auto right = -1;
  auto bottom = -1;

  for (auto y = 0; y < image.height(); ++y)
  {
    for (auto x = 0; x < image.width(); ++x)
    {
      if (differsFromBackground(image.pixelColor(x, y), background))
      {
        left = std::min(left, x);
        top = std::min(top, y);
        right = std::max(right, x);
        bottom = std::max(bottom, y);
      }
    }
  }

  if (right < left || bottom < top)
  {
    return image;
  }

  const auto padding = int(
    std::round(std::max(right - left + 1, bottom - top + 1) * ThumbnailCropPaddingRatio));
  const auto cropLeft = std::max(0, left - padding);
  const auto cropTop = std::max(0, top - padding);
  const auto cropRight = std::min(image.width() - 1, right + padding);
  const auto cropBottom = std::min(image.height() - 1, bottom + padding);

  return image.copy(
    cropLeft, cropTop, cropRight - cropLeft + 1, cropBottom - cropTop + 1);
}

Result<void> checkPrefabNameAvailable(
  const std::filesystem::path& directory, const std::string& name)
{
  const auto path = prefabPathForName(directory, name);
  if (!isPrefabAssetPath(path))
  {
    return Error{"Prefab name must use the .tbprefab extension"};
  }
  if (fs::Disk::pathInfo(path) != fs::PathInfo::Unknown)
  {
    return Error{fmt::format("Prefab already exists: {}", path.filename().string())};
  }
  return kdl::void_success;
}

Result<std::string> readPrefabAsset(const std::filesystem::path& path)
{
  if (!isPrefabAssetPath(path))
  {
    return Error{"Not a prefab asset"};
  }

  return fs::Disk::openFile(path) | kdl::transform([](auto file) {
           auto reader = file->reader().buffer();
           return std::string{reader.stringView()};
         });
}

Result<void> writePrefabAsset(const std::filesystem::path& path, const std::string& text)
{
  if (!isPrefabAssetPath(path))
  {
    return Error{"Not a prefab asset"};
  }

  return fs::Disk::createDirectory(path.parent_path())
         | kdl::and_then([&](auto) -> Result<void> {
             return kdl::with_ostream(
               path,
               std::ios::out | std::ios::binary | std::ios::trunc,
               [&](auto& stream) -> Result<void> {
                 stream << text;
                 if (!stream.good())
                 {
                   return Error{"Failed to write prefab asset"};
                 }
                 return Result<void>{};
               });
           });
}

Result<void> renamePrefabAsset(const std::filesystem::path& path, const std::string& name)
{
  if (!isPrefabAssetPath(path))
  {
    return Error{"Not a prefab asset"};
  }
  if (fs::Disk::pathInfo(path) != fs::PathInfo::File)
  {
    return Error{"Prefab asset does not exist"};
  }

  const auto newPath = prefabPathForName(path.parent_path(), name);
  if (newPath == path)
  {
    return kdl::void_success;
  }
  if (const auto result = checkPrefabNameAvailable(path.parent_path(), name);
      result.is_error())
  {
    return result;
  }

  return fs::Disk::moveFile(path, newPath) | kdl::and_then([&]() -> Result<void> {
           const auto oldThumbnailPath = prefabThumbnailPath(path);
           if (fs::Disk::pathInfo(oldThumbnailPath) != fs::PathInfo::File)
           {
             return kdl::void_success;
           }
           return fs::Disk::moveFile(oldThumbnailPath, prefabThumbnailPath(newPath));
         });
}

Result<void> deletePrefabAsset(const std::filesystem::path& path)
{
  if (!isPrefabAssetPath(path))
  {
    return Error{"Not a prefab asset"};
  }

  return fs::Disk::deleteFile(path) | kdl::and_then([&](auto) -> Result<void> {
           return fs::Disk::deleteFile(prefabThumbnailPath(path))
                  | kdl::transform([](auto) {});
         });
}

} // namespace tb::ui
