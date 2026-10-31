/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom, licensed under GPL version 3 or later.
*/
#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>

class QPainter;
class QRectF;

namespace tb::ui
{

/**
 * Small vector glyphs used by the asset library browser.
 *
 * The glyphs describe library content (asset kinds, states and library actions). They
 * are drawn from a 16 x 16 design grid with a single caller supplied color, so every
 * theme gets crisp icons at any device pixel ratio without shipping more SVG assets.
 */
enum class AssetLibraryGlyph
{
  Library,
  All,
  Texture,
  Model,
  Sprite,
  Sound,
  Skybox,
  Pending,
  Missing,
  Grid,
  List,
  Import,
  Rescan,
  Convert,
  Reveal,
  More,
  Search,
  ThumbSmall,
  ThumbLarge,
  Project,
  Rename,
  Link,
};

void paintAssetLibraryGlyph(
  QPainter& painter, const QRectF& rect, AssetLibraryGlyph glyph, const QColor& color);

QPixmap assetLibraryGlyphPixmap(
  AssetLibraryGlyph glyph, const QColor& color, int logicalSize, qreal devicePixelRatio);

/** Icon with 1x and 2x pixmaps; the disabled state is derived by Qt. */
QIcon assetLibraryGlyphIcon(
  AssetLibraryGlyph glyph, const QColor& color, int logicalSize = 16);

} // namespace tb::ui
