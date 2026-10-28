/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/ProjectTextureImport.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QImage>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QVBoxLayout>

#include "img/DecodeImage.h"
#include "img/GoldSrcTexture.h"
#include "ui/QPathUtils.h"

namespace tb::ui
{
Result<bool> prepareProjectTexture(
  QWidget* parent,
  project::AssetImport& asset,
  const std::optional<project::Asset>& previous)
{
  if (
    asset.type != project::AssetType::Texture
    || QString::fromStdString(asset.sourceName).endsWith(".wad", Qt::CaseInsensitive))
    return true;
  auto decoded = img::decodeImage(asset.original.data(), asset.original.size());
  if (decoded.is_error())
    return std::get<Error>(decoded.error());
  const auto& image = decoded.value();
  auto name = QString{};
  if (previous)
    name = pathAsQString(std::filesystem::path{previous->publishPath}.stem());
  else
  {
    for (const auto c : QString::fromStdString(asset.displayName))
      name += (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z')
                  || (c >= u'0' && c <= u'9') || c == u'_' || c == u'-'
                ? c
                : QChar{u'_'};
    if (name.isEmpty())
      name = "texture";
    if (image.alphaDomain != img::ImageAlphaDomain::Opaque)
      name.prepend('{');
    name.truncate(15);
    bool accepted = false;
    name = QInputDialog::getText(
      parent,
      QObject::tr("Import Texture"),
      QObject::tr(
        "GoldSrc texture name (1–15 ASCII characters; { prefix for transparency):"),
      QLineEdit::Normal,
      name,
      &accepted);
    if (!accepted)
      return false;
  }
  if (
    previous
    && !QString::fromStdString(previous->conversionRecipe).contains("goldsrc-texture"))
    return Error{
      "An image cannot replace a WAD collection. Reimport the collection as a WAD, or "
      "import the image as a new texture."};
  auto converted = img::convertGoldSrcTexture(image, name.toStdString());
  if (converted.is_error())
    return std::get<Error>(converted.error());
  const auto& result = converted.value();
  auto dialog = QDialog{parent};
  dialog.setWindowTitle(QObject::tr("Confirm Texture Conversion"));
  auto* layout = new QVBoxLayout{&dialog};
  auto* description = new QLabel{
    QObject::tr("%1 × %2 → %3 × %4\n256-color GoldSrc texture, four mip levels.\n%5\nThe "
                "original image is retained in the project.")
      .arg(image.width)
      .arg(image.height)
      .arg(result.preview.width)
      .arg(result.preview.height)
      .arg(
        result.masked ? QObject::tr("Transparency uses a binary mask at alpha 128.")
                      : QObject::tr("Opaque texture; alpha is discarded.")),
    &dialog};
  layout->addWidget(description);
  auto* preview = new QLabel{&dialog};
  const auto pixels = QImage{
    result.preview.pixels.data(),
    int(result.preview.width),
    int(result.preview.height),
    int(result.preview.width * 4),
    QImage::Format_RGBA8888};
  preview->setPixmap(QPixmap::fromImage(pixels).scaled(
    384, 384, Qt::KeepAspectRatio, Qt::FastTransformation));
  preview->setAlignment(Qt::AlignCenter);
  layout->addWidget(preview);
  auto* buttons =
    new QDialogButtonBox{QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog};
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  if (dialog.exec() != QDialog::Accepted)
    return false;
  asset.virtualPath = "textures/" + result.name + ".wad";
  asset.compatible = result.wad;
  asset.conversionRecipe = QJsonDocument{QJsonObject{
                                           {"importer", "goldsrc-texture"},
                                           {"version", 1},
                                           {"name", name},
                                           {"width", int(result.preview.width)},
                                           {"height", int(result.preview.height)},
                                           {"masked", result.masked},
                                           {"alphaThreshold", 128},
                                           {"formatValidated", true}}}
                             .toJson(QJsonDocument::Compact)
                             .toStdString();
  return true;
}
} // namespace tb::ui
