/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/ProjectToolPanel.h"

#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QScopedValueRollback>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "ui/MapEditorView.h"
#include "ui/MapViewBase.h"

#include <algorithm>

namespace tb::ui
{
ProjectToolPanel::ProjectToolPanel(QWidget* workspace, QWidget* content, QWidget* parent)
  : QWidget{parent}
  , m_content{content}
  , m_workspace{workspace}
{
  setObjectName("ProjectTools");
  m_surface = new QWidget{this};
  m_surface->installEventFilter(this);
  auto* surfaceLayout = new QVBoxLayout{m_surface};
  surfaceLayout->setContentsMargins(0, 0, 0, 0);
  m_splitter = new QSplitter{Qt::Vertical, m_surface};
  m_splitter->setChildrenCollapsible(false);
  m_splitter->addWidget(workspace);
  surfaceLayout->addWidget(m_splitter);

  m_panel = new QFrame{m_surface};
  m_panel->setObjectName("ProjectToolDrawer");
  m_panel->setFrameShape(QFrame::StyledPanel);
  m_panel->setAutoFillBackground(true);
  auto* panelLayout = new QVBoxLayout{m_panel};
  panelLayout->setContentsMargins(4, 4, 4, 4);
  auto* header = new QHBoxLayout{};
  m_title = new QLabel{m_panel};
  header->addWidget(m_title, 1);
  m_pin = new QToolButton{m_panel};
  m_pin->setObjectName("ProjectToolsPin");
  m_pin->setProperty("tbControlRole", "compact");
  m_pin->setText(tr("Pin"));
  m_pin->setToolTip(tr("Keep the tool panel open while editing the map"));
  m_pin->setCheckable(true);
  m_pin->setChecked(QSettings{}.value("ProjectTools/Pinned", false).toBool());
  header->addWidget(m_pin);
  auto* close = new QToolButton{m_panel};
  close->setObjectName("ProjectToolsClose");
  close->setProperty("tbControlRole", "compact");
  close->setText(tr("Close"));
  header->addWidget(close);
  panelLayout->addLayout(header);
  m_pages = new QStackedWidget{m_panel};
  m_pages->addWidget(content);
  panelLayout->addWidget(m_pages, 1);

  m_bar = new QWidget{this};
  m_bar->setObjectName("ProjectToolBar");
  auto* barLayout = new QHBoxLayout{m_bar};
  barLayout->setContentsMargins(4, 2, 4, 2);
  const auto tools = std::map<ProjectToolId, QString>{
    {ProjectToolId::ContentBrowser, tr("Content Browser")},
    {ProjectToolId::OutputLog, tr("Output Log")},
    {ProjectToolId::PythonConsole, tr("Python Console")},
    {ProjectToolId::Issues, tr("Issues")}};
  for (const auto& [id, title] : tools)
  {
    auto* button = new QToolButton{m_bar};
    button->setObjectName(QString{"ProjectTool%1"}.arg(static_cast<int>(id)));
    button->setProperty("tbControlRole", "navigation");
    button->setText(title);
    button->setCheckable(true);
    connect(button, &QToolButton::clicked, this, [this, id]() { selectTool(id); });
    barLayout->addWidget(button);
    m_buttons.emplace(id, button);
  }
  m_buttons.at(ProjectToolId::ContentBrowser)
    ->setToolTip(tr("Content Browser (Ctrl+Shift+Space)"));
  auto* extensions = new QToolButton{m_bar};
  extensions->setObjectName("ProjectToolsExtensions");
  extensions->setProperty("tbControlRole", "navigation");
  extensions->setText(tr("Extensions"));
  connect(extensions, &QToolButton::clicked, this, [this]() {
    if (!m_pin->isChecked())
      setExpanded(false);
    emit extensionsRequested();
  });
  barLayout->addWidget(extensions);
  barLayout->addStretch();
  auto* layout = new QVBoxLayout{this};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(m_surface, 1);
  layout->addWidget(m_bar);

  connect(close, &QToolButton::clicked, this, [this]() { setExpanded(false); });
  connect(m_pin, &QToolButton::toggled, this, [this](const bool pinned) {
    QSettings{}.setValue("ProjectTools/Pinned", pinned);
    // Reparenting produces intermediate focus changes outside the drawer.
    const auto changingMode = QScopedValueRollback{m_changingMode, true};
    const auto focus = QPointer<QWidget>{QApplication::focusWidget()};
    updateGeometryForMode();
    // Reparenting between the splitter and overlay clears Qt's keyboard focus.
    if (m_expanded)
    {
      if (focus && (focus == m_panel || m_panel->isAncestorOf(focus)))
        focus->setFocus(Qt::OtherFocusReason);
      else
        m_pages->currentWidget()->setFocus(Qt::OtherFocusReason);
    }
  });
  auto* shortcut =
    new QShortcut{QKeySequence{Qt::CTRL | Qt::SHIFT | Qt::Key_Space}, this};
  connect(shortcut, &QShortcut::activated, this, [this]() {
    selectTool(ProjectToolId::ContentBrowser);
  });
  auto* escape = new QShortcut{QKeySequence{Qt::Key_Escape}, m_panel};
  escape->setContext(Qt::WidgetWithChildrenShortcut);
  connect(escape, &QShortcut::activated, this, [this]() { setExpanded(false); });
  connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* focus) {
    if (
      !m_expanded || m_changingMode || m_pin->isChecked() || !focus
      || QApplication::activePopupWidget() || QApplication::activeModalWidget())
      return;
    if (
      focus == m_panel || m_panel->isAncestorOf(focus) || focus == m_bar
      || m_bar->isAncestorOf(focus))
      return;
    // Dialogs and menus opened by a tool keep the drawer and its selection intact.
    if (focus->window() == window())
      setExpanded(false, false);
  });
  setMapEditor(nullptr);
  setExpanded(m_pin->isChecked());
}

