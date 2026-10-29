/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom.
*/

#pragma once

#include "base/Result.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tb::project
{

/**
 * Dependencies a GoldSrc studio model declares about itself.
 *
 * A GoldSrc model embeds its textures: the texture table is followed by the miptex data
 * inside the same file, so a texture name is never an external image. What genuinely
 * lives outside the model are the separate texture file (`<name>T.mdl` for player models)
 * and the external animation groups (`<name>01.mdl`, `<name>02.mdl`, ...), which the
 * engine loads next to the base model.
 */
struct ModelDependencies
{
  // Values above one mean the model loads `<name>01.mdl` .. `<name>NN.mdl`; group 0 is
  // the base model itself.
  std::uint32_t sequenceGroups = 1;
};

/**
 * Reads the dependency hints of a GoldSrc studio model.
 *
 * Fails when the payload is not a version 10 MDL. Only the header field that matters for
 * dependency capture is read, so a model with damaged optional tables still identifies
 * its external groups.
 */
Result<ModelDependencies> readGoldSrcModelDependencies(
  const std::vector<std::uint8_t>& bytes);

/**
 * Candidate animation group file names, derived from the base model name.
 *
 * The engine derives these names from the model path rather than trusting the group
 * table, so a missing candidate simply produces no dependency.
 */
std::vector<std::string> modelAnimationGroupNames(
  const std::string& modelPath, std::uint32_t sequenceGroups);

/**
 * Candidate separate texture file name for a model.
 *
 * GoldSrc player models ship as `name.mdl` plus `nameT.mdl`; the candidate is a lookup
 * hint that produces no dependency when the file does not exist in the library.
 * Returns an empty string when the name offers no candidate.
 */
std::string modelCompanionName(const std::string& modelFileName);

/** Normalized lookup key used to match dependency names against library assets. */
std::string modelDependencyKey(const std::string& name);

} // namespace tb::project
