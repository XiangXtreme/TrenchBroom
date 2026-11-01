/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/SmartAssetPropertyEditor.h"

#include <QPushButton>
#include <QVBoxLayout>
namespace tb::ui
{
SmartAssetPropertyEditor::SmartAssetPropertyEditor(
  MapDocument& document, const BrowserCellType type, const bool skybox, QWidget* parent)
  : SmartPropertyEditor{document, parent}
{
  auto* layout = new QVBoxLayout{this};
  layout->setContentsMargins(0, 0, 0, 0);
  m_button =
    new QPushButton{skybox ? tr("Choose skybox asset") : tr("Choose sound asset")};
  layout->addWidget(m_button);
  connect(m_button, &QPushButton::clicked, this, [this, type, skybox]() {
    browseAsset(type, skybox);
  });
}
void SmartAssetPropertyEditor::doUpdateVisual(const std::vector<mdl::EntityNodeBase*>&)
{
  m_button->setEnabled(canBrowseProjectAssets());
  m_button->setToolTip(
    canBrowseProjectAssets() ? tr("Inspect and select a resource for this field")
                             : tr("A project map is required"));
}
} // namespace tb::ui
