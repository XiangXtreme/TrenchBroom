/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <QTemporaryDir>

#include "base/Result.h"

#include <filesystem>
#include <memory>

class QString;
class QWidget;

namespace tb::ui
{
class AppController;
class ProjectWindow;
class ProjectSettingsDialog;

/** Owns a disposable real project and destroys its views before removing its files. */
class UiWorkspaceSnapshot
{
  QTemporaryDir m_directory;
  std::unique_ptr<ProjectWindow> m_window;
  std::unique_ptr<ProjectSettingsDialog> m_settings;

public:
  UiWorkspaceSnapshot();
  ~UiWorkspaceSnapshot();
  Result<void> initialize(
    AppController& appController,
    const std::filesystem::path& mapPath,
    const QString& target);
  QWidget* widget() const;
};

void configureWorkspaceSnapshot(QWidget& widget, const QString& target);
/** Empty when the actual project, tool, tab and status state matches the target. */
QString workspaceSnapshotPendingReason(QWidget& widget, const QString& target);
} // namespace tb::ui
