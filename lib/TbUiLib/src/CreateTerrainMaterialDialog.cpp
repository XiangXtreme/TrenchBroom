/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/CreateTerrainMaterialDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "ui/PanelLayout.h"

#include <algorithm>

namespace tb::ui
{
CreateTerrainMaterialDialog::CreateTerrainMaterialDialog(
  const std::vector<project::LibraryAsset>& textureAssets, QWidget* parent)
  : QDialog{parent}
{
  setObjectName(QStringLiteral("CreateTerrainMaterialDialog"));
  setWindowTitle(tr("Create Terrain Material"));
  setMinimumWidth(380);

  auto* rootLayout = new QVBoxLayout{this};
  rootLayout->setContentsMargins(16, 16, 16, 16);
  rootLayout->setSpacing(12);

  auto* generalForm = createPanelForm();
  m_nameEdit = new QLineEdit{this};
  m_nameEdit->setObjectName(QStringLiteral("MaterialNameEdit"));
  m_nameEdit->setPlaceholderText(tr("e.g. alpine_blend"));
  generalForm->addRow(tr("Material Name:"), m_nameEdit);
  rootLayout->addLayout(generalForm);

  auto* slotsGroup = new QGroupBox{tr("Texture Slots"), this};
  slotsGroup->setObjectName(QStringLiteral("MaterialSlotsGroup"));
  auto* slotsLayout = createPanelColumn(slotsGroup);

  const auto populateTextures = [&](QComboBox* combo) {
    for (const auto& asset : textureAssets)
    {
      if (asset.missing)
        continue;
      combo->addItem(
        tr("%1 — %2").arg(
          QString::fromStdString(asset.displayName),
          QString::fromStdString(asset.virtualPath)),
        QString::fromStdString(asset.id));
    }
  };

  const auto createSlotRow = [&](
                               const QString& defaultName,
                               QLineEdit*& nameEdit,
                               QComboBox*& textureCombo,
                               const QString& nameObj,
                               const QString& texObj,
                               const int defaultIndex) {
    auto* rowLayout = createPanelActions();

    nameEdit = new QLineEdit{defaultName, slotsGroup};
    nameEdit->setObjectName(nameObj);
    nameEdit->setFixedWidth(90);

    textureCombo = new QComboBox{slotsGroup};
    textureCombo->setObjectName(texObj);
    populateTextures(textureCombo);
    if (defaultIndex >= 0 && defaultIndex < textureCombo->count())
      textureCombo->setCurrentIndex(defaultIndex);

    rowLayout->addWidget(nameEdit);
    rowLayout->addWidget(textureCombo, 1);
    slotsLayout->addLayout(rowLayout);
  };

  createSlotRow(
    tr("Grass"),
    m_slot0Name,
    m_slot0Texture,
    QStringLiteral("MaterialSlot0Name"),
    QStringLiteral("MaterialSlot0Texture"),
    0);
  createSlotRow(
    tr("Dirt"),
    m_slot1Name,
    m_slot1Texture,
    QStringLiteral("MaterialSlot1Name"),
    QStringLiteral("MaterialSlot1Texture"),
    textureAssets.size() > 1 ? 1 : 0);
  createSlotRow(
    tr("Rock"),
    m_slot2Name,
    m_slot2Texture,
    QStringLiteral("MaterialSlot2Name"),
    QStringLiteral("MaterialSlot2Texture"),
    textureAssets.size() > 2 ? 2 : 0);

  rootLayout->addWidget(slotsGroup);
  auto* sizeHint = new QLabel{
    tr("Use opaque square textures of 64, 128, or 256 pixels. 64-pixel textures are "
       "tiled and 256-pixel textures are resampled to a 128-pixel period."),
    this};
  sizeHint->setWordWrap(true);
  rootLayout->addWidget(sizeHint);

  m_buttonBox =
    new QDialogButtonBox{QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this};
  m_buttonBox->setObjectName(QStringLiteral("MaterialDialogButtons"));
  if (auto* okBtn = m_buttonBox->button(QDialogButtonBox::Ok))
    okBtn->setText(tr("Create"));

  rootLayout->addWidget(m_buttonBox);

  connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

  const auto onValidate = [this] { validate(); };
  connect(m_nameEdit, &QLineEdit::textChanged, this, onValidate);
  connect(m_slot0Name, &QLineEdit::textChanged, this, onValidate);
  connect(m_slot1Name, &QLineEdit::textChanged, this, onValidate);
  connect(m_slot2Name, &QLineEdit::textChanged, this, onValidate);
  connect(m_slot0Texture, &QComboBox::currentIndexChanged, this, onValidate);
  connect(m_slot1Texture, &QComboBox::currentIndexChanged, this, onValidate);
  connect(m_slot2Texture, &QComboBox::currentIndexChanged, this, onValidate);

  validate();
}

void CreateTerrainMaterialDialog::validate()
{
  const auto hasName = !m_nameEdit->text().trimmed().isEmpty();
  const auto hasTex0 = !m_slot0Texture->currentData().toString().isEmpty();
  const auto hasTex1 = !m_slot1Texture->currentData().toString().isEmpty();
  const auto hasTex2 = !m_slot2Texture->currentData().toString().isEmpty();
  if (auto* okBtn = m_buttonBox->button(QDialogButtonBox::Ok))
    okBtn->setEnabled(hasName && hasTex0 && hasTex1 && hasTex2);
}

void CreateTerrainMaterialDialog::setSelectedTextures(
  const std::array<std::string, 3>& assetIds)
{
  const auto combos = std::array{m_slot0Texture, m_slot1Texture, m_slot2Texture};
  for (auto slot = size_t{0}; slot < combos.size(); ++slot)
  {
    const auto index = combos[slot]->findData(QString::fromStdString(assetIds[slot]));
    if (index >= 0)
      combos[slot]->setCurrentIndex(index);
  }
}

project::AssetLibrary::TerrainMaterialDefinition CreateTerrainMaterialDialog::definition()
  const
{
  auto def = project::AssetLibrary::TerrainMaterialDefinition{};
  def.name = m_nameEdit->text().trimmed().toStdString();
  def.folder = "materials";
  def.materialSlots[0] = {
    m_slot0Name->text().trimmed().toStdString(),
    m_slot0Texture->currentData().toString().toStdString()};
  def.materialSlots[1] = {
    m_slot1Name->text().trimmed().toStdString(),
    m_slot1Texture->currentData().toString().toStdString()};
  def.materialSlots[2] = {
    m_slot2Name->text().trimmed().toStdString(),
    m_slot2Texture->currentData().toString().toStdString()};
  return def;
}
} // namespace tb::ui
