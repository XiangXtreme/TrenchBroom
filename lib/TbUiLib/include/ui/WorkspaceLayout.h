/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <QByteArray>
#include <QString>

#include "ui/InfoPanel.h"
#include "ui/Inspector.h"

class QSettings;

namespace tb::ui
{
enum class WorkspaceKind
{
  Map,
  Project,
};

struct EditorLayout
{
  double inspectorRatio = 0.33;
  double infoRatio = 0.25;
  bool inspectorVisible = true;
  bool infoVisible = true;
  InspectorPage inspectorPage = InspectorPage::Map;
  InfoPanelPage infoPage = InfoPanelPage::Console;
  // Consumed once by the view, then saved as validated proportions.
  QByteArray legacyHorizontal;
  QByteArray legacyVertical;
};

struct ToolLayout
{
  QString tool = "content-browser";
  bool expanded = false;
  bool pinned = false;
  bool keepOpen = false;
  double heightRatio = 0.4;
};

/** Explicit settings identities, independent of widget discovery names. */
EditorLayout loadEditorLayout(QSettings& settings, WorkspaceKind kind);
void saveEditorLayout(
  QSettings& settings, WorkspaceKind kind, const EditorLayout& layout);
ToolLayout loadToolLayout(QSettings& settings, WorkspaceKind kind);
void saveToolLayout(QSettings& settings, WorkspaceKind kind, const ToolLayout& layout);
} // namespace tb::ui
