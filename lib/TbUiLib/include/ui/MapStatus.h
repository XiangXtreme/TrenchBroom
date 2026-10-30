/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <QWidget>

#include "base/NotifierConnection.h"

#include <memory>

class QComboBox;
class QToolButton;
class QTimer;

namespace tb::ui
{
class MapDocument;
class ElidedLabel;

/** One status surface bound explicitly to the workspace's active document. */
class MapStatus : public QWidget
{
  Q_OBJECT
  std::weak_ptr<MapDocument> m_document;
  NotifierConnection m_connections;
  ElidedLabel* m_label = nullptr;
  QComboBox* m_grid = nullptr;
  QToolButton* m_snap = nullptr;
  QTimer* m_refreshTimer = nullptr;

public:
  explicit MapStatus(QWidget* parent = nullptr);
  void setDocument(const std::shared_ptr<MapDocument>& document);

private:
  void refresh();
  void refreshDelayed();
};
} // namespace tb::ui
