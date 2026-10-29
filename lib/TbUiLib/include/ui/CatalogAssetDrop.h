/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once
#include <QString>

#include "base/Result.h"
#include "project/ProjectAsset.h"
#include "ui/AssetBrowserModel.h"
#include "ui/ProjectSession.h"

#include <memory>
#include <vector>
namespace tb::ui
{
class ProjectSession;
QString catalogAssetDragData(const BrowserAsset& asset, const std::string& projectId);
bool isCatalogAssetDrag(const QString& text);
/** Frozen bytes and revisions; preparing or cancelling this value never writes a project.
 */
class PreparedAssetDrop
{
  std::shared_ptr<ProjectSession> m_session;
  std::vector<project::AssetCapture> m_plan;
  std::vector<project::Asset> m_expected;
  std::vector<CaptureConflict> m_conflicts;
  std::string m_nativePayload;
  QString m_original;
  bool m_committed = false;

public:
  static Result<std::unique_ptr<PreparedAssetDrop>> prepare(
    std::shared_ptr<ProjectSession> session, const QString& text);
  const std::string& nativePayload() const;
  bool matches(const QString& text) const;
  /**
   * Project resources this drop would overwrite.
   *
   * The drop never resolves a conflict by itself: committing with unresolved conflicts
   * fails, so the host can ask the user whether to replace the existing resource.
   */
  const std::vector<CaptureConflict>& conflicts() const;
  void resolveConflicts(CaptureConflictChoice choice);
  Result<void> commit();
};
} // namespace tb::ui
