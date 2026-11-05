/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

namespace tb::ui
{
struct TerrainMaterial;

namespace detail
{
void precomputeTerrainMaterialLevels(TerrainMaterial& material);
bool terrainMaterialLevelsMatch(const TerrainMaterial& material);
} // namespace detail
} // namespace tb::ui
