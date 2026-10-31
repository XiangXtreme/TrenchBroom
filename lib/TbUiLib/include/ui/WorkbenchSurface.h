/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom.
*/

#pragma once

#include <QColor>
#include <QWidget>

class QString;

namespace tb::ui
{

/** Shared antialiased pane chrome. It leaves GL widgets and input ownership intact. */
class WorkbenchBorder : public QWidget
{
  Q_OBJECT
  Q_PROPERTY(qreal cornerRadius MEMBER m_radius)
  Q_PROPERTY(qreal borderWidth MEMBER m_borderWidth)
  Q_PROPERTY(QColor borderColor MEMBER m_borderColor)
  Q_PROPERTY(QColor activeBorderColor MEMBER m_activeBorderColor)
  Q_PROPERTY(QColor outsideColor MEMBER m_outsideColor)
private:
  qreal m_radius = 0;
  qreal m_borderWidth = 0;
  QColor m_borderColor;
  QColor m_activeBorderColor;
  QColor m_outsideColor;
  bool m_active = false;

public:
  explicit WorkbenchBorder(QWidget* host, bool trackFocus = false);

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void paintEvent(QPaintEvent* event) override;
};

/**
 * A rounded, styled frame around one workbench pane.
 *
 * The map editor is built from panes (map view, info panel, inspector) that each need the
 * same visual container and antialiased edge handling, so the frame belongs to the
 * workbench and not to the window that happens to host it.
 */
class WorkbenchSurface : public QWidget
{
public:
  enum class Kind
  {
    Panel,
    Workspace
  };
  WorkbenchSurface(const QString& objectName, QWidget* content, Kind kind = Kind::Panel);
};

} // namespace tb::ui