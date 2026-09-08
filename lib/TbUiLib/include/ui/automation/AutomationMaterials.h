/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.
 */

#pragma once

#include "mdl/BrushFaceHandle.h"

#include "vm/vec.h"

#include <optional>
#include <string>
#include <vector>

namespace tb::mdl
{
class Map;
} // namespace tb::mdl

namespace tb::gl
{
class Material;
} // namespace tb::gl

namespace tb::ui::automation
{

enum class AutomationFaceAlignment
{
  Reset,
  Paraxial,
  Parallel,
};

struct AutomationFaceFilter
{
  std::string semantic = "all";
  std::optional<vm::vec3d> normal;
  double normalTolerance = 0.75;
};

struct AutomationTextureLocks
{
  bool alignment = false;
  bool uv = false;
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

/**
 * Filters an explicit face collection using the shared top/bottom/side or
 * normal semantics. This does not resolve editor objects or selectors.
 */
std::vector<mdl::BrushFaceHandle> filterBrushFaceHandles(
  std::vector<mdl::BrushFaceHandle> faces,
  const AutomationFaceFilter& filter,
  std::string& error);

/**
 * Returns loaded materials in material-manager order. Searches use the same
 * case-insensitive name and relative-path matching for both automation
 * adapters.
 */
std::vector<const gl::Material*> listMaterials(const mdl::Map& map);
std::vector<const gl::Material*> searchMaterials(
  const mdl::Map& map, const std::string& query, size_t limit);

AutomationTextureLocks textureLocks(const mdl::Map& map);
AutomationTextureLocks setTextureLocks(
  mdl::Map& map, std::optional<bool> alignment, std::optional<bool> uv);
void persistTextureLocks(std::optional<bool> alignment, std::optional<bool> uv);

} // namespace tb::ui::automation
