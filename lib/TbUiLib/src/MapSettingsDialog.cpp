/*
 Copyright (C) 2010 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "ui/MapSettingsDialog.h"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRadioButton>

#include "mdl/GameConfig.h"
#include "mdl/GameInfo.h"
#include "mdl/Map.h"
#include "mdl/Map_World.h"
#include "mdl/WorldNode.h" // IWYU pragma: keep
#include "ui/ClickableLabel.h"
#include "ui/CollapsibleTitledPanel.h"
#include "ui/MapDocument.h"
#include "ui/ModEditor.h"
#include "ui/PanelLayout.h"
#include "ui/QPathUtils.h"
#include "ui/QStyleUtils.h"
#include "ui/QVecUtils.h"
#include "ui/ViewConstants.h"

#include <fmt/format.h>
#include <fmt/ostream.h>

#include <optional>

namespace tb::ui
{
namespace
{

std::optional<vm::vec3d> parseVec(const QString& qString)
{
  if (const auto vec = parse<double, 3u>(qString))
  {
    return *vec;
  }

  if (const auto val = parse<double, 1u>(qString))
  {
    return vm::vec3d::fill(val->x());
  }

  return std::nullopt;
}

QString formatVec(const std::optional<vm::bbox3d>& bbox, const bool max)
{
  if (bbox)
  {
    const auto& vec = max ? bbox->max : bbox->min;
    // Just print the first component to save space if all components are equal.
    return vec.x() == vec.y() && vec.y() == vec.z() ? QString::number(vec.x())
                                                    : toString(vec);
  }

  return QObject::tr("None");
}

} // namespace

MapSettingsDialog::MapSettingsDialog(MapDocument& document, QWidget* parent)
  : QDialog{parent}
{
  setObjectName("MapSettingsDialog");
  setWindowTitle(tr("Map Settings"));
  setWindowModality(Qt::WindowModal);
  resize(640, 600);

  auto* column = createPanelColumn(this);
  auto* mapName = new QLabel{
    document.map().path().empty() ? tr("Untitled map")
                                  : pathAsQString(document.map().path())};
  mapName->setObjectName("MapSettingsDialog_Target");
  mapName->setTextFormat(Qt::PlainText);
  mapName->setWordWrap(true);
  mapName->setTextInteractionFlags(Qt::TextSelectableByMouse);
  column->addWidget(mapName);
  auto* hint = new QLabel{tr("Changes apply immediately and can be undone in the map.")};
  hint->setWordWrap(true);
  setInfoStyle(hint);
  column->addWidget(hint);

  auto* bounds = new CollapsibleTitledPanel{tr("Map Bounds")};
  bounds->setObjectName("MapSettingsDialog_Bounds");
  bounds->createContentLayout()->addWidget(new MapPropertiesEditor{document});
  column->addWidget(bounds);

  auto* mods = new CollapsibleTitledPanel{tr("Mods")};
  mods->setObjectName("MapSettingsDialog_Mods");
  mods->setContentSizing(CollapsibleTitledPanel::ContentSizing::Expanding);
  mods->createContentLayout()->addWidget(new ModEditor{document});
  column->addWidget(mods, 1);

  auto* buttons = new QDialogButtonBox{QDialogButtonBox::Close};
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  column->addWidget(buttons);
}

// MapPropertiesEditor

MapPropertiesEditor::MapPropertiesEditor(MapDocument& document, QWidget* parent)
  : QWidget{parent}
  , m_document{document}
{
  setObjectName("MapPropertiesEditor");
  createGui();
  connectObservers();
}

std::optional<vm::bbox3d> MapPropertiesEditor::parseLineEdits()
{
  const auto min = parseVec(m_softBoundsFromMapMinEdit->text());
  const auto max = parseVec(m_softBoundsFromMapMaxEdit->text());

  return min && max && min < max ? std::optional{vm::bbox3d{*min, *max}} : std::nullopt;
}

void MapPropertiesEditor::createGui()
{
  m_softBoundsDisabled = new QRadioButton{};
  m_softBoundsDisabled->setObjectName("MapBounds_Disabled");
  auto* softBoundsDisabledLabel = new ClickableLabel{tr("Soft bounds disabled")};

  m_softBoundsFromGame = new QRadioButton{};
  m_softBoundsFromGame->setObjectName("MapBounds_Game");
  m_softBoundsFromGameMinLabel = new QLabel{};
  m_softBoundsFromGameMaxLabel = new QLabel{};
  auto* softBoundsFromGameLabel = new ClickableLabel{tr("Use game default")};

  auto* minCaptionLabel = new QLabel{tr("Min:")};
  auto* maxCaptionLabel = new QLabel{tr("Max:")};

  setInfoStyle(minCaptionLabel);
  setInfoStyle(maxCaptionLabel);
  setInfoStyle(m_softBoundsFromGameMinLabel);
  setInfoStyle(m_softBoundsFromGameMaxLabel);

  auto* softBoundsFromGameValueLayout = new QHBoxLayout{};
  softBoundsFromGameValueLayout->setContentsMargins(0, 0, 0, 0);
  softBoundsFromGameValueLayout->setSpacing(LayoutConstants::MediumHMargin);
  softBoundsFromGameValueLayout->addWidget(minCaptionLabel);
  softBoundsFromGameValueLayout->addWidget(m_softBoundsFromGameMinLabel);
  softBoundsFromGameValueLayout->addWidget(maxCaptionLabel);
  softBoundsFromGameValueLayout->addWidget(m_softBoundsFromGameMaxLabel);
  softBoundsFromGameValueLayout->addStretch(1);

  auto* softBoundsFromGameLayout = new QVBoxLayout{};
  softBoundsFromGameLayout->setContentsMargins(0, 0, 0, 0);
  softBoundsFromGameLayout->setSpacing(LayoutConstants::NarrowVMargin);
  softBoundsFromGameLayout->addWidget(softBoundsFromGameLabel);
  softBoundsFromGameLayout->addLayout(softBoundsFromGameValueLayout);

  m_softBoundsFromMap = new QRadioButton{};
  m_softBoundsFromMap->setObjectName("MapBounds_Custom");
  auto* softBoundsFromMapLabel = new ClickableLabel{tr("Use custom bounds")};
  m_softBoundsFromMapMinEdit = new QLineEdit{};
  m_softBoundsFromMapMinEdit->setPlaceholderText("min");
  m_softBoundsFromMapMaxEdit = new QLineEdit{};
  m_softBoundsFromMapMinEdit->setObjectName("MapBounds_Min");
  m_softBoundsFromMapMaxEdit->setObjectName("MapBounds_Max");
  m_softBoundsFromMapMaxEdit->setPlaceholderText("max");

  auto* softBoundsFromMapValueLayout = new QHBoxLayout{};
  softBoundsFromMapValueLayout->setSpacing(LayoutConstants::MediumHMargin);
  softBoundsFromMapValueLayout->addWidget(m_softBoundsFromMapMinEdit);
  softBoundsFromMapValueLayout->addWidget(m_softBoundsFromMapMaxEdit);
  softBoundsFromMapValueLayout->addStretch(1);

  auto* softBoundsFromMapLayout = new QVBoxLayout{};
  softBoundsFromMapLayout->setContentsMargins(0, 0, 0, 0);
  softBoundsFromMapLayout->setSpacing(LayoutConstants::NarrowVMargin);
  softBoundsFromMapLayout->addWidget(softBoundsFromMapLabel);
  softBoundsFromMapLayout->addLayout(softBoundsFromMapValueLayout);

  auto* gridLayout = new QGridLayout{};
  gridLayout->setContentsMargins(
    LayoutConstants::MediumHMargin,
    LayoutConstants::MediumVMargin,
    LayoutConstants::MediumHMargin,
    LayoutConstants::MediumVMargin);
  gridLayout->setHorizontalSpacing(LayoutConstants::NarrowHMargin);
  gridLayout->setVerticalSpacing(LayoutConstants::MediumVMargin);

  gridLayout->addWidget(m_softBoundsDisabled, 0, 0, Qt::AlignTop);
  gridLayout->addWidget(softBoundsDisabledLabel, 0, 1, Qt::AlignTop);
  gridLayout->addWidget(m_softBoundsFromGame, 1, 0, Qt::AlignTop);
  gridLayout->addLayout(softBoundsFromGameLayout, 1, 1, Qt::AlignTop);
  gridLayout->addWidget(m_softBoundsFromMap, 2, 0, Qt::AlignTop);
  gridLayout->addLayout(softBoundsFromMapLayout, 2, 1, Qt::AlignTop);

  setLayout(gridLayout);

  connect(
    softBoundsDisabledLabel,
    &ClickableLabel::clicked,
    m_softBoundsDisabled,
    &QAbstractButton::click);
  connect(
    softBoundsFromGameLabel,
    &ClickableLabel::clicked,
    m_softBoundsFromGame,
    &QAbstractButton::click);
  connect(
    softBoundsFromMapLabel,
    &ClickableLabel::clicked,
    m_softBoundsFromMap,
    &QAbstractButton::click);

  connect(m_softBoundsDisabled, &QAbstractButton::clicked, this, [&](const auto checked) {
    if (checked)
    {
      setSoftMapBounds(m_document.map(), {mdl::SoftMapBoundsType::Map, std::nullopt});
    }
  });
  connect(m_softBoundsFromGame, &QAbstractButton::clicked, this, [&](const auto checked) {
    if (checked)
    {
      setSoftMapBounds(m_document.map(), {mdl::SoftMapBoundsType::Game, std::nullopt});
    }
  });
  connect(m_softBoundsFromMap, &QAbstractButton::clicked, this, [&](const auto checked) {
    m_softBoundsFromMapMinEdit->setEnabled(true);
    m_softBoundsFromMapMaxEdit->setEnabled(true);

    if (checked)
    {
      // Only commit the change to the document right now if both text fields can be
      // parsed. Otherwise, it will be committed below in textEditingFinished once both
      // text fields have a valid value entered.
      if (const auto parsed = parseLineEdits())
      {
        setSoftMapBounds(m_document.map(), {mdl::SoftMapBoundsType::Map, *parsed});
      }
    }
  });

  const auto textEditingFinished = [&]() {
    // QLineEdit::editingFinished is emitted not just in response to user actions,
    // but also e.g. if another radio button is clicked and the min/max line edits get
    // disabled. So unfortunately we need to check if we are inside updateGui() and avoid
    // committing a change in that case.
    if (!m_updatingGui)
    {
      if (const auto parsed = parseLineEdits())
      {
        setSoftMapBounds(m_document.map(), {mdl::SoftMapBoundsType::Map, *parsed});
      }
    }
  };
  connect(
    m_softBoundsFromMapMinEdit, &QLineEdit::editingFinished, this, textEditingFinished);
  connect(
    m_softBoundsFromMapMaxEdit, &QLineEdit::editingFinished, this, textEditingFinished);

  updateGui();
}

void MapPropertiesEditor::connectObservers()
{
  m_notifierConnection +=
    m_document.documentWasLoadedNotifier.connect([&] { updateGui(); });
  m_notifierConnection +=
    m_document.documentDidChangeNotifier.connect([&] { updateGui(); });
  m_notifierConnection +=
    m_document.commandDoneNotifier.connect([this](const auto&) { updateGui(); });
  m_notifierConnection +=
    m_document.commandUndoneNotifier.connect([this](const auto&) { updateGui(); });
}

/**
 * Refresh the UI from the model
 */
