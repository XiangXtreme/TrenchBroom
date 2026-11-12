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

QMenu* createLightmapPreviewMenu(LightmapPreview& preview, QWidget* parent);
QWidget* createLightmapPreviewStatus(LightmapPreview& preview, QWidget* parent);
} // namespace tb::ui
