/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */

#pragma once

#include <memory>

class QWidget;

namespace tb::ui
{
enum class UiAssetLibrarySnapshotKind
{
  Library,
  Project,
  TerrainMaterial,
};

/**
 * Builds a disposable asset library and returns a browser window opened on it.
 *
 * The fixture is written into a temporary folder and removed with the returned widget.
 * It covers what the browser has to render: ready textures, textures that still need a
 * GoldSrc conversion, a converted texture, a skybox group, a model, a sprite, a sound and
 * a source file that went missing after the scan. The terrain material variant also
 * selects a reusable three-texture material.
 */
std::unique_ptr<QWidget> createUiAssetLibrarySnapshot(UiAssetLibrarySnapshotKind kind);

} // namespace tb::ui
