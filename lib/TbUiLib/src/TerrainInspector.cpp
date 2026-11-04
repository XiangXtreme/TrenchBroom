/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/TerrainInspector.h"

#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
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
#include "render/RenderService.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/ProjectSession.h"
#include "ui/QPathUtils.h"

#include <algorithm>
#include <cmath>

namespace tb::ui
{
TerrainInspector::TerrainInspector(AppController& app, MapDocument& document)
  : m_app{app}
  , m_document{document}
{
  m_status = new QLabel{};
  m_status->setWordWrap(true);
  m_material = new QComboBox{};
  m_material->addItems({tr("Grass"), tr("Dirt"), tr("Rock")});
  m_radius = new QSpinBox{};
  m_radius->setRange(24, 256);
  m_radius->setValue(60);
  m_radius->setSuffix(tr(" units"));
  m_create = new QPushButton{tr("Create Terrain Region")};
  m_paint = new QPushButton{tr("Paint Terrain")};
  m_paint->setCheckable(true);
  m_generate = new QPushButton{tr("Export Compile Snapshot…")};

  auto* form = new QFormLayout{};
  form->addRow(tr("Material"), m_material);
  form->addRow(tr("Brush radius"), m_radius);
  auto* layout = new QVBoxLayout{};
  layout->addWidget(m_status);
  layout->addWidget(m_create);
  layout->addLayout(form);
  layout->addWidget(m_paint);
  layout->addWidget(m_generate);
  layout->addStretch();
  setLayout(layout);

  connect(m_create, &QPushButton::clicked, this, &TerrainInspector::createRegion);
  connect(m_generate, &QPushButton::clicked, this, &TerrainInspector::generate);
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

void TerrainInspector::refresh()
{
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
  m_radius->setEnabled(hasRegion);
  if (!hasRegion)
  {
    m_paint->setChecked(false);
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
    return;
  }
  const auto surfaces = terrainSurfaces(*group, region.value());
  if (surfaces.is_error())
  {
    m_status->setText(QString::fromStdString(std::get<Error>(surfaces.error()).msg));
    m_paint->setEnabled(false);
    m_generate->setEnabled(false);
    return;
  }
  m_status->setText(tr("%1 · %2 source top faces")
                      .arg(QString::fromStdString(group->name()))
                      .arg(surfaces.value().size()));
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
    m_stroke->paint(previous + delta * (double(i) / steps), radius, material);
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
        if (dirt + rock < 0.01f)
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
          0.40f * grass + 0.62f * dirt + 0.51f * rock,
          0.45f * grass + 0.45f * dirt + 0.50f * rock,
          0.24f * grass + 0.31f * dirt + 0.51f * rock,
          0.35f + 0.4f * (dirt + rock)});
        renderer.renderFilledPolygon(vertices);
      }
    }
  }
}
} // namespace tb::ui
