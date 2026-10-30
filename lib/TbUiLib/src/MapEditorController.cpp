/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/MapEditorController.h"

#include <QApplication>
#include <QClipboard>
#include <QInputDialog>
#include <QMessageBox>
#include <QMimeData>
#include <QRegularExpression>

#include "base/PreferenceManager.h"
#include "mdl/EditorContext.h"
#include "mdl/Grid.h"
#include "mdl/GroupNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Brushes.h"
#include "mdl/Map_CopyPaste.h"
#include "mdl/Map_Geometry.h"
#include "mdl/Map_Groups.h"
#include "mdl/Map_NodeVisibility.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Patches.h"
#include "mdl/Map_Selection.h"
#include "mdl/PasteType.h"
#include "mdl/WorldNode.h"
#include "prefs/Preferences.h"
#include "ui/ChamferTool.h"
#include "ui/ClipTool.h"
#include "ui/Console.h"
#include "ui/EdgeTool.h"
#include "ui/FaceTool.h"
#include "ui/MapDocument.h"
#include "ui/MapEditorView.h"
#include "ui/MapViewToolBox.h"
#include "ui/QStringUtils.h"
#include "ui/QVecUtils.h"
#include "ui/ReplaceMaterialDialog.h"
#include "ui/SwitchableMapViewContainer.h"
#include "ui/VertexTool.h"
#include "ui/ViewUtils.h"

#include "kd/contracts.h"

#include "vm/vec.h"
#include "vm/vec_io.h"

#include <string>
#include <vector>

