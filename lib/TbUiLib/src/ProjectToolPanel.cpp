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
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "ui/MapEditorView.h"
#include "ui/MapViewBase.h"

#include <algorithm>

namespace tb::ui
{
namespace
{
const auto ToolNames = std::map<ProjectToolId, QString>{
  {ProjectToolId::ContentBrowser, "content-browser"},
  {ProjectToolId::OutputLog, "output-log"},
  {ProjectToolId::PythonConsole, "python-console"},
  {ProjectToolId::Issues, "issues"}};
}

ProjectToolPanel::ProjectToolPanel(QWidget* workspace, QWidget* content, QWidget* parent)
  : QWidget{parent}
  , m_content{content}
  , m_workspace{workspace}
{
  auto settings = QSettings{};
  m_layout = loadToolLayout(settings, WorkspaceKind::Project);
  m_pendingTool = m_layout.tool;
  setObjectName("ProjectTools");
  m_surface = new QWidget{this};
  m_surface->installEventFilter(this);
  auto* surfaceLayout = new QVBoxLayout{m_surface};
  surfaceLayout->setContentsMargins(0, 0, 0, 0);
  m_splitter = new QSplitter{Qt::Vertical, m_surface};
  m_splitter->setObjectName("ProjectToolsSplitter");
  m_splitter->setChildrenCollapsible(false);
  connect(m_splitter, &QSplitter::splitterMoved, this, [this]() {
    captureHeight();
    saveLayout();
  });
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
  m_pin->setChecked(m_layout.pinned);
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
    if (!pinned)
      captureHeight();
    // Reparenting produces intermediate focus changes outside the drawer.
    const auto changingMode = QScopedValueRollback{m_changingMode, true};
    const auto focus = QPointer<QWidget>{QApplication::focusWidget()};
    updateGeometryForMode();
    saveLayout();
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
  setExpanded(m_layout.expanded);
  m_restoringLayout = false;
}

void ProjectToolPanel::addStatusWidget(QWidget* status)
{
  auto* layout = static_cast<QHBoxLayout*>(m_bar->layout());
  delete layout->takeAt(layout->count() - 1);
  layout->addWidget(status, 1);
}

ProjectToolPanel::~ProjectToolPanel()
{
  saveLayout();
}

void ProjectToolPanel::showEvent(QShowEvent* event)
{
  QWidget::showEvent(event);
  if (m_layoutPending)
  {
    QTimer::singleShot(0, this, [this]() {
      if (!m_layoutPending || !isVisible())
        return;
      if (m_pin->isChecked())
      {
        const auto height = m_splitter->height();
        const auto panelHeight = static_cast<int>(height * m_layout.heightRatio);
        m_splitter->setSizes({height - panelHeight, panelHeight});
      }
      updateGeometryForMode();
      m_layoutPending = false;
    });
  }
}

void ProjectToolPanel::captureHeight()
{
  if (m_layoutPending || m_panel->parentWidget() != m_splitter || !m_expanded)
    return;
  const auto sizes = m_splitter->sizes();
  const auto total = sizes[0] + sizes[1];
  if (total > 0 && sizes[1] > 0)
    m_layout.heightRatio = std::clamp(static_cast<double>(sizes[1]) / total, 0.1, 0.8);
}

void ProjectToolPanel::saveLayout()
{
  if (m_restoringLayout)
    return;
  m_layout.tool = ToolNames.at(m_tool);
  m_layout.expanded = m_expanded;
  m_layout.pinned = m_pin->isChecked();
  auto settings = QSettings{};
  saveToolLayout(settings, WorkspaceKind::Project, m_layout);
}

void ProjectToolPanel::resetLayout()
{
  m_tool = ProjectToolId::ContentBrowser;
  m_pendingTool.clear();
  m_pin->setChecked(false);
  m_layout = ToolLayout{};
  setExpanded(false);
  saveLayout();
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
  else if (!m_pendingTool.isEmpty())
  {
    for (const auto& [id, name] : ToolNames)
      if (name == m_pendingTool)
        m_tool = id;
    m_pendingTool.clear();
  }
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
  if (!expanded)
    captureHeight();
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
  saveLayout();
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
    const auto panelHeight = static_cast<int>(m_surface->height() * m_layout.heightRatio);
    m_splitter->setSizes({std::max(100, m_surface->height() - panelHeight), panelHeight});
  }
  else if (!pinned)
  {
    if (m_panel->parentWidget() != m_surface)
      m_panel->setParent(m_surface);
    const auto height = std::min(
      std::max(100, static_cast<int>(m_surface->height() * m_layout.heightRatio)),
      m_surface->height());
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
