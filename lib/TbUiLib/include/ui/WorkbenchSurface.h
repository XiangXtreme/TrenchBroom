/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom.
*/

#pragma once

#include <QWidget>

class QString;

namespace tb::ui
{

/**
 * A rounded, styled frame around one workbench pane.
 *
 * The map editor is built from panes (map view, info panel, inspector) that each need the
 * same visual container and mask handling, so the frame belongs to the workbench and not
 * to the window that happens to host it.
 */
class WorkbenchSurface : public QWidget
{
public:
  WorkbenchSurface(const QString& objectName, QWidget* content);

protected:
  void resizeEvent(QResizeEvent* event) override;
};

} // namespace tb::ui