#include "ui/McpSettingsWidget.h"

#include <QClipboard>
#include <QComboBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>

#include "mcp/McpMode.h"
#include "ui/AppController.h"
#include "ui/QStyleUtils.h"
#include "ui/ViewConstants.h"

#include <utility>

namespace tb::ui
{
namespace
{

constexpr int ModeRole = Qt::UserRole;

void addMode(QComboBox& combo, const QString& label, const mcp::McpMode mode)
{
  combo.addItem(label, mcp::modeName(mode));
}

mcp::McpMode modeFromCombo(const QComboBox& combo)
{
  const auto value = combo.currentData(ModeRole).toString();
  return mcp::parseMode(value).value_or(mcp::McpMode::Off);
}

int findModeIndex(const QComboBox& combo, const mcp::McpMode mode)
{
  return combo.findData(mcp::modeName(mode), ModeRole);
}

QString httpUrl(const mcp::McpBridgeConfig& config)
{
  return QString{"http://%1:%2/mcp"}.arg(config.httpHost).arg(config.httpPort);
}

QString claudeCommand(const QString& url)
{
  return QString{"claude mcp add --scope user --transport http trenchbroom %1"}.arg(url);
}

} // namespace

McpSettingsWidget::McpSettingsWidget(AppController& appController, QWidget* parent)
  : McpSettingsWidget{appController, mcp::defaultConfigPath(), parent}
{
}

McpSettingsWidget::McpSettingsWidget(
  AppController& appController, QString configPath, QWidget* parent)
  : QGroupBox{tr("MCP"), parent}
  , m_appController{appController}
  , m_config{mcp::defaultBridgeConfig()}
  , m_configPath{std::move(configPath)}
{
  setObjectName("McpSettings_Group");
  loadConfig();
  createGui();
  updateControls();
}

void McpSettingsWidget::createGui()
{
  auto* infoLabel = new QLabel{
    tr("Local MCP clients can connect through this computer's HTTP endpoint. Leave "
       "access off when it is not in use.")};
  infoLabel->setWordWrap(true);
  setInfoStyle(infoLabel);

  m_modeCombo = new QComboBox{};
  m_modeCombo->setObjectName("McpSettings_Mode");
  addMode(*m_modeCombo, tr("Off"), mcp::McpMode::Off);
  addMode(*m_modeCombo, tr("Read-only"), mcp::McpMode::ReadOnly);
  addMode(*m_modeCombo, tr("Edit"), mcp::McpMode::Edit);
  m_modeCombo->setToolTip(
    tr("Read-only permits inspection and capture. Edit also permits map changes."));
  connect(
    m_modeCombo,
    QOverload<int>::of(&QComboBox::currentIndexChanged),
    this,
    &McpSettingsWidget::modeChanged);

  m_statusLabel = new QLabel{};
  m_statusLabel->setObjectName("McpSettings_Status");
  setInfoStyle(m_statusLabel);

  m_httpUrlEdit = new QLineEdit{};
  m_httpUrlEdit->setObjectName("McpSettings_HttpUrl");
  m_httpUrlEdit->setReadOnly(true);

  auto* copyUrlButton = new QPushButton{tr("Copy URL")};
  copyUrlButton->setObjectName("McpSettings_CopyUrl");
  connect(copyUrlButton, &QPushButton::clicked, this, &McpSettingsWidget::copyHttpUrl);

  auto* endpointLayout = new QHBoxLayout{};
  endpointLayout->setContentsMargins(QMargins{});
  endpointLayout->setSpacing(LayoutConstants::MediumHMargin);
  endpointLayout->addWidget(m_httpUrlEdit, 1);
  endpointLayout->addWidget(copyUrlButton);

  auto* copyClaudeCommandButton = new QPushButton{tr("Copy Setup Command")};
  copyClaudeCommandButton->setObjectName("McpSettings_CopyClaudeCommand");
  copyClaudeCommandButton->setToolTip(
    tr("Copy the Claude Code command that registers this endpoint."));
  connect(
    copyClaudeCommandButton,
    &QPushButton::clicked,
    this,
    &McpSettingsWidget::copyClaudeCommand);

  m_errorLabel = new QLabel{};
  m_errorLabel->setObjectName("McpSettings_Error");
  m_errorLabel->setWordWrap(true);

  auto* layout = new QFormLayout{};
  layout->setContentsMargins(
    LayoutConstants::WideHMargin,
    LayoutConstants::WideVMargin,
    LayoutConstants::WideHMargin,
    LayoutConstants::WideVMargin);
  layout->setHorizontalSpacing(LayoutConstants::WideHMargin);
  layout->setVerticalSpacing(LayoutConstants::WideVMargin);
  layout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
  layout->addRow(infoLabel);
  layout->addRow(tr("Access"), m_modeCombo);
  layout->addRow(tr("Status"), m_statusLabel);
  layout->addRow(tr("Endpoint"), endpointLayout);
  layout->addRow(tr("Claude Code"), copyClaudeCommandButton);
  layout->addRow(m_errorLabel);
  setLayout(layout);
}

void McpSettingsWidget::resetToDefaults()
{
  m_config = mcp::defaultBridgeConfig();
  applyConfigChange();
}

void McpSettingsWidget::updateControls()
{
  const auto modeBlocker = QSignalBlocker{m_modeCombo};

  const auto modeIndex = findModeIndex(*m_modeCombo, m_config.mode);
  m_modeCombo->setCurrentIndex(modeIndex >= 0 ? modeIndex : 0);
  m_httpUrlEdit->setText(httpUrl(m_config));
  m_httpUrlEdit->setCursorPosition(0);

  const auto modeText = mcp::modeName(m_config.mode);
  const auto statusText =
    !m_appController.mcpStartupError().isEmpty()
      ? tr("Startup failed: %1").arg(m_appController.mcpStartupError())
    : m_appController.mcpHttpServerIsListening()
      ? tr("Listening in %1 mode").arg(modeText)
    : m_config.mode == mcp::McpMode::Off
      ? tr("Disabled")
      : tr("Configured as %1, but not listening").arg(modeText);
  m_statusLabel->setText(statusText);

  m_errorLabel->setVisible(!m_error.isEmpty());
  m_errorLabel->setText(m_error);
}

void McpSettingsWidget::loadConfig()
{
  auto error = QString{};
  const auto config = mcp::readOrCreateBridgeConfig(m_configPath, &error);
  if (config)
  {
    m_config = *config;
    m_error.clear();
  }
  else
  {
    m_error = error;
  }
}

bool McpSettingsWidget::saveConfig()
{
  auto error = QString{};
  if (mcp::writeBridgeConfig(m_config, m_configPath, &error))
  {
    m_error.clear();
    return true;
  }

  m_error = error;
  updateControls();
  return false;
}

void McpSettingsWidget::applyConfigChange()
{
  if (saveConfig())
  {
    m_appController.restartMcpBridge();
  }
  updateControls();
}

void McpSettingsWidget::modeChanged(const int /* index */)
{
  m_config.mode = modeFromCombo(*m_modeCombo);
  applyConfigChange();
}

void McpSettingsWidget::copyHttpUrl()
{
  QGuiApplication::clipboard()->setText(httpUrl(m_config));
}

void McpSettingsWidget::copyClaudeCommand()
{
  QGuiApplication::clipboard()->setText(claudeCommand(httpUrl(m_config)));
}

} // namespace tb::ui
