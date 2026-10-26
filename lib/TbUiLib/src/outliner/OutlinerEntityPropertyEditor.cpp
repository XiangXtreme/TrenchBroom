#include "ui/outliner/OutlinerEntityPropertyEditor.h"

#include <QApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QCompleter>
#include <QEvent>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStringList>
#include <QStringListModel>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "base/Color.h"
#include "base/PreferenceManager.h"
#include "prefs/Preferences.h"
#include "mdl/EntityColorPropertyValue.h"
#include "mdl/EntityDefinition.h"
#include "mdl/EntityNodeBase.h"
#include "mdl/EntityProperties.h"
#include "mdl/GameInfo.h"
#include "mdl/Map.h"
#include "mdl/Map_Entities.h"
#include "mdl/PropertyDefinition.h"
#include "ui/BitmapButton.h"
#include "ui/ElidedLabel.h"
#include "ui/FileDialogDefaultDir.h"
#include "ui/FlagsEditor.h"
#include "ui/MapDocument.h"
#include "ui/QColorUtils.h"
#include "ui/QPathUtils.h"
#include "ui/QStringUtils.h"
#include "ui/QWidgetUtils.h"
#include "ui/SmartSkyboxEditor.h"
#include "ui/SmartWadEditor.h"
#include "ui/TitleBar.h"
#include "ui/TitledPanel.h"
#include "ui/ViewUtils.h"

#include "kd/string_compare.h"
#include "kd/string_utils.h"

#include <filesystem>
#include <unordered_set>

namespace tb::ui
{
namespace
{
class ResponsivePropertyRow : public QWidget
{
private:
  static constexpr int CompactWidth = 420;

  QGridLayout* m_layout = nullptr;
  QWidget* m_keyWidget = nullptr;
  QWidget* m_valueWidget = nullptr;
  QWidget* m_actionsWidget = nullptr;
  bool m_compact = false;

public:
  explicit ResponsivePropertyRow(QWidget* parent = nullptr)
    : QWidget{parent}
    , m_layout{new QGridLayout{this}}
  {
    m_layout->setContentsMargins(6, 4, 4, 4);
    m_layout->setHorizontalSpacing(6);
    m_layout->setVerticalSpacing(4);
  }

  void setWidgets(QWidget* keyWidget, QWidget* valueWidget, QWidget* actionsWidget)
  {
    m_keyWidget = keyWidget;
    m_valueWidget = valueWidget;
    m_actionsWidget = actionsWidget;
    updateLayout(width());
  }

protected:
  void resizeEvent(QResizeEvent* event) override
  {
    QWidget::resizeEvent(event);
    updateLayout(event->size().width());
  }

private:
  void updateLayout(const int width)
  {
    if (!m_keyWidget || !m_valueWidget || !m_actionsWidget)
    {
      return;
    }

    const auto compact = width < CompactWidth;
    if (m_layout->indexOf(m_keyWidget) >= 0 && compact == m_compact)
    {
      return;
    }

    m_layout->removeWidget(m_keyWidget);
    m_layout->removeWidget(m_valueWidget);
    m_layout->removeWidget(m_actionsWidget);
    for (auto i = 0; i < 3; ++i)
    {
      m_layout->setColumnStretch(i, 0);
    }

    m_compact = compact;
    setProperty("compact", compact);

    if (compact)
    {
      m_layout->addWidget(m_keyWidget, 0, 0);
      m_layout->addWidget(m_actionsWidget, 0, 1, Qt::AlignRight);
      m_layout->addWidget(m_valueWidget, 1, 0, 1, 2);
      m_layout->setColumnStretch(0, 1);
    }
    else
    {
      m_layout->addWidget(m_keyWidget, 0, 0);
      m_layout->addWidget(m_valueWidget, 0, 1);
      m_layout->addWidget(m_actionsWidget, 0, 2, Qt::AlignRight);
      m_layout->setColumnStretch(0, 1);
      m_layout->setColumnStretch(1, 2);
    }
  }
};

void setInactiveEditorPalette(QWidget* editor)
{
  auto palette = editor->palette();
  palette.setColor(
    QPalette::Disabled,
    QPalette::PlaceholderText,
    palette.color(QPalette::Disabled, QPalette::Text));
  editor->setPalette(palette);
}

class OutlinerChoiceComboBox : public QComboBox
{
private:
  bool& m_popupVisible;
  OutlinerEntityPropertyEditor* m_editor;

public:
  OutlinerChoiceComboBox(
    bool& popupVisible, OutlinerEntityPropertyEditor* editor, QWidget* parent)
    : QComboBox{parent}
    , m_popupVisible{popupVisible}
    , m_editor{editor}
  {
  }

protected:
  void showPopup() override
  {
    m_popupVisible = true;
    QComboBox::showPopup();
  }

  void hidePopup() override
  {
    QComboBox::hidePopup();
    m_popupVisible = false;
    m_editor->onChoiceComboPopupHidden();
  }

