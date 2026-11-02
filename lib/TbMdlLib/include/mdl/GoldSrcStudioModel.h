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
  // 0 is embedded in the main file; larger values need the matching sequence group.
  std::size_t group = 0;
};

/**
 * Optional sibling files of a GoldSrc studio model.
 *
 * GoldSrc splits a model when its textures do not fit: the main file then has
 * textureindex 0 and the texture and skin tables live in <stem>T.mdl. Animation
 * groups beyond the embedded group live in <stem>01.mdl, <stem>02.mdl, ... and
 * their sequence descriptors point at them by index.
 */
struct GoldSrcStudioExternalData
{
  const std::vector<std::uint8_t>* textureHeader = nullptr;
  // Index 0 holds sequence group 1, index 1 group 2, and so on.
  const std::vector<std::vector<std::uint8_t>>* sequenceGroups = nullptr;
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
  // True when the texture and skin tables were read from a separate header.
  bool externalTextures = false;
  // File names from the sequence-group table (group 0 is the main file and empty).
  std::vector<std::string> sequenceGroupFiles;
  std::vector<GoldSrcStudioSequence> sequenceList;
  std::size_t skinFamilies = 0;
  std::size_t skinReferences = 0;
  std::vector<std::string> textures;
  std::vector<GoldSrcStudioBodyPart> bodyParts;
};

bool isGoldSrcStudioModel(const std::vector<std::uint8_t>& bytes);

Result<GoldSrcStudioModelInfo> readGoldSrcStudioModelInfo(
  const std::vector<std::uint8_t>& bytes,
  const GoldSrcStudioExternalData& external = {});

/** One embedded texture, decoded from the indexed pixels and its 256 colour palette. */
struct GoldSrcStudioTexture
{
  std::size_t width = 0;
  std::size_t height = 0;
  // GoldSrc texture flags (STUDIO_NF_*); bit 0x40 marks a masked texture.
  std::uint32_t flags = 0;
  // Row-major 0xAARRGGBB pixels; a masked texture's palette entry 255 is transparent.
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
 * sequences fall back to the bone rest values. Bone controllers are not applied. When
 * decodeTexturePixels is false, only texture dimensions and normalized UVs are read so
 * callers can reuse pixels decoded for an earlier frame of the same model.
 */
Result<GoldSrcStudioMesh> buildGoldSrcStudioMesh(
  const std::vector<std::uint8_t>& bytes,
  const std::vector<std::size_t>& variants = {},
  std::size_t sequence = 0,
  std::size_t frame = 0,
  bool decodeTexturePixels = true,
  const GoldSrcStudioExternalData& external = {},
  std::size_t skinFamily = 0);

} // namespace tb::mdl
