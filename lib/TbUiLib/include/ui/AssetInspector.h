/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <QWidget>

#include "mdl/GoldSrcStudioModel.h"
#include "ui/AssetInspection.h"
#include "ui/GoldSrcSpritePreview.h"
#include "ui/ModelThumbnail.h"

#include <optional>
#include <utility>
#include <vector>

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
  QCheckBox* m_autoplay = nullptr;
  QSlider* m_position = nullptr;
  QPushButton* m_apply = nullptr;
  QPushButton* m_capture = nullptr;
  QPushButton* m_update = nullptr;
  QPushButton* m_uses = nullptr;
  QPushButton* m_showLibrary = nullptr;
  QPushButton* m_showSnapshot = nullptr;
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
  QComboBox* m_modelBody = nullptr;
  QLabel* m_modelLimits = nullptr;
  QCheckBox* m_modelBounds = nullptr;
  QCheckBox* m_modelOrigin = nullptr;
  QWidget* m_studioControls = nullptr;
  QComboBox* m_studioSequence = nullptr;
  QComboBox* m_studioBlend = nullptr;
  QComboBox* m_studioSkin = nullptr;
  QPushButton* m_studioPlay = nullptr;
  QSlider* m_studioFrame = nullptr;
  QTimer* m_studioTimer = nullptr;
  std::size_t m_studioSequenceIndex = 0;
  std::size_t m_studioFrameIndex = 0;
  std::size_t m_studioBlendIndex = 0;
  std::size_t m_studioSkinFamily = 0;
  std::optional<ModelPreviewScene> m_model;
  std::optional<mdl::GoldSrcStudioModelInfo> m_studioModel;
  // Sibling texture and animation files stay alive for every on-demand frame.
  GoldSrcStudioExternalFiles m_studioExternalFiles;
  mdl::GoldSrcStudioExternalData m_studioExternal;
  std::vector<std::size_t> m_studioVariants;
  std::vector<std::pair<std::size_t, std::size_t>> m_studioChoices;
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
  void setNavigation(bool library, bool snapshot);

signals:
  void assetChanged();
  void applyRequested();
  void captureRequested();
  void updateRequested();
  void findUsesRequested();
  void showLibraryRequested();
  void showSnapshotRequested();

protected:
  void hideEvent(QHideEvent* event) override;

private:
  void refreshPreview();
  void stopPreview();
  void showSpriteFrame();
  void showSky();
  void showModel();
  void showPrefab();
  void configureStudioModel();
  void configureStudioBlend();
  void rebuildStudioPose();
  void showStudioFrame();
  void stepStudioAnimation();
};
} // namespace tb::ui
