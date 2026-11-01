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
 * The renderer only previews the bind pose, so sequences are reported as a count and
 * never presented as playable. Textures are embedded in the model and only listed for
 * diagnostics.
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

/** Bind-pose geometry of one variant per body part, in model space. */
struct GoldSrcStudioMesh
{
  std::vector<vm::vec3f> positions;
  std::vector<std::array<std::size_t, 3>> triangles;
};

/**
 * Builds the geometry of the given variant of every body part at one animation frame.
 *
 * The pose is decoded from the requested sequence, which is how the engine renders a
 * model; frame 0 of the first sequence is therefore the bind pose. Models without
 * sequences fall back to the bone rest values. Bone controllers and skin textures are not
 * applied, so the geometry is a readable silhouette for the asset browser.
 */
Result<GoldSrcStudioMesh> buildGoldSrcStudioMesh(
  const std::vector<std::uint8_t>& bytes,
  const std::vector<std::size_t>& variants = {},
  std::size_t sequence = 0,
  std::size_t frame = 0);

} // namespace tb::mdl
