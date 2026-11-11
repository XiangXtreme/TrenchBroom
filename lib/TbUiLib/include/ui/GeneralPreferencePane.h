#pragma once

#include "ui/PreferencePane.h"

class QButtonGroup;
class QCheckBox;
class QLineEdit;
class QPushButton;
class QRadioButton;

namespace tb::ui
{

class GeneralPreferencePane : public PreferencePane
{
  Q_OBJECT
private:
  QRadioButton* m_englishRadioButton = nullptr;
  QRadioButton* m_chineseRadioButton = nullptr;
  QButtonGroup* m_languageButtonGroup = nullptr;
  QCheckBox* m_prefixWorldspawnOnCopyCheckBox = nullptr;
  QCheckBox* m_enable2DBoxSelectionCheckBox = nullptr;
  QLineEdit* m_prefabDirectoryEdit = nullptr;
  QPushButton* m_choosePrefabDirectoryButton = nullptr;
  QPushButton* m_pieMenuSettingsButton = nullptr;

signals:
  void languageChanged();

public:
  explicit GeneralPreferencePane(QWidget* parent = nullptr);

private:
  void createGui();
  bool canResetToDefaults() override;
  void doResetToDefaults() override;
  void updateControls() override;
  bool validate() override;
  void showRestartRequiredMessage();
};

} // namespace tb::ui
