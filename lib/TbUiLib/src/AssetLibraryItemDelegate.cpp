/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom, licensed under GPL version 3 or later.
*/
#include "ui/AssetLibraryItemDelegate.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPixmap>
#include <QStyleOptionViewItem>

#include <algorithm>

namespace tb::ui
{
namespace
{
QColor blend(const QColor& base, const QColor& over, const double amount)
{
  return QColor{
    int(base.red() + (over.red() - base.red()) * amount),
    int(base.green() + (over.green() - base.green()) * amount),
    int(base.blue() + (over.blue() - base.blue()) * amount)};
}

QSize thumbnailSize(const QSize& area)
{
  const auto side = std::min(area.width(), area.height());
  return QSize{std::max(0, side), std::max(0, side)};
}
} // namespace

AssetLibraryItemDelegate::AssetLibraryItemDelegate(QObject* parent)
  : QStyledItemDelegate{parent}
{
}

void AssetLibraryItemDelegate::setListMode(const bool listMode)
{
  m_listMode = listMode;
}

void AssetLibraryItemDelegate::paint(
  QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
  painter->save();
  painter->setRenderHint(QPainter::Antialiasing, true);
  if (m_listMode)
    paintList(painter, option, index);
  else
    paintGrid(painter, option, index);
  painter->restore();
}

void AssetLibraryItemDelegate::paintGrid(
  QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
  const auto palette = option.palette;
  const auto selected = (option.state & QStyle::State_Selected) != 0;
  const auto hovered = (option.state & QStyle::State_MouseOver) != 0;
  const auto card = palette.color(QPalette::Active, QPalette::AlternateBase);
  const auto accent = palette.color(QPalette::Active, QPalette::Highlight);
  const auto border = palette.color(QPalette::Active, QPalette::Mid);
  const auto surface =
    selected ? blend(card, accent, 0.35)
             : hovered ? palette.color(QPalette::Active, QPalette::Midlight)
                       : card;

  auto cardRect = option.rect.adjusted(3, 3, -3, -3);
  painter->setPen(QPen{selected ? accent : border, 1.0});
  painter->setBrush(surface);
  painter->drawRoundedRect(cardRect, 4, 4);

  const auto inner = cardRect.adjusted(6, 6, -6, -6);
  const auto nameHeight = QFontMetrics{option.font}.height();
  const auto statusHeight = QFontMetrics{option.font}.height() - 2;
  const auto textBlock = nameHeight + statusHeight + 10;
  auto thumbArea = QRect{
    inner.left(),
    inner.top(),
    inner.width(),
    std::max(20, inner.height() - textBlock)};
  const auto side = thumbnailSize(thumbArea.size());
  const auto thumbRect = QRect{
    thumbArea.left() + (thumbArea.width() - side.width()) / 2,
    thumbArea.top() + (thumbArea.height() - side.height()) / 2,
    side.width(),
    side.height()};
  paintThumbnail(painter, thumbRect, index, palette);

  const auto textRect = QRect{
    inner.left(), thumbRect.bottom() + 6, inner.width(), inner.bottom() - thumbRect.bottom() - 6};
  paintTexts(painter, textRect, index, palette, Qt::AlignHCenter);
}

void AssetLibraryItemDelegate::paintList(
  QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
  const auto palette = option.palette;
  const auto selected = (option.state & QStyle::State_Selected) != 0;
  const auto hovered = (option.state & QStyle::State_MouseOver) != 0;
  const auto card = palette.color(QPalette::Active, QPalette::AlternateBase);
  const auto accent = palette.color(QPalette::Active, QPalette::Highlight);
  const auto border = palette.color(QPalette::Active, QPalette::Mid);
  const auto surface =
    selected ? blend(card, accent, 0.35)
             : hovered ? palette.color(QPalette::Active, QPalette::Midlight)
                       : card;

  const auto cardRect = option.rect.adjusted(3, 2, -3, -2);
  painter->setPen(QPen{selected ? accent : border, 1.0});
  painter->setBrush(surface);
  painter->drawRoundedRect(cardRect, 4, 4);

  const auto side = std::max(0, cardRect.height() - 10);
  const auto thumbRect = QRect{cardRect.left() + 6, cardRect.top() + 5, side, side};
  paintThumbnail(painter, thumbRect, index, palette);

  const auto textRect = QRect{
    thumbRect.right() + 10,
    cardRect.top() + 5,
    std::max(0, cardRect.right() - thumbRect.right() - 16),
    cardRect.height() - 10};
  paintTexts(painter, textRect, index, palette, Qt::AlignLeft | Qt::AlignVCenter);
}

void AssetLibraryItemDelegate::paintThumbnail(
  QPainter* painter,
  const QRect& rect,
  const QModelIndex& index,
  const QPalette& palette) const
{
  if (rect.isEmpty())
    return;

  const auto pixmap = index.data(ThumbnailRole).value<QPixmap>();
  if (!pixmap.isNull())
  {
    const auto scaled =
      pixmap.scaled(rect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    painter->save();
    painter->setClipRect(rect);
    painter->drawPixmap(
      rect.left() + (rect.width() - scaled.width()) / 2,
      rect.top() + (rect.height() - scaled.height()) / 2,
      scaled);
    painter->restore();
    return;
  }

  // Assets without a decodable preview get a quiet tile with their kind instead of a
  // blank square.
  const auto glyph = index.data(GlyphRole).toString();
  painter->save();
  painter->setPen(Qt::NoPen);
  const auto tile = palette.color(QPalette::Active, QPalette::Button);
  painter->setBrush(tile);
  painter->drawRoundedRect(rect, 3, 3);
  if (!glyph.isEmpty())
  {
    auto font = painter->font();
    font.setBold(true);
    font.setPointSizeF(std::max(7.0, font.pointSizeF() * 0.9));
    painter->setFont(font);
    painter->setPen(palette.color(QPalette::Active, QPalette::PlaceholderText));
    painter->drawText(rect, Qt::AlignCenter, glyph);
  }
  painter->restore();
}

void AssetLibraryItemDelegate::paintTexts(
  QPainter* painter,
  const QRect& rect,
  const QModelIndex& index,
  const QPalette& palette,
  const Qt::Alignment alignment) const
{
  if (rect.isEmpty())
    return;

  const auto textColor = palette.color(QPalette::Active, QPalette::WindowText);
  const auto dimColor = palette.color(QPalette::Active, QPalette::PlaceholderText);
  const auto status = index.data(StatusRole).toInt();

  auto nameFont = painter->font();
  nameFont.setBold(true);
  const auto nameMetrics = QFontMetrics{nameFont};
  const auto name = nameMetrics.elidedText(
    index.data(Qt::DisplayRole).toString(), Qt::ElideMiddle, rect.width());
  painter->setFont(nameFont);
  painter->setPen(textColor);
  painter->drawText(
    QRect{rect.left(), rect.top(), rect.width(), nameMetrics.height()},
    alignment | Qt::AlignTop,
    name);

  auto statusFont = painter->font();
  statusFont.setBold(false);
  statusFont.setPointSizeF(std::max(6.5, statusFont.pointSizeF() * 0.92));
  const auto statusMetrics = QFontMetrics{statusFont};
  const auto statusText = statusMetrics.elidedText(
    index.data(SubtitleRole).toString(), Qt::ElideRight, rect.width() - 10);

  const auto statusColor = [status]() {
    switch (status)
    {
    case Pending:
      return QColor{198, 170, 117};
    case Missing:
      return QColor{199, 127, 127};
    default:
      return QColor{143, 180, 155};
    }
  }();

  const auto lineTop = rect.top() + nameMetrics.height() + 3;
  painter->setFont(statusFont);
  if (!statusText.isEmpty() && lineTop + statusMetrics.height() <= rect.bottom() + 2)
  {
    const auto dotSize = 5;
    auto textArea = QRect{
      rect.left(), lineTop, rect.width(), statusMetrics.height()};

    if (alignment & Qt::AlignHCenter)
    {
      const auto textWidth = statusMetrics.horizontalAdvance(statusText) + dotSize + 4;
      textArea.setLeft(rect.left() + (rect.width() - textWidth) / 2);
      textArea.setWidth(textWidth);
    }

    painter->setPen(Qt::NoPen);
    painter->setBrush(statusColor);
    painter->drawEllipse(
      QPoint{textArea.left() + dotSize / 2 + 1,
             textArea.top() + textArea.height() / 2},
      dotSize / 2 + 1,
      dotSize / 2 + 1);
    painter->setPen(dimColor);
    painter->drawText(
      QRect{textArea.left() + dotSize + 4,
            textArea.top(),
            textArea.width() - dotSize - 4,
            textArea.height()},
      alignment | Qt::AlignTop,
      statusText);
  }
}

QSize AssetLibraryItemDelegate::sizeHint(
  const QStyleOptionViewItem& option, const QModelIndex& index) const
{
  Q_UNUSED(index);
  if (m_listMode)
    return QSize{option.rect.width(), 58};
  return QSize{150, 160};
}

} // namespace tb::ui
