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
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>

#include "UiComponentSnapshot.h"
#include "base/PreferenceManager.h"
#include "fs/DiskIO.h"
#include "gl/Material.h"
#include "gl/MaterialManager.h"
#include "gl/Resource.h"
#include "gl/Texture.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/EntityNode.h"
#include "mdl/GameManager.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/MapHeader.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/Node.h"
#include "mdl/WorldNode.h"
#include "prefs/Preferences.h"
#include "ui/ActionExecutionContext.h"
#include "ui/AppController.h"
#include "ui/CommandPaletteDialog.h"
#include "ui/InfoPanel.h"
#include "ui/Inspector.h"
#include "ui/MapDocument.h"
#include "ui/MapViewToolBox.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/MaterialBrowser.h"
#include "ui/PathTool.h"
#include "ui/PreferenceDialog.h"
#include "ui/QPathUtils.h"
#include "ui/ThemeRegistry.h"
#include "ui/UiSnapshot.h"
#include "ui/WelcomeWindow.h"

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
    QStringLiteral("outliner"),
    QStringLiteral("outliner-hierarchy"),
    QStringLiteral("outliner-filter"),
    QStringLiteral("outliner-properties-entity"),
    QStringLiteral("outliner-reparent-layer-before"),
    QStringLiteral("outliner-reparent-layer-after"),
    QStringLiteral("outliner-brush-entity-before"),
    QStringLiteral("outliner-brush-entity-after"),
    QStringLiteral("entity-browser"),
    QStringLiteral("entity-browser-empty"),
    QStringLiteral("face-inspector"),
    QStringLiteral("material-browser-empty"),
    QStringLiteral("plugin-inspector"),
    QStringLiteral("supporting"),
    QStringLiteral("path-tool-preview"),
    QStringLiteral("python-console"),
    QStringLiteral("command-palette"),
    QStringLiteral("components"),
    QStringLiteral("preferences"),
    QStringLiteral("preferences-colors"),
    QStringLiteral("preferences-mouse"),
    QStringLiteral("preferences-keyboard"),
    QStringLiteral("preferences-misc")};
  return pages.contains(page);
}

bool uiSnapshotPageRequiresMap(const QString& page)
{
  return page != QStringLiteral("map") && page != QStringLiteral("components")
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

  return fs::Disk::withInputStream(pathFromQString(fileNames.front()), mdl::readMapHeader)
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

  if (
    targetName == QStringLiteral("outliner-properties-entity")
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
    || targetName == QStringLiteral("outliner-properties-entity");

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
    horizontalSplitter->setSizes(QList<int>{860, 580});
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
}

void configureSupportingSnapshot(QWidget& targetWidget)
{
  auto* mapWindow = qobject_cast<MapWindow*>(&targetWidget);
  if (mapWindow == nullptr)
  {
    return;
  }

  mapWindow->switchToInfoPanelPage(InfoPanelPage::Assets);
  if (
    auto* splitter = mapWindow->findChild<QSplitter*>(
      QStringLiteral("MapWindow_VerticalSplitterSplitter")))
  {
    splitter->setSizes(QList<int>{560, 340});
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
  if (
    auto* splitter = mapWindow->findChild<QSplitter*>(
      QStringLiteral("MapWindow_VerticalSplitterSplitter")))
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

  if (
    targetName == QStringLiteral("outliner-hierarchy")
    || targetName == QStringLiteral("outliner-filter"))
  {
    expectedNode = findSnapshotGroup(worldNode, "Entry Hall");
    expectedDescription = QStringLiteral("Entry Hall group");
  }
  else if (targetName == QStringLiteral("outliner-properties-entity"))
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
      auto* content = mapWindow->addPluginPanel(QStringLiteral("Chamfer Tool"));
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
    mapWindow->switchToInspectorPage(
      showEntityBrowser ? InspectorPage::Entity : InspectorPage::Face);
  }
  if (
    auto* splitter =
      mapWindow->findChild<QSplitter*>(QStringLiteral("MapWindow_HorizontalSplitter")))
  {
    splitter->setSizes(QList<int>{940, 500});
  }

  if (targetName.endsWith(QStringLiteral("-empty")))
  {
    const auto searchName = showEntityBrowser ? QStringLiteral("EntityBrowser_Search")
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
    const auto row = targetName == QStringLiteral("preferences-colors")     ? 2
                     : targetName == QStringLiteral("preferences-mouse")    ? 3
                     : targetName == QStringLiteral("preferences-keyboard") ? 4
                     : targetName == QStringLiteral("preferences-misc")     ? 5
                                                                            : 1;
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

  if (targetName == QStringLiteral("preferences-misc"))
  {
    if (auto* pages = targetWidget.findChild<QStackedWidget*>(
          QStringLiteral("PreferenceDialog_Pages"));
        pages != nullptr)
    {
      if (
        auto* scrollArea = pages->currentWidget()->findChild<QScrollArea*>(
          QStringLiteral("PreferencePane_ScrollArea")))
      {
        scrollArea->verticalScrollBar()->setValue(
          scrollArea->verticalScrollBar()->maximum());
      }
    }
  }
}

void configureSnapshot(QWidget& targetWidget, const QString& targetName)
{
  if (isOutlinerSnapshotTarget(targetName))
  {
    configureOutlinerSnapshot(targetWidget, targetName);
  }
  else if (targetName == QStringLiteral("supporting"))
  {
    configureSupportingSnapshot(targetWidget);
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
  auto commandPaletteDialog = std::unique_ptr<CommandPaletteDialog>{};

  if (options.page == QStringLiteral("components"))
  {
    componentSnapshot = createUiComponentSnapshot();
    targetWidget = componentSnapshot.get();
    targetName = options.page;
    targetWidget->resize(1080, 760);
  }
  else if (options.page.startsWith(QStringLiteral("preferences")))
  {
    preferencesDialog = std::make_unique<PreferenceDialog>(appController, nullptr);
    targetWidget = preferencesDialog.get();
    targetName = options.page;
    targetWidget->resize(
      targetName == QStringLiteral("preferences-misc") ? QSize{920, 560}
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
        if (options.page == QStringLiteral("command-palette"))
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
