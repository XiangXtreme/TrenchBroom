/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

namespace tb::mdl
{
enum class PasteType;
}

namespace tb::ui
{
class AppController;
class MapDocument;
class MapEditorView;

/** Document editing commands shared by standalone and project editor views.
 * Borrows its owning view and document; it never resolves a target through a window.
 */
class MapEditorController
{
private:
  AppController& m_appController;
  MapDocument& m_document;
  MapEditorView& m_editor;

public:
  MapEditorController(AppController& app, MapDocument& document, MapEditorView& editor);

  void cutSelection();
  void copySelection();
  void copyToClipboard();
  bool canCutSelection() const;
  bool canCopySelection() const;
  void pasteAtCursorPosition();
  void pasteAtOriginalPosition();
  mdl::PasteType paste();
  bool canPaste() const;
  void duplicateSelection();
  bool canDuplicateSelection() const;
  void deleteSelection();
  bool canDeleteSelection() const;
  void selectAll();
  void selectSiblings();
  void selectTouching();
  void selectInside();
  void selectTall();
  void selectByLineNumber();
  void selectInverse();
  void selectNone();
  bool canSelect() const;
  bool canSelectSiblings() const;
  bool canSelectByBrush() const;
  bool canSelectTall() const;
  bool canDeselect() const;
  bool canChangeSelection() const;
  bool canSelectInverse() const;
  void groupSelectedObjects();
  bool canGroupSelectedObjects() const;
  void ungroupSelectedObjects();
  bool canUngroupSelectedObjects() const;
  void renameSelectedGroups();
  bool canRenameSelectedGroups() const;
  void replaceMaterial();
  void moveSelectedObjects();
  bool canMoveSelectedObjects() const;
  void togglePathTool();
  bool canTogglePathTool() const;
  bool pathToolActive() const;
  void performPathCreation();
  void removeLastPathPoint();
  void csgConvexMerge();
  bool canDoCsgConvexMerge() const;
  void csgSubtract();
  bool canDoCsgSubtract() const;
  void csgHollow();
  bool canDoCsgHollow() const;
  void csgIntersect();
  bool canDoCsgIntersect() const;
  void snapVerticesToInteger();
  void snapVerticesToGrid();
  bool canSnapVertices() const;
  void convertSelectionToPatches();
  bool canConvertSelectionToPatches() const;
  void toggleAlignmentLock();
  void toggleUvLock();
  void toggleShowGrid();
  void toggleSnapToGrid();
  void incGridSize();
  bool canIncGridSize() const;
  void decGridSize();
  bool canDecGridSize() const;
  void setGridSize(int size);
  void isolateSelection();
  bool canIsolateSelection() const;
  void hideSelection();
  bool canHideSelection() const;
  void showAll();
};
} // namespace tb::ui
