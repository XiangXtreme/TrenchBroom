/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once
#include "ui/AssetBrowserModel.h"
#include "ui/SmartPropertyEditor.h"
class QPushButton;
namespace tb::ui
{
class SmartAssetPropertyEditor : public SmartPropertyEditor
{
  QPushButton* m_button = nullptr;

public:
  SmartAssetPropertyEditor(
    MapDocument& document, BrowserCellType type, bool skybox, QWidget* parent = nullptr);

private:
  void doUpdateVisual(const std::vector<mdl::EntityNodeBase*>&) override;
};
} // namespace tb::ui
