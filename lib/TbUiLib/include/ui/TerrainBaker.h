/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <QPolygonF>

#include "base/Result.h"
#include "project/GoldSrcWad.h"

#include "vm/vec.h"

#include <cstddef>
#include <string>
#include <vector>

namespace tb::ui
{
struct TerrainRegion;
struct TerrainMaterial;

enum class TerrainFaceKind
{
  Base,
  Shared,
  Local,
};

struct BakeFace
{
  QPolygonF polygon;
  vm::vec3d u;
  vm::vec3d v;
  size_t material = 0;
  TerrainFaceKind kind = TerrainFaceKind::Base;
};

struct TerrainBake
{
  std::vector<BakeFace> faces;
  std::vector<project::WadTextureEntry> textures;
};

// Controls sharing for custom materials; built-in terrain generation is unchanged.
enum class TerrainBakeMode
{
  Local,
  Shared,
};

Result<TerrainBake> runTerrainBaker(
  const TerrainRegion& region,
  const TerrainMaterial* material = nullptr,
  TerrainBakeMode mode = TerrainBakeMode::Shared);

} // namespace tb::ui
