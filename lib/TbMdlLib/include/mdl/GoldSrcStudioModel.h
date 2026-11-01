/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom, licensed under GPL version 3 or later.
*/

#pragma once

#include "base/Result.h"

#include "vm/bbox.h"
#include "vm/vec.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace tb::mdl
{

/** One authored sequence; frame 0 of the first sequence is the model's bind pose. */
struct GoldSrcStudioSequence
{
  std::string label;
  float fps = 0.0f;
  bool looping = false;
  std::size_t frameCount = 0;
};

/** One body part of a GoldSrc studio model and its selectable variants. */
struct GoldSrcStudioBodyPart
{
  std::string name;
  // Model names of the variants; index 0 is the model the engine shows by default.
  std::vector<std::string> variants;
};

/**
 * Metadata of a GoldSrc studio model (version 10) that can be read without animation
 * data.
 *
 * Includes sequence metadata, embedded texture names and body variant names.
 */
struct GoldSrcStudioModelInfo
{
  std::string name;
  vm::bbox3f bounds;
  std::size_t bones = 0;
  std::size_t sequenceGroups = 1;
  std::size_t sequences = 0;
  std::vector<GoldSrcStudioSequence> sequenceList;
  std::size_t skinFamilies = 0;
  std::size_t skinReferences = 0;
  std::vector<std::string> textures;
  std::vector<GoldSrcStudioBodyPart> bodyParts;
};

bool isGoldSrcStudioModel(const std::vector<std::uint8_t>& bytes);

Result<GoldSrcStudioModelInfo> readGoldSrcStudioModelInfo(
  const std::vector<std::uint8_t>& bytes);

/** One embedded texture, decoded from the indexed pixels and its 256 colour palette. */
struct GoldSrcStudioTexture
{
  std::size_t width = 0;
  std::size_t height = 0;
  // Row-major 0xAARRGGBB pixels.
  std::vector<std::uint32_t> pixels;
};

/** Geometry of one variant per body part at one animation frame, in model space. */
struct GoldSrcStudioMesh
{
  std::vector<vm::vec3f> positions;
  // Normalized texture coordinates parallel to positions.
  std::vector<std::array<float, 2>> texCoords;
  std::vector<std::array<std::size_t, 3>> triangles;
  // Index into textures, parallel to triangles.
  std::vector<std::size_t> triangleTextures;
  std::vector<GoldSrcStudioTexture> textures;
};

/**
 * Builds the geometry of the given variant of every body part at one animation frame.
 *
 * The pose is decoded from the requested sequence, which is how the engine renders a
 * model; frame 0 of the first sequence is therefore the bind pose. Models without
 * sequences fall back to the bone rest values. Uses the default skin family; bone
 * controllers and external sequence groups are not applied. When decodeTexturePixels
 * is false, only texture dimensions and normalized UVs are read so callers can reuse
 * pixels decoded for an earlier frame of the same model.
 */
Result<GoldSrcStudioMesh> buildGoldSrcStudioMesh(
  const std::vector<std::uint8_t>& bytes,
  const std::vector<std::size_t>& variants = {},
  std::size_t sequence = 0,
  std::size_t frame = 0,
  bool decodeTexturePixels = true);

} // namespace tb::mdl