void ProjectToolPanel::registerMapEditor(MapEditorView& editor)
{
  if (m_mapPanels.contains(&editor))
    return;
  editor.useExternalInfoPanel();
  auto* panel = editor.infoPanel();
  m_pages->addWidget(panel);
  m_mapPanels.emplace(&editor, panel);
}

void ProjectToolPanel::unregisterMapEditor(MapEditorView& editor)
{
  const auto it = m_mapPanels.find(&editor);
  if (it == m_mapPanels.end())
    return;
  if (m_activeEditor == &editor)
    setMapEditor(nullptr);
  auto* panel = it->second;
  m_mapPanels.erase(it);
  m_pages->removeWidget(panel);
  delete panel;
}

void ProjectToolPanel::setMapEditor(MapEditorView* editor)
{
  if (m_activeEditor && m_activeEditor != editor)
    m_activeEditor->setExternalInfoPanelExpanded(false);
  m_activeEditor = editor;
  m_mapPanel = editor ? m_mapPanels.at(editor) : nullptr;
  for (const auto& [id, button] : m_buttons)
    button->setEnabled(id == ProjectToolId::ContentBrowser || editor != nullptr);
  findChild<QToolButton*>("ProjectToolsExtensions")->setEnabled(editor != nullptr);
  if (!editor)
    m_tool = ProjectToolId::ContentBrowser;
  updatePage();
}

void ProjectToolPanel::selectTool(const ProjectToolId tool)
{
  if (tool == m_tool && m_expanded)
    setExpanded(false);
  else
  {
    m_tool = tool;
    setExpanded(true);
  }
}

void ProjectToolPanel::showContent()
{
  m_tool = ProjectToolId::ContentBrowser;
  setExpanded(true);
}

