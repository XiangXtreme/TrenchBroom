/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/TerrainInspector.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/EditorContext.h"
#include "mdl/GameInfo.h"
#include "mdl/GroupNode.h"
#include "mdl/HitAdapter.h"
#include "mdl/HitFilter.h"
#include "mdl/Map.h"
#include "mdl/Map_Nodes.h"
#include "mdl/NodeContents.h"
#include "mdl/PickResult.h"
#include "mdl/TransactionScope.h"
#include "mdl/WorldNode.h"
#include "project/ProjectStore.h"
#include "render/RenderService.h"
#include "ui/AppController.h"
#include "ui/AssetLibraryWindow.h"
#include "ui/MapDocument.h"
#include "ui/MapEditorView.h"
#include "ui/ProjectSession.h"
#include "ui/QPathUtils.h"
#include "ui/SwitchableMapViewContainer.h"
#include "ui/TerrainExport.h"
#include "ui/TerrainMaterial.h"

#include <algorithm>
#include <cmath>

namespace tb::ui
{
TerrainInspector::TerrainInspector(AppController& app, MapDocument& document)
  : m_app{app}
  , m_document{document}
{
  m_status = new QLabel{this};
  m_status->setObjectName(QStringLiteral("TerrainInspector_Status"));
  m_status->setWordWrap(true);

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
  m_wadBudget->setToolTip(tr("Maximum WAD size for this terrain region only."));

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

  m_paint = new QPushButton{tr("Paint Terrain")};
  m_paint->setObjectName(QStringLiteral("TerrainInspector_PaintTerrain"));
  m_paint->setCheckable(true);

  m_generate = new QPushButton{tr("Export Compile Snapshot…")};
  m_generate->setObjectName(QStringLiteral("TerrainInspector_ExportSnapshot"));

  auto* form = new QFormLayout{};
  form->addRow(tr("Terrain Material"), m_terrainMaterial);
  form->addRow(tr("Paint Slot"), m_material);
  form->addRow(tr("Brush radius"), m_radius);
  form->addRow(tr("Brush strength"), m_strength);
  form->addRow(tr("Brush hardness"), m_hardness);
  form->addRow(tr("Extra top face limit"), m_faceBudget);
  form->addRow(tr("Minimum top face width"), m_minFaceWidth);
  form->addRow(tr("Terrain WAD limit"), m_wadBudget);
  form->addRow(tr("Local patch density"), m_patchDensity);

  auto* layout = new QVBoxLayout{};
  layout->addWidget(m_status);
  layout->addWidget(m_create);
  layout->addLayout(form);
  layout->addWidget(m_paint);
  layout->addWidget(m_openLibrary);
  layout->addWidget(m_analyze);
  layout->addWidget(m_showCuts);
  layout->addWidget(m_estimate);
  layout->addWidget(m_generate);
  layout->addStretch();
  setLayout(layout);

  connect(m_create, &QPushButton::clicked, this, &TerrainInspector::createRegion);
  connect(m_generate, &QPushButton::clicked, this, &TerrainInspector::generate);
  connect(m_analyze, &QPushButton::clicked, this, &TerrainInspector::analyze);
  connect(m_showCuts, &QCheckBox::toggled, this, [this] { refreshViews(); });
  const auto updateBudget = [this](int) {
    auto* group = selectedTerrainGroup(m_document.map());
    if (!group)
      return;
    auto region = TerrainRegion::decode(group->group().terrainPaint());
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
  connect(
    m_openLibrary, &QPushButton::clicked, this, &TerrainInspector::openAssetLibrary);
  connect(
    m_terrainMaterial, &QComboBox::currentIndexChanged, this, [this](const int index) {
      if (m_refreshingMaterial)
        return;
      auto* group = selectedTerrainGroup(m_document.map());
      if (!group)
        return;
      auto region = TerrainRegion::decode(group->group().terrainPaint());
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
    if (!on)
      endStroke(false);
  });
  const auto update = [this](const auto&) { refresh(); };
  m_connection += m_document.selectionDidChangeNotifier.connect(update);
  m_connection += m_document.nodesDidChangeNotifier.connect(update);
  m_connection += m_document.nodesWereRemovedNotifier.connect(update);
  m_connection += m_document.nodesWereAddedNotifier.connect(update);
  m_connection += m_document.groupWasOpenedNotifier.connect([this] { refresh(); });
  m_connection += m_document.groupWasClosedNotifier.connect([this] { refresh(); });
  m_connection += m_document.documentWasLoadedNotifier.connect([this] { refresh(); });
  refresh();
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
    m_slotColors = {
      {{0.40f, 0.45f, 0.24f}, {0.62f, 0.45f, 0.31f}, {0.51f, 0.50f, 0.51f}}};
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
    if (images.is_success())
    {
      for (auto slot = size_t{0}; slot < 3; ++slot)
      {
        const auto image = images.value()[slot].convertToFormat(QImage::Format_RGBA8888);
        auto sum = std::array<unsigned long long, 3>{};
        auto count = 0u;
        for (auto y = 0; y < image.height(); y += 8)
          for (auto x = 0; x < image.width(); x += 8)
          {
            const auto* pixel = image.constScanLine(y) + x * 4;
            for (auto channel = size_t{0}; channel < 3; ++channel)
              sum[channel] += pixel[channel];
            ++count;
          }
        if (count)
          for (auto channel = size_t{0}; channel < 3; ++channel)
            m_slotColors[slot][channel] = float(double(sum[channel]) / (255.0 * count));
      }
    }
  }
}

void TerrainInspector::refresh()
{
  m_cutPreview.clear();
  m_cutPreviewGroup.reset();
  m_showCuts->setChecked(false);
  m_showCuts->setEnabled(false);
  const auto& map = m_document.map();
  const auto supported = m_app.projectFor(m_document)
                         && map.gameInfo().gameConfig.name == "Half-Life"
                         && map.worldNode().mapFormat() == mdl::MapFormat::Valve;
  const auto* group = supported ? selectedTerrainGroup(map) : nullptr;
  const auto hasRegion = group && !group->group().terrainPaint().empty();
  m_create->setEnabled(group && !hasRegion);
  m_generate->setEnabled(hasRegion);
  m_paint->setEnabled(hasRegion);
  m_material->setEnabled(hasRegion);
  m_terrainMaterial->setEnabled(hasRegion);
  m_radius->setEnabled(hasRegion);
  m_strength->setEnabled(hasRegion);
  m_hardness->setEnabled(hasRegion);
  m_faceBudget->setEnabled(hasRegion);
  m_wadBudget->setEnabled(hasRegion);
  m_minFaceWidth->setEnabled(hasRegion);
  m_patchDensity->setEnabled(hasRegion);
  m_analyze->setEnabled(hasRegion);
  m_estimate->setText(
    hasRegion ? tr("Analyze to preview generated terrain statistics.") : QString{});
  m_openLibrary->setEnabled(supported);

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
    m_status->setText(
      supported ? tr("Select a brush group to create a terrain region.")
                : tr("Open a Half-Life Valve 220 map in a project to edit terrain."));
    return;
  }
  const auto region = TerrainRegion::decode(group->group().terrainPaint());
  if (region.is_error())
  {
    m_status->setText(QString::fromStdString(std::get<Error>(region.error()).msg));
    m_paint->setEnabled(false);
    m_generate->setEnabled(false);
    m_analyze->setEnabled(false);
    m_refreshingMaterial = false;
    return;
  }

