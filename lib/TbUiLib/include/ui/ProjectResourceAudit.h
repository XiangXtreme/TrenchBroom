/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include "base/Result.h"
#include "ui/AssetBrowserModel.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace tb::ui
{
class ProjectSession;

enum class ProjectResourceStatus
{
  Embedded,
  Missing,
  Ambiguous,
  InvalidPath,
  Unsupported,
  ScanError,
};
QString projectResourceStatusLabel(ProjectResourceStatus status);

struct ProjectResourceUse
{
  std::string mapId;
  std::string mapPath;
  size_t line = 0; // Entity start or brush face in the scanned MAP snapshot.
  std::string location;
  std::string reference;
  ProjectResourceStatus status = ProjectResourceStatus::Missing;
  std::vector<std::string> assetIds;
  std::string detail;
};

struct ProjectResourceAudit
{
  size_t mapsScanned = 0;
  std::vector<ProjectResourceUse> uses;
};

// Explicit GoldSrc MAP references only. Embedded means present, not engine validated.
// Results are rebuilt from current contents, including open unsaved maps. No mutation.
Result<ProjectResourceAudit> auditGoldSrcProjectResources(
  const ProjectSession& session, const std::optional<std::string>& mapId = std::nullopt);

std::vector<BrowserAsset> browserAssetsUsedInMap(
  const std::vector<BrowserAsset>& assets,
  const ProjectResourceAudit& audit,
  const std::string& mapId);
} // namespace tb::ui
