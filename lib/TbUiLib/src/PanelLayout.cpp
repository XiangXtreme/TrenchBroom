/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/PanelLayout.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include <algorithm>

namespace tb::ui
{
namespace
{
constexpr auto ContentPadding = 12;
constexpr auto ContentTopPadding = 8;
constexpr auto SectionGap = 8;
constexpr auto FormColumnGap = 10;
constexpr auto FormRowGap = 6;

void configureScrollArea(QScrollArea& scroll)
{
  scroll.setWidgetResizable(true);
  scroll.setFrameShape(QFrame::NoFrame);
  scroll.setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
}
} // namespace

void setPanelInsets(QLayout& layout, const PanelInsets insets)
{
  layout.setContentsMargins(
    insets == PanelInsets::Content
      ? QMargins{ContentPadding, ContentTopPadding, ContentPadding, ContentPadding}
      : QMargins{});
}

void configurePanelForm(QFormLayout& form, const PanelInsets insets)
{
  setPanelInsets(form, insets);
  form.setHorizontalSpacing(FormColumnGap);
  form.setVerticalSpacing(FormRowGap);
  form.setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
  form.setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  form.setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  form.setRowWrapPolicy(QFormLayout::WrapLongRows);
}

QFormLayout* createPanelForm(QWidget* parent, const PanelInsets insets)
{
  auto* form = new QFormLayout{parent};
  configurePanelForm(*form, insets);
  return form;
}

QVBoxLayout* createPanelColumn(QWidget* parent, const PanelInsets insets)
{
  auto* column = new QVBoxLayout{parent};
  setPanelInsets(*column, insets);
  column->setSpacing(SectionGap);
  return column;
}

QHBoxLayout* createPanelActions(QWidget* parent)
{
  auto* row = new QHBoxLayout{parent};
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(FormRowGap);
  return row;
}

QScrollArea* createPanelScrollArea(QWidget* content, QWidget* parent)
{
  auto* scroll = new QScrollArea{parent};
  configureScrollArea(*scroll);
  scroll->setWidget(content);
  return scroll;
}

PanelList::PanelList(QWidget* parent)
  : QScrollArea{parent}
{
  configureScrollArea(*this);
  auto* content = new QWidget{};
  content->setAttribute(Qt::WA_StyledBackground);
  m_entries = new QVBoxLayout{content};
  m_entries->setContentsMargins(SectionGap, SectionGap, SectionGap, ContentPadding);
  m_entries->setSpacing(SectionGap);
  m_entries->addStretch(1);
  setWidget(content);
}

QWidget* PanelList::contentWidget() const
{
  return widget();
}

std::vector<QWidget*> PanelList::entries() const
{
  auto result = std::vector<QWidget*>{};
  for (auto i = 0; i < m_entries->count() - 1; ++i)
    result.push_back(m_entries->itemAt(i)->widget());
  return result;
}

void PanelList::addWidget(QWidget* entry)
{
  insertWidget(m_entries->count() - 1, entry);
}

void PanelList::insertWidget(const int index, QWidget* entry)
{
  m_entries->removeWidget(entry);
  m_entries->insertWidget(std::clamp(index, 0, m_entries->count() - 1), entry);
}

void PanelList::removeWidget(QWidget* entry)
{
  m_entries->removeWidget(entry);
}

} // namespace tb::ui
