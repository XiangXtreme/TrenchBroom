/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom.
*/

#pragma once

#include <QPointer>
#include <QWidget>

#include "ui/InfoPanel.h"
#include "ui/Inspector.h"
#include "ui/WorkspaceLayout.h"
#include "ui/python/PythonCompletionEngine.h"

#include <functional>
#include <string>

namespace tb::ui
{

class AppController;
class Console;
class InfoPanel;
class MapDocument;
class MapViewBase;
class PythonConsole;
class Splitter;
class SwitchableMapViewContainer;
class WorkbenchSurface;

/**
 * The map editing workbench: the map views, the info panel and the inspector, arranged in
 * the splitter layout the editor uses.
 *
 * It is a plain widget on purpose. A map document can be edited by whichever host owns
 * the view — a standalone map window, or a tab inside a project window — and closing a
 * view destroys the view, not the document.
 */
class MapEditorView : public QWidget
{
  Q_OBJECT
private:
  AppController& m_appController;
  MapDocument& m_document;

  Splitter* m_hSplitter = nullptr;
  Splitter* m_vSplitter = nullptr;

  WorkbenchSurface* m_mapViewSurface = nullptr;
  WorkbenchSurface* m_infoPanelSurface = nullptr;
  WorkbenchSurface* m_inspectorSurface = nullptr;

  SwitchableMapViewContainer* m_mapView = nullptr;
  QPointer<InfoPanel> m_infoPanel;
  Inspector* m_inspector = nullptr;
  EditorLayout m_layout;
  bool m_layoutPending = true;
  bool m_externalInfoPanel = false;
  bool m_externalInfoPanelExpanded = false;

public:
  MapEditorView(AppController& appController, MapDocument& document);
  ~MapEditorView() override;

  SwitchableMapViewContainer* mapView() const;
  MapViewBase* currentMapViewBase() const;

  InfoPanel* infoPanel() const;
  /** The host takes QWidget ownership and must destroy the panel before the document. */
  void useExternalInfoPanel();
  void setExternalInfoPanelExpanded(bool expanded);
  Inspector* inspector() const;
  Console& console() const;
  PythonConsole* pythonConsole() const;

  Splitter* hSplitter() const;
  Splitter* vSplitter() const;

  void setPythonConsoleCommandExecutor(std::function<void(const std::string&)> executor);
  void setPythonConsoleCompletionRootProvider(PythonCompletionRootProvider provider);

  /** Panel visibility belongs to the view so a host does not have to reimplement it. */
  void showInfoPanelPage(InfoPanelPage page);
  void showInspectorPage(InspectorPage page);
  bool toggleInspector();
  bool isInspectorVisible() const;
  bool toggleInfoPanel();
  bool isInfoPanelVisible() const;

  void undo();
  void redo();
  bool canUndo() const;
  bool canRedo() const;
  void repeatLastCommands();
  void clearRepeatableCommands();
  bool hasRepeatableCommands() const;

  void moveCameraToNextTracePoint();
  void moveCameraToPreviousTracePoint();
  bool canMoveCameraToNextTracePoint() const;
  bool canMoveCameraToPreviousTracePoint() const;
  void reset2dCameras(MapViewBase& sourceView);
  void focusCameraOnSelection(bool animate = true);
  bool canFocusCamera() const;
  void toggleMaximizeCurrentView();
  bool currentViewMaximized() const;

  /** Returns true when the view consumed the drag, matching the window's Escape handling.
   */
  bool cancelDrag();

  /** Restores panel state and applies proportions when the host has laid out the view. */
  void restoreLayoutState();
  void resetLayout();

  void saveWidgetState() const;

private:
  void applyLayoutState();

protected:
  void showEvent(QShowEvent* event) override;

signals:
  void infoPanelPageRequested(InfoPanelPage page);
  void infoPanelToggleRequested();
  void infoPanelVisibilityChanged();
};

} // namespace tb::ui
