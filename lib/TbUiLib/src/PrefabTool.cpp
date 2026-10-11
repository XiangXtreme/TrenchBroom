/*
 Copyright (C) 2026 XiangXtreme

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

#include "ui/PrefabTool.h"

#include <QMessageBox>
#include <QPushButton>

#include "base/Logger.h"
#include "base/ParserStatus.h"
#include "fs/DiskIO.h"
#include "fs/File.h"
#include "fs/Reader.h"
#include "gl/MaterialManager.h"
#include "mdl/BrushNode.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityProperties.h"
#include "mdl/GameConfig.h"
#include "mdl/GameFileSystem.h"
#include "mdl/GameInfo.h"
#include "mdl/Group.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "mdl/MapReader.h"
#include "mdl/Map_Assets.h"
#include "mdl/Map_CopyPaste.h"
#include "mdl/Map_Geometry.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/NodeReader.h"
#include "mdl/PasteType.h"
#include "mdl/PatchNode.h"
#include "mdl/Transaction.h"
#include "mdl/WadPropertyUtils.h"
#include "mdl/WorldNode.h"
#include "ui/MapDocument.h"
#include "ui/PrefabAsset.h"

#include "kd/path_utils.h"
#include "kd/ranges/to.h"
#include "kd/string_utils.h"
#include "kd/vector_utils.h"

#include "vm/mat_ext.h"

#include <ranges>

namespace tb::ui
{
namespace
{

class SilentParserStatus : public ParserStatus
{
private:
  NullLogger m_logger;

public:
  SilentParserStatus()
    : ParserStatus{m_logger, ""}
  {
  }

private:
  void doProgress(double) override {}
};

class PrefabWadReader : public mdl::MapReader
{
private:
  std::string m_wadPropertyKey;
  std::vector<std::string> m_wadPaths;

public:
  PrefabWadReader(
    const std::string_view str,
    const mdl::MapFormat sourceMapFormat,
    const mdl::MapFormat targetMapFormat,
    const mdl::EntityPropertyConfig& entityPropertyConfig,
    std::string wadPropertyKey)
    : MapReader{str, sourceMapFormat, targetMapFormat, entityPropertyConfig}
    , m_wadPropertyKey{std::move(wadPropertyKey)}
  {
  }

  Result<void> read(
    const vm::bbox3d& worldBounds, ParserStatus& status, kdl::task_manager& taskManager)
  {
    return readEntities(worldBounds, status, taskManager);
  }

  std::vector<std::string> wadPaths()
  {
    std::erase_if(m_wadPaths, [](const auto& path) { return path.empty(); });
    return kdl::vec_sort_and_remove_duplicates(std::move(m_wadPaths));
  }

private:
  mdl::Node* onWorldNode(
    std::unique_ptr<mdl::WorldNode> worldNode, ParserStatus&) override
  {
    if (const auto* wadProperty = worldNode->entity().property(m_wadPropertyKey))
    {
      kdl::vec_append(m_wadPaths, mdl::splitWadProperty(*wadProperty));
    }
    return nullptr;
  }

  void onLayerNode(std::unique_ptr<mdl::Node>, ParserStatus&) override {}

  void onNode(mdl::Node*, std::unique_ptr<mdl::Node>, ParserStatus&) override {}
};

Result<std::string> readPrefabAssetText(
  const mdl::GameFileSystem& gameFileSystem, const std::filesystem::path& path)
{
  if (!isPrefabAssetPath(path))
  {
    return Error{"Not a prefab asset"};
  }

  if (path.is_absolute())
  {
    return ui::readPrefabAsset(path);
  }

  const auto prefix = std::filesystem::path{"prefabs"};
  if (kdl::path_has_prefix(path, prefix))
  {
    return ui::readPrefabAsset(
      configuredPrefabDirectory() / path.lexically_relative(prefix));
  }

  return gameFileSystem.openFile(path) | kdl::transform([](auto file) {
           auto reader = file->reader().buffer();
           return std::string{reader.stringView()};
         });
}

void collectPrefabNodeBounds(std::vector<vm::bbox3d>& bounds, const mdl::Node& node)
{
  node.accept(kdl::overload(
    [&](const mdl::WorldNode& worldNode) {
      for (const auto* child : worldNode.children())
      {
        collectPrefabNodeBounds(bounds, *child);
      }
    },
    [&](const mdl::LayerNode& layerNode) {
      for (const auto* child : layerNode.children())
      {
        collectPrefabNodeBounds(bounds, *child);
      }
    },
    [&](const mdl::GroupNode& groupNode) { bounds.push_back(groupNode.logicalBounds()); },
    [&](const mdl::EntityNode& entityNode) {
      if (mdl::isWorldspawn(entityNode.entity().classname()))
      {
        for (const auto* child : entityNode.children())
        {
          collectPrefabNodeBounds(bounds, *child);
        }
      }
      else
      {
        bounds.push_back(entityNode.logicalBounds());
      }
    },
    [&](const mdl::BrushNode& brushNode) { bounds.push_back(brushNode.logicalBounds()); },
    [&](const mdl::PatchNode& patchNode) {
      bounds.push_back(patchNode.logicalBounds());
    }));
}

vm::bbox3d computePrefabAssetBounds(const std::vector<mdl::Node*>& nodes)
{
  auto bounds = std::vector<vm::bbox3d>{};
  for (const auto* node : nodes)
  {
    collectPrefabNodeBounds(bounds, *node);
  }
  return vm::bbox3d::build(bounds).value_or(vm::bbox3d{});
}

Result<std::vector<mdl::Node*>> readPrefabAssetNodes(
  mdl::Map& map, const std::string& text)
{
  auto parserStatus = SilentParserStatus{};
  return mdl::NodeReader::read(
    text,
    map.worldNode().mapFormat(),
    map.worldBounds(),
    map.worldNode().entityPropertyConfig(),
    parserStatus,
    map.taskManager());
}

bool translatePrefabNode(mdl::Node& node, const vm::vec3d& delta, mdl::Map& map)
{
  const auto transformation = vm::translation_matrix(delta);
  const auto updateAngleProperty =
    map.worldNode().entityPropertyConfig().updateAnglePropertyAfterTransform;

  return node.accept(kdl::overload(
    [&](mdl::WorldNode& worldNode) {
      auto result = true;
      for (auto* child : worldNode.children())
      {
        result = translatePrefabNode(*child, delta, map) && result;
      }
      return result;
    },
    [&](mdl::LayerNode& layerNode) {
      auto result = true;
      for (auto* child : layerNode.children())
      {
        result = translatePrefabNode(*child, delta, map) && result;
      }
      return result;
    },
    [&](mdl::GroupNode& groupNode) {
      auto group = groupNode.group();
      group.transform(transformation);
      groupNode.setGroup(std::move(group));

      auto result = true;
      for (auto* child : groupNode.children())
      {
        result = translatePrefabNode(*child, delta, map) && result;
      }
      return result;
    },
    [&](mdl::EntityNode& entityNode) {
      auto entity = entityNode.entity();
      entity.transform(transformation, updateAngleProperty);
      entityNode.setEntity(std::move(entity));

      auto result = true;
      for (auto* child : entityNode.children())
      {
        result = translatePrefabNode(*child, delta, map) && result;
      }
      return result;
    },
    [&](mdl::BrushNode& brushNode) {
      auto brush = brushNode.brush();
      if (brush.transform(map.worldBounds(), transformation, false).is_error())
      {
        return false;
      }
      brushNode.setBrush(std::move(brush));
      return true;
    },
    [&](mdl::PatchNode& patchNode) {
      auto patch = patchNode.patch();
      patch.transform(transformation);
      patchNode.setPatch(std::move(patch));
      return true;
    }));
}

bool translatePrefabNodes(
  const std::vector<mdl::Node*>& nodes, const vm::vec3d& delta, mdl::Map& map)
{
  auto result = true;
  for (auto* node : nodes)
  {
    result = translatePrefabNode(*node, delta, map) && result;
  }
  return result;
}

void collectPrefabMaterialNames(std::vector<std::string>& result, const mdl::Node& node)
{
  node.accept(kdl::overload(
    [&](const mdl::WorldNode& worldNode) {
      for (const auto* child : worldNode.children())
      {
        collectPrefabMaterialNames(result, *child);
      }
    },
    [&](const mdl::LayerNode& layerNode) {
      for (const auto* child : layerNode.children())
      {
        collectPrefabMaterialNames(result, *child);
      }
    },
    [&](const mdl::GroupNode& groupNode) {
      for (const auto* child : groupNode.children())
      {
        collectPrefabMaterialNames(result, *child);
      }
    },
    [&](const mdl::EntityNode& entityNode) {
      for (const auto* child : entityNode.children())
      {
        collectPrefabMaterialNames(result, *child);
      }
    },
    [&](const mdl::BrushNode& brushNode) {
      for (const auto& face : brushNode.brush().faces())
      {
        const auto& materialName = face.materialName();
        if (!materialName.empty())
        {
          result.push_back(materialName);
        }
      }
    },
    [&](const mdl::PatchNode& patchNode) {
      const auto& materialName = patchNode.patch().materialName();
      if (!materialName.empty())
      {
        result.push_back(materialName);
      }
    }));
}

std::vector<std::string> prefabMaterialNames(mdl::Map& map, const std::string& prefabText)
{
  auto nodesResult = readPrefabAssetNodes(map, prefabText);
  if (nodesResult.is_error())
  {
    return {};
  }

  auto nodes = std::move(nodesResult).value();
  auto result = std::vector<std::string>{};
  for (const auto* node : nodes)
  {
    collectPrefabMaterialNames(result, *node);
  }
  kdl::vec_clear_and_delete(nodes);
  return kdl::vec_sort_and_remove_duplicates(std::move(result));
}

std::vector<std::string> prefabWorldspawnWadPaths(
  mdl::Map& map, const std::string& prefabText)
{
  if (!map.gameInfo().gameConfig.materialConfig.property)
  {
    return {};
  }

  auto parserStatus = SilentParserStatus{};
  for (const auto compatibleMapFormat :
       mdl::compatibleFormats(map.worldNode().mapFormat()))
  {
    auto reader = PrefabWadReader{
      prefabText,
      compatibleMapFormat,
      map.worldNode().mapFormat(),
      map.worldNode().entityPropertyConfig(),
      *map.gameInfo().gameConfig.materialConfig.property};
    if (reader.read(map.worldBounds(), parserStatus, map.taskManager()))
    {
      return reader.wadPaths();
    }
  }
  return {};
}

std::vector<std::string> missingPrefabMaterials(mdl::Map& map, const std::string& text)
{
  auto result = prefabMaterialNames(map, text);
  std::erase_if(result, [&](const auto& materialName) {
    return map.materialManager().material(materialName) != nullptr;
  });
  return result;
}

PrefabTool::PrefabMaterialImportAction defaultMaterialImportCallback(
  const std::vector<std::string>& missingMaterials,
  const std::vector<std::filesystem::path>& materialCollections,
  const std::vector<std::string>& wadPaths)
{
  auto details = QString{};
  for (const auto& materialName : missingMaterials)
  {
    details += QString::fromStdString(materialName) + "\n";
  }
  if (!materialCollections.empty() || !wadPaths.empty())
  {
    details += "\nSources:\n";
    for (const auto& path : materialCollections)
    {
      details += QString::fromStdString(path.generic_string()) + "\n";
    }
    for (const auto& path : wadPaths)
    {
      details += QString::fromStdString(path) + "\n";
    }
  }

  auto messageBox = QMessageBox{};
  messageBox.setWindowTitle(QObject::tr("Place Prefab"));
  messageBox.setText(QObject::tr("This prefab uses materials that are not loaded."));
  messageBox.setInformativeText(
    QObject::tr("Import the prefab material sources into this map?"));
  messageBox.setDetailedText(details.trimmed());

  auto* importButton =
    messageBox.addButton(QObject::tr("Import and Place"), QMessageBox::AcceptRole);
  const auto* continueButton = messageBox.addButton(
    QObject::tr("Place Without Importing"), QMessageBox::DestructiveRole);
  messageBox.addButton(QMessageBox::Cancel);
  messageBox.setDefaultButton(importButton);
  messageBox.exec();

  if (messageBox.clickedButton() == importButton)
  {
    return PrefabTool::PrefabMaterialImportAction::Import;
  }
  if (messageBox.clickedButton() == continueButton)
  {
    return PrefabTool::PrefabMaterialImportAction::ContinueWithoutImport;
  }
  return PrefabTool::PrefabMaterialImportAction::Cancel;
}

void importPrefabMaterialSources(
  mdl::Map& map,
  const std::vector<std::filesystem::path>& materialCollections,
  const std::vector<std::string>& wadPaths)
{
  if (!wadPaths.empty() || !materialCollections.empty())
  {
    auto entity = map.worldNode().entity();
    if (!wadPaths.empty() && map.gameInfo().gameConfig.materialConfig.property)
    {
      auto wads = std::vector<std::string>{};
      if (
        const auto* wadProperty =
          entity.property(*map.gameInfo().gameConfig.materialConfig.property))
      {
        wads = mdl::splitWadProperty(*wadProperty);
      }
      kdl::vec_append(wads, wadPaths);
      entity.addOrUpdateProperty(
        *map.gameInfo().gameConfig.materialConfig.property,
        mdl::joinWadProperty(kdl::vec_sort_and_remove_duplicates(std::move(wads))));
    }

    if (!materialCollections.empty())
    {
      auto enabledMaterialCollections = mdl::enabledMaterialCollections(map);
      kdl::vec_append(enabledMaterialCollections, materialCollections);
      entity.addOrUpdateProperty(
        mdl::EntityPropertyKeys::TbEnabledMaterialCollections,
        kdl::str_join(
          kdl::vec_sort_and_remove_duplicates(std::move(enabledMaterialCollections))
            | std::views::transform([](const auto& path) { return path.string(); })
            | kdl::ranges::to<std::vector>(),
          ";"));
    }

    mdl::updateNodeContents(
      map,
      "Import Prefab Materials",
      {{&map.worldNode(), mdl::NodeContents{std::move(entity)}}},
      {});
  }

  mdl::reloadMaterialCollections(map);
}

bool ensurePrefabMaterials(
  mdl::Map& map,
  const std::string& prefabText,
  const PrefabTool::PrefabMaterialImportCallback& callback)
{
  const auto materialCollections = prefabMaterialCollections(prefabText);
  auto wadPaths = prefabWadPaths(prefabText);
  if (wadPaths.empty())
  {
    wadPaths = prefabWorldspawnWadPaths(map, prefabText);
  }
  if (materialCollections.empty() && wadPaths.empty())
  {
    return true;
  }

  const auto missingMaterials = missingPrefabMaterials(map, prefabText);
  if (missingMaterials.empty())
  {
    return true;
  }

  const auto action = callback ? callback(missingMaterials, materialCollections, wadPaths)
                               : defaultMaterialImportCallback(
                                   missingMaterials, materialCollections, wadPaths);
  switch (action)
  {
  case PrefabTool::PrefabMaterialImportAction::Import:
    importPrefabMaterialSources(map, materialCollections, wadPaths);
    return true;
  case PrefabTool::PrefabMaterialImportAction::ContinueWithoutImport:
    return true;
  case PrefabTool::PrefabMaterialImportAction::Cancel:
    return false;
  }

  return false;
}

} // namespace

PrefabTool::PrefabTool(MapDocument& document)
  : Tool{true}
  , m_document{document}
{
}

bool PrefabTool::canPlacePrefab(const std::filesystem::path& path) const
{
  return isPrefabAssetPath(path);
}

mdl::Map& PrefabTool::map() const
{
  return m_document.map();
}

const std::optional<vm::bbox3d>& PrefabTool::previewBounds() const
{
  return m_previewBounds;
}

const std::vector<mdl::Node*>& PrefabTool::previewNodes() const
{
  return m_previewNodes;
}

size_t PrefabTool::previewVersion() const
{
  return m_previewVersion;
}

void PrefabTool::setMaterialImportCallback(PrefabMaterialImportCallback callback)
{
  m_materialImportCallback = std::move(callback);
}

bool PrefabTool::updatePreview(
  const std::filesystem::path& path,
  const InputState& inputState,
  const PlacementDelta& placementDelta)
{
  auto& map = m_document.map();
  const auto textResult = readPrefabAssetText(map.gameFileSystem(), path);
  if (textResult.is_error())
  {
    clearPreview();
    return false;
  }

  auto nodesResult = readPrefabAssetNodes(map, textResult.value());
  if (nodesResult.is_error())
  {
    clearPreview();
    return false;
  }

  auto nodes = std::move(nodesResult).value();
  const auto bounds = computePrefabAssetBounds(nodes);
  const auto delta = placementDelta(map, inputState, bounds, map.referenceBounds());
  if (!translatePrefabNodes(nodes, delta, map))
  {
    kdl::vec_clear_and_delete(nodes);
    clearPreview();
    return false;
  }

  kdl::vec_clear_and_delete(m_previewNodes);
  m_previewNodes = std::move(nodes);
  m_previewBounds = bounds.translate(delta);
  ++m_previewVersion;
  return true;
}

void PrefabTool::clearPreview()
{
  if (m_previewBounds || !m_previewNodes.empty())
  {
    kdl::vec_clear_and_delete(m_previewNodes);
    m_previewBounds = std::nullopt;
    ++m_previewVersion;
  }
}

bool PrefabTool::placePrefab(
  const std::filesystem::path& path,
  const InputState& inputState,
  const PlacementDelta& placementDelta)
{
  auto& map = m_document.map();
  const auto textResult = readPrefabAssetText(map.gameFileSystem(), path);
  if (textResult.is_error())
  {
    return false;
  }
  if (!ensurePrefabMaterials(map, textResult.value(), m_materialImportCallback))
  {
    return false;
  }

  const auto referenceBounds = map.referenceBounds();
  auto transaction = mdl::Transaction{map, "Place Prefab"};

  if (mdl::paste(map, textResult.value()) != mdl::PasteType::Node)
  {
    transaction.cancel();
    return false;
  }

  if (const auto bounds = map.selectionBounds())
  {
    const auto delta = placementDelta(map, inputState, *bounds, referenceBounds);
    if (!mdl::translateSelection(map, delta))
    {
      transaction.cancel();
      return false;
    }
  }

  return transaction.commit();
}

} // namespace tb::ui
