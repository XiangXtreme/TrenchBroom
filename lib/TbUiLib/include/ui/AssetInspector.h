/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <QWidget>

#include "ui/AssetInspection.h"
#include "ui/GoldSrcSpritePreview.h"
#include "ui/ModelThumbnail.h"

#include <optional>

class QAudioOutput;
class QBuffer;
class QComboBox;
class QLabel;
class QMediaPlayer;
class QPushButton;
class QSlider;
class QTreeWidget;
class QTimer;
class QDoubleSpinBox;
class QCheckBox;

namespace tb::ui
{
class AssetImageView;

/** Shared, read-only inspection UI. Hosts bind actions to an explicit editing target. */
class AssetInspector : public QWidget
{
  Q_OBJECT
  std::optional<inspection::Asset> m_asset;
  AssetImageView* m_image = nullptr;
  QLabel* m_name = nullptr;
  QLabel* m_status = nullptr;
  QLabel* m_summary = nullptr;
  QComboBox* m_mode = nullptr;
  QTreeWidget* m_details = nullptr;
  QWidget* m_audioControls = nullptr;
  QMediaPlayer* m_player = nullptr;
  QAudioOutput* m_output = nullptr;
  QBuffer* m_audio = nullptr;
  QPushButton* m_play = nullptr;
  QSlider* m_position = nullptr;
  QPushButton* m_apply = nullptr;
  QPushButton* m_capture = nullptr;
  QPushButton* m_update = nullptr;
  QPushButton* m_uses = nullptr;
  QWidget* m_spriteControls = nullptr;
  QSlider* m_spriteFrame = nullptr;
  QPushButton* m_spritePlay = nullptr;
  QDoubleSpinBox* m_spriteRate = nullptr;
  QTimer* m_spriteTimer = nullptr;
  std::optional<GoldSrcSpriteAnimation> m_sprite;
  QComboBox* m_skyMode = nullptr;
  double m_skyYaw = 0;
  double m_skyPitch = 0;
  QWidget* m_modelControls = nullptr;
  QComboBox* m_modelFrame = nullptr;
  QCheckBox* m_modelBounds = nullptr;
  QCheckBox* m_modelOrigin = nullptr;
  std::optional<ModelPreviewScene> m_model;
  float m_modelYaw = 0.55f;
  float m_modelPitch = 0.38f;

public:
  explicit AssetInspector(QWidget* parent = nullptr);
  ~AssetInspector() override;
  void setAsset(Result<inspection::Asset> asset);
  void clear();
  void setActions(
    bool apply, bool capture, bool update, bool uses, const QString& target = {});
  const std::optional<inspection::Asset>& asset() const;

signals:
  void assetChanged();
  void applyRequested();
  void captureRequested();
  void updateRequested();
  void findUsesRequested();

protected:
  void hideEvent(QHideEvent* event) override;

private:
  void refreshPreview();
  void stopPreview();
  void showSpriteFrame();
  void showSky();
  void showModel();
};
} // namespace tb::ui
