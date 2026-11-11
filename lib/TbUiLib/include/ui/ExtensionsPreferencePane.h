/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include "ui/PreferencePane.h"

namespace tb::ui
{
class AppController;
class McpSettingsWidget;

class ExtensionsPreferencePane : public PreferencePane
{
  Q_OBJECT
private:
  McpSettingsWidget* m_mcpSettings;

public:
  explicit ExtensionsPreferencePane(
    AppController& appController, QWidget* parent = nullptr);
  ExtensionsPreferencePane(
    AppController& appController,
    const QString& mcpConfigPath,
    QWidget* parent = nullptr);

  bool canResetToDefaults() override;
  void updateControls() override;
  bool validate() override;

private:
  void doResetToDefaults() override;
};
} // namespace tb::ui
