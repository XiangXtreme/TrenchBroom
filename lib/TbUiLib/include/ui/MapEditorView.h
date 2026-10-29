/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom.
*/

#pragma once

#include <QWidget>

#include "ui/InfoPanel.h"
#include "ui/Inspector.h"
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
  InfoPanel* m_infoPanel = nullptr;
  Inspector* m_inspector = nullptr;

public:
  MapEditorView(AppController& appController, MapDocument& document);
  ~MapEditorView() override;

  SwitchableMapViewContainer* mapView() const;
  MapViewBase* currentMapViewBase() const;

  InfoPanel* infoPanel() const;
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

  /** Returns true when the view consumed the drag, matching the window's Escape handling.
   */
  bool cancelDrag();

  /** Restores splitter and panel state. Call once the view is inside its host widget. */
  void restoreLayoutState();

  void saveWidgetState() const;
};

} // namespace tb::ui