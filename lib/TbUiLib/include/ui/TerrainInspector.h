/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include "base/NotifierConnection.h"
#include "mdl/IdType.h"
#include "ui/TabBook.h"
#include "ui/TerrainExport.h"
#include "ui/TerrainRegion.h"

#include <array>
#include <optional>

class QComboBox;
class QCheckBox;
class QLabel;
class QPushButton;
class QSpinBox;

namespace tb::mdl
{
class PickResult;
}
namespace tb::render
{
class RenderBatch;
class RenderContext;
} // namespace tb::render
namespace tb::ui
{
class AppController;
class MapDocument;

class TerrainInspector : public TabBookPage
{
  Q_OBJECT

private:
  AppController& m_app;
  MapDocument& m_document;
  QLabel* m_status = nullptr;
  QComboBox* m_terrainMaterial = nullptr;
  QComboBox* m_material = nullptr;
  QSpinBox* m_radius = nullptr;
  QSpinBox* m_strength = nullptr;
  QSpinBox* m_hardness = nullptr;
  QSpinBox* m_faceBudget = nullptr;
  QSpinBox* m_wadBudget = nullptr;
  QSpinBox* m_minFaceWidth = nullptr;
  QComboBox* m_patchDensity = nullptr;
  QCheckBox* m_showCuts = nullptr;
  QLabel* m_estimate = nullptr;
  QPushButton* m_openLibrary = nullptr;
  QPushButton* m_create = nullptr;
  QPushButton* m_paint = nullptr;
  QPushButton* m_generate = nullptr;
  QPushButton* m_analyze = nullptr;
  std::optional<TerrainRegion> m_stroke;
  std::optional<mdl::IdType> m_strokeGroup;
  std::optional<vm::vec2d> m_previousPoint;
  std::optional<mdl::IdType> m_cutPreviewGroup;
  std::vector<TerrainEstimate::Cut> m_cutPreview;
  NotifierConnection m_connection;
  bool m_refreshingMaterial = false;
  bool m_materialPreviewValid = true;
  std::string m_previewIdentity;
  std::array<std::array<float, 3>, 3> m_slotColors{
    {{0.40f, 0.45f, 0.24f}, {0.62f, 0.45f, 0.31f}, {0.51f, 0.50f, 0.51f}}};

public:
  TerrainInspector(AppController& app, MapDocument& document);
  bool painting() const;
  bool stroking() const;
  void beginStroke(const mdl::PickResult& pick);
  void continueStroke(const mdl::PickResult& pick);
  void endStroke(bool commit);
  void render(render::RenderContext& context, render::RenderBatch& batch) const;

private:
  void refresh();
  void createRegion();
  void generate();
  void analyze();
  void refreshViews();
  void openAssetLibrary();
  void updateSlotLabels(const std::string& materialAssetId);
  bool saveRegion(
    mdl::GroupNode& group, const TerrainRegion& region, const QString& label);
};
} // namespace tb::ui
