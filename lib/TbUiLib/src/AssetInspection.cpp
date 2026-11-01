/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/AssetInspection.h"

#include <QFile>
#include <QPainter>

#include "fs/File.h"
#include "fs/Reader.h"
#include "img/DecodeImage.h"
#include "mdl/GameFileSystem.h"
#include "mdl/Map.h"
#include "project/ContentHash.h"
#include "project/GoldSrcWad.h"
#include "ui/AssetLibrarySource.h"
#include "ui/MapDocument.h"
#include "ui/ProjectReference.h"
#include "ui/ProjectSession.h"
#include "ui/QPathUtils.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace tb::ui::inspection
{
namespace
{
std::uint32_t u32(const Bytes& bytes, const size_t offset)
{
  if (offset > bytes.size() || bytes.size() - offset < 4)
    return 0;
  return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8)
         | (std::uint32_t(bytes[offset + 2]) << 16)
         | (std::uint32_t(bytes[offset + 3]) << 24);
}

constexpr auto SkySuffixes = std::array{"rt", "bk", "lf", "ft", "up", "dn"};
void addSkyFace(Asset& result, const std::string& name, Result<Bytes> bytes)
{
  const auto stem =
    QString::fromStdString(name).section('/', -1).section('.', 0, 0).toLower();
  for (auto i = size_t{}; i < SkySuffixes.size(); ++i)
  {
    if (!stem.endsWith(SkySuffixes[i]))
      continue;
    auto image = bytes.is_error() ? Result<QImage>{std::get<Error>(bytes.error())}
                                  : decodeImage(bytes.value());
    if (image.is_error())
      result.diagnostics << QString::fromStdString(
        name + ": " + std::get<Error>(image.error()).msg);
    else
    {
      result.identity +=
        "/" + QString::fromStdString(project::contentHash(bytes.value()));
      result.skyOriginal[i] = image.value();
      result.skyCompatible[i] =
        image.value()
          .scaled(256, 256, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
          .convertToFormat(QImage::Format_RGB32);
      if (image.value().width() != image.value().height())
        result.diagnostics
          << QObject::tr("%1 is not square").arg(QString::fromStdString(name));
      if (image.value().hasAlphaChannel())
      {
        const auto rgba = image.value().convertToFormat(QImage::Format_RGBA8888);
        auto transparent = false;
        for (auto y = 0; y < rgba.height() && !transparent; ++y)
          for (auto x = 0; x < rgba.width(); ++x)
            if (rgba.constScanLine(y)[x * 4 + 3] != 255)
            {
              transparent = true;
              break;
            }
        if (transparent)
          result.diagnostics
            << QObject::tr("%1 has transparency; publishing requires opaque sky faces")
                 .arg(QString::fromStdString(name));
      }
    }
    return;
  }
}
/**
 * Adds prefab volumes and resolves its explicit references against the project.
 *
 * Without a project there is nothing to resolve against, so only the authored statistics
 * are reported instead of guessing that a reference is missing.
 */
void inspectPrefab(Asset& asset, ProjectSession* session)
{
  const auto text = std::string{asset.compatible.begin(), asset.compatible.end()};
  auto contents = readPrefabContents(text);
  if (contents.is_error())
  {
    asset.prefabIssues << QObject::tr("Prefab text could not be read: %1")
                            .arg(QString::fromStdString(
                              std::get<Error>(contents.error()).msg));
    return;
  }
  asset.prefab = std::move(contents).value();
  if (!session)
  {
    asset.prefabIssues << QObject::tr(
      "Open the prefab from a project to verify that its resources are captured.");
    return;
  }

  const auto index = ProjectReferenceIndex::build(*session);
  if (index.is_error())
  {
    asset.prefabIssues << QObject::tr("Reference index unavailable: %1")
                            .arg(
                              QString::fromStdString(std::get<Error>(index.error()).msg));
    return;
  }

  const auto isToolTexture = [](const std::string& name) {
    auto lowered = name;
    std::ranges::transform(lowered, lowered.begin(), [](const unsigned char c) {
      return char(std::tolower(c));
    });
    static constexpr auto names = std::array{
      std::string_view{"aaatrigger"},
      std::string_view{"bevel"},
      std::string_view{"clip"},
      std::string_view{"hint"},
      std::string_view{"hintskip"},
      std::string_view{"light"},
      std::string_view{"null"},
      std::string_view{"origin"},
      std::string_view{"skip"},
      std::string_view{"split"},
      std::string_view{"trigger"},
      std::string_view{"waterskip"}};
    return std::ranges::any_of(
      names, [&](const auto candidate) { return lowered == candidate; });
  };

  auto missing = QStringList{};
  auto ambiguous = QStringList{};
  for (const auto& material : asset.prefab->materials)
  {
    if (isToolTexture(material))
      continue;
    const auto match = index.value().resolveTexture(material);
    if (match.status == ReferenceStatus::Missing)
      missing << QString::fromStdString(material);
    else if (match.status == ReferenceStatus::Ambiguous)
      ambiguous << QString::fromStdString(material);
  }
  for (const auto& reference : asset.prefab->models)
  {
    const auto match = index.value().resolvePath(reference);
    if (match.status == ReferenceStatus::Missing)
      missing << QString::fromStdString(reference);
    else if (match.status == ReferenceStatus::Ambiguous)
      ambiguous << QString::fromStdString(reference);
  }
  for (const auto& reference : asset.prefab->sounds)
  {
    const auto match = index.value().resolvePath(reference);
    if (match.status == ReferenceStatus::Missing)
      missing << QString::fromStdString(reference);
    else if (match.status == ReferenceStatus::Ambiguous)
      ambiguous << QString::fromStdString(reference);
  }

  constexpr auto MaxNames = 6;
  const auto summarize = [](const QStringList& names) {
    auto visible = names.mid(0, MaxNames).join(QStringLiteral(", "));
    if (names.size() > MaxNames)
      visible += QObject::tr(" … and %1 more").arg(names.size() - MaxNames);
    return visible;
  };
  if (!missing.isEmpty())
    asset.prefabIssues << QObject::tr("%1 referenced resource(s) not captured: %2")
                            .arg(missing.size())
                            .arg(summarize(missing));
  if (!ambiguous.isEmpty())
    asset.prefabIssues << QObject::tr("%1 ambiguous reference(s): %2")
                            .arg(ambiguous.size())
                            .arg(summarize(ambiguous));
}

void checkSky(Asset& result)
{
  auto size = QSize{};
  for (auto i = size_t{}; i < SkySuffixes.size(); ++i)
  {
    const auto& face = result.skyOriginal[i];
    if (face.isNull())
      result.diagnostics << QObject::tr("Missing face: %1").arg(SkySuffixes[i]);
    else if (size.isEmpty())
      size = face.size();
    else if (size != face.size())
      result.diagnostics << QObject::tr("Face dimensions differ: %1").arg(SkySuffixes[i]);
  }
}
} // namespace

bool matchesWaveKind(const WaveInfo& info, const WaveKind kind)
{
  return kind == WaveKind::All || (kind == WaveKind::Looping) == info.loop.has_value();
}

QImage renderSkyCross(const std::array<QImage, 6>& faces, const int side)
{
  if (side <= 0 || side > 1024)
    return {};
  auto image = QImage{side * 4, side * 3, QImage::Format_RGBA8888};
  image.fill(Qt::transparent);
  auto painter = QPainter{&image};
  const auto positions = std::array{
    QPoint{2, 1}, QPoint{3, 1}, QPoint{0, 1}, QPoint{1, 1}, QPoint{1, 0}, QPoint{1, 2}};
  for (auto i = size_t{}; i < faces.size(); ++i)
  {
    const auto rect = QRect{positions[i] * side, QSize{side, side}};
    if (!faces[i].isNull())
      painter.drawImage(rect, faces[i]);
    painter.fillRect(rect.x(), rect.y(), side, 22, QColor{0, 0, 0, 170});
    painter.setPen(Qt::white);
    painter.drawText(rect.adjusted(4, 0, -4, 0), Qt::AlignTop | Qt::AlignLeft,
      QString::fromLatin1(SkySuffixes[i]) + (faces[i].isNull() ? " — missing" : QString{" %1×%2"}.arg(faces[i].width()).arg(faces[i].height())));
  }
  return image;
}

QImage renderSkyView(
  const std::array<QImage, 6>& faces,
  const double yaw,
  const double pitch,
  const int width,
  const int height)
{
  if (
    width <= 0 || height <= 0 || width > 2048 || height > 2048 || !std::isfinite(yaw)
    || !std::isfinite(pitch))
    return {};
  auto image = QImage{width, height, QImage::Format_RGB32};
  image.fill(Qt::black);
  for (auto row = 0; row < height; ++row)
  {
    auto* out = reinterpret_cast<QRgb*>(image.scanLine(row));
    for (auto col = 0; col < width; ++col)
    {
      const auto h = (2.0 * col - width) / width;
      const auto v = (height - 2.0 * row) / width;
      const auto forward = std::cos(pitch) - v * std::sin(pitch);
      const auto x = forward * std::cos(yaw) - h * std::sin(yaw);
      const auto y = forward * std::sin(yaw) + h * std::cos(yaw);
      const auto z = std::sin(pitch) + v * std::cos(pitch);
      const auto ax = std::abs(x), ay = std::abs(y), az = std::abs(z);
      auto face = size_t{};
      auto u = 0.0, t = 0.0;
      // Keep these directions and UVs aligned with Sky.fragsh.
      if (ay >= ax && ay >= az)
      {
        face = y < 0 ? 3 : 1;
        u = y < 0 ? (1 - x / ay) / 2 : (1 + x / ay) / 2;
        t = (1 - z / ay) / 2;
      }
      else if (ax >= ay && ax >= az)
      {
        face = x > 0 ? 0 : 2;
        u = x > 0 ? (1 - y / ax) / 2 : (1 + y / ax) / 2;
        t = (1 - z / ax) / 2;
      }
      else
      {
        face = z > 0 ? 4 : 5;
        u = (1 - y / az) / 2;
        t = z > 0 ? (1 + x / az) / 2 : (1 - x / az) / 2;
      }
      const auto& texture = faces[face];
      if (!texture.isNull())
        out[col] = texture.pixel(
          std::clamp(int(u * texture.width()), 0, texture.width() - 1),
          std::clamp(int(t * texture.height()), 0, texture.height() - 1));
    }
  }
  return image;
}

Result<WaveInfo> readWaveInfo(const Bytes& bytes)
{
  if (
    bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4)
    || std::memcmp(bytes.data() + 8, "WAVE", 4))
    return Error{"Not a RIFF WAVE file."};
  auto info = WaveInfo{};
  auto byteRate = std::uint32_t{};
  auto dataSize = size_t{};
  const auto end = std::min(bytes.size(), size_t(u32(bytes, 4)) + 8);
  for (auto offset = size_t{12}; offset + 8 <= end;)
  {
    const auto size = size_t(u32(bytes, offset + 4));
    const auto start = offset + 8;
    if (size > end - start)
      return Error{"Truncated WAVE chunk."};
    if (!std::memcmp(bytes.data() + offset, "fmt ", 4))
    {
      if (size < 16)
        return Error{"Truncated WAVE format."};
      info.channels = std::uint16_t(u32(bytes, start) >> 16);
      info.sampleRate = u32(bytes, start + 4);
      byteRate = u32(bytes, start + 8);
      info.bits = std::uint16_t(u32(bytes, start + 12) >> 16);
    }
    else if (!std::memcmp(bytes.data() + offset, "data", 4))
      dataSize += size;
    else if (
      !std::memcmp(bytes.data() + offset, "smpl", 4) && size >= 60
      && u32(bytes, start + 28) > 0)
      info.loop = {u32(bytes, start + 44), u32(bytes, start + 48)};
    offset = start + size + (size & 1);
  }
  if (!info.channels || !info.sampleRate || !byteRate || !dataSize)
    return Error{"WAVE format or sample data is missing."};
  info.seconds = double(dataSize) / double(byteRate);
  return info;
}

