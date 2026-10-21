/*
 Copyright (C) 2010 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ui/ViewPreferencePane.h"

#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QFontDatabase>
#include <QLabel>
#include <QListView>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QtGlobal>

#include "base/PreferenceManager.h"
#include "gl/MiniGl.h"
#include "prefs/Preferences.h"
#include "ui/FormWithSectionsLayout.h"
#include "ui/MaterialBrowserIconSize.h"
#include "ui/QStyleUtils.h"
#include "ui/SliderWithLabel.h"
#include "ui/ThemeRegistry.h"
#include "ui/ViewConstants.h"

#include "kd/contracts.h"
#include "kd/range_utils.h"

#include "vm/scalar.h"

#include <array>
#include <string>

namespace tb::ui
{
namespace
{
constexpr auto PythonConsoleFontPopupRows = 10;
constexpr auto PythonConsoleFontPopupMaxHeight = 320;

struct FilterMode
{
  int minFilter;
  int magFilter;
  std::string name;
};

const auto FilterModes = std::array<FilterMode, 6>{
  FilterMode{GL_NEAREST, GL_NEAREST, "Nearest"},
  FilterMode{GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST, "Nearest (mipmapped)"},
  FilterMode{GL_NEAREST_MIPMAP_LINEAR, GL_NEAREST, "Nearest (mipmapped, interpolated)"},
  FilterMode{GL_LINEAR, GL_LINEAR, "Linear"},
  FilterMode{GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR, "Linear (mipmapped)"},
  FilterMode{GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, "Linear (mipmapped, interpolated)"},
};

std::optional<size_t> findFilterMode(const int minFilter, const int magFilter)
{
  return kdl::index_of(FilterModes, [&](const FilterMode& filterMode) {
    return filterMode.minFilter == minFilter && filterMode.magFilter == magFilter;
  });
}

constexpr int brightnessToUI(const float value)
{
  return int(vm::round(100.0f * (value - 1.0f)));
}

constexpr float brightnessFromUI(const int value)
{
  return (float(value) / 100.0f) + 1.0f;
}

static_assert(0 == brightnessToUI(brightnessFromUI(0)));

} // namespace

ViewPreferencePane::ViewPreferencePane(QWidget* parent)
  : PreferencePane{parent}
{
  createGui();
  bindEvents();
}

void ViewPreferencePane::createGui()
{
  auto* viewPreferences = createViewPreferences();

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(QMargins{});
  layout->setSpacing(0);
  layout->addSpacing(LayoutConstants::NarrowVMargin);
  layout->addWidget(viewPreferences, 1);
  layout->addSpacing(LayoutConstants::MediumVMargin);

  createScrollableContent(layout);
}

QWidget* ViewPreferencePane::createViewPreferences()
{
  auto* viewBox = new QWidget{this};

  auto* viewPrefsHeader = new QLabel{"Map Views"};
  setEmphasizedStyle(viewPrefsHeader);

  m_themeCombo = new QComboBox{};
  m_themeCombo->setObjectName(QStringLiteral("ViewPreference_ThemeCombo"));
  for (const auto& theme : ThemeRegistry::instance().themes())
  {
    m_themeCombo->addItem(theme.name, theme.id);
    const auto toolTip = theme.author.isEmpty()
                           ? tr("ID: %1").arg(theme.id)
                           : tr("ID: %1\nBy %2").arg(theme.id, theme.author);
    m_themeCombo->setItemData(m_themeCombo->count() - 1, toolTip, Qt::ToolTipRole);
  }
  auto* themeInfo = new QLabel{};
  themeInfo->setText(tr("Requires restart after changing"));
  setInfoStyle(themeInfo);
  auto* themeLayout = new QHBoxLayout{};
  themeLayout->addWidget(m_themeCombo);
  themeLayout->addSpacing(LayoutConstants::NarrowHMargin);
  themeLayout->addWidget(themeInfo);
  themeLayout->setContentsMargins(0, 0, 0, 0);

  m_layoutCombo = new QComboBox{};
  m_layoutCombo->setToolTip("Sets the layout of the editing views.");
  m_layoutCombo->addItem("One Pane");
  m_layoutCombo->addItem("Two Panes");
  m_layoutCombo->addItem("Three Panes");
  m_layoutCombo->addItem("Four Panes");

  m_default2DToolCombo = new QComboBox{};
  m_default2DToolCombo->setObjectName("ViewPreference_Default2DTool");
  m_default2DToolCombo->addItem(tr("Create Brush"), false);
  m_default2DToolCombo->addItem(tr("Select"), true);

  m_link2dCameras = new QCheckBox{"Sync 2D views"};
  m_link2dCameras->setToolTip("All 2D views pan and zoom together.");

  auto* viewLayoutLayout = new QHBoxLayout{};
  viewLayoutLayout->addWidget(m_layoutCombo);
  viewLayoutLayout->addSpacing(LayoutConstants::NarrowHMargin);
  viewLayoutLayout->addWidget(m_link2dCameras);
  viewLayoutLayout->setContentsMargins(0, 0, 0, 0);

  m_brightnessSlider = new SliderWithLabel{brightnessToUI(0.0f), brightnessToUI(2.0f)};
  m_brightnessSlider->setMaximumWidth(400);
  m_brightnessSlider->setToolTip(
    "Sets the brightness for materials and model skins in the 3D editing view.");
  m_gridAlphaSlider = new SliderWithLabel{0, 100};
  m_gridAlphaSlider->setMaximumWidth(400);
  m_gridAlphaSlider->setToolTip(
    "Sets the visibility of the grid lines in the 3D editing view.");
  m_fovSlider = new SliderWithLabel{50, 150};
  m_fovSlider->setMaximumWidth(400);
  m_fovSlider->setToolTip("Sets the field of vision in the 3D editing view.");

  m_showAxes = new QCheckBox{};
  m_showAxes->setToolTip(
    "Toggle showing the coordinate system axes in the 3D editing view.");

  m_filterModeCombo = new QComboBox{};
  m_filterModeCombo->setToolTip("Sets the texture filtering mode in the editing views.");
  for (const auto& filterMode : FilterModes)
  {
    m_filterModeCombo->addItem(QString::fromStdString(filterMode.name));
  }

  m_enableMsaa = new QCheckBox{};
  m_enableMsaa->setToolTip("Enable multisampling");

  m_fpsLimitSpin = new QSpinBox{};
  m_fpsLimitSpin->setObjectName(QStringLiteral("ViewPreference_FpsLimitSpin"));
  m_fpsLimitSpin->setRange(0, Preferences::MaxRenderFpsLimit);
  m_fpsLimitSpin->setSpecialValueText(tr("Unlimited"));
  m_fpsLimitSpin->setSuffix(QStringLiteral(" FPS"));
  m_fpsLimitSpin->setMaximumWidth(160);

  m_materialBrowserIconSizeCombo = new QComboBox{};
  m_materialBrowserIconSizeCombo->setObjectName(
    QStringLiteral("ViewPreference_MaterialBrowserIconSizeCombo"));
  for (const auto size : MaterialBrowserIconSizes)
  {
    m_materialBrowserIconSizeCombo->addItem(
      QStringLiteral("%1%").arg(qRound(size * 100.0f)), size);
  }
  m_materialBrowserIconSizeCombo->setToolTip(
    "Sets the icon size in the material browser.");

  m_rendererFontSizeCombo = new QComboBox{};
  m_rendererFontSizeCombo->setEditable(true);
  m_rendererFontSizeCombo->setToolTip(
    "Sets the font size for various labels in the editing views.");
  m_rendererFontSizeCombo->addItems({"8",  "9",  "10", "11", "12", "13", "14", "15",
                                     "16", "17", "18", "19", "20", "22", "24", "26",
                                     "28", "32", "36", "40", "48", "56", "64", "72"});
  m_rendererFontSizeCombo->setValidator(new QIntValidator{1, 96});

  m_pythonConsoleFontFamilyCombo = new QComboBox{};
  m_pythonConsoleFontFamilyCombo->setObjectName(
    QStringLiteral("ViewPreference_PythonConsoleFontFamilyCombo"));
  m_pythonConsoleFontFamilyCombo->setMinimumContentsLength(20);
  m_pythonConsoleFontFamilyCombo->setMaxVisibleItems(PythonConsoleFontPopupRows);
  m_pythonConsoleFontFamilyCombo->setSizeAdjustPolicy(
    QComboBox::AdjustToMinimumContentsLengthWithIcon);
  m_pythonConsoleFontFamilyCombo->setToolTip(
    tr("Sets the monospace font used by the Python console."));

  auto* pythonConsoleFontList = new QListView{m_pythonConsoleFontFamilyCombo};
  pythonConsoleFontList->setObjectName(
    QStringLiteral("ViewPreference_PythonConsoleFontList"));
  pythonConsoleFontList->setUniformItemSizes(true);
  pythonConsoleFontList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  pythonConsoleFontList->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  pythonConsoleFontList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  pythonConsoleFontList->setMaximumHeight(PythonConsoleFontPopupMaxHeight);
  m_pythonConsoleFontFamilyCombo->setView(pythonConsoleFontList);

  m_pythonConsoleFontFamilyCombo->addItem(tr("System Monospace"), QString{});
  for (const auto& family : QFontDatabase::families())
  {
    if (QFontDatabase::isFixedPitch(family))
    {
      m_pythonConsoleFontFamilyCombo->addItem(family, family);
      m_pythonConsoleFontFamilyCombo->setItemData(
        m_pythonConsoleFontFamilyCombo->count() - 1, QFont{family}, Qt::FontRole);
    }
  }

  m_pythonConsoleFontSizeSpin = new QSpinBox{};
  m_pythonConsoleFontSizeSpin->setObjectName(
    QStringLiteral("ViewPreference_PythonConsoleFontSizeSpin"));
  m_pythonConsoleFontSizeSpin->setRange(
    Preferences::MinPythonConsoleFontSize, Preferences::MaxPythonConsoleFontSize);
  m_pythonConsoleFontSizeSpin->setSuffix(QStringLiteral(" pt"));
  m_pythonConsoleFontSizeSpin->setMaximumWidth(120);
  m_pythonConsoleFontSizeSpin->setToolTip(
    tr("Sets the font size of Python console output and input."));

  auto* pythonConsoleFontLayout = new QHBoxLayout{};
  pythonConsoleFontLayout->setContentsMargins(0, 0, 0, 0);
  pythonConsoleFontLayout->setSpacing(LayoutConstants::NarrowHMargin);
  pythonConsoleFontLayout->addWidget(m_pythonConsoleFontFamilyCombo, 1);
  pythonConsoleFontLayout->addWidget(m_pythonConsoleFontSizeSpin);

  auto* layout = new FormWithSectionsLayout{};
  layout->setContentsMargins(
    LayoutConstants::DialogOuterMargin,
    LayoutConstants::DialogOuterMargin,
    LayoutConstants::DialogOuterMargin,
    LayoutConstants::DialogOuterMargin);
  layout->setVerticalSpacing(LayoutConstants::WideVMargin);
  // override the default to make the sliders take up maximum width
  layout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

  layout->addSection("User Interface");
  layout->addRow("Theme", themeLayout);

  layout->addSection("Map Views");
  layout->addRow("Layout", viewLayoutLayout);
  layout->addRow(tr("Default 2D tool"), m_default2DToolCombo);
  layout->addRow("Brightness", m_brightnessSlider);
  layout->addRow("Grid", m_gridAlphaSlider);
  layout->addRow("FOV", m_fovSlider);
  layout->addRow("Show axes", m_showAxes);
  layout->addRow("Filter mode", m_filterModeCombo);
  layout->addRow("Enable multisampling", m_enableMsaa);
  layout->addRow(tr("FPS limit"), m_fpsLimitSpin);

  layout->addSection("Material Browser");
  layout->addRow("Icon size", m_materialBrowserIconSizeCombo);

  layout->addSection("Fonts");
  layout->addRow("Renderer Font Size", m_rendererFontSizeCombo);
  layout->addRow(tr("Python Console"), pythonConsoleFontLayout);

  viewBox->setLayout(layout);

  return viewBox;
}

void ViewPreferencePane::bindEvents()
{
  connect(m_default2DToolCombo, &QComboBox::activated, this, [this](const int index) {
    PreferenceManager::instance().set(
      Preferences::Default2DSelectMode, m_default2DToolCombo->itemData(index).toBool());
  });
  connect(
    m_fpsLimitSpin, &QSpinBox::valueChanged, this, &ViewPreferencePane::fpsLimitChanged);
  connect(
    m_layoutCombo,
    QOverload<int>::of(&QComboBox::currentIndexChanged),
    this,
    &ViewPreferencePane::layoutChanged);
  connect(
    m_link2dCameras,
    &QCheckBox::checkStateChanged,
    this,
    &ViewPreferencePane::link2dCamerasChanged);
  connect(
    m_brightnessSlider,
    &SliderWithLabel::valueChanged,
    this,
    &ViewPreferencePane::brightnessChanged);
  connect(
    m_gridAlphaSlider,
    &SliderWithLabel::valueChanged,
    this,
    &ViewPreferencePane::gridAlphaChanged);
  connect(
    m_fovSlider, &SliderWithLabel::valueChanged, this, &ViewPreferencePane::fovChanged);
  connect(
    m_showAxes,
    &QCheckBox::checkStateChanged,
    this,
    &ViewPreferencePane::showAxesChanged);
  connect(
    m_enableMsaa,
    &QCheckBox::checkStateChanged,
    this,
    &ViewPreferencePane::enableMsaaChanged);
  connect(
    m_themeCombo,
    QOverload<int>::of(&QComboBox::activated),
    this,
    &ViewPreferencePane::themeChanged);
  connect(
    m_filterModeCombo,
    QOverload<int>::of(&QComboBox::currentIndexChanged),
    this,
    &ViewPreferencePane::filterModeChanged);
  connect(
    m_materialBrowserIconSizeCombo,
    QOverload<int>::of(&QComboBox::currentIndexChanged),
    this,
    &ViewPreferencePane::materialBrowserIconSizeChanged);
  connect(
    m_rendererFontSizeCombo,
    &QComboBox::currentTextChanged,
    this,
    &ViewPreferencePane::rendererFontSizeChanged);
  connect(
    m_pythonConsoleFontFamilyCombo,
    QOverload<int>::of(&QComboBox::currentIndexChanged),
    this,
    &ViewPreferencePane::pythonConsoleFontFamilyChanged);
  connect(
    m_pythonConsoleFontSizeSpin,
    &QSpinBox::valueChanged,
    this,
    &ViewPreferencePane::pythonConsoleFontSizeChanged);
}

bool ViewPreferencePane::canResetToDefaults()
{
  return true;
}

void ViewPreferencePane::doResetToDefaults()
{
  auto& prefs = PreferenceManager::instance();
  prefs.resetToDefault(Preferences::MapViewLayout);
  prefs.resetToDefault(Preferences::Default2DSelectMode);
  prefs.resetToDefault(Preferences::Link2DCameras);
  prefs.resetToDefault(Preferences::Brightness);
  prefs.resetToDefault(Preferences::GridAlpha);
  prefs.resetToDefault(Preferences::CameraFov);
  prefs.resetToDefault(Preferences::ShowAxes);
  prefs.resetToDefault(Preferences::EnableMSAA);
  prefs.resetToDefault(Preferences::RenderFpsLimit);
  prefs.resetToDefault(Preferences::TextureMinFilter);
  prefs.resetToDefault(Preferences::TextureMagFilter);
  prefs.resetToDefault(Preferences::Theme);
  prefs.resetToDefault(Preferences::MaterialBrowserIconSize);
  prefs.resetToDefault(Preferences::RendererFontSize);
  prefs.resetToDefault(Preferences::PythonConsoleFontFamily);
  prefs.resetToDefault(Preferences::PythonConsoleFontSize);
}

void ViewPreferencePane::updateControls()
{
  const auto layoutBlocker = QSignalBlocker{m_layoutCombo};
  const auto defaultToolBlocker = QSignalBlocker{m_default2DToolCombo};
  const auto link2DCamerasBlocker = QSignalBlocker{m_link2dCameras};
  const auto brightnessBlocker = QSignalBlocker{m_brightnessSlider};
  const auto gridAlphaBlocker = QSignalBlocker{m_gridAlphaSlider};
  const auto fovBlocker = QSignalBlocker{m_fovSlider};

  const auto filterModeBlocker = QSignalBlocker{m_filterModeCombo};

  const auto showAxesBlocker = QSignalBlocker{m_showAxes};
  const auto enableMsaaBlocker = QSignalBlocker{m_enableMsaa};
  const auto fpsLimitBlocker = QSignalBlocker{m_fpsLimitSpin};
  const auto themeBlocker = QSignalBlocker{m_themeCombo};

  const auto materialBrowserIconSizeBlocker =
    QSignalBlocker{m_materialBrowserIconSizeCombo};

  const auto rendererFontSizeBlocker = QSignalBlocker{m_rendererFontSizeCombo};
  const auto pythonConsoleFontFamilyBlocker =
    QSignalBlocker{m_pythonConsoleFontFamilyCombo};
  const auto pythonConsoleFontSizeBlocker = QSignalBlocker{m_pythonConsoleFontSizeSpin};

  auto& prefs = PreferenceManager::instance();

  m_layoutCombo->setCurrentIndex(prefs.getPendingValue(Preferences::MapViewLayout));
  m_default2DToolCombo->setCurrentIndex(
    prefs.getPendingValue(Preferences::Default2DSelectMode) ? 1 : 0);
  m_link2dCameras->setChecked(prefs.getPendingValue(Preferences::Link2DCameras));
  m_brightnessSlider->setValue(
    brightnessToUI(prefs.getPendingValue(Preferences::Brightness)));
  m_gridAlphaSlider->setRatio(prefs.getPendingValue(Preferences::GridAlpha));
  m_fovSlider->setValue(int(prefs.getPendingValue(Preferences::CameraFov)));

  if (
    const auto filterModeIndex = findFilterMode(
      prefs.getPendingValue(Preferences::TextureMinFilter),
      prefs.getPendingValue(Preferences::TextureMagFilter)))
  {
    m_filterModeCombo->setCurrentIndex(int(*filterModeIndex));
  }

  m_showAxes->setChecked(prefs.getPendingValue(Preferences::ShowAxes));
  m_enableMsaa->setChecked(prefs.getPendingValue(Preferences::EnableMSAA));
  m_fpsLimitSpin->setValue(prefs.getPendingValue(Preferences::RenderFpsLimit));
  m_themeCombo->setCurrentIndex(
    findThemeIndex(QString::fromStdString(prefs.getPendingValue(Preferences::Theme))));

  const auto materialBrowserIconSize =
    prefs.getPendingValue(Preferences::MaterialBrowserIconSize);
  const auto materialBrowserIconSizeIndex =
    m_materialBrowserIconSizeCombo->findData(materialBrowserIconSize);
  m_materialBrowserIconSizeCombo->setCurrentIndex(
    materialBrowserIconSizeIndex >= 0 ? materialBrowserIconSizeIndex : 0);

  m_rendererFontSizeCombo->setCurrentText(
    QString::asprintf("%i", prefs.getPendingValue(Preferences::RendererFontSize)));
  const auto pythonConsoleFontFamily =
    QString::fromStdString(prefs.getPendingValue(Preferences::PythonConsoleFontFamily));
  const auto pythonConsoleFontFamilyIndex =
    m_pythonConsoleFontFamilyCombo->findData(pythonConsoleFontFamily);
  m_pythonConsoleFontFamilyCombo->setCurrentIndex(
    pythonConsoleFontFamilyIndex >= 0 ? pythonConsoleFontFamilyIndex : 0);
  m_pythonConsoleFontSizeSpin->setValue(
    prefs.getPendingValue(Preferences::PythonConsoleFontSize));
}

bool ViewPreferencePane::validate()
{
  return true;
}

int ViewPreferencePane::findThemeIndex(const QString& theme) const
{
  const auto themeId = ThemeRegistry::instance().canonicalThemeId(theme);
  const auto index = m_themeCombo->findData(themeId);
  return index >= 0
           ? index
           : m_themeCombo->findData(QString::fromStdString(Preferences::SystemTheme));
}

void ViewPreferencePane::layoutChanged(const int index)
{
  contract_pre(index >= 0 && index < 4);

  auto& prefs = PreferenceManager::instance();
  prefs.set(Preferences::MapViewLayout, index);
}

void ViewPreferencePane::link2dCamerasChanged(const int state)
{
  const auto value = state == Qt::Checked;
  auto& prefs = PreferenceManager::instance();
  prefs.set(Preferences::Link2DCameras, value);
}

void ViewPreferencePane::brightnessChanged(const int value)
{
  auto& prefs = PreferenceManager::instance();
  prefs.set(Preferences::Brightness, brightnessFromUI(value));
}

void ViewPreferencePane::gridAlphaChanged(const int /* value */)
{
  const auto ratio = m_gridAlphaSlider->ratio();
  auto& prefs = PreferenceManager::instance();
  prefs.set(Preferences::GridAlpha, ratio);
}

