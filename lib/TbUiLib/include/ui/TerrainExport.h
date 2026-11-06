/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include "base/Result.h"
#include "ui/TerrainBaker.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace tb::project
{
class ProjectStore;
}
namespace tb::mdl
{
class GroupNode;
}

namespace tb::ui
{
// Bump when generation changes so saved results are refreshed for unchanged maps.
inline constexpr std::uint64_t TerrainGeneratorVersion = 6;

struct TerrainSnapshot
{
  std::vector<std::uint8_t> map;
  std::map<std::string, std::vector<std::uint8_t>> textures;
  size_t topFaces = 0;
};

struct TerrainEstimate
{
  struct Cut
  {
    size_t surfaceIndex = 0;
    QPolygonF polygon;
    TerrainFaceKind kind = TerrainFaceKind::Base;
    bool belowMinWidth = false;
  };

  size_t sourceFaces = 0;
  size_t generatedFaces = 0;
  size_t textures = 0;
  size_t wadBytes = 0;
  double narrowestFaceWidth = 0;
  double narrowestFaceX = 0;
  double narrowestFaceY = 0;
  size_t facesBelowMinWidth = 0;
  std::vector<Cut> cuts;
};

/** Calculate the generated faces and terrain-only WAD size for a selected group. */
Result<TerrainEstimate> estimateTerrainGroup(
  const mdl::GroupNode& group, const project::ProjectStore* store = nullptr);

/** Bakes a disposable compile copy of a serialized project map. */
Result<std::optional<TerrainSnapshot>> bakeTerrainSnapshot(
  const std::vector<std::uint8_t>& source,
  const project::ProjectStore* store = nullptr,
  std::vector<std::string>* dependencies = nullptr);
} // namespace tb::ui
