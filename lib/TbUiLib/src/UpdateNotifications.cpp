/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/UpdateNotifications.h"

#include <QCoreApplication>

#include "ui/TaskNotification.h"
#include "update/Updater.h"

#include <variant>

namespace tb::ui
{
namespace
{
QString tr(const char* text)
{
  return QCoreApplication::translate("UpdateNotifications", text);
}
} // namespace

void updateUpdateNotification(
  TaskNotificationCenter& center,
  upd::Updater& updater,
  const upd::UpdateControllerState& state)
{
  const auto id = QStringLiteral("update");
  auto notification = TaskNotification{};
  if (std::holds_alternative<upd::CheckingForUpdatesState>(state))
  {
    notification.title = tr("Checking for updates");
    notification.progress = -1;
    notification.showDelayMs = updater.userInitiatedCheck() ? 0 : 1000;
  }
  else if (const auto* available = std::get_if<upd::UpdateAvailableState>(&state))
  {
    notification.state = TaskNotification::State::Succeeded;
    notification.title = tr("Update available");
    notification.detail = available->updateInfo.updateVersion;
    notification.dismissible = true;
    notification.expireAfterMs = -1;
    notification.actions.push_back(
      {tr("View update"), [&updater] { updater.showUpdateDialog(); }});
  }
  else if (std::holds_alternative<upd::UpToDateState>(state))
  {
    notification.state = TaskNotification::State::Succeeded;
    notification.title = tr("TrenchBroom is up to date");
  }
  else if (
    std::holds_alternative<upd::DownloadingUpdateState>(state)
    || std::holds_alternative<upd::PreparingUpdateState>(state))
  {
    notification.title = std::holds_alternative<upd::DownloadingUpdateState>(state)
                           ? tr("Downloading update")
                           : tr("Preparing update");
    notification.progress = -1;
    notification.actions.push_back(
      {tr("View update"), [&updater] { updater.showUpdateDialog(); }});
  }
  else if (std::holds_alternative<upd::UpdatePendingState>(state))
  {
    notification.state = TaskNotification::State::Succeeded;
    notification.title = tr("Update ready to install");
    notification.dismissible = true;
    notification.expireAfterMs = -1;
    notification.actions.push_back(
      {tr("View update"), [&updater] { updater.showUpdateDialog(); }});
  }
  else if (const auto* error = std::get_if<upd::UpdateErrorState>(&state))
  {
    notification.state = TaskNotification::State::Failed;
    notification.title = tr("Update failed");
    notification.detail = error->errorMessage;
    notification.dismissible = true;
    notification.actions.push_back(
      {tr("View update"), [&updater] { updater.showUpdateDialog(); }});
    notification.actions.push_back(
      {tr("Retry"), [&updater] { updater.checkForUpdates(); }});
  }
  else
  {
    center.remove(id);
    return;
  }
  center.update(id, notification);
}

void connectUpdateNotifications(TaskNotificationCenter& center, upd::Updater& updater)
{
  QObject::connect(
    &updater,
    &upd::Updater::stateChanged,
    &center,
    [&center, &updater](const auto& state) {
      updateUpdateNotification(center, updater, state);
    });
  updateUpdateNotification(center, updater, updater.state());
}
} // namespace tb::ui
