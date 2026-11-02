/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/WorkspaceToolPanel.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QScopedValueRollback>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantAnimation>

#include "ui/ImageUtils.h"
#include "ui/MapEditorView.h"
#include "ui/MapViewBase.h"

#include <algorithm>

namespace tb::ui
{
namespace
{
const auto ToolNames = std::map<WorkspaceToolId, QString>{
  {WorkspaceToolId::ContentBrowser, "content-browser"},
  {WorkspaceToolId::OutputLog, "output-log"},
  {WorkspaceToolId::PythonConsole, "python-console"},
  {WorkspaceToolId::Issues, "issues"}};
}

WorkspaceToolPanel::WorkspaceToolPanel(
  const WorkspaceKind kind, QWidget* workspace, QWidget* content, QWidget* parent)
  : QWidget{parent}
  , m_content{content}
  , m_workspace{workspace}
  , m_kind{kind}
{
  auto settings = QSettings{};
  m_layout = loadToolLayout(settings, m_kind);
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
  // The frame clips a stable body while its visible height slides open or closed.
  m_body = new QWidget{m_panel};
  m_panel->installEventFilter(this);
  m_resizeHandle = new QWidget{m_panel};
  m_resizeHandle->setObjectName("WorkspaceDrawerResize");
  m_resizeHandle->setAttribute(Qt::WA_StyledBackground);
  m_resizeHandle->setCursor(Qt::SizeVerCursor);
  m_resizeHandle->setToolTip(tr("Drag to resize the tool panel"));
  m_resizeHandle->installEventFilter(this);
  auto* grip = new QWidget{m_resizeHandle};
  grip->setObjectName("WorkspaceDrawerGrip");
  grip->setAttribute(Qt::WA_StyledBackground);
  grip->setAttribute(Qt::WA_TransparentForMouseEvents);
  grip->setFixedSize(38, 2);
  auto* gripLayout = new QHBoxLayout{m_resizeHandle};
  gripLayout->setContentsMargins(0, 2, 0, 2);
  gripLayout->addWidget(grip, 0, Qt::AlignCenter);
  auto* panelLayout = new QVBoxLayout{m_body};
  panelLayout->setContentsMargins(0, 0, 0, 0);
  panelLayout->setSpacing(0);
  m_pages = new QStackedWidget{m_body};
  m_pages->addWidget(content);
  panelLayout->addWidget(m_pages, 1);

  m_bar = new QWidget{m_body};
  m_bar->setObjectName("ProjectToolBar");
  m_bar->setAttribute(Qt::WA_StyledBackground);
  auto* barLayout = new QHBoxLayout{m_bar};
  barLayout->setContentsMargins(6, 1, 6, 1);
  barLayout->setSpacing(2);
  const auto tools = std::map<WorkspaceToolId, QString>{
    {WorkspaceToolId::ContentBrowser, tr("Content Browser")},
    {WorkspaceToolId::OutputLog, tr("Output Log")},
    {WorkspaceToolId::PythonConsole, tr("Python Console")},
    {WorkspaceToolId::Issues, tr("Issues")}};
  for (const auto& [id, title] : tools)
  {
    auto* button = new QToolButton{m_bar};
    button->setObjectName(QString{"ProjectTool%1"}.arg(static_cast<int>(id)));
    button->setProperty("tbControlRole", "navigation");
    const auto icon = id == WorkspaceToolId::ContentBrowser  ? "BrowserFolderSmall.svg"
                      : id == WorkspaceToolId::Issues        ? "IssueBrowser.svg"
                      : id == WorkspaceToolId::PythonConsole ? "PythonConsole.svg"
                                                             : "OutputLog.svg";
    button->setIcon(loadSVGIcon(std::filesystem::path{icon}));
    button->setIconSize(QSize{16, 16});
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setText(title);
    button->setCheckable(true);
    connect(button, &QToolButton::clicked, this, [this, id]() { selectTool(id); });
    barLayout->addWidget(button);
    m_buttons.emplace(id, button);
  }
  m_buttons.at(WorkspaceToolId::ContentBrowser)
    ->setToolTip(tr("Content Browser (Ctrl+Shift+Space)"));
  auto* extensions = new QToolButton{m_bar};
  extensions->setObjectName("ProjectToolsExtensions");
  extensions->setProperty("tbControlRole", "navigation");
  extensions->setText(tr("Extensions"));
  extensions->setIcon(loadSVGIcon(std::filesystem::path{"InspectorPlugin.svg"}));
  extensions->setIconSize(QSize{16, 16});
  extensions->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  connect(extensions, &QToolButton::clicked, this, [this]() {
    if (m_mode->currentIndex() != 1)
      setExpanded(false);
    emit extensionsRequested();
  });
  barLayout->addWidget(extensions);
  m_contentControls = new QWidget{m_bar};
  auto* contentLayout = new QHBoxLayout{m_contentControls};
  contentLayout->setContentsMargins(8, 0, 0, 0);
  contentLayout->setSpacing(4);
  barLayout->addWidget(m_contentControls);
  barLayout->addStretch();
  m_mode = new QComboBox{m_bar};
  m_mode->setObjectName("ProjectToolsMode");
  m_mode->setProperty("tbControlRole", "compact");
  m_mode->setAccessibleName(tr("Tool panel mode"));
  m_mode->setToolTip(
    tr("Overlay closes when returning to the map; Push keeps the panel open"));
  m_mode->addItems({tr("Overlay"), tr("Push")});
  m_mode->setCurrentIndex(m_layout.pinned ? 1 : 0);
  barLayout->addWidget(m_mode);
  barLayout->addSpacing(4);
  m_keepOpen = new QCheckBox{tr("Keep open"), m_bar};
  m_keepOpen->setObjectName("ProjectToolsKeepOpen");
  m_keepOpen->setProperty("tbControlRole", "compact");
  m_keepOpen->setAccessibleName(tr("Keep tool panel open on blur"));
  m_keepOpen->setToolTip(
    tr("Keep the overlay open when returning to the map (Ctrl+Shift+Space to toggle)"));
  m_keepOpen->setChecked(m_layout.keepOpen);
  m_keepOpen->setEnabled(!m_layout.pinned);
  connect(m_keepOpen, &QCheckBox::toggled, this, [this](const bool checked) {
    m_layout.keepOpen = checked;
    saveLayout();
  });
  barLayout->addWidget(m_keepOpen);
  barLayout->addSpacing(4);
  m_fold = new QToolButton{m_bar};
  m_fold->setObjectName("ProjectToolsClose");
  m_fold->setProperty("tbControlRole", "compact");
  m_fold->setIconSize(QSize{16, 16});
  barLayout->addWidget(m_fold);

  m_footer = new QWidget{this};
  auto* footerLayout = new QVBoxLayout{m_footer};
  footerLayout->setContentsMargins(0, 0, 0, 0);
  footerLayout->setSpacing(0);
  panelLayout->insertWidget(0, m_bar);
  m_status = new QWidget{m_footer};
  m_status->setObjectName("WorkspaceStatusBar");
  m_status->setAttribute(Qt::WA_StyledBackground);
  auto* statusLayout = new QHBoxLayout{m_status};
  statusLayout->setContentsMargins(10, 2, 8, 2);
  statusLayout->setSpacing(6);
  footerLayout->addWidget(m_status);
  m_status->hide();
  auto* layout = new QVBoxLayout{this};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(m_surface, 1);
  layout->addWidget(m_footer);

  m_animation = new QVariantAnimation{this};
  m_animation->setObjectName("WorkspaceDrawerAnimation");
  m_animation->setEasingCurve(QEasingCurve::OutCubic);
  connect(
    m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
      applyDrawerHeight(value.toInt());
    });
  connect(m_animation, &QVariantAnimation::finished, this, [this]() {
    m_pages->setVisible(m_expanded);
    applyDrawerHeight(m_expanded ? expandedHeight() : collapsedHeight());
  });

