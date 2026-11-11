/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/ExtensionsPreferencePane.h"

#include <QPushButton>
#include <QVBoxLayout>

#include "ui/AppController.h"
#include "ui/FormWithSectionsLayout.h"
#include "ui/McpSettingsWidget.h"

namespace tb::ui
{
ExtensionsPreferencePane::ExtensionsPreferencePane(
  AppController& appController, QWidget* parent)
  : ExtensionsPreferencePane{appController, mcp::defaultConfigPath(), parent}
{
}

ExtensionsPreferencePane::ExtensionsPreferencePane(
  AppController& appController, const QString& mcpConfigPath, QWidget* parent)
  : PreferencePane{parent}
{
  auto* pluginManager = new QPushButton{tr("Python Plugin Manager...")};
  connect(pluginManager, &QPushButton::clicked, this, [&appController]() {
    appController.showPythonPluginManager();
  });

  auto* plugins = new FormWithSectionsLayout{};
  plugins->addSection(tr("Python plugins"));
  plugins->addRow(tr("Installed plugins"), pluginManager);

  m_mcpSettings = new McpSettingsWidget{appController, mcpConfigPath};
  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(16, 16, 16, 16);
  layout->setSpacing(24);
  layout->addLayout(plugins);
  layout->addWidget(m_mcpSettings);
  layout->addStretch();
  createScrollableContent(layout);
}

bool ExtensionsPreferencePane::canResetToDefaults()
{
  return true;
}

void ExtensionsPreferencePane::doResetToDefaults()
{
  m_mcpSettings->resetToDefaults();
}

void ExtensionsPreferencePane::updateControls()
{
  m_mcpSettings->updateControls();
}

bool ExtensionsPreferencePane::validate()
{
  return true;
}
} // namespace tb::ui
