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
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace tb::ui
{
ProjectToolPanel::ProjectToolPanel(QWidget* workspace, QWidget* content, QWidget* parent)
  : QWidget{parent}
  , m_content{content}
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
  const auto titles = QStringList{
    tr("Content Browser"), tr("Output Log"), tr("Python Console"), tr("Issues")};
  for (int i = 0; i < titles.size(); ++i)
  {
    auto* button = new QToolButton{m_bar};
    button->setObjectName(QString{"ProjectTool%1"}.arg(i));
    button->setProperty("tbControlRole", "navigation");
    button->setText(titles[i]);
    button->setCheckable(true);
    connect(button, &QToolButton::clicked, this, [this, i]() { selectTool(i); });
    barLayout->addWidget(button);
    m_buttons.push_back(button);
  }
  m_buttons[0]->setToolTip(tr("Content Browser (Ctrl+Shift+Space)"));
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
    updateGeometryForMode();
    // Reparenting between the splitter and overlay clears Qt's keyboard focus.
    if (m_expanded)
      m_pages->currentWidget()->setFocus(Qt::OtherFocusReason);
  });
  auto* shortcut =
    new QShortcut{QKeySequence{Qt::CTRL | Qt::SHIFT | Qt::Key_Space}, this};
  connect(shortcut, &QShortcut::activated, this, [this]() { selectTool(0); });
  auto* escape = new QShortcut{QKeySequence{Qt::Key_Escape}, m_panel};
  escape->setContext(Qt::WidgetWithChildrenShortcut);
  connect(escape, &QShortcut::activated, this, [this]() { setExpanded(false); });
  connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* focus) {
    if (
      !m_expanded || m_pin->isChecked() || !focus || QApplication::activePopupWidget()
      || QApplication::activeModalWidget())
      return;
    if (
      focus == m_panel || m_panel->isAncestorOf(focus) || focus == m_bar
      || m_bar->isAncestorOf(focus))
      return;
    // Dialogs and menus opened by a tool keep the drawer and its selection intact.
    if (focus->window() == window())
      setExpanded(false);
  });
  setMapPanel(nullptr);
  setExpanded(m_pin->isChecked());
}

void ProjectToolPanel::setMapPanel(InfoPanel* panel)
{
  m_mapPanel = panel;
  if (panel && m_pages->indexOf(panel) < 0)
    m_pages->addWidget(panel);
  for (size_t i = 1; i < m_buttons.size(); ++i)
    m_buttons[i]->setEnabled(panel != nullptr);
  findChild<QToolButton*>("ProjectToolsExtensions")->setEnabled(panel != nullptr);
  if (!panel)
    m_tool = 0;
  updatePage();
}

void ProjectToolPanel::selectTool(const int tool)
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
  m_tool = 0;
  setExpanded(true);
}

void ProjectToolPanel::showInfoPage(const InfoPanelPage page)
{
  if (page == InfoPanelPage::Assets)
    showContent();
  else if (m_mapPanel)
  {
    m_tool = static_cast<int>(page) + 1;
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

void ProjectToolPanel::setExpanded(const bool expanded)
{
  m_expanded = expanded;
  updatePage();
  updateGeometryForMode();
  if (expanded)
    m_pages->currentWidget()->setFocus(Qt::ShortcutFocusReason);
}

void ProjectToolPanel::updatePage()
{
  if (m_tool != 0 && m_mapPanel)
  {
    m_mapPanel->switchToPage(static_cast<InfoPanelPage>(m_tool - 1));
    m_pages->setCurrentWidget(m_mapPanel);
  }
  else
    m_pages->setCurrentWidget(m_content);
  for (size_t i = 0; i < m_buttons.size(); ++i)
    m_buttons[i]->setChecked(m_expanded && int(i) == m_tool);
  m_title->setText(m_buttons[size_t(m_tool)]->text());
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
