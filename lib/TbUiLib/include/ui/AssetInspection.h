/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <QImage>
#include <QString>
#include <QStringList>

#include "base/Result.h"
#include "project/ProjectAsset.h"
#include "ui/AssetBrowserModel.h"

#include <array>
#include <cstdint>
#include <vector>

namespace tb::ui
{
class AssetLibrarySource;
class MapDocument;
class ProjectSession;
namespace inspection
{
using Bytes = std::vector<std::uint8_t>;

struct WaveInfo
{
  std::uint32_t sampleRate = 0;
  std::uint16_t channels = 0;
  std::uint16_t bits = 0;
  double seconds = 0;
  std::optional<std::pair<std::uint32_t, std::uint32_t>> loop;
};
Result<WaveInfo> readWaveInfo(const Bytes& bytes);

/** A read-only selection snapshot. Each host owns one; no global selection or cache. */
struct Asset
{
  BrowserAsset resource;
  QString name;
  QString source;
  QString recipe;
  QString identity;
  Bytes original;
  Bytes compatible;
  // Same face order as SkyRenderer and Sky.fragsh: rt, bk, lf, ft, up, dn.
  std::array<QImage, 6> skyOriginal;
  std::array<QImage, 6> skyCompatible;
  QStringList diagnostics;
};

QImage renderSkyCross(const std::array<QImage, 6>& faces, int side = 160);
QImage renderSkyView(
  const std::array<QImage, 6>& faces,
  double yaw,
  double pitch,
  int width = 512,
  int height = 320);

/** GoldSrc palettes contain RGB triples. Masked miptex index 255 is transparent. */
Result<QImage> decodeImage(const Bytes& bytes);
Result<Asset> readLibraryAsset(
  const AssetLibrarySource& source, const project::LibraryAsset& asset);
Result<Asset> readBrowserAsset(
  const BrowserAsset& asset, ProjectSession* project, MapDocument& document);
} // namespace inspection
} // namespace tb::ui
