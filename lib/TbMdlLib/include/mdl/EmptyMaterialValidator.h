/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include "mdl/Validator.h"

namespace tb::mdl
{
class EmptyMaterialValidator : public Validator
{
public:
  EmptyMaterialValidator();

private:
  void doValidate(
    BrushNode& brushNode, std::vector<std::unique_ptr<Issue>>& issues) const override;
};
} // namespace tb::mdl
