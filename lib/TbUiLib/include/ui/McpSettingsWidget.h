#pragma once

#include <QGroupBox>

#include "mcp/McpBridgeConfig.h"

class QComboBox;
class QLabel;
class QLineEdit;

namespace tb::ui
{

class AppController;

class McpSettingsWidget : public QGroupBox
{
  Q_OBJECT
private:
  AppController& m_appController;
  mcp::McpBridgeConfig m_config;
  QString m_configPath;
  QString m_error;
  QComboBox* m_modeCombo = nullptr;
  QLineEdit* m_httpUrlEdit = nullptr;
  QLabel* m_statusLabel = nullptr;
  QLabel* m_errorLabel = nullptr;

public:
  explicit McpSettingsWidget(AppController& appController, QWidget* parent = nullptr);
  McpSettingsWidget(
    AppController& appController, QString configPath, QWidget* parent = nullptr);

  void resetToDefaults();
  void updateControls();

private:
  void createGui();
  void loadConfig();
  bool saveConfig();
  void applyConfigChange();
  void modeChanged(int index);
  void copyHttpUrl();
  void copyClaudeCommand();
};

} // namespace tb::ui