void MapPropertiesEditor::updateGui()
{
  const kdl::set_temp flagChange(m_updatingGui, true);

  auto& map = m_document.map();
  const auto gameBounds = map.gameInfo().gameConfig.softMapBounds;
  m_softBoundsFromGameMinLabel->setText(formatVec(gameBounds, false));
  m_softBoundsFromGameMaxLabel->setText(formatVec(gameBounds, true));

  const auto bounds = softMapBounds(map);

  if (bounds.source == mdl::SoftMapBoundsType::Map && !bounds.bounds)
  {
    m_softBoundsDisabled->setChecked(true);

    m_softBoundsFromMapMinEdit->setEnabled(false);
    m_softBoundsFromMapMaxEdit->setEnabled(false);
  }
  else if (bounds.source == mdl::SoftMapBoundsType::Map)
  {
    m_softBoundsFromMap->setChecked(true);

    m_softBoundsFromMapMinEdit->setEnabled(true);
    m_softBoundsFromMapMaxEdit->setEnabled(true);

    m_softBoundsFromMapMinEdit->setText(formatVec(bounds.bounds, false));
    m_softBoundsFromMapMaxEdit->setText(formatVec(bounds.bounds, true));
  }
  else
  {
    m_softBoundsFromGame->setChecked(true);

    m_softBoundsFromMapMinEdit->setEnabled(false);
    m_softBoundsFromMapMaxEdit->setEnabled(false);
  }
}

} // namespace tb::ui
