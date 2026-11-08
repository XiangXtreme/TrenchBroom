/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

class QMenu;

namespace tb::mdl
{
class Map;
class LayerNode;
} // namespace tb::mdl

namespace tb::ui
{
bool canRemoveLayer(const mdl::Map& map, const mdl::LayerNode* layer);
bool removeLayer(mdl::Map& map, mdl::LayerNode* layer);
void populateLayerActions(QMenu& menu, mdl::Map& map, mdl::LayerNode* layer);
} // namespace tb::ui
