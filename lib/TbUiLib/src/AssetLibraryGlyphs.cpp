/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom, licensed under GPL version 3 or later.
*/
#include "ui/AssetLibraryGlyphs.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QRectF>

#include <utility>

namespace tb::ui
{
namespace
{
constexpr auto Stroke = 1.35;

QPen strokePen(const QColor& color, const qreal width = Stroke)
{
  auto pen = QPen{color, width};
  pen.setCapStyle(Qt::RoundCap);
  pen.setJoinStyle(Qt::RoundJoin);
  return pen;
}

void arrowHead(QPainter& painter, const QPointF& tip, const QPointF& direction)
{
  // direction is a unit-ish vector pointing towards the tip.
  const auto normal = QPointF{-direction.y(), direction.x()};
  const auto back = tip - direction * 2.6;
  painter.drawPolyline(
    QPolygonF{back + normal * 2.4, tip, back - normal * 2.4});
}
} // namespace

void paintAssetLibraryGlyph(
  QPainter& painter, const QRectF& rect, const AssetLibraryGlyph glyph, const QColor& color)
{
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.translate(rect.topLeft());
  painter.scale(rect.width() / 16.0, rect.height() / 16.0);
  painter.setPen(strokePen(color));
  painter.setBrush(Qt::NoBrush);

  switch (glyph)
  {
  case AssetLibraryGlyph::Library: {
    const auto layer = [&](const qreal y, const bool filled) {
      const auto shape = QPolygonF{
        QPointF{8, y}, QPointF{14, y + 3}, QPointF{8, y + 6}, QPointF{2, y + 3}};
      painter.setBrush(filled ? QBrush{color} : QBrush{Qt::NoBrush});
      painter.drawPolygon(shape);
    };
    layer(7.5, false);
    layer(4.5, false);
    layer(1.5, true);
    break;
  }
  case AssetLibraryGlyph::All:
  case AssetLibraryGlyph::Grid: {
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    for (const auto x : {2.25, 9.0})
      for (const auto y : {2.25, 9.0})
        painter.drawRoundedRect(QRectF{x, y, 4.75, 4.75}, 1.2, 1.2);
    break;
  }
  case AssetLibraryGlyph::Texture: {
    const auto frame = QRectF{2.5, 2.5, 11, 11};
    painter.drawRoundedRect(frame, 2, 2);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRect(QRectF{2.5, 2.5, 5.5, 5.5});
    painter.drawRect(QRectF{8, 8, 5.5, 5.5});
    break;
  }
  case AssetLibraryGlyph::Model: {
    painter.drawPolygon(QPolygonF{
      QPointF{8, 1.75},
      QPointF{13.75, 4.9},
      QPointF{13.75, 11.1},
      QPointF{8, 14.25},
      QPointF{2.25, 11.1},
      QPointF{2.25, 4.9}});
    painter.drawPolyline(
      QPolygonF{QPointF{2.25, 4.9}, QPointF{8, 8}, QPointF{13.75, 4.9}});
    painter.drawLine(QPointF{8, 8}, QPointF{8, 14.25});
    break;
  }
  case AssetLibraryGlyph::Sprite: {
    auto path = QPainterPath{QPointF{8, 1.25}};
    path.quadTo(QPointF{8.9, 7.1}, QPointF{14.75, 8});
    path.quadTo(QPointF{8.9, 8.9}, QPointF{8, 14.75});
    path.quadTo(QPointF{7.1, 8.9}, QPointF{1.25, 8});
    path.quadTo(QPointF{7.1, 7.1}, QPointF{8, 1.25});
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawPath(path);
    break;
  }
  case AssetLibraryGlyph::Sound: {
    painter.setPen(strokePen(color, 1.8));
    const auto bars = {
      std::pair{3.0, 3.0}, std::pair{6.0, 5.5}, std::pair{9.0, 2.0}, std::pair{12.0, 4.0}};
    for (const auto& [x, inset] : bars)
      painter.drawLine(QPointF{x, 2.0 + inset}, QPointF{x, 14.0 - inset});
    painter.drawLine(QPointF{15.0, 6.5}, QPointF{15.0, 9.5});
    break;
  }
  case AssetLibraryGlyph::Skybox: {
    painter.drawLine(QPointF{1.75, 11}, QPointF{14.25, 11});
    painter.drawArc(QRectF{3.75, 6.25, 8.5, 8.5}, 0, 180 * 16);
    painter.drawLine(QPointF{8, 2}, QPointF{8, 3.75});
    painter.drawLine(QPointF{3.1, 4.1}, QPointF{4.3, 5.3});
    painter.drawLine(QPointF{12.9, 4.1}, QPointF{11.7, 5.3});
    painter.drawLine(QPointF{4.5, 13.75}, QPointF{11.5, 13.75});
    break;
  }
  case AssetLibraryGlyph::Pending: {
    painter.drawEllipse(QPointF{8, 8}, 5.9, 5.9);
    painter.drawPolyline(
      QPolygonF{QPointF{8, 4.6}, QPointF{8, 8}, QPointF{10.6, 9.6}});
    break;
  }
  case AssetLibraryGlyph::Missing: {
    painter.drawPolygon(
      QPolygonF{QPointF{8, 2}, QPointF{14.5, 13.5}, QPointF{1.5, 13.5}});
    painter.drawLine(QPointF{8, 6.25}, QPointF{8, 9.25});
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(QPointF{8, 11.4}, 0.95, 0.95);
    break;
  }
  case AssetLibraryGlyph::List: {
    for (const auto y : {3.5, 8.0, 12.5})
    {
      painter.setPen(Qt::NoPen);
      painter.setBrush(color);
      painter.drawRoundedRect(QRectF{1.75, y - 1.5, 3, 3}, 0.8, 0.8);
      painter.setPen(strokePen(color));
      painter.drawLine(QPointF{7, y}, QPointF{14.25, y});
    }
    break;
  }
  case AssetLibraryGlyph::Import: {
    painter.drawLine(QPointF{8, 1.75}, QPointF{8, 9.75});
    arrowHead(painter, QPointF{8, 9.75}, QPointF{0, 1});
    painter.drawPolyline(QPolygonF{
      QPointF{2.25, 10}, QPointF{2.25, 13.75}, QPointF{13.75, 13.75}, QPointF{13.75, 10}});
    break;
  }
  case AssetLibraryGlyph::Rescan: {
    painter.drawArc(QRectF{2.25, 2.25, 11.5, 11.5}, 60 * 16, 270 * 16);
    // The arc ends at 330 degrees (counter clockwise from 3 o'clock, lower right); the
    // head follows the counter clockwise tangent there.
    arrowHead(painter, QPointF{12.98, 10.88}, QPointF{0.5, -0.87});
    break;
  }
  case AssetLibraryGlyph::Convert: {
    painter.drawLine(QPointF{2.25, 5}, QPointF{13.25, 5});
    arrowHead(painter, QPointF{13.5, 5}, QPointF{1, 0});
    painter.drawLine(QPointF{13.75, 11}, QPointF{2.75, 11});
    arrowHead(painter, QPointF{2.5, 11}, QPointF{-1, 0});
    break;
  }
  case AssetLibraryGlyph::Reveal: {
    auto path = QPainterPath{QPointF{1.75, 4}};
    path.lineTo(1.75, 13);
    path.lineTo(14.25, 13);
    path.lineTo(14.25, 5.5);
    path.lineTo(7.75, 5.5);
    path.lineTo(6.25, 3.25);
    path.lineTo(2.5, 3.25);
    path.closeSubpath();
    painter.drawPath(path);
    break;
  }
  case AssetLibraryGlyph::More: {
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    for (const auto x : {3.25, 8.0, 12.75})
      painter.drawEllipse(QPointF{x, 8}, 1.4, 1.4);
    break;
  }
  case AssetLibraryGlyph::Search: {
    painter.drawEllipse(QPointF{6.75, 6.75}, 4.5, 4.5);
    painter.setPen(strokePen(color, 1.7));
    painter.drawLine(QPointF{10.1, 10.1}, QPointF{14, 14});
    break;
  }
  case AssetLibraryGlyph::ThumbSmall: {
    painter.drawRoundedRect(QRectF{4.5, 4.5, 7, 7}, 1.5, 1.5);
    break;
  }
  case AssetLibraryGlyph::ThumbLarge: {
    painter.drawRoundedRect(QRectF{1.75, 1.75, 12.5, 12.5}, 2.2, 2.2);
    painter.drawPolyline(QPolygonF{
      QPointF{1.75, 11.5}, QPointF{6, 7.5}, QPointF{9, 10.25}, QPointF{11, 8.5},
      QPointF{14.25, 11.25}});
    break;
  }
  case AssetLibraryGlyph::Project: {
    painter.drawRoundedRect(QRectF{2, 3, 12, 10.5}, 2, 2);
    painter.drawLine(QPointF{8, 5.75}, QPointF{8, 10.75});
    painter.drawLine(QPointF{5.5, 8.25}, QPointF{10.5, 8.25});
    break;
  }
  case AssetLibraryGlyph::ProjectAsset: {
    // A resource the project owns: a card with content lines, so it cannot be mistaken for
    // the add-to-project action the Project glyph stands for.
    painter.drawRoundedRect(QRectF{2, 3, 12, 10.5}, 2, 2);
    painter.drawLine(QPointF{4.75, 6.9}, QPointF{11.25, 6.9});
    painter.drawLine(QPointF{4.75, 9.6}, QPointF{9.25, 9.6});
    break;
  }
  case AssetLibraryGlyph::Rename: {
    painter.drawLine(QPointF{3, 13}, QPointF{6, 13});
    painter.drawPolygon(QPolygonF{
      QPointF{3, 13}, QPointF{3, 10.25}, QPointF{10.5, 2.75}, QPointF{13.25, 5.5},
      QPointF{5.75, 13}});
    break;
  }
  case AssetLibraryGlyph::Link: {
    painter.drawRoundedRect(QRectF{1.75, 5.25, 7.5, 5.5}, 2.75, 2.75);
    painter.drawRoundedRect(QRectF{6.75, 5.25, 7.5, 5.5}, 2.75, 2.75);
    break;
  }
  }

  painter.restore();
}

QPixmap assetLibraryGlyphPixmap(
  const AssetLibraryGlyph glyph,
  const QColor& color,
  const int logicalSize,
  const qreal devicePixelRatio)
{
  const auto pixels = int(logicalSize * devicePixelRatio + 0.5);
  auto pixmap = QPixmap{pixels, pixels};
  pixmap.fill(Qt::transparent);
  {
    auto painter = QPainter{&pixmap};
    paintAssetLibraryGlyph(painter, QRectF{0, 0, qreal(pixels), qreal(pixels)}, glyph, color);
  }
  pixmap.setDevicePixelRatio(devicePixelRatio);
  return pixmap;
}

QIcon assetLibraryGlyphIcon(
  const AssetLibraryGlyph glyph, const QColor& color, const int logicalSize)
{
  auto icon = QIcon{};
  for (const auto ratio : {1.0, 2.0})
    icon.addPixmap(assetLibraryGlyphPixmap(glyph, color, logicalSize, ratio));
  return icon;
}

} // namespace tb::ui
