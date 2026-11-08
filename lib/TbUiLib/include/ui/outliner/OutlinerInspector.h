#pragma once

#include "ui/TabBook.h"

namespace tb::ui
{
class OutlinerTreeWidget;
class MapDocument;
class SelectionPropertyEditor;
} // namespace tb::ui

class QLineEdit;
class QComboBox;
class QSplitter;
class QToolButton;

namespace tb::ui
{
class OutlinerInspector : public TabBookPage
{
  Q_OBJECT
private:
  MapDocument& m_document;

  QSplitter* m_splitter = nullptr;
  OutlinerTreeWidget* m_treeWidget = nullptr;
  SelectionPropertyEditor* m_propertyEditor = nullptr;
  QLineEdit* m_searchField = nullptr;
  QComboBox* m_sortBox = nullptr;
  QToolButton* m_propertiesToggle = nullptr;

public:
  explicit OutlinerInspector(MapDocument& document, QWidget* parent = nullptr);
  ~OutlinerInspector() override;
  void showProperties();

signals:
  void mapSettingsRequested();
};

} // namespace tb::ui
