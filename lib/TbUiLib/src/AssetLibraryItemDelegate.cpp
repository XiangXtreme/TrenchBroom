/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom, licensed under GPL version 3 or later.
*/
#include "ui/AssetLibraryItemDelegate.h"

#include "ui/AssetLibraryGlyphs.h"

#include <QApplication>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStyle>
#include <QStyleOptionViewItem>

#include <algorithm>
#include <array>
#include <cmath>

namespace tb::ui
{
namespace
{
constexpr auto CardMargin = 3;
constexpr auto CardRadius = 6.0;
constexpr auto ThumbRadius = 4.0;
constexpr auto CardInset = 8;
constexpr auto StatusDotRadius = 3.5;

QColor blend(const QColor& base, const QColor& over, const double amount)
{
  return QColor{
    int(base.red() + (over.red() - base.red()) * amount),
    int(base.green() + (over.green() - base.green()) * amount),
    int(base.blue() + (over.blue() - base.blue()) * amount)};
}

QColor withAlpha(QColor color, const int alpha)
{
  color.setAlpha(alpha);
  return color;
}

QColor themeAccent(const QPalette& palette)
{
  auto accent = palette.color(QPalette::Active, QPalette::Accent);
  if (!accent.isValid())
    accent = palette.color(QPalette::Active, QPalette::Highlight);
  return accent;
}

/**
 * Status colors used when the view does not carry the theme values. The values are
 * muted in light themes and lifted in dark ones so a small dot stays legible.
 */
QColor fallbackStatusColor(const int status, const QPalette& palette)
{
  const auto lightBackground =
    palette.color(QPalette::Active, QPalette::Window).lightnessF() > 0.5f;
  switch (status)
  {
  case AssetLibraryItemDelegate::Pending:
    return lightBackground ? QColor{0xa8, 0x7a, 0x1f} : QColor{0xe4, 0xb3, 0x5a};
  case AssetLibraryItemDelegate::Missing:
    return lightBackground ? QColor{0xa9, 0x3b, 0x3b} : QColor{0xe6, 0x7d, 0x7d};
  default:
    return lightBackground ? QColor{0x2f, 0x7d, 0x4f} : QColor{0x6c, 0xbe, 0x8b};
  }
}

QColor statusColorFor(const QStyleOptionViewItem& option, const int status)
{
  if (const auto* view = qobject_cast<const AssetLibraryListView*>(option.widget))
    return view->statusColor(status);
  return fallbackStatusColor(status, option.palette);
}

QString statusText(const int status)
{
  switch (status)
  {
  case AssetLibraryItemDelegate::Pending:
    return QObject::tr("Pending");
  case AssetLibraryItemDelegate::Missing:
    return QObject::tr("Missing");
  default:
    return QObject::tr("Ready");
  }
}

/** Sidebar glyph for a filter key; unknown keys fall back to the library stack. */
AssetLibraryGlyph glyphForFilter(const QString& filter)
{
  if (filter == QStringLiteral("texture") || filter == QStringLiteral("material"))
    return AssetLibraryGlyph::Texture;
  if (filter == QStringLiteral("model"))
    return AssetLibraryGlyph::Model;
  if (filter == QStringLiteral("sprite"))
    return AssetLibraryGlyph::Sprite;
  if (filter == QStringLiteral("sound"))
    return AssetLibraryGlyph::Sound;
  if (filter == QStringLiteral("skybox"))
    return AssetLibraryGlyph::Skybox;
  if (filter == QStringLiteral("pending"))
    return AssetLibraryGlyph::Pending;
  if (filter == QStringLiteral("missing"))
    return AssetLibraryGlyph::Missing;
  return AssetLibraryGlyph::All;
}

void drawStatusBadge(
  QPainter& painter,
  const QPointF& center,
  const int status,
  const QColor& color,
  const QColor& ringColor)
{
  painter.setPen(Qt::NoPen);
  painter.setBrush(ringColor);
  painter.drawEllipse(center, StatusDotRadius + 2.0, StatusDotRadius + 2.0);
  painter.setBrush(color);
  painter.drawEllipse(center, StatusDotRadius, StatusDotRadius);
  Q_UNUSED(status);
}
} // namespace

// ---------------------------------------------------------------------------------------
// AssetLibraryListView

AssetLibraryListView::AssetLibraryListView(QWidget* parent)
  : QListWidget{parent}
{
  // Hover feedback is painted by the delegates, so the view has to report mouse movement
  // without a button held down.
  setMouseTracking(true);
}

QColor AssetLibraryListView::readyColor() const
{
  return m_readyColor;
}

void AssetLibraryListView::setReadyColor(const QColor& color)
{
  m_readyColor = color;
  viewport()->update();
}

QColor AssetLibraryListView::pendingColor() const
{
  return m_pendingColor;
}

void AssetLibraryListView::setPendingColor(const QColor& color)
{
  m_pendingColor = color;
  viewport()->update();
}

QColor AssetLibraryListView::missingColor() const
{
  return m_missingColor;
}

void AssetLibraryListView::setMissingColor(const QColor& color)
{
  m_missingColor = color;
  viewport()->update();
}

QColor AssetLibraryListView::statusColor(const int status) const
{
  switch (status)
  {
  case AssetLibraryItemDelegate::Pending:
    if (m_pendingColor.isValid())
      return m_pendingColor;
    break;
  case AssetLibraryItemDelegate::Missing:
    if (m_missingColor.isValid())
      return m_missingColor;
    break;
  default:
    if (m_readyColor.isValid())
      return m_readyColor;
    break;
  }
  return fallbackStatusColor(status, palette());
}

// ---------------------------------------------------------------------------------------
// AssetLibraryItemDelegate

AssetLibraryItemDelegate::AssetLibraryItemDelegate(QObject* parent)
  : QStyledItemDelegate{parent}
{
}

void AssetLibraryItemDelegate::setListMode(const bool listMode)
{
  m_listMode = listMode;
}

bool AssetLibraryItemDelegate::listMode() const
{
  return m_listMode;
}

void AssetLibraryItemDelegate::setCellSize(const QSize& size)
{
  m_cellSize = size;
}

void AssetLibraryItemDelegate::paint(
  QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
  painter->save();
  painter->setRenderHint(QPainter::Antialiasing, true);
  painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
  if (m_listMode)
    paintList(painter, option, index);
  else
    paintGrid(painter, option, index);
  painter->restore();
}

void AssetLibraryItemDelegate::paintGrid(
  QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
  const auto& palette = option.palette;
  const auto selected = (option.state & QStyle::State_Selected) != 0;
  const auto hovered = (option.state & QStyle::State_MouseOver) != 0;
  const auto accent = themeAccent(palette);
  const auto base = palette.color(QPalette::Active, QPalette::AlternateBase);
  const auto border = palette.color(QPalette::Active, QPalette::Mid);

  const auto card = option.rect.adjusted(CardMargin, CardMargin, -CardMargin, -CardMargin);
  if (card.width() <= CardInset * 2 || card.height() <= CardInset * 2)
    return;

  const auto surface =
    selected ? blend(base, accent, 0.22)
    : hovered ? palette.color(QPalette::Active, QPalette::Midlight)
              : base;

  painter->setPen(QPen{
    selected ? withAlpha(accent, 210)
    : hovered ? border
              : withAlpha(border, 150),
    1.0});
  painter->setBrush(surface);
  painter->drawRoundedRect(QRectF{card}.adjusted(0.5, 0.5, -0.5, -0.5), CardRadius, CardRadius);

  auto nameFont = option.font;
  nameFont.setBold(true);
  const auto nameMetrics = QFontMetrics{nameFont};

  auto metaFont = option.font;
  metaFont.setPointSizeF(std::max(6.5, option.font.pointSizeF() * 0.88));
  const auto metaMetrics = QFontMetrics{metaFont};

  const auto inner = card.adjusted(CardInset, CardInset, -CardInset, -CardInset);
  const auto textTop = inner.bottom() - metaMetrics.height() - 2;
  const auto nameTop = textTop - nameMetrics.height() - 2;

  const auto thumbArea = QRect{inner.left(), inner.top(), inner.width(), nameTop - inner.top() - 6};
  const auto side = std::max(0, std::min(thumbArea.width(), thumbArea.height()));
  const auto thumbRect = QRect{
    thumbArea.left() + (thumbArea.width() - side) / 2,
    thumbArea.top() + (thumbArea.height() - side) / 2,
    side,
    side};

  paintThumbnail(painter, thumbRect, index, palette, false);

  const auto status = index.data(StatusRole).toInt();
  if (!thumbRect.isEmpty())
  {
    drawStatusBadge(
      *painter,
      QPointF{qreal(thumbRect.right()) - 5.0, qreal(thumbRect.top()) + 5.0},
      status,
      statusColorFor(option, status),
      surface);
  }

  painter->setFont(nameFont);
  painter->setPen(palette.color(QPalette::Active, QPalette::Text));
  const auto nameRect = QRect{inner.left(), nameTop, inner.width(), nameMetrics.height()};
  painter->drawText(
    nameRect,
    Qt::AlignHCenter | Qt::AlignTop,
    nameMetrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideMiddle, nameRect.width()));

