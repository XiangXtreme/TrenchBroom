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
#include "ui/PrefabAsset.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
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

/** Sound browser filter: every waveform, only looping ones, or only one-shot cues. */
enum class WaveKind
{
  All,
  Looping,
  OneShot,
};

bool matchesWaveKind(const WaveInfo& info, WaveKind kind);

/** Optional GoldSrc model bytes resolved from project or library dependencies. */
struct StudioExternalBytes
{
  Bytes textureHeader;
  std::vector<Bytes> sequenceGroups;
};

/** A read-only selection snapshot. Each host owns one; no global selection or cache. */
struct Asset
{
  BrowserAsset resource;
  QString name;
  QString source;
  QString recipe;
  std::string nativeFormat;
  QString identity;
  Bytes original;
  Bytes compatible;
  // Same face order as SkyRenderer and Sky.fragsh: rt, bk, lf, ft, up, dn.
  std::array<QImage, 6> skyOriginal;
  std::array<QImage, 6> skyCompatible;
  QStringList diagnostics;
  // GoldSrc model siblings captured as separate project or library assets.
  StudioExternalBytes studioExternal;
  // Set for prefabs: authored volumes plus explicit resource references.
  std::optional<PrefabContents> prefab;
  // Reference problems, or a note that they could not be checked without a project.
  QStringList prefabIssues;
};

QImage renderSkyCross(const std::array<QImage, 6>& faces, int side = 160);
/** Shared compatible skybox thumbnail for the library and content browser. */
QImage skyThumbnail(const Asset& asset, int side = 256);
QImage renderSkyView(
  const std::array<QImage, 6>& faces,
  double yaw,
  double pitch,
  int width = 512,
  int height = 320);

/** GoldSrc palettes contain RGB triples. Masked miptex index 255 is transparent. */
Result<QImage> decodeImage(
  std::span<const std::uint8_t> bytes, std::string_view nativeFormat = {});
Result<Asset> readLibraryAsset(
  const AssetLibrarySource& source,
  const project::LibraryAsset& asset,
  ProjectSession* project = nullptr);
Result<Asset> readBrowserAsset(
  const BrowserAsset& asset, ProjectSession* project, MapDocument& document);
/** Reads one project asset, including decoded sky faces and model dependencies. */
Result<Asset> readProjectAsset(ProjectSession& project, const std::string& assetId);
} // namespace inspection
} // namespace tb::ui
