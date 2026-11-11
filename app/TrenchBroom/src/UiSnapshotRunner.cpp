/*
 Copyright (C) 2026 Kristian Duske

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

#include "UiSnapshotRunner.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDeadlineTimer>
#include <QDebug>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>

#include "UiAssetLibrarySnapshot.h"
#include "UiComponentSnapshot.h"
#include "UiWorkspaceSnapshot.h"
#include "base/PreferenceManager.h"
#include "fs/DiskIO.h"
#include "gl/Camera.h"
#include "gl/Material.h"
#include "gl/MaterialManager.h"
#include "gl/PerspectiveCamera.h"
#include "gl/Resource.h"
#include "gl/Texture.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/EntityLinkManager.h"
#include "mdl/EntityModel.h"
#include "mdl/EntityModelManager.h"
#include "mdl/EntityNode.h"
#include "mdl/GameManager.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/MapHeader.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/Map_World.h"
#include "mdl/Node.h"
#include "mdl/WorldNode.h"
#include "prefs/Preferences.h"
#include "ui/ActionExecutionContext.h"
#include "ui/AppController.h"
#include "ui/AreaMapView.h"
#include "ui/AssetInspector.h"
#include "ui/ClickableTitleBar.h"
#include "ui/CollapsibleTitledPanel.h"
#include "ui/CommandPaletteDialog.h"
#include "ui/CompilationDialog.h"
#include "ui/CompilationProfileManager.h"
#include "ui/CyclingMapView.h"
#include "ui/EntityBrowserView.h"
#include "ui/EntityMotionPreview.h"
#include "ui/EntityMotionPreviewWidget.h"
#include "ui/InfoPanel.h"
#include "ui/Inspector.h"
#include "ui/IssueBrowserView.h"
#include "ui/MapDocument.h"
#include "ui/MapEditorView.h"
#include "ui/MapSettingsDialog.h"
#include "ui/MapView2D.h"
#include "ui/MapView3D.h"
#include "ui/MapViewToolBox.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/MaterialBrowser.h"
#include "ui/PathTool.h"
#include "ui/PluginInspector.h"
#include "ui/PreferenceDialog.h"
#include "ui/QPathUtils.h"
#include "ui/SwitchableMapViewContainer.h"
#include "ui/ThemeRegistry.h"
#include "ui/UiSnapshot.h"
#include "ui/WelcomeWindow.h"
#include "ui/WorkspaceToolPanel.h"

#include "kd/filesystem_utils.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <variant>

namespace tb::ui
{

bool isSupportedUiSnapshotPage(const QString& page)
{
  static const auto pages = QStringList{
    QStringLiteral("map"),
    QStringLiteral("map-settings"),
    QStringLiteral("compilation-hltools"),
    QStringLiteral("compilation-options"),
    QStringLiteral("compilation-error"),
    QStringLiteral("compilation-simulated"),
    QStringLiteral("map-areas"),
    QStringLiteral("map-area-split-preview"),
    QStringLiteral("map-area-join-preview"),
    QStringLiteral("map-area-partial-join-preview"),
    QStringLiteral("map-area-dock-preview"),
    QStringLiteral("outliner"),
    QStringLiteral("outliner-hierarchy"),
    QStringLiteral("outliner-filter"),
    QStringLiteral("outliner-properties-entity"),
    QStringLiteral("outliner-properties-table"),
    QStringLiteral("outliner-properties-narrow"),
    QStringLiteral("outliner-properties-wad"),
    QStringLiteral("outliner-motion"),
    QStringLiteral("outliner-motion-table"),
    QStringLiteral("outliner-motion-narrow"),
    QStringLiteral("outliner-reparent-layer-before"),
    QStringLiteral("outliner-reparent-layer-after"),
    QStringLiteral("outliner-brush-entity-before"),
    QStringLiteral("outliner-brush-entity-after"),
    QStringLiteral("entity-browser"),
    QStringLiteral("entity-browser-empty"),
    QStringLiteral("goldsrc-entity-links"),
    QStringLiteral("entity-directions"),
    QStringLiteral("goldsrc-light-directions"),
    QStringLiteral("face-inspector"),
    QStringLiteral("material-browser-empty"),
    QStringLiteral("plugin-inspector"),
    QStringLiteral("terrain"),
    QStringLiteral("terrain-collapsed"),
    QStringLiteral("issues"),
    QStringLiteral("issues-empty"),
    QStringLiteral("supporting"),
    QStringLiteral("output-log"),
    QStringLiteral("path-tool-preview"),
    QStringLiteral("python-console"),
    QStringLiteral("command-palette"),
    QStringLiteral("components"),
    QStringLiteral("asset-library"),
    QStringLiteral("asset-library-material"),
    QStringLiteral("asset-library-builtin"),
    QStringLiteral("asset-library-project"),
    QStringLiteral("asset-library-narrow"),
    QStringLiteral("asset-library-project-narrow"),
    QStringLiteral("project-empty"),
    QStringLiteral("project-settings"),
    QStringLiteral("project-drawer"),
    QStringLiteral("project-pinned"),
    QStringLiteral("project-map-switch"),
    QStringLiteral("project-all-closed"),
    QStringLiteral("preferences"),
    QStringLiteral("preferences-colors"),
    QStringLiteral("preferences-mouse"),
    QStringLiteral("preferences-keyboard"),
    QStringLiteral("preferences-general"),
    QStringLiteral("preferences-extensions"),
    QStringLiteral("preferences-play-physics"),
    QStringLiteral("preferences-play-display"),
    QStringLiteral("preferences-play-analysis"),
    QStringLiteral("preferences-search"),
    QStringLiteral("preferences-search-empty"),
    QStringLiteral("preferences-kreedzbroom")};
  return pages.contains(page);
}

bool uiSnapshotPageRequiresMap(const QString& page)
{
  return page != QStringLiteral("map") && page != QStringLiteral("components")
         && !page.startsWith(QStringLiteral("asset-library"))
         && !page.startsWith(QStringLiteral("preferences"));
}

namespace
{

bool configureGamePath(
  AppController& appController,
  const UiSnapshotCommandLineOptions& options,
  const QStringList& fileNames)
{
  if (options.gamePath.isEmpty())
  {
    return true;
  }

  if (fileNames.empty())
  {
    qCritical() << "--ui-snapshot-game-path requires one map file";
    return false;
  }

  return kdl::with_istream(pathFromQString(fileNames.front()), mdl::readMapHeader)
         | kdl::transform([&](const auto& detectedGameAndFormat) {
             const auto& gameName = detectedGameAndFormat.first;
             if (!gameName)
             {
               qCritical() << "Could not detect the snapshot map's game";
               return false;
             }

             auto* gameInfo = appController.gameManager().gameInfo(*gameName);
             if (gameInfo == nullptr)
             {
               qCritical() << "Snapshot map uses an unavailable game:"
                           << QString::fromStdString(*gameName);
               return false;
             }

             setPref(gameInfo->gamePathPreference, pathFromQString(options.gamePath));
             return true;
           })
         | kdl::transform_error([](const auto& error) {
             qCritical().noquote() << "Could not read the snapshot map header:"
                                   << QString::fromStdString(error.msg);
             return false;
           })
         | kdl::value();
}

WelcomeWindow* findWelcomeWindow()
{
  for (auto* widget : QApplication::topLevelWidgets())
  {
    if (auto* welcomeWindow = qobject_cast<WelcomeWindow*>(widget))
    {
      return welcomeWindow;
    }
  }
  return nullptr;
}

bool isOutlinerSnapshotTarget(const QString& targetName)
{
  return targetName.startsWith(QStringLiteral("outliner"));
}

const std::vector<vm::vec3d>& pathToolSnapshotPoints()
{
  static const auto points = std::vector<vm::vec3d>{
    {-80.0, -48.0, 56.0},
    {-24.0, -64.0, 72.0},
    {24.0, -24.0, 80.0},
    {64.0, -48.0, 64.0},
  };
  return points;
}

template <typename NodeType, typename Predicate>
NodeType* findSnapshotNode(mdl::Node& node, const Predicate& predicate)
{
  if (auto* typedNode = dynamic_cast<NodeType*>(&node);
      typedNode && predicate(*typedNode))
  {
    return typedNode;
  }

  for (auto* child : node.children())
  {
    if (auto* result = findSnapshotNode<NodeType>(*child, predicate))
    {
      return result;
    }
  }
  return nullptr;
}

mdl::LayerNode* findSnapshotLayer(mdl::WorldNode& worldNode, const std::string& name)
{
  const auto layers = worldNode.customLayers();
  const auto it = std::ranges::find_if(
    layers, [&](const auto* layer) { return layer->name() == name; });
  return it != layers.end() ? *it : nullptr;
}

mdl::BrushNode* findSnapshotBrush(mdl::WorldNode& worldNode, const std::string& material)
{
  return findSnapshotNode<mdl::BrushNode>(worldNode, [&](const auto& brushNode) {
    return brushNode.brush().faceCount() > 0u
           && brushNode.brush().face(0u).materialName() == material;
  });
}

mdl::EntityNode* findSnapshotEntity(
  mdl::WorldNode& worldNode, const std::string& classname)
{
  return findSnapshotNode<mdl::EntityNode>(worldNode, [&](const auto& entityNode) {
    return entityNode.entity().classname() == classname;
  });
}

mdl::GroupNode* findSnapshotGroup(mdl::WorldNode& worldNode, const std::string& name)
{
  return findSnapshotNode<mdl::GroupNode>(
    worldNode, [&](const auto& groupNode) { return groupNode.name() == name; });
}

QTreeWidgetItem* findSnapshotTreeItem(QTreeWidget& tree, const mdl::Node* selectedNode)
{
  if (selectedNode == nullptr)
  {
    return nullptr;
  }

  auto stack = QList<QTreeWidgetItem*>{};
  for (auto i = 0; i < tree.topLevelItemCount(); ++i)
  {
    stack.push_back(tree.topLevelItem(i));
  }

  while (!stack.empty())
  {
    auto* item = stack.takeLast();
    if (item->data(0, Qt::UserRole).value<mdl::Node*>() == selectedNode)
    {
      return item;
    }
    for (auto i = 0; i < item->childCount(); ++i)
    {
      stack.push_back(item->child(i));
    }
  }
  return nullptr;
}

mdl::Node* configureOutlinerSnapshotModel(MapWindow& mapWindow, const QString& targetName)
{
  auto& map = mapWindow.document().map();
  auto& worldNode = map.worldNode();
  auto* selectedNode = static_cast<mdl::Node*>(nullptr);

  if (targetName.startsWith(QStringLiteral("outliner-motion")))
  {
    selectedNode = findSnapshotEntity(worldNode, "func_door_rotating");
    if (!selectedNode)
    {
      auto* entity = new mdl::EntityNode{mdl::Entity{
        {{"classname", "func_door_rotating"},
         {"distance", "90"},
         {"origin", "80 96 0"},
         {"targetname", "entry_door"}}}};
      const auto builder = mdl::BrushBuilder{worldNode.mapFormat(), map.worldBounds()};
      auto brush =
        builder.createCuboid(vm::bbox3d{{80, 96, 0}, {200, 112, 96}}, "__TB_empty");
      if (!brush.is_success())
      {
        delete entity;
        return nullptr;
      }
      entity->addChild(new mdl::BrushNode{std::move(brush) | kdl::value()});
      mdl::addNodes(map, {{&mdl::parentForNodes(map), {entity}}});
      selectedNode = entity;
    }
  }
  else if (targetName == QStringLiteral("outliner-properties-wad"))
  {
    mdl::setWorldProperty(
      map,
      "wad",
      "D:/CS/Mapper/Shared_Assets/Wad_Common/halflife.wad;"
      "D:/CS/Mapper/Shared_Assets/Wad_Common/sdhlt.wad;"
      "D:/CS/Mapper/Map_Assets/kz_relics.wad;"
      "D:/CS/Mapper/Map_Assets/kz_terrain.wad;"
      "D:/CS/Mapper/Shared_Assets/Wad_Common/decals.wad;"
      "D:/CS/Mapper/Map_Assets/kz_signs.wad");
    selectedNode = &worldNode;
  }
  else if (
    targetName.startsWith(QStringLiteral("outliner-properties-"))
    || targetName == QStringLiteral("outliner"))
  {
    selectedNode = findSnapshotEntity(worldNode, "light");
  }
  else if (
    targetName == QStringLiteral("outliner-filter")
    || targetName == QStringLiteral("outliner-hierarchy"))
  {
    selectedNode = findSnapshotGroup(worldNode, "Entry Hall");
  }
  else if (
    targetName == QStringLiteral("outliner-reparent-layer-before")
    || targetName == QStringLiteral("outliner-reparent-layer-after"))
  {
    auto* sourceBrush = findSnapshotBrush(worldNode, "snapshot_layer_source");
    auto* targetLayer = findSnapshotLayer(worldNode, "Architecture");
    if (
      targetName == QStringLiteral("outliner-reparent-layer-after")
      && sourceBrush != nullptr && targetLayer != nullptr
      && sourceBrush->parent() != targetLayer)
    {
      if (!mdl::reparentNodes(map, {{targetLayer, {sourceBrush}}}))
      {
        return nullptr;
      }
    }
    selectedNode = sourceBrush;
  }
  else if (
    targetName == QStringLiteral("outliner-brush-entity-before")
    || targetName == QStringLiteral("outliner-brush-entity-after"))
  {
    auto* sourceBrush = findSnapshotBrush(worldNode, "snapshot_entity_source");
    auto* targetEntity = findSnapshotEntity(worldNode, "func_door");
    const auto showAfter = targetName == QStringLiteral("outliner-brush-entity-after");
    if (
      showAfter && sourceBrush != nullptr && targetEntity != nullptr
      && sourceBrush->parent() != targetEntity)
    {
      if (!mdl::reparentNodes(map, {{targetEntity, {sourceBrush}}}))
      {
        return nullptr;
      }
    }
    selectedNode = showAfter ? static_cast<mdl::Node*>(targetEntity)
                             : static_cast<mdl::Node*>(sourceBrush);
  }

  if (selectedNode != nullptr)
  {
    mdl::deselectAll(map);
    mdl::selectNodes(map, {selectedNode});
  }
  return selectedNode;
}

void configureOutlinerSnapshot(QWidget& targetWidget, const QString& targetName)
{
  auto* mapWindow = qobject_cast<MapWindow*>(&targetWidget);
  if (mapWindow == nullptr)
  {
    return;
  }

  auto* selectedNode = configureOutlinerSnapshotModel(*mapWindow, targetName);
  const auto showProperties =
    targetName == QStringLiteral("outliner")
    || targetName.startsWith(QStringLiteral("outliner-motion"))
    || targetName.startsWith(QStringLiteral("outliner-properties-"));
  setPref(
    Preferences::EntityPropertiesTable,
    targetName == QStringLiteral("outliner-properties-table")
      || targetName == QStringLiteral("outliner-motion-table"));

  if (
    auto* propertiesToggle = targetWidget.findChild<QAbstractButton*>(
      QStringLiteral("OutlinerInspector_PropertiesToggle")))
  {
    if (propertiesToggle->isChecked() != showProperties)
    {
      propertiesToggle->click();
    }
  }

  if (
    auto* horizontalSplitter =
      mapWindow->findChild<QSplitter*>(QStringLiteral("MapWindow_HorizontalSplitter")))
  {
    horizontalSplitter->setSizes(
      targetName == QStringLiteral("outliner-properties-narrow")
          || targetName == QStringLiteral("outliner-motion-narrow")
          || targetName == QStringLiteral("outliner-properties-wad")
        ? QList<int>{1100, 340}
        : QList<int>{860, 580});
  }

  if (auto* outlinerSplitter =
        mapWindow->findChild<QSplitter*>(QStringLiteral("OutlinerInspector_Splitter"));
      outlinerSplitter != nullptr && showProperties)
  {
    outlinerSplitter->setSizes(QList<int>{330, 470});
  }

  if (
    auto* search =
      targetWidget.findChild<QLineEdit*>(QStringLiteral("OutlinerInspector_Search")))
  {
    const auto filter = targetName == QStringLiteral("outliner-filter")
                          ? QStringLiteral("type:group")
                          : QString{};
    if (search->text() != filter)
    {
      search->setText(filter);
    }
  }

  if (auto* sort =
        targetWidget.findChild<QComboBox*>(QStringLiteral("OutlinerInspector_Sort"));
      sort != nullptr && targetName == QStringLiteral("outliner-filter"))
  {
    sort->setCurrentIndex(1);
  }

  if (
    auto* tree =
      targetWidget.findChild<QTreeWidget*>(QStringLiteral("OutlinerTreeWidget")))
  {
    tree->expandAll();
    if (auto* snapshotItem = findSnapshotTreeItem(*tree, selectedNode))
    {
      const auto signalBlocker = QSignalBlocker{tree};
      tree->clearSelection();
      tree->setCurrentItem(snapshotItem);
      snapshotItem->setSelected(true);
      tree->setFocus(Qt::OtherFocusReason);
    }
  }

  if (targetName.startsWith(QStringLiteral("outliner-motion")))
    mapWindow->toolBox().entityMotionPreview().setProgress(0.5);

  if (targetName == QStringLiteral("outliner-properties-wad"))
  {
    QApplication::processEvents();
    for (auto* button : targetWidget.findChildren<QToolButton*>())
      if (button->property("propertyKey").toString() == "wad" && button->isCheckable())
        button->setChecked(true);
    QApplication::processEvents();
    auto* list = targetWidget.findChild<QListWidget*>("SmartWadEditor_Paths");
    auto* scroll = targetWidget.findChild<QScrollArea*>("outlinerPropertyScrollArea");
    if (list && scroll)
    {
      list->setCurrentRow(1);
      scroll->ensureWidgetVisible(list->parentWidget()->parentWidget());
    }
  }
}

void configureIssuesSnapshot(QWidget& targetWidget, const QString& targetName)
{
  auto* window = qobject_cast<MapWindow*>(&targetWidget);
  if (!window)
    return;
  if (!targetWidget.property("issuesSnapshotInitialized").toBool())
  {
    auto& map = window->document().map();
    auto entities = std::vector<mdl::Node*>{};
    for (int i = 0; i < 12; ++i)
      entities.push_back(new mdl::EntityNode{mdl::Entity{
        {{"classname", "trigger_multiple"},
         {"target", "missing_destination"},
         {"message", ""},
         {"targetname", "check_trigger_" + std::to_string(i + 1)}}}});
    mdl::addNodes(map, {{&mdl::parentForNodes(map), entities}});
    targetWidget.setProperty("issuesSnapshotInitialized", true);
  }
  window->switchToInfoPanelPage(InfoPanelPage::Issues);
  if (auto* mode = window->findChild<QComboBox*>("ProjectToolsMode"))
    mode->setCurrentIndex(1);
  if (auto* splitter = window->findChild<QSplitter*>("ProjectToolsSplitter"))
    splitter->setSizes({460, 440});
  auto* issues = window->findChild<IssueBrowserView*>();
  if (!issues)
    return;
  issues->validate();
  if (targetName == "issues-empty")
    issues->findChild<QLineEdit*>("IssueBrowser_Search")
      ->setText("no_matching_diagnostic");
  else
  {
    auto* tree = issues->findChild<QTreeView*>("IssueBrowser_Tree");
    if (tree && tree->model()->rowCount() > 0)
    {
      const auto group = tree->model()->index(0, 0);
      tree->setExpanded(group, true);
      tree->selectionModel()->select(
        tree->model()->index(0, 0, group),
        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }
  }
}

void configureSupportingSnapshot(QWidget& targetWidget)
{
  auto* mapWindow = qobject_cast<MapWindow*>(&targetWidget);
  if (mapWindow == nullptr)
  {
    return;
  }

  mapWindow->switchToInfoPanelPage(InfoPanelPage::Assets);
  if (auto* mode = mapWindow->findChild<QComboBox*>("ProjectToolsMode"))
    mode->setCurrentIndex(1);
  if (
    auto* splitter =
      mapWindow->findChild<QSplitter*>(QStringLiteral("ProjectToolsSplitter")))
  {
    splitter->setSizes(QList<int>{560, 340});
  }
}

void configureGoldSrcEntitySnapshot(QWidget& targetWidget)
{
  auto* mapWindow = qobject_cast<MapWindow*>(&targetWidget);
  if (!mapWindow)
  {
    return;
  }
  auto& map = mapWindow->document().map();
  if (auto* manager = findSnapshotEntity(map.worldNode(), "multi_manager"))
  {
    if (!manager->selected())
    {
      mdl::deselectAll(map);
      mdl::selectNodes(map, {manager});
    }
  }
  setPref(Preferences::EntityLinkMode, Preferences::EntityLinkModeAll);
  setPref(Preferences::ShowAxes, false);
  mapWindow->switchToInspectorPage(InspectorPage::Outliner);
  if (mapWindow->infoPanelVisible())
  {
    mapWindow->toggleInfoPanel();
  }
  for (auto* view : mapWindow->findChildren<MapView3D*>())
  {
    view->stopCameraAnimation();
    auto& camera = static_cast<MapViewBase&>(*view).camera();
    camera.moveTo(vm::vec3f{96, -110, 170});
    camera.lookAt(vm::vec3f{96, 112, 48}, vm::vec3f{0, 0, 1});
  }
}

void configureEntityDirectionSnapshot(QWidget& targetWidget, const bool lights)
{
  auto* mapWindow = qobject_cast<MapWindow*>(&targetWidget);
  if (!mapWindow)
  {
    return;
  }
  if (!targetWidget.property("entityDirectionsInitialized").toBool())
  {
    mapWindow->switchMapViewLayout(MapViewLayout::FourPanes);
    auto& map = mapWindow->document().map();
    auto entities = std::vector<mdl::Node*>{};
    for (auto* node : map.worldNode().defaultLayer()->children())
    {
      if (auto* entity = dynamic_cast<mdl::EntityNode*>(node);
          entity
          && (lights ? (entity->entity().classname() == "light_spot"
                        || entity->entity().classname() == "light_environment"
                        || entity->entity().classname() == "info_sunlight"
                        || entity->entity().classname() == "light")
                     : entity->entity().classname() == "info_player_start"))
      {
        entities.push_back(entity);
      }
    }
    mdl::deselectAll(map);
    mdl::selectNodes(map, entities);
    setPref(Preferences::ShowAxes, false);
    setPref(Preferences::ShowEntityClassnames, false);
    mapWindow->switchToInspectorPage(InspectorPage::Outliner);
    if (mapWindow->infoPanelVisible())
    {
      mapWindow->toggleInfoPanel();
    }
    targetWidget.setProperty("entityDirectionsInitialized", true);
  }
  for (auto* view : mapWindow->findChildren<MapView2D*>())
  {
    view->stopCameraAnimation();
    auto& camera = static_cast<MapViewBase&>(*view).camera();
    camera.moveTo(vm::vec3f{0, 0, 40} - camera.direction() * 4096.0f);
    camera.setZoom(0.9f);
  }
  for (auto* view : mapWindow->findChildren<MapView3D*>())
  {
    view->stopCameraAnimation();
    auto& camera = static_cast<MapViewBase&>(*view).camera();
    camera.moveTo(vm::vec3f{260, -380, 300});
    camera.lookAt(vm::vec3f{0, 0, 40}, vm::vec3f{0, 0, 1});
  }
}

void configurePathToolSnapshot(QWidget& targetWidget)
{
  auto* mapWindow = qobject_cast<MapWindow*>(&targetWidget);
  if (mapWindow == nullptr)
  {
    return;
  }

  auto& toolBox = mapWindow->toolBox();
  if (!toolBox.pathToolActive())
  {
    toolBox.togglePathTool();
  }

  auto& pathTool = toolBox.pathTool();
  pathTool.clearPoints();
  for (const auto& point : pathToolSnapshotPoints())
  {
    pathTool.addPoint(point);
  }

  if (
    auto* horizontalSplitter =
      mapWindow->findChild<QSplitter*>(QStringLiteral("MapWindow_HorizontalSplitter")))
  {
    horizontalSplitter->setSizes(QList<int>{1080, 360});
  }
  if (
    auto* verticalSplitter = mapWindow->findChild<QSplitter*>(
      QStringLiteral("MapWindow_VerticalSplitterSplitter")))
  {
    verticalSplitter->setSizes(QList<int>{800, 100});
  }
}

void configurePythonConsoleSnapshot(QWidget& targetWidget)
{
  auto* mapWindow = qobject_cast<MapWindow*>(&targetWidget);
  if (mapWindow == nullptr)
  {
    return;
  }

  mapWindow->switchToInfoPanelPage(InfoPanelPage::PythonConsole);
  if (auto* mode = mapWindow->findChild<QComboBox*>("ProjectToolsMode"))
    mode->setCurrentIndex(1);
  if (
    auto* splitter =
      mapWindow->findChild<QSplitter*>(QStringLiteral("ProjectToolsSplitter")))
  {
    splitter->setSizes(QList<int>{560, 340});
  }

  auto* input =
    mapWindow->findChild<QPlainTextEdit*>(QStringLiteral("PythonConsole_Input"));
  auto* runButton =
    mapWindow->findChild<QToolButton*>(QStringLiteral("PythonConsole_Run"));
  if (
    input != nullptr && runButton != nullptr
    && !mapWindow->property("uiSnapshotPythonConsoleExecuted").toBool())
  {
    input->setPlainText(
      QStringLiteral("trenchbroom.documents.current().entities[0].classname"));
    runButton->click();
    mapWindow->setProperty("uiSnapshotPythonConsoleExecuted", true);
  }

  if (input != nullptr)
  {
    input->setPlainText(QStringLiteral("print('Ready')"));
    input->setFocus(Qt::OtherFocusReason);
  }
}

bool isInspectorSnapshotTarget(const QString& targetName)
{
  return targetName == QStringLiteral("entity-browser")
         || targetName == QStringLiteral("entity-browser-empty")
         || targetName == QStringLiteral("face-inspector")
         || targetName == QStringLiteral("material-browser-empty")
         || targetName == QStringLiteral("plugin-inspector");
}

bool isMaterialBrowserSnapshotTarget(const QString& targetName)
{
  return targetName == QStringLiteral("face-inspector")
         || targetName == QStringLiteral("material-browser-empty");
}

enum class UiSnapshotReadinessState
{
  Ready,
  Pending,
  Failed,
};

struct UiSnapshotReadiness
{
  UiSnapshotReadinessState state;
  QString detail;
};

UiSnapshotReadiness goldSrcEntitySnapshotReadiness(QWidget& targetWidget)
{
  auto* mapWindow = qobject_cast<MapWindow*>(&targetWidget);
  if (!mapWindow)
  {
    return {UiSnapshotReadinessState::Failed, "Entity snapshot is not a map window"};
  }
  auto& map = mapWindow->document().map();
  auto* manager = findSnapshotEntity(map.worldNode(), "multi_manager");
  for (const auto* targetName : {"test1", "test2", "test3", "shake"})
  {
    auto* target =
      findSnapshotNode<mdl::EntityNode>(map.worldNode(), [&](const auto& node) {
        return node.entity().hasProperty(mdl::EntityPropertyKeys::Targetname, targetName);
      });
    if (!manager || !target)
    {
      return {UiSnapshotReadinessState::Failed, "GoldSrc entity fixture is incomplete"};
    }
    if (
      std::string_view{targetName} != "shake"
      && !map.entityLinkManager().hasLink(*manager, *target, targetName))
    {
      return {UiSnapshotReadinessState::Failed, "Multi-manager target link is missing"};
    }
    if (std::string_view{targetName} == "shake")
    {
      const auto& links =
        map.entityLinkManager().linksTo(*target, mdl::EntityPropertyKeys::Targetname);
      if (links.size() != 1u || links.begin()->propertyKey != "target")
      {
        return {UiSnapshotReadinessState::Failed, "Standard target link is missing"};
      }
    }
  }
  for (const auto* classname : {"multi_manager", "trigger_changetarget", "env_shake"})
  {
    const auto* node = findSnapshotEntity(map.worldNode(), classname);
    if (!node)
    {
      return {UiSnapshotReadinessState::Failed, "GoldSrc entity fixture is incomplete"};
    }
    const auto spec = node->entity().modelSpecification();
    if (!spec)
    {
      return {UiSnapshotReadinessState::Failed, "Entity icon definition is invalid"};
    }
    const auto* model = map.entityModelManager().model(spec.value().path);
    if (!model || !model->data())
    {
      return {UiSnapshotReadinessState::Pending, "Waiting for entity icon models"};
    }
    const auto* texture = model->data()->surfaces().front().skin(0)->texture();
    if (!texture || !texture->isReady())
    {
      return {UiSnapshotReadinessState::Pending, "Waiting for entity icon textures"};
    }
  }
  return {UiSnapshotReadinessState::Ready, "GoldSrc target links and entity icons ready"};
}

UiSnapshotReadiness outlinerSnapshotReadiness(
  QWidget& targetWidget, const QString& targetName)
{
  auto* mapWindow = qobject_cast<MapWindow*>(&targetWidget);
  if (mapWindow == nullptr)
  {
    return {
      UiSnapshotReadinessState::Failed,
      QStringLiteral("Outliner snapshot target is not a map window")};
  }

  auto* tree = mapWindow->findChild<QTreeWidget*>(QStringLiteral("OutlinerTreeWidget"));
  if (tree == nullptr)
  {
    return {
      UiSnapshotReadinessState::Failed,
      QStringLiteral("Outliner tree widget was not found")};
  }
  if (tree->topLevelItemCount() == 0)
  {
    return {
      UiSnapshotReadinessState::Pending, QStringLiteral("Outliner tree has no rows")};
  }

  if (targetName == QStringLiteral("outliner"))
  {
    return {UiSnapshotReadinessState::Ready, {}};
  }

  auto& worldNode = mapWindow->document().map().worldNode();
  auto* expectedNode = static_cast<mdl::Node*>(nullptr);
  auto* parentedNode = static_cast<mdl::Node*>(nullptr);
  auto* expectedParent = static_cast<mdl::Node*>(nullptr);
  auto checkParent = false;
  auto expectParent = false;
  auto expectedDescription = QString{};

  if (targetName.startsWith(QStringLiteral("outliner-motion")))
  {
    auto* widget = targetWidget.findChild<EntityMotionPreviewWidget*>();
    const auto& preview = mapWindow->toolBox().entityMotionPreview();
    if (!widget || !widget->isVisible() || !preview.active() || preview.progress() != 0.5)
      return {
        UiSnapshotReadinessState::Failed, "Entity motion preview fixture is missing"};
    if (widget->minimumSizeHint().width() > widget->width())
      return {
        UiSnapshotReadinessState::Failed, "Entity motion preview controls are clipped"};
    return {UiSnapshotReadinessState::Ready, "Selected rotating door at 50% motion"};
  }

  if (targetName == QStringLiteral("outliner-properties-wad"))
  {
    auto* list = targetWidget.findChild<QListWidget*>("SmartWadEditor_Paths");
    if (!list || !list->isVisible() || list->count() != 6)
      return {UiSnapshotReadinessState::Failed, "Expanded WAD list fixture is missing"};
    if (
      list->viewport()->height() < 6 * list->sizeHintForRow(0)
      || list->horizontalScrollBar()->maximum() != 0)
      return {UiSnapshotReadinessState::Pending, "WAD list must retain six full rows"};
    return {UiSnapshotReadinessState::Ready, "Expanded WAD files retain six full rows"};
  }
  else if (
    targetName == QStringLiteral("outliner-hierarchy")
    || targetName == QStringLiteral("outliner-filter"))
  {
    expectedNode = findSnapshotGroup(worldNode, "Entry Hall");
    expectedDescription = QStringLiteral("Entry Hall group");
  }
  else if (targetName.startsWith(QStringLiteral("outliner-properties-")))
  {
    expectedNode = findSnapshotEntity(worldNode, "light");
    expectedDescription = QStringLiteral("light entity");
  }
  else if (targetName.startsWith(QStringLiteral("outliner-reparent-layer-")))
  {
    expectedNode = findSnapshotBrush(worldNode, "snapshot_layer_source");
    parentedNode = expectedNode;
    expectedParent = findSnapshotLayer(worldNode, "Architecture");
    checkParent = true;
    expectParent = targetName.endsWith(QStringLiteral("-after"));
    expectedDescription = QStringLiteral("layer reparent source brush");
  }
  else if (targetName.startsWith(QStringLiteral("outliner-brush-entity-")))
  {
    auto* sourceBrush = findSnapshotBrush(worldNode, "snapshot_entity_source");
    expectedParent = findSnapshotEntity(worldNode, "func_door");
    checkParent = true;
    expectParent = targetName.endsWith(QStringLiteral("-after"));
    parentedNode = sourceBrush;
    expectedNode = expectParent ? static_cast<mdl::Node*>(expectedParent)
                                : static_cast<mdl::Node*>(sourceBrush);
    expectedDescription = expectParent
                            ? QStringLiteral("func_door entity")
                            : QStringLiteral("brush entity reparent source brush");
  }

  if (expectedNode == nullptr)
  {
    return {
      UiSnapshotReadinessState::Failed,
      QStringLiteral("Outliner fixture is missing the %1").arg(expectedDescription)};
  }
  if (checkParent && expectedParent == nullptr)
  {
    return {
      UiSnapshotReadinessState::Failed,
      QStringLiteral("Outliner fixture is missing the target for the %1")
        .arg(expectedDescription)};
  }
  if (checkParent && parentedNode == nullptr)
  {
    return {
      UiSnapshotReadinessState::Failed,
      QStringLiteral("Outliner fixture is missing the reparent source node")};
  }
  if (checkParent && (parentedNode->parent() == expectedParent) != expectParent)
  {
    return {
      UiSnapshotReadinessState::Failed,
      QStringLiteral("Outliner fixture has the wrong parent for the %1")
        .arg(expectedDescription)};
  }
  auto* expectedItem = findSnapshotTreeItem(*tree, expectedNode);
  if (
    expectedItem == nullptr || expectedItem->isHidden()
    || tree->currentItem() != expectedItem || !expectedItem->isSelected())
  {
    return {
      UiSnapshotReadinessState::Pending,
      QStringLiteral("Outliner tree has not revealed and selected the %1")
        .arg(expectedDescription)};
  }

  return {
    UiSnapshotReadinessState::Ready,
    QStringLiteral("Outliner state ready: %1").arg(expectedDescription)};
}

UiSnapshotReadiness uiSnapshotReadiness(QWidget& targetWidget, const QString& targetName)
{
  if (targetName == "preferences-search" || targetName == "preferences-search-empty")
  {
    const auto* navigation =
      targetWidget.findChild<QListWidget*>("PreferenceDialog_NavigationList");
    const auto* pages = targetWidget.findChild<QStackedWidget*>("PreferenceDialog_Pages");
    const auto* tabs = targetWidget.findChild<QTabWidget*>("KreedzBroomPreference_Tabs");
    const auto* empty = targetWidget.findChild<QLabel*>("PreferenceDialog_NoResults");
    const auto noResults = targetName == "preferences-search-empty";
    const auto ready = navigation && pages && tabs && empty
      && (noResults ? empty->isVisible() && pages->isHidden()
        : navigation->currentItem()
          && navigation->currentItem()->data(Qt::UserRole) == "play"
          && pages->currentIndex() == navigation->currentRow()
          && tabs->currentIndex() == 1 && tabs->isVisible());
    return {
      ready ? UiSnapshotReadinessState::Ready : UiSnapshotReadinessState::Failed,
      "Preference search must show the matching tab or its empty state"};
  }
  if (targetName.startsWith("compilation-"))
  {
    const auto* output = targetWidget.findChild<QLineEdit*>("HltoolsOutput");
    const auto* quality = targetWidget.findChild<QComboBox*>("HltoolsQuality");
    const auto* gpu = targetWidget.findChild<QCheckBox*>("HltoolsGpu");
    const auto* diagnostic = targetWidget.findChild<QLabel*>("CompilationTaskDiagnostic");
    const auto* compile = targetWidget.findChild<QPushButton*>("CompilationCompile");
    const auto* status = targetWidget.findChild<QLabel*>("CompilationRunStatus");
    const auto stateReady = compile && status
      && !targetWidget.findChild<QLabel*>("CompilationTarget")
      && (targetName == "compilation-error"
        ? diagnostic && diagnostic->isVisible()
          && diagnostic->toolTip().contains("MISSING_COMPILER_OPTION")
          && !compile->isEnabled()
        : compile->isEnabled() && (!diagnostic || !diagnostic->isVisible())
          && (targetName != "compilation-simulated"
              || status->text().contains("Simulation completed")));
    const auto ready = stateReady && output && quality && gpu && output->isVisible()
                       && quality->isVisible() && gpu->isVisible()
                       && !output->text().isEmpty() && quality->currentData() == "normal"
                       && gpu->isChecked() && output->text() == "${MAP_BASE_NAME}.bsp";
    if (targetName == "compilation-options")
    {
      const auto* options =
        targetWidget.findChild<CollapsibleTitledPanel*>("HltoolsOptionsPanel");
      const auto* sky = targetWidget.findChild<QCheckBox*>("HltoolsOption_noskyclip");
      if (
        !options || !options->expanded() || !sky || !sky->isVisible()
        || !sky->isChecked())
        return {
          UiSnapshotReadinessState::Failed,
          "Compiler option list must be expanded and reflect the parameter expression"};
    }
    return {
      ready ? UiSnapshotReadinessState::Ready : UiSnapshotReadinessState::Failed,
      "Built-in compiler output, quality and GPU controls must be visible"};
  }
  if (targetName == "terrain" || targetName == "terrain-collapsed")
  {
    for (const auto* name : {"Creation", "Editing", "Compilation"})
    {
      const auto* section = targetWidget.findChild<CollapsibleTitledPanel*>(
        QStringLiteral("TerrainInspector_%1").arg(name));
      const auto* bar = section ? section->findChild<ClickableTitleBar*>() : nullptr;
      if (!bar || !section->isVisible())
        return {UiSnapshotReadinessState::Failed, "Terrain section is missing or hidden"};
      const auto expanded = targetName == "terrain" && QString{name} != "Compilation";
      if (
        section->expanded() != expanded
        || bar->height() != bar->sizeHint().expandedTo(bar->minimumSize()).height()
        || (!expanded && section->height() != bar->height()
              + section->contentsMargins().top() + section->contentsMargins().bottom()))
        return {UiSnapshotReadinessState::Pending, "Terrain section layout is settling"};
    }
    return {UiSnapshotReadinessState::Ready, "Terrain section state and geometry ready"};
  }
  if (targetName == "map-areas" || targetName.startsWith("map-area-"))
  {
    auto* areas = targetWidget.findChild<AreaMapView*>();
    auto* preview = targetWidget.findChild<QWidget*>("MapViewAreaPreview");
    if (!areas || areas->areas().size() != (targetName == "map-area-partial-join-preview" ? 3u : 4u)
        || (targetName != "map-areas" && (!preview || !preview->isVisible()
            || preview->property("join").toBool() != targetName.endsWith("join-preview"))))
      return {
        UiSnapshotReadinessState::Failed,
        "Map area layout or gesture preview is missing"};
    return {UiSnapshotReadinessState::Ready, "Map area layout and gesture state ready"};
  }
  if (auto* tools = targetWidget.findChild<WorkspaceToolPanel*>();
      tools && tools->isAnimating())
    return {
      UiSnapshotReadinessState::Pending, "Workspace drawer animation is still running"};
  if (targetName == "issues" || targetName == "issues-empty")
  {
    auto* issues = targetWidget.findChild<IssueBrowserView*>();
    auto* tree = issues ? issues->findChild<QTreeView*>("IssueBrowser_Tree") : nullptr;
    auto* details = issues ? issues->findChild<QLabel*>("IssueBrowser_Details") : nullptr;
    const auto ready = tree && details && tree->isVisible() && !details->text().isEmpty()
      && (targetName == "issues-empty" ? tree->model()->rowCount() == 0 : !tree->selectionModel()->selectedRows().isEmpty());
    return {
      ready ? UiSnapshotReadinessState::Ready : UiSnapshotReadinessState::Failed,
      "Issue groups, filtering and details must reach their requested state"};
  }
  if (targetName == QStringLiteral("output-log"))
  {
    auto* bar = targetWidget.findChild<QWidget*>("InfoPanel_TabBar");
    auto* output = targetWidget.findChild<QTextEdit*>("Console_TextView");
    auto* navigation = targetWidget.findChild<QWidget*>("ProjectToolBar");
    if (!bar || !output || !navigation || bar->isVisible() || !output->isVisible())
      return {
        UiSnapshotReadinessState::Failed,
        "Output log must not retain an empty action row"};
    const auto gap =
      output->mapTo(&targetWidget, QPoint{}).y()
      - navigation->mapTo(&targetWidget, QPoint{0, navigation->height()}).y();
    return {
      gap >= 0 && gap <= 2 ? UiSnapshotReadinessState::Ready
                           : UiSnapshotReadinessState::Failed,
      "Output log must meet the drawer tab strip without a blank spacer"};
  }
  if (
    targetName == QStringLiteral("asset-library-material")
    || targetName == QStringLiteral("asset-library-builtin"))
  {
    const auto* inspector = targetWidget.findChild<AssetInspector*>();
    if (
      !inspector || !inspector->asset()
      || inspector->asset()->nativeFormat != "goldsrc-terrain-material"
      || inspector->asset()->terrainPreview.isNull())
      return {
        UiSnapshotReadinessState::Failed,
        "Library inspector must load its three-texture terrain material"};
    return {UiSnapshotReadinessState::Ready, {}};
  }
  if (
    targetName == QStringLiteral("asset-library-project")
    || targetName == QStringLiteral("asset-library-project-narrow"))
  {
    auto* usage = targetWidget.findChild<QListWidget*>("AssetLibraryUsage");
    auto* inspector = targetWidget.findChild<AssetInspector*>();
    if (
      !usage || !inspector || !inspector->asset() || !inspector->asset()->resource.skybox)
      return {
        UiSnapshotReadinessState::Failed,
        "Bound library inspector must load its captured sky fixture"};
    for (int i = 0; i < usage->count(); ++i)
      if (!usage->item(i)->data(Qt::UserRole).toString().isEmpty())
        return {UiSnapshotReadinessState::Ready, {}};
    return {
      UiSnapshotReadinessState::Failed,
      "Bound library fixture must resolve its project map references"};
  }
  if (targetName.startsWith(QStringLiteral("project-")))
  {
    const auto reason = workspaceSnapshotPendingReason(targetWidget, targetName);
    return {
      reason.isEmpty() ? UiSnapshotReadinessState::Ready
                       : UiSnapshotReadinessState::Pending,
      reason};
  }
  if (targetName == QStringLiteral("goldsrc-entity-links"))
  {
    return goldSrcEntitySnapshotReadiness(targetWidget);
  }
  if (
    targetName == QStringLiteral("entity-directions")
    || targetName == QStringLiteral("goldsrc-light-directions"))
  {
    auto* mapWindow = qobject_cast<MapWindow*>(&targetWidget);
    const auto visible = [&](auto* view) { return view->isVisibleTo(&targetWidget); };
    const auto ready =
      mapWindow && mapWindow->document().map().selection().nodes.size() == 6u
      && std::ranges::count_if(mapWindow->findChildren<MapView2D*>(), visible) == 3
      && std::ranges::count_if(mapWindow->findChildren<MapView3D*>(), visible) == 1;
    return {
      ready ? UiSnapshotReadinessState::Ready : UiSnapshotReadinessState::Failed,
      QStringLiteral("Direction fixture needs six selected entities and four map views")};
  }
  if (isOutlinerSnapshotTarget(targetName))
  {
    return outlinerSnapshotReadiness(targetWidget, targetName);
  }

  if (targetName == QStringLiteral("path-tool-preview"))
  {
    auto* mapWindow = qobject_cast<MapWindow*>(&targetWidget);
    if (mapWindow == nullptr)
    {
      return {
        UiSnapshotReadinessState::Failed,
        QStringLiteral("Path Tool snapshot target is not a map window")};
    }

    auto& toolBox = mapWindow->toolBox();
    const auto& pathTool = toolBox.pathTool();
    if (!toolBox.pathToolActive() || pathTool.points() != pathToolSnapshotPoints())
    {
      return {
        UiSnapshotReadinessState::Pending,
        QStringLiteral("Path Tool preview points are not ready")};
    }
    return {
      UiSnapshotReadinessState::Ready,
      QStringLiteral("Path Tool preview ready with %1 points")
        .arg(pathTool.points().size())};
  }

  if (!isMaterialBrowserSnapshotTarget(targetName))
  {
    return {UiSnapshotReadinessState::Ready, {}};
  }

  auto* mapWindow = qobject_cast<MapWindow*>(&targetWidget);
  if (mapWindow == nullptr)
  {
    return {
      UiSnapshotReadinessState::Failed,
      QStringLiteral("Material browser snapshot target is not a map window")};
  }

  const auto& materials = mapWindow->document().map().materialManager().materials();
  if (materials.empty())
  {
    return {
      UiSnapshotReadinessState::Pending,
      QStringLiteral("Material browser snapshot has no loaded materials")};
  }

  auto readyCount = size_t{0};
  auto pendingCount = size_t{0};
  auto failedNames = QStringList{};
  for (const auto* material : materials)
  {
    const auto& resource = material->textureResource();
    if (const auto* failed = std::get_if<gl::ResourceFailed>(&resource.state()))
    {
      failedNames.push_back(QStringLiteral("%1 (%2)").arg(
        QString::fromStdString(material->name()), QString::fromStdString(failed->error)));
    }
    else if (const auto* texture = material->texture();
             texture != nullptr && texture->isReady())
    {
      ++readyCount;
    }
    else
    {
      ++pendingCount;
    }
  }

  if (!failedNames.empty())
  {
    return {
      UiSnapshotReadinessState::Failed,
      QStringLiteral("Material textures failed: %1")
        .arg(failedNames.mid(0, 3).join(QStringLiteral("; ")))};
  }

  const auto detail = QStringLiteral("Material textures ready: %1/%2; pending: %3")
                        .arg(readyCount)
                        .arg(materials.size())
                        .arg(pendingCount);
  return {
    pendingCount == 0 ? UiSnapshotReadinessState::Ready
                      : UiSnapshotReadinessState::Pending,
    detail};
}

void configureInspectorSnapshot(QWidget& targetWidget, const QString& targetName)
{
  auto* mapWindow = qobject_cast<MapWindow*>(&targetWidget);
  if (mapWindow == nullptr)
  {
    return;
  }

  const auto showPluginInspector = targetName == QStringLiteral("plugin-inspector");
  if (showPluginInspector)
  {
    mapWindow->switchToInspectorPage(InspectorPage::Plugin);
    if (
      mapWindow->findChild<QWidget*>(QStringLiteral("UiSnapshot_PluginPanelContent"))
      == nullptr)
    {
      auto* content = mapWindow->addPluginPanel(QStringLiteral("Chamfer Tool")).content();
      content->setObjectName(QStringLiteral("UiSnapshot_PluginPanelContent"));

      auto* status = new QLabel{QStringLiteral("Ready"), content};
      auto* distance = new QDoubleSpinBox{content};
      distance->setObjectName(QStringLiteral("UiSnapshot_PluginDistance"));
      distance->setDecimals(1);
      distance->setRange(0.1, 1024.0);
      distance->setValue(8.0);

      auto* segments = new QSpinBox{content};
      segments->setRange(1, 64);
      segments->setValue(1);

      auto* edgeButton = new QPushButton{QStringLiteral("Chamfer Edge Handles"), content};
      auto* vertexButton =
        new QPushButton{QStringLiteral("Chamfer Vertex Handles"), content};

      auto* panelLayout = new QFormLayout{};
      panelLayout->setContentsMargins(8, 8, 8, 8);
      panelLayout->addRow(QStringLiteral("Status:"), status);
      panelLayout->addRow(QStringLiteral("Distance:"), distance);
      panelLayout->addRow(QStringLiteral("Segments:"), segments);
      panelLayout->addRow(edgeButton);
      panelLayout->addRow(vertexButton);
      content->setLayout(panelLayout);
    }

    if (
      auto* distance = mapWindow->findChild<QDoubleSpinBox*>(
        QStringLiteral("UiSnapshot_PluginDistance")))
    {
      distance->setFocus(Qt::OtherFocusReason);
      distance->selectAll();
    }
  }
  else if (targetName == QStringLiteral("face-inspector"))
  {
    setPref(Preferences::MaterialBrowserIconSize, 5.0f);
    if (auto* materialBrowser = mapWindow->findChild<MaterialBrowser*>())
    {
      materialBrowser->setGroup(true);
    }
  }

  const auto showEntityBrowser = targetName.startsWith(QStringLiteral("entity-browser"));
  if (!showPluginInspector)
  {
    if (showEntityBrowser)
    {
      mapWindow->editorView().showEntityBrowser();
      if (auto* mode = mapWindow->findChild<QComboBox*>("ProjectToolsMode"))
        mode->setCurrentIndex(1);
      if (!targetName.endsWith(QStringLiteral("-empty")))
        if (auto* grid = mapWindow->findChild<EntityBrowserView*>("EntityBrowser_Grid"))
          grid->selectDefinition("light");
    }
    else
      mapWindow->switchToInspectorPage(InspectorPage::Face);
  }
  if (
    auto* splitter =
      mapWindow->findChild<QSplitter*>(QStringLiteral("MapWindow_HorizontalSplitter")))
  {
    splitter->setSizes(
      targetName == QStringLiteral("face-inspector") ? QList<int>{10000, 1}
                                                     : QList<int>{940, 500});
  }

  if (targetName.endsWith(QStringLiteral("-empty")))
  {
    const auto searchName = showEntityBrowser ? QStringLiteral("ModelBrowser_Search")
                                              : QStringLiteral("MaterialBrowser_Search");
    if (auto* search = mapWindow->findChild<QLineEdit*>(searchName))
    {
      const auto filterText = QStringLiteral("__ui_snapshot_no_match__");
      search->setText(filterText);
      search->textEdited(filterText);
    }
  }
}

void configurePreferencesSnapshot(
  QWidget& targetWidget, const QString& targetName, const QString& theme = {})
{
  if (
    auto* navigation = targetWidget.findChild<QListWidget*>(
      QStringLiteral("PreferenceDialog_NavigationList")))
  {
    const auto id = targetName == QStringLiteral("preferences-colors")     ? "colors"
                    : targetName == QStringLiteral("preferences-mouse")    ? "mouse"
                    : targetName == QStringLiteral("preferences-keyboard") ? "keyboard"
                    : targetName == QStringLiteral("preferences-general")  ? "general"
                    : targetName == QStringLiteral("preferences-extensions")
                      ? "extensions"
                    : targetName == QStringLiteral("preferences-kreedzbroom")
                        || targetName.startsWith(QStringLiteral("preferences-play-"))
                      ? "play"
                      : "view";
    for (int row = 0; row < navigation->count(); ++row)
      if (navigation->item(row)->data(Qt::UserRole).toString() == QLatin1String{id})
        navigation->setCurrentRow(row);
    navigation->setFocus(Qt::OtherFocusReason);
  }

  if (auto* themeCombo =
        targetWidget.findChild<QComboBox*>(QStringLiteral("ViewPreference_ThemeCombo"));
      !theme.isEmpty() && themeCombo != nullptr)
  {
    const auto themeId = ThemeRegistry::instance().canonicalThemeId(theme);
    const auto index = themeCombo->findData(themeId);
    if (index >= 0)
    {
      themeCombo->setCurrentIndex(index);
    }
  }

  if (auto* tabs = targetWidget.findChild<QTabWidget*>("KreedzBroomPreference_Tabs"))
  {
    if (targetName == QStringLiteral("preferences-play-physics"))
      tabs->setCurrentIndex(1);
    if (targetName == QStringLiteral("preferences-play-display"))
      tabs->setCurrentIndex(2);
    if (targetName == QStringLiteral("preferences-play-analysis"))
      tabs->setCurrentIndex(3);
  }
  if (auto* search = targetWidget.findChild<QLineEdit*>("PreferenceDialog_Search"))
  {
    if (targetName.startsWith(QStringLiteral("preferences-search")))
      search->clear();
    if (targetName == QStringLiteral("preferences-search"))
      search->setText("sv_gravity");
    if (targetName == QStringLiteral("preferences-search-empty"))
      search->setText("zzzz-no-setting");
  }
}

void configureMapAreaSnapshot(QWidget& targetWidget, const QString& targetName)
{
  auto* container = targetWidget.findChild<SwitchableMapViewContainer*>();
  if (!container)
    return;
  if (!targetWidget.property("mapAreasInitialized").toBool())
  {
    container->switchToMapView(
      targetName == "map-area-partial-join-preview" ? MapViewLayout::ThreePanes
                                                    : MapViewLayout::FourPanes);
    targetWidget.setProperty("mapAreasInitialized", true);
  }
  if (
    !targetWidget.isVisible() || targetName == "map-areas"
    || targetWidget.property("mapAreaGestureStarted").toBool())
    return;
  auto* layout = container->findChild<AreaMapView*>();
  auto areas = layout->areas();
  std::ranges::sort(areas, {}, [&](auto* area) {
    const auto p = area->mapTo(layout, QPoint{});
    return p.y() * layout->width() + p.x();
  });
  auto* source = areas.front();
  auto* grip = source->findChild<QWidget*>("MapViewAreaGrip");
  const auto start = grip->mapToGlobal(grip->rect().center());
  const auto destination = targetName == "map-area-dock-preview"
                             ? areas.back()->mapToGlobal(areas.back()->rect().center())
                           : targetName.endsWith("join-preview")
                             ? areas[1]->mapToGlobal(QPoint{8, areas[1]->height() / 2})
                             : source->mapToGlobal(source->rect().center());
  auto press = QMouseEvent{
    QEvent::MouseButtonPress,
    QPointF{grip->rect().center()},
    QPointF{start},
    Qt::LeftButton,
    Qt::LeftButton,
    Qt::NoModifier};
  QApplication::sendEvent(grip, &press);
  auto move = QMouseEvent{
    QEvent::MouseMove,
    QPointF{grip->mapFromGlobal(destination)},
    QPointF{destination},
    Qt::NoButton,
    Qt::LeftButton,
    Qt::NoModifier};
  QApplication::sendEvent(grip, &move);
  targetWidget.setProperty("mapAreaGestureStarted", true);
}

void configureSnapshot(QWidget& targetWidget, const QString& targetName)
{
  if (targetName == "terrain" || targetName == "terrain-collapsed")
  {
    if (auto* window = qobject_cast<MapWindow*>(&targetWidget))
    {
      window->switchToInspectorPage(InspectorPage::Terrain);
      for (const auto* name : {"Creation", "Editing", "Compilation"})
      {
        if (
          auto* section = window->findChild<CollapsibleTitledPanel*>(
            QStringLiteral("TerrainInspector_%1").arg(name)))
        {
          section->setExpanded(targetName == "terrain" && QString{name} != "Compilation");
          if (QString{name} == "Editing")
            section->findChild<ClickableTitleBar*>()->setFocus(Qt::OtherFocusReason);
        }
      }
      if (auto* splitter = window->findChild<QSplitter*>("MapWindow_HorizontalSplitter"))
        splitter->setSizes({1100, 340});
    }
  }
  else if (targetName == "map-areas" || targetName.startsWith("map-area-"))
  {
    configureMapAreaSnapshot(targetWidget, targetName);
  }
  else if (targetName.startsWith(QStringLiteral("project-")))
  {
    configureWorkspaceSnapshot(targetWidget, targetName);
  }
  else if (targetName == QStringLiteral("goldsrc-entity-links"))
  {
    configureGoldSrcEntitySnapshot(targetWidget);
  }
  else if (
    targetName == QStringLiteral("entity-directions")
    || targetName == QStringLiteral("goldsrc-light-directions"))
  {
    configureEntityDirectionSnapshot(
      targetWidget, targetName == QStringLiteral("goldsrc-light-directions"));
  }
  else if (isOutlinerSnapshotTarget(targetName))
  {
    configureOutlinerSnapshot(targetWidget, targetName);
  }
  else if (targetName == "issues" || targetName == "issues-empty")
  {
    configureIssuesSnapshot(targetWidget, targetName);
  }
  else if (targetName == QStringLiteral("supporting"))
  {
    configureSupportingSnapshot(targetWidget);
  }
  else if (targetName == QStringLiteral("output-log"))
  {
    if (auto* mapWindow = qobject_cast<MapWindow*>(&targetWidget))
    {
      mapWindow->switchToInfoPanelPage(InfoPanelPage::Console);
      mapWindow->findChild<QComboBox*>("ProjectToolsMode")->setCurrentIndex(1);
      mapWindow->findChild<QSplitter*>("ProjectToolsSplitter")->setSizes({560, 340});
    }
  }
  else if (targetName == QStringLiteral("path-tool-preview"))
  {
    configurePathToolSnapshot(targetWidget);
  }
  else if (targetName == QStringLiteral("python-console"))
  {
    configurePythonConsoleSnapshot(targetWidget);
  }
  else if (isInspectorSnapshotTarget(targetName))
  {
    configureInspectorSnapshot(targetWidget, targetName);
  }
  else if (targetName.startsWith(QStringLiteral("preferences")))
  {
    configurePreferencesSnapshot(targetWidget, targetName);
  }
}

void failUiSnapshot(
  QApplication& app,
  const UiSnapshotCommandLineOptions& options,
  const QString& message,
  const int exitCode)
{
  qCritical().noquote() << message;

  const auto outputInfo = QFileInfo{options.outputPath};
  if (QDir{}.mkpath(outputInfo.absolutePath()))
  {
    auto failureFile =
      QFile{outputInfo.dir().filePath(outputInfo.completeBaseName() + ".error.txt")};
    if (failureFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
      QTextStream{&failureFile} << message << Qt::endl;
    }
  }
  app.exit(exitCode);
}

void captureUiSnapshot(
  QApplication& app,
  const QPointer<QWidget>& guardedWidget,
  const QString& targetName,
  const UiSnapshotCommandLineOptions& options)
{
  if (guardedWidget.isNull())
  {
    failUiSnapshot(app, options, "UI snapshot target was destroyed before capture", 3);
    return;
  }

  const auto readiness = uiSnapshotReadiness(*guardedWidget, targetName);
  if (readiness.state != UiSnapshotReadinessState::Ready)
  {
    failUiSnapshot(
      app,
      options,
      QStringLiteral("UI snapshot state validation failed: %1").arg(readiness.detail),
      3);
    return;
  }

  auto error = QString{};
  const auto snapshotOptions = UiSnapshotOptions{
    options.outputPath,
    targetName,
    options.theme,
    qEnvironmentVariable("QT_SCALE_FACTOR", "system")};
  if (!saveUiSnapshot(*guardedWidget, snapshotOptions, &error))
  {
    failUiSnapshot(app, options, QStringLiteral("UI snapshot failed: %1").arg(error), 4);
    return;
  }

  qInfo().noquote() << "UI snapshot saved:" << options.outputPath;
  app.exit(0);
}

void waitForUiSnapshotReadiness(
  QApplication& app,
  const QPointer<QWidget>& guardedWidget,
  const QString& targetName,
  const UiSnapshotCommandLineOptions& options,
  const QDeadlineTimer deadline)
{
  if (guardedWidget.isNull())
  {
    failUiSnapshot(
      app, options, "UI snapshot target was destroyed while waiting for resources", 3);
    return;
  }

  const auto readiness = uiSnapshotReadiness(*guardedWidget, targetName);
  if (readiness.state == UiSnapshotReadinessState::Failed)
  {
    failUiSnapshot(
      app,
      options,
      QStringLiteral("UI snapshot resource validation failed: %1").arg(readiness.detail),
      3);
    return;
  }
  if (readiness.state == UiSnapshotReadinessState::Pending)
  {
    if (deadline.hasExpired())
    {
      failUiSnapshot(
        app,
        options,
        QStringLiteral("UI snapshot resource wait timed out: %1").arg(readiness.detail),
        3);
      return;
    }

    QTimer::singleShot(50, &app, [&app, guardedWidget, targetName, options, deadline]() {
      waitForUiSnapshotReadiness(app, guardedWidget, targetName, options, deadline);
    });
    return;
  }

  if (!readiness.detail.isEmpty())
  {
    qInfo().noquote() << "UI snapshot resources ready:" << readiness.detail;
  }
  guardedWidget->update();
  QTimer::singleShot(100, &app, [&app, guardedWidget, targetName, options]() {
    captureUiSnapshot(app, guardedWidget, targetName, options);
  });
}

void scheduleUiSnapshot(
  QApplication& app,
  QWidget& targetWidget,
  const QString& targetName,
  const UiSnapshotCommandLineOptions& options)
{
  const auto guardedWidget = QPointer<QWidget>{&targetWidget};
  QTimer::singleShot(0, &app, [&app, guardedWidget, targetName, options]() {
    if (guardedWidget.isNull())
    {
      failUiSnapshot(
        app,
        options,
        QStringLiteral("UI snapshot target was destroyed before layout: %1")
          .arg(targetName),
        3);
      return;
    }

    configureSnapshot(*guardedWidget, targetName);
    guardedWidget->ensurePolished();
    guardedWidget->update();
    QTimer::singleShot(250, &app, [&app, guardedWidget, targetName, options]() {
      if (!guardedWidget.isNull())
      {
        configureSnapshot(*guardedWidget, targetName);
      }
      waitForUiSnapshotReadiness(
        app, guardedWidget, targetName, options, QDeadlineTimer{5000});
    });
  });
}

} // namespace

int runUiSnapshot(
  QApplication& app,
  AppController& appController,
  const UiSnapshotCommandLineOptions& options,
  const QStringList& fileNames,
  const OpenFilesForUiSnapshot& openFiles)
{
  if (!configureGamePath(appController, options, fileNames))
  {
    return 3;
  }

  auto* targetWidget = static_cast<QWidget*>(nullptr);
  auto targetName = QString{};
  auto componentSnapshot = std::unique_ptr<QWidget>{};
  auto preferencesDialog = std::unique_ptr<PreferenceDialog>{};
  auto mapSettingsDialog = std::unique_ptr<MapSettingsDialog>{};
  auto compilationCamera = gl::PerspectiveCamera{};
  auto compilationDialog = std::unique_ptr<CompilationDialog>{};
  auto commandPaletteDialog = std::unique_ptr<CommandPaletteDialog>{};
  auto workspaceSnapshot = std::unique_ptr<UiWorkspaceSnapshot>{};
  auto assetLibrarySnapshot = std::unique_ptr<QWidget>{};

  if (options.page == QStringLiteral("components"))
  {
    componentSnapshot = createUiComponentSnapshot();
    targetWidget = componentSnapshot.get();
    targetName = options.page;
    targetWidget->resize(1080, 760);
  }
  else if (options.page.startsWith(QStringLiteral("asset-library")))
  {
    const auto kind =
      options.page == QStringLiteral("asset-library-builtin")
        ? UiAssetLibrarySnapshotKind::Builtin
      : options.page == QStringLiteral("asset-library-material")
        ? UiAssetLibrarySnapshotKind::TerrainMaterial
      : (options.page == QStringLiteral("asset-library-project")
         || options.page == QStringLiteral("asset-library-project-narrow"))
        ? UiAssetLibrarySnapshotKind::Project
        : UiAssetLibrarySnapshotKind::Library;
    assetLibrarySnapshot = createUiAssetLibrarySnapshot(kind);
    if (!assetLibrarySnapshot)
    {
      qCritical() << "Could not build the asset library snapshot fixture";
      return 3;
    }
    targetWidget = assetLibrarySnapshot.get();
    targetName = options.page;
    // The narrow targets keep the same panes but check that they degrade instead of
    // clipping: they are the only UI snapshot below 1000 logical pixels.
    targetWidget->resize(
      options.page.endsWith(QStringLiteral("-narrow")) ? QSize{900, 640}
                                                       : QSize{1180, 760});
  }
  else if (options.page.startsWith(QStringLiteral("preferences")))
  {
    preferencesDialog = std::make_unique<PreferenceDialog>(appController, nullptr);
    targetWidget = preferencesDialog.get();
    targetName = options.page;
    targetWidget->resize(
      targetName == QStringLiteral("preferences-general") ? QSize{920, 560}
                                                          : QSize{960, 640});
    configurePreferencesSnapshot(*targetWidget, targetName, options.theme);
  }
  else if (fileNames.empty())
  {
    targetWidget = findWelcomeWindow();
    targetName = QStringLiteral("welcome");
    if (targetWidget != nullptr)
    {
      targetWidget->resize(700, 500);
    }
  }
  else
  {
    targetName =
      options.page == QStringLiteral("map") ? QStringLiteral("workbench") : options.page;
    if (openFiles(fileNames))
    {
      auto* mapWindow = appController.mapWindowManager().topMapWindow();
      if (mapWindow != nullptr)
      {
        if (options.page.startsWith(QStringLiteral("project-")))
        {
          workspaceSnapshot = std::make_unique<UiWorkspaceSnapshot>();
          const auto result = workspaceSnapshot->initialize(
            appController, pathFromQString(fileNames.front()), options.page);
          if (result.is_error())
          {
            qCritical().noquote()
              << QString::fromStdString(std::get<Error>(result.error()).msg);
            return 3;
          }
          targetWidget = workspaceSnapshot->widget();
        }
        else if (options.page == QStringLiteral("map-settings"))
        {
          mapSettingsDialog =
            std::make_unique<MapSettingsDialog>(mapWindow->document(), mapWindow);
          targetWidget = mapSettingsDialog.get();
        }
        else if (options.page.startsWith(QStringLiteral("compilation-")))
        {
          compilationDialog = std::make_unique<CompilationDialog>(
            appController, mapWindow->document(), compilationCamera, mapWindow, false);
          compilationDialog->findChild<CompilationProfileManager*>()->addHltoolsProfile();
          if (options.page == "compilation-error")
            compilationDialog->findChild<QLineEdit*>("HltoolsParameters")
              ->setText("${MISSING_COMPILER_OPTION}");
          else if (options.page == "compilation-simulated")
            compilationDialog->findChild<QPushButton*>("CompilationSimulate")->click();
          targetWidget = compilationDialog.get();
          targetWidget->resize(1080, 720);
          if (options.page == "compilation-options")
          {
            compilationDialog->findChild<QLineEdit*>("HltoolsParameters")
              ->setText("-noskyclip -allleaks -bounce 4");
            compilationDialog->findChild<CollapsibleTitledPanel*>("HltoolsOptionsPanel")
              ->expand();
            targetWidget->resize(1080, 860);
            for (auto* splitter : targetWidget->findChildren<QSplitter*>())
              if (splitter->orientation() == Qt::Vertical)
                splitter->setSizes({560, 240});
          }
        }
        else if (options.page == QStringLiteral("command-palette"))
        {
          auto context = ActionExecutionContext{
            appController, mapWindow, mapWindow->currentMapViewBase()};
          commandPaletteDialog = std::make_unique<CommandPaletteDialog>(
            appController.actionManager(),
            context,
            std::filesystem::path{"Menu/View/Command Palette..."},
            mapWindow);
          targetWidget = commandPaletteDialog.get();
          targetWidget->resize(640, 480);
        }
        else
        {
          targetWidget = mapWindow;
          mapWindow->resize(1440, 900);
          if (isOutlinerSnapshotTarget(targetName))
          {
            mapWindow->switchToInspectorPage(InspectorPage::Outliner);
          }
          configureSnapshot(*mapWindow, targetName);
        }
      }
    }
  }

  if (targetWidget == nullptr)
  {
    qCritical() << "UI snapshot target was not created:" << targetName;
    return 3;
  }

  targetWidget->setAttribute(Qt::WA_DontShowOnScreen);
  targetWidget->show();
  scheduleUiSnapshot(app, *targetWidget, targetName, options);
  return app.exec();
}

} // namespace tb::ui
