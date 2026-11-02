/*
 Copyright (C) 2025 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "update/UpdateIndicator.h"

#include <QBoxLayout>
#include <QLabel>

#include "update/Overload.h"
#include "update/UpdateDialog.h"

#include <variant>

namespace upd
{

UpdateIndicator::UpdateIndicator(UpdateController& updateController, QWidget* parent)
  : QLabel{parent}
  , m_updateController{updateController}
{
  updateUI(m_updateController.state());

  connect(
    &m_updateController,
    &UpdateController::stateChanged,
    this,
    &UpdateIndicator::stateChanged);

  connect(this, &QLabel::linkActivated, this, &UpdateIndicator::linkActivated);
}

UpdateIndicator::~UpdateIndicator() = default;

void UpdateIndicator::stateChanged(const UpdateControllerState& state)
{
  updateUI(state);
}

void UpdateIndicator::linkActivated(const QString& uri)
{
  if (uri == "upd://checkForUpdates")
  {
    m_updateController.checkForUpdates();
  }

  auto dialog = UpdateDialog{m_updateController};
  dialog.exec();
}

void UpdateIndicator::updateUI(const UpdateControllerState& state)
{
  const auto showIfParented = [this]() {
    if (parentWidget() != nullptr)
      setVisible(true);
  };
  std::visit(
    overload(
      [&](const IdleState&) {
        showIfParented();
        setText(tr(R"(<a href="upd://checkForUpdates">Check for updates</a>)"));
      },
      [&](const CheckingForUpdatesState&) {
        showIfParented();
        setText(tr("Checking for updates..."));
      },
      [&](const UpdateAvailableState&) {
        showIfParented();
        setText(tr(R"(<a href="upd://showDialog">Update available</a>)"));
      },
      [&](const UpToDateState&) {
        showIfParented();
        setText(tr(R"(Up to date)"));
      },
      [&](const DownloadingUpdateState&) {
        showIfParented();
        setText(tr("Downloading update..."));
      },
      [&](const PreparingUpdateState&) {
        showIfParented();
        setText(tr("Preparing update..."));
      },
      [&](const UpdatePendingState&) {
        showIfParented();
        setText(tr("Update pending"));
      },
      [&](const UpdateErrorState&) {
        setVisible(true);
        setText(tr(R"(<a href="upd://showDialog">Update error</a>)"));
      },
      [&](const UpdateDisabledState&) {
        setVisible(true);
        setText(tr(R"(Updates disabled)"));
      }),
    state);
}

} // namespace upd
