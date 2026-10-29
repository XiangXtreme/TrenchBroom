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
// `libraryAsset` adjusts the wording when the conversion is stored in an asset library
// instead of a project.
Result<bool> prepareProjectTexture(
  QWidget* parent,
  project::AssetImport& asset,
  const std::optional<project::Asset>& previous = std::nullopt,
  bool libraryAsset = false);
} // namespace tb::ui
