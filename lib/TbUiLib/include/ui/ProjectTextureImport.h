/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include "project/ProjectAsset.h"

#include <optional>

class QWidget;
namespace tb::ui
{
// Returns false when the user cancels. Reimport keeps the engine-facing name.
Result<bool> prepareProjectTexture(
  QWidget* parent,
  project::AssetImport& asset,
  const std::optional<project::Asset>& previous = std::nullopt);
} // namespace tb::ui
