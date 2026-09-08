/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.
 */

#include "ui/automation/AutomationMaterials.h"

#include "mdl/BrushFace.h"
#include "mdl/Map.h"
#include "mdl/Map_Brushes.h"
#include "mdl/Map_Selection.h"
#include "mdl/UpdateBrushFaceAttributes.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace tb::ui::automation
{

std::optional<AutomationFaceAlignment> automationFaceAlignmentFromString(
  const std::string& mode)
{
  if (mode == "reset")
  {
    return AutomationFaceAlignment::Reset;
  }
  if (mode == "paraxial" || mode == "world")
  {
    return AutomationFaceAlignment::Paraxial;
  }
  if (mode == "parallel" || mode == "face")
  {
    return AutomationFaceAlignment::Parallel;
  }
  return std::nullopt;
}

bool alignBrushFaceAxes(
  mdl::Map& map,
  const std::vector<mdl::BrushFaceHandle>& faces,
  const AutomationFaceAlignment alignment)
{
  if (faces.empty())
  {
    return false;
  }

  auto update = mdl::UpdateBrushFaceAttributes{};
  switch (alignment)
  {
  case AutomationFaceAlignment::Reset:
    update.axis = mdl::ResetAxis{};
    break;
  case AutomationFaceAlignment::Paraxial:
    update.axis = mdl::ToParaxial{};
    break;
  case AutomationFaceAlignment::Parallel:
    update.axis = mdl::ToParallel{};
    break;
  }

  mdl::deselectAll(map);
  mdl::selectBrushFaces(map, faces);
  return mdl::setBrushFaceAttributes(map, update);
}

bool copyBrushFaceAttributes(
  mdl::Map& map,
  const mdl::BrushFaceHandle& source,
  const std::vector<mdl::BrushFaceHandle>& targets)
{
  if (targets.empty())
  {
    return false;
  }

  const auto sourceFace = source.face();
  mdl::deselectAll(map);
  mdl::selectBrushFaces(map, targets);
  return mdl::setBrushFaceAttributes(map, mdl::copyAll(sourceFace));
}

bool setBrushFaceMaterial(
  mdl::Map& map,
  const std::vector<mdl::BrushFaceHandle>& faces,
  const std::string& material)
{
  if (faces.empty() || material.empty())
  {
    return false;
  }

  mdl::deselectAll(map);
  mdl::selectBrushFaces(map, faces);
  return mdl::setBrushFaceAttributes(map, {.materialName = material});
}

std::vector<mdl::BrushFaceHandle> filterBrushFaceHandles(
  std::vector<mdl::BrushFaceHandle> faces,
  const AutomationFaceFilter& filter,
  std::string& error)
{
  auto semantic = filter.semantic;
  std::ranges::transform(semantic, semantic.begin(), [](const auto character) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  });
  semantic.erase(
    semantic.begin(),
    std::find_if(semantic.begin(), semantic.end(), [](const auto character) {
      return !std::isspace(static_cast<unsigned char>(character));
    }));
  semantic.erase(
    std::find_if(
      semantic.rbegin(),
      semantic.rend(),
      [](const auto character) {
        return !std::isspace(static_cast<unsigned char>(character));
      })
      .base(),
    semantic.end());

  if (
    !filter.normal && semantic != "" && semantic != "all" && semantic != "top"
    && semantic != "bottom" && semantic != "side" && semantic != "sides")
  {
    error = "face_semantic must be all, top, bottom, or side";
    return {};
  }
  if (
    !std::isfinite(filter.normalTolerance) || filter.normalTolerance < 0.0
    || filter.normalTolerance > 1.0)
  {
    error = "normal_tolerance must be between 0 and 1";
    return {};
  }
  if (
    filter.normal
    && vm::is_zero(vm::squared_length(*filter.normal), vm::Cd::almost_zero()))
  {
    error = "normal must not be zero";
    return {};
  }

  const auto requestedNormal =
    filter.normal ? std::make_optional(vm::normalize(*filter.normal)) : std::nullopt;
  faces.erase(
    std::remove_if(
      faces.begin(),
      faces.end(),
      [&](const auto& handle) {
        const auto normal = handle.face().normal();
        if (requestedNormal)
        {
          return vm::dot(normal, *requestedNormal) < filter.normalTolerance;
        }
        if (semantic.empty() || semantic == "all")
        {
          return false;
        }
        if (semantic == "top")
        {
          return normal.z() < filter.normalTolerance;
        }
        if (semantic == "bottom")
        {
          return normal.z() > -filter.normalTolerance;
        }
        return std::abs(normal.z()) > 1.0 - filter.normalTolerance;
      }),
    faces.end());
  if (faces.empty())
  {
    error = requestedNormal ? "normal matched no brush faces"
                            : "face_semantic matched no brush faces";
  }
  return faces;
}

} // namespace tb::ui::automation
