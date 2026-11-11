#include "ui/GeneralPreferencePane.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QFileDialog>
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
#include "ui/FormWithSectionsLayout.h"
#include "ui/PieMenuSettingsDialog.h"
#include "ui/PrefabAsset.h"
#include "ui/QPathUtils.h"
#include "ui/QStyleUtils.h"
#include "ui/ViewConstants.h"


namespace tb::ui
{
GeneralPreferencePane::GeneralPreferencePane(QWidget* parent)
  : PreferencePane{parent}
{
  createGui();
  updateControls();
}

void GeneralPreferencePane::createGui()
{
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

  auto* prefabDirectoryLayout = new QHBoxLayout{};
  prefabDirectoryLayout->setContentsMargins(QMargins{});
  prefabDirectoryLayout->setSpacing(LayoutConstants::MediumHMargin);
  prefabDirectoryLayout->addWidget(m_prefabDirectoryEdit, 1);
  prefabDirectoryLayout->addWidget(m_choosePrefabDirectoryButton);

  auto* layout = new FormWithSectionsLayout{};
  layout->setContentsMargins(16, 16, 16, 16);
  layout->addSection(
    tr("Language"), tr("Language changes take effect after restarting."));
  layout->addRow(tr("Display language"), languageChoicesLayout);
  layout->addSection(tr("Editing"));
  layout->addRow(editorLayout);
  layout->addSection(tr("Assets"));
  layout->addRow(tr("Prefab directory"), prefabDirectoryLayout);
  layout->addSection(tr("Menus"));
  layout->addRow(tr("Pie menu"), m_pieMenuSettingsButton);
  createScrollableContent(layout);
}

bool GeneralPreferencePane::canResetToDefaults()
{
  return true;
}

void GeneralPreferencePane::doResetToDefaults()
{
  auto& prefs = PreferenceManager::instance();
  prefs.resetToDefault(Preferences::Language);
  prefs.resetToDefault(Preferences::PrefixWorldspawnHeaderOnCopy);
  prefs.resetToDefault(Preferences::Enable2DBoxSelection);
  prefs.resetToDefault(Preferences::PrefabDirectory);
  updateControls();
}

void GeneralPreferencePane::updateControls()
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
}

bool GeneralPreferencePane::validate()
{
  return true;
}

void GeneralPreferencePane::showRestartRequiredMessage()
{
  QMessageBox::information(
    this,
    tr("Restart Required"),
    tr("Language settings will take effect after applying preferences and restarting the "
       "application"),
    QMessageBox::Ok);
}

} // namespace tb::ui
