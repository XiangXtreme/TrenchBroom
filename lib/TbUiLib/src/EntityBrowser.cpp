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

#include "ui/EntityBrowser.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QVBoxLayout>

#include "mdl/EntityDefinition.h"
#include "mdl/EntityDefinitionManager.h"
#include "mdl/EntityDefinitionUtils.h"
#include "mdl/Map.h"
#include "mdl/Map_Selection.h"
#include "mdl/WorldNode.h"
#include "ui/EntityBrowserView.h"
#include "ui/EntityDefinitionFileChooser.h"
#include "ui/MapDocument.h"

namespace tb::ui
{
EntityBrowser::EntityBrowser(
  AppController& appController, MapDocument& document, QWidget* parent)
  : QWidget{parent}
  , m_document{&document}
  , m_appController{appController}
{
  setObjectName("EntityBrowser");
  auto* column = new QVBoxLayout{this};
  column->setContentsMargins(0, 0, 0, 0);
  column->setSpacing(0);
  createGui();
  connectObservers();
  reload();
}

void EntityBrowser::setDocument(MapDocument& document)
{
  if (m_document == &document)
    return;
  const auto folder = m_view->folder();
  const auto filter = m_view->filterText();
  const auto sort = m_view->sortOrder();
  const auto group = m_view->group();
  const auto used = m_view->hideUnused();
  m_notifierConnection.disconnect();
  delete m_content;
  m_document = &document;
  createGui();
  m_view->setFolder(folder);
  m_view->setFilterText(filter);
  m_view->setSortOrder(sort);
  m_view->setGroup(group);
  m_view->setHideUnused(used);
  connectObservers();
  reload();
}

EntityBrowserView& EntityBrowser::view() const
{
  return *m_view;
}

void EntityBrowser::setMapActionsEnabled(const bool enabled)
{
  m_mapActionsEnabled = enabled;
  m_view->setMapActionsEnabled(enabled);
  refreshSelection();
}

void EntityBrowser::reload()
{
  m_view->setDefaultModelScaleExpression(
    m_document->map().worldNode().entityPropertyConfig().defaultModelScaleExpression);
  m_view->refreshDefinitions();
}

void EntityBrowser::createGui()
{
  m_content = new QWidget{this};
  auto* column = new QVBoxLayout{m_content};
  column->setContentsMargins(0, 0, 0, 0);
  column->setSpacing(0);
  layout()->addWidget(m_content);
  auto* canvas = new QHBoxLayout{};
  canvas->setContentsMargins(0, 0, 0, 0);
  canvas->setSpacing(0);
  m_scrollBar = new QScrollBar{Qt::Vertical};
  m_view = new EntityBrowserView{m_appController, m_scrollBar, *m_document};
  m_view->setObjectName("EntityBrowser_Grid");
  m_view->setMapActionsEnabled(m_mapActionsEnabled);
  canvas->addWidget(m_view, 1);
  canvas->addWidget(m_scrollBar);
  column->addLayout(canvas, 1);
  auto* footer = new QHBoxLayout{};
  footer->setContentsMargins(8, 4, 8, 4);
  footer->setSpacing(8);
  m_summary = new QLabel{};
  m_summary->setObjectName("EntityBrowser_Summary");
  m_summary->setTextFormat(Qt::PlainText);
  m_summary->setWordWrap(true);
  m_summary->setMaximumHeight(44);
  m_summary->setMinimumWidth(0);
  m_summary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  footer->addWidget(m_summary, 1);
  m_create = new QPushButton{};
  m_create->setObjectName("EntityBrowser_Create");
  footer->addWidget(m_create);
  column->addLayout(footer);
  setFocusProxy(m_view);
  connect(
    m_view,
    &EntityBrowserView::definitionSelected,
    this,
    &EntityBrowser::refreshSelection);
  connect(m_view, &EntityBrowserView::definitionsChanged, this, [this] {
    refreshSelection();
    emit contentsChanged();
  });
  connect(m_view, &EntityBrowserView::definitionActivated, m_create, &QPushButton::click);
  connect(m_create, &QPushButton::clicked, this, [this] {
    refreshSelection();
    if (m_create->isEnabled())
      emit createEntityRequested(
        QString::fromStdString(m_view->selectedDefinitionName()));
  });
}

void EntityBrowser::refreshSelection()
{
  const auto* definition = m_document->map().entityDefinitionManager().definition(
    m_view->selectedDefinitionName());
  const auto point = definition && definition->pointEntityDefinition.has_value();
  const auto summary =
    definition
      ? QString::fromStdString(definition->name + " — " + definition->description)
      : tr("Select an entity type. Drag a point entity into a map view to place it.");
  m_summary->setText(summary.simplified());
  m_summary->setToolTip(summary);
  m_create->setText(
    !definition || point ? tr("Create in current view")
                         : tr("Turn selection into entity"));
  m_create->setEnabled(
    m_mapActionsEnabled && definition
    && (point || m_document->map().selection().hasOnlyGeometryNodes()));
  m_create->setToolTip(
    !m_mapActionsEnabled ? tr("Open a map to create entities")
    : point
      ? tr(
          "Create at the current view position, or drag the type into a view to place it")
      : tr("Select brushes, then convert them to this entity type"));
}

void EntityBrowser::showSettings()
{
  if (!m_mapActionsEnabled)
    return;
  if (auto* existing = m_content->findChild<QDialog*>("EntityBrowser_DefinitionSettings"))
  {
    existing->show();
    existing->raise();
    existing->activateWindow();
    return;
  }
  auto* dialog = new QDialog{m_content};
  dialog->setObjectName("EntityBrowser_DefinitionSettings");
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(tr("Entity Definitions"));
  dialog->resize(560, 400);
  auto* settingsLayout = new QVBoxLayout{dialog};
  settingsLayout->addWidget(new EntityDefinitionFileChooser{*m_document, dialog});
  auto* buttons = new QDialogButtonBox{QDialogButtonBox::Close};
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  settingsLayout->addWidget(buttons);
  dialog->show();
}

void EntityBrowser::connectObservers()
{
  m_notifierConnection +=
    m_document->documentWasLoadedNotifier.connect([this] { reload(); });
  m_notifierConnection +=
    m_document->documentDidChangeNotifier.connect([this] { reload(); });
  m_notifierConnection +=
    m_document->entityDefinitionsDidChangeNotifier.connect([this] { reload(); });
  const auto updateUsage = [this](const auto&) {
    if (
      m_view->hideUnused()
      || m_view->sortOrder() == mdl::EntityDefinitionSortOrder::Usage)
      m_view->refreshDefinitions();
  };
  m_notifierConnection += m_document->commandDoneNotifier.connect(updateUsage);
  m_notifierConnection += m_document->commandUndoneNotifier.connect(updateUsage);
  m_notifierConnection += m_document->map().selectionDidChangeNotifier.connect(
    [this](const auto&) { refreshSelection(); });
}
} // namespace tb::ui