  {
    const auto facesBlocker = QSignalBlocker{m_faceBudget};
    const auto wadBlocker = QSignalBlocker{m_wadBudget};
    const auto widthBlocker = QSignalBlocker{m_minFaceWidth};
    const auto densityBlocker = QSignalBlocker{m_patchDensity};
    m_faceBudget->setValue(region.value().maxAddedFaces);
    m_wadBudget->setValue(region.value().maxWadKiB);
    m_minFaceWidth->setValue(region.value().minFaceWidth);
    m_patchDensity->setCurrentIndex(m_patchDensity->findData(region.value().patchDensityPercent));
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

  updateSlotLabels(region.value().materialAssetId);

  if (foundIndex == -1 || !m_materialPreviewValid)
  {
    m_status->setText(
      tr("The bound terrain material or one of its textures is missing or invalid."));
    m_generate->setEnabled(false);
    m_analyze->setEnabled(false);
    return;
  }

  const auto surfaces = terrainSurfaces(*group, region.value());
  if (surfaces.is_error())
  {
    m_status->setText(QString::fromStdString(std::get<Error>(surfaces.error()).msg));
    m_paint->setEnabled(false);
    m_generate->setEnabled(false);
    m_analyze->setEnabled(false);
    return;
  }
  m_status->setText(tr("%1 · %2 source top faces")
                      .arg(QString::fromStdString(group->name()))
                      .arg(surfaces.value().size()));
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
    m_estimate->setText(QString::fromStdString(std::get<Error>(estimate.error()).msg));
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
  if (m_faceBudget->value() >= 0 && added > size_t(m_faceBudget->value()))
    text += tr("\nExtra top faces exceed the configured limit.");
  if (m_wadBudget->value() >= 0 && stats.wadBytes > size_t(m_wadBudget->value()) * 1024)
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

void TerrainInspector::refreshViews()
{
  for (auto* widget = parentWidget(); widget; widget = widget->parentWidget())
  {
    if (auto* editor = qobject_cast<MapEditorView*>(widget))
    {
      if (auto* view = editor->mapView())
        view->refreshViews();
      return;
    }
  }
}

bool TerrainInspector::saveRegion(
  mdl::GroupNode& group, const TerrainRegion& region, const QString& label)
{
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

void TerrainInspector::createRegion()
{
  if (auto* group = selectedTerrainGroup(m_document.map()))
  {
    const auto region = createTerrainRegion(*group);
    if (region.is_error())
      m_status->setText(QString::fromStdString(std::get<Error>(region.error()).msg));
    else if (!saveRegion(*group, region.value(), tr("Create Terrain Region")))
      m_status->setText(tr("Could not create the terrain region."));
  }
}

bool TerrainInspector::painting() const
{
  return isVisible() && m_paint->isEnabled() && m_paint->isChecked();
}

bool TerrainInspector::stroking() const
{
  return m_stroke.has_value();
}

void TerrainInspector::beginStroke(const mdl::PickResult& pick)
{
  const auto* group = selectedTerrainGroup(m_document.map());
  if (!group)
    return;
  auto region = TerrainRegion::decode(group->group().terrainPaint());
  if (region.is_error())
    return;
  m_stroke = std::move(region).value();
  m_strokeGroup = group->persistentId();
  continueStroke(pick);
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
  if (!face || face->node().parent() != group || face->face().normal().z() <= 1e-6)
  {
    m_previousPoint.reset();
    return;
  }
  const auto inverse = vm::invert(group->group().transformation());
  if (!inverse)
    return;
  const auto point = m_stroke->project(*inverse * hit.hitPoint());
  const auto radius = m_radius->value() / TerrainRegion::UnitsPerTexel;
  const auto material = size_t(m_material->currentIndex());
  const auto previous = m_previousPoint.value_or(point);
  const auto delta = point - previous;
  const auto steps = std::max(1, int(std::ceil(vm::length(delta) / (radius / 4.0))));
  for (auto i = 1; i <= steps; ++i)
    m_stroke->paint(
      previous + delta * (double(i) / steps),
      radius,
      material,
      double(m_strength->value()) / 100.0,
      double(m_hardness->value()) / 100.0);
  m_previousPoint = point;
}

void TerrainInspector::endStroke(const bool commit)
{
  auto stroke = std::exchange(m_stroke, std::nullopt);
  auto id = std::exchange(m_strokeGroup, std::nullopt);
  m_previousPoint.reset();
  if (commit && stroke)
  {
    if (auto* group = selectedTerrainGroup(m_document.map());
        group && group->persistentId() == id)
    {
      if (!saveRegion(*group, *stroke, tr("Paint Terrain")))
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
    m_status->setText(QString::fromStdString(std::get<Error>(result.error()).msg));
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
  render::RenderContext& context, render::RenderBatch& batch) const
{
  if (!isVisible())
    return;
  const auto* group = selectedTerrainGroup(m_document.map());
  if (!group || group->group().terrainPaint().empty())
    return;
  const auto region = m_stroke && group->persistentId() == m_strokeGroup
                        ? Result<TerrainRegion>{*m_stroke}
                        : TerrainRegion::decode(group->group().terrainPaint());
  if (region.is_error())
    return;
  const auto surfaces = terrainSurfaces(*group, region.value());
  if (surfaces.is_error())
    return;
  const auto& transform = group->group().transformation();
  auto renderer = render::RenderService{context, batch};
  renderer.setHideOccludedObjects();
  renderer.setShowBackfaces();
  for (const auto& surface : surfaces.value())
  {
    const auto& plane = surface.brush->brush().face(surface.top).boundary();
    if (!m_document.map().editorContext().visible(*surface.brush))
      continue;
    const auto bounds = surface.outline.boundingRect();
    for (auto row = std::max(0, int(std::floor(bounds.top() / TerrainRegion::Step)));
         row < TerrainRegion::Rows && row * TerrainRegion::Step < bounds.bottom();
         ++row)
    {
      for (auto col = std::max(0, int(std::floor(bounds.left() / TerrainRegion::Step)));
           col < TerrainRegion::Columns && col * TerrainRegion::Step < bounds.right();
           ++col)
      {
        const auto& a = region.value().weight(col, row);
        const auto& b = region.value().weight(col + 1, row);
        const auto& c = region.value().weight(col + 1, row + 1);
        const auto& d = region.value().weight(col, row + 1);
        const auto dirt = float((a[1] + b[1] + c[1] + d[1]) / 4);
        const auto rock = float((a[2] + b[2] + c[2] + d[2]) / 4);
        if (dirt + rock < 0.01f && region.value().materialAssetId.empty())
          continue;
        const auto tile = QPolygonF{QRectF{
          col * TerrainRegion::Step,
          row * TerrainRegion::Step,
          TerrainRegion::Step,
          TerrainRegion::Step}};
        const auto clip = surface.outline.intersected(tile);
        if (terrainArea(clip) < 1e-7)
          continue;
        auto vertices = std::vector<vm::vec3f>{};
        for (const auto& pixel : clip)
        {
          const auto xy = transform * region.value().unproject(pixel);
          vertices.emplace_back(
            vm::vec3d{xy.x(), xy.y(), plane.zAt(vm::vec2d{xy.x(), xy.y()}) + 0.2});
        }
        if (vertices.front() == vertices.back())
          vertices.pop_back();
        const auto grass = 1 - dirt - rock;
        renderer.setForegroundColor(RgbaF{
          m_slotColors[0][0] * grass + m_slotColors[1][0] * dirt
            + m_slotColors[2][0] * rock,
          m_slotColors[0][1] * grass + m_slotColors[1][1] * dirt
            + m_slotColors[2][1] * rock,
          m_slotColors[0][2] * grass + m_slotColors[1][2] * dirt
            + m_slotColors[2][2] * rock,
          0.35f + 0.4f * (dirt + rock)});
        renderer.renderFilledPolygon(vertices);
      }
    }
  }
  if (m_showCuts->isChecked() && !m_stroke && m_cutPreviewGroup == group->persistentId())
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
