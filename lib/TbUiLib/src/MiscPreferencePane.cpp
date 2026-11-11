#include "ui/MiscPreferencePane.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "base/PreferenceManager.h"
#include "prefs/Preferences.h"
#include "ui/AppController.h"
#include "ui/McpSettingsWidget.h"
#include "ui/PieMenuSettingsDialog.h"
#include "ui/PrefabAsset.h"
#include "ui/QPathUtils.h"
#include "ui/QStyleUtils.h"
#include "ui/ViewConstants.h"

#include <utility>

namespace tb::ui
{
namespace
{
QGroupBox* createGroupBox(const QString& title, QLayout* contentLayout)
{
  auto* groupBox = new QGroupBox{title};
  contentLayout->setContentsMargins(
    LayoutConstants::WideHMargin,
    LayoutConstants::WideVMargin,
    LayoutConstants::WideHMargin,
    LayoutConstants::WideVMargin);
  contentLayout->setSpacing(LayoutConstants::WideVMargin);
  groupBox->setLayout(contentLayout);
  return groupBox;
}
} // namespace

MiscPreferencePane::MiscPreferencePane(AppController& appController, QWidget* parent)
  : MiscPreferencePane{appController, mcp::defaultConfigPath(), parent}
{
}

MiscPreferencePane::MiscPreferencePane(
  AppController& appController, QString mcpConfigPath, QWidget* parent)
  : PreferencePane{parent}
  , m_appController{appController}
  , m_mcpConfigPath{std::move(mcpConfigPath)}
{
  createGui();
  updateControls();
}

void MiscPreferencePane::createGui()
{
  auto* languageLabel = new QLabel{tr("Select the display language.")};
  setInfoStyle(languageLabel);

  m_englishRadioButton =
    new QRadioButton{QString::fromStdString(Preferences::languageEnglish())};
  m_chineseRadioButton =
    new QRadioButton{QString::fromStdString(Preferences::languageChinese())};

  m_languageButtonGroup = new QButtonGroup{this};
  m_languageButtonGroup->addButton(m_englishRadioButton, 0);
  m_languageButtonGroup->addButton(m_chineseRadioButton, 1);

  connect(m_englishRadioButton, &QRadioButton::clicked, this, [this]() {
    auto& prefs = PreferenceManager::instance();
    prefs.set(Preferences::Language, Preferences::languageEnglish());
    emit languageChanged();
    showRestartRequiredMessage();
  });

  connect(m_chineseRadioButton, &QRadioButton::clicked, this, [this]() {
    auto& prefs = PreferenceManager::instance();
    prefs.set(Preferences::Language, Preferences::languageChinese());
    emit languageChanged();
    showRestartRequiredMessage();
  });

  auto* languageChoicesLayout = new QHBoxLayout{};
  languageChoicesLayout->setContentsMargins(0, 0, 0, 0);
  languageChoicesLayout->setSpacing(LayoutConstants::WideHMargin);
  languageChoicesLayout->addWidget(m_englishRadioButton);
  languageChoicesLayout->addWidget(m_chineseRadioButton);
  languageChoicesLayout->addStretch(1);

  auto* languageLayout = new QVBoxLayout{};
  languageLayout->addWidget(languageLabel);
  languageLayout->addLayout(languageChoicesLayout);

  m_prefixWorldspawnOnCopyCheckBox =
    new QCheckBox{tr("Prefix worldspawn header on copy")};
  connect(
    m_prefixWorldspawnOnCopyCheckBox, &QCheckBox::toggled, this, [](const bool checked) {
      auto& prefs = PreferenceManager::instance();
      prefs.set(Preferences::PrefixWorldspawnHeaderOnCopy, checked);
    });

  m_enable2DBoxSelectionCheckBox =
    new QCheckBox{tr("Enable 2D box selection with Ctrl+drag")};
  connect(
    m_enable2DBoxSelectionCheckBox, &QCheckBox::toggled, this, [](const bool checked) {
      auto& prefs = PreferenceManager::instance();
      prefs.set(Preferences::Enable2DBoxSelection, checked);
    });

  auto* editorLayout = new QVBoxLayout{};
  editorLayout->addWidget(m_prefixWorldspawnOnCopyCheckBox);
  editorLayout->addWidget(m_enable2DBoxSelectionCheckBox);

  m_pieMenuSettingsButton = new QPushButton{tr("Pie Menu Settings...")};
  m_pythonPluginManagerButton = new QPushButton{tr("Python Plugin Manager...")};

  m_prefabDirectoryEdit = new QLineEdit{};
  m_prefabDirectoryEdit->setObjectName("prefabDirectoryEdit");
  m_prefabDirectoryEdit->setPlaceholderText(pathAsQString(defaultPrefabDirectory()));

  m_choosePrefabDirectoryButton = new QPushButton{tr("...")};

  connect(m_prefabDirectoryEdit, &QLineEdit::editingFinished, this, [this]() {
    auto& prefs = PreferenceManager::instance();
    prefs.set(
      Preferences::PrefabDirectory, pathFromQString(m_prefabDirectoryEdit->text()));
  });

  connect(m_choosePrefabDirectoryButton, &QPushButton::clicked, this, [this]() {
    auto& prefs = PreferenceManager::instance();
    auto directory = prefs.getPendingValue(Preferences::PrefabDirectory);
    if (directory.empty())
    {
      directory = defaultPrefabDirectory();
    }

    const auto pathStr = QFileDialog::getExistingDirectory(
      this, tr("Prefab Directory"), pathAsQString(directory));
    if (!pathStr.isEmpty())
    {
      m_prefabDirectoryEdit->setText(pathStr);
      prefs.set(Preferences::PrefabDirectory, pathFromQString(pathStr));
    }
  });

  connect(m_pieMenuSettingsButton, &QPushButton::clicked, this, [this]() {
    auto dialog = PieMenuSettingsDialog{this};
    dialog.exec();
  });

  connect(m_pythonPluginManagerButton, &QPushButton::clicked, this, [this]() {
    m_appController.showPythonPluginManager();
  });

  auto* prefabDirectoryLayout = new QHBoxLayout{};
  prefabDirectoryLayout->setContentsMargins(QMargins{});
  prefabDirectoryLayout->setSpacing(LayoutConstants::MediumHMargin);
  prefabDirectoryLayout->addWidget(m_prefabDirectoryEdit, 1);
  prefabDirectoryLayout->addWidget(m_choosePrefabDirectoryButton);

  auto* toolLayout = new QVBoxLayout{};
  toolLayout->addWidget(new QLabel{tr("Prefab directory")});
  toolLayout->addLayout(prefabDirectoryLayout);
  toolLayout->addWidget(m_pieMenuSettingsButton);
  toolLayout->addWidget(m_pythonPluginManagerButton);

  m_mcpSettingsWidget = new McpSettingsWidget{m_appController, m_mcpConfigPath};

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(
    LayoutConstants::DialogOuterMargin,
    LayoutConstants::DialogOuterMargin,
    LayoutConstants::DialogOuterMargin,
    LayoutConstants::DialogOuterMargin);
  layout->setSpacing(LayoutConstants::WideVMargin);
  layout->addWidget(createGroupBox(tr("Language"), languageLayout));
  layout->addWidget(createGroupBox(tr("Editor"), editorLayout));
  layout->addWidget(createGroupBox(tr("Tools"), toolLayout));
  layout->addWidget(m_mcpSettingsWidget);
  layout->addStretch(1);
  createScrollableContent(layout);
}

bool MiscPreferencePane::canResetToDefaults()
{
  return true;
}

void MiscPreferencePane::doResetToDefaults()
{
  auto& prefs = PreferenceManager::instance();
  prefs.resetToDefault(Preferences::Language);
  prefs.resetToDefault(Preferences::PrefixWorldspawnHeaderOnCopy);
  prefs.resetToDefault(Preferences::Enable2DBoxSelection);
  prefs.resetToDefault(Preferences::PrefabDirectory);
  m_mcpSettingsWidget->resetToDefaults();
  updateControls();
}

void MiscPreferencePane::updateControls()
{
  const auto englishBlocker = QSignalBlocker{m_englishRadioButton};
  const auto chineseBlocker = QSignalBlocker{m_chineseRadioButton};
  const auto prefixBlocker = QSignalBlocker{m_prefixWorldspawnOnCopyCheckBox};
  const auto boxSelectionBlocker = QSignalBlocker{m_enable2DBoxSelectionCheckBox};
  const auto prefabDirectoryBlocker = QSignalBlocker{m_prefabDirectoryEdit};

  const auto& language = pref(Preferences::Language);
  m_englishRadioButton->setChecked(language == Preferences::languageEnglish());
  m_chineseRadioButton->setChecked(language == Preferences::languageChinese());
  m_prefixWorldspawnOnCopyCheckBox->setChecked(
    pref(Preferences::PrefixWorldspawnHeaderOnCopy));
  m_enable2DBoxSelectionCheckBox->setChecked(pref(Preferences::Enable2DBoxSelection));
  m_prefabDirectoryEdit->setText(pathAsQString(
    PreferenceManager::instance().getPendingValue(Preferences::PrefabDirectory)));
  m_mcpSettingsWidget->updateControls();
}

bool MiscPreferencePane::validate()
{
  return true;
}

void MiscPreferencePane::showRestartRequiredMessage()
{
  QMessageBox::information(
    this,
    tr("Restart Required"),
    tr("Language settings will take effect after applying preferences and restarting the "
       "application"),
    QMessageBox::Ok);
}

} // namespace tb::ui