Result<QImage> decodeImage(const Bytes& bytes)
{
  if (bytes.size() >= 4 && std::memcmp(bytes.data(), "WAD3", 4) == 0)
  {
    const auto archive = project::readGoldSrcWad(bytes);
    if (archive.is_error())
      return std::get<Error>(archive.error());
    for (const auto& entry : archive.value().textures)
    {
      if (!entry.valid)
        continue;
      const auto& lump = entry.miptex;
      const auto width = u32(lump, 16), height = u32(lump, 20);
      if (!width || !height || width > 4096 || height > 4096)
        continue;
      const auto count = size_t(width) * height;
      const auto pixels = size_t(u32(lump, 24));
      const auto palette = size_t(u32(lump, 36)) + count / 64 + 2;
      if (
        pixels > lump.size() || count > lump.size() - pixels || palette > lump.size()
        || 768 > lump.size() - palette)
        continue;
      auto image = QImage{int(width), int(height), QImage::Format_RGBA8888};
      for (auto y = 0u; y < height; ++y)
      {
        auto* row = image.scanLine(int(y));
        for (auto x = 0u; x < width; ++x)
        {
          const auto index = lump[pixels + size_t(y) * width + x];
          const auto color = palette + size_t(index) * 3;
          row[x * 4] = lump[color];
          row[x * 4 + 1] = lump[color + 1];
          row[x * 4 + 2] = lump[color + 2];
          row[x * 4 + 3] = entry.name.starts_with('{') && index == 255 ? 0 : 255;
        }
      }
      return image;
    }
    return Error{"The WAD contains no readable texture."};
  }
  const auto decoded = img::decodeImage(bytes.data(), bytes.size());
  if (decoded.is_error())
    return std::get<Error>(decoded.error());
  const auto& image = decoded.value();
  return QImage{
    image.pixels.data(),
    int(image.width),
    int(image.height),
    int(image.width * 4),
    QImage::Format_RGBA8888}
    .copy();
}

