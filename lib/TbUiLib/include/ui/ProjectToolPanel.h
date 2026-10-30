/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <QPointer>
#include <QWidget>

#include "ui/InfoPanel.h"

#include <map>

class QFrame;
class QLabel;
class QSplitter;
class QStackedWidget;
class QToolButton;

namespace tb::ui
{
class MapEditorView;

enum class ProjectToolId
{
  ContentBrowser,
  OutputLog,
  PythonConsole,
  Issues,
};

/** One project tool surface, either overlaid temporarily or pinned into the layout. */
class ProjectToolPanel : public QWidget
{
  Q_OBJECT
  QWidget* m_surface = nullptr;
  QWidget* m_bar = nullptr;
  QSplitter* m_splitter = nullptr;
  QFrame* m_panel = nullptr;
  QStackedWidget* m_pages = nullptr;
  QWidget* m_content = nullptr;
  QPointer<InfoPanel> m_mapPanel;
  MapEditorView* m_activeEditor = nullptr;
  std::map<MapEditorView*, InfoPanel*> m_mapPanels;
  QPointer<QWidget> m_restoreFocus;
  QWidget* m_workspace = nullptr;
  QLabel* m_title = nullptr;
  QToolButton* m_pin = nullptr;
  std::map<ProjectToolId, QToolButton*> m_buttons;
  ProjectToolId m_tool = ProjectToolId::ContentBrowser;
  bool m_expanded = false;
  bool m_changingMode = false;

public:
  ProjectToolPanel(QWidget* workspace, QWidget* content, QWidget* parent = nullptr);
  /** Transfers panel ownership to this host, until unregisterMapEditor. */
  void registerMapEditor(MapEditorView& editor);
  /** Called before the editor's document and rendering context are released. */
  void unregisterMapEditor(MapEditorView& editor);
  void setMapEditor(MapEditorView* editor);
  void showContent();
  void showInfoPage(InfoPanelPage page);
  void toggle();
  bool isExpanded() const;
  ProjectToolId currentTool() const;
  bool isToolVisible(ProjectToolId tool) const;

signals:
  void extensionsRequested();

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  void selectTool(ProjectToolId tool);
  void setExpanded(bool expanded, bool restoreFocus = true);
  void updatePage();
  void updateGeometryForMode();
};
} // namespace tb::ui
