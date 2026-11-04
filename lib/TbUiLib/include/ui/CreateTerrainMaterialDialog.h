/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <QDialog>

#include "project/AssetLibrary.h"

#include <vector>

class QComboBox;
class QDialogButtonBox;
class QLineEdit;

namespace tb::ui
{
class CreateTerrainMaterialDialog : public QDialog
{
  Q_OBJECT

  QLineEdit* m_nameEdit = nullptr;
  QLineEdit* m_slot0Name = nullptr;
  QComboBox* m_slot0Texture = nullptr;
  QLineEdit* m_slot1Name = nullptr;
  QComboBox* m_slot1Texture = nullptr;
  QLineEdit* m_slot2Name = nullptr;
  QComboBox* m_slot2Texture = nullptr;
  QDialogButtonBox* m_buttonBox = nullptr;

public:
  explicit CreateTerrainMaterialDialog(
    const std::vector<project::LibraryAsset>& textureAssets, QWidget* parent = nullptr);

  void setSelectedTextures(const std::array<std::string, 3>& assetIds);
  project::AssetLibrary::TerrainMaterialDefinition definition() const;

private:
  void validate();
};
} // namespace tb::ui
