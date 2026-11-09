/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <QPointer>
#include <QWidget>

#include "ui/InfoPanel.h"
#include "ui/WorkspaceLayout.h"

#include <map>

class QFrame;
class QComboBox;
class QCheckBox;
class QSplitter;
class QStackedWidget;
class QToolButton;
class QVariantAnimation;

namespace tb::ui
{
class MapEditorView;

enum class WorkspaceToolId
{
  ContentBrowser,
  OutputLog,
  PythonConsole,
  Issues,
};

/** One workspace tool surface, either overlaid temporarily or pinned into the layout. */
class WorkspaceToolPanel : public QWidget
{
  Q_OBJECT
  QWidget* m_surface = nullptr;
  QWidget* m_bar = nullptr;
  QWidget* m_footer = nullptr;
  QWidget* m_status = nullptr;
  QSplitter* m_splitter = nullptr;
  QFrame* m_panel = nullptr;
  QWidget* m_body = nullptr;
  QWidget* m_resizeHandle = nullptr;
  bool m_resizing = false;
  int m_resizeStartY = 0;
  int m_resizeStartHeight = 0;
  QVariantAnimation* m_animation = nullptr;
  QStackedWidget* m_pages = nullptr;
  QPointer<QWidget> m_content;
  QPointer<InfoPanel> m_mapPanel;
  MapEditorView* m_activeEditor = nullptr;
  std::map<MapEditorView*, InfoPanel*> m_mapPanels;
  QPointer<QWidget> m_restoreFocus;
  QWidget* m_workspace = nullptr;
  QComboBox* m_mode = nullptr;
  QCheckBox* m_keepOpen = nullptr;
  QWidget* m_contentControls = nullptr;
  QToolButton* m_fold = nullptr;
  std::map<WorkspaceToolId, QToolButton*> m_buttons;
  WorkspaceToolId m_tool = WorkspaceToolId::ContentBrowser;
  bool m_expanded = false;
  bool m_changingMode = false;
  bool m_restoringLayout = true;
  bool m_layoutPending = true;
  bool m_updatingGeometry = false;
  WorkspaceKind m_kind;
  ToolLayout m_layout;
  QString m_pendingTool;

public:
  WorkspaceToolPanel(
    WorkspaceKind kind, QWidget* workspace, QWidget* content, QWidget* parent = nullptr);
  ~WorkspaceToolPanel() override;
  void resetLayout();
  void addStatusWidget(QWidget* status);
  void addContentControl(QWidget* control);
  /** Transfers panel ownership to this host, until unregisterMapEditor. */
  void registerMapEditor(MapEditorView& editor);
  /** Called before the editor's document and rendering context are released. */
  void unregisterMapEditor(MapEditorView& editor);
  void setMapEditor(MapEditorView* editor);
  void showContent();
  void showInfoPage(InfoPanelPage page);
  void toggle();
  bool isExpanded() const;
  bool isAnimating() const;
  WorkspaceToolId currentTool() const;
  bool isToolVisible(WorkspaceToolId tool) const;

protected:
  void showEvent(QShowEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  void selectTool(WorkspaceToolId tool);
  void setExpanded(bool expanded, bool restoreFocus = true);
  void updatePage();
  void updateGeometryForMode();
  int collapsedHeight() const;
  int expandedHeight() const;
  void applyDrawerHeight(int height);
  void saveLayout();
  void captureHeight();
};
} // namespace tb::ui