  painter->setFont(metaFont);
  painter->setPen(palette.color(QPalette::Active, QPalette::PlaceholderText));
  const auto metaRect = QRect{inner.left(), textTop, inner.width(), metaMetrics.height()};
  painter->drawText(
    metaRect,
    Qt::AlignHCenter | Qt::AlignTop,
    metaMetrics.elidedText(index.data(TypeLabelRole).toString(), Qt::ElideRight, metaRect.width()));
}

void AssetLibraryItemDelegate::paintList(
  QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
  const auto& palette = option.palette;
  const auto selected = (option.state & QStyle::State_Selected) != 0;
  const auto hovered = (option.state & QStyle::State_MouseOver) != 0;
  const auto accent = themeAccent(palette);
  const auto base = palette.color(QPalette::Active, QPalette::AlternateBase);
  const auto border = palette.color(QPalette::Active, QPalette::Mid);

  const auto card = option.rect.adjusted(CardMargin, 2, -CardMargin, -2);
  if (card.width() <= 40 || card.height() <= 8)
    return;

  const auto surface =
    selected ? blend(base, accent, 0.22)
    : hovered ? palette.color(QPalette::Active, QPalette::Midlight)
              : base;

  painter->setPen(QPen{
    selected ? withAlpha(accent, 210)
    : hovered ? border
              : withAlpha(border, 130),
    1.0});
  painter->setBrush(surface);
  painter->drawRoundedRect(QRectF{card}.adjusted(0.5, 0.5, -0.5, -0.5), CardRadius, CardRadius);

  const auto side = std::max(0, card.height() - 10);
  const auto thumbRect = QRect{card.left() + 5, card.top() + 5, side, side};
  paintThumbnail(painter, thumbRect, index, palette, true);

  const auto status = index.data(StatusRole).toInt();
  const auto dotCenterX = qreal(card.right()) - 14.0;
  drawStatusBadge(
    *painter,
    QPointF{dotCenterX, qreal(card.center().y())},
    status,
    statusColorFor(option, status),
    surface);

  auto nameFont = option.font;
  nameFont.setBold(true);
  const auto nameMetrics = QFontMetrics{nameFont};

  auto metaFont = option.font;
  metaFont.setPointSizeF(std::max(6.5, option.font.pointSizeF() * 0.88));
  const auto metaMetrics = QFontMetrics{metaFont};

  const auto textLeft = thumbRect.right() + 9;
  const auto textWidth = std::max(0, int(dotCenterX) - 12 - textLeft);
  const auto textBlock = nameMetrics.height() + metaMetrics.height() + 2;
  const auto textTop = card.center().y() - textBlock / 2;

  painter->setFont(nameFont);
  painter->setPen(palette.color(QPalette::Active, QPalette::Text));
  const auto nameRect = QRect{textLeft, textTop, textWidth, nameMetrics.height()};
  painter->drawText(
    nameRect,
    Qt::AlignLeft | Qt::AlignVCenter,
    nameMetrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideMiddle, nameRect.width()));

  auto meta = index.data(TypeLabelRole).toString();
  if (const auto detail = index.data(DetailRole).toString(); !detail.isEmpty())
    meta = meta.isEmpty() ? detail : QStringLiteral("%1 · %2").arg(meta, detail);
  if (const auto statusLine = statusText(status);
      status == Pending || status == Missing)
    meta = meta.isEmpty() ? statusLine : QStringLiteral("%1 · %2").arg(meta, statusLine);

  painter->setFont(metaFont);
  painter->setPen(palette.color(QPalette::Active, QPalette::PlaceholderText));
  const auto metaRect = QRect{textLeft, textTop + nameMetrics.height() + 2, textWidth, metaMetrics.height()};
  painter->drawText(
    metaRect,
    Qt::AlignLeft | Qt::AlignVCenter,
    metaMetrics.elidedText(meta, Qt::ElideRight, metaRect.width()));
}

