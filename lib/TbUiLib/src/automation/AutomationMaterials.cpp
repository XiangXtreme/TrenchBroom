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

} // namespace tb::ui::automation
