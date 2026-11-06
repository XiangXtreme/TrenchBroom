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

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

class QComboBox;
class QCheckBox;
class QFormLayout;
class QLabel;
class QPushButton;
class QSpinBox;
class QTimer;

namespace tb::mdl
{
class PickResult;
class Node;
} // namespace tb::mdl
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
  QFormLayout* m_form = nullptr;
  QComboBox* m_editMode = nullptr;
  QComboBox* m_sculptMode = nullptr;
  QComboBox* m_gridSpacing = nullptr;
  QCheckBox* m_lockEdges = nullptr;
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
  QPushButton* m_convert = nullptr;
  QPushButton* m_paint = nullptr;
  QPushButton* m_generate = nullptr;
  QPushButton* m_analyze = nullptr;
  QTimer* m_refreshTimer = nullptr;
  QTimer* m_sculptTimer = nullptr;
  std::optional<TerrainRegion> m_stroke;
  size_t m_strokeRevision = 0;
  std::optional<mdl::IdType> m_strokeGroup;
  std::optional<vm::vec2d> m_previousPoint;
  std::optional<double> m_flattenHeight;
  std::optional<mdl::IdType> m_cutPreviewGroup;
  std::vector<TerrainEstimate::Cut> m_cutPreview;
  NotifierConnection m_connection;
  bool m_refreshingMaterial = false;
  bool m_materialPreviewValid = true;
  bool m_sculptStroke = false;
  bool m_sculptApplying = false;
  std::string m_previewIdentity;
  std::optional<mdl::IdType> m_refreshedGroup;
  std::string m_refreshedPaint;

public:
  TerrainInspector(AppController& app, MapDocument& document);
  bool painting() const;
  bool stroking() const;
  size_t strokeRevision() const;
  const TerrainRegion* previewStroke(const mdl::GroupNode& group) const;
  void beginStroke(const mdl::PickResult& pick);
  void continueStroke(const mdl::PickResult& pick);
  void endStroke(bool commit);
  void render(
    render::RenderContext& context,
    render::RenderBatch& batch,
    const mdl::PickResult* cursorPick) const;

private:
  void refresh();
  void nodesChanged(const std::vector<mdl::Node*>& nodes);
  void createRegion();
  void convertSelection();
  void updateModeControls();
  void sculptTo(const vm::vec2d& point);
  void generate();
  void analyze();
  void refreshViews();
  void openAssetLibrary();
  void updateSlotLabels(const std::string& materialAssetId);
  bool saveRegion(
    mdl::GroupNode& group, const TerrainRegion& region, const QString& label);
};
} // namespace tb::ui
