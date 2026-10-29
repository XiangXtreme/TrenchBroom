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

#include <QTemporaryDir>

#include "project/ProjectStore.h"
#include "ui/ProjectRecovery.h"
#include "ui/ProjectSession.h"
#include "ui/QPathUtils.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{

template <typename T>
T require(Result<T> result)
{
  if (result.is_error())
    FAIL(std::get<Error>(result.error()).msg);
  return std::move(result).value();
}

void require(Result<void> result)
{
  if (result.is_error())
    FAIL(std::get<Error>(result.error()).msg);
}

project::AssetImport sound(const std::string& path)
{
  auto asset = project::AssetImport{};
  asset.type = project::AssetType::Sound;
  asset.virtualPath = path;
  asset.displayName = path;
  asset.original = {1, 2, 3};
  asset.compatible = asset.original;
  return asset;
}

} // namespace

TEST_CASE("ProjectRecovery")
{
  auto temporary = QTemporaryDir{};
  REQUIRE(temporary.isValid());
  const auto projectPath = pathFromQString(temporary.filePath("workspace.tbproj"));
  auto session = require(ProjectSession::create(projectPath, "Workspace", "Quake"));

  SECTION("writes and discards the snapshot next to the project")
  {
    CHECK(
      projectRecoveryDirectory(projectPath) == projectPath.parent_path() / "autosave");
    CHECK(
      projectRecoveryPath(projectPath)
      == projectPath.parent_path() / "autosave" / "workspace.tbproj");
    CHECK_FALSE(hasRecoverableProjectSnapshot(projectPath));

    require(session->importAsset(sound("sound/a.wav")));
    require(session->save());
    require(writeProjectRecovery(*session, projectPath));
    CHECK(std::filesystem::exists(projectRecoveryPath(projectPath)));
    CHECK(hasRecoverableProjectSnapshot(projectPath));

    discardProjectRecovery(projectPath);
    CHECK_FALSE(std::filesystem::exists(projectRecoveryPath(projectPath)));
    CHECK_FALSE(hasRecoverableProjectSnapshot(projectPath));
    // The empty recovery folder is removed with the snapshot.
    CHECK_FALSE(std::filesystem::exists(projectRecoveryDirectory(projectPath)));
  }

  SECTION("restores unsaved content over the project and keeps a backup")
  {
    const auto saved = require(session->importAsset(sound("sound/saved.wav")));
    require(session->save());
    const auto unsaved = require(session->importAsset(sound("sound/unsaved.wav")));
    require(writeProjectRecovery(*session, projectPath));

    const auto backup = require(restoreProjectRecovery(projectPath));
    CHECK(std::filesystem::exists(backup));
    CHECK_FALSE(std::filesystem::exists(projectRecoveryPath(projectPath)));

    const auto restored = require(project::ProjectStore::open(projectPath));
    CHECK(require(restored->findAsset(unsaved.id)).has_value());

    const auto previous = require(project::ProjectStore::open(backup));
    CHECK(require(previous->findAsset(saved.id)).has_value());
    CHECK_FALSE(require(previous->findAsset(unsaved.id)).has_value());
  }

  SECTION("refuses to restore when no snapshot exists")
  {
    CHECK(restoreProjectRecovery(projectPath).is_error());
    CHECK_FALSE(std::filesystem::exists(projectRecoveryPath(projectPath)));
  }
}

} // namespace tb::ui
