/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include "ui/ProjectSession.h"

class QWidget;

namespace tb::ui
{
/**
 * Asks whether a capture may replace the project resource that already publishes the
 * same name. Returns false when the user cancels the capture.
 */
bool confirmCaptureConflict(QWidget* parent, const CaptureConflict& conflict);
} // namespace tb::ui
