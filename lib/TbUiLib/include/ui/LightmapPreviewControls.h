/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

class QMenu;
class QWidget;

namespace tb::ui
{
class LightmapPreview;
class TaskNotificationCenter;

QMenu* createLightmapPreviewMenu(LightmapPreview& preview, QWidget* parent);
QWidget* createLightmapPreviewStatus(LightmapPreview& preview, QWidget* parent);
void connectLightmapPreviewNotifications(
  LightmapPreview& preview, TaskNotificationCenter& center, QWidget* dialogParent);
} // namespace tb::ui