void ViewPreferencePane::fovChanged(const int value)
{
  auto& prefs = PreferenceManager::instance();
  prefs.set(Preferences::CameraFov, float(value));
}

void ViewPreferencePane::showAxesChanged(const int state)
{
  const auto value = state == Qt::Checked;
  auto& prefs = PreferenceManager::instance();
  prefs.set(Preferences::ShowAxes, value);
}

void ViewPreferencePane::enableMsaaChanged(const int state)
{
  const auto value = state == Qt::Checked;
  auto& prefs = PreferenceManager::instance();
  prefs.set(Preferences::EnableMSAA, value);
}

void ViewPreferencePane::fpsLimitChanged(const int value)
{
  PreferenceManager::instance().set(Preferences::RenderFpsLimit, value);
}

void ViewPreferencePane::filterModeChanged(const int value)
{
  contract_assert(value < static_cast<int>(FilterModes.size()));

  const auto index = static_cast<size_t>(value);

  const auto minFilter = FilterModes[index].minFilter;
  const auto magFilter = FilterModes[index].magFilter;

  auto& prefs = PreferenceManager::instance();
  prefs.set(Preferences::TextureMinFilter, minFilter);
  prefs.set(Preferences::TextureMagFilter, magFilter);
}

void ViewPreferencePane::themeChanged(const int index)
{
  if (index < 0 || index >= m_themeCombo->count())
  {
    return;
  }

  auto& prefs = PreferenceManager::instance();
  prefs.set(Preferences::Theme, m_themeCombo->itemData(index).toString().toStdString());
}

