/*
 Copyright (C) 2010 Kristian Duske

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

#pragma once

#include <QString>
#include <QWidget>

#include "mdl/IssueType.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QStandardItemModel;
class QToolButton;
class QTreeView;

namespace tb::mdl
{
class Issue;
class IssueQuickFix;
} // namespace tb::mdl

namespace tb
{
namespace ui
{
class FlagsPopupEditor;
class MapDocument;
class SignalDelayer;

QString issueTypeLabel(const std::string& description);

// The model and details contain values only: node edits can destroy cached Issues
// before the delayed validation runs. Live issues are usable only while m_valid.
struct IssueBrowserEntry
{
  size_t id;
  mdl::IssueType type;
  QString category;
  QString path;
  QString description;
  QString property;
  QString value;
  size_t line;
  bool hidden;
  bool isProperty;
};

class IssueBrowserView : public QWidget
{
  Q_OBJECT
private:
  MapDocument& m_document;
  int m_hiddenIssueTypes = 0;
  bool m_showHiddenIssues = false;
  bool m_valid = false;
  bool m_rebuilding = false;
  std::vector<IssueBrowserEntry> m_entries;
  std::unordered_map<size_t, const mdl::Issue*> m_liveIssues;
  QTreeView* m_tree = nullptr;
  QStandardItemModel* m_model = nullptr;
  QLineEdit* m_search = nullptr;
  FlagsPopupEditor* m_filter = nullptr;
  std::unordered_map<int, bool> m_expansion;
  QLabel* m_status = nullptr;
  QLabel* m_title = nullptr;
  QLabel* m_details = nullptr;
  QLabel* m_result = nullptr;
  QPushButton* m_locate = nullptr;
  QPushButton* m_editProperty = nullptr;
  QPushButton* m_ignore = nullptr;
  QPushButton* m_restore = nullptr;
  QToolButton* m_fix = nullptr;
  SignalDelayer* m_validateSignalDelayer = nullptr;

public:
  explicit IssueBrowserView(MapDocument& document, QWidget* parent = nullptr);
  int hiddenIssueTypes() const;
  void setHiddenIssueTypes(int hiddenIssueTypes);
  void setShowHiddenIssues(bool show);
  void reload();
  void updateFilterFlags();
  void deselectAll();

signals:
  void focusObjectsRequested(bool faces);
  void editPropertyRequested(const QString& key);

public slots:
  void validate();
  void locateSelection();

private:
  void createGui();
  void updateIssues();
  struct SelectionState
  {
    std::unordered_set<size_t> ids;
    std::unordered_set<int> types;
  };
  SelectionState selectionState() const;
  void rebuildModel();
  void rebuildModel(const SelectionState& selection);
  void updateDetails();
  std::vector<size_t> selectedEntries() const;
  std::vector<const mdl::Issue*> collectIssues() const;
  std::vector<const mdl::IssueQuickFix*> collectQuickFixes() const;
  bool selectObjects(const std::vector<const mdl::Issue*>& issues);
  void setIssueVisibility(bool show);
  void populateFixMenu(QMenu& menu);
  void applyQuickFix(const std::string& description);
};
} // namespace ui
} // namespace tb
