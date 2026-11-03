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
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QString>

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
#include <optional>
#include <utility>
#include <vector>

namespace tb::ui
{
namespace
{

constexpr auto MinPreviewSize = 16;
constexpr auto MaxPreviewSize = 256;
constexpr auto MinColumns = 1;
constexpr auto MaxColumns = 10;
constexpr auto MinRows = 1;
constexpr auto MaxRows = 8;
constexpr auto SheetPadding = 12;
constexpr auto SheetCaptionHeight = 26;
constexpr auto SheetLabelHeight = 32;

struct RenderedPreview
{
  std::string status;
  QImage image;
  std::string path;
  std::string source;
  std::string message;
};

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

std::string digestOf(const std::string& text)
{
  return QCryptographicHash::hash(
           QByteArray::fromStdString(text), QCryptographicHash::Sha1)
    .toHex()
    .toStdString();
}

Result<std::filesystem::path> writePng(
  const QImage& image, const std::filesystem::path& folder, const std::string& key)
{
  auto ec = std::error_code{};
  std::filesystem::create_directories(folder, ec);
  const auto path = folder / (digestOf(key).substr(0, 20) + ".png");
  if (!image.save(pathAsQString(path), "PNG"))
  {
    return Error{"Could not write " + path.generic_string()};
  }
  return path;
}

RenderedPreview unsupported(std::string source, std::string message)
{
  auto result = RenderedPreview{};
  result.status = "unsupported";
  result.source = std::move(source);
  result.message = std::move(message);
  return result;
}

RenderedPreview missing(std::string source, std::string message)
{
  auto result = RenderedPreview{};
  result.status = "missing";
  result.source = std::move(source);
  result.message = std::move(message);
  return result;
}

RenderedPreview imagePreview(QImage image, std::string source)
{
  if (image.isNull())
  {
    return unsupported(std::move(source), "The asset produced an empty image.");
  }
  auto result = RenderedPreview{};
  result.status = "image";
  result.source = std::move(source);
  result.image = std::move(image);
  return result;
}

/** Resolves the stored file of one asset, or explains why it cannot be read. */
Result<std::filesystem::path> resolveAssetFile(
  ProjectSession* project, const AutomationAssetSummary& asset)
{
  if (asset.scope != "game")
  {
    if (project == nullptr)
    {
      return Error{"The current document has no owning project."};
    }
    if (asset.scope == "project")
    {
      const auto stored = project->store().findAsset(asset.id);
      if (stored.is_error())
      {
        return std::get<Error>(stored.error());
      }
      if (!stored.value())
      {
        return Error{"The project no longer contains this asset."};
      }
      return project->assetFile(*stored.value());
    }
    return project->libraryAssetFile(asset.libraryId, asset.id);
  }
  if (asset.absolutePath.empty())
  {
    return Error{"The game asset has no resolved file on disk."};
  }
  return std::filesystem::path{asset.absolutePath};
}

RenderedPreview renderPreview(
  ProjectSession* project, const AutomationAssetSummary& asset, const int size)
{
  const auto resolvedSize = std::clamp(size, MinPreviewSize, MaxPreviewSize);
  const auto file = resolveAssetFile(project, asset);
  if (file.is_error())
  {
    return missing(asset.scope, std::get<Error>(file.error()).msg);
  }

  if (asset.type == "sound")
  {
    const auto bytes = readFileBytes(file.value());
    if (bytes.is_error())
    {
      return missing("sound", std::get<Error>(bytes.error()).msg);
    }
    const auto wave = inspection::readWaveInfo(bytes.value());
    if (wave.is_error())
    {
      return unsupported("sound", std::get<Error>(wave.error()).msg);
    }
    auto result = RenderedPreview{};
    result.status = "audio";
    result.source = "sound";
    result.path = file.value().generic_string();
    result.message = std::to_string(wave.value().sampleRate) + " Hz, "
                     + std::to_string(wave.value().channels) + " channel(s), "
                     + std::to_string(wave.value().bits) + " bit";
    return result;
  }

  if (asset.type == "map")
  {
    return unsupported("map", "A map has no thumbnail; open it to inspect it.");
  }

  if (asset.type == "prefab")
  {
    return unsupported("prefab", "Prefabs are inspected as scenes, not as images.");
  }

  if (asset.type == "model")
  {
    const auto scene =
      loadModelPreviewScene(file.value().parent_path(), file.value().filename(), true);
    if (!scene || scene->frames.empty())
    {
      return unsupported("model", "Only GoldSrc studio models can be previewed.");
    }
    return imagePreview(
      fitPreview(renderModelPreview(scene->frames.front(), resolvedSize), resolvedSize),
      "model");
  }

  if (asset.type == "sprite")
  {
    const auto bytes = readFileBytes(file.value());
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
    return imagePreview(fitPreview(image, resolvedSize), "sprite");
  }

  if (asset.nativeFormat == "skybox")
  {
    return unsupported(
      "skybox",
      "A skybox needs all six faces; compare them in the asset library instead.");
  }

  const auto bytes = readFileBytes(file.value());
  if (bytes.is_error())
  {
    return missing(asset.type, std::get<Error>(bytes.error()).msg);
  }
  const auto decoded = inspection::decodeImage(bytes.value(), asset.nativeFormat);
  if (decoded.is_error())
  {
    return unsupported(asset.type, std::get<Error>(decoded.error()).msg);
  }
  return imagePreview(fitPreview(decoded.value(), resolvedSize), asset.type);
}

QString cellLabel(const AutomationAssetSummary& asset, const std::size_t index)
{
  return QStringLiteral("#%1  %2").arg(index).arg(
    QString::fromStdString(asset.displayName));
}

QString cellSubtitle(const AutomationAssetSummary& asset, const RenderedPreview& preview)
{
  auto subtitle =
    QString::fromStdString(asset.assetType.empty() ? asset.type : asset.assetType);
  if (preview.status == "image")
  {
    subtitle +=
      QStringLiteral(" · %1×%2").arg(preview.image.width()).arg(preview.image.height());
  }
  else if (!preview.message.empty())
  {
    subtitle += QStringLiteral(" · ") + QString::fromStdString(preview.message);
  }
  return subtitle;
}

void drawCell(
  QPainter& painter,
  const QRect& box,
  const AutomationAssetSummary& asset,
  const RenderedPreview& preview,
  const std::size_t index)
{
  painter.fillRect(box, QColor{44, 44, 44});

  if (preview.status == "image" && !preview.image.isNull())
  {
    const auto& image = preview.image;
    const auto x = box.x() + (box.width() - image.width()) / 2;
    const auto y = box.y() + (box.height() - image.height()) / 2;
    painter.drawImage(QPoint{x, y}, image);
  }
  else
  {
    painter.setPen(QColor{150, 150, 150});
    auto status = QString::fromStdString(preview.status);
    if (!preview.message.empty())
    {
      status += QStringLiteral("/") + QString::fromStdString(preview.message);
    }
    painter.drawText(
      box.adjusted(6, 6, -6, -6), Qt::AlignCenter | Qt::TextWordWrap, status);
  }

  auto labelFont = QFont{};
  labelFont.setPixelSize(11);
  labelFont.setBold(true);
  painter.setFont(labelFont);
  painter.setPen(QColor{235, 235, 235});
  const auto labelRect = QRect{box.x(), box.bottom() + 2, box.width(), 14};
  painter.drawText(
    labelRect,
    Qt::AlignLeft | Qt::AlignVCenter,
    QFontMetrics{labelFont}.elidedText(
      cellLabel(asset, index), Qt::ElideMiddle, box.width()));

  auto subtitleFont = QFont{};
  subtitleFont.setPixelSize(10);
  painter.setFont(subtitleFont);
  painter.setPen(QColor{160, 160, 160});
  const auto subtitleRect = QRect{box.x(), box.bottom() + 15, box.width(), 14};
  painter.drawText(
    subtitleRect,
    Qt::AlignLeft | Qt::AlignVCenter,
    QFontMetrics{subtitleFont}.elidedText(
      cellSubtitle(asset, preview), Qt::ElideRight, box.width()));
}

} // namespace

AutomationAssetPreview previewAutomationAsset(
  ProjectSession* project,
  const AutomationAssetSummary& asset,
  const int size,
  const std::filesystem::path& outputFolder)
{
  const auto resolvedSize = std::clamp(size, MinPreviewSize, MaxPreviewSize);
  const auto rendered = renderPreview(project, asset, resolvedSize);

  auto result = AutomationAssetPreview{};
  result.status = rendered.status;
  result.source = rendered.source;
  result.message = rendered.message;
  if (rendered.status != "image")
  {
    result.path = rendered.path;
    return result;
  }

  const auto key = asset.scope + "|" + asset.id + "|" + std::to_string(resolvedSize);
  const auto written = writePng(rendered.image, outputFolder, key);
  if (written.is_error())
  {
    result.status = "missing";
    result.message = std::get<Error>(written.error()).msg;
    return result;
  }
  result.path = written.value().generic_string();
  result.width = rendered.image.width();
  result.height = rendered.image.height();
  return result;
}

AutomationAssetSheetSet renderAutomationAssetSheets(
  ProjectSession* project,
  const std::vector<AutomationAssetSummary>& assets,
  const int size,
  const int columns,
  const int rows,
  const std::filesystem::path& outputFolder)
{
  const auto resolvedSize = std::clamp(size, MinPreviewSize, MaxPreviewSize);
  const auto resolvedColumns = std::clamp(columns, MinColumns, MaxColumns);
  const auto resolvedRows = std::clamp(rows, MinRows, MaxRows);
  const auto capacity = std::size_t(resolvedColumns) * std::size_t(resolvedRows);

  auto result = AutomationAssetSheetSet{};
  result.count = assets.size();
  result.columns = resolvedColumns;
  result.rows = resolvedRows;

  const auto sheetCount =
    std::max<std::size_t>(1u, (assets.size() + capacity - 1u) / capacity);

  for (std::size_t first = 0; first < assets.size(); first += capacity)
  {
    const auto last = std::min(assets.size(), first + capacity);
    const auto sheetRows = int(
      (last - first + std::size_t(resolvedColumns) - 1) / std::size_t(resolvedColumns));
    const auto sheetWidth =
      SheetPadding + resolvedColumns * (resolvedSize + SheetPadding);
    const auto sheetHeight =
      SheetCaptionHeight + SheetPadding
      + sheetRows * (resolvedSize + SheetLabelHeight + SheetPadding);

    auto image = QImage{sheetWidth, sheetHeight, QImage::Format_RGB32};
    image.fill(QColor{30, 30, 30});

    auto painter = QPainter{&image};
    auto captionFont = QFont{};
    captionFont.setPixelSize(12);
    captionFont.setBold(true);
    painter.setFont(captionFont);
    painter.setPen(QColor{235, 235, 235});
    painter.drawText(
      QRect{SheetPadding, 4, sheetWidth - 2 * SheetPadding, SheetCaptionHeight - 8},
      Qt::AlignLeft | Qt::AlignVCenter,
      QStringLiteral("assets %1-%2 of %3 · sheet %4/%5")
        .arg(first + 1)
        .arg(last)
        .arg(assets.size())
        .arg(result.sheets.size() + 1u)
        .arg(sheetCount));

    auto sheet = AutomationAssetSheet{};
    sheet.firstIndex = first;
    for (auto index = first; index < last; ++index)
    {
      const auto slot = index - first;
      const auto column = int(slot) % resolvedColumns;
      const auto row = int(slot) / resolvedColumns;
      const auto x = SheetPadding + column * (resolvedSize + SheetPadding);
      const auto y = SheetCaptionHeight + SheetPadding
                     + row * (resolvedSize + SheetLabelHeight + SheetPadding);
      const auto box = QRect{x, y, resolvedSize, resolvedSize};

      const auto& asset = assets[index];
      const auto preview = renderPreview(project, asset, resolvedSize);
      drawCell(painter, box, asset, preview, index);

      auto cell = AutomationAssetSheetCell{};
      cell.index = index;
      cell.scope = asset.scope;
      cell.id = asset.id;
      cell.displayName = asset.displayName;
      cell.type = asset.type;
      cell.status = preview.status;
      cell.message = preview.message;
      cell.x = x;
      cell.y = y;
      cell.width = resolvedSize;
      cell.height = resolvedSize;
      sheet.cells.push_back(std::move(cell));
    }
    painter.end();

    const auto key = "sheet|" + std::to_string(first) + "|"
                     + std::to_string(resolvedColumns) + "x"
                     + std::to_string(resolvedRows) + "|" + std::to_string(resolvedSize);
    const auto written = writePng(image, outputFolder, key);
    sheet.width = sheetWidth;
    sheet.height = sheetHeight;
    sheet.path = written.is_error() ? std::get<Error>(written.error()).msg
                                    : written.value().generic_string();
    result.sheets.push_back(std::move(sheet));
    if (written.is_error())
    {
      break;
    }
  }

  return result;
}

} // namespace tb::ui