void ViewPreferencePane::materialBrowserIconSizeChanged(const int index)
{
  if (index < 0 || index >= m_materialBrowserIconSizeCombo->count())
  {
    return;
  }

  auto ok = false;
  const auto iconSize = m_materialBrowserIconSizeCombo->itemData(index).toFloat(&ok);
  if (ok)
  {
    PreferenceManager::instance().set(Preferences::MaterialBrowserIconSize, iconSize);
  }
}

void ViewPreferencePane::rendererFontSizeChanged(const QString& str)
{
  bool ok;
  const auto value = str.toInt(&ok);
  if (ok)
  {
    auto& prefs = PreferenceManager::instance();
    prefs.set(Preferences::RendererFontSize, value);
  }
}

void ViewPreferencePane::pythonConsoleFontFamilyChanged(const int index)
{
  if (index >= 0 && index < m_pythonConsoleFontFamilyCombo->count())
  {
    PreferenceManager::instance().set(
      Preferences::PythonConsoleFontFamily,
      m_pythonConsoleFontFamilyCombo->itemData(index).toString().toStdString());
  }
}

void ViewPreferencePane::pythonConsoleFontSizeChanged(const int value)
{
  PreferenceManager::instance().set(Preferences::PythonConsoleFontSize, value);
}

} // namespace tb::ui