void ProjectToolPanel::showInfoPage(const InfoPanelPage page)
{
  if (page == InfoPanelPage::Assets)
    showContent();
  else if (m_mapPanel)
  {
    switch (page)
    {
    case InfoPanelPage::Console:
      m_tool = ProjectToolId::OutputLog;
      break;
    case InfoPanelPage::PythonConsole:
      m_tool = ProjectToolId::PythonConsole;
      break;
    case InfoPanelPage::Issues:
      m_tool = ProjectToolId::Issues;
      break;
    case InfoPanelPage::Assets:
      break;
    }
    setExpanded(true);
  }
}

void ProjectToolPanel::toggle()
{
  setExpanded(!m_expanded);
}

bool ProjectToolPanel::isExpanded() const
{
  return m_expanded;
}

void ProjectToolPanel::setExpanded(const bool expanded, const bool restoreFocus)
{
  const auto wasExpanded = m_expanded;
  if (expanded && !wasExpanded)
  {
    auto* focus = QApplication::focusWidget();
    if (
      focus && focus->window() == window() && focus != m_panel
      && !m_panel->isAncestorOf(focus) && !m_bar->isAncestorOf(focus))
      m_restoreFocus = focus;
  }
  m_expanded = expanded;
  updatePage();
  updateGeometryForMode();
  if (expanded)
    m_pages->currentWidget()->setFocus(Qt::ShortcutFocusReason);
  else if (wasExpanded && restoreFocus)
  {
    if (m_restoreFocus && m_restoreFocus->isVisible() && m_restoreFocus->isEnabled())
      m_restoreFocus->setFocus(Qt::ShortcutFocusReason);
    else if (m_activeEditor)
      m_activeEditor->currentMapViewBase()->setFocus(Qt::ShortcutFocusReason);
    else
      m_workspace->setFocus(Qt::ShortcutFocusReason);
  }
  if (!expanded)
    m_restoreFocus.clear();
}

ProjectToolId ProjectToolPanel::currentTool() const
{
  return m_tool;
}

bool ProjectToolPanel::isToolVisible(const ProjectToolId tool) const
{
  return m_expanded && m_tool == tool;
}

void ProjectToolPanel::updatePage()
{
  if (m_tool != ProjectToolId::ContentBrowser && m_mapPanel)
  {
    switch (m_tool)
    {
    case ProjectToolId::OutputLog:
      m_mapPanel->switchToPage(InfoPanelPage::Console);
      break;
    case ProjectToolId::PythonConsole:
      m_mapPanel->switchToPage(InfoPanelPage::PythonConsole);
      break;
    case ProjectToolId::Issues:
      m_mapPanel->switchToPage(InfoPanelPage::Issues);
      break;
    case ProjectToolId::ContentBrowser:
      break;
    }
    m_pages->setCurrentWidget(m_mapPanel);
  }
  else
    m_pages->setCurrentWidget(m_content);
  for (const auto& [id, button] : m_buttons)
    button->setChecked(isToolVisible(id));
  m_title->setText(m_buttons.at(m_tool)->text());
  if (m_activeEditor)
    m_activeEditor->setExternalInfoPanelExpanded(m_expanded);
}

void ProjectToolPanel::updateGeometryForMode()
{
  const auto pinned = m_pin->isChecked();
  if (pinned && m_panel->parentWidget() != m_splitter)
  {
    m_splitter->addWidget(m_panel);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 0);
    m_splitter->setSizes({std::max(100, m_surface->height() - 320), 320});
  }
  else if (!pinned)
  {
    if (m_panel->parentWidget() != m_surface)
      m_panel->setParent(m_surface);
    const auto height = std::min(360, m_surface->height());
    m_panel->setGeometry(0, m_surface->height() - height, m_surface->width(), height);
    m_panel->raise();
  }
  m_panel->setVisible(m_expanded);
}

bool ProjectToolPanel::eventFilter(QObject* watched, QEvent* event)
{
  if (watched == m_surface && event->type() == QEvent::Resize)
    updateGeometryForMode();
  return QWidget::eventFilter(watched, event);
}
} // namespace tb::ui