  void wheelEvent(QWheelEvent* event) override { event->ignore(); }
};

struct ValueConsensus
{
  bool mixed = false;
  bool anyPresent = false;
  std::string value;
};

ValueConsensus consensusValue(
  const std::string& key, const std::vector<mdl::EntityNodeBase*>& nodes)
{
  auto result = ValueConsensus{};
  bool hasFirst = false;

  for (const auto* node : nodes)
  {
    const auto* v = node->entity().property(key);
    if (v != nullptr)
    {
      result.anyPresent = true;
    }

    if (!hasFirst)
    {
      if (v != nullptr)
      {
        result.value = *v;
      }
      hasFirst = true;
    }
    else
    {
      const auto current = v != nullptr ? *v : std::string{};
      if (current != result.value)
      {
        result.mixed = true;
      }
    }
  }

  if (!result.anyPresent)
  {
    result.mixed = false;
    result.value.clear();
  }

  return result;
}

std::vector<std::string> buildKeyOrder(const std::vector<mdl::EntityNodeBase*>& nodes)
{
  auto result = std::vector<std::string>{};
  auto seen = std::unordered_set<std::string>{};

  if (!nodes.empty())
  {
    for (const auto& prop : nodes.front()->entity().properties())
    {
      seen.insert(prop.key());
      result.push_back(prop.key());
    }
  }

  for (const auto* node : nodes)
  {
    for (const auto& prop : node->entity().properties())
    {
      if (seen.insert(prop.key()).second)
      {
        result.push_back(prop.key());
      }
    }
  }

  return result;
}

std::vector<std::string> buildKeyOrderWithInactiveDefinitions(
  const std::vector<mdl::EntityNodeBase*>& nodes, const mdl::EntityDefinition* definition)
{
  auto result = buildKeyOrder(nodes);

  if (!definition)
  {
    return result;
  }

  auto seen = std::unordered_set<std::string>{};
  for (const auto& key : result)
  {
    seen.insert(key);
  }

  for (const auto& propDef : definition->propertyDefinitions)
  {
    if (seen.insert(propDef.key).second)
    {
      result.push_back(propDef.key);
    }
  }

  return result;
}

bool matchesSmartColorKeyPattern(const std::string& propertyKey)
{
  static const auto patterns = std::vector<std::string>{
    "color",
    "*_color",
    "*_color2",
    "*_colour",
  };
  return std::ranges::any_of(patterns, [&](const auto& pattern) {
    return kdl::cs::str_matches_glob(propertyKey, pattern);
  });
}

bool isColorPropertyDefinition(const mdl::PropertyDefinition& propertyDefinition)
{
  return std::holds_alternative<mdl::PropertyValueTypes::Color<RgbF>>(
           propertyDefinition.valueType)
         || std::holds_alternative<mdl::PropertyValueTypes::Color<RgbB>>(
           propertyDefinition.valueType)
         || std::holds_alternative<mdl::PropertyValueTypes::Color<Rgb>>(
           propertyDefinition.valueType);
}

bool isPropertyReadOnly(const mdl::Entity& entity, const std::string& key)
{
  if (const auto* entityDefinition = entity.definition())
  {
    if (
      const auto* propertyDefinition = mdl::getPropertyDefinition(entityDefinition, key))
    {
      return propertyDefinition->readOnly;
    }
  }
  return false;
}

bool isPropertyKeyMutable(const mdl::Entity& entity, const std::string& key)
{
  if (isPropertyReadOnly(entity, key))
  {
    return false;
  }

  if (mdl::isWorldspawn(entity.classname()))
  {
    return !(
      key == mdl::EntityPropertyKeys::Classname || key == mdl::EntityPropertyKeys::TbMods
      || key == mdl::EntityPropertyKeys::TbEntityDefinitions
      || key == mdl::EntityPropertyKeys::Wad
      || key == mdl::EntityPropertyKeys::TbEnabledMaterialCollections
      || key == mdl::EntityPropertyKeys::TbSoftMapBounds
      || key == mdl::EntityPropertyKeys::TbLayerColor
      || key == mdl::EntityPropertyKeys::TbLayerLocked
      || key == mdl::EntityPropertyKeys::TbLayerHidden
      || key == mdl::EntityPropertyKeys::TbLayerOmitFromExport);
  }

  return true;
}

std::optional<QColor> parseEntityColorToQColor(
  const mdl::EntityDefinition* entityDefinition,
  const std::string& propertyKey,
  const std::string& propertyValue)
{
  const auto parsed =
    mdl::parseEntityColorPropertyValue(entityDefinition, propertyKey, propertyValue);
  if (parsed.is_success())
  {
    return toQColor(parsed.value().color);
  }
  return std::nullopt;
}

constexpr size_t SpawnflagsNumFlags = 24;
constexpr size_t SpawnflagsNumCols = 3;

void getSpawnflagsLabelsAndTooltips(
  const std::vector<mdl::EntityNodeBase*>& nodes,
  const std::string& propertyKey,
  QStringList& labels,
  QStringList& tooltips)
{
  auto defaultLabels = QStringList{};

  for (size_t i = 0; i < SpawnflagsNumFlags; ++i)
  {
    auto defaultLabel = QString::number(1 << i);
    defaultLabels.push_back(defaultLabel);
    labels.push_back(defaultLabel);
    tooltips.push_back("");
  }

  for (size_t i = 0; i < SpawnflagsNumFlags; ++i)
  {
    auto firstPass = true;
    for (const auto* node : nodes)
    {
      const auto indexI = int(i);
      auto label = defaultLabels[indexI];
      auto tooltip = QString{""};

      if (
        const auto* propDef =
          mdl::getPropertyDefinition(node->entity().definition(), propertyKey))
      {
        if (
          const auto* flagType =
            std::get_if<mdl::PropertyValueTypes::Flags>(&propDef->valueType))
        {
          const auto flagValue = int(1 << i);
          if (const auto* flag = flagType->flag(flagValue))
          {
            label = QString::fromStdString(flag->shortDescription);
            tooltip = QString::fromStdString(flag->longDescription);
          }
        }
      }

      if (firstPass)
      {
        labels[indexI] = label;
        tooltips[indexI] = tooltip;
        firstPass = false;
      }
      else if (labels[indexI] != label)
      {
        labels[indexI] = defaultLabels[indexI];
        tooltips[indexI].clear();
      }
    }
  }
}

int getSpawnflagsValue(const mdl::EntityNodeBase* node, const std::string& propertyKey)
{
  if (const auto* value = node->entity().property(propertyKey))
  {
    return kdl::str_to_int(*value).value_or(0);
  }
  return 0;
}

void getSpawnflagsSetAndMixedValues(
  const std::vector<mdl::EntityNodeBase*>& nodes,
  const std::string& propertyKey,
  int& setFlags,
  int& mixedFlags)
{
  if (nodes.empty())
  {
    setFlags = 0;
    mixedFlags = 0;
    return;
  }

  auto it = std::begin(nodes);
  auto end = std::end(nodes);
  setFlags = getSpawnflagsValue(*it, propertyKey);
  mixedFlags = 0;

  while (++it != end)
  {
    combineFlags(
      SpawnflagsNumFlags, getSpawnflagsValue(*it, propertyKey), setFlags, mixedFlags);
  }
}

QIcon colorSwatchIcon(const QColor& color, const QPalette& palette)
{
  auto pixmap = QPixmap{14, 14};
  pixmap.fill(Qt::transparent);

  auto painter = QPainter{&pixmap};
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(palette.color(QPalette::Mid));
  painter.setBrush(color);
  painter.drawRoundedRect(pixmap.rect().adjusted(0, 0, -1, -1), 2, 2);
  return QIcon{pixmap};
}

QString propertyValueTypeName(const mdl::PropertyValueType& type)
{
  return std::visit(
    [](const auto& valueType) -> QString {
      using T = std::decay_t<decltype(valueType)>;
      if constexpr (std::is_same_v<T, mdl::PropertyValueTypes::LinkTarget>)
      {
        return "target";
      }
      else if constexpr (std::is_same_v<T, mdl::PropertyValueTypes::LinkSource>)
      {
        return "target_source";
      }
      else if constexpr (std::is_same_v<T, mdl::PropertyValueTypes::String>)
      {
        return "string";
      }
      else if constexpr (std::is_same_v<T, mdl::PropertyValueTypes::Boolean>)
      {
        return "boolean";
      }
      else if constexpr (std::is_same_v<T, mdl::PropertyValueTypes::Integer>)
      {
        return "integer";
      }
      else if constexpr (std::is_same_v<T, mdl::PropertyValueTypes::Float>)
      {
        return "float";
      }
      else if constexpr (std::is_same_v<T, mdl::PropertyValueTypes::Choice>)
      {
        return "choice";
      }
      else if constexpr (std::is_same_v<T, mdl::PropertyValueTypes::Flags>)
      {
        return "flags";
      }
      else if constexpr (std::is_same_v<T, mdl::PropertyValueTypes::Origin>)
      {
        return "origin";
      }
      else if constexpr (std::is_same_v<T, mdl::PropertyValueTypes::Input>)
      {
        return "input";
      }
      else if constexpr (std::is_same_v<T, mdl::PropertyValueTypes::Output>)
      {
        return "output";
      }
      else if constexpr (
        std::is_same_v<T, mdl::PropertyValueTypes::Color<RgbF>>
        || std::is_same_v<T, mdl::PropertyValueTypes::Color<RgbB>>
        || std::is_same_v<T, mdl::PropertyValueTypes::Color<Rgb>>)
      {
        return "color";
      }
      else
      {
        return "unknown";
      }
    },
    type);
}

QString toUnicodeFromFgd(const std::string& string)
{
  return QString::fromUtf8(string.data(), static_cast<qsizetype>(string.size()));
}

QString makePropertyTooltip(
  const std::string& key,
  const mdl::PropertyDefinition* propertyDef)
{
  auto lines = QStringList{};
  if (
    propertyDef != nullptr && !propertyDef->shortDescription.empty()
    && propertyDef->shortDescription != key)
  {
    lines.push_back(
      QString{"<b>%1</b> (%2)"}
        .arg(toUnicodeFromFgd(propertyDef->shortDescription).toHtmlEscaped())
        .arg(QString::fromStdString(key).toHtmlEscaped()));
  }
  else
  {
    lines.push_back(
      QString{"<b>%1</b>"}.arg(QString::fromStdString(key).toHtmlEscaped()));
  }

  if (propertyDef != nullptr)
  {
    auto meta = QStringList{};
    meta.push_back(
      QString{"%1: %2"}
        .arg(QCoreApplication::translate("OutlinerEntityPropertyEditor", "Type"))
        .arg(propertyValueTypeName(propertyDef->valueType)));
    if (const auto defVal = mdl::PropertyDefinition::defaultValue(*propertyDef))
    {
      meta.push_back(
        QString{"%1: %2"}
          .arg(QCoreApplication::translate("OutlinerEntityPropertyEditor", "Default"))
          .arg(toUnicodeFromFgd(*defVal).toHtmlEscaped()));
    }
    if (propertyDef->readOnly)
    {
      meta.push_back(
        QCoreApplication::translate("OutlinerEntityPropertyEditor", "Read Only"));
    }
    lines.push_back(QString{"<small>%1</small>"}.arg(meta.join(" &nbsp;|&nbsp; ")));

    if (!propertyDef->longDescription.empty())
    {
      lines.push_back(
        toUnicodeFromFgd(propertyDef->longDescription)
          .toHtmlEscaped()
          .replace("\n", "<br>"));
    }
  }

  return lines.join("<br>");
}
} // namespace

OutlinerEntityPropertyEditor::OutlinerEntityPropertyEditor(
  MapDocument& document, QWidget* parent)
  : QWidget{parent}
  , m_document{document}
{
  m_propertiesPanel = new TitledPanel{tr("Entity Properties").toUpper(), true, true};
  m_propertiesPanel->setObjectName("OutlinerPropertyPanel");
  m_propertiesPanel->getTitleBar()->setObjectName("OutlinerPropertySectionHeader");
  m_propertiesPanel->getPanel()->setObjectName("OutlinerPropertyPanelBody");

  auto* titleBarLayout =
    qobject_cast<QHBoxLayout*>(m_propertiesPanel->getTitleBar()->layout());
  m_smartEditButton = createBitmapToggleButton(
    "InspectorEntity.svg",
    tr("Toggle SmartEdit (Show friendly names / Raw keys)"),
    m_propertiesPanel->getTitleBar());
  m_smartEditButton->setObjectName("outlinerPropertySmartEditToggle");
  m_smartEditButton->setProperty("tbControlRole", "compact");
  m_smartEditButton->setIconSize(QSize{14, 14});
  m_smartEditButton->setFixedSize(QSize{22, 22});
  m_smartEditButton->setChecked(pref(Preferences::EntityPropertiesSmartEdit));
  if (titleBarLayout != nullptr)
  {
    titleBarLayout->addWidget(m_smartEditButton, 0, Qt::AlignRight);
  }

  connect(m_smartEditButton, &QAbstractButton::toggled, this, [this](const bool checked) {
    setPref(Preferences::EntityPropertiesSmartEdit, checked);
    scheduleUpdate(true);
  });

  m_selectionSummary = new QLabel{m_propertiesPanel->getPanel()};
  m_selectionSummary->setObjectName("outlinerPropertySelectionSummary");
  m_selectionSummary->setAttribute(Qt::WA_StyledBackground, true);
  m_selectionSummary->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_selectionSummary->hide();

  m_scrollArea = new QScrollArea{m_propertiesPanel->getPanel()};
  m_scrollArea->setObjectName("outlinerPropertyScrollArea");
  m_scrollArea->setFrameShape(QFrame::NoFrame);
  m_scrollArea->setWidgetResizable(true);

  m_scrollContents = new QWidget{m_scrollArea};
  m_scrollContents->setObjectName("outlinerPropertyList");
  m_scrollContents->setAttribute(Qt::WA_StyledBackground, true);
  m_scrollLayout = new QVBoxLayout{m_scrollContents};
  m_scrollLayout->setContentsMargins(6, 4, 6, 4);
  m_scrollLayout->setSpacing(2);
  m_scrollLayout->addStretch(1);
  m_scrollArea->setWidget(m_scrollContents);

  auto* addPropertyBar = new ResponsivePropertyRow{m_propertiesPanel->getPanel()};
  m_addPropertyBar = addPropertyBar;
  addPropertyBar->setObjectName("outlinerPropertyAddBar");
  addPropertyBar->setAttribute(Qt::WA_StyledBackground, true);

  m_addKey = new QLineEdit{m_addPropertyBar};
  m_addKey->setObjectName("outlinerPropertyAddKey");
  m_addKey->setPlaceholderText(tr("Key"));
  m_addKey->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

  m_addKeyCompleter = new QCompleter{this};
  m_addKeyCompleter->setCaseSensitivity(Qt::CaseInsensitive);
  m_addKeyCompleter->setFilterMode(Qt::MatchContains);
  m_completerModel = new QStringListModel{m_addKeyCompleter};
  m_addKeyCompleter->setModel(m_completerModel);
  m_addKey->setCompleter(m_addKeyCompleter);

  m_addValue = new QLineEdit{m_addPropertyBar};
  m_addValue->setObjectName("outlinerPropertyAddValue");
  m_addValue->setPlaceholderText(tr("Value"));
  m_addValue->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

  auto* addActions = new QWidget{m_addPropertyBar};
  auto* addActionsLayout = new QHBoxLayout{addActions};
  addActionsLayout->setContentsMargins(0, 0, 0, 0);
  addActionsLayout->setSpacing(2);

  auto* addButton = createBitmapButton("Add.svg", tr("Add property"), addActions);
  addButton->setProperty("tbControlRole", "compact");
  addButton->setIconSize(QSize{16, 16});
  addButton->setFixedSize(QSize{24, 24});
  addActionsLayout->addWidget(addButton);

  addPropertyBar->setWidgets(m_addKey, m_addValue, addActions);

  connect(addButton, &QAbstractButton::clicked, this, [this]() {
    const auto key = m_addKey->text().trimmed();
    if (key.isEmpty())
    {
      return;
    }

    mdl::setEntityProperty(
      m_document.map(), key.toStdString(), m_addValue->text().toStdString(), false);
    m_addKey->clear();
    m_addValue->clear();
    scheduleUpdate(true);
  });

  auto* propertiesLayout = new QVBoxLayout{m_propertiesPanel->getPanel()};
  propertiesLayout->setContentsMargins(0, 0, 0, 0);
  propertiesLayout->setSpacing(0);
  propertiesLayout->addWidget(m_selectionSummary, 0);
  propertiesLayout->addWidget(m_scrollArea, 1);
  propertiesLayout->addWidget(m_addPropertyBar, 0);

  auto* layout = new QVBoxLayout{this};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(m_propertiesPanel, 1);

  connectObservers();
  scheduleUpdate();
}

OutlinerEntityPropertyEditor::~OutlinerEntityPropertyEditor() = default;

void OutlinerEntityPropertyEditor::connectObservers()
{
  m_notifierConnection += m_document.selectionDidChangeNotifier.connect(
    [this](const mdl::SelectionChange&) { scheduleUpdate(); });
  m_notifierConnection +=
    m_document.entityDefinitionsDidChangeNotifier.connect([this]() { scheduleUpdate(); });
  m_notifierConnection +=
    m_document.documentWasLoadedNotifier.connect([this]() { scheduleUpdate(true); });
  m_notifierConnection += m_document.nodesDidChangeNotifier.connect([this](const auto&) {
    refreshVisiblePropertyValues();
    refreshEmbeddedEditors();
  });
  m_notifierConnection +=
    PreferenceManager::instance().preferenceDidChangeNotifier.connect(
      [this](const auto& path) {
        if (path == Preferences::EntityPropertiesSmartEdit.path)
        {
          const auto smartEdit = pref(Preferences::EntityPropertiesSmartEdit);
          if (m_smartEditButton->isChecked() != smartEdit)
          {
            const QSignalBlocker blocker{m_smartEditButton};
            m_smartEditButton->setChecked(smartEdit);
          }
          scheduleUpdate(true);
        }
      });
}

void OutlinerEntityPropertyEditor::scheduleUpdate(const bool force)
{
  if (force)
  {
    m_forceUpdate = true;
  }

  if (m_updateQueued)
  {
    return;
  }

  m_updateQueued = true;
  QTimer::singleShot(0, this, [this]() {
    m_updateQueued = false;
    updateFromSelection();
  });
}

void OutlinerEntityPropertyEditor::onChoiceComboPopupHidden()
{
  if (!m_updateDeferred)
  {
    return;
  }

  const auto force = m_forceUpdate;
  m_updateDeferred = false;
  scheduleUpdate(force);
}

void OutlinerEntityPropertyEditor::updateFromSelection()
{
  if (m_comboPopupVisible)
  {
    m_updateDeferred = true;
    return;
  }

  if (!m_forceUpdate)
  {
    if (auto* focusWidget = QApplication::focusWidget();
        focusWidget != nullptr && (focusWidget == this || isAncestorOf(focusWidget)))
    {
      return;
    }
  }
  m_forceUpdate = false;
  m_updateDeferred = false;

  const auto entities = m_document.map().selection().allEntities();
  rebuildPropertyRows(entities);
}

void OutlinerEntityPropertyEditor::refreshVisiblePropertyValues()
{
  const auto entityNodes = m_document.map().selection().allEntities();
  if (entityNodes.empty())
  {
    return;
  }

  for (auto* valueEdit : findChildren<QLineEdit*>("outlinerPropertyValue"))
  {
    const auto keyVariant = valueEdit->property("propertyKey");
    if (!keyVariant.isValid())
    {
      continue;
    }

    const auto key = keyVariant.toString().toStdString();
    const auto consensus = consensusValue(key, entityNodes);
    const auto inactive =
      valueEdit->property("inactive").toBool() && !consensus.anyPresent;
    const QSignalBlocker blocker{valueEdit};
    if (inactive)
    {
      valueEdit->clear();
      valueEdit->setPlaceholderText(tr("<inactive>"));
    }
    else if (consensus.mixed)
    {
      valueEdit->clear();
      valueEdit->setPlaceholderText(tr("<multiple>"));
    }
    else
    {
      valueEdit->setText(
        mapStringToUnicode(m_document.map().encoding(), consensus.value));
      valueEdit->setPlaceholderText(QString{});
    }
  }

  for (auto* valueCombo : findChildren<QComboBox*>("outlinerPropertyValue"))
  {
    const auto keyVariant = valueCombo->property("propertyKey");
    if (!keyVariant.isValid())
    {
      continue;
    }

    const auto key = keyVariant.toString().toStdString();
    const auto consensus = consensusValue(key, entityNodes);
    const auto inactive =
      valueCombo->property("inactive").toBool() && !consensus.anyPresent;
    const QSignalBlocker blocker{valueCombo};
    if (auto* comboLineEdit = valueCombo->lineEdit())
    {
      if (inactive)
      {
        valueCombo->setCurrentIndex(-1);
        valueCombo->setEditText(QString{});
        comboLineEdit->setPlaceholderText(tr("<inactive>"));
      }
      else if (consensus.mixed)
      {
        valueCombo->setCurrentIndex(-1);
        valueCombo->setEditText(QString{});
        comboLineEdit->setPlaceholderText(tr("<multiple>"));
      }
      else
      {
        valueCombo->setEditText(
          mapStringToUnicode(m_document.map().encoding(), consensus.value));
        comboLineEdit->setPlaceholderText(QString{});
      }
    }
  }
}

void OutlinerEntityPropertyEditor::refreshEmbeddedEditors()
{
  const auto entityNodes = m_document.map().selection().allEntities();
  if (m_embeddedWadEditor)
  {
    m_embeddedWadEditor->update(entityNodes);
  }
  if (m_embeddedSkyboxEditor)
  {
    m_embeddedSkyboxEditor->update(entityNodes);
  }
}

void OutlinerEntityPropertyEditor::rebuildPropertyRows(
  const std::vector<mdl::EntityNodeBase*>& entityNodes)
{
  m_embeddedWadEditorContainer = nullptr;
  m_embeddedWadEditor = nullptr;
  m_embeddedSkyboxEditorContainer = nullptr;
  m_embeddedSkyboxEditor = nullptr;
  m_embeddedSpawnflagsEditorContainer = nullptr;
  m_embeddedSpawnflagsEditor = nullptr;

  const auto clearLayout = [&](auto&& self, QLayout* l) -> void {
    while (auto* item = l->takeAt(0))
    {
      if (auto* w = item->widget())
      {
        w->setParent(nullptr);
        delete w;
      }
      if (auto* childLayout = item->layout())
      {
        self(self, childLayout);
        delete childLayout;
      }
      delete item;
    }
  };

  clearLayout(clearLayout, m_scrollLayout);

  if (entityNodes.empty())
  {
    m_selectionSummary->setText(tr("No entity selected"));
    m_selectionSummary->hide();
    auto* emptyLabel = new QLabel{tr("No entity selected"), m_scrollContents};
    emptyLabel->setObjectName("infoLabel");
    emptyLabel->setContentsMargins(4, 4, 4, 4);
    m_scrollLayout->addWidget(emptyLabel, 0);
    m_scrollLayout->addStretch(1);
    return;
  }

  const auto& firstClass = entityNodes.front()->entity().classname();
  const auto sameClass = std::ranges::all_of(entityNodes, [&](const auto* entityNode) {
    return entityNode->entity().classname() == firstClass;
  });

  if (!sameClass)
  {
    m_selectionSummary->setText(
      tr("Mixed selection (%1 entities)").arg(entityNodes.size()));
    m_selectionSummary->show();
    auto* mixedLabel =
      new QLabel{tr("Different entity types selected"), m_scrollContents};
    mixedLabel->setObjectName("infoLabel");
    mixedLabel->setContentsMargins(8, 8, 8, 8);
    m_scrollLayout->addWidget(mixedLabel, 0);
    m_scrollLayout->addStretch(1);
    return;
  }

  const auto className = mapStringToUnicode(m_document.map().encoding(), firstClass);
  m_selectionSummary->setText(
    entityNodes.size() == 1
      ? className
      : tr("%1 (%2 entities)").arg(className).arg(entityNodes.size()));
  m_selectionSummary->setVisible(entityNodes.size() > 1);

  const auto* entityDefinition = mdl::selectEntityDefinition(entityNodes);
  if (entityDefinition != nullptr && !entityDefinition->description.empty())
  {
    const auto classDesc = toUnicodeFromFgd(entityDefinition->description);
    m_propertiesPanel->getTitleBar()->setToolTip(
      QString{"<b>%1</b>: %2"}.arg(className.toHtmlEscaped(), classDesc.toHtmlEscaped()));
    m_selectionSummary->setToolTip(
      QString{"<b>%1</b>: %2"}.arg(className.toHtmlEscaped(), classDesc.toHtmlEscaped()));
  }
  else
  {
    m_propertiesPanel->getTitleBar()->setToolTip(QString{});
    m_selectionSummary->setToolTip(QString{});
  }

  auto completions = QStringList{};
  if (entityDefinition != nullptr)
  {
    for (const auto& propDef : entityDefinition->propertyDefinitions)
    {
      completions.push_back(QString::fromStdString(propDef.key));
    }
  }
  completions.sort(Qt::CaseInsensitive);
  completions.removeDuplicates();
  m_completerModel->setStringList(completions);

  const auto keys = buildKeyOrderWithInactiveDefinitions(entityNodes, entityDefinition);
  const auto& wadKey = m_document.map().gameInfo().gameConfig.materialConfig.property;
  const auto canShowWadEditor = wadKey.has_value() && entityNodes.size() == 1
                                && entityNodes.front()->entity().classname()
                                     == mdl::EntityPropertyValues::WorldspawnClassname;
  const auto canShowSkyboxEditor = entityNodes.size() == 1
                                   && entityNodes.front()->entity().classname()
                                        == mdl::EntityPropertyValues::WorldspawnClassname;

  auto actionWidgets = std::vector<QWidget*>{};
  for (const auto& key : keys)
  {
    auto* row = new ResponsivePropertyRow{m_scrollContents};
    row->setObjectName("outlinerPropertyRow");
    row->setProperty("propertyKey", QString::fromStdString(key));
    row->setAttribute(Qt::WA_StyledBackground, true);

    const auto isSmartEdit = pref(Preferences::EntityPropertiesSmartEdit);
    const auto* propertyDef = mdl::selectPropertyDefinition(key, entityNodes);

    auto labelText = QString::fromStdString(key);
    if (isSmartEdit && propertyDef != nullptr && !propertyDef->shortDescription.empty())
    {
      labelText = toUnicodeFromFgd(propertyDef->shortDescription);
    }

    auto* keyLabel = new ElidedLabel{labelText, Qt::ElideRight, row};
    keyLabel->setObjectName("outlinerPropertyKey");
    keyLabel->setProperty("propertyKey", QString::fromStdString(key));
    keyLabel->setMinimumWidth(0);
    keyLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    keyLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    const auto rowTooltip = makePropertyTooltip(key, propertyDef);
    keyLabel->setToolTip(rowTooltip);

    QLineEdit* valueEdit = nullptr;
    QComboBox* valueCombo = nullptr;

    if (propertyDef != nullptr)
    {
      if (
        const auto* choiceType =
          std::get_if<mdl::PropertyValueTypes::Choice>(&propertyDef->valueType))
      {
        valueCombo = new OutlinerChoiceComboBox{m_comboPopupVisible, this, row};
        valueCombo->setObjectName("outlinerPropertyValue");
        valueCombo->setEditable(true);
        valueCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        valueCombo->setProperty("propertyKey", QString::fromStdString(key));

        for (const auto& option : choiceType->options)
        {
          const auto value = toUnicodeFromFgd(option.value);
          const auto label = toUnicodeFromFgd(option.value + " : " + option.description);
          valueCombo->addItem(label, value);
        }

        if (auto* comboLineEdit = valueCombo->lineEdit())
        {
          comboLineEdit->setProperty("propertyKey", QString::fromStdString(key));
        }

        if (propertyDef->readOnly)
        {
          valueCombo->setDisabled(true);
        }
      }
    }

    if (!valueCombo)
    {
      valueEdit = new QLineEdit{row};
      valueEdit->setObjectName("outlinerPropertyValue");
      valueEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
      valueEdit->setProperty("propertyKey", QString::fromStdString(key));

      if (propertyDef && propertyDef->readOnly)
      {
        valueEdit->setDisabled(true);
      }
    }

    const auto consensus = consensusValue(key, entityNodes);
    const auto inactive = propertyDef != nullptr && !consensus.anyPresent;
    if (valueCombo != nullptr)
    {
      valueCombo->setProperty("inactive", inactive);
      valueCombo->setToolTip(rowTooltip);
    }
    if (valueEdit != nullptr)
    {
      valueEdit->setProperty("inactive", inactive);
      valueEdit->setToolTip(rowTooltip);
    }
    if (inactive)
    {
      row->setProperty("inactive", true);
      keyLabel->setProperty("inactive", true);
      if (valueCombo)
      {
        valueCombo->setDisabled(true);
      }
      if (valueEdit)
      {
        valueEdit->setDisabled(true);
        setInactiveEditorPalette(valueEdit);
        valueEdit->setPlaceholderText(tr("<inactive>"));
      }
      if (valueCombo && valueCombo->lineEdit())
      {
        const QSignalBlocker blocker{valueCombo};
        valueCombo->setCurrentIndex(-1);
        valueCombo->setEditText(QString{});
        setInactiveEditorPalette(valueCombo->lineEdit());
        valueCombo->lineEdit()->setPlaceholderText(tr("<inactive>"));
      }
    }

    if (valueCombo)
    {
      if (auto* comboLineEdit = valueCombo->lineEdit())
      {
        if (!inactive && consensus.mixed)
        {
          const QSignalBlocker blocker{valueCombo};
          valueCombo->setCurrentIndex(-1);
          valueCombo->setEditText(QString{});
          comboLineEdit->setPlaceholderText(tr("<multiple>"));
        }
        else if (!inactive)
        {
          const QSignalBlocker blocker{valueCombo};
          valueCombo->setEditText(
            mapStringToUnicode(m_document.map().encoding(), consensus.value));
          comboLineEdit->setPlaceholderText(QString{});
        }
      }
    }
    else if (valueEdit)
    {
      if (!inactive && consensus.mixed)
      {
        valueEdit->clear();
        valueEdit->setPlaceholderText(tr("<multiple>"));
      }
      else if (!inactive)
      {
        valueEdit->setText(QString::fromStdString(consensus.value));
        valueEdit->setPlaceholderText(QString{});
      }
    }

    auto* activateButton = createBitmapButton("Add.svg", tr("Activate property"), row);
    activateButton->setProperty("tbControlRole", "compact");
    activateButton->setIconSize(QSize{16, 16});
    activateButton->setFixedSize(QSize{24, 24});
    activateButton->setProperty("propertyKey", QString::fromStdString(key));
    activateButton->setHidden(!inactive);

    auto* removeButton = createBitmapButton("Remove.svg", tr("Remove property"), row);
    removeButton->setProperty("tbControlRole", "compact");
    removeButton->setIconSize(QSize{16, 16});
    removeButton->setFixedSize(QSize{24, 24});
    removeButton->setProperty("propertyKey", QString::fromStdString(key));
    removeButton->setHidden(inactive);
    if (propertyDef && propertyDef->readOnly)
    {
      removeButton->setDisabled(true);
    }
    if (!inactive)
    {
      auto keyMutable = true;
      for (const auto* node : entityNodes)
      {
        keyMutable = keyMutable && isPropertyKeyMutable(node->entity(), key);
      }
      if (!keyMutable)
      {
        removeButton->setDisabled(true);
      }
    }

    QToolButton* modelBrowseButton = nullptr;
    if (!inactive && key == "model")
    {
      modelBrowseButton = createBitmapButton("Folder.svg", tr("Load model file"), row);
      modelBrowseButton->setProperty("tbControlRole", "compact");
      modelBrowseButton->setIconSize(QSize{16, 16});
      modelBrowseButton->setFixedSize(QSize{24, 24});
      if (propertyDef && propertyDef->readOnly)
      {
        modelBrowseButton->setDisabled(true);
      }
    }

    QToolButton* colorButton = nullptr;
    if (
      !inactive
      && (matchesSmartColorKeyPattern(key) || (propertyDef && isColorPropertyDefinition(*propertyDef))))
    {
      colorButton = new QToolButton{row};
      colorButton->setObjectName("outlinerPropertyColorButton");
      colorButton->setProperty("tbControlRole", "compact");
      colorButton->setFixedSize(QSize{24, 24});
      colorButton->setIconSize(QSize{14, 14});
      colorButton->setProperty("propertyKey", QString::fromStdString(key));
      colorButton->setToolTip(tr("Choose color"));

      auto displayColor = QColor{Qt::black};
      if (!entityNodes.empty())
      {
        if (!consensus.mixed && consensus.anyPresent)
        {
          if (
            const auto qColor = parseEntityColorToQColor(
              entityNodes.front()->entity().definition(), key, consensus.value))
          {
            displayColor = *qColor;
          }
        }
        else
        {
          const auto* nodeWithValue = [&]() -> const mdl::EntityNodeBase* {
            for (const auto* node : entityNodes)
            {
              if (node->entity().property(key) != nullptr)
              {
                return node;
              }
            }
            return entityNodes.front();
          }();

          if (nodeWithValue)
          {
            if (const auto* propertyValue = nodeWithValue->entity().property(key))
            {
              if (
                const auto qColor = parseEntityColorToQColor(
                  nodeWithValue->entity().definition(), key, *propertyValue))
              {
                displayColor = *qColor;
              }
            }
          }
        }
      }

      colorButton->setIcon(colorSwatchIcon(displayColor, colorButton->palette()));
      colorButton->setProperty("displayColor", displayColor);
    }

    QToolButton* spawnflagsToggleButton = nullptr;
    if (
      !inactive && key == mdl::EntityPropertyKeys::Spawnflags && propertyDef
      && std::holds_alternative<mdl::PropertyValueTypes::Flags>(propertyDef->valueType))
    {
      spawnflagsToggleButton = new QToolButton{row};
      spawnflagsToggleButton->setProperty("tbControlRole", "compact");
      spawnflagsToggleButton->setCheckable(true);
      spawnflagsToggleButton->setFixedSize(QSize{24, 24});
      spawnflagsToggleButton->setToolTip(tr("Show spawnflags editor"));

      if (propertyDef->readOnly)
      {
        spawnflagsToggleButton->setDisabled(true);
      }

      const QSignalBlocker blocker{spawnflagsToggleButton};
      spawnflagsToggleButton->setChecked(m_spawnflagsEditorExpanded);
      spawnflagsToggleButton->setArrowType(
        m_spawnflagsEditorExpanded ? Qt::DownArrow : Qt::RightArrow);
    }

    QToolButton* wadToggleButton = nullptr;
    if (canShowWadEditor && key == *wadKey)
    {
      wadToggleButton = new QToolButton{row};
      wadToggleButton->setProperty("tbControlRole", "compact");
      wadToggleButton->setCheckable(true);
      wadToggleButton->setFixedSize(QSize{24, 24});
      wadToggleButton->setToolTip(tr("Show wad file editor"));

      const QSignalBlocker blocker{wadToggleButton};
      wadToggleButton->setChecked(m_wadEditorExpanded);
      wadToggleButton->setArrowType(m_wadEditorExpanded ? Qt::DownArrow : Qt::RightArrow);
    }

    QToolButton* skyboxToggleButton = nullptr;
    if (canShowSkyboxEditor && key == mdl::EntityPropertyKeys::Skyname)
    {
      skyboxToggleButton = new QToolButton{row};
      skyboxToggleButton->setProperty("tbControlRole", "compact");
      skyboxToggleButton->setCheckable(true);
      skyboxToggleButton->setFixedSize(QSize{24, 24});
      skyboxToggleButton->setProperty("propertyKey", QString::fromStdString(key));
      skyboxToggleButton->setToolTip(tr("Show skybox editor"));

      if (propertyDef && propertyDef->readOnly)
      {
        skyboxToggleButton->setDisabled(true);
      }

      const QSignalBlocker blocker{skyboxToggleButton};
      skyboxToggleButton->setChecked(m_skyboxEditorExpanded);
      skyboxToggleButton->setArrowType(
        m_skyboxEditorExpanded ? Qt::DownArrow : Qt::RightArrow);
    }

    auto* actions = new QWidget{row};
    actions->setObjectName("outlinerPropertyActions");
    actions->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    actionWidgets.push_back(actions);
    auto* actionsLayout = new QHBoxLayout{actions};
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(2);

    if (spawnflagsToggleButton)
    {
      actionsLayout->addWidget(spawnflagsToggleButton);
    }
    if (wadToggleButton)
    {
      actionsLayout->addWidget(wadToggleButton);
    }
    if (skyboxToggleButton)
    {
      actionsLayout->addWidget(skyboxToggleButton);
    }
    if (modelBrowseButton)
    {
      actionsLayout->addWidget(modelBrowseButton);
    }
    if (colorButton)
    {
      actionsLayout->addWidget(colorButton);
    }
    actionsLayout->addWidget(activateButton);
    actionsLayout->addWidget(removeButton);

    row->setWidgets(
      keyLabel, valueCombo ? static_cast<QWidget*>(valueCombo) : valueEdit, actions);

    m_scrollLayout->addWidget(row, 0);

    if (spawnflagsToggleButton)
    {
      auto* container = new QWidget{m_scrollContents};
      container->setObjectName("outlinerEmbeddedSpawnflagsEditor");
      auto* containerLayout = new QVBoxLayout{container};
      containerLayout->setContentsMargins(6, 0, 6, 4);
      containerLayout->setSpacing(0);

      m_embeddedSpawnflagsEditorContainer = container;
      auto* flagsEditor = new FlagsEditor{SpawnflagsNumCols, container};
      m_embeddedSpawnflagsEditor = flagsEditor;

      auto labels = QStringList{};
      auto tooltips = QStringList{};
      getSpawnflagsLabelsAndTooltips(entityNodes, key, labels, tooltips);
      flagsEditor->setFlags(labels, tooltips);

      int setFlags = 0;
      int mixedFlags = 0;
      getSpawnflagsSetAndMixedValues(entityNodes, key, setFlags, mixedFlags);
      flagsEditor->setFlagValue(setFlags, mixedFlags);

      if (propertyDef && propertyDef->readOnly)
      {
        flagsEditor->setDisabled(true);
      }

      containerLayout->addWidget(flagsEditor, 1);
      container->setVisible(m_spawnflagsEditorExpanded);
      m_scrollLayout->addWidget(container, 0);

      connect(
        spawnflagsToggleButton,
        &QToolButton::toggled,
        this,
        [this, container, spawnflagsToggleButton](const bool checked) {
          m_spawnflagsEditorExpanded = checked;
          spawnflagsToggleButton->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
          container->setVisible(checked);
        });

      connect(
        flagsEditor,
        &FlagsEditor::flagChanged,
        this,
        [this, flagsEditor, key](const size_t index, const int, const int, const int) {
          const auto set = flagsEditor->isFlagSet(index);
          mdl::updateEntitySpawnflag(m_document.map(), key, index, set);
          scheduleUpdate(true);
        });
    }

    if (wadToggleButton)
    {
      auto* container = new QWidget{m_scrollContents};
      container->setObjectName("outlinerEmbeddedWadEditor");
      auto* containerLayout = new QVBoxLayout{container};
      containerLayout->setContentsMargins(6, 0, 6, 4);
      containerLayout->setSpacing(0);

      m_embeddedWadEditorContainer = container;
      m_embeddedWadEditor = new SmartWadEditor{m_document, container};
      m_embeddedWadEditor->activate(*wadKey);
      m_embeddedWadEditor->update(entityNodes);
      containerLayout->addWidget(m_embeddedWadEditor, 1);

      container->setVisible(m_wadEditorExpanded);
      m_scrollLayout->addWidget(container, 0);

      connect(
        wadToggleButton,
        &QToolButton::toggled,
        this,
        [this, container, wadToggleButton](const bool checked) {
          m_wadEditorExpanded = checked;
          wadToggleButton->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
          container->setVisible(checked);
        });
    }

    if (skyboxToggleButton)
    {
      auto* container = new QWidget{m_scrollContents};
      container->setObjectName("outlinerEmbeddedSkyboxEditor");
      auto* containerLayout = new QVBoxLayout{container};
      containerLayout->setContentsMargins(6, 0, 6, 4);
      containerLayout->setSpacing(0);

      m_embeddedSkyboxEditorContainer = container;
      m_embeddedSkyboxEditor = new SmartSkyboxEditor{m_document, container};
      m_embeddedSkyboxEditor->activate(mdl::EntityPropertyKeys::Skyname);
      m_embeddedSkyboxEditor->update(entityNodes);
      containerLayout->addWidget(m_embeddedSkyboxEditor, 1);
      connect(m_embeddedSkyboxEditor, &SmartSkyboxEditor::skyboxApplied, this, [this]() {
        refreshVisiblePropertyValues();
        refreshEmbeddedEditors();
      });

      container->setVisible(m_skyboxEditorExpanded);
      m_scrollLayout->addWidget(container, 0);

      connect(
        skyboxToggleButton,
        &QToolButton::toggled,
        this,
        [this, container, skyboxToggleButton](const bool checked) {
          m_skyboxEditorExpanded = checked;
          skyboxToggleButton->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
          container->setVisible(checked);
        });
    }

    if (colorButton)
    {
      connect(
        colorButton,
        &QAbstractButton::clicked,
        this,
        [this, key, propertyDef, colorButton]() {
          const auto qColor = QColorDialog::getColor(
            colorButton->property("displayColor").value<QColor>(),
            colorButton,
            tr("Choose color"));
          if (!qColor.isValid())
          {
            return;
          }

          colorButton->setIcon(colorSwatchIcon(qColor, colorButton->palette()));
          colorButton->setProperty("displayColor", qColor);
          auto requestedColor = Rgb{fromQColor(qColor).to<RgbB>()};
          if (!m_document.map().selection().allEntities().empty())
          {
            const auto range =
              mdl::detectColorRange(key, m_document.map().selection().allEntities());
            if (range == mdl::ColorRange::Float)
            {
              requestedColor = Rgb{fromQColor(qColor).to<RgbF>()};
            }
            else if (range == mdl::ColorRange::Byte)
            {
              requestedColor = Rgb{fromQColor(qColor).to<RgbB>()};
            }
            else if (propertyDef)
            {
              if (std::holds_alternative<mdl::PropertyValueTypes::Color<RgbF>>(
                    propertyDef->valueType))
              {
                requestedColor = Rgb{fromQColor(qColor).to<RgbF>()};
              }
              else if (std::holds_alternative<mdl::PropertyValueTypes::Color<RgbB>>(
                         propertyDef->valueType))
              {
                requestedColor = Rgb{fromQColor(qColor).to<RgbB>()};
              }
            }
          }

          mdl::setEntityColorProperty(m_document.map(), key, requestedColor);
          scheduleUpdate(true);
        });
    }

    if (modelBrowseButton)
    {
      connect(modelBrowseButton, &QAbstractButton::clicked, this, [this, key]() {
        const auto caption = tr("Load Model File");
        const auto filter = tr("Model files (*.mdl);;All files (*.*)");

        const auto pathQStr = QFileDialog::getOpenFileName(
          nullptr, caption, fileDialogDefaultDirectory(FileDialogDir::GamePath), filter);
        if (pathQStr.isEmpty())
        {
          return;
        }

        updateFileDialogDefaultDirectoryWithFilename(FileDialogDir::GamePath, pathQStr);

        const auto absModelPath = pathFromQString(pathQStr);
        const auto gamePath = m_document.map().gamePath();

        auto ec = std::error_code{};
        const auto relativeModelPathFull =
          std::filesystem::relative(absModelPath, gamePath, ec);
        if (ec)
        {
          return;
        }

        auto relativePathStr = relativeModelPathFull.string();
        auto modelsPos = relativePathStr.find("models/");
        if (modelsPos == std::string::npos)
        {
          modelsPos = relativePathStr.find("models\\");
        }

        std::string finalModelPath;
        if (modelsPos != std::string::npos)
        {
          finalModelPath = relativePathStr.substr(modelsPos);
        }
        else
        {
          finalModelPath = relativeModelPathFull.generic_string();
        }

        for (auto& ch : finalModelPath)
        {
          if (ch == '\\')
          {
            ch = '/';
          }
        }

        if (finalModelPath.empty())
        {
          return;
        }

        mdl::setEntityProperty(m_document.map(), key, finalModelPath, false);
        scheduleUpdate(true);
      });
    }

    connect(
      activateButton,
      &QAbstractButton::clicked,
      this,
      [this, activateButton, propertyDef]() {
        const auto keyVariant = activateButton->property("propertyKey");
        if (!keyVariant.isValid())
        {
          return;
        }

        auto value = std::string{};
        if (propertyDef)
        {
          value = mdl::PropertyDefinition::defaultValue(*propertyDef).value_or("");
        }

        mdl::setEntityProperty(
          m_document.map(), keyVariant.toString().toStdString(), value, false);
        scheduleUpdate(true);
      });

    if (valueCombo)
    {
      connect(
        valueCombo,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        [this, valueCombo](const int index) {
          const auto keyVariant = valueCombo->property("propertyKey");
          if (!keyVariant.isValid())
          {
            return;
          }

          if (index < 0)
          {
            return;
          }

          const auto valueVariant = valueCombo->itemData(index);
          if (!valueVariant.isValid())
          {
            return;
          }

          valueCombo->setFocus(Qt::MouseFocusReason);

          const auto value = valueVariant.toString();
          {
            const QSignalBlocker blocker{valueCombo};
            valueCombo->setEditText(value);
          }

          mdl::setEntityProperty(
            m_document.map(),
            keyVariant.toString().toStdString(),
            mapStringFromUnicode(m_document.map().encoding(), value),
            false);
        });

      if (auto* comboLineEdit = valueCombo->lineEdit())
      {
        connect(comboLineEdit, &QLineEdit::editingFinished, this, [this, valueCombo]() {
          const auto keyVariant = valueCombo->property("propertyKey");
          if (!keyVariant.isValid())
          {
            return;
          }

          auto value = valueCombo->currentText();
          const auto optionIndex = valueCombo->findText(value, Qt::MatchExactly);
          if (optionIndex >= 0)
          {
            const auto valueVariant = valueCombo->itemData(optionIndex);
            if (valueVariant.isValid())
            {
              value = valueVariant.toString();
            }
          }

          mdl::setEntityProperty(
            m_document.map(),
            keyVariant.toString().toStdString(),
            mapStringFromUnicode(m_document.map().encoding(), value),
            false);
          scheduleUpdate();
        });
      }
    }
    else
    {
      connect(valueEdit, &QLineEdit::editingFinished, this, [this, valueEdit]() {
        const auto keyVariant = valueEdit->property("propertyKey");
        if (!keyVariant.isValid())
        {
          return;
        }
        const auto propertyKey = keyVariant.toString().toStdString();
        mdl::setEntityProperty(
          m_document.map(), propertyKey, valueEdit->text().toStdString(), false);
        scheduleUpdate();
      });
    }

    connect(removeButton, &QAbstractButton::clicked, this, [this, removeButton]() {
      const auto keyVariant = removeButton->property("propertyKey");
      if (!keyVariant.isValid())
      {
        return;
      }
      const auto propertyKey = keyVariant.toString().toStdString();
      mdl::removeEntityProperty(m_document.map(), propertyKey);
      scheduleUpdate(true);
    });
  }

  const auto actionWidth = std::ranges::max(
    actionWidgets | std::views::transform([](const auto* widget) {
      return widget->sizeHint().width();
    }),
    {},
    std::identity{});
  for (auto* actionWidget : actionWidgets)
  {
    actionWidget->setFixedWidth(actionWidth);
  }

  m_scrollLayout->addStretch(1);
}

} // namespace tb::ui
