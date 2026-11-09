/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <QWidget>

#include "base/NotifierConnection.h"

class QComboBox;
class QVBoxLayout;

namespace tb::ui
{
class EntityPropertyEditor;
class MapDocument;
class OutlinerEntityPropertyEditor;

/** Shared property surface used by the scene and entity inspectors.
 * Form and table are complementary presentations of the same native map properties.
 */
class SelectionPropertyEditor : public QWidget
{
  Q_OBJECT
private:
  MapDocument& m_document;
  QComboBox* m_presentation = nullptr;
  QVBoxLayout* m_layout = nullptr;
  OutlinerEntityPropertyEditor* m_form = nullptr;
  EntityPropertyEditor* m_table = nullptr;
  NotifierConnection m_notifierConnection;

public:
  explicit SelectionPropertyEditor(MapDocument& document, QWidget* parent = nullptr);
  void focusProperty(const QString& key);

signals:
  void focusMapViewRequested();

private:
  void updatePresentation();
};
} // namespace tb::ui
