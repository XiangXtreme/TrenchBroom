/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/TerrainInspector.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QRubberBand>
#include <QScopeGuard>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include "base/PreferenceManager.h"
#include "gl/Camera.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/EditorContext.h"
#include "mdl/EntityDefinition.h"
#include "mdl/EntityDefinitionManager.h"
#include "mdl/EntityNode.h"
#include "mdl/GameInfo.h"
#include "mdl/Grid.h"
#include "mdl/GroupNode.h"
#include "mdl/HitAdapter.h"
#include "mdl/HitFilter.h"
#include "mdl/Map.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Picking.h"
#include "mdl/NodeContents.h"
#include "mdl/PickResult.h"
#include "mdl/TransactionScope.h"
#include "mdl/WorldNode.h"
#include "prefs/Preferences.h"
#include "project/ProjectStore.h"
#include "render/RenderContext.h"
#include "render/RenderService.h"
#include "render/RenderTiming.h"
#include "render/SelectionBoundsRenderer.h"
#include "ui/AppController.h"
#include "ui/AssetLibraryWindow.h"
#include "ui/CollapsibleTitledPanel.h"
#include "ui/MapDocument.h"
#include "ui/MapViewToolBox.h"
#include "ui/PanelLayout.h"
#include "ui/ProjectSession.h"
#include "ui/QPathUtils.h"
#include "ui/TerrainExport.h"
#include "ui/TerrainMaterial.h"
#include "ui/TerrainOperations.h"
#include "ui/TerrainTool.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace tb::ui
{
namespace
{
QString terrainErrorMessage(const Error& error)
{
  return QCoreApplication::translate("tb::ui::TerrainInspector", error.msg.c_str());
}

bool terrainEditable(const mdl::Map& map, const mdl::GroupNode& group)
{
  const auto check = [&](const auto& self, const mdl::Node& node) -> bool {
    return map.editorContext().editable(node) && map.editorContext().visible(node)
           && std::ranges::all_of(
             node.children(), [&](const auto* child) { return self(self, *child); });
  };
  return check(check, group);
}

bool terrainOwnsBrush(const mdl::GroupNode& group, const mdl::BrushNode& brush)
{
  auto* parent = brush.parent();
  if (dynamic_cast<const mdl::EntityNode*>(parent))
    parent = parent->parent();
  if (parent == &group)
    return true;
  return isTerrainChunkGroup(group) && parent && parent->parent() == &group;
}

Result<size_t> terrainSurfaceCount(
  const mdl::GroupNode& group, const TerrainRegion& region)
{
  const auto surfaces = terrainGroupSurfaces(group, region);
  if (surfaces.is_error())
    return std::get<Error>(surfaces.error());
  return surfaces.value().size();
}

vm::vec3d terrainVertex(
  const TerrainRegion& region, const mdl::GroupNode& group, const size_t index)
{
  const auto& field = *region.heightField;
  const auto width = size_t(field.columns + 1);
  const auto column = index % width;
  const auto row = index / width;
  return group.group().transformation()
         * vm::vec3d{
           region.origin.x() + double(column) * field.spacing,
           region.origin.y() - double(row) * field.spacing,
           field.heights[index]};
}

bool terrainEdgeVertex(const TerrainHeightField& field, const size_t index)
{
  const auto width = size_t(field.columns + 1);
  const auto column = index % width;
  const auto row = index / width;
  return column == 0 || column == size_t(field.columns) || row == 0
         || row == size_t(field.rows);
}

std::optional<QPointF> terrainVertexScreenPosition(
  const gl::Camera& camera, const vm::vec3d& position)
{
  const auto projected = camera.project(vm::vec3f{position});
  if (projected.z() < 0.0f || projected.z() > 1.0f)
    return std::nullopt;
  return QPointF{double(projected.x()), double(camera.viewport().height) - projected.y()};
}
} // namespace

TerrainInspector::TerrainInspector(
  AppController& app, MapDocument& document, MapViewToolBox& toolBox)
  : m_app{app}
  , m_document{document}
  , m_toolBox{&toolBox}
{
  m_status = new QLabel{this};
  m_status->setObjectName(QStringLiteral("TerrainInspector_Status"));
  m_status->setWordWrap(true);
  m_status->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  m_help = new QLabel{this};
  m_help->setObjectName(QStringLiteral("TerrainInspector_Help"));
  m_help->setWordWrap(true);

  m_editMode = new QComboBox{this};
  m_editMode->setObjectName(QStringLiteral("TerrainInspector_EditMode"));
  m_editMode->addItem(tr("Material"));
  m_editMode->addItem(tr("Sculpt"));
  m_editMode->addItem(tr("Vertices"));

  m_sculptMode = new QComboBox{this};
  m_sculptMode->setObjectName(QStringLiteral("TerrainInspector_SculptMode"));
  m_sculptMode->addItem(tr("Raise"), int(TerrainSculptMode::Raise));
  m_sculptMode->addItem(tr("Lower"), int(TerrainSculptMode::Lower));
  m_sculptMode->addItem(tr("Smooth"), int(TerrainSculptMode::Smooth));
  m_sculptMode->addItem(tr("Flatten"), int(TerrainSculptMode::Flatten));

  m_gridSpacing = new QComboBox{this};
  m_gridSpacing->setObjectName(QStringLiteral("TerrainInspector_GridSpacing"));
  for (const auto spacing : {16, 32, 64, 128})
    m_gridSpacing->addItem(tr("%1 units").arg(spacing), spacing);
  m_gridSpacing->setCurrentIndex(m_gridSpacing->findData(64));

  m_lockEdges = new QCheckBox{tr("Lock terrain edges"), this};
  m_lockEdges->setObjectName(QStringLiteral("TerrainInspector_LockEdges"));
  m_lockEdges->setChecked(true);

  m_softSelection = new QCheckBox{tr("Soft selection (3D sphere)"), this};
  m_softSelection->setObjectName(QStringLiteral("TerrainInspector_SoftSelection"));
  m_softSelection->setToolTip(
    tr("Drag the terrain surface to move vertices inside a 3D sphere with distance "
       "falloff."));
  m_selectOccluded = new QCheckBox{tr("Select occluded vertices"), this};
  m_selectOccluded->setObjectName(QStringLiteral("TerrainInspector_SelectOccluded"));
  m_softRadius = new QSpinBox{this};
  m_softRadius->setObjectName(QStringLiteral("TerrainInspector_SelectionRadius"));
  m_softRadius->setRange(1, 4096);
  m_softRadius->setValue(128);
  m_softRadius->setSuffix(tr(" units"));
  m_softHardness = new QSpinBox{this};
  m_softHardness->setObjectName(QStringLiteral("TerrainInspector_SelectionHardness"));
  m_softHardness->setRange(0, 100);
  m_softHardness->setSuffix(QStringLiteral("%"));

  m_newHeight = new QSpinBox{this};
  m_newHeight->setObjectName(QStringLiteral("TerrainInspector_NewHeight"));
  m_newHeight->setRange(-65536, 65536);
  m_newHeight->setSuffix(tr(" units"));
  m_newThickness = new QSpinBox{this};
  m_newThickness->setObjectName(QStringLiteral("TerrainInspector_NewThickness"));
  m_newThickness->setRange(1, 65536);
  m_newThickness->setValue(64);
  m_newThickness->setSuffix(tr(" units"));

  m_brushEntity = new QCheckBox{tr("Compile as brush entity"), this};
  m_brushEntity->setObjectName(QStringLiteral("TerrainInspector_BrushEntity"));
  m_brushEntityClass = new QComboBox{this};
  m_brushEntityClass->setObjectName(QStringLiteral("TerrainInspector_BrushEntityClass"));
  m_brushEntityClass->setAccessibleName(tr("Terrain brush entity class"));
  refreshBrushEntityDefinitions();

  m_material = new QComboBox{this};
  m_material->setObjectName(QStringLiteral("TerrainInspector_SlotSelect"));
  m_material->addItems({tr("Slot 0: Grass"), tr("Slot 1: Dirt"), tr("Slot 2: Rock")});

  m_terrainMaterial = new QComboBox{this};
  m_terrainMaterial->setObjectName(QStringLiteral("TerrainInspector_TerrainMaterial"));

  m_radius = new QSpinBox{};
  m_radius->setObjectName(QStringLiteral("TerrainInspector_Radius"));
  m_radius->setRange(24, 256);
  m_radius->setValue(60);
  m_radius->setSuffix(tr(" units"));

  m_strength = new QSpinBox{};
  m_strength->setObjectName(QStringLiteral("TerrainInspector_Strength"));
  m_strength->setRange(1, 100);
  m_strength->setValue(100);
  m_strength->setSuffix(QStringLiteral("%"));

  m_hardness = new QSpinBox{};
  m_hardness->setObjectName(QStringLiteral("TerrainInspector_Hardness"));
  m_hardness->setRange(0, 100);
  m_hardness->setValue(35);
  m_hardness->setSuffix(QStringLiteral("%"));
  for (auto i = size_t{1}; i < m_brushSettings.size(); ++i)
    m_brushSettings[i].strength = 25;

  m_faceBudget = new QSpinBox{};
  m_faceBudget->setObjectName(QStringLiteral("TerrainInspector_FaceBudget"));
  m_faceBudget->setRange(-1, 100000);
  m_faceBudget->setSpecialValueText(tr("Unlimited"));
  m_faceBudget->setValue(-1);
  m_faceBudget->setKeyboardTracking(false);

  m_wadBudget = new QSpinBox{};
  m_wadBudget->setObjectName(QStringLiteral("TerrainInspector_WadBudget"));
  m_wadBudget->setRange(-1, 1048576);
  m_wadBudget->setSpecialValueText(tr("Unlimited"));
  m_wadBudget->setSuffix(tr(" KiB"));
  m_wadBudget->setValue(-1);
  m_wadBudget->setKeyboardTracking(false);
  m_faceBudget->setToolTip(tr("For chunked terrain, this limit applies to each chunk."));
  m_wadBudget->setToolTip(tr("For chunked terrain, this limit applies to each chunk."));

  m_minFaceWidth = new QSpinBox{};
  m_minFaceWidth->setObjectName(QStringLiteral("TerrainInspector_MinFaceWidth"));
  m_minFaceWidth->setRange(0, 1280);
  m_minFaceWidth->setSpecialValueText(tr("Off"));
  m_minFaceWidth->setSuffix(tr(" units"));
  m_minFaceWidth->setKeyboardTracking(false);
  m_minFaceWidth->setToolTip(tr("Minimum horizontal width of every generated top face."));

  m_patchDensity = new QComboBox{};
  m_patchDensity->setObjectName(QStringLiteral("TerrainInspector_PatchDensity"));
  m_patchDensity->addItem(QStringLiteral("50%"), 50);
  m_patchDensity->addItem(QStringLiteral("100%"), 100);
  m_patchDensity->addItem(QStringLiteral("200%"), 200);
  m_patchDensity->setCurrentIndex(1);
  m_patchDensity->setToolTip(tr("Resolution of locally baked terrain patches."));

  m_estimate = new QLabel{this};
  m_estimate->setObjectName(QStringLiteral("TerrainInspector_Estimate"));
  m_estimate->setWordWrap(true);
  m_analyze = new QPushButton{tr("Analyze Terrain")};
  m_analyze->setObjectName(QStringLiteral("TerrainInspector_Analyze"));
  m_showCuts = new QCheckBox{tr("Show terrain cuts")};
  m_showCuts->setObjectName(QStringLiteral("TerrainInspector_ShowCuts"));
  m_showCuts->setToolTip(
    tr("Base: cyan; shared transition: yellow; local patch: purple; below minimum width: "
       "red."));
  m_showCuts->setEnabled(false);

  m_openLibrary = new QPushButton{tr("Asset Library…")};
  m_openLibrary->setObjectName(QStringLiteral("TerrainInspector_OpenLibrary"));

  m_create = new QPushButton{tr("Create Terrain Region")};
  m_create->setObjectName(QStringLiteral("TerrainInspector_CreateRegion"));

  m_convert = new QPushButton{tr("Create Sculptable Terrain")};
  m_convert->setObjectName(QStringLiteral("TerrainInspector_Convert"));

  m_drawNew = new QPushButton{tr("Draw New Terrain")};
  m_drawNew->setObjectName(QStringLiteral("TerrainInspector_DrawNew"));
  m_drawNew->setCheckable(true);

  m_paint = new QPushButton{tr("Paint Terrain")};
  m_paint->setObjectName(QStringLiteral("TerrainInspector_PaintTerrain"));
  m_paint->setCheckable(true);

  m_generate = new QPushButton{tr("Export Compile Snapshot…")};
  m_generate->setObjectName(QStringLiteral("TerrainInspector_ExportSnapshot"));

  auto* form = createPanelForm();
  m_form = form;
  form->addRow(tr("Edit mode"), m_editMode);
  form->addRow(tr("Sculpt tool"), m_sculptMode);
  form->addRow(m_lockEdges);
  form->addRow(m_softSelection);
  form->addRow(m_selectOccluded);
  form->addRow(tr("Selection radius"), m_softRadius);
  form->addRow(tr("Selection hardness"), m_softHardness);
  form->addRow(tr("Terrain Material"), m_terrainMaterial);
  form->addRow(tr("Paint Slot"), m_material);
  form->addRow(tr("Brush radius"), m_radius);
  form->addRow(tr("Brush strength"), m_strength);
  form->addRow(tr("Brush hardness"), m_hardness);

  auto* createForm = createPanelForm();
  createForm->addRow(tr("Grid spacing"), m_gridSpacing);
  createForm->addRow(tr("New terrain height"), m_newHeight);
  createForm->addRow(tr("New terrain thickness"), m_newThickness);
  auto* creation = new CollapsibleTitledPanel{tr("Create Terrain"), false, this};
  m_creation = creation;
  creation->setObjectName(QStringLiteral("TerrainInspector_Creation"));
  creation->setPresentation(CollapsibleTitledPanel::Presentation::Card);
  auto* creationLayout = creation->createContentLayout();
  creationLayout->addLayout(createForm);
  creationLayout->addWidget(m_drawNew);
  creationLayout->addWidget(m_convert);
  creationLayout->addWidget(m_create);

  m_compileForm = createPanelForm();
  m_compileForm->addRow(m_brushEntity);
  m_compileForm->addRow(tr("Entity class"), m_brushEntityClass);
  m_compileForm->addRow(tr("Extra top face limit"), m_faceBudget);
  m_compileForm->addRow(tr("Minimum top face width"), m_minFaceWidth);
  m_compileForm->addRow(tr("Terrain WAD limit"), m_wadBudget);
  m_compileForm->addRow(tr("Local patch density"), m_patchDensity);
  auto* compilation = new CollapsibleTitledPanel{tr("Compile Settings"), false, this};
  compilation->setObjectName(QStringLiteral("TerrainInspector_Compilation"));
  compilation->setPresentation(CollapsibleTitledPanel::Presentation::Card);
  auto* compilationLayout = compilation->createContentLayout();
  compilationLayout->addLayout(m_compileForm);
  compilationLayout->addWidget(m_analyze);
  compilationLayout->addWidget(m_showCuts);
  compilationLayout->addWidget(m_estimate);
  compilationLayout->addWidget(m_generate);

  auto* editing = new CollapsibleTitledPanel{tr("Edit Terrain"), true, this};
  editing->setObjectName(QStringLiteral("TerrainInspector_Editing"));
  editing->setPresentation(CollapsibleTitledPanel::Presentation::Card);
  auto* editingLayout = editing->createContentLayout();
  editingLayout->addLayout(form);
  editingLayout->addWidget(m_paint);
  editingLayout->addWidget(m_help);
  editingLayout->addWidget(m_openLibrary);

  auto* scroll = new PanelList{this};
  scroll->setObjectName(QStringLiteral("TerrainInspector_Scroll"));
  scroll->addWidget(creation);
  scroll->addWidget(editing);
  scroll->addWidget(compilation);
  auto* layout = new QVBoxLayout{};
  layout->addWidget(m_status);
  layout->addWidget(scroll, 1);
  setLayout(layout);

  connect(m_create, &QPushButton::clicked, this, &TerrainInspector::createRegion);
  connect(m_convert, &QPushButton::clicked, this, &TerrainInspector::convertSelection);
  connect(m_editMode, &QComboBox::currentIndexChanged, this, [this] {
    const auto wasEditing = m_toolBox && m_toolBox->terrainToolActive();
    // Deactivation notifies the panel synchronously. Keep the requested page while
    // cancelling a stroke, then activate its tool after the controls are refreshed.
    if (m_toolBox)
      m_toolBox->setTerrainMode(TerrainToolMode::None);
    updateBrushSettings();
    refresh();
    if (wasEditing && m_toolBox)
      m_toolBox->setTerrainMode(
        m_editMode->currentIndex() == 2   ? TerrainToolMode::Vertices
        : m_editMode->currentIndex() == 1 ? TerrainToolMode::Sculpt
                                          : TerrainToolMode::Paint);
    refreshViews();
  });
  connect(m_sculptMode, &QComboBox::currentIndexChanged, this, [this] {
    if (m_toolBox)
      m_toolBox->terrainTool().cancelInteraction();
    updateBrushSettings();
    updateToolHelp();
    refreshViews();
  });
  connect(m_generate, &QPushButton::clicked, this, &TerrainInspector::generate);
  connect(m_analyze, &QPushButton::clicked, this, &TerrainInspector::analyze);
  connect(m_showCuts, &QCheckBox::toggled, this, [this] { refreshViews(); });
  const auto updateBudget = [this](int) {
    auto* group = selectedTerrainGroup(m_document.map());
    if (!group)
      return;
    auto region = readTerrainGroup(*group);
    if (region.is_error())
      return;
    auto updated = std::move(region).value();
    updated.maxAddedFaces = m_faceBudget->value();
    updated.maxWadKiB = m_wadBudget->value();
    updated.minFaceWidth = m_minFaceWidth->value();
    updated.patchDensityPercent = m_patchDensity->currentData().toInt();
    if (!saveRegion(*group, updated, tr("Change Terrain Budget")))
      m_status->setText(tr("Could not save the terrain budget."));
  };
  connect(m_faceBudget, &QSpinBox::valueChanged, this, updateBudget);
  connect(m_wadBudget, &QSpinBox::valueChanged, this, updateBudget);
  connect(m_minFaceWidth, &QSpinBox::valueChanged, this, updateBudget);
  connect(m_patchDensity, &QComboBox::currentIndexChanged, this, updateBudget);
  const auto updateBrushEntity = [this] {
    m_brushEntityClass->setEnabled(
      m_brushEntity->isChecked() && m_brushEntity->isEnabled());
    if (drawingTerrain())
      return;
    auto* group = selectedTerrainGroup(m_document.map());
    if (!group)
      return;
    auto region = readTerrainGroup(*group);
    if (region.is_error() || !region.value().heightField)
      return;
    auto updated = std::move(region).value();
    updated.brushEntityClass = m_brushEntity->isChecked()
                                 ? m_brushEntityClass->currentText().toStdString()
                                 : std::string{};
    saveRegion(*group, updated, tr("Change Terrain Brush Entity"));
  };
  connect(m_brushEntity, &QCheckBox::toggled, this, updateBrushEntity);
  connect(m_brushEntityClass, &QComboBox::currentIndexChanged, this, updateBrushEntity);
  connect(
    m_openLibrary, &QPushButton::clicked, this, &TerrainInspector::openAssetLibrary);
  connect(
    m_terrainMaterial, &QComboBox::currentIndexChanged, this, [this](const int index) {
      if (m_refreshingMaterial)
        return;
      auto* group = selectedTerrainGroup(m_document.map());
      if (!group)
        return;
      auto region = readTerrainGroup(*group);
      if (region.is_error())
        return;
      const auto newMatId = m_terrainMaterial->itemData(index).toString().toStdString();
      if (region.value().materialAssetId != newMatId)
      {
        auto reg = region.value();
        reg.materialAssetId = newMatId;
        saveRegion(*group, reg, tr("Change Terrain Material"));
        updateSlotLabels(newMatId);
      }
    });
  connect(m_paint, &QPushButton::toggled, this, [this](const bool on) {
    if (m_toolBox)
      m_toolBox->setTerrainMode(
        on ? (m_editMode->currentIndex() == 2   ? TerrainToolMode::Vertices
              : m_editMode->currentIndex() == 1 ? TerrainToolMode::Sculpt
                                                : TerrainToolMode::Paint)
           : TerrainToolMode::None);
    syncToolButtons();
    refreshViews();
  });
  connect(m_drawNew, &QPushButton::toggled, this, [this](const bool on) {
    if (m_toolBox)
      m_toolBox->setTerrainMode(on ? TerrainToolMode::Draw : TerrainToolMode::None);
    syncToolButtons();
    refreshViews();
  });
  const auto selectionSettingsChanged = [this] {
    if (m_toolBox && m_toolBox->dragging())
      m_toolBox->cancelMouseDrag();
    cancelVertexInteraction();
    updateModeControls();
    refreshViews();
  };
  connect(m_softSelection, &QCheckBox::toggled, this, selectionSettingsChanged);
  connect(m_selectOccluded, &QCheckBox::toggled, this, selectionSettingsChanged);
  connect(m_softRadius, &QSpinBox::valueChanged, this, selectionSettingsChanged);
  connect(m_softHardness, &QSpinBox::valueChanged, this, selectionSettingsChanged);
  const auto brushSettingsChanged = [this] {
    m_brushSettings[m_brushSettingsIndex] = {
      m_radius->value(), m_strength->value(), m_hardness->value()};
    refreshViews();
  };
  connect(m_radius, &QSpinBox::valueChanged, this, brushSettingsChanged);
  connect(m_strength, &QSpinBox::valueChanged, this, brushSettingsChanged);
  connect(m_hardness, &QSpinBox::valueChanged, this, brushSettingsChanged);
  connect(m_lockEdges, &QCheckBox::toggled, this, [this] {
    if (m_toolBox && m_toolBox->dragging())
      m_toolBox->cancelMouseDrag();
    cancelVertexInteraction();
    refresh();
    refreshViews();
  });
  m_refreshTimer = new QTimer{this};
  m_refreshTimer->setSingleShot(true);
  m_refreshTimer->setInterval(150);
  connect(m_refreshTimer, &QTimer::timeout, this, &TerrainInspector::refresh);
  m_sculptTimer = new QTimer{this};
  m_sculptTimer->setInterval(50);
  connect(m_sculptTimer, &QTimer::timeout, this, [this] {
    if (m_sculptStroke && m_previousPoint)
      sculptTo(*m_previousPoint);
  });
  m_connection +=
    m_document.selectionDidChangeNotifier.connect([this](const auto&) { refresh(); });
  m_connection += m_document.nodesDidChangeNotifier.connect(
    [this](const auto& nodes) { nodesChanged(nodes); });
  m_connection += m_document.nodesWillBeRemovedNotifier.connect(
    [this](const auto& nodes) { nodesChanged(nodes); });
  m_connection += m_document.nodesWereAddedNotifier.connect(
    [this](const auto& nodes) { nodesChanged(nodes); });
  m_connection +=
    m_document.nodeLockingDidChangeNotifier.connect([this](const auto&) { refresh(); });
  m_connection += m_document.nodeVisibilityDidChangeNotifier.connect(
    [this](const auto&) { refresh(); });
  m_connection += m_document.groupWasOpenedNotifier.connect([this] { refresh(); });
  m_connection += m_document.groupWasClosedNotifier.connect([this] { refresh(); });
  m_connection += m_document.documentWasLoadedNotifier.connect([this] { refresh(); });
  m_connection += m_document.map().entityDefinitionsDidChangeNotifier.connect([this] {
    refreshBrushEntityDefinitions();
    refresh();
  });
  toolBox.terrainTool().setInspector(this);
  const auto syncTerrainTool = [this,
                                terrain = &toolBox.terrainTool()](const Tool& tool) {
    if (&tool == terrain)
      syncToolButtons();
  };
  m_connection += toolBox.toolActivatedNotifier.connect(syncTerrainTool);
  m_connection += toolBox.toolDeactivatedNotifier.connect(syncTerrainTool);
  refresh();
}

TerrainInspector::~TerrainInspector()
{
  m_connection.disconnect();
  if (m_toolBox)
  {
    m_toolBox->setTerrainMode(TerrainToolMode::None);
    m_toolBox->terrainTool().setInspector(nullptr);
  }
}

bool TerrainInspector::canActivate(const TerrainToolMode mode) const
{
  switch (mode)
  {
  case TerrainToolMode::Draw:
    return m_drawNew->isEnabled();
  case TerrainToolMode::Paint:
    return m_canPaint;
  case TerrainToolMode::Sculpt:
  case TerrainToolMode::Vertices:
    return m_canEditHeightField;
  case TerrainToolMode::None:
    return false;
  }
  return false;
}

void TerrainInspector::openAssetLibrary()
{
  auto* window = new AssetLibraryWindow{this};
  if (auto project = m_app.projectFor(m_document))
    window->setProjectContext(project);
  window->show();
}

void TerrainInspector::updateSlotLabels(const std::string& materialAssetId)
{
  const auto selectedSlot = std::max(0, m_material->currentIndex());
  const auto restoreSlot = qScopeGuard([&] {
    m_material->setCurrentIndex(std::min(selectedSlot, m_material->count() - 1));
  });
  m_material->clear();
  if (materialAssetId.empty())
  {
    m_previewIdentity.clear();
    m_materialPreviewValid = true;
    m_material->addItems({tr("Slot 0: Grass"), tr("Slot 1: Dirt"), tr("Slot 2: Rock")});
    return;
  }
  auto project = m_app.projectFor(m_document);
  if (!project)
  {
    m_previewIdentity.clear();
    m_materialPreviewValid = false;
    m_material->addItems({tr("Slot 0"), tr("Slot 1"), tr("Slot 2")});
    return;
  }
  const auto content =
    project->store().readAsset(materialAssetId, project::ContentKind::Original);
  if (content.is_error())
  {
    m_previewIdentity.clear();
    m_materialPreviewValid = false;
    m_material->addItems({tr("Slot 0"), tr("Slot 1"), tr("Slot 2")});
    return;
  }
  const auto jsonDoc = QJsonDocument::fromJson(QByteArray{
    reinterpret_cast<const char*>(content.value().data()),
    qsizetype(content.value().size())});
  if (!jsonDoc.isObject())
  {
    m_previewIdentity.clear();
    m_materialPreviewValid = false;
    m_material->addItems({tr("Slot 0"), tr("Slot 1"), tr("Slot 2")});
    return;
  }
  const auto slotsArray = jsonDoc.object().value(QStringLiteral("slots")).toArray();
  const auto edges = project->store().dependencies(materialAssetId);
  auto previewIdentity = materialAssetId;
  const auto material = project->store().findAsset(materialAssetId);
  if (material.is_success() && material.value())
    previewIdentity += ":" + std::to_string(material.value()->revision);
  for (int i = 0; i < 3; ++i)
  {
    if (i < slotsArray.size() && slotsArray[i].isObject())
    {
      const auto obj = slotsArray[i].toObject();
      const auto sName = obj.value(QStringLiteral("name")).toString(tr("Slot %1").arg(i));
      auto sTex = QString{};
      if (edges.is_success())
      {
        const auto kind = "terrain-slot-" + std::to_string(i);
        const auto edge = std::ranges::find_if(
          edges.value(), [&](const auto& candidate) { return candidate.kind == kind; });
        if (edge != edges.value().end())
        {
          const auto texture = project->store().findAsset(edge->dependencyId);
          if (texture.is_success() && texture.value())
          {
            sTex = QString::fromStdString(texture.value()->displayName);
            previewIdentity +=
              ":" + edge->dependencyId + ":" + std::to_string(texture.value()->revision);
          }
        }
      }
      m_material->addItem(
        sTex.isEmpty() ? sName : QStringLiteral("%1 (%2)").arg(sName, sTex));
    }
    else
    {
      m_material->addItem(tr("Slot %1").arg(i));
    }
  }
  if (m_previewIdentity != previewIdentity)
  {
    m_previewIdentity = std::move(previewIdentity);
    const auto images = loadTerrainMaterialImages(project->store(), materialAssetId);
    m_materialPreviewValid = images.is_success();
  }
}

void TerrainInspector::refreshBrushEntityDefinitions()
{
  const auto blocker = QSignalBlocker{m_brushEntityClass};
  const auto previous = m_brushEntityClass->currentText();
  auto names = QStringList{};
  for (const auto& definition : m_document.map().entityDefinitionManager().definitions())
    if (
      mdl::getType(definition) == mdl::EntityDefinitionType::Brush
      && definition.name != "worldspawn")
      names.push_back(QString::fromStdString(definition.name));
  names.sort();
  m_brushEntityClass->clear();
  m_brushEntityClass->addItems(names);
  auto index = m_brushEntityClass->findText(previous);
  if (index < 0)
    index = m_brushEntityClass->findText(QStringLiteral("func_detail"));
  m_brushEntityClass->setCurrentIndex(index >= 0 ? index : 0);
}

void TerrainInspector::updateModeControls()
{
  const auto sculpt = m_editMode->currentIndex() == 1;
  const auto vertices = m_editMode->currentIndex() == 2;
  const auto material = m_editMode->currentIndex() == 0;
  const auto* group = selectedTerrainGroup(m_document.map());
  const auto region = group ? readTerrainGroup(*group)
                            : Result<TerrainRegion>{Error{"No terrain selected."}};
  const auto hasHeightField =
    region.is_success() && region.value().heightField.has_value();
  const auto drawing = sculpt && m_drawNew->isChecked();
  const auto soft = vertices && m_softSelection->isChecked();
  m_form->setRowVisible(m_softSelection, vertices);
  m_form->setRowVisible(m_selectOccluded, vertices && !soft);
  m_form->setRowVisible(m_softRadius, soft);
  m_form->setRowVisible(m_softHardness, soft);
  m_softSelection->setEnabled(m_canEditHeightField);
  m_selectOccluded->setEnabled(m_canEditHeightField);
  m_softRadius->setEnabled(m_canEditHeightField);
  m_softHardness->setEnabled(m_canEditHeightField);
  m_form->setRowVisible(m_sculptMode, sculpt && !drawing);
  m_form->setRowVisible(m_lockEdges, (sculpt && !drawing) || vertices);
  m_compileForm->setRowVisible(m_brushEntityClass, drawing || hasHeightField);
  m_compileForm->setRowVisible(m_brushEntity, drawing || hasHeightField);
  m_brushEntity->setEnabled(
    m_brushEntityClass->count() > 0 && (drawing || m_canEditHeightField));
  m_brushEntityClass->setEnabled(
    m_brushEntity->isEnabled() && m_brushEntity->isChecked());
  m_form->setRowVisible(m_terrainMaterial, material);
  m_form->setRowVisible(m_material, material);
  m_form->setRowVisible(m_radius, !vertices && !drawing);
  m_form->setRowVisible(m_strength, !vertices && !drawing);
  m_form->setRowVisible(m_hardness, !vertices && !drawing);
  m_create->setVisible(!group || group->group().terrainPaint().empty());
  m_convert->setVisible(!hasHeightField && !drawing);
  m_paint->setVisible(!drawing);
  m_openLibrary->setVisible(material);
  m_paint->setText(
    m_paint->isChecked() ? tr("Finish Editing")
    : vertices           ? tr("Edit Terrain Vertices")
    : sculpt             ? tr("Sculpt Terrain")
                         : tr("Paint Terrain"));
  m_drawNew->setText(drawing ? tr("Finish Drawing") : tr("Draw New Terrain"));
  updateToolHelp();
}

void TerrainInspector::updateBrushSettings()
{
  // Vertex editing has separate selection controls and does not replace the last brush.
  if (m_editMode->currentIndex() == 2)
    return;
  const auto index = m_editMode->currentIndex() == 0
                       ? size_t{0}
                       : size_t(m_sculptMode->currentIndex() + 1);
  m_brushSettingsIndex = index;
  const auto& settings = m_brushSettings[index];
  const auto radiusBlocker = QSignalBlocker{m_radius};
  const auto strengthBlocker = QSignalBlocker{m_strength};
  const auto hardnessBlocker = QSignalBlocker{m_hardness};
  m_radius->setValue(settings.radius);
  m_strength->setValue(settings.strength);
  m_hardness->setValue(settings.hardness);
}

void TerrainInspector::updateToolHelp()
{
  if (drawingTerrain())
    m_help->setText(tr("Drag a rectangle in the top (XY) view to create terrain."));
  else if (!m_paint->isChecked())
    m_help->setText(tr("Choose an edit mode, then start editing the selected terrain."));
  else if (m_editMode->currentIndex() == 2)
    m_help->setText(
      m_softSelection->isChecked()
        ? tr("Drag the terrain surface to move vertices within the sphere. Orange "
             "shows the influence; Esc cancels the drag.")
        : tr("Click or box-select vertices, then drag along the height direction. "
             "Hold Ctrl or Shift to add to the selection."));
  else if (m_editMode->currentIndex() == 1)
    m_help->setText(tr("Sculpt: %1. Drag in the 3D view; Esc cancels the stroke.")
                      .arg(m_sculptMode->currentText()));
  else
    m_help->setText(
      tr("Paint the selected slot in the 3D view; Esc cancels the stroke."));
}

void TerrainInspector::invalidateAnalysis()
{
  if (m_cutPreviewGroup)
    m_estimate->setText(tr("Terrain changed. Analyze again to update the statistics."));
  m_cutPreview.clear();
  m_cutPreviewGroup.reset();
  m_showCuts->setChecked(false);
  m_showCuts->setEnabled(false);
}

void TerrainInspector::refresh()
{
  m_refreshTimer->stop();
  const auto validateTool = qScopeGuard([this] {
    if (
      m_toolBox && m_toolBox->terrainToolActive()
      && !canActivate(m_toolBox->terrainTool().mode()))
      m_toolBox->setTerrainMode(TerrainToolMode::None);
  });
  m_canPaint = false;
  m_canEditHeightField = false;
  render::RenderTiming::count("terrain.inspector_refreshes", 1);
  const auto& map = m_document.map();
  const auto supported = m_app.projectFor(m_document)
                         && map.gameInfo().gameConfig.name == "Half-Life"
                         && map.worldNode().mapFormat() == mdl::MapFormat::Valve;
  const auto* group = supported ? selectedTerrainGroup(map) : nullptr;
  if (!group || group->persistentId() != m_vertexSelectionGroup)
  {
    cancelVertexInteraction();
    m_selectedVertices.clear();
    m_vertexSelectionGroup = group ? group->persistentId() : std::nullopt;
  }
  if (m_sculptStroke && (!group || group->persistentId() != m_strokeGroup))
    endStroke(false);
  const auto groupId = group ? group->persistentId() : std::nullopt;
  const auto revision = group ? terrainGroupRevision(*group) : std::string{};
  if (m_refreshedGroup != groupId || m_refreshedPaint != revision)
  {
    invalidateAnalysis();
    if (m_refreshedGroup != groupId)
      m_estimate->clear();
  }
  m_refreshedGroup = groupId;
  m_refreshedPaint = revision;
  const auto hasRegion = group && !group->group().terrainPaint().empty();
  const auto editable = group && terrainEditable(map, *group);
  const auto& selection = map.selection();
  const auto selectedBrushes =
    !selection.brushes.empty() && selection.brushes.size() == selection.nodes.size();
  if (!supported && m_toolBox)
    m_toolBox->setTerrainMode(TerrainToolMode::None);
  m_editMode->setEnabled(supported);
  m_drawNew->setEnabled(supported);
  m_convert->setEnabled(supported && (editable || selectedBrushes));
  m_create->setEnabled(editable && !hasRegion);
  m_generate->setEnabled(hasRegion);
  m_paint->setEnabled(hasRegion && editable);
  m_material->setEnabled(hasRegion && editable);
  m_terrainMaterial->setEnabled(hasRegion && editable);
  m_radius->setEnabled(hasRegion && editable);
  m_strength->setEnabled(hasRegion && editable);
  m_hardness->setEnabled(hasRegion && editable);
  m_faceBudget->setEnabled(hasRegion && editable);
  m_wadBudget->setEnabled(hasRegion && editable);
  m_minFaceWidth->setEnabled(hasRegion && editable);
  m_patchDensity->setEnabled(hasRegion && editable);
  m_analyze->setEnabled(hasRegion);
  if (!hasRegion)
    m_estimate->clear();
  else if (m_estimate->text().isEmpty())
    m_estimate->setText(tr("Analyze to preview generated terrain statistics."));
  m_openLibrary->setEnabled(supported);
  updateModeControls();

  m_refreshingMaterial = true;
  m_terrainMaterial->clear();
  m_terrainMaterial->addItem(tr("(Default 3-Blend)"), QString{});
  if (auto project = m_app.projectFor(m_document))
  {
    const auto assets = project->store().assets();
    if (assets.is_success())
    {
      for (const auto& asset : assets.value())
      {
        if (asset.type == project::AssetType::Material)
          m_terrainMaterial->addItem(
            QString::fromStdString(asset.displayName), QString::fromStdString(asset.id));
      }
    }
  }

  if (!hasRegion)
  {
    m_paint->setChecked(false);
    updateSlotLabels({});
    m_refreshingMaterial = false;
    if (m_drawNew->isChecked())
      m_status->setText(tr("Drag a rectangle in the top (XY) view to create terrain."));
    else
      m_status->setText(
        supported ? tr("Select ground brushes or a brush group to create terrain.")
                  : tr("Open a Half-Life Valve 220 map in a project to edit terrain."));
    return;
  }
  const auto region = readTerrainGroup(*group);
  if (region.is_error())
  {
    m_status->setText(terrainErrorMessage(std::get<Error>(region.error())));
    m_paint->setChecked(false);
    m_paint->setEnabled(false);
    m_generate->setEnabled(false);
    m_analyze->setEnabled(false);
    m_refreshingMaterial = false;
    return;
  }

  m_canPaint = editable;
  m_canEditHeightField = editable && region.value().heightField.has_value();
  updateModeControls();
  if (m_editMode->currentIndex() != 0 && !region.value().heightField)
    m_paint->setEnabled(false);
  if (!editable)
    m_paint->setChecked(false);

  {
    const auto facesBlocker = QSignalBlocker{m_faceBudget};
    const auto wadBlocker = QSignalBlocker{m_wadBudget};
    const auto widthBlocker = QSignalBlocker{m_minFaceWidth};
    const auto densityBlocker = QSignalBlocker{m_patchDensity};
    m_faceBudget->setValue(region.value().maxAddedFaces);
    m_wadBudget->setValue(region.value().maxWadKiB);
    m_minFaceWidth->setValue(region.value().minFaceWidth);
    m_patchDensity->setCurrentIndex(
      m_patchDensity->findData(region.value().patchDensityPercent));
    if (!drawingTerrain())
    {
      const auto entityBlocker = QSignalBlocker{m_brushEntity};
      const auto classBlocker = QSignalBlocker{m_brushEntityClass};
      const auto& classname = region.value().brushEntityClass;
      m_brushEntity->setChecked(!classname.empty());
      if (!classname.empty())
        m_brushEntityClass->setCurrentIndex(
          m_brushEntityClass->findText(QString::fromStdString(classname)));
      m_brushEntityClass->setEnabled(
        m_brushEntity->isEnabled() && m_brushEntity->isChecked());
    }
  }

  const auto targetMatId = QString::fromStdString(region.value().materialAssetId);
  const auto foundIndex = m_terrainMaterial->findData(targetMatId);
  if (foundIndex != -1)
    m_terrainMaterial->setCurrentIndex(foundIndex);
  else
  {
    m_terrainMaterial->addItem(tr("Missing material (%1)").arg(targetMatId), targetMatId);
    m_terrainMaterial->setCurrentIndex(m_terrainMaterial->count() - 1);
  }
  m_refreshingMaterial = false;

  const auto previousPreview = m_previewIdentity;
  updateSlotLabels(region.value().materialAssetId);
  if (previousPreview != m_previewIdentity || !m_materialPreviewValid)
    invalidateAnalysis();

  if (foundIndex == -1 || !m_materialPreviewValid)
  {
    m_status->setText(
      tr("The bound terrain material or one of its textures is missing or invalid."));
    m_generate->setEnabled(false);
    m_analyze->setEnabled(false);
    return;
  }

  const auto surfaces = terrainSurfaceCount(*group, region.value());
  if (surfaces.is_error())
  {
    m_status->setText(terrainErrorMessage(std::get<Error>(surfaces.error())));
    m_paint->setChecked(false);
    m_paint->setEnabled(false);
    m_generate->setEnabled(false);
    m_analyze->setEnabled(false);
    return;
  }
  if (!editable)
  {
    m_status->setText(tr("The terrain or one of its brushes is locked or hidden."));
    return;
  }
  if (
    m_editMode->currentIndex() != 0 && !m_drawNew->isChecked() && m_lockEdges->isChecked()
    && region.value().heightField
    && (region.value().heightField->columns < 2 || region.value().heightField->rows < 2))
  {
    m_status->setText(
      tr("All vertices are on locked edges; unlock the edges to edit terrain."));
    return;
  }
  if (m_drawNew->isChecked())
    m_status->setText(tr("Drag a rectangle in the top (XY) view to create terrain."));
  else
    m_status->setText(tr("%1 · %2 source top faces")
                        .arg(QString::fromStdString(group->name()))
                        .arg(surfaces.value()));
}

void TerrainInspector::nodesChanged(const std::vector<mdl::Node*>& nodes)
{
  if (m_sculptApplying)
    return;
  const auto* group = selectedTerrainGroup(m_document.map());
  if (!group || !std::ranges::any_of(nodes, [&](const auto* node) {
        return node == group || group->isAncestorOf(*node) || node->isAncestorOf(*group);
      }))
    return;
  if (
    group->persistentId() != m_refreshedGroup
    || terrainGroupRevision(*group) != m_refreshedPaint)
  {
    refresh();
    return;
  }
  // Geometry changes can arrive for every mouse movement. Refresh validation and
  // material controls once after the edit settles, not during each drag event.
  invalidateAnalysis();
  m_refreshTimer->start();
}

void TerrainInspector::analyze()
{
  m_cutPreview.clear();
  m_cutPreviewGroup.reset();
  m_showCuts->setChecked(false);
  m_showCuts->setEnabled(false);
  const auto* group = selectedTerrainGroup(m_document.map());
  if (!group)
    return;
  const auto project = m_app.projectFor(m_document);
  auto estimate = estimateTerrainGroup(*group, project ? &project->store() : nullptr);
  if (estimate.is_error())
  {
    m_estimate->setText(terrainErrorMessage(std::get<Error>(estimate.error())));
    return;
  }
  const auto& stats = estimate.value();
  const auto added = stats.generatedFaces > stats.sourceFaces
                       ? stats.generatedFaces - stats.sourceFaces
                       : size_t{0};
  auto text =
    tr("Source: %1 · Generated: %2 · Added: %3\nTerrain textures: %4 · WAD: %5 KiB")
      .arg(stats.sourceFaces)
      .arg(stats.generatedFaces)
      .arg(added)
      .arg(stats.textures)
      .arg(QString::number(double(stats.wadBytes) / 1024.0, 'f', 1));
  if (stats.generatedFaces > 0)
    text += tr("\nNarrowest top face: %1 units near (%2, %3)")
              .arg(QString::number(stats.narrowestFaceWidth, 'f', 1))
              .arg(QString::number(stats.narrowestFaceX, 'f', 1))
              .arg(QString::number(stats.narrowestFaceY, 'f', 1));
  const auto countKind = [&](const TerrainFaceKind kind) {
    return std::ranges::count_if(
      stats.cuts, [kind](const auto& cut) { return cut.kind == kind; });
  };
  text += tr("\nBase: %1 · Shared: %2 · Local: %3")
            .arg(countKind(TerrainFaceKind::Base))
            .arg(countKind(TerrainFaceKind::Shared))
            .arg(countKind(TerrainFaceKind::Local));
  if (isTerrainChunkGroup(*group))
    text += tr("\nChunk texture and WAD totals are estimated before deduplication.");
  if (stats.faceBudgetExceeded)
    text += tr("\nExtra top faces exceed the configured limit.");
  if (stats.wadBudgetExceeded)
    text += tr("\nTerrain WAD exceeds the configured limit.");
  if (stats.facesBelowMinWidth > 0)
    text += tr("\n%1 top faces are below the configured minimum width.")
              .arg(stats.facesBelowMinWidth);
  m_estimate->setText(text);
  m_cutPreviewGroup = group->persistentId();
  m_cutPreview = std::move(estimate).value().cuts;
  m_showCuts->setEnabled(!m_cutPreview.empty());
  m_showCuts->setChecked(!m_cutPreview.empty());
}

void TerrainInspector::syncToolButtons()
{
  const auto mode = m_toolBox && m_toolBox->terrainToolActive()
                      ? m_toolBox->terrainTool().mode()
                      : TerrainToolMode::None;
  const auto modeBlocker = QSignalBlocker{m_editMode};
  if (mode == TerrainToolMode::Paint)
    m_editMode->setCurrentIndex(0);
  else if (mode == TerrainToolMode::Sculpt || mode == TerrainToolMode::Draw)
    m_editMode->setCurrentIndex(1);
  else if (mode == TerrainToolMode::Vertices)
    m_editMode->setCurrentIndex(2);
  const auto paintBlocker = QSignalBlocker{m_paint};
  const auto drawBlocker = QSignalBlocker{m_drawNew};
  m_paint->setChecked(
    mode == TerrainToolMode::Paint || mode == TerrainToolMode::Sculpt
    || mode == TerrainToolMode::Vertices);
  m_drawNew->setChecked(mode == TerrainToolMode::Draw);
  if (mode == TerrainToolMode::Draw)
    m_creation->expand();
  m_paint->setEnabled(
    m_editMode->currentIndex() == 0 ? m_canPaint : m_canEditHeightField);
  updateBrushSettings();
  updateModeControls();
}

void TerrainInspector::refreshViews()
{
  if (m_toolBox)
    m_toolBox->terrainTool().refreshViews();
}

bool TerrainInspector::saveRegion(
  mdl::GroupNode& group, const TerrainRegion& region, const QString& label)
{
  if (!terrainEditable(m_document.map(), group))
    return false;
  if (isTerrainChunkGroup(group))
  {
    auto decoded = readTerrainChunks(group);
    if (decoded.is_error())
      return false;
    auto chunks = std::move(decoded).value().chunks;
    for (auto& chunk : chunks)
    {
      chunk.region.materialAssetId = region.materialAssetId;
      chunk.region.brushEntityClass = region.brushEntityClass;
      chunk.region.maxAddedFaces = region.maxAddedFaces;
      chunk.region.maxWadKiB = region.maxWadKiB;
      chunk.region.minFaceWidth = region.minFaceWidth;
      chunk.region.patchDensityPercent = region.patchDensityPercent;
    }
    return saveChunkRegions(group, chunks, label);
  }
  auto content = group.group();
  const auto encoded = region.encode();
  if (content.terrainPaint() == encoded)
    return true;
  content.setTerrainPaint(encoded);
  auto& map = m_document.map();
  const auto name = label.toStdString();
  // Keep each finished stroke as its own undo step.
  map.startTransaction(name, mdl::TransactionScope::LongRunning);
  if (!mdl::updateNodeContents(
        map, name, {{&group, mdl::NodeContents{std::move(content)}}}))
  {
    map.cancelTransaction();
    return false;
  }
  return map.commitTransaction();
}

bool TerrainInspector::saveChunkRegions(
  mdl::GroupNode& group, const std::vector<TerrainChunk>& chunks, const QString& label)
{
  if (
    !terrainEditable(m_document.map(), group) || !isTerrainChunkGroup(group)
    || chunks.size() != group.children().size())
    return false;
  auto changes = std::vector<std::pair<mdl::Node*, mdl::NodeContents>>{};
  for (const auto& chunk : chunks)
  {
    if (chunk.childIndex >= group.children().size())
      return false;
    auto* child = dynamic_cast<mdl::GroupNode*>(group.children()[chunk.childIndex]);
    if (!child)
      return false;
    auto content = child->group();
    const auto encoded = chunk.region.encode();
    if (encoded != content.terrainPaint())
    {
      content.setTerrainPaint(encoded);
      changes.emplace_back(child, mdl::NodeContents{std::move(content)});
    }
  }
  if (changes.empty())
    return true;
  auto& map = m_document.map();
  const auto name = label.toStdString();
  map.startTransaction(name, mdl::TransactionScope::LongRunning);
  if (!mdl::updateNodeContents(map, name, std::move(changes)))
  {
    map.cancelTransaction();
    return false;
  }
  return map.commitTransaction();
}

void TerrainInspector::createRegion()
{
  if (auto* group = selectedTerrainGroup(m_document.map()))
  {
    const auto region = createTerrainRegion(*group);
    if (region.is_error())
      m_status->setText(terrainErrorMessage(std::get<Error>(region.error())));
    else if (!saveRegion(*group, region.value(), tr("Create Terrain Region")))
      m_status->setText(tr("Could not create the terrain region."));
  }
}

void TerrainInspector::convertSelection()
{
  const auto result = convertSelectedTerrainToHeightField(
    m_document.map(), m_gridSpacing->currentData().toDouble());
  if (result.is_error())
    m_status->setText(terrainErrorMessage(std::get<Error>(result.error())));
  else
    refresh();
}

bool TerrainInspector::painting() const
{
  return m_toolBox && m_toolBox->terrainToolActive()
    && (m_toolBox->terrainTool().mode() == TerrainToolMode::Paint
      || m_toolBox->terrainTool().mode() == TerrainToolMode::Sculpt);
}

bool TerrainInspector::vertexEditing() const
{
  return m_toolBox && m_toolBox->terrainToolActive()
         && m_toolBox->terrainTool().mode() == TerrainToolMode::Vertices;
}

bool TerrainInspector::vertexInteracting() const
{
  return m_vertexSelecting || m_vertexDragging;
}

const TerrainRegion* TerrainInspector::vertexRegion(const mdl::GroupNode& group) const
{
  if (m_vertexDragging && m_stroke && group.persistentId() == m_strokeGroup)
    return &*m_stroke;
  const auto encoded = terrainGroupRevision(group);
  if (m_vertexCachedGroup != group.persistentId() || m_vertexCachedPaint != encoded)
  {
    m_vertexCachedGroup = group.persistentId();
    m_vertexCachedPaint = encoded;
    render::RenderTiming::count("terrain.region_decodes", 1);
    auto decoded = readTerrainGroup(group);
    m_vertexCachedRegion = decoded.is_success()
                             ? std::make_unique<TerrainRegion>(std::move(decoded).value())
                             : nullptr;
  }
  return m_vertexCachedRegion.get();
}

bool TerrainInspector::vertexVisible(
  const gl::Camera& camera, const vm::vec3d& position) const
{
  if (camera.orthographicProjection() || m_selectOccluded->isChecked())
    return true;
  const auto ray = vm::ray3d{camera.pickRay(vm::vec3f{position})};
  auto pick = mdl::PickResult::byDistance();
  mdl::pick(m_document.map(), ray, pick);
  const auto& hit = pick.first(mdl::HitFilters::type(mdl::BrushNode::BrushHitType));
  return !hit.isMatch() || hit.distance() + 0.5 >= vm::length(position - ray.origin);
}

std::optional<size_t> TerrainInspector::nearestVertex(
  const gl::Camera& camera,
  const TerrainRegion& region,
  const mdl::GroupNode& group,
  const QPointF& point) const
{
  const auto& field = *region.heightField;
  auto nearest = std::optional<size_t>{};
  auto nearestDistance = 8.0 * 8.0;
  auto nearestDepth = 1.0f;
  for (auto i = size_t{0}; i < field.heights.size(); ++i)
  {
    if (m_lockEdges->isChecked() && terrainEdgeVertex(field, i))
      continue;
    const auto position = terrainVertex(region, group, i);
    const auto screen = terrainVertexScreenPosition(camera, position);
    if (!screen)
      continue;
    const auto delta = *screen - point;
    const auto distance = delta.x() * delta.x() + delta.y() * delta.y();
    const auto depth = camera.project(vm::vec3f{position}).z();
    if (
      distance < nearestDistance || (distance == nearestDistance && depth < nearestDepth))
    {
      if (!vertexVisible(camera, position))
        continue;
      nearest = i;
      nearestDistance = distance;
      nearestDepth = depth;
    }
  }

  return nearest;
}

std::optional<vm::vec3d> TerrainInspector::softSelectionCenter(
  const gl::Camera& camera,
  const TerrainRegion& region,
  const mdl::GroupNode& group,
  const mdl::PickResult& pick,
  const QPointF& point) const
{
  const auto& hit = pick.first(mdl::HitFilters::type(mdl::BrushNode::BrushHitType));
  const auto face = mdl::hitToFaceHandle(hit);
  if (face && terrainOwnsBrush(group, face->node()) && face->face().normal().z() > 1e-6)
    return hit.hitPoint();
  if (camera.orthographicProjection())
    if (const auto nearest = nearestVertex(camera, region, group, point))
      return terrainVertex(region, group, *nearest);
  return std::nullopt;
}

void TerrainInspector::beginVertexInteraction(
  const gl::Camera& camera,
  QWidget& view,
  const QPointF& point,
  const bool additive,
  const mdl::PickResult& pick)
{
  if (!vertexEditing() || vertexInteracting())
    return;
  auto& map = m_document.map();
  const auto* group = selectedTerrainGroup(map);
  if (!group || !terrainEditable(map, *group))
    return;
  const auto* region = vertexRegion(*group);
  if (!region || !region->heightField)
    return;
  const auto& field = *region->heightField;
  const auto nearest = m_softSelection->isChecked()
                         ? std::optional<size_t>{}
                         : nearestVertex(camera, *region, *group, point);
  const auto softCenter = m_softSelection->isChecked()
                            ? softSelectionCenter(camera, *region, *group, pick, point)
                            : std::optional<vm::vec3d>{};
  if (m_softSelection->isChecked() && !softCenter)
    return;

  m_vertexStart = point;
  m_vertexAdditive = additive;
  if (!nearest && !softCenter)
  {
    m_vertexSelecting = true;
    if (!m_vertexBox || m_vertexBox->parentWidget() != &view)
    {
      delete m_vertexBox.data();
      m_vertexBox = new QRubberBand{QRubberBand::Rectangle, &view};
    }
    m_vertexBox->setGeometry(QRect{point.toPoint(), point.toPoint()});
    m_vertexBox->show();
    return;
  }

  if (nearest && !m_selectedVertices.contains(*nearest))
  {
    if (!additive)
      m_selectedVertices.clear();
    m_selectedVertices.insert(*nearest);
  }
  const auto world = softCenter ? *softCenter : terrainVertex(*region, *group, *nearest);
  m_vertexDragWeights.assign(field.heights.size(), 0.0);
  if (softCenter)
    m_vertexDragWeights = field.softSelectionWeights(
      *softCenter,
      region->origin,
      group->group().transformation(),
      double(m_softRadius->value()),
      double(m_softHardness->value()) / 100.0,
      m_lockEdges->isChecked());
  else
    for (const auto i : m_selectedVertices)
      if (
        i < m_vertexDragWeights.size()
        && (!m_lockEdges->isChecked() || !terrainEdgeVertex(field, i)))
        m_vertexDragWeights[i] = 1.0;
  if (std::ranges::none_of(
        m_vertexDragWeights, [](const auto weight) { return weight > 0.0; }))
    return;
  const auto& transform = group->group().transformation();
  const auto vertical = transform * vm::vec3d{0, 0, 1} - transform * vm::vec3d{0, 0, 0};
  const auto projected = camera.project(vm::vec3f{world});
  const auto projectedUp = camera.project(vm::vec3f{world + vertical * 64.0});
  const auto axis = QPointF{
    double(projectedUp.x() - projected.x()), double(projected.y() - projectedUp.y())};
  const auto pixelsPerUnit = std::hypot(axis.x(), axis.y()) / 64.0;
  if (pixelsPerUnit > 0.01)
  {
    m_vertexScreenAxis = axis / (pixelsPerUnit * 64.0);
    m_vertexUnitsPerPixel = 1.0 / pixelsPerUnit;
  }
  else
  {
    const auto a = camera.unproject(float(point.x()), float(point.y()), projected.z());
    const auto b =
      camera.unproject(float(point.x()), float(point.y() + 1), projected.z());
    m_vertexScreenAxis = QPointF{0.0, -1.0};
    m_vertexUnitsPerPixel = double(vm::length(b - a)) / vm::length(vertical);
  }
  if (!std::isfinite(m_vertexUnitsPerPixel))
    return;
  m_vertexSoftCenter = softCenter;
  m_vertexHeightDelta = 0.0;

  m_vertexOriginalHeights = field.heights;
  m_stroke = *region;
  m_strokeGroup = group->persistentId();
  m_sculptStroke = true;
  m_vertexDragging = true;
  map.startTransaction(
    tr("Move Terrain Vertices").toStdString(), mdl::TransactionScope::LongRunning);
  refreshViews();
}

void TerrainInspector::moveVertexInteraction(const QPointF& point)
{
  if (m_vertexSelecting)
  {
    if (m_vertexBox)
      m_vertexBox->setGeometry(
        QRect{m_vertexStart.toPoint(), point.toPoint()}.normalized());
    return;
  }
  if (!m_vertexDragging || !m_stroke || !m_stroke->heightField)
    return;
  auto& map = m_document.map();
  auto* group = selectedTerrainGroup(map);
  if (!group || group->persistentId() != m_strokeGroup || !terrainEditable(map, *group))
  {
    cancelVertexInteraction();
    return;
  }
  auto field = *m_stroke->heightField;
  const auto displacement = point - m_vertexStart;
  const auto delta = map.grid().snap(
    (displacement.x() * m_vertexScreenAxis.x()
     + displacement.y() * m_vertexScreenAxis.y())
    * m_vertexUnitsPerPixel);
  m_vertexHeightDelta = delta;
  for (auto i = size_t{0}; i < field.heights.size(); ++i)
    if (m_vertexDragWeights[i] > 0.0)
      field.heights[i] = std::max(
        field.bottom + 1.0, m_vertexOriginalHeights[i] + delta * m_vertexDragWeights[i]);
  if (field.heights == m_stroke->heightField->heights)
    return;
  m_sculptApplying = true;
  const auto result = updateTerrainHeightField(map, *group, field);
  m_sculptApplying = false;
  if (result.is_error())
  {
    const auto message = terrainErrorMessage(std::get<Error>(result.error()));
    cancelVertexInteraction();
    m_status->setText(message);
    return;
  }
  m_stroke->heightField = std::move(field);
  invalidateAnalysis();
  m_refreshedPaint = terrainGroupRevision(*group);
  refreshViews();
}

void TerrainInspector::endVertexInteraction(
  const gl::Camera& camera, const QPointF& point, const bool commit)
{
  if (!commit)
  {
    cancelVertexInteraction();
    return;
  }
  if (m_vertexDragging)
  {
    moveVertexInteraction(point);
    if (m_vertexDragging)
    {
      m_vertexDragging = false;
      m_vertexOriginalHeights.clear();
      m_vertexDragWeights.clear();
      m_vertexSoftCenter.reset();
      endStroke(true);
    }
    return;
  }
  if (!m_vertexSelecting)
    return;
  m_vertexSelecting = false;
  if (m_vertexBox)
    m_vertexBox->hide();
  const auto* group = selectedTerrainGroup(m_document.map());
  if (!group || !terrainEditable(m_document.map(), *group))
    return;
  const auto* region = vertexRegion(*group);
  if (!region || !region->heightField)
    return;
  if (!m_vertexAdditive)
    m_selectedVertices.clear();
  const auto& field = *region->heightField;
  const auto rectangle = QRectF{m_vertexStart, point}.normalized();
  for (auto i = size_t{0}; i < field.heights.size(); ++i)
  {
    if (m_lockEdges->isChecked() && terrainEdgeVertex(field, i))
      continue;
    const auto screen =
      terrainVertexScreenPosition(camera, terrainVertex(*region, *group, i));
    if (
      screen && rectangle.contains(*screen)
      && vertexVisible(camera, terrainVertex(*region, *group, i)))
      m_selectedVertices.insert(i);
  }
  refreshViews();
}

void TerrainInspector::cancelVertexInteraction()
{
  m_vertexSelecting = false;
  m_vertexDragWeights.clear();
  m_vertexSoftCenter.reset();
  if (m_vertexBox)
    m_vertexBox->hide();
  if (std::exchange(m_vertexDragging, false))
  {
    m_vertexOriginalHeights.clear();
    endStroke(false);
  }
}

void TerrainInspector::renderVertices(
  render::RenderContext& context,
  render::RenderBatch& batch,
  const mdl::PickResult& pick,
  const QPointF& point) const
{
  if (!vertexEditing())
    return;
  const auto* group = selectedTerrainGroup(m_document.map());
  if (!group)
    return;
  const auto* region = vertexRegion(*group);
  if (!region || !region->heightField)
    return;
  const auto& field = *region->heightField;
  const auto soft = m_softSelection->isChecked();
  auto center =
    soft ? (m_vertexDragging
              ? m_vertexSoftCenter
              : softSelectionCenter(context.camera(), *region, *group, pick, point))
         : std::optional<vm::vec3d>{};
  auto previewWeights = std::vector<double>{};
  if (center && !m_vertexDragging)
    previewWeights = field.softSelectionWeights(
      *center,
      region->origin,
      group->group().transformation(),
      double(m_softRadius->value()),
      double(m_softHardness->value()) / 100.0,
      m_lockEdges->isChecked());
  const auto& weights = m_vertexDragging ? m_vertexDragWeights : previewWeights;
  auto handles = std::array<std::vector<vm::vec3f>, 5>{};
  for (auto i = size_t{0}; i < field.heights.size(); ++i)
  {
    if (m_lockEdges->isChecked() && terrainEdgeVertex(field, i))
      continue;
    const auto weight = soft ? (i < weights.size() ? weights[i] : 0.0)
                             : (m_selectedVertices.contains(i) ? 1.0 : 0.0);
    const auto bucket = weight > 0.0 ? size_t(std::ceil(weight * 4.0)) : size_t{0};
    handles[bucket].push_back(vm::vec3f{terrainVertex(*region, *group, i)});
  }
  auto renderer = render::RenderService{context, batch};
  if (soft || m_selectOccluded->isChecked() || context.camera().orthographicProjection())
    renderer.setShowOccludedObjectsTransparent();
  else
    renderer.setHideOccludedObjects();
  for (auto bucket = size_t{0}; bucket < handles.size(); ++bucket)
  {
    const auto weight = float(bucket) / 4.0f;
    renderer.setForegroundColor(
      bucket == 0 ? RgbaF{0.9f, 0.9f, 0.9f, 1.0f}
                  : RgbaF{1.0f, 0.85f - 0.3f * weight, 0.55f - 0.45f * weight, 1.0f});
    renderer.renderHandles(handles[bucket]);
  }
  if (center)
  {
    if (m_vertexDragging)
    {
      const auto& transform = group->group().transformation();
      *center = *center + transform * vm::vec3d{0, 0, m_vertexHeightDelta}
                - transform * vm::vec3d{0, 0, 0};
    }
    renderer.setShowOccludedObjectsTransparent();
    renderer.setForegroundColor(RgbaF{1.0f, 0.55f, 0.1f, 1.0f});
    for (const auto axis : {vm::axis::x, vm::axis::y, vm::axis::z})
    {
      renderer.renderCircle(vm::vec3f{*center}, axis, 64, float(m_softRadius->value()));
      if (m_softHardness->value() > 0 && m_softHardness->value() < 100)
        renderer.renderCircle(
          vm::vec3f{*center},
          axis,
          64,
          float(m_softRadius->value()) * float(m_softHardness->value()) / 100.0f);
    }
  }
}

bool TerrainInspector::drawingTerrain() const
{
  return m_toolBox && m_toolBox->terrainToolActive()
         && m_toolBox->terrainTool().mode() == TerrainToolMode::Draw;
}

std::optional<vm::bbox3d> TerrainInspector::drawnTerrainBounds(
  const vm::vec2d& first, const vm::vec2d& second) const
{
  const auto spacing = m_gridSpacing->currentData().toDouble();
  const auto& grid = m_document.map().grid();
  const auto increment = grid.snap() ? std::max(spacing, grid.actualSize()) : spacing;
  const auto snap = [increment](const double value) {
    return std::round(value / increment) * increment;
  };
  const auto height = double(m_newHeight->value());
  const auto bounds = vm::bbox3d{
    {snap(std::min(first.x(), second.x())),
     snap(std::min(first.y(), second.y())),
     height - double(m_newThickness->value())},
    {snap(std::max(first.x(), second.x())),
     snap(std::max(first.y(), second.y())),
     height}};
  if (bounds.is_empty() || !m_document.map().worldBounds().contains(bounds))
    return std::nullopt;
  return bounds;
}

void TerrainInspector::renderDrawnTerrain(
  render::RenderContext& context,
  render::RenderBatch& batch,
  const vm::bbox3d& bounds) const
{
  auto renderer = render::RenderService{context, batch};
  renderer.setShowOccludedObjectsTransparent();
  renderer.setShowBackfaces();
  const auto spacing = m_gridSpacing->currentData().toDouble();
  const auto columns = int(std::lround(bounds.size().x() / spacing));
  const auto rows = int(std::lround(bounds.size().y() / spacing));
  const auto valid = double(columns) * double(rows) * 2.0 <= MaxTerrainBrushes;
  const auto color =
    valid ? pref(Preferences::SelectedEdgeColor) : Color{RgbaF{1, 0.2f, 0.2f, 1}};
  const auto a = vm::vec3f{vm::vec3d{bounds.min.x(), bounds.min.y(), bounds.max.z()}};
  const auto b = vm::vec3f{vm::vec3d{bounds.max.x(), bounds.min.y(), bounds.max.z()}};
  const auto c = vm::vec3f{bounds.max};
  const auto d = vm::vec3f{vm::vec3d{bounds.min.x(), bounds.max.y(), bounds.max.z()}};
  const auto down = vm::vec3f{0, 0, -float(bounds.size().z())};
  renderer.setForegroundColor(RgbaF{color.to<RgbF>(), 0.25f});
  renderer.renderFilledPolygon({a, b, c, d});
  renderer.renderFilledPolygon({a + down, b + down, b, a});
  renderer.renderFilledPolygon({b + down, c + down, c, b});
  renderer.renderFilledPolygon({c + down, d + down, d, c});
  renderer.renderFilledPolygon({d + down, a + down, a, d});
  renderer.setForegroundColor(color);
  renderer.renderBounds(vm::bbox3f{bounds});
  // Keep preview work bounded even when the proposed rectangle exceeds the brush budget.
  if (valid)
  {
    auto lines = std::vector<vm::vec3f>{};
    for (auto column = 0; column <= columns; ++column)
    {
      const auto x = float(bounds.min.x() + column * spacing);
      lines.insert(lines.end(), {{x, a.y(), a.z()}, {x, d.y(), a.z()}});
    }
    for (auto row = 0; row <= rows; ++row)
    {
      const auto y = float(bounds.min.y() + row * spacing);
      lines.insert(lines.end(), {{a.x(), y, a.z()}, {b.x(), y, a.z()}});
      if (row < rows)
        for (auto column = 0; column < columns; ++column)
        {
          const auto x = float(bounds.min.x() + column * spacing);
          lines.insert(
            lines.end(),
            {{x, y + float(spacing), a.z()}, {x + float(spacing), y, a.z()}});
        }
    }
    renderer.renderLines(lines);
  }
  auto dimensions = render::SelectionBoundsRenderer{bounds};
  dimensions.render(context, batch);
}

void TerrainInspector::createDrawnTerrain(const vm::vec2d& first, const vm::vec2d& second)
{
  if (!drawingTerrain())
    return;
  const auto spacing = m_gridSpacing->currentData().toDouble();
  const auto bounds = drawnTerrainBounds(first, second);
  if (!bounds)
    return;
  const auto start = vm::vec2d{bounds->min.x(), bounds->min.y()};
  const auto finish = vm::vec2d{bounds->max.x(), bounds->max.y()};
  const auto large =
    std::abs(finish.x() - start.x()) > TerrainRegion::Width * TerrainRegion::UnitsPerTexel
    || std::abs(finish.y() - start.y())
         > TerrainRegion::Height * TerrainRegion::UnitsPerTexel;
  const auto brushEntityClass =
    m_brushEntity->isChecked() && m_brushEntityClass->currentIndex() >= 0
      ? m_brushEntityClass->currentText().toStdString()
      : std::string{};
  const auto result = large ? createTerrainChunkRectangle(
                                m_document.map(),
                                start,
                                finish,
                                double(m_newHeight->value()),
                                double(m_newThickness->value()),
                                spacing,
                                brushEntityClass)
                            : createTerrainRectangle(
                                m_document.map(),
                                start,
                                finish,
                                double(m_newHeight->value()),
                                double(m_newThickness->value()),
                                spacing,
                                brushEntityClass);
  if (result.is_error())
    m_status->setText(terrainErrorMessage(std::get<Error>(result.error())));
  else
  {
    m_drawNew->setChecked(false);
    refresh();
    refreshViews();
  }
}

void TerrainInspector::cancelDrawing()
{
  m_drawNew->setChecked(false);
  refresh();
}

bool TerrainInspector::stroking() const
{
  return m_stroke.has_value();
}

size_t TerrainInspector::strokeRevision() const
{
  return m_strokeRevision;
}

const TerrainRegion* TerrainInspector::previewStroke(const mdl::GroupNode& group) const
{
  if (!m_stroke || m_sculptStroke)
    return nullptr;
  if (group.persistentId() == m_strokeGroup)
    return &*m_stroke;
  const auto* parent = dynamic_cast<const mdl::GroupNode*>(group.parent());
  if (!parent || parent->persistentId() != m_strokeGroup)
    return nullptr;
  for (const auto& chunk : m_chunkStroke)
    if (
      chunk.childIndex < parent->children().size()
      && parent->children()[chunk.childIndex] == &group)
      return &chunk.region;
  return nullptr;
}

void TerrainInspector::beginStroke(const mdl::PickResult& pick)
{
  if (m_stroke)
    endStroke(false);
  const auto* group = selectedTerrainGroup(m_document.map());
  if (!group || !terrainEditable(m_document.map(), *group))
    return;
  auto region = readTerrainGroup(*group);
  if (region.is_error())
    return;
  const auto sculpt = m_editMode->currentIndex() == 1;
  if (sculpt && !region.value().heightField)
    return;
  m_chunkStroke.clear();
  if (!sculpt && isTerrainChunkGroup(*group))
  {
    auto chunks = readTerrainChunks(*group);
    if (chunks.is_error())
      return;
    m_chunkStroke = std::move(chunks).value().chunks;
  }
  ++m_strokeRevision;
  m_stroke = std::move(region).value();
  m_strokeGroup = group->persistentId();
  m_sculptStroke = sculpt;
  if (m_sculptStroke)
    m_document.map().startTransaction(
      "Sculpt Terrain", mdl::TransactionScope::LongRunning);
  continueStroke(pick);
  if (m_sculptStroke)
  {
    if (m_previousPoint)
      m_sculptTimer->start();
    else
      endStroke(false);
  }
}

void TerrainInspector::continueStroke(const mdl::PickResult& pick)
{
  if (!m_stroke)
    return;
  const auto* group = selectedTerrainGroup(m_document.map());
  if (!group || group->persistentId() != m_strokeGroup)
  {
    endStroke(false);
    return;
  }
  const auto& hit = pick.first(mdl::HitFilters::type(mdl::BrushNode::BrushHitType));
  const auto face = mdl::hitToFaceHandle(hit);
  if (
    !face || !terrainOwnsBrush(*group, face->node()) || face->face().normal().z() <= 1e-6)
  {
    m_previousPoint.reset();
    if (m_sculptStroke)
      m_sculptTimer->stop();
    return;
  }
  const auto inverse = vm::invert(group->group().transformation());
  if (!inverse)
    return;
  const auto local = *inverse * hit.hitPoint();
  const auto point = m_stroke->project(local);
  if (m_sculptStroke)
  {
    if (!m_flattenHeight)
      m_flattenHeight = local.z();
    sculptTo(point * TerrainRegion::UnitsPerTexel);
    if (m_sculptStroke && !m_sculptTimer->isActive())
      m_sculptTimer->start();
    return;
  }
  const auto radius = m_radius->value() / TerrainRegion::UnitsPerTexel;
  const auto material = size_t(m_material->currentIndex());
  const auto previous = m_previousPoint.value_or(point);
  const auto delta = point - previous;
  const auto steps = std::max(1, int(std::ceil(vm::length(delta) / (radius / 4.0))));
  for (auto i = 1; i <= steps; ++i)
  {
    const auto sample = previous + delta * (double(i) / steps);
    if (m_chunkStroke.empty())
      m_stroke->paint(
        sample,
        radius,
        material,
        double(m_strength->value()) / 100.0,
        double(m_hardness->value()) / 100.0);
    else
      for (auto& chunk : m_chunkStroke)
      {
        const auto localSample = m_stroke->unproject(QPointF{sample.x(), sample.y()});
        chunk.region.paint(
          chunk.region.project(localSample),
          radius,
          material,
          double(m_strength->value()) / 100.0,
          double(m_hardness->value()) / 100.0);
      }
  }
  m_previousPoint = point;
}

void TerrainInspector::sculptTo(const vm::vec2d& point)
{
  if (!m_sculptStroke || !m_stroke || !m_stroke->heightField || !m_flattenHeight)
    return;
  auto* group = selectedTerrainGroup(m_document.map());
  if (!group || group->persistentId() != m_strokeGroup)
  {
    endStroke(false);
    return;
  }
  auto height = *m_stroke->heightField;
  const auto mode = TerrainSculptMode(m_sculptMode->currentData().toInt());
  const auto radius = double(m_radius->value());
  const auto strength =
    double(m_strength->value()) / 100.0
    * (mode == TerrainSculptMode::Smooth || mode == TerrainSculptMode::Flatten ? 0.15 : 1.0);
  const auto previous = m_previousPoint.value_or(point);
  const auto delta = point - previous;
  const auto steps = std::max(1, int(std::ceil(vm::length(delta) / (radius / 4.0))));
  auto changed = false;
  for (auto i = 1; i <= steps; ++i)
  {
    const auto result = height.sculpt(
      mode,
      previous + delta * (double(i) / steps),
      radius,
      strength,
      double(m_hardness->value()) / 100.0,
      2.0,
      *m_flattenHeight,
      m_lockEdges->isChecked());
    changed = changed || result.changed;
  }
  if (changed)
  {
    m_sculptApplying = true;
    const auto result = updateTerrainHeightField(m_document.map(), *group, height);
    m_sculptApplying = false;
    if (result.is_error())
    {
      const auto message = terrainErrorMessage(std::get<Error>(result.error()));
      endStroke(false);
      m_status->setText(message);
      return;
    }
    m_stroke->heightField = std::move(height);
    invalidateAnalysis();
    m_refreshedPaint = terrainGroupRevision(*group);
    refreshViews();
  }
  m_previousPoint = point;
}

void TerrainInspector::endStroke(const bool commit)
{
  auto stroke = std::exchange(m_stroke, std::nullopt);
  auto chunks = std::exchange(m_chunkStroke, {});
  auto id = std::exchange(m_strokeGroup, std::nullopt);
  const auto sculpt = std::exchange(m_sculptStroke, false);
  m_sculptTimer->stop();
  m_previousPoint.reset();
  m_flattenHeight.reset();
  if (sculpt)
  {
    auto& map = m_document.map();
    if (commit)
    {
      if (!map.commitTransaction())
        m_status->setText(tr("Could not save the terrain stroke."));
    }
    else
      map.cancelTransaction();
    refresh();
    refreshViews();
    return;
  }
  if (commit && stroke)
  {
    if (auto* group = selectedTerrainGroup(m_document.map());
        group && group->persistentId() == id)
    {
      if (!(chunks.empty() ? saveRegion(*group, *stroke, tr("Paint Terrain"))
                           : saveChunkRegions(*group, chunks, tr("Paint Terrain"))))
        m_status->setText(tr("Could not save the terrain stroke."));
    }
  }
}

void TerrainInspector::generate()
{
  const auto project = m_app.projectFor(m_document);
  if (!project)
    return;
  const auto asset = project->assetFor(m_document);
  if (!asset)
    return;
  const auto dir =
    QFileDialog::getExistingDirectory(this, tr("Export Terrain Compile Snapshot"));
  if (dir.isEmpty())
    return;
  const auto result = project->exportCompileSnapshot(*asset, pathFromQString(dir));
  if (result.is_error())
  {
    m_status->setText(terrainErrorMessage(std::get<Error>(result.error())));
    return;
  }
  const auto size = QFileInfo{dir + QStringLiteral("/textures.wad")}.size();
  auto summary = QFile{dir + QStringLiteral("/snapshot.txt")};
  auto tops = QString{};
  auto textures = 0;
  if (summary.open(QIODevice::ReadOnly))
  {
    while (!summary.atEnd())
    {
      const auto line = summary.readLine();
      if (line.startsWith("terrain topFaces="))
        tops = QString::fromLatin1(line.mid(17).trimmed());
      if (line.startsWith("texture "))
        ++textures;
    }
  }
  m_status->setText(tr("Exported %1 terrain top faces · %2 WAD textures · %3 MB")
                      .arg(tops)
                      .arg(textures)
                      .arg(QString::number(double(size) / 1e6, 'f', 3)));
}

void TerrainInspector::render(
  render::RenderContext& context,
  render::RenderBatch& batch,
  const mdl::PickResult* cursorPick) const
{
  if (!isVisible() && !painting())
    return;
  if ((!painting() || !cursorPick) && (!m_showCuts->isChecked() || m_stroke))
    return;
  const auto* group = selectedTerrainGroup(m_document.map());
  if (!group || group->group().terrainPaint().empty())
    return;

  auto renderer = render::RenderService{context, batch};
  renderer.setHideOccludedObjects();
  renderer.setShowBackfaces();
  if (painting() && cursorPick)
  {
    const auto& hit =
      cursorPick->first(mdl::HitFilters::type(mdl::BrushNode::BrushHitType));
    const auto face = mdl::hitToFaceHandle(hit);
    const auto inverse = vm::invert(group->group().transformation());
    if (
      face && terrainOwnsBrush(*group, face->node()) && face->face().normal().z() > 1e-6
      && inverse)
    {
      const auto& transform = group->group().transformation();
      const auto& plane = face->face().boundary();
      const auto localCenter = *inverse * hit.hitPoint();
      const auto* region = vertexRegion(*group);
      const auto drawRing = [&](const double radius) {
        auto points = std::vector<vm::vec3f>{};
        constexpr auto segments = 64;
        points.reserve(segments + 1);
        auto locals = std::vector<vm::vec3d>{};
        auto positions = std::vector<vm::vec2d>{};
        locals.reserve(segments + 1);
        positions.reserve(segments + 1);
        for (auto i = 0; i <= segments; ++i)
        {
          const auto angle = 2.0 * vm::Cd::pi() * double(i) / segments;
          locals.push_back(
            localCenter
            + vm::vec3d{radius * std::cos(angle), radius * std::sin(angle), 0});
          if (region && region->heightField)
            positions.push_back(
              region->project(locals.back()) * TerrainRegion::UnitsPerTexel);
        }
        const auto heights = positions.empty()
                               ? std::vector<std::optional<double>>{}
                               : region->heightField->sampleMany(positions);
        const auto drawSegment = [&] {
          if (points.size() < 2)
          {
            points.clear();
            return;
          }
          renderer.setLineWidth(4.0f);
          renderer.setForegroundColor(RgbaF{0, 0, 0, 0.85f});
          renderer.renderLineStrip(points);
          for (auto& point : points)
            point = point + vm::vec3f{0, 0, 0.05f};
          renderer.setLineWidth(2.0f);
          renderer.setForegroundColor(RgbaF{1, 1, 1, 1});
          renderer.renderLineStrip(points);
          points.clear();
        };
        for (auto i = 0; i <= segments; ++i)
        {
          auto local = locals[size_t(i)];
          if (!heights.empty())
          {
            const auto height = heights[size_t(i)];
            if (!height)
            {
              drawSegment();
              continue;
            }
            local[2] = *height;
            points.emplace_back(transform * local + vm::vec3d{0, 0, 0.5});
          }
          else
          {
            const auto xy = transform * local;
            points.emplace_back(
              vm::vec3d{xy.x(), xy.y(), plane.zAt(vm::vec2d{xy.x(), xy.y()})}
              + plane.normal * 0.5);
          }
        }
        drawSegment();
      };
      drawRing(double(m_radius->value()));
      if (m_hardness->value() > 0 && m_hardness->value() < 100)
        drawRing(double(m_radius->value()) * double(m_hardness->value()) / 100.0);
      render::RenderTiming::count("terrain.brush_cursors", 1);
    }
  }

  if (!m_showCuts->isChecked() || m_stroke)
    return;
  const auto region = readTerrainGroup(*group);
  if (region.is_error())
    return;
  const auto surfaces = terrainGroupSurfaces(*group, region.value());
  if (surfaces.is_error())
    return;
  const auto& transform = group->group().transformation();
  if (m_cutPreviewGroup == group->persistentId())
  {
    auto lines = std::array<std::vector<vm::vec3f>, 4>{};
    for (const auto& cut : m_cutPreview)
    {
      if (cut.surfaceIndex >= surfaces.value().size())
        continue;
      const auto& surface = surfaces.value()[cut.surfaceIndex];
      if (!m_document.map().editorContext().visible(*surface.brush))
        continue;
      const auto& plane = surface.brush->brush().face(surface.top).boundary();
      auto points = std::vector<vm::vec3f>{};
      for (const auto& pixel : cut.polygon)
      {
        const auto xy = transform * region.value().unproject(pixel);
        points.emplace_back(
          vm::vec3d{xy.x(), xy.y(), plane.zAt(vm::vec2d{xy.x(), xy.y()}) + 0.4});
      }
      if (points.size() > 1 && points.front() == points.back())
        points.pop_back();
      if (points.size() < 3)
        continue;
      auto& category = lines[cut.belowMinWidth ? 3 : size_t(cut.kind)];
      for (auto i = size_t{0}; i < points.size(); ++i)
      {
        category.push_back(points[i]);
        category.push_back(points[(i + 1) % points.size()]);
      }
    }
    const auto colors = std::array<RgbaF, 4>{
      RgbaF{0.1f, 0.9f, 1.0f, 1.0f},
      RgbaF{1.0f, 0.9f, 0.1f, 1.0f},
      RgbaF{0.8f, 0.3f, 1.0f, 1.0f},
      RgbaF{1.0f, 0.2f, 0.2f, 1.0f}};
    renderer.setLineWidth(2.0f);
    for (auto i = size_t{0}; i < lines.size(); ++i)
    {
      if (lines[i].empty())
        continue;
      renderer.setForegroundColor(colors[i]);
      renderer.renderLines(lines[i]);
    }
  }
}
} // namespace tb::ui