Result<Asset> readLibraryAsset(
  const AssetLibrarySource& source,
  const project::LibraryAsset& asset,
  ProjectSession* session)
{
  auto original = source.readOriginal(asset.id);
  if (original.is_error())
    return std::get<Error>(original.error());
  auto compatible = source.readContent(asset.id);
  if (compatible.is_error())
    return std::get<Error>(compatible.error());
  auto result = Asset{};
  result.resource.type = asset.type == project::AssetType::Skybox
                           ? BrowserCellType::Texture
                           : assetTypeForExtension(pathFromQString(
                               QString::fromStdString(asset.sourceContainer)));
  result.resource.skybox = asset.type == project::AssetType::Skybox;
  result.resource.source = BrowserSource::AssetLibrary;
  result.resource.libraryId = source.libraryId();
  result.resource.libraryAssetId = asset.id;
  result.resource.libraryRevision = asset.revision;
  result.resource.libraryContentHash = asset.contentHash;
  result.resource.path = pathFromQString(QString::fromStdString(asset.virtualPath));
  result.resource.absolutePath =
    pathFromQString(source.location())
    / pathFromQString(QString::fromStdString(asset.sourceContainer));
  result.resource.missing = asset.missing;
  result.name = QString::fromStdString(asset.displayName);
  result.source = source.name();
  result.recipe = QString::fromStdString(asset.recipe);
  result.identity =
    QString::fromStdString(source.libraryId() + "/" + asset.id + "/" + asset.contentHash);
  result.original = std::move(original).value();
  result.compatible = std::move(compatible).value();
  if (result.resource.skybox)
  {
    const auto dependencies = source.dependencies(asset.id);
    const auto entries = source.assets();
    if (dependencies.is_success() && entries.is_success())
      for (const auto& edge : dependencies.value())
        if (edge.kind == "skybox-face")
          for (const auto& face : entries.value())
            if (face.id == edge.dependencyId)
              addSkyFace(result, face.virtualPath, source.readOriginal(face.id));
    checkSky(result);
  }
  if (result.resource.type == BrowserCellType::Prefab)
    inspectPrefab(result, session);
  return result;
}

