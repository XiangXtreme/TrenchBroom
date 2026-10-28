/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <QDialog>

#include "ui/ProjectResourceAudit.h"

#include <functional>
#include <optional>
#include <string>

class QCheckBox;
class QLabel;
class QLineEdit;
class QTreeWidget;

namespace tb::ui
{
class ProjectSession;

class ProjectResourceDialog : public QDialog
{
  const ProjectSession& m_session;
  std::optional<std::string> m_assetId;
  std::function<void(const std::string&)> m_openMap;
  ProjectResourceAudit m_audit;
  QTreeWidget* m_uses;
  QLabel* m_summary;
  QLineEdit* m_search;
  QCheckBox* m_issuesOnly;

public:
  ProjectResourceDialog(
    const ProjectSession& session,
    std::optional<std::string> assetId,
    std::function<void(const std::string&)> openMap,
    QWidget* parent = nullptr);

private:
  void rescan();
  void refresh();
  void openMap();
};
} // namespace tb::ui
