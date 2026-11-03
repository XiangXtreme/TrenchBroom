/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom.

  TrenchBroom is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  TrenchBroom is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
  */

#pragma once

#include "ui/automation/AutomationAssets.h"

#include <filesystem>
#include <string>

namespace tb::ui
{
class ProjectSession;

/**
 * One rendered asset preview.
 *
 * "image" points at a PNG inside the output folder, "audio" at the source file, and
 * "unsupported" or "missing" explain in `message` why no image was produced, so a caller
 * can report the asset without treating it as a failed request.
 */
struct AutomationAssetPreview
{
  std::string status;
  std::string path;
  int width = 0;
  int height = 0;
  std::string source;
  std::string message;
};

/**
 * Renders a preview for one catalog asset and writes it as a PNG.
 *
 * Project and library assets need their owning session; game assets are read straight
 * from `absolutePath` and may be previewed with a null project.
 */
AutomationAssetPreview previewAutomationAsset(
  ProjectSession* project,
  const AutomationAssetSummary& asset,
  int size,
  const std::filesystem::path& outputFolder);

} // namespace tb::ui
