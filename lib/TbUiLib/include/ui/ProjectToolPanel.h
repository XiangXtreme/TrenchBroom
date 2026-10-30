/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <QPointer>
#include <QWidget>

#include "ui/InfoPanel.h"

#include <vector>

class QFrame;
class QLabel;
class QSplitter;
class QStackedWidget;
class QToolButton;

namespace tb::ui
{
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
  QLabel* m_title = nullptr;
  QToolButton* m_pin = nullptr;
  std::vector<QToolButton*> m_buttons;
  int m_tool = 0;
  bool m_expanded = false;

public:
  ProjectToolPanel(QWidget* workspace, QWidget* content, QWidget* parent = nullptr);
  void setMapPanel(InfoPanel* panel);
  void showContent();
  void showInfoPage(InfoPanelPage page);
  void toggle();
  bool isExpanded() const;

signals:
  void extensionsRequested();

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  void selectTool(int tool);
  void setExpanded(bool expanded);
  void updatePage();
  void updateGeometryForMode();
};
} // namespace tb::ui
