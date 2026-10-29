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

#include "base/Result.h"

#include <filesystem>

namespace tb::ui
{

class ProjectSession;

/**
 * Recovery snapshots live next to the project so an interrupted session can be found
 * without a global registry: `<project dir>/autosave/<project stem>.tbproj`.
 */
std::filesystem::path projectRecoveryDirectory(const std::filesystem::path& projectPath);

std::filesystem::path projectRecoveryPath(const std::filesystem::path& projectPath);

/**
 * True when a recovery snapshot exists and is newer than the project file.
 *
 * A snapshot is only useful when it contains work the saved project does not: writing a
 * snapshot and then saving the project makes the project the newer file, so the stale
 * snapshot is not offered on the next start.
 */
bool hasRecoverableProjectSnapshot(const std::filesystem::path& projectPath);

/**
 * Writes a consistent snapshot of the session for crash recovery.
 *
 * The snapshot is written to a temporary file and renamed into place, so an interrupted
 * write never replaces a usable snapshot with a partial one.
 */
Result<void> writeProjectRecovery(
  ProjectSession& session, const std::filesystem::path& projectPath);

/** Removes the snapshot; a missing snapshot is not an error. */
void discardProjectRecovery(const std::filesystem::path& projectPath);

/**
 * Restores the snapshot over the project file.
 *
 * The current project is copied to a timestamped backup in the recovery folder first and
 * the snapshot is validated before it replaces anything, so recovery cannot destroy the
 * last saved state. Returns the backup path.
 */
Result<std::filesystem::path> restoreProjectRecovery(
  const std::filesystem::path& projectPath);

} // namespace tb::ui
