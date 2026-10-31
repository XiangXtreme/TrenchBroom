/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/ProjectTextureImport.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QVBoxLayout>

#include "img/DecodeImage.h"
#include "img/GoldSrcTexture.h"
#include "ui/AssetLibraryImport.h"
#include "ui/QPathUtils.h"

namespace tb::ui
{
Result<bool> prepareProjectTexture(
  QWidget* parent,
  project::AssetImport& asset,
  const std::optional<project::Asset>& previous,
  const bool libraryAsset)
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
    name = QString::fromStdString(goldSrcTextureNameFromStem(
      asset.displayName, image.alphaDomain != img::ImageAlphaDomain::Opaque));
    bool accepted = false;
    name = QInputDialog::getText(
      parent,
      libraryAsset ? QObject::tr("Convert Texture") : QObject::tr("Import Texture"),
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
      QObject::tr(
        "An image cannot replace a WAD collection. Reimport the collection as a WAD, or "
        "import the image as a new texture.")
        .toStdString()};
  auto converted = img::convertGoldSrcTexture(image, name.toStdString());
  if (converted.is_error())
    return std::get<Error>(converted.error());
  const auto& result = converted.value();
  auto dialog = QDialog{parent};
  dialog.setWindowTitle(QObject::tr("Confirm Texture Conversion"));
  auto* layout = new QVBoxLayout{&dialog};
  auto* description = new QLabel{
    QObject::tr("%1 × %2 → %3 × %4\n256-color GoldSrc texture, four mip levels.\n%5\n%6")
      .arg(image.width)
      .arg(image.height)
      .arg(result.preview.width)
      .arg(result.preview.height)
      .arg(
        result.masked ? QObject::tr("Transparency uses a binary mask at alpha 128.")
                      : QObject::tr("Opaque texture; alpha is discarded."))
      .arg(
        libraryAsset ? QObject::tr("The original image is retained in the library.")
                     : QObject::tr("The original image is retained in the project.")),
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
  if (!libraryAsset)
    asset.virtualPath = "textures/" + result.name + ".wad";
  asset.compatible = result.wad;
  asset.conversionRecipe = goldSrcTextureRecipe(
    result.name,
    static_cast<std::uint32_t>(result.preview.width),
    static_cast<std::uint32_t>(result.preview.height),
    result.masked);
  return true;
}
} // namespace tb::ui
