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

#include "ui/FaceInspector.h"

#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "gl/Material.h"
#include "mdl/BrushFace.h"
#include "mdl/GameConfig.h"
#include "mdl/GameInfo.h"
#include "mdl/Map.h"
#include "mdl/Map_Brushes.h"
#include "mdl/UpdateBrushFaceAttributes.h"
#include "ui/AppController.h"
#include "ui/BorderLine.h"
#include "ui/FaceAttribsEditor.h"
#include "ui/MapDocument.h"
#include "ui/MaterialBrowser.h"
#include "ui/MaterialCollectionEditor.h"
#include "ui/ModelBrowser.h"
#include "ui/QStyleUtils.h"
#include "ui/Splitter.h"
#include "ui/SwitchableTitledPanel.h"
#include "ui/ViewConstants.h"
#include "ui/WidgetState.h"

#include <algorithm>
#include <vector>

namespace tb::ui
{
namespace
{

void resetMaterialBrowserInfo(mdl::Map& map, QWidget* materialBrowserInfo)
{
  const auto& gameConfig = map.gameInfo().gameConfig;
  materialBrowserInfo->setVisible(gameConfig.materialConfig.property != std::nullopt);
}

} // namespace

FaceInspector::FaceInspector(
  AppController& appController, MapDocument& document, QWidget* parent)
  : TabBookPage{parent}
  , m_document{document}
{
  createGui(appController);
  connectObservers();
}

FaceInspector::~FaceInspector()
{
  saveWidgetState(m_splitter);
}

bool FaceInspector::cancelMouseDrag()
{
  return m_faceAttribsEditor->cancelMouseDrag();
}

void FaceInspector::revealMaterial(const gl::Material* material)
{
  m_materialBrowser->revealMaterial(material);
  m_materialBrowser->setSelectedMaterial(material);
}

void FaceInspector::createGui(AppController& appController)
{
  m_splitter = new Splitter{Qt::Vertical};
  m_splitter->setObjectName("FaceInspector_Splitter");

  m_splitter->addWidget(createFaceAttribsEditor(appController));
  m_splitter->addWidget(createMaterialBrowser(appController));

  // when the window resizes, the browser should get extra space
  m_splitter->setStretchFactor(0, 0);
  m_splitter->setStretchFactor(1, 1);

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(m_splitter, 1);
  setLayout(layout);

  connect(
    m_materialBrowser,
    &MaterialBrowser::materialSelected,
    this,
    &FaceInspector::materialSelected);

  restoreWidgetState(m_splitter);
}

QWidget* FaceInspector::createFaceAttribsEditor(AppController& appController)
{
  m_faceAttribsEditor = new FaceAttribsEditor{appController, m_document};
  return m_faceAttribsEditor;
}

QWidget* FaceInspector::createMaterialBrowser(AppController& appController)
{
  auto* panel =
    new SwitchableTitledPanel{tr("Material Browser"), {{tr("Browser"), tr("Settings")}}};

  m_materialBrowser = new MaterialBrowser{appController, m_document};

  auto* materialBrowserLayout = new QVBoxLayout{};
  materialBrowserLayout->setContentsMargins(0, 0, 0, 0);
  materialBrowserLayout->addWidget(m_materialBrowser, 1);
  if (appController.projectFor(m_document))
  {
    auto* choose = new QPushButton{tr("Choose texture asset…")};
    materialBrowserLayout->addWidget(choose);
    connect(choose, &QPushButton::clicked, this, [this, &appController]() {
      const auto faces = m_document.map().selection().allBrushFaces();
      if (faces.empty())
        return;
      const auto result = ModelBrowser::chooseAssetPath(
        appController, m_document, BrowserCellType::Texture, this, [this, faces]() {
          return faces == m_document.map().selection().allBrushFaces();
        });
      if (result.is_error())
        QMessageBox::warning(
          this,
          tr("Select Texture"),
          QString::fromStdString(std::get<Error>(result.error()).msg));
      else if (result.value())
        mdl::setBrushFaceAttributes(
          m_document.map(), faces, {.materialName = *result.value()});
    });
    const auto update = [this, choose](const auto&...) {
      choose->setEnabled(!m_document.map().selection().allBrushFaces().empty());
      choose->setToolTip(
        choose->isEnabled() ? tr("Apply a project or library texture to selected faces")
                            : tr("Select one or more brush faces"));
    };
    m_notifierConnection += m_document.selectionDidChangeNotifier.connect(update);
    update();
  }
  panel->getPanel(0)->setLayout(materialBrowserLayout);

  auto* materialCollectionEditor = new MaterialCollectionEditor{m_document};
  m_materialBrowserInfo = createMaterialBrowserInfo();

  auto* materialCollectionEditorLayout = new QVBoxLayout{};
  materialCollectionEditorLayout->setContentsMargins(0, 0, 0, 0);
  materialCollectionEditorLayout->setSpacing(0);
  materialCollectionEditorLayout->addWidget(materialCollectionEditor, 1);
  materialCollectionEditorLayout->addWidget(m_materialBrowserInfo, 0);

  panel->getPanel(1)->setLayout(materialCollectionEditorLayout);

  return panel;
}

QWidget* FaceInspector::createMaterialBrowserInfo()
{
  auto* label = new QLabel{tr(
    R"(To manage wad files, select the "wad" property of the worldspawn entity to reveal a wad file manager below the entity property table.)")};

  label->setWordWrap(true);
  setInfoStyle(label);

  auto* labelLayout = new QVBoxLayout{};
  labelLayout->setContentsMargins(
    LayoutConstants::WideHMargin,
    LayoutConstants::WideVMargin,
    LayoutConstants::WideHMargin,
    LayoutConstants::WideVMargin);
  labelLayout->addWidget(label);

  auto* panelLayout = new QVBoxLayout{};
  panelLayout->setContentsMargins(0, 0, 0, 0);
  panelLayout->setSpacing(0);
  panelLayout->addWidget(new BorderLine{}, 0);
  panelLayout->addLayout(labelLayout);

  auto* panel = new QWidget{};
  panel->setLayout(panelLayout);
  return panel;
}

void FaceInspector::materialSelected(const gl::Material* material)
{
  auto& map = m_document.map();
  const auto faces = map.selection().allBrushFaces();

  if (material)
  {
    if (!faces.empty())
    {
      const auto allFacesHaveMaterial = std::ranges::all_of(
        faces,
        [&](const auto& faceHandle) { return faceHandle.face().material() == material; });

      const auto materialNameToSet =
        !allFacesHaveMaterial ? material->name() : mdl::BrushFace::NoMaterialName;

      map.setCurrentMaterialName(materialNameToSet);
      setBrushFaceAttributes(map, {.materialName = materialNameToSet});
    }
    else
    {
      map.setCurrentMaterialName(
        map.currentMaterialName() != material->name() ? material->name()
                                                      : mdl::BrushFace::NoMaterialName);
    }
  }
}

void FaceInspector::connectObservers()
{
  m_notifierConnection +=
    m_document.documentWasLoadedNotifier.connect(this, &FaceInspector::documentWasLoaded);
}

void FaceInspector::documentWasLoaded()
{
  resetMaterialBrowserInfo(m_document.map(), m_materialBrowserInfo);
}

} // namespace tb::ui
