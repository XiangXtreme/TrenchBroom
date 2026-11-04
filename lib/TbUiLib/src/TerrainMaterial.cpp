/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/TerrainMaterial.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>

#include "img/GoldSrcTexture.h"
#include "project/GoldSrcWad.h"
#include "project/ProjectStore.h"
#include "ui/AssetInspection.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>

namespace tb::ui
{
namespace
{
constexpr auto PackageMagic =
  std::array<std::uint8_t, 8>{'T', 'B', 'T', 'M', 'A', 'T', '1', 0};
constexpr auto PackageImageBytes =
  size_t{TerrainMaterial::Period * TerrainMaterial::Period * 4};
constexpr auto PackageDigestBytes = size_t{32};

std::uint32_t readU32(const std::vector<std::uint8_t>& bytes, const size_t at)
{
  return std::uint32_t(bytes[at]) | (std::uint32_t(bytes[at + 1]) << 8)
         | (std::uint32_t(bytes[at + 2]) << 16) | (std::uint32_t(bytes[at + 3]) << 24);
}
} // namespace

Result<std::array<QImage, 3>> loadTerrainMaterialImages(
  const project::ProjectStore& store, const std::string& materialAssetId)
{
  const auto material = store.findAsset(materialAssetId);
  if (material.is_error())
    return std::get<Error>(material.error());
  if (!material.value() || material.value()->type != project::AssetType::Material)
    return Error{"The bound terrain material is missing from the project."};
  const auto source = store.readAsset(materialAssetId, project::ContentKind::Original);
  if (source.is_error())
    return std::get<Error>(source.error());
  const auto json = QJsonDocument::fromJson(QByteArray{
    reinterpret_cast<const char*>(source.value().data()),
    qsizetype(source.value().size())});
  if (
    !json.isObject()
    || json.object().value(QStringLiteral("format")).toString()
         != QStringLiteral("goldsrc-terrain-material")
    || json.object().value(QStringLiteral("version")).toInt(-1) != 1)
    return Error{"The bound asset is not a GoldSrc terrain material."};

  const auto edges = store.dependencies(materialAssetId);
  if (edges.is_error())
    return std::get<Error>(edges.error());
  auto images = std::array<QImage, 3>{};
  auto found = std::array<bool, 3>{};
  for (const auto& edge : edges.value())
  {
    const auto slot = edge.kind == "terrain-slot-0"   ? 0
                      : edge.kind == "terrain-slot-1" ? 1
                      : edge.kind == "terrain-slot-2" ? 2
                                                      : -1;
    if (slot < 0)
      continue;
    if (found[size_t(slot)])
      return Error{fmt::format("Terrain material slot {} has multiple textures.", slot)};
    const auto texture = store.findAsset(edge.dependencyId);
    if (texture.is_error())
      return std::get<Error>(texture.error());
    if (!texture.value() || texture.value()->type != project::AssetType::Texture)
      return Error{fmt::format("Terrain material slot {} texture is missing.", slot)};
    const auto bytes =
      store.readAsset(edge.dependencyId, project::ContentKind::Compatible);
    if (bytes.is_error())
      return std::get<Error>(bytes.error());
    auto image = inspection::decodeImage(bytes.value());
    if (image.is_error())
      return Error{fmt::format(
        "Cannot decode terrain material slot {}: {}",
        slot,
        std::get<Error>(image.error()).msg)};
    images[size_t(slot)] = std::move(image).value();
    found[size_t(slot)] = true;
  }
  if (!std::ranges::all_of(found, [](const auto value) { return value; }))
    return Error{
      "The terrain material does not have all three captured texture dependencies."};
  return images;
}

Result<std::array<QImage, 3>> normalizeTerrainMaterialImages(std::array<QImage, 3> images)
{
  constexpr auto side = TerrainMaterial::Period;
  for (auto slot = size_t{0}; slot < images.size(); ++slot)
  {
    auto& image = images[slot];
    if (
      image.isNull() || image.width() != image.height()
      || (image.width() != 64 && image.width() != 128 && image.width() != 256))
      return Error{fmt::format(
        "Terrain material slot {} must be a square 64, 128, or 256 pixel texture.",
        slot)};
    image = image.convertToFormat(QImage::Format_RGBA8888);
    for (auto y = 0; y < image.height(); ++y)
    {
      const auto* row = image.constScanLine(y);
      for (auto x = 0; x < image.width(); ++x)
        if (row[x * 4 + 3] != 255)
          return Error{fmt::format("Terrain material slot {} must be opaque.", slot)};
    }
    if (image.width() == 64)
    {
      auto tiled = QImage{side, side, QImage::Format_RGBA8888};
      for (auto y = 0; y < side; ++y)
      {
        auto* row = tiled.scanLine(y);
        const auto* source = image.constScanLine(y % 64);
        std::copy_n(source, 64 * 4, row);
        std::copy_n(source, 64 * 4, row + 64 * 4);
      }
      image = std::move(tiled);
    }
    else if (image.width() != side)
      image = image.scaled(side, side, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  }

  return images;
}

QImage renderTerrainMaterialPreview(std::array<QImage, 3> images)
{
  auto normalized = normalizeTerrainMaterialImages(std::move(images));
  if (normalized.is_error())
    return {};
  const auto& sources = normalized.value();
  constexpr auto side = TerrainMaterial::Period;
  auto result = QImage{side, side, QImage::Format_RGBA8888};
  for (auto y = 0; y < side; ++y)
  {
    auto* row = result.scanLine(y);
    for (auto x = 0; x < side; ++x)
    {
      const auto position = double(x) * 2.0 / double(side - 1);
      const auto first = position < 1.0 ? size_t{0} : size_t{1};
      const auto weight = position - double(first);
      const auto* a = sources[first].constScanLine(y) + x * 4;
      const auto* b = sources[first + 1].constScanLine(y) + x * 4;
      for (auto channel = 0; channel < 3; ++channel)
        row[x * 4 + channel] = static_cast<uchar>(
          std::lround(double(a[channel]) * (1.0 - weight) + double(b[channel]) * weight));
      row[x * 4 + 3] = 255;
    }
  }
  return result;
}

Result<TerrainMaterial> prepareTerrainMaterial(std::array<QImage, 3> images)
{
  auto normalized = normalizeTerrainMaterialImages(std::move(images));
  if (normalized.is_error())
    return std::get<Error>(normalized.error());
  images = std::move(normalized).value();
  constexpr auto side = TerrainMaterial::Period;
  // Include base texels, pairwise ramps, and three-way mixtures in the quantizer's
  // training image so one palette can represent both transitions and local patches.
  constexpr auto mosaicSide = side * 3;
  auto mosaic = img::Image{
    mosaicSide,
    mosaicSide,
    std::vector<unsigned char>(size_t(mosaicSide * mosaicSide * 4), 255),
    img::ImageAlphaDomain::Opaque};
  constexpr auto pairs = std::array{std::array{0, 1}, std::array{0, 2}, std::array{1, 2}};
  for (auto y = 0; y < mosaicSide; ++y)
  {
    for (auto x = 0; x < mosaicSide; ++x)
    {
      const auto column = x / side;
      const auto row = y / side;
      const auto sx = x % side;
      const auto sy = y % side;
      auto weights = std::array<double, 3>{};
      const auto u = double(sx) / double(side - 1);
      const auto v = double(sy) / double(side - 1);
      if (row == 0)
        weights[size_t(column)] = 1.0;
      else if (row == 1)
      {
        weights[size_t(pairs[size_t(column)][0])] = 1.0 - u;
        weights[size_t(pairs[size_t(column)][1])] = u;
      }
      else
      {
        weights[size_t(column)] = (1.0 - u) * (1.0 - v);
        weights[size_t((column + 1) % 3)] = u * (1.0 - v);
        weights[size_t((column + 2) % 3)] = v;
      }
      const auto offset = size_t((y * mosaicSide + x) * 4);
      for (auto channel = 0; channel < 3; ++channel)
      {
        auto value = 0.0;
        for (auto slot = size_t{0}; slot < images.size(); ++slot)
          value +=
            weights[slot] * double(images[slot].constScanLine(sy)[sx * 4 + channel]);
        mosaic.pixels[offset + size_t(channel)] =
          static_cast<unsigned char>(std::lround(value));
      }
    }
  }
  const auto converted = img::convertGoldSrcTexture(mosaic, "terrain_palette");
  if (converted.is_error())
    return std::get<Error>(converted.error());
  const auto wad = project::readGoldSrcWad(converted.value().wad);
  if (wad.is_error())
    return std::get<Error>(wad.error());
  if (wad.value().textures.size() != 1 || !wad.value().textures.front().valid)
    return Error{"Cannot construct the terrain material palette."};
  const auto& lump = wad.value().textures.front().miptex;
  const auto width = size_t(readU32(lump, 16));
  const auto height = size_t(readU32(lump, 20));
  const auto paletteOffset = size_t(readU32(lump, 36)) + width * height / 64 + 2;
  if (paletteOffset > lump.size() || 768 > lump.size() - paletteOffset)
    return Error{"The generated terrain palette is invalid."};
  auto result = TerrainMaterial{std::move(images), {}};
  std::copy_n(lump.begin() + std::ptrdiff_t(paletteOffset), 768, result.palette.begin());
  return result;
}

std::vector<std::uint8_t> encodeTerrainMaterialPackage(const TerrainMaterial& material)
{
  auto bytes = std::vector<std::uint8_t>{PackageMagic.begin(), PackageMagic.end()};
  bytes.reserve(
    PackageMagic.size() + 3 * PackageImageBytes + material.palette.size()
    + PackageDigestBytes);
  for (const auto& image : material.images)
  {
    if (
      image.size() != QSize{TerrainMaterial::Period, TerrainMaterial::Period}
      || image.format() != QImage::Format_RGBA8888
      || size_t(image.sizeInBytes()) != PackageImageBytes)
      return {};
    bytes.insert(bytes.end(), image.constBits(), image.constBits() + PackageImageBytes);
  }
  bytes.insert(bytes.end(), material.palette.begin(), material.palette.end());
  const auto digest = QCryptographicHash::hash(
    QByteArrayView{reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size())},
    QCryptographicHash::Sha256);
  bytes.insert(bytes.end(), digest.begin(), digest.end());
  return bytes;
}

std::optional<TerrainMaterial> decodeTerrainMaterialPackage(
  const std::vector<std::uint8_t>& bytes, std::array<QImage, 3> normalizedImages)
{
  constexpr auto expectedSize =
    PackageMagic.size() + 3 * PackageImageBytes + 768 + PackageDigestBytes;
  if (
    bytes.size() != expectedSize
    || !std::equal(PackageMagic.begin(), PackageMagic.end(), bytes.begin()))
    return std::nullopt;
  const auto digest = QCryptographicHash::hash(
    QByteArrayView{
      reinterpret_cast<const char*>(bytes.data()),
      qsizetype(bytes.size() - PackageDigestBytes)},
    QCryptographicHash::Sha256);
  if (
    std::memcmp(
      digest.constData(),
      bytes.data() + bytes.size() - PackageDigestBytes,
      PackageDigestBytes)
    != 0)
    return std::nullopt;
  auto offset = PackageMagic.size();
  for (const auto& image : normalizedImages)
  {
    if (
      image.size() != QSize{TerrainMaterial::Period, TerrainMaterial::Period}
      || image.format() != QImage::Format_RGBA8888
      || size_t(image.sizeInBytes()) != PackageImageBytes
      || std::memcmp(image.constBits(), bytes.data() + offset, PackageImageBytes) != 0)
      return std::nullopt;
    offset += PackageImageBytes;
  }
  auto result = TerrainMaterial{std::move(normalizedImages), {}};
  std::copy_n(
    bytes.begin() + std::ptrdiff_t(offset),
    result.palette.size(),
    result.palette.begin());
  return result;
}

Result<TerrainMaterial> loadTerrainMaterial(
  const project::ProjectStore& store, const std::string& materialAssetId)
{
  auto images = loadTerrainMaterialImages(store, materialAssetId);
  if (images.is_error())
    return std::get<Error>(images.error());
  auto normalized = normalizeTerrainMaterialImages(std::move(images).value());
  if (normalized.is_error())
    return std::get<Error>(normalized.error());
  const auto package = store.readAsset(materialAssetId, project::ContentKind::Compatible);
  if (package.is_success())
    if (auto material = decodeTerrainMaterialPackage(package.value(), normalized.value()))
      return std::move(*material);
  return prepareTerrainMaterial(std::move(normalized).value());
}
} // namespace tb::ui
