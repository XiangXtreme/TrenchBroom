/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "mdl/EmptyMaterialValidator.h"

#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/Issue.h"

#include "kd/string_compare.h"

namespace tb::mdl
{
namespace
{
const auto Type = freeIssueType();
}

EmptyMaterialValidator::EmptyMaterialValidator()
  : Validator{Type, "Placeholder texture"}
{
}

void EmptyMaterialValidator::doValidate(
  BrushNode& brushNode, std::vector<std::unique_ptr<Issue>>& issues) const
{
  const auto& brush = brushNode.brush();
  for (auto i = size_t{0}; i < brush.faceCount(); ++i)
  {
    const auto& name = brush.face(i).materialName();
    if (name.empty() || kdl::ci::str_is_equal(name, BrushFace::NoMaterialName))
      issues.push_back(std::make_unique<BrushFaceIssue>(
        Type, brushNode, i, "Face uses the __TB_empty placeholder texture."));
  }
}
} // namespace tb::mdl