void AssetLibraryItemDelegate::paintThumbnail(
  QPainter* painter,
  const QRect& rect,
  const QModelIndex& index,
  const QPalette& palette,
  const bool compact) const
{
  if (rect.isEmpty())
    return;

  auto path = QPainterPath{};
  path.addRoundedRect(QRectF{rect}, ThumbRadius, ThumbRadius);
  const auto kind = index.data(KindRole).toInt();
  const auto kindColor = assetLibraryKindColor(kind, palette);

  painter->save();

  const auto pixmap = index.data(ThumbnailRole).value<QPixmap>();
  if (!pixmap.isNull())
  {
    painter->setClipPath(path);
    painter->fillRect(rect, palette.color(QPalette::Active, QPalette::Base));
    const auto scaled =
      pixmap.scaled(rect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    painter->drawPixmap(
      rect.left() + (rect.width() - scaled.width()) / 2,
      rect.top() + (rect.height() - scaled.height()) / 2,
      scaled);
    painter->restore();
    return;
  }

  // Assets without a decodable preview get a tinted tile carrying their kind glyph rather
  // than an empty square.
  painter->setPen(Qt::NoPen);
  painter->setBrush(withAlpha(kindColor, 26));
  painter->drawPath(path);

  const auto glyphSide = rect.height() * (compact ? 0.54 : 0.44);
  auto glyphRect = QRectF{0, 0, glyphSide, glyphSide};
  glyphRect.moveCenter(QRectF{rect}.center());
  paintAssetLibraryGlyph(
    *painter, glyphRect, static_cast<AssetLibraryGlyph>(kind), withAlpha(kindColor, 230));

  painter->restore();
}

QSize AssetLibraryItemDelegate::sizeHint(
  const QStyleOptionViewItem& option, const QModelIndex& index) const
{
  Q_UNUSED(index);
  if (m_listMode)
    return QSize{std::max(160, option.rect.width()), 54};
  return m_cellSize;
}

// ---------------------------------------------------------------------------------------
// AssetLibraryCategoryDelegate

AssetLibraryCategoryDelegate::AssetLibraryCategoryDelegate(QObject* parent)
  : QStyledItemDelegate{parent}
{
}

void AssetLibraryCategoryDelegate::paint(
  QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
  painter->save();
  painter->setRenderHint(QPainter::Antialiasing, true);

  if (index.data(SectionRole).toBool())
  {
    auto font = option.font;
    font.setBold(true);
    font.setPointSizeF(std::max(6.5, option.font.pointSizeF() * 0.82));
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
    painter->setFont(font);
    painter->setPen(option.palette.color(QPalette::Active, QPalette::PlaceholderText));
    painter->drawText(
      option.rect.adjusted(10, 0, -10, -2),
      Qt::AlignLeft | Qt::AlignBottom,
      index.data(Qt::DisplayRole).toString().toUpper());
    painter->restore();
    return;
  }

  auto itemOption = option;
  initStyleOption(&itemOption, index);
  itemOption.text.clear();
  itemOption.icon = QIcon{};

  const auto* style = itemOption.widget ? itemOption.widget->style() : QApplication::style();
  style->drawPrimitive(QStyle::PE_PanelItemViewItem, &itemOption, painter, itemOption.widget);

  const auto selected = (itemOption.state & QStyle::State_Selected) != 0;
  const auto textColor = itemOption.palette.color(
    QPalette::Active, selected ? QPalette::HighlightedText : QPalette::Text);
  const auto countColor = itemOption.palette.color(
    QPalette::Active,
    selected ? QPalette::HighlightedText : QPalette::PlaceholderText);

  auto rect = option.rect.adjusted(10, 0, -10, 0);
  const auto iconSide = 16;
  const auto iconRect = QRectF{
    qreal(rect.left()),
    qreal(rect.top() + (rect.height() - iconSide) / 2),
    qreal(iconSide),
    qreal(iconSide)};
  paintAssetLibraryGlyph(
    *painter, iconRect, glyphForFilter(index.data(FilterRole).toString()), textColor);

  auto font = option.font;
  font.setPointSizeF(std::max(7.0, option.font.pointSizeF() * 0.96));
  const auto metrics = QFontMetrics{font};
  painter->setFont(font);

  const auto count = index.data(CountRole).toInt();
  const auto countText = count >= 0 ? QString::number(count) : QString{};
  const auto countWidth = countText.isEmpty() ? 0 : metrics.horizontalAdvance(countText) + 10;

  const auto textLeft = rect.left() + iconSide + 9;
  const auto textRect = QRect{
    textLeft, rect.top(), std::max(0, rect.right() - countWidth - textLeft), rect.height()};
  painter->setPen(textColor);
  painter->drawText(
    textRect,
    Qt::AlignLeft | Qt::AlignVCenter,
    metrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, textRect.width()));

  if (!countText.isEmpty())
  {
    painter->setPen(countColor);
    painter->drawText(
      QRect{rect.right() - countWidth, rect.top(), countWidth, rect.height()},
      Qt::AlignRight | Qt::AlignVCenter,
      countText);
  }

  painter->restore();
}

