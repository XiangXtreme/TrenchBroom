/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include "base/NotifierConnection.h"
#include "mdl/IdType.h"
#include "ui/TabBook.h"
#include "ui/TerrainRegion.h"

#include <optional>

class QComboBox;
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
  QComboBox* m_material = nullptr;
  QSpinBox* m_radius = nullptr;
  QPushButton* m_create = nullptr;
  QPushButton* m_paint = nullptr;
  QPushButton* m_generate = nullptr;
  std::optional<TerrainRegion> m_stroke;
  std::optional<mdl::IdType> m_strokeGroup;
  std::optional<vm::vec2d> m_previousPoint;
  NotifierConnection m_connection;

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
  bool saveRegion(
    mdl::GroupNode& group, const TerrainRegion& region, const QString& label);
};
} // namespace tb::ui
