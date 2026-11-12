/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include "update/UpdateController.h"

namespace upd
{
class Updater;
}

namespace tb::ui
{
class TaskNotificationCenter;

void updateUpdateNotification(
  TaskNotificationCenter& center,
  upd::Updater& updater,
  const upd::UpdateControllerState& state);
void connectUpdateNotifications(TaskNotificationCenter& center, upd::Updater& updater);
} // namespace tb::ui
