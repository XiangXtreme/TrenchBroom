/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom, licensed under GPL version 3 or later.
*/
#pragma once

#include <QColor>
#include <QListWidget>
#include <QSize>
#include <QStyledItemDelegate>

class QPainter;
class QPalette;
class QStyleOptionViewItem;

namespace tb::ui
{

/**
 * Item view used by the asset library browser.
 *
 * It behaves like a plain QListWidget. The only addition are the state colors which the
 * delegates use to visualize library data; they are assigned from theme tokens in
 * base.qss (`qproperty-readyColor` and friends), so feature code never hardcodes a
 * theme color.
 */
class AssetLibraryListView : public QListWidget
{
  Q_OBJECT
  Q_PROPERTY(QColor readyColor READ readyColor WRITE setReadyColor)
  Q_PROPERTY(QColor pendingColor READ pendingColor WRITE setPendingColor)
  Q_PROPERTY(QColor missingColor READ missingColor WRITE setMissingColor)

  QColor m_readyColor;
  QColor m_pendingColor;
  QColor m_missingColor;

public:
  explicit AssetLibraryListView(QWidget* parent = nullptr);

  QColor readyColor() const;
  void setReadyColor(const QColor& color);
  QColor pendingColor() const;
  void setPendingColor(const QColor& color);
  QColor missingColor() const;
  void setMissingColor(const QColor& color);

  /** Resolves the color for an AssetLibraryItemDelegate::Status value. */
  QColor statusColor(int status) const;
};

/**
 * Draws a library asset as a card: thumbnail, type badge, name and a detail line.
 *
 * Colors come from the item view palette and the AssetLibraryListView state colors, so
 * every theme works without a second stylesheet. The delegate only visualizes library
 * data, it is not a restyled foundational control.
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
    /** int(AssetLibraryGlyph) describing the asset kind. */
    KindRole,
    PathRole,
    /** Short secondary value for list mode, for example the file size. */
    DetailRole,
    TypeLabelRole,
  };

  enum Status
  {
    Ready = 0,
    Pending = 1,
    Missing = 2,
  };

  explicit AssetLibraryItemDelegate(QObject* parent = nullptr);

  void setListMode(bool listMode);
  bool listMode() const;
  /** Size of one grid cell including the card margin. */
  void setCellSize(const QSize& size);

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
    QPainter* painter,
    const QRect& rect,
    const QModelIndex& index,
    const QPalette& palette,
    bool compact) const;

  bool m_listMode = false;
  QSize m_cellSize = QSize{136, 180};
};

/**
 * Sidebar rows of the asset library: a section caption, or a filter with an icon and a
 * right aligned count. Selection, hover and focus use the canonical item view painting.
 */
class AssetLibraryCategoryDelegate : public QStyledItemDelegate
{
  Q_OBJECT
public:
  enum Role
  {
    FilterRole = Qt::UserRole,
    CountRole = Qt::UserRole + 10,
    SectionRole,
  };

  explicit AssetLibraryCategoryDelegate(QObject* parent = nullptr);

  void paint(
    QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index)
    const override;
  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index)
    const override;
};

/** Accent tint for an asset kind, derived from the theme accent. */
QColor assetLibraryKindColor(int kind, const QPalette& palette);

} // namespace tb::ui
