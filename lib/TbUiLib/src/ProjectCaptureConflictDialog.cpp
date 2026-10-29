/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/ProjectCaptureConflictDialog.h"

#include <QAbstractButton>
#include <QMessageBox>
#include <QPushButton>

namespace tb::ui
{
bool confirmCaptureConflict(QWidget* parent, const CaptureConflict& conflict)
{
  auto box = QMessageBox{parent};
  box.setIcon(QMessageBox::Warning);
  box.setWindowTitle(QObject::tr("Replace Project Resource"));
  box.setText(QObject::tr("The project already contains a resource at \"%1\".")
                .arg(QString::fromStdString(conflict.path)));

  auto details = QStringList{};
  details
    << QObject::tr("Incoming: %1").arg(QString::fromStdString(conflict.displayName));
  if (conflict.existing)
  {
    details << QObject::tr("Existing: %1 (revision %2)")
                 .arg(QString::fromStdString(conflict.existing->displayName))
                 .arg(conflict.existing->revision);
    if (!conflict.existing->library.empty())
      details << QObject::tr("Captured from an asset library.");
  }
  details << QObject::tr(
    "Two project resources cannot publish the same name. Replacing keeps the project "
    "path, so maps keep resolving it; the previous revision stays in the project "
    "history.");

  box.setInformativeText(details.join('\n'));
  auto* replace = box.addButton(QObject::tr("Replace"), QMessageBox::AcceptRole);
  box.addButton(QObject::tr("Cancel"), QMessageBox::RejectRole);
  box.setDefaultButton(replace);
  box.exec();
  return box.clickedButton() == static_cast<QAbstractButton*>(replace);
}
} // namespace tb::ui