Result<Asset> readBrowserAsset(
  const BrowserAsset& asset, ProjectSession* session, MapDocument& document)
{
  if (asset.source == BrowserSource::AssetLibrary)
  {
    if (!session)
      return Error{"No project resource context is available."};
    const auto libraries = session->libraries();
    if (libraries.is_error())
      return std::get<Error>(libraries.error());
    for (const auto& connection : libraries.value())
    {
      if (connection.info.id != asset.libraryId)
        continue;
      auto source = openLocalAssetLibrary(connection.info.path);
      if (!source)
        return Error{"The asset library is unavailable."};
      const auto entries = source->assets();
      if (entries.is_error())
        return std::get<Error>(entries.error());
      for (const auto& entry : entries.value())
        if (entry.id == asset.libraryAssetId)
        {
          auto result = readLibraryAsset(*source, entry, session);
          if (result.is_success())
          {
            auto loaded = std::move(result).value();
            loaded.resource.projectAssetId = asset.projectAssetId;
            loaded.resource.projectRevision = asset.projectRevision;
            loaded.resource.projectUpdateAvailable = asset.projectUpdateAvailable;
            return loaded;
          }
          return result;
        }
    }
    return Error{"The library resource no longer exists."};
  }
  auto result = Asset{};
  result.resource = asset;
  result.name = asset.displayName.empty() ? pathAsQString(asset.path.filename())
                                          : QString::fromStdString(asset.displayName);
  result.source = QObject::tr(browserSourceName(asset.source));
  if (asset.source == BrowserSource::Project)
  {
    if (!session || asset.projectId != session->store().info().id)
      return Error{"The resource belongs to another project."};
    const auto id = asset.projectAssetId.empty() ? asset.assetId : asset.projectAssetId;
    const auto stored = session->store().findAsset(id);
    if (stored.is_error())
      return std::get<Error>(stored.error());
    if (!stored.value())
      return Error{"The project resource no longer exists."};
    const auto& entry = *stored.value();
    auto original = session->store().readAsset(id, project::ContentKind::Original);
    auto compatible = session->store().readAsset(id, project::ContentKind::Compatible);
    if (original.is_error())
      return std::get<Error>(original.error());
    if (compatible.is_error())
      return std::get<Error>(compatible.error());
    result.original = std::move(original).value();
    result.compatible = std::move(compatible).value();
    result.recipe = QString::fromStdString(entry.conversionRecipe);
    result.name = QString::fromStdString(entry.displayName);
    result.identity = QString::fromStdString(
      asset.projectId + "/" + id + "/" + std::to_string(entry.revision));
    result.resource.revision = entry.revision;
    result.resource.assetId = id;
    result.resource.projectAssetId = id;
    result.resource.libraryId = entry.library.libraryId;
    result.resource.libraryAssetId = entry.library.libraryAssetId;
    result.resource.libraryRevision = entry.library.libraryRevision;
    result.resource.path = entry.publishPath;
    result.resource.type = assetTypeForExtension(entry.publishPath);
    if (entry.type == project::AssetType::Texture)
      result.resource.type = BrowserCellType::Texture;
    result.resource.skybox = entry.type == project::AssetType::Skybox;
    if (result.resource.skybox)
    {
      result.resource.type = BrowserCellType::Texture;
      const auto dependencies = session->store().dependencies(id);
      if (dependencies.is_success())
        for (const auto& edge : dependencies.value())
          if (edge.kind == "skybox-face")
          {
            const auto face = session->store().findAsset(edge.dependencyId);
            if (face.is_success() && face.value())
              addSkyFace(
                result,
                face.value()->sourceName,
                session->store().readAsset(
                  edge.dependencyId, project::ContentKind::Original));
          }
      checkSky(result);
    }
    const auto file = session->assetFile(entry);
    if (file.is_success())
      result.resource.absolutePath = file.value();
  }
  else
  {
    const auto path = asset.absolutePath.empty() ? asset.path : asset.absolutePath;
    if (path.is_absolute())
    {
      auto file = QFile{pathAsQString(path)};
      if (!file.open(QIODevice::ReadOnly))
        return Error{file.errorString().toStdString()};
      const auto bytes = file.readAll();
      if (file.error() != QFileDevice::NoError)
        return Error{file.errorString().toStdString()};
      result.original.assign(bytes.begin(), bytes.end());
    }
    else
    {
      const auto file = document.map().gameFileSystem().openFile(path);
      if (file.is_error())
        return std::get<Error>(file.error());
      auto reader = file.value()->reader().buffer();
      const auto bytes = reader.stringView();
      result.original.assign(bytes.begin(), bytes.end());
    }
    result.compatible = result.original;
    result.identity = pathAsQString(path) + "/"
                      + QString::fromStdString(project::contentHash(result.original));
  }
  if (result.resource.type == BrowserCellType::Prefab)
    inspectPrefab(result, session);
  return result;
}
} // namespace tb::ui::inspection