QSize AssetLibraryCategoryDelegate::sizeHint(
  const QStyleOptionViewItem& option, const QModelIndex& index) const
{
  if (index.data(SectionRole).toBool())
    return QSize{option.rect.width(), 30};
  return QSize{option.rect.width(), 30};
}

// ---------------------------------------------------------------------------------------
// Kind colors

QColor assetLibraryKindColor(const int kind, const QPalette& palette)
{
  const auto accent = themeAccent(palette);
  auto hue = double(accent.hsvHueF());
  if (hue < 0.0)
    hue = 0.56; // Achromatic accents still need distinguishable kind tints.

  auto offset = 0.0;
  switch (static_cast<AssetLibraryGlyph>(kind))
  {
  case AssetLibraryGlyph::Model:
    offset = 0.10;
    break;
  case AssetLibraryGlyph::Sprite:
    offset = 0.21;
    break;
  case AssetLibraryGlyph::Sound:
    offset = 0.39;
    break;
  case AssetLibraryGlyph::Skybox:
    offset = 0.56;
    break;
  case AssetLibraryGlyph::Pending:
    offset = 0.09;
    break;
  case AssetLibraryGlyph::Missing:
    offset = 0.86;
    break;
  default:
    offset = 0.0;
    break;
  }

  hue = std::fmod(hue + offset, 1.0);
  const auto saturation = std::clamp(double(accent.hsvSaturationF()), 0.38, 0.92);
  const auto value = std::clamp(double(accent.valueF()), 0.45, 0.96);
  return QColor::fromHsvF(float(hue), float(saturation), float(value));
}

} // namespace tb::ui
