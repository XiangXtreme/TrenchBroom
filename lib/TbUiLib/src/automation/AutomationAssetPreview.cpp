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

#include "ui/automation/AutomationAssetPreview.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QImage>

#include "project/ProjectAsset.h"
#include "project/ProjectStore.h"
#include "ui/AssetInspection.h"
#include "ui/GoldSrcSpritePreview.h"
#include "ui/ModelThumbnail.h"
#include "ui/ProjectSession.h"
#include "ui/QPathUtils.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <vector>

namespace tb::ui
{
namespace
{

constexpr auto MinPreviewSize = 16;
constexpr auto MaxPreviewSize = 1024;

std::string safeFileName(const std::string& text)
{
  auto result = std::string{};
  for (const auto character : text)
  {
    if (result.size() == 48u)
    {
      break;
    }
    const auto value = static_cast<unsigned char>(character);
    result += std::isalnum(value) != 0 ? character : '_';
  }
  return result.empty() ? std::string{"asset"} : result;
}

Result<std::vector<std::uint8_t>> readFileBytes(const std::filesystem::path& path)
{
  auto file = QFile{pathAsQString(path)};
  if (!file.open(QIODevice::ReadOnly))
  {
    return Error{"Could not read " + path.generic_string()};
  }
  const auto bytes = file.readAll();
  return std::vector<std::uint8_t>{bytes.begin(), bytes.end()};
}

QImage fitPreview(const QImage& image, const int size)
{
  if (image.width() <= size && image.height() <= size)
  {
    return image;
  }
  return image.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

Result<std::filesystem::path> writePreviewPng(
  const QImage& image, const std::filesystem::path& folder, const std::string& key)
{
  auto ec = std::error_code{};
  std::filesystem::create_directories(folder, ec);
  const auto digest =
    QCryptographicHash::hash(QByteArray::fromStdString(key), QCryptographicHash::Sha1)
      .toHex()
      .toStdString();
  const auto path = folder / (digest.substr(0, 20) + ".png");
  if (!image.save(pathAsQString(path), "PNG"))
  {
    return Error{"Could not write " + path.generic_string()};
  }
  return path;
}

AutomationAssetPreview unsupported(std::string source, std::string message)
{
  auto result = AutomationAssetPreview{};
  result.status = "unsupported";
  result.source = std::move(source);
  result.message = std::move(message);
  return result;
}

AutomationAssetPreview missing(std::string source, std::string message)
{
  auto result = AutomationAssetPreview{};
  result.status = "missing";
  result.source = std::move(source);
  result.message = std::move(message);
  return result;
}

/** Wraps one decoded image into a written preview, keeping failures non-fatal. */
AutomationAssetPreview imageResult(
  const QImage& image,
  const std::string& source,
  const std::string& key,
  const int size,
  const std::filesystem::path& outputFolder)
{
  if (image.isNull())
  {
    return unsupported(source, "The asset produced an empty image.");
  }
  const auto scaled = fitPreview(image, size);
  const auto written = writePreviewPng(scaled, outputFolder, key);
  if (written.is_error())
  {
    return missing(source, std::get<Error>(written.error()).msg);
  }
  auto result = AutomationAssetPreview{};
  result.status = "image";
  result.source = source;
  result.path = written.value().generic_string();
  result.width = scaled.width();
  result.height = scaled.height();
  return result;
}

} // namespace

AutomationAssetPreview previewAutomationAsset(
  ProjectSession* project,
  const AutomationAssetSummary& asset,
  const int size,
  const std::filesystem::path& outputFolder)
{
  const auto resolvedSize = std::clamp(size, MinPreviewSize, MaxPreviewSize);
  const auto key = asset.scope + "|" + asset.id + "|" + std::to_string(resolvedSize);

  auto file = std::filesystem::path{};
  if (asset.scope != "game" && project == nullptr)
  {
    return missing(asset.scope, "The current document has no owning project.");
  }
  if (asset.scope == "project")
  {
    const auto stored = project->store().findAsset(asset.id);
    if (stored.is_error())
    {
      return missing("project", std::get<Error>(stored.error()).msg);
    }
    if (!stored.value())
    {
      return missing("project", "The project no longer contains this asset.");
    }
    const auto materialized = project->assetFile(*stored.value());
    if (materialized.is_error())
    {
      return missing("project", std::get<Error>(materialized.error()).msg);
    }
    file = materialized.value();
  }
  else if (asset.scope == "library")
  {
    const auto materialized = project->libraryAssetFile(asset.libraryId, asset.id);
    if (materialized.is_error())
    {
      return missing("library", std::get<Error>(materialized.error()).msg);
    }
    file = materialized.value();
  }
  else
  {
    if (asset.absolutePath.empty())
    {
      return missing("game", "The game asset has no resolved file on disk.");
    }
    file = asset.absolutePath;
  }

  if (asset.type == "sound")
  {
    const auto bytes = readFileBytes(file);
    if (bytes.is_error())
    {
      return missing("sound", std::get<Error>(bytes.error()).msg);
    }
    const auto wave = inspection::readWaveInfo(bytes.value());
    if (wave.is_error())
    {
      return unsupported("sound", std::get<Error>(wave.error()).msg);
    }
    auto result = AutomationAssetPreview{};
    result.status = "audio";
    result.source = "sound";
    result.path = file.generic_string();
    result.message = std::to_string(wave.value().sampleRate) + " Hz, "
                     + std::to_string(wave.value().channels) + " channel(s), "
                     + std::to_string(wave.value().bits) + " bit";
    return result;
  }

  if (asset.type == "model")
  {
    const auto scene = loadModelPreviewScene(file.parent_path(), file.filename(), true);
    if (!scene || scene->frames.empty())
    {
      return unsupported("model", "Only GoldSrc studio models can be previewed.");
    }
    return imageResult(
      renderModelPreview(scene->frames.front(), resolvedSize),
      "model",
      key,
      resolvedSize,
      outputFolder);
  }

  if (asset.type == "sprite")
  {
    const auto bytes = readFileBytes(file);
    if (bytes.is_error())
    {
      return missing("sprite", std::get<Error>(bytes.error()).msg);
    }
    const auto sprite = loadGoldSrcSpritePreview(bytes.value());
    if (!sprite)
    {
      return unsupported("sprite", "Only GoldSrc sprites can be previewed.");
    }
    const auto image =
      QImage{
        sprite->rgba.data(),
        int(sprite->width),
        int(sprite->height),
        int(sprite->width * 4u),
        QImage::Format_RGBA8888}
        .copy();
    return imageResult(image, "sprite", key, resolvedSize, outputFolder);
  }

  if (asset.nativeFormat == "skybox")
  {
    return unsupported(
      "skybox",
      "A skybox needs all six faces; compare them in the asset library instead.");
  }

  const auto bytes = readFileBytes(file);
  if (bytes.is_error())
  {
    return missing(asset.type, std::get<Error>(bytes.error()).msg);
  }
  const auto decoded = inspection::decodeImage(bytes.value(), asset.nativeFormat);
  if (decoded.is_error())
  {
    return unsupported(asset.type, std::get<Error>(decoded.error()).msg);
  }
  return imageResult(decoded.value(), asset.type, key, resolvedSize, outputFolder);
}

} // namespace tb::ui
