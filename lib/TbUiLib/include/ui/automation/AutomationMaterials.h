/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.
 */

#pragma once

#include "mdl/BrushFaceHandle.h"

#include <optional>
#include <string>
#include <vector>

namespace tb::mdl
{
class Map;
} // namespace tb::mdl

namespace tb::ui::automation
{

enum class AutomationFaceAlignment
{
  Reset,
  Paraxial,
  Parallel,
};

std::optional<AutomationFaceAlignment> automationFaceAlignmentFromString(
  const std::string& mode);

/**
 * Applies an axis alignment to faces while preserving the caller's transaction
 * ownership. The operation selects the supplied faces; callers that need to
 * preserve a prior selection restore it before committing their transaction.
 */
bool alignBrushFaceAxes(
  mdl::Map& map,
  const std::vector<mdl::BrushFaceHandle>& faces,
  AutomationFaceAlignment alignment);

bool copyBrushFaceAttributes(
  mdl::Map& map,
  const mdl::BrushFaceHandle& source,
  const std::vector<mdl::BrushFaceHandle>& targets);

bool setBrushFaceMaterial(
  mdl::Map& map,
  const std::vector<mdl::BrushFaceHandle>& faces,
  const std::string& material);

} // namespace tb::ui::automation
