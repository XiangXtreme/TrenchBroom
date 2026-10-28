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
 * A model is never usable alone: its skins are separate texture files, and multiplayer
 * player models ship a `<name>T.mdl` companion. Capturing a model without that closure
 * produces a project that opens but renders untextured, which is exactly the failure this
 * information prevents.
 */
struct ModelDependencies
{
  // Skin texture names as stored in the model header, without directory resolution.
  std::vector<std::string> skins;
};

/**
 * Candidate multiplayer companion model name for a model file name.
 *
 * GoldSrc player models ship as `name.mdl` plus `nameT.mdl`. The candidate is only a
 * lookup hint: when no such file exists in the library, it produces no dependency.
 * Returns an empty string when the name offers no candidate.
 */
std::string modelCompanionName(const std::string& modelFileName);

/**
 * Reads the dependency hints of a GoldSrc studio model.
 *
 * Fails when the payload is not a version 10 MDL. Damaged optional tables are tolerated
 * (the model still identifies itself), but a truncated header is an error.
 */
Result<ModelDependencies> readGoldSrcModelDependencies(
  const std::vector<std::uint8_t>& bytes);

/** Normalized lookup key used to match dependency names against library assets. */
std::string modelDependencyKey(const std::string& name);

} // namespace tb::project
