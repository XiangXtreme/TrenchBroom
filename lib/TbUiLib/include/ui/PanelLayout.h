/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <QListWidget>
#include <QScrollArea>

#include <vector>

class QFormLayout;
class QHBoxLayout;
class QVBoxLayout;

namespace tb::ui
{

enum class PanelInsets
{
  None,
  Content,
  Toolbar
};

// Standard Qt layouts with shared defaults. Feature code adds content and may
// constrain widths for its data without defining another visual implementation.
void setPanelInsets(QLayout& layout, PanelInsets insets);
void configurePanelForm(QFormLayout& form, PanelInsets insets = PanelInsets::None);
QFormLayout* createPanelForm(
  QWidget* parent = nullptr, PanelInsets insets = PanelInsets::None);
QVBoxLayout* createPanelColumn(
  QWidget* parent = nullptr, PanelInsets insets = PanelInsets::Content);
QHBoxLayout* createPanelActions(QWidget* parent = nullptr);
QScrollArea* createPanelScrollArea(QWidget* content, QWidget* parent = nullptr);

// A bounded list inside a form: retain complete rows instead of collapsing to
// QAbstractScrollArea's tiny minimum. Extra items scroll within the list.
class PanelItemList : public QListWidget
{
private:
  int m_minimumRows;
  int m_maximumRows;

public:
  explicit PanelItemList(int minimumRows, int maximumRows, QWidget* parent = nullptr);
  QSize minimumSizeHint() const override;
  QSize sizeHint() const override;

protected:
  bool event(QEvent* event) override;
};

// Keeps surplus space below the entries. Removing an entry does not delete or
// reparent it; its owner may reuse it or destroy it explicitly.
class PanelList : public QScrollArea
{
private:
  QVBoxLayout* m_entries;

public:
  explicit PanelList(QWidget* parent = nullptr);

  QWidget* contentWidget() const;
  std::vector<QWidget*> entries() const;
  void addWidget(QWidget* widget);
  void insertWidget(int index, QWidget* widget);
  void removeWidget(QWidget* widget);
  void setContentVisible(QWidget* content, bool visible);
};

} // namespace tb::ui
