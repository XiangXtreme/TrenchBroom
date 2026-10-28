#pragma once

#include <QWidget>

#include "base/NotifierConnection.h"

#include <vector>

class QLabel;
class QLineEdit;
class QScrollArea;
class QVBoxLayout;
class QToolButton;
class QCompleter;
class QStringListModel;

namespace tb::mdl
{
class EntityNodeBase;
} // namespace tb::mdl

namespace tb::ui
{
class TitledPanel;
class FlagsEditor;
class MapDocument;
class SmartSkyboxEditor;
class SmartWadEditor;
class TransformCard;

class OutlinerEntityPropertyEditor : public QWidget
{
  Q_OBJECT
private:
  MapDocument& m_document;
  NotifierConnection m_notifierConnection;

  TitledPanel* m_propertiesPanel = nullptr;
  QLabel* m_selectionSummary = nullptr;
  QScrollArea* m_scrollArea = nullptr;
  QWidget* m_scrollContents = nullptr;
  QVBoxLayout* m_scrollLayout = nullptr;

  QToolButton* m_smartEditButton = nullptr;
  QCompleter* m_addKeyCompleter = nullptr;
  QStringListModel* m_completerModel = nullptr;

  QWidget* m_addPropertyBar = nullptr;
  QLineEdit* m_addKey = nullptr;
  QLineEdit* m_addValue = nullptr;

  QWidget* m_embeddedWadEditorContainer = nullptr;
  SmartWadEditor* m_embeddedWadEditor = nullptr;
  bool m_wadEditorExpanded = false;

  QWidget* m_embeddedSkyboxEditorContainer = nullptr;
  SmartSkyboxEditor* m_embeddedSkyboxEditor = nullptr;
  bool m_skyboxEditorExpanded = false;

  QWidget* m_embeddedSpawnflagsEditorContainer = nullptr;
  FlagsEditor* m_embeddedSpawnflagsEditor = nullptr;
  bool m_spawnflagsEditorExpanded = false;
  TransformCard* m_transformCard = nullptr;
  bool m_transformCardExpanded = true;

  bool m_updateQueued = false;
  bool m_forceUpdate = false;
  bool m_comboPopupVisible = false;
  bool m_updateDeferred = false;

public:
  explicit OutlinerEntityPropertyEditor(MapDocument& document, QWidget* parent = nullptr);
  ~OutlinerEntityPropertyEditor() override;
  void onChoiceComboPopupHidden();

private:
  void connectObservers();
  void scheduleUpdate(bool force = false);
  void updateFromSelection();
  void refreshVisiblePropertyValues();
  void refreshEmbeddedEditors();

  void rebuildPropertyRows(const std::vector<mdl::EntityNodeBase*>& entityNodes);
};

} // namespace tb::ui
