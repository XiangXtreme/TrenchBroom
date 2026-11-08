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

#include "ui/UvEditor.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QSpinBox>
#include <QToolButton>
#include <QtGlobal>

#include "mdl/GameConfig.h"
#include "mdl/GameInfo.h" // IWYU pragma: keep
#include "mdl/Map.h"
#include "mdl/Map_Brushes.h"
#include "mdl/UpdateBrushFaceAttributes.h"
#include "ui/BitmapButton.h"
#include "ui/MapDocument.h"
#include "ui/PanelLayout.h"
#include "ui/QStyleUtils.h"
#include "ui/UvView.h"
#include "ui/ViewConstants.h"

namespace tb::ui
{

UvEditor::UvEditor(AppController& appController, MapDocument& document, QWidget* parent)
  : QWidget{parent}
  , m_document{document}
{
  createGui(appController);
  connectObservers();
}

bool UvEditor::cancelMouseDrag()
{
  return m_uvView->cancelDrag();
}

void UvEditor::resizeEvent(QResizeEvent* event)
{
  QWidget::resizeEvent(event);
  const auto margins = m_toolbarLayout->contentsMargins();
  const auto requiredWidth =
    m_actions->minimumSizeHint().width() + m_grid->minimumSizeHint().width()
    + m_toolbarLayout->spacing() + margins.left() + margins.right();
  m_toolbarLayout->setDirection(
    width() < requiredWidth ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
}

void UvEditor::updateButtons()
{
  const auto& selection = m_document.map().selection();
  const auto singleFace = selection.brushFaces.size() == 1;
  const auto enabled = !selection.allBrushFaces().empty();
  m_uvView->setVisible(singleFace);
  m_emptyState->setVisible(!singleFace);
  m_grid->setVisible(singleFace);
  m_actions->setVisible(enabled);

  m_resetUvButton->setEnabled(enabled);
  m_resetUvToWorldButton->setEnabled(enabled);
  m_flipUAxisButton->setEnabled(enabled);
  m_flipVAxisButton->setEnabled(enabled);
  m_rotateUvCCWButton->setEnabled(enabled);
  m_rotateUvCWButton->setEnabled(enabled);
}

void UvEditor::createGui(AppController& appController)
{
  setObjectName(QStringLiteral("UvEditor"));
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
  m_uvView = new UvView{appController, m_document};
  m_uvView->setFixedHeight(220);
  m_emptyState = new QLabel{tr("Select a single face to preview and edit its UVs.")};
  m_emptyState->setObjectName("UvEditor_EmptyState");
  m_emptyState->setWordWrap(true);
  setInfoStyle(m_emptyState);

  m_resetUvButton = createBitmapButton("ResetUV.svg", tr("Reset UV alignment"), this);
  m_resetUvToWorldButton = createBitmapButton(
    "ResetUVToWorld.svg", tr("Reset UV alignment to world aligned"), this);
  m_flipUAxisButton = createBitmapButton("FlipUAxis.svg", tr("Flip U axis"), this);
  m_flipVAxisButton = createBitmapButton("FlipVAxis.svg", tr("Flip V axis"), this);
  m_rotateUvCCWButton =
    createBitmapButton("RotateUVCCW.svg", tr("Rotate UV 90° counter-clockwise"), this);
  m_rotateUvCWButton =
    createBitmapButton("RotateUVCW.svg", tr("Rotate UV 90° clockwise"), this);

  connect(m_resetUvButton, &QAbstractButton::clicked, this, &UvEditor::resetUvClicked);
  connect(
    m_resetUvToWorldButton,
    &QAbstractButton::clicked,
    this,
    &UvEditor::resetUvToWorldClicked);
  connect(m_flipUAxisButton, &QAbstractButton::clicked, this, &UvEditor::flipUvHClicked);
  connect(m_flipVAxisButton, &QAbstractButton::clicked, this, &UvEditor::flipUvVClicked);
  connect(
    m_rotateUvCCWButton, &QAbstractButton::clicked, this, &UvEditor::rotateUvCCWClicked);
  connect(
    m_rotateUvCWButton, &QAbstractButton::clicked, this, &UvEditor::rotateUvCWClicked);

  auto* gridLabel = new QLabel{"Grid "};
  setEmphasizedStyle(gridLabel);
  m_xSubDivisionEditor = new QSpinBox{};
  m_xSubDivisionEditor->setRange(1, 16);
  m_xSubDivisionEditor->setValue(1);

  m_ySubDivisionEditor = new QSpinBox{};
  m_ySubDivisionEditor->setRange(1, 16);
  m_ySubDivisionEditor->setValue(1);

  connect(
    m_xSubDivisionEditor,
    QOverload<int>::of(&QSpinBox::valueChanged),
    this,
    &UvEditor::subDivisionChanged);
  connect(
    m_ySubDivisionEditor,
    QOverload<int>::of(&QSpinBox::valueChanged),
    this,
    &UvEditor::subDivisionChanged);

  m_actions = new QWidget{};
  m_actions->setObjectName("UvEditor_Actions");
  auto* bottomLayout = createPanelActions(m_actions);
  bottomLayout->addWidget(m_resetUvButton);
  bottomLayout->addWidget(m_resetUvToWorldButton);
  bottomLayout->addWidget(m_flipUAxisButton);
  bottomLayout->addWidget(m_flipVAxisButton);
  bottomLayout->addWidget(m_rotateUvCCWButton);
  bottomLayout->addWidget(m_rotateUvCWButton);
  bottomLayout->addStretch();
  m_grid = new QWidget{};
  m_grid->setObjectName("UvEditor_Grid");
  auto* gridLayout = createPanelActions(m_grid);
  gridLayout->addWidget(gridLabel);
  gridLayout->addWidget(new QLabel{"X:"});
  gridLayout->addWidget(m_xSubDivisionEditor, 1);
  gridLayout->addWidget(new QLabel{"Y:"});
  gridLayout->addWidget(m_ySubDivisionEditor, 1);

  auto* toolbar = new QWidget{};
  toolbar->setObjectName(QStringLiteral("UvEditor_Toolbar"));
  toolbar->setAttribute(Qt::WA_StyledBackground);
  toolbar->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  m_toolbarLayout = new QBoxLayout{QBoxLayout::TopToBottom, toolbar};
  m_toolbarLayout->setContentsMargins(
    LayoutConstants::NarrowHMargin, 0, LayoutConstants::NarrowHMargin, 0);
  m_toolbarLayout->setSpacing(LayoutConstants::NarrowHMargin);
  m_toolbarLayout->addWidget(m_actions, 1);
  m_toolbarLayout->addWidget(m_grid);

  auto* outerLayout = new QVBoxLayout{};
  outerLayout->setContentsMargins(0, 0, 0, 0);
  outerLayout->setSpacing(LayoutConstants::NarrowVMargin);
  outerLayout->addWidget(m_uvView, 1);
  outerLayout->addWidget(m_emptyState);
  outerLayout->addWidget(toolbar);
  setLayout(outerLayout);

  updateButtons();
}

void UvEditor::connectObservers()
{
  m_notifierConnection +=
    m_document.documentWasLoadedNotifier.connect([&]() { updateButtons(); });
  m_notifierConnection +=
    m_document.documentDidChangeNotifier.connect([&]() { updateButtons(); });
  m_notifierConnection +=
    m_document.selectionDidChangeNotifier.connect([&](const auto&) { updateButtons(); });
}

void UvEditor::resetUvClicked()
{
  auto& map = m_document.map();
  setBrushFaceAttributes(
    map, mdl::resetAll(map.gameInfo().gameConfig.faceAttribsConfig.defaultUvAttributes));
}

void UvEditor::resetUvToWorldClicked()
{
  auto& map = m_document.map();
  setBrushFaceAttributes(
    map,
    mdl::resetAllToParaxial(
      map.gameInfo().gameConfig.faceAttribsConfig.defaultUvAttributes));
}

void UvEditor::flipUvHClicked()
{
  setBrushFaceAttributes(m_document.map(), {.xScale = mdl::MultiplyValue{-1.0f}});
}

void UvEditor::flipUvVClicked()
{
  setBrushFaceAttributes(m_document.map(), {.yScale = mdl::MultiplyValue{-1.0f}});
}

void UvEditor::rotateUvCCWClicked()
{
  setBrushFaceAttributes(m_document.map(), {.rotation = mdl::AddValue{90.0f}});
}

void UvEditor::rotateUvCWClicked()
{
  setBrushFaceAttributes(m_document.map(), {.rotation = mdl::AddValue{-90.0f}});
}

void UvEditor::subDivisionChanged()
{
  const auto x = m_xSubDivisionEditor->value();
  const auto y = m_ySubDivisionEditor->value();
  m_uvView->setSubDivisions(vm::vec2i(x, y));
}

} // namespace tb::ui
