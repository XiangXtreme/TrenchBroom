/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <QDialog>

#include "project/ProjectAsset.h"

#include <memory>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;

namespace tb::ui
{
class ProjectSession;

class ProjectSettingsDialog : public QDialog
{
  Q_OBJECT
private:
  std::shared_ptr<ProjectSession> m_session;
  QLineEdit* m_namespace;
  QLineEdit* m_directory;
  QCheckBox* m_migrate;
  QLabel* m_examples;
  QLabel* m_status;
  QTreeWidget* m_preview;
  QPushButton* m_save;
  project::PublishSettings settings() const;
  void refresh();
  void save();

public:
  explicit ProjectSettingsDialog(
    std::shared_ptr<ProjectSession> session, QWidget* parent = nullptr);
};
} // namespace tb::ui
