/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <QObject>

#include "base/NotifierConnection.h"
#include "ui/GoldSrcEntityMotion.h"

namespace tb::render
{
class RenderContext;
class RenderBatch;
} // namespace tb::render
namespace tb::ui
{
class MapDocument;

/** Per-editor, display-only state shared by the property editor and all map views. */
class EntityMotionPreview : public QObject
{
  Q_OBJECT
public:
  explicit EntityMotionPreview(MapDocument& document);
  const std::optional<GoldSrcEntityMotion>& motion() const;
  double progress() const;
  bool opposite() const;
  bool active() const;
  void setProgress(double progress);
  void setOpposite(bool opposite);
  void reset();
  void setSuspended(bool suspended);
  void render(render::RenderContext& context, render::RenderBatch& batch) const;

signals:
  void changed();

private:
  void refresh();
  void clear();
  MapDocument& m_document;
  const mdl::EntityNode* m_owner = nullptr; // Identity only; never dereferenced.
  std::optional<GoldSrcEntityMotion> m_motion;
  double m_progress = 0;
  bool m_opposite = false;
  bool m_active = false;
  bool m_suspended = false;
  NotifierConnection m_notifierConnection;
};
} // namespace tb::ui
