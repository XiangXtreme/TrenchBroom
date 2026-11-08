#include "ui/outliner/OutlinerInspector.h"

#include <QAbstractButton>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QSettings>
#include <QSize>
#include <QSizePolicy>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include "mdl/LayerNode.h"
#include "mdl/Map_Layers.h"
#include "ui/BitmapButton.h"
#include "ui/MapDocument.h"
#include "ui/QWidgetUtils.h"
#include "ui/SearchBox.h"
#include "ui/SelectionPropertyEditor.h"
#include "ui/Splitter.h"
#include "ui/ViewUtils.h"
#include "ui/WidgetState.h"
#include "ui/outliner/OutlinerTreeWidget.h"

namespace tb::ui
{

OutlinerInspector::OutlinerInspector(MapDocument& document, QWidget* parent)
  : TabBookPage(parent)
  , m_document(document)
{
  if (objectName().isEmpty())
  {
    setObjectName("OutlinerInspector");
  }

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  auto* topRowWidget = new QWidget{this};
  topRowWidget->setObjectName("OutlinerInspector_Toolbar");
  topRowWidget->setAttribute(Qt::WA_StyledBackground, true);
  topRowWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

  auto* toolbarLayout = new QVBoxLayout{topRowWidget};
  toolbarLayout->setContentsMargins(8, 5, 8, 5);
  toolbarLayout->setSpacing(6);
  auto* topRow = new QHBoxLayout{};
  topRow->setContentsMargins(0, 0, 0, 0);
  topRow->setSpacing(6);

  m_searchField = createSearchBox(topRowWidget);
  m_searchField->setObjectName("OutlinerInspector_Search");
  m_searchField->setPlaceholderText(tr("Search scene"));
  m_searchField->setToolTip(
    tr("Search names, type:entity, type:group, vis:hidden, lock:locked or selected"));
  toolbarLayout->addWidget(m_searchField);
  toolbarLayout->addLayout(topRow);

  m_sortBox = new QComboBox(topRowWidget);
  m_sortBox->setObjectName("OutlinerInspector_Sort");
  m_sortBox->setMinimumWidth(74);
  m_sortBox->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  m_sortBox->setProperty("tbControlRole", "compact");
  m_sortBox->setToolTip(
    tr("Sort objects in the view; layer order is changed separately"));
  m_sortBox->addItem(
    tr("Default"), static_cast<int>(OutlinerTreeWidget::SortMode::Default));
  m_sortBox->addItem(tr("Type"), static_cast<int>(OutlinerTreeWidget::SortMode::Type));
  m_sortBox->addItem(
    tr("File Order"), static_cast<int>(OutlinerTreeWidget::SortMode::FileOrder));
  {
    const auto settings = QSettings{};
    const auto sortMode = settings
                            .value(
                              widgetSettingsPath(this, "SortMode"),
                              static_cast<int>(OutlinerTreeWidget::SortMode::Default))
                            .toInt();
    const auto index = m_sortBox->findData(sortMode);
    m_sortBox->setCurrentIndex(index >= 0 ? index : 0);
  }
  topRow->addWidget(m_sortBox);
  topRow->addStretch();

  auto* addLayerButton =
    createBitmapButton("Add.svg", tr("Add a new layer"), topRowWidget);
  addLayerButton->setObjectName("OutlinerInspector_AddLayer");
  addLayerButton->setProperty("tbControlRole", "compact");
  addLayerButton->setAccessibleName(tr("Add a new layer"));
  addLayerButton->setIconSize(QSize{16, 16});
  topRow->addWidget(addLayerButton);

  auto* mapSettings = createBitmapButton(
    "GeneralPreferences.svg", tr("Map Settings… (Ctrl+1)"), topRowWidget);
  mapSettings->setObjectName("OutlinerInspector_MapSettings");
  mapSettings->setProperty("tbControlRole", "compact");
  mapSettings->setAccessibleName(tr("Map Settings…"));
  topRow->addWidget(mapSettings);
  connect(
    mapSettings, &QToolButton::clicked, this, &OutlinerInspector::mapSettingsRequested);

  auto* treeOptions =
    createBitmapButton("InspectorOutliner.svg", tr("Scene tree options"), topRowWidget);
  treeOptions->setObjectName("OutlinerInspector_TreeOptions");
  treeOptions->setProperty("tbControlRole", "compact");
  treeOptions->setPopupMode(QToolButton::InstantPopup);
  auto* treeMenu = new QMenu{treeOptions};
  treeOptions->setMenu(treeMenu);
  topRow->addWidget(treeOptions);
  auto* treeVisible = treeMenu->addAction(tr("Show scene tree"));
  treeVisible->setObjectName("OutlinerInspector_TreeVisible");
  treeVisible->setCheckable(true);
  treeVisible->setChecked(
    QSettings{}.value(widgetSettingsPath(this, "TreeVisible"), true).toBool());

  auto* propertiesToggle = m_propertiesToggle =
    createBitmapToggleButton("Map_entity.svg", tr("Toggle properties panel"), this);
  propertiesToggle->setObjectName("OutlinerInspector_PropertiesToggle");
  propertiesToggle->setProperty("tbControlRole", "compact");
  propertiesToggle->setIconSize(QSize{16, 16});
  topRow->addWidget(propertiesToggle);

  layout->addWidget(topRowWidget, 0);

  m_splitter = new Splitter{Qt::Vertical};
  m_splitter->setObjectName("OutlinerInspector_Splitter");

  m_treeWidget = new OutlinerTreeWidget(m_document, m_splitter);
  m_splitter->addWidget(m_treeWidget);
  treeMenu->addSeparator();
  treeMenu->addAction(tr("Expand all"), m_treeWidget, &QTreeWidget::expandAll);
  treeMenu->addAction(tr("Collapse all"), m_treeWidget, &QTreeWidget::collapseAll);
  treeMenu->addAction(tr("Reveal selection"), this, [this]() {
    m_searchField->clear();
    m_treeWidget->revealSelection();
  });

  m_propertyEditor = new SelectionPropertyEditor{m_document, m_splitter};
  m_splitter->addWidget(m_propertyEditor);

  connect(addLayerButton, &QAbstractButton::clicked, this, [this]() {
    const auto name = queryLayerName(this, "Unnamed");
    if (name.empty())
    {
      return;
    }

    if (auto* layerNode = mdl::createLayer(m_document.map(), name))
    {
      m_searchField->clear();
      m_treeWidget->revealNode(layerNode);
    }
  });

  m_splitter->setStretchFactor(0, 2);
  m_splitter->setStretchFactor(1, 3);
  m_splitter->setSizes({300, 500});

  layout->addWidget(m_splitter, 1);

  restoreWidgetState(m_splitter);

  {
    auto settings = QSettings{};
    const auto visiblePath = widgetSettingsPath(this, "PropertiesVisible");
    const auto propertiesVisible =
      !treeVisible->isChecked() || settings.value(visiblePath, true).toBool();

    propertiesToggle->setChecked(propertiesVisible);
    m_propertyEditor->setVisible(propertiesVisible);
    m_treeWidget->setVisible(treeVisible->isChecked());
    if (!propertiesVisible)
    {
      m_splitter->setSizes({1, 0});
    }
  }

  connect(
    propertiesToggle,
    &QAbstractButton::clicked,
    this,
    [this, treeVisible](const bool checked) {
      auto settings = QSettings{};
      const auto visiblePath = widgetSettingsPath(this, "PropertiesVisible");
      settings.setValue(visiblePath, checked);

      if (checked)
      {
        m_propertyEditor->setVisible(true);
        restoreWidgetState(m_splitter);
        const auto sizes = m_splitter->sizes();
        if (sizes.size() >= 2 && sizes.at(1) == 0)
        {
          m_splitter->setSizes({300, 500});
        }
      }
      else
      {
        if (!m_treeWidget->isHidden())
          saveWidgetState(m_splitter);
        treeVisible->setChecked(true);
        m_propertyEditor->setVisible(false);
        m_splitter->setSizes({1, 0});
      }
    });

  connect(
    treeVisible, &QAction::toggled, this, [this, propertiesToggle](const bool visible) {
      if (!visible)
      {
        if (!m_propertyEditor->isHidden())
          saveWidgetState(m_splitter);
        else
          propertiesToggle->click();
      }
      m_treeWidget->setVisible(visible);
      if (visible && !m_propertyEditor->isHidden())
      {
        restoreWidgetState(m_splitter);
        const auto sizes = m_splitter->sizes();
        if (sizes.size() == 2 && (sizes[0] == 0 || sizes[1] == 0))
          m_splitter->setSizes({300, 500});
      }
      auto settings = QSettings{};
      settings.setValue(widgetSettingsPath(this, "TreeVisible"), visible);
    });

  auto* searchTimer = new QTimer{this};
  searchTimer->setInterval(120);
  searchTimer->setSingleShot(true);
  connect(
    m_searchField,
    &QLineEdit::textChanged,
    this,
    [this, searchTimer](const QString& text) {
      if (text.trimmed().isEmpty())
      {
        searchTimer->stop();
        m_treeWidget->setFilterText({});
      }
      else
      {
        searchTimer->start();
      }
    });
  connect(searchTimer, &QTimer::timeout, this, [this]() {
    m_treeWidget->setFilterText(m_searchField->text());
  });
  connect(m_sortBox, &QComboBox::currentIndexChanged, this, [this](int index) {
    const auto modeValue = m_sortBox->itemData(index).toInt();
    m_treeWidget->setSortMode(static_cast<OutlinerTreeWidget::SortMode>(modeValue));
    auto settings = QSettings{};
    settings.setValue(widgetSettingsPath(this, "SortMode"), modeValue);
  });

  m_treeWidget->setSortMode(
    static_cast<OutlinerTreeWidget::SortMode>(m_sortBox->currentData().toInt()));
}

void OutlinerInspector::showProperties()
{
  if (!m_propertiesToggle->isChecked())
    m_propertiesToggle->click();
}

OutlinerInspector::~OutlinerInspector()
{
  if (!m_propertyEditor->isHidden() && !m_treeWidget->isHidden())
  {
    saveWidgetState(m_splitter);
  }
}

} // namespace tb::ui