  connect(m_fold, &QToolButton::clicked, this, &WorkspaceToolPanel::toggle);
  connect(m_mode, &QComboBox::currentIndexChanged, this, [this](const int index) {
    const auto pinned = index == 1;
    if (m_keepOpen)
      m_keepOpen->setEnabled(!pinned);
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
    selectTool(WorkspaceToolId::ContentBrowser);
  });
  auto* escape = new QShortcut{QKeySequence{Qt::Key_Escape}, m_panel};
  escape->setContext(Qt::WidgetWithChildrenShortcut);
  connect(escape, &QShortcut::activated, this, [this]() { setExpanded(false); });
  connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* focus) {
    if (
      !m_expanded || m_changingMode || (m_mode->currentIndex() == 1)
      || (m_keepOpen && m_keepOpen->isChecked()) || !focus
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

void WorkspaceToolPanel::addContentControl(QWidget* control)
{
  m_contentControls->layout()->addWidget(control);
}

void WorkspaceToolPanel::addStatusWidget(QWidget* status)
{
  static_cast<QHBoxLayout*>(m_status->layout())->addWidget(status, 1);
  m_status->show();
}

WorkspaceToolPanel::~WorkspaceToolPanel()
{
  saveLayout();
}

void WorkspaceToolPanel::showEvent(QShowEvent* event)
{
  QWidget::showEvent(event);
  if (m_layoutPending)
  {
    QTimer::singleShot(0, this, [this]() {
      if (!m_layoutPending || !isVisible())
        return;
      if (m_mode->currentIndex() == 1)
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

void WorkspaceToolPanel::captureHeight()
{
  if (
    m_layoutPending || isAnimating() || m_panel->parentWidget() != m_splitter
    || !m_expanded)
    return;
  const auto sizes = m_splitter->sizes();
  const auto total = sizes[0] + sizes[1];
  if (total > 0 && sizes[1] > 0)
    m_layout.heightRatio = std::clamp(static_cast<double>(sizes[1]) / total, 0.1, 0.8);
}

void WorkspaceToolPanel::saveLayout()
{
  if (m_restoringLayout)
    return;
  m_layout.tool = ToolNames.at(m_tool);
  m_layout.expanded = m_expanded;
  m_layout.pinned = (m_mode->currentIndex() == 1);
  m_layout.keepOpen = m_keepOpen ? m_keepOpen->isChecked() : m_layout.keepOpen;
  auto settings = QSettings{};
  saveToolLayout(settings, m_kind, m_layout);
}

void WorkspaceToolPanel::resetLayout()
{
  m_tool = WorkspaceToolId::ContentBrowser;
  m_pendingTool.clear();
  m_mode->setCurrentIndex(0);
  m_layout = ToolLayout{};
  if (m_keepOpen)
    m_keepOpen->setChecked(false);
  setExpanded(false);
  saveLayout();
}

void WorkspaceToolPanel::registerMapEditor(MapEditorView& editor)
{
  if (m_mapPanels.contains(&editor))
    return;
  editor.useExternalInfoPanel();
  auto* panel = editor.infoPanel();
  m_pages->addWidget(panel);
  m_mapPanels.emplace(&editor, panel);
}

void WorkspaceToolPanel::unregisterMapEditor(MapEditorView& editor)
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

void WorkspaceToolPanel::setMapEditor(MapEditorView* editor)
{
  if (m_activeEditor && m_activeEditor != editor)
    m_activeEditor->setExternalInfoPanelExpanded(false);
  m_activeEditor = editor;
  m_mapPanel = editor ? m_mapPanels.at(editor) : nullptr;
  for (const auto& [id, button] : m_buttons)
    button->setEnabled(id == WorkspaceToolId::ContentBrowser || editor != nullptr);
  findChild<QToolButton*>("ProjectToolsExtensions")->setEnabled(editor != nullptr);
  if (!editor)
    m_tool = WorkspaceToolId::ContentBrowser;
  else if (!m_pendingTool.isEmpty())
  {
    for (const auto& [id, name] : ToolNames)
      if (name == m_pendingTool)
        m_tool = id;
    m_pendingTool.clear();
  }
  updatePage();
}

void WorkspaceToolPanel::selectTool(const WorkspaceToolId tool)
{
  if (tool == m_tool && m_expanded)
    setExpanded(false);
  else
  {
    m_tool = tool;
    setExpanded(true);
  }
}

void WorkspaceToolPanel::showContent()
{
  m_tool = WorkspaceToolId::ContentBrowser;
  setExpanded(true);
}

void WorkspaceToolPanel::showInfoPage(const InfoPanelPage page)
{
  if (page == InfoPanelPage::Assets)
    showContent();
  else if (m_mapPanel)
  {
    switch (page)
    {
    case InfoPanelPage::Console:
      m_tool = WorkspaceToolId::OutputLog;
      break;
    case InfoPanelPage::PythonConsole:
      m_tool = WorkspaceToolId::PythonConsole;
      break;
    case InfoPanelPage::Issues:
      m_tool = WorkspaceToolId::Issues;
      break;
    case InfoPanelPage::Assets:
      break;
    }
    setExpanded(true);
  }
}

void WorkspaceToolPanel::toggle()
{
  setExpanded(!m_expanded);
}

bool WorkspaceToolPanel::isExpanded() const
{
  return m_expanded;
}

bool WorkspaceToolPanel::isAnimating() const
{
  return m_animation->state() == QAbstractAnimation::Running;
}

void WorkspaceToolPanel::setExpanded(const bool expanded, const bool restoreFocus)
{
  const auto changingMode = QScopedValueRollback{m_changingMode, true};
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
  const auto duration =
    style()->styleHint(QStyle::SH_Widget_Animation_Duration, nullptr, this);
  if (
    expanded != wasExpanded && isVisible() && !m_restoringLayout && !m_layoutPending
    && duration > 0)
  {
    const auto startHeight = m_panel->height();
    m_animation->stop();
    m_pages->show();
    m_animation->setDuration(360);
    m_animation->setStartValue(startHeight);
    m_animation->setEndValue(expanded ? expandedHeight() : collapsedHeight());
    m_animation->start();
  }
  else if (!isAnimating())
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

WorkspaceToolId WorkspaceToolPanel::currentTool() const
{
  return m_tool;
}

bool WorkspaceToolPanel::isToolVisible(const WorkspaceToolId tool) const
{
  return m_expanded && m_tool == tool;
}

void WorkspaceToolPanel::updatePage()
{
  if (m_tool != WorkspaceToolId::ContentBrowser && m_mapPanel)
  {
    switch (m_tool)
    {
    case WorkspaceToolId::OutputLog:
      m_mapPanel->switchToPage(InfoPanelPage::Console);
      break;
    case WorkspaceToolId::PythonConsole:
      m_mapPanel->switchToPage(InfoPanelPage::PythonConsole);
      break;
    case WorkspaceToolId::Issues:
      m_mapPanel->switchToPage(InfoPanelPage::Issues);
      break;
    case WorkspaceToolId::ContentBrowser:
      break;
    }
    m_pages->setCurrentWidget(m_mapPanel);
  }
  else
  {
    if (auto* panel = qobject_cast<InfoPanel*>(m_content.data()))
      panel->switchToPage(InfoPanelPage::Assets);
    if (m_content)
      m_pages->setCurrentWidget(m_content);
  }
  for (const auto& [id, button] : m_buttons)
    button->setChecked(isToolVisible(id));
  m_contentControls->setVisible(m_expanded && m_tool == WorkspaceToolId::ContentBrowser);
  m_fold->setIcon(loadSVGIcon(std::filesystem::path{m_expanded ? "Down.svg" : "Up.svg"}));
  m_fold->setToolTip(m_expanded ? tr("Collapse tool panel") : tr("Expand tool panel"));
  m_fold->setAccessibleName(m_fold->toolTip());
  if (m_activeEditor)
    m_activeEditor->setExternalInfoPanelExpanded(m_expanded);
}

void WorkspaceToolPanel::updateGeometryForMode()
{
  if (m_updatingGeometry)
    return;
  const auto updating = QScopedValueRollback{m_updatingGeometry, true};
  m_animation->stop();
  const auto pinned = m_mode->currentIndex() == 1;
  if (m_keepOpen)
    m_keepOpen->setEnabled(!pinned);
  // The collapsed strip is always covered by the drawer, including during motion.
  // Keeping this allocation constant prevents overlay mode from resizing the map.
  m_surface->layout()->setContentsMargins(0, 0, 0, !pinned ? collapsedHeight() : 0);
  m_panel->setMinimumHeight(collapsedHeight());
  if (pinned && m_panel->parentWidget() != m_splitter)
  {
    m_splitter->addWidget(m_panel);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 0);
  }
  else if (!pinned)
  {
    if (m_panel->parentWidget() != m_surface)
      m_panel->setParent(m_surface);
    m_panel->raise();
  }
  m_panel->show();
  m_pages->setVisible(m_expanded);
  applyDrawerHeight(m_expanded ? expandedHeight() : collapsedHeight());
}

int WorkspaceToolPanel::collapsedHeight() const
{
  return m_bar->sizeHint().height() + 2;
}

int WorkspaceToolPanel::expandedHeight() const
{
  return std::clamp(
    static_cast<int>(m_surface->height() * m_layout.heightRatio),
    std::min(
      m_surface->height(),
      collapsedHeight() + m_pages->currentWidget()->minimumSizeHint().height()),
    std::max(collapsedHeight(), m_surface->height()));
}

void WorkspaceToolPanel::applyDrawerHeight(const int height)
{
  const auto changingMode = QScopedValueRollback{m_changingMode, true};
  if (m_panel->parentWidget() == m_splitter)
    m_splitter->setSizes(
      {std::max(0, m_splitter->height() - m_splitter->handleWidth() - height), height});
  else
    m_panel->setGeometry(0, m_surface->height() - height, m_surface->width(), height);
  const auto bodyHeight =
    !m_pages->isHidden()
      ? (isAnimating() || !m_expanded ? std::max(expandedHeight(), m_panel->height())
                                      : m_panel->height())
      : collapsedHeight();
  m_body->setGeometry(
    1, 1, std::max(0, m_panel->width() - 2), std::max(0, bodyHeight - 2));
  m_body->layout()->activate();
  m_resizeHandle->setGeometry(1, 0, std::max(0, m_panel->width() - 2), 6);
  m_resizeHandle->setVisible(m_expanded);
  m_resizeHandle->raise();
}

bool WorkspaceToolPanel::eventFilter(QObject* watched, QEvent* event)
{
  if (!m_animation)
    return QWidget::eventFilter(watched, event);
  if (watched == m_resizeHandle)
  {
    if (event->type() == QEvent::MouseButtonPress)
    {
      auto* mouse = static_cast<QMouseEvent*>(event);
      if (mouse->button() == Qt::LeftButton && m_expanded)
      {
        m_animation->stop();
        m_resizing = true;
        m_resizeStartY = mouse->globalPosition().toPoint().y();
        m_resizeStartHeight = m_panel->height();
        m_resizeHandle->grabMouse();
        return true;
      }
    }
    else if (event->type() == QEvent::MouseMove && m_resizing)
    {
      const auto* mouse = static_cast<QMouseEvent*>(event);
      const auto height =
        m_resizeStartHeight + m_resizeStartY - mouse->globalPosition().toPoint().y();
      m_layout.heightRatio = std::clamp(
        static_cast<double>(height) / std::max(1, m_surface->height()), 0.15, 0.8);
      applyDrawerHeight(expandedHeight());
      return true;
    }
    else if (event->type() == QEvent::MouseButtonRelease && m_resizing)
    {
      m_resizing = false;
      m_resizeHandle->releaseMouse();
      saveLayout();
      return true;
    }
    else if (event->type() == QEvent::UngrabMouse)
    {
      m_resizing = false;
    }
  }
  if (watched == m_surface && event->type() == QEvent::Resize && !m_panel->isHidden())
    updateGeometryForMode();
  if (watched == m_panel && event->type() == QEvent::Resize && !m_updatingGeometry)
  {
    const auto bodyHeight =
      !m_pages->isHidden()
        ? (isAnimating() || !m_expanded ? std::max(expandedHeight(), m_panel->height())
                                        : m_panel->height())
        : collapsedHeight();
    m_body->setGeometry(
      1, 1, std::max(0, m_panel->width() - 2), std::max(0, bodyHeight - 2));
    m_resizeHandle->setGeometry(1, 0, std::max(0, m_panel->width() - 2), 6);
  }
  return QWidget::eventFilter(watched, event);
}
} // namespace tb::ui
