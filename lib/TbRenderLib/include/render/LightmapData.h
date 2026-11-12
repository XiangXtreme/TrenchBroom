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

#pragma once

#include "vm/plane.h"
#include "vm/vec.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace tb
{
namespace mdl
{
class BrushFace;
class Map;
} // namespace mdl

namespace render
{

/**
 * One face of a baked lightmap atlas: its plane, its lightmap axes, the luxel rectangle
 * it occupies and where those pixels live in the atlas.
 */
struct LightmapFaceRecord
{
  vm::vec3f normal;
  float dist = 0.0f;
  vm::vec4f vecs[2] = {};
  vm::vec2i mins = {};
  vm::vec2i size = {};
  vm::vec2i tile = {};
  std::vector<vm::vec2f> poly; // face polygon in luxel units
};

/**
 * The result of a bake: one atlas plus the record of every lit face in it.
 */
struct LightmapBake
{
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> pixels; // rgb8, rows top down
  std::vector<LightmapFaceRecord> faces;
};

/**
 * The lightmap rectangle a single brush face samples from the assembled atlas.
 */
struct LightmapBlock
{
  vm::vec2i mins = {}; // first luxel, in the face's lightmap axes
  vm::vec2i size = {};
  vm::vec2i tile = {};    // pixel origin in the assembled atlas
  vm::vec4f vecs[2] = {}; // the lightmap axes the block was sampled with
};

/**
 * The lightmap rectangle a terrain piece samples from the assembled atlas.
 */
struct TerrainLightmapBlock
{
  vm::vec3f normal;
  float dist = 0.0f;
  vm::vec4f vecs[2] = {};
  vm::vec2i mins = {};
  vm::vec2i size = {};
  vm::vec2i tile = {};
  std::vector<vm::vec2f> poly;
};

/** A convex portion of a terrain face and its corresponding baked lightmap, if any. */
struct TerrainLightmapFragment
{
  std::vector<vm::vec3d> vertices;
  const TerrainLightmapBlock* block = nullptr;
};

/**
 * A lightmap atlas holding one rectangle per brush face and terrain piece.
 */
struct LightmapAssembly
{
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> pixels; // rgb8, rows top down
  // Sorted by face address for lookup during vertex construction.
  std::vector<std::pair<const mdl::BrushFace*, LightmapBlock>> blocks;
  std::vector<TerrainLightmapBlock> terrainBlocks;

  const LightmapBlock* blockFor(const mdl::BrushFace& face) const;

  /** Selects blocks that could overlap the given face, for reuse by its fragments. */
  std::vector<const TerrainLightmapBlock*> terrainBlocksFor(
    const vm::plane3d& plane, const std::vector<vm::vec3d>& vertices) const;

  /** Splits a convex face along baked face boundaries; uncovered fragments stay unlit. */
  std::vector<TerrainLightmapFragment> terrainFragments(
    const vm::plane3d& plane, const std::vector<vm::vec3d>& vertices) const;
  std::vector<TerrainLightmapFragment> terrainFragments(
    const std::vector<const TerrainLightmapBlock*>& candidates,
    const std::vector<vm::vec3d>& vertices) const;

  /** Returns an atlas coordinate for a fragment, using the white texel if unmatched. */
  vm::vec2f terrainUv(const TerrainLightmapBlock* block, const vm::vec3d& point) const;
};

/**
 * A face that needs its own lightmap rectangle: its plane and its outline in world space.
 * The optional brush face is only carried through to the assembly so callers can find the
 * rectangle again.
 */
struct FaceLightmapInput
{
  vm::plane3d plane;
  std::vector<vm::vec3f> outline;
  const mdl::BrushFace* face = nullptr;
  std::string materialName;
  std::string entitySignature;
  // Exact editor geometry/UV snapshot; never dereference a saved face pointer.
  std::vector<double> signature;
};

std::vector<FaceLightmapInput> collectLightmapFaces(const mdl::Map& map);
std::vector<FaceLightmapInput> matchingLightmapFaces(
  const std::vector<FaceLightmapInput>& snapshot, const mdl::Map& map);

/**
 * Samples one face's lightmap rectangle out of the baked faces that cover it.
 *
 * Returns nothing when no baked face covers the face. The pixels come back in rgb8 with
 * the rectangle's own origin, and luxels that no baked face covers are filled from their
 * neighbours so that linear filtering never picks up the background.
 */
std::optional<LightmapBlock> buildFaceLightmap(
  const FaceLightmapInput& face,
  const LightmapBake& bake,
  std::vector<std::uint8_t>& pixels);

/**
 * Gives every face its own lightmap rectangle and packs those rectangles into one atlas.
 *
 * The bake splits surfaces more finely than brush faces do, and its rectangles are
 * scattered over one image with unrelated origins, so a face cannot be sampled straight
 * out of it. Resampling here leaves exactly one rectangle per face, which is what lets
 * the renderer compute one lightmap coordinate per vertex.
 */
void buildLightmapAssembly(
  const std::vector<FaceLightmapInput>& faces,
  const LightmapBake& bake,
  LightmapAssembly& result);

/**
 * Collects brush faces from exportable layers and builds an assembly for them.
 */
void buildLightmapAssembly(
  const mdl::Map& map, const LightmapBake& bake, LightmapAssembly& result);

} // namespace render
} // namespace tb
