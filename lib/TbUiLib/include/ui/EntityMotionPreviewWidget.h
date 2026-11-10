/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <QWidget>

class QLabel;
class QCheckBox;
class QSlider;
class QComboBox;
class QPushButton;
namespace tb::ui
{
class EntityMotionPreview;

class EntityMotionPreviewWidget : public QWidget
{
  Q_OBJECT
public:
  explicit EntityMotionPreviewWidget(
    EntityMotionPreview& preview, QWidget* parent = nullptr);

private:
  void refresh();
  EntityMotionPreview& m_preview;
  QLabel* m_summary;
  QComboBox* m_direction;
  QSlider* m_slider;
  QPushButton* m_start;
  QPushButton* m_end;
  QCheckBox* m_enabled;
};
} // namespace tb::ui
