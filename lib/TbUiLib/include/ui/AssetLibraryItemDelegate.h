/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom, licensed under GPL version 3 or later.
*/
#pragma once

#include <QStyledItemDelegate>

class QPainter;
class QPalette;
class QStyleOptionViewItem;

namespace tb::ui
{

/**
 * Draws a library asset as a card: thumbnail, name and a type / state line.
 *
 * Colors come from the item view palette so every theme works without a second
 * stylesheet. The delegate only visualizes library data, it is not a restyled
 * foundational control.
 */
class AssetLibraryItemDelegate : public QStyledItemDelegate
{
  Q_OBJECT
public:
  enum Role
  {
    AssetIdRole = Qt::UserRole + 1,
    SubtitleRole,
    StatusRole,
    ThumbnailRole,
    GlyphRole,
  };

  enum Status
  {
    Ready = 0,
    Pending = 1,
    Missing = 2,
  };

  explicit AssetLibraryItemDelegate(QObject* parent = nullptr);

  void setListMode(bool listMode);

  void paint(
    QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index)
    const override;
  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index)
    const override;

private:
  void paintGrid(
    QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index)
    const;
  void paintList(
    QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index)
    const;
  void paintThumbnail(
    QPainter* painter, const QRect& rect, const QModelIndex& index, const QPalette& palette)
    const;
  void paintTexts(
    QPainter* painter,
    const QRect& rect,
    const QModelIndex& index,
    const QPalette& palette,
    Qt::Alignment alignment) const;

  bool m_listMode = false;
};

} // namespace tb::ui