namespace tb::ui
{
namespace
{
bool widgetOrChildHasFocus(const QWidget* widget)
{
  contract_pre(widget != nullptr);

  const auto* focusWidget = QApplication::focusWidget();
  return widget == focusWidget || widget->isAncestorOf(focusWidget);
}

} // namespace

MapEditorController::MapEditorController(
  AppController& app, MapDocument& document, MapEditorView& editor)
  : m_appController{app}
  , m_document{document}
  , m_editor{editor}
{
}

void MapEditorController::cutSelection()
{
  if (canCutSelection())
  {
    auto& map = m_document.map();

    copyToClipboard();
    auto transaction = mdl::Transaction{map, "Cut"};
    removeSelectedNodes(map);
    transaction.commit();
  }
}

void MapEditorController::copySelection()
{
  if (canCopySelection())
  {
    copyToClipboard();
  }
}

void MapEditorController::copyToClipboard()
{
  auto& map = m_document.map();
  const auto& selection = map.selection();
  auto str = selection.hasNodes()        ? serializeSelectedNodes(map)
             : selection.hasBrushFaces() ? serializeSelectedBrushFaces(map)
                                         : std::string{};
  if (selection.hasNodes() && pref(Preferences::PrefixWorldspawnHeaderOnCopy))
  {
    str = serializeWorldspawnHeader(map) + str;
  }

  auto* clipboard = QApplication::clipboard();
  clipboard->setText(mapStringToUnicode(map.encoding(), str));
}

bool MapEditorController::canCutSelection() const
{
  const auto& map = m_document.map();
  const auto& selection = map.selection();
  return widgetOrChildHasFocus(m_editor.mapView()) && selection.hasNodes()
         && !m_editor.mapView()->toolBox().anyModalToolActive();
}

bool MapEditorController::canCopySelection() const
{
  const auto& map = m_document.map();
  const auto& selection = map.selection();
  return widgetOrChildHasFocus(m_editor.mapView())
         && (selection.hasNodes() || selection.hasBrushFaces());
}

void MapEditorController::pasteAtCursorPosition()
{
  if (canPaste())
  {
    auto& map = m_document.map();
    const auto referenceBounds = map.referenceBounds();

    auto transaction = mdl::Transaction{map, "Paste"};
    switch (paste())
    {
    case mdl::PasteType::Node:
      if (const auto bounds = map.selectionBounds())
      {
        // The pasted objects must be hidden to prevent the picking done in
        // pasteObjectsDelta from hitting them
        // (https://github.com/TrenchBroom/TrenchBroom/issues/2755)
        const auto nodes = map.selection().nodes;

        hideNodes(map, nodes);
        const auto delta =
          m_editor.mapView()->pasteObjectsDelta(*bounds, referenceBounds);
        showNodes(map, nodes);
        selectNodes(map, nodes); // Hiding deselected the nodes, so reselect them
        if (!translateSelection(map, delta))
        {
          transaction.cancel();
          break;
        }
      }
      transaction.commit();
      break;
    case mdl::PasteType::BrushFace:
      transaction.commit();
      break;
    case mdl::PasteType::Failed:
      transaction.cancel();
      break;
    }
  }
}

void MapEditorController::pasteAtOriginalPosition()
{
  if (canPaste())
  {
    paste();
  }
}

mdl::PasteType MapEditorController::paste()
{
  auto* clipboard = QApplication::clipboard();
  const auto qtext = clipboard->text();

  if (qtext.isEmpty())
  {
    m_editor.console().error() << "Clipboard is empty";
    return mdl::PasteType::Failed;
  }

  auto& map = m_document.map();
  return mdl::paste(map, mapStringFromUnicode(map.encoding(), qtext));
}

/**
 * This is relatively expensive so only call it when the clipboard changes or e.g. the
 * user tries to paste.
 */
bool MapEditorController::canPaste() const
{
  if (!widgetOrChildHasFocus(m_editor.mapView()) || !m_editor.mapView()->isCurrent())
  {
    return false;
  }

  const auto* clipboard = QApplication::clipboard();
  const auto* mimeData = clipboard->mimeData();
  return mimeData && mimeData->hasText();
}

void MapEditorController::duplicateSelection()
{
  if (canDuplicateSelection())
  {
    duplicateSelectedNodes(m_document.map());
  }
}

bool MapEditorController::canDuplicateSelection() const
{
  const auto& map = m_document.map();
  return map.selection().hasNodes();
}

void MapEditorController::deleteSelection()
{
  if (canDeleteSelection())
  {
    auto& toolBox = m_editor.mapView()->toolBox();

    if (toolBox.clipToolActive())
    {
      toolBox.clipTool().removeLastPoint();
    }
    else if (toolBox.vertexToolActive())
    {
      toolBox.vertexTool().removeSelection();
    }
    else if (toolBox.edgeToolActive())
    {
      toolBox.edgeTool().removeSelection();
    }
    else if (toolBox.chamferToolActive())
    {
      toolBox.chamferTool().removeSelection();
    }
    else if (toolBox.faceToolActive())
    {
      toolBox.faceTool().removeSelection();
    }
    else if (!toolBox.anyModalToolActive())
    {
      auto& map = m_document.map();
      removeSelectedNodes(map);
    }
  }
}

bool MapEditorController::canDeleteSelection() const
{
  auto& toolBox = m_editor.mapView()->toolBox();

  if (toolBox.clipToolActive())
  {
    return toolBox.clipTool().canRemoveLastPoint();
  }
  if (toolBox.vertexToolActive())
  {
    return toolBox.vertexTool().canRemoveSelection();
  }
  if (toolBox.edgeToolActive())
  {
    return toolBox.edgeTool().canRemoveSelection();
  }
  if (toolBox.chamferToolActive())
  {
    return toolBox.chamferTool().canRemoveSelection();
  }
  if (toolBox.faceToolActive())
  {
    return toolBox.faceTool().canRemoveSelection();
  }
  return canCutSelection();
}

void MapEditorController::selectAll()
{
  if (canSelect())
  {
    selectAllNodes(m_document.map());
  }
}

void MapEditorController::selectSiblings()
{
  if (canSelectSiblings())
  {
    selectSiblingNodes(m_document.map());
  }
}

void MapEditorController::selectTouching()
{
  if (canSelectByBrush())
  {
    selectTouchingNodes(m_document.map(), true);
  }
}

void MapEditorController::selectInside()
{
  if (canSelectByBrush())
  {
    selectContainedNodes(m_document.map(), true);
  }
}

void MapEditorController::selectTall()
{
  if (canSelectTall())
  {
    m_editor.mapView()->selectTall();
  }
}

void MapEditorController::selectByLineNumber()
{
  if (canSelect())
  {
    const auto string = QInputDialog::getText(
      &m_editor,
      "Select by Line Numbers",
      "Enter a comma- or space separated list of line numbers.");
    if (!string.isEmpty())
    {
      auto positions = std::vector<size_t>{};
      for (const auto& token : string.split(QRegularExpression{"[, ]"}))
      {
        auto ok = false;
        const auto position = token.toLong(&ok);
        if (ok && position > 0)
        {
          positions.push_back(static_cast<size_t>(position));
        }
      }

      selectNodesWithFilePosition(m_document.map(), positions);
    }
  }
}

void MapEditorController::selectInverse()
{
  if (canSelectInverse())
  {
    invertNodeSelection(m_document.map());
  }
}

void MapEditorController::selectNone()
{
  if (canDeselect())
  {
    auto& map = m_document.map();
    deselectAll(map);
  }
}

bool MapEditorController::canSelect() const
{
  return canChangeSelection();
}

bool MapEditorController::canSelectSiblings() const
{
  auto& map = m_document.map();
  return canChangeSelection() && map.selection().hasNodes();
}

bool MapEditorController::canSelectByBrush() const
{
  auto& map = m_document.map();
  return canChangeSelection() && map.selection().hasOnlyBrushes();
}

bool MapEditorController::canSelectTall() const
{
  auto& map = m_document.map();
  return canChangeSelection() && map.selection().hasOnlyBrushes()
         && m_editor.mapView()->canSelectTall();
}

bool MapEditorController::canDeselect() const
{
  auto& map = m_document.map();
  return canChangeSelection() && map.selection().hasNodes();
}

bool MapEditorController::canChangeSelection() const
{
  auto& map = m_document.map();
  return map.editorContext().canChangeSelection();
}

bool MapEditorController::canSelectInverse() const
{
  auto& map = m_document.map();
  return map.editorContext().canChangeSelection();
}

void MapEditorController::groupSelectedObjects()
{
  if (canGroupSelectedObjects())
  {
    const auto name = queryGroupName(&m_editor, "Unnamed");
    if (!name.empty())
    {
      groupSelectedNodes(m_document.map(), name);
    }
  }
}

bool MapEditorController::canGroupSelectedObjects() const
{
  auto& map = m_document.map();
  return map.selection().hasNodes()
         && !m_editor.mapView()->toolBox().anyModalToolActive();
}

void MapEditorController::ungroupSelectedObjects()
{
  if (canUngroupSelectedObjects())
  {
    ungroupSelectedNodes(m_document.map());
  }
}

bool MapEditorController::canUngroupSelectedObjects() const
{
  auto& map = m_document.map();
  return map.selection().hasGroups()
         && !m_editor.mapView()->toolBox().anyModalToolActive();
}

void MapEditorController::renameSelectedGroups()
{
  if (canRenameSelectedGroups())
  {
    auto& map = m_document.map();
    contract_assert(map.selection().hasOnlyGroups());

    const auto suggestion = map.selection().groups.front()->name();
    const auto name = queryGroupName(&m_editor, suggestion);
    if (!name.empty())
    {
      mdl::renameSelectedGroups(map, name);
    }
  }
}

bool MapEditorController::canRenameSelectedGroups() const
{
  auto& map = m_document.map();
  return map.selection().hasOnlyGroups();
}

void MapEditorController::replaceMaterial()
{
  auto dialog = ReplaceMaterialDialog{m_appController, m_document, &m_editor};
  dialog.exec();
}

void MapEditorController::moveSelectedObjects()
{
  auto ok = false;
  const auto str = QInputDialog::getText(
    &m_editor,
    "Move Objects",
    "Enter coordinates: X Y Z",
    QLineEdit::Normal,
    toString(vm::vec3d{}),
    &ok);
  if (ok)
  {
    if (const auto offset = parse<double, 3>(str))
    {
      auto& map = m_document.map();
      translateSelection(map, *offset);
    }
    else
    {
      QMessageBox::warning(
        &m_editor, "Error", MapEditorView::tr("Invalid coordinates: '%1'").arg(str));
    }
  }
}

bool MapEditorController::canMoveSelectedObjects() const
{
  auto& map = m_document.map();
  return map.selection().hasNodes()
         && !m_editor.mapView()->toolBox().anyModalToolActive();
}

void MapEditorController::togglePathTool()
{
  if (canTogglePathTool())
  {
    m_editor.mapView()->togglePathTool();
  }
}

bool MapEditorController::canTogglePathTool() const
{
  return m_editor.mapView()->canTogglePathTool();
}

bool MapEditorController::pathToolActive() const
{
  return m_editor.mapView()->pathToolActive();
}

void MapEditorController::performPathCreation()
{
  if (pathToolActive())
  {
    m_editor.mapView()->performPathCreation();
    m_editor.mapView()->togglePathTool();
  }
}

void MapEditorController::removeLastPathPoint()
{
  if (pathToolActive())
  {
    m_editor.mapView()->removeLastPathPoint();
  }
}

void MapEditorController::csgConvexMerge()
{
  if (canDoCsgConvexMerge())
  {
    auto& toolBox = m_editor.mapView()->toolBox();

    if (toolBox.vertexToolActive() && toolBox.vertexTool().canDoCsgConvexMerge())
    {
      toolBox.vertexTool().csgConvexMerge();
    }
    else if (toolBox.edgeToolActive() && toolBox.edgeTool().canDoCsgConvexMerge())
    {
      toolBox.edgeTool().csgConvexMerge();
    }
    else if (toolBox.faceToolActive() && toolBox.faceTool().canDoCsgConvexMerge())
    {
      toolBox.faceTool().csgConvexMerge();
    }
    else
    {
      mdl::csgConvexMerge(m_document.map());
    }
  }
}

bool MapEditorController::canDoCsgConvexMerge() const
{
  const auto& toolBox = m_editor.mapView()->toolBox();
  const auto& map = m_document.map();
  const auto& selection = map.selection();

  return (selection.hasBrushFaces() && selection.brushFaces.size() > 1)
         || (selection.hasOnlyBrushes() && selection.brushes.size() > 1)
         || (toolBox.vertexToolActive() && toolBox.vertexTool().canDoCsgConvexMerge())
         || (toolBox.edgeToolActive() && toolBox.edgeTool().canDoCsgConvexMerge())
         || (toolBox.faceToolActive() && toolBox.faceTool().canDoCsgConvexMerge());
}

void MapEditorController::csgSubtract()
{
  if (canDoCsgSubtract())
  {
    mdl::csgSubtract(m_document.map());
  }
}

bool MapEditorController::canDoCsgSubtract() const
{
  const auto& map = m_document.map();
  const auto& selection = map.selection();
  return selection.hasOnlyBrushes() && !selection.brushes.empty();
}

void MapEditorController::csgHollow()
{
  if (canDoCsgHollow())
  {
    mdl::csgHollow(m_document.map());
  }
}

bool MapEditorController::canDoCsgHollow() const
{
  const auto& map = m_document.map();
  const auto& selection = map.selection();
  return selection.hasOnlyBrushes() && !selection.brushes.empty();
}

void MapEditorController::csgIntersect()
{
  if (canDoCsgIntersect())
  {
    mdl::csgIntersect(m_document.map());
  }
}

bool MapEditorController::canDoCsgIntersect() const
{
  const auto& map = m_document.map();
  const auto& selection = map.selection();
  return selection.hasOnlyBrushes() && selection.brushes.size() > 1;
}

void MapEditorController::snapVerticesToInteger()
{
  if (canSnapVertices())
  {
    auto& map = m_document.map();
    snapVertices(map, 1u);
  }
}

void MapEditorController::snapVerticesToGrid()
{
  if (canSnapVertices())
  {
    auto& map = m_document.map();
    snapVertices(map, map.grid().actualSize());
  }
}

bool MapEditorController::canSnapVertices() const
{
  const auto& map = m_document.map();
  const auto& selection = map.selection();
  return !selection.allBrushes().empty();
}

void MapEditorController::convertSelectionToPatches()
{
  if (canConvertSelectionToPatches())
  {
    auto& map = m_document.map();
    mdl::convertSelectionToPatches(map, 3, 3);

    if (!m_editor.mapView()->toolBox().controlPointToolActive())
    {
      m_editor.mapView()->toolBox().toggleControlPointTool();
    }
  }
}

bool MapEditorController::canConvertSelectionToPatches() const
{
  const auto& map = m_document.map();
  const auto& selection = map.selection();
  return mdl::hasPatchSupport(map.worldNode().mapFormat())
         && selection.hasAnyBrushFaces();
}

void MapEditorController::toggleAlignmentLock()
{
  togglePref(Preferences::AlignmentLock);
}

void MapEditorController::toggleUvLock()
{
  togglePref(Preferences::UvLock);
}

void MapEditorController::toggleShowGrid()
{
  auto& map = m_document.map();
  map.grid().toggleVisible();
}

void MapEditorController::toggleSnapToGrid()
{
  auto& map = m_document.map();
  map.grid().toggleSnap();
}

void MapEditorController::incGridSize()
{
  if (canIncGridSize())
  {
    auto& map = m_document.map();
    map.grid().incSize();
  }
}

bool MapEditorController::canIncGridSize() const
{
  const auto& map = m_document.map();
  return map.grid().size() < mdl::Grid::MaxSize;
}

void MapEditorController::decGridSize()
{
  if (canDecGridSize())
  {
    auto& map = m_document.map();
    map.grid().decSize();
  }
}

bool MapEditorController::canDecGridSize() const
{
  const auto& map = m_document.map();
  return map.grid().size() > mdl::Grid::MinSize;
}

void MapEditorController::setGridSize(const int size)
{
  auto& map = m_document.map();
  map.grid().setSize(size);
}

void MapEditorController::isolateSelection()
{
  if (canIsolateSelection())
  {
    isolateSelectedNodes(m_document.map());
  }
}

bool MapEditorController::canIsolateSelection() const
{
  const auto& map = m_document.map();
  return map.selection().hasNodes();
}

void MapEditorController::hideSelection()
{
  if (canHideSelection())
  {
    hideSelectedNodes(m_document.map());
  }
}

bool MapEditorController::canHideSelection() const
{
  const auto& map = m_document.map();
  return map.selection().hasNodes();
}

void MapEditorController::showAll()
{
  showAllNodes(m_document.map());
}

} // namespace tb::ui
