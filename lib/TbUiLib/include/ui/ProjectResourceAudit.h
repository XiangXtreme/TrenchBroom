/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include "base/Result.h"

#include <cstddef>
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
Result<ProjectResourceAudit> auditGoldSrcProjectResources(const ProjectSession& session);
} // namespace tb::ui
