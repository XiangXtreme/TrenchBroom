/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include "base/Result.h"
#include "ui/TerrainRegion.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace tb::mdl
{
class GroupNode;
}

namespace tb::ui
{
/** A region stored as children of one logical terrain group. */
struct TerrainChunk
{
  size_t childIndex;
  int column;
  int row;
  TerrainRegion region;
};

struct TerrainChunks
{
  std::unique_ptr<TerrainRegion> combined;
  std::vector<TerrainChunk> chunks;
};

/** The marker identifies the group as a container, not as a paint region. */
bool isTerrainChunkGroup(const mdl::GroupNode& group);
const char* terrainChunkGroupMarker();

/** Read and validate all chunks, including their shared height samples. */
Result<TerrainChunks> readTerrainChunks(const mdl::GroupNode& group);
/** Read either a single region or the combined logical terrain. */
Result<TerrainRegion> readTerrainGroup(const mdl::GroupNode& group);
/** Surface outlines are expressed relative to the combined group's origin. */
Result<std::vector<TerrainSurface>> terrainGroupSurfaces(
  const mdl::GroupNode& group, const TerrainRegion& region);
/** A cache identity that changes when any child region changes. */
std::string terrainGroupRevision(const mdl::GroupNode& group);
} // namespace tb::ui
