/*
 Copyright (C) 2010 Kristian Duske
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/MapStatus.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>
#include <QTimer>
#include <QToolButton>

#include "mdl/BrushNode.h"
#include "mdl/EditorContext.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityNodeBase.h"
#include "mdl/GameInfo.h"
#include "mdl/Grid.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "mdl/ModelUtils.h"
#include "mdl/PatchNode.h"
#include "mdl/WorldNode.h"
#include "ui/ElidedLabel.h"
#include "ui/MapDocument.h"

#include "kd/overload.h"
#include "kd/string_format.h"
#include "kd/string_utils.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace tb::ui
{
namespace
{
template <typename T>
const mdl::EntityNodeBase* commonEntityForNodeList(const std::vector<T*>& nodes)
{
  return !nodes.empty()
             && std::ranges::all_of(
               nodes,
               [&](const auto* node) {
                 return node->entity() == nodes.front()->entity();
               })
           ? nodes.front()->entity()
           : nullptr;
}

std::optional<std::string> commonClassnameForEntityList(
  const std::vector<mdl::EntityNode*>& nodes)
{
  return !nodes.empty()
             && std::ranges::all_of(
               nodes,
               [&](const auto* entityNode) {
                 return entityNode->entity().classname()
                        == nodes.front()->entity().classname();
               })
           ? std::optional{nodes.front()->entity().classname()}
           : std::nullopt;
}

std::string numberWithSuffix(
  size_t count, const std::string& singular, const std::string& plural)
{
  return std::to_string(count) + " " + kdl::str_plural(count, singular, plural);
}

QString describeSelection(const mdl::Map& map)
{
  const auto& editorContext = map.editorContext();

  const auto Arrow = QString(" ") + QString(QChar(0x203A)) + QString(" ");

  auto pipeSeparatedSections = QStringList{};

  pipeSeparatedSections << QString::fromStdString(map.gameInfo().gameConfig.name)
                        << QString::fromStdString(
                             mdl::formatName(map.worldNode().mapFormat()))
                        << QString::fromStdString(editorContext.currentLayer()->name());

  // open groups
  auto groups = std::vector<mdl::GroupNode*>{};
  for (auto* group = editorContext.currentGroup(); group != nullptr;
       group = group->containingGroup())
  {
    groups.push_back(group);
  }

  if (!groups.empty())
  {
    auto openGroups = QStringList{};

    // groups vector is sorted from innermost to outermost, so visit it in reverse order
    for (auto it = groups.rbegin(); it != groups.rend(); ++it)
    {
      const auto* group = *it;
      openGroups << QString::fromStdString(group->name());
    }

    const auto openGroupsString =
      QObject::tr("Open groups: %1").arg(openGroups.join(Arrow));
    pipeSeparatedSections << openGroupsString;
  }

  // build a vector of strings describing the things that are selected
  auto tokens = std::vector<std::string>{};

  const auto& selection = map.selection();

  // selected brushes
  if (selection.hasBrushes())
  {
    const auto* commonEntityNode = commonEntityForNodeList(selection.brushes);

    // if all selected brushes are from the same entity, print the entity name
    auto token = numberWithSuffix(selection.brushes.size(), "brush", "brushes");
    token += commonEntityNode ? " (" + commonEntityNode->entity().classname() + ")"
                              : " (multiple entities)";
    tokens.push_back(token);
  }

  // selected patches
  if (selection.hasPatches())
  {
    const auto* commonEntityNode = commonEntityForNodeList(selection.patches);

    // if all selected patches are from the same entity, print the entity name
    auto token = numberWithSuffix(selection.patches.size(), "patch", "patches");
    token += commonEntityNode ? " (" + commonEntityNode->entity().classname() + ")"
                              : " (multiple entities)";
    tokens.push_back(token);
  }

  // selected brush faces
  if (map.selection().hasBrushFaces())
  {
    const auto token =
      numberWithSuffix(map.selection().brushFaces.size(), "face", "faces");
    tokens.push_back(token);
  }

  // entities
  if (selection.hasEntities())
  {
    const auto commonClassname = commonClassnameForEntityList(selection.entities);

    auto token = numberWithSuffix(selection.entities.size(), "entity", "entities");
    token += " (" + commonClassname.value_or("multiple classnames") + ") ";
    tokens.push_back(token);
  }

  // groups
  if (selection.hasGroups())
  {
    tokens.push_back(numberWithSuffix(selection.groups.size(), "group", "groups"));
  }

  // get the layers of the selected nodes
  const auto selectedObjectLayers =
    mdl::collectContainingLayersUserSorted(selection.nodes);
  auto layersDescription = QString{};
  if (selectedObjectLayers.size() == 1)
  {
    auto* layer = selectedObjectLayers.front();
    layersDescription =
      QObject::tr(" in layer \"%1\"").arg(QString::fromStdString(layer->name()));
  }
  else if (selectedObjectLayers.size() > 1)
  {
    layersDescription = QObject::tr(" in %1 layers").arg(selectedObjectLayers.size());
  }

  // now, turn `tokens` into a comma-separated string
  if (!tokens.empty())
  {
    pipeSeparatedSections << QObject::tr("%1%2 selected")
                               .arg(QString::fromStdString(
                                 kdl::str_join(tokens, ", ", ", and ", " and ")))
                               .arg(layersDescription);
  }

  // count hidden objects
  size_t hiddenGroups = 0u;
  size_t hiddenEntities = 0u;
  size_t hiddenBrushes = 0u;
  size_t hiddenPatches = 0u;

  map.worldNode().accept(kdl::overload(
    [](auto&& thisLambda, const mdl::WorldNode& worldNode) {
      worldNode.visitChildren(thisLambda);
    },
    [](auto&& thisLambda, const mdl::LayerNode& layerNode) {
      layerNode.visitChildren(thisLambda);
    },
    [&](auto&& thisLambda, const mdl::GroupNode& groupNode) {
      if (!editorContext.visible(groupNode))
      {
        ++hiddenGroups;
      }
      groupNode.visitChildren(thisLambda);
    },
    [&](auto&& thisLambda, const mdl::EntityNode& entityNode) {
      if (!editorContext.visible(entityNode))
      {
        ++hiddenEntities;
      }
      entityNode.visitChildren(thisLambda);
    },
    [&](const mdl::BrushNode& brushNode) {
      if (!editorContext.visible(brushNode))
      {
        ++hiddenBrushes;
      }
    },
    [&](const mdl::PatchNode& patchNode) {
      if (!editorContext.visible(patchNode))
      {
        ++hiddenPatches;
      }
    }));

  // print hidden objects
  if (hiddenGroups > 0 || hiddenEntities > 0 || hiddenBrushes > 0 || hiddenPatches > 0)
  {
    auto hiddenDescriptors = std::vector<std::string>{};

    if (hiddenGroups > 0)
    {
      hiddenDescriptors.push_back(numberWithSuffix(hiddenGroups, "group", "groups"));
    }
    if (hiddenEntities > 0)
    {
      hiddenDescriptors.push_back(numberWithSuffix(hiddenEntities, "entity", "entities"));
    }
    if (hiddenBrushes > 0)
    {
      hiddenDescriptors.push_back(numberWithSuffix(hiddenBrushes, "brush", "brushes"));
    }
    if (hiddenPatches > 0)
    {
      hiddenDescriptors.push_back(numberWithSuffix(hiddenPatches, "patch", "patches"));
    }

    pipeSeparatedSections << QObject::tr("%1 hidden")
                               .arg(QString::fromStdString(kdl::str_join(
                                 hiddenDescriptors, ", ", ", and ", " and ")));
  }

  return pipeSeparatedSections.join(QLatin1String("   |   "));
}

} // namespace


MapStatus::MapStatus(QWidget* parent)
  : QWidget{parent}
{
  setObjectName("MapStatus");
  auto* layout = new QHBoxLayout{this};
  layout->setContentsMargins(0, 0, 0, 0);
  m_label = new ElidedLabel{Qt::ElideRight, this};
  m_label->setObjectName("MapWindow_StatusLabel");
  m_label->setMinimumWidth(0);
  m_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  m_grid = new QComboBox{this};
  m_grid->setObjectName("MapWindow_GridChoice");
  m_grid->setProperty("tbControlRole", "compact");
  m_grid->setToolTip(tr("Grid size"));
  for (int i = mdl::Grid::MinSize; i <= mdl::Grid::MaxSize; ++i)
    m_grid->addItem(tr("Grid %1").arg(QString::number(mdl::Grid::actualSize(i), 'g')), i);
  m_snap = new QToolButton{this};
  m_snap->setObjectName("MapWindow_SnapToggle");
  m_snap->setProperty("tbControlRole", "compact");
  m_snap->setText(tr("Snap"));
  m_snap->setToolTip(tr("Snap to grid"));
  m_snap->setCheckable(true);
  layout->addWidget(m_label, 1);
  layout->setSpacing(6);
  m_saveState = new QLabel{this};
  m_saveState->setObjectName("MapWindow_SaveState");
  layout->addWidget(m_saveState);
  layout->addSpacing(8);
  layout->addWidget(m_grid);
  layout->addWidget(m_snap);
  m_refreshTimer = new QTimer{this};
  m_refreshTimer->setSingleShot(true);
  m_refreshTimer->setInterval(100);
  connect(m_refreshTimer, &QTimer::timeout, this, &MapStatus::refresh);
  connect(m_grid, &QComboBox::activated, this, [this](const int index) {
    if (const auto document = m_document.lock())
      document->map().grid().setSize(index + mdl::Grid::MinSize);
  });
  connect(m_snap, &QToolButton::clicked, this, [this]() {
    if (const auto document = m_document.lock())
      document->map().grid().toggleSnap();
  });
  refresh();
}

void MapStatus::setDocument(const std::shared_ptr<MapDocument>& document)
{
  if (document && m_document.lock() == document)
    return;
  m_refreshTimer->stop();
  m_connections.disconnect();
  m_document = document;
  if (document)
  {
    const auto delayed = [this](const auto&...) { refreshDelayed(); };
    m_connections += document->documentWasLoadedNotifier.connect([this]() { refresh(); });
    m_connections += document->documentWasSavedNotifier.connect(delayed);
    m_connections += document->modificationStateDidChangeNotifier.connect(delayed);
    m_connections += document->selectionDidChangeNotifier.connect(delayed);
    m_connections += document->currentLayerDidChangeNotifier.connect(delayed);
    m_connections += document->groupWasOpenedNotifier.connect(delayed);
    m_connections += document->groupWasClosedNotifier.connect(delayed);
    m_connections += document->nodeVisibilityDidChangeNotifier.connect(delayed);
    m_connections += document->editorContextDidChangeNotifier.connect(delayed);
    m_connections += document->gridDidChangeNotifier.connect([this]() { refresh(); });
  }
  refresh();
}

void MapStatus::refreshDelayed()
{
  if (!m_refreshTimer->isActive())
    m_refreshTimer->start();
}

void MapStatus::refresh()
{
  const auto document = m_document.lock();
  m_saveState->setVisible(document != nullptr);
  m_grid->setEnabled(document != nullptr);
  m_snap->setEnabled(document != nullptr);
  if (!document)
  {
    m_label->setText(tr("No active map"));
    m_label->setToolTip(m_label->text());
    m_grid->setCurrentIndex(-1);
    m_snap->setChecked(false);
    return;
  }
  const auto& map = document->map();
  const auto state =
    map.modified() ? QStringLiteral("modified") : QStringLiteral("saved");
  if (m_saveState->property("tbStatus").toString() != state)
  {
    m_saveState->setProperty("tbStatus", state);
    m_saveState->style()->unpolish(m_saveState);
    m_saveState->style()->polish(m_saveState);
  }
  m_saveState->setText(map.modified() ? tr("● Unsaved edits") : tr("✓ No pending edits"));
  m_label->setText(describeSelection(map));
  m_label->setToolTip(m_label->text());
  m_grid->setCurrentIndex(map.grid().size() - mdl::Grid::MinSize);
  m_snap->setChecked(map.grid().snap());
}
} // namespace tb::ui
