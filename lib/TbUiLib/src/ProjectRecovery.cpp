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

#include "ui/ProjectRecovery.h"

#include <QDateTime>

#include "project/ProjectStore.h"
#include "ui/ProjectSession.h"

#include <system_error>

namespace tb::ui
{
namespace
{

std::string failure(const std::error_code& error, const std::string& what)
{
  return what + ": " + error.message();
}

std::filesystem::path temporarySnapshotPath(const std::filesystem::path& snapshot)
{
  return snapshot.parent_path() / (snapshot.filename().string() + ".tmp");
}

} // namespace

std::filesystem::path projectRecoveryDirectory(const std::filesystem::path& projectPath)
{
  return projectPath.parent_path() / "autosave";
}

std::filesystem::path projectRecoveryPath(const std::filesystem::path& projectPath)
{
  return projectRecoveryDirectory(projectPath) / projectPath.filename();
}

bool hasRecoverableProjectSnapshot(const std::filesystem::path& projectPath)
{
  auto error = std::error_code{};
  const auto snapshot = projectRecoveryPath(projectPath);
  if (!std::filesystem::exists(snapshot, error) || error)
  {
    return false;
  }

  const auto snapshotTime = std::filesystem::last_write_time(snapshot, error);
  if (error)
  {
    return false;
  }

  // A snapshot without its project is still the only copy of the work.
  if (!std::filesystem::exists(projectPath, error) || error)
  {
    return true;
  }

  const auto projectTime = std::filesystem::last_write_time(projectPath, error);
  return !error && snapshotTime > projectTime;
}

Result<void> writeProjectRecovery(
  ProjectSession& session, const std::filesystem::path& projectPath)
{
  const auto snapshot = projectRecoveryPath(projectPath);
  auto error = std::error_code{};
  std::filesystem::create_directories(snapshot.parent_path(), error);
  if (error)
  {
    return Error{failure(error, "Cannot create the recovery folder")};
  }

  const auto temporary = temporarySnapshotPath(snapshot);
  std::filesystem::remove(temporary, error);
  error.clear();

  if (const auto written = session.saveCopy(temporary); written.is_error())
  {
    auto ignored = std::error_code{};
    std::filesystem::remove(temporary, ignored);
    return Error{std::get<Error>(written.error()).msg};
  }

  // Replace the previous snapshot only after the new one is complete.
  std::filesystem::remove(snapshot, error);
  error.clear();
  std::filesystem::rename(temporary, snapshot, error);
  if (error)
  {
    auto ignored = std::error_code{};
    std::filesystem::remove(temporary, ignored);
    return Error{failure(error, "Cannot publish the recovery snapshot")};
  }
  return kdl::void_success;
}

void discardProjectRecovery(const std::filesystem::path& projectPath)
{
  auto error = std::error_code{};
  const auto snapshot = projectRecoveryPath(projectPath);
  std::filesystem::remove(snapshot, error);
  std::filesystem::remove(temporarySnapshotPath(snapshot), error);

  const auto directory = projectRecoveryDirectory(projectPath);
  error.clear();
  if (
    std::filesystem::exists(directory, error) && !error
    && std::filesystem::is_empty(directory, error) && !error)
  {
    std::filesystem::remove(directory, error);
  }
}

Result<std::filesystem::path> restoreProjectRecovery(
  const std::filesystem::path& projectPath)
{
  auto error = std::error_code{};
  const auto snapshot = projectRecoveryPath(projectPath);
  if (!std::filesystem::exists(snapshot, error) || error)
  {
    return Error{"No recovery snapshot exists for this project."};
  }

  {
    // Never replace the project with a file that is not a readable project.
    const auto valid = project::ProjectStore::open(snapshot);
    if (valid.is_error())
    {
      return Error{
        "The recovery snapshot is not a readable project: "
        + std::get<Error>(valid.error()).msg};
    }
  }

  auto backup = std::filesystem::path{};
  if (std::filesystem::exists(projectPath, error) && !error)
  {
    backup =
      projectRecoveryDirectory(projectPath)
      / (projectPath.filename().string() + ".backup-"
         + QDateTime::currentDateTime()
             .toString(QString{"yyyyMMdd-HHmmss-zzz"})
             .toStdString()
         + ".tbproj");
    std::filesystem::copy_file(
      projectPath, backup, std::filesystem::copy_options::none, error);
    if (error)
    {
      return Error{failure(error, "Cannot back up the current project")};
    }
  }

  std::filesystem::remove(projectPath, error);
  error.clear();
  std::filesystem::rename(snapshot, projectPath, error);
  if (error)
  {
    // The project file is only removed after its backup exists, so a failed install puts
    // the last saved state back instead of leaving the path empty.
    if (!backup.empty())
    {
      auto restored = std::error_code{};
      std::filesystem::copy_file(
        backup, projectPath, std::filesystem::copy_options::overwrite_existing, restored);
    }
    return Error{failure(error, "Cannot install the recovery snapshot")};
  }
  return backup;
}

} // namespace tb::ui
