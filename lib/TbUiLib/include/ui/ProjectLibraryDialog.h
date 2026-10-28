/*
  Copyright (C) 2026 XiangXtreme
  This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <QDialog>

#include <string>
#include <vector>

class QLabel;
class QPushButton;
class QTreeWidget;

namespace tb::ui
{
class ProjectSession;

/**
 * Reviews library updates for the project snapshots.
 *
 * The dialog never changes a snapshot by itself: it lists which captured resources have
 * a newer library revision and which are missing, shows where each one is used, and only
 * applies the checked rows as one project transaction.
 */
class ProjectLibraryDialog : public QDialog
{
  Q_OBJECT
  ProjectSession& m_session;
  QTreeWidget* m_updates = nullptr;
  QLabel* m_summary = nullptr;
  QPushButton* m_updateButton = nullptr;
  QPushButton* m_ignoreButton = nullptr;
  bool m_pendingRescan = false;

public:
  ProjectLibraryDialog(ProjectSession& session, bool rescan, QWidget* parent = nullptr);

private:
  void reload();
  void refreshButtons();
  void updateSelected();
  void ignoreSelected();
  std::vector<std::string> checkedAssets() const;
};
} // namespace tb::ui
