/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/EntityMotionPreviewWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

#include "ui/EntityMotionPreview.h"
#include "ui/PanelLayout.h"

namespace tb::ui
{
EntityMotionPreviewWidget::EntityMotionPreviewWidget(
  EntityMotionPreview& preview, QWidget* parent)
  : QWidget{parent}
  , m_preview{preview}
{
  setObjectName("EntityMotionPreview");
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  auto* column = createPanelColumn(this, PanelInsets::Toolbar);
  auto* heading = createPanelActions();
  m_enabled = new QCheckBox{tr("Preview"), this};
  m_enabled->setObjectName("EntityMotionPreview_Enabled");
  m_enabled->setAccessibleName(tr("Show motion preview"));
  m_enabled->setToolTip(tr("Show or hide the motion preview without changing the map"));
  heading->addWidget(m_enabled);
  m_summary = new QLabel{this};
  m_summary->setWordWrap(true);
  heading->addWidget(m_summary, 1);
  m_direction = new QComboBox{this};
  m_direction->setProperty("tbControlRole", "compact");
  m_direction->addItems({tr("Default direction"), tr("Opposite direction")});
  m_direction->setAccessibleName(tr("Opening direction"));
  m_direction->setToolTip(
    tr("Two-way doors open away from the activator. Preview either direction."));
  heading->addWidget(m_direction);
  column->addLayout(heading);
  auto* controls = createPanelActions();
  m_start = new QPushButton{tr("Initial"), this};
  m_end = new QPushButton{tr("Activated"), this};
  for (auto* button : {m_start, m_end})
    button->setProperty("tbControlRole", "compact");
  m_start->setToolTip(tr("Preview the position at game start"));
  m_end->setToolTip(tr("Preview the position after activation"));
  m_slider = new QSlider{Qt::Horizontal, this};
  m_slider->setObjectName("EntityMotionPreview_Progress");
  m_slider->setRange(0, 1000);
  m_slider->setPageStep(100);
  m_slider->setAccessibleName(tr("Motion preview progress"));
  controls->addWidget(m_start);
  controls->addWidget(m_slider, 1);
  controls->addWidget(m_end);
  column->addLayout(controls);
  connect(m_slider, &QSlider::valueChanged, this, [this](int value) {
    m_preview.setProgress(double(value) / 1000.0);
  });
  connect(m_start, &QPushButton::clicked, this, [this] { m_preview.setProgress(0); });
  connect(m_end, &QPushButton::clicked, this, [this] { m_preview.setProgress(1); });
  connect(m_enabled, &QCheckBox::toggled, this, [this](const bool enabled) {
    if (enabled)
      m_preview.setProgress(m_preview.progress());
    else
      m_preview.reset();
  });
  connect(m_direction, &QComboBox::currentIndexChanged, this, [this](int index) {
    m_preview.setOpposite(index == 1);
  });
  connect(
    &preview, &EntityMotionPreview::changed, this, &EntityMotionPreviewWidget::refresh);
  refresh();
}

void EntityMotionPreviewWidget::refresh()
{
  const auto& motion = m_preview.motion();
  setVisible(motion.has_value());
  if (!motion)
    return;
  const auto movable = motion->problem == MotionProblem::None;
  m_slider->setEnabled(movable);
  m_start->setEnabled(movable);
  m_end->setEnabled(movable);
  m_enabled->setEnabled(movable);
  m_direction->setVisible(movable && motion->bidirectional);
  const auto sliderBlocker = QSignalBlocker{m_slider};
  const auto directionBlocker = QSignalBlocker{m_direction};
  const auto enabledBlocker = QSignalBlocker{m_enabled};
  m_enabled->setChecked(m_preview.active());
  m_slider->setValue(qRound(m_preview.progress() * 1000));
  m_direction->setCurrentIndex(m_preview.opposite() ? 1 : 0);
  auto summary = QString{};
  switch (motion->problem)
  {
  case MotionProblem::None:
    summary = tr("%1% · %2%3")
                .arg(qRound(m_preview.progress() * 100))
                .arg(motion->distance, 0, 'f', 1)
                .arg(motion->rotating ? tr("°") : tr(" units"));
    break;
  case MotionProblem::Stationary:
    summary = tr("This entity does not move");
    break;
  case MotionProblem::InvalidProperty:
    summary = tr("Invalid motion properties");
    break;
  case MotionProblem::NoGeometry:
    summary = tr("No model brushes");
    break;
  case MotionProblem::CompilerOverride:
    summary = tr("Compiler geometry overrides are not supported");
    break;
  case MotionProblem::OriginConflict:
    summary = tr("Conflicting ORIGIN brush and origin properties");
    break;
  }
  m_summary->setText(summary);
  m_summary->setToolTip(
    motion->startOpen
      ? tr("Start Open: the initial position is the open endpoint.")
      : tr("Display-only preview. Geometry, properties and undo history are unchanged."));
}
} // namespace tb::ui
