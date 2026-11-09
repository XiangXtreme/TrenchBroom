/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QApplication>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QtTest/QTest>

#include "ui/CollapsibleTitledPanel.h"
#include "ui/PanelLayout.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("PanelItemList")
{
  auto scroll = PanelList{};
  auto* list = new PanelItemList{4, 6};
  list->addItem("halflife.wad");
  scroll.addWidget(list);
  scroll.resize(320, 90);
  scroll.show();
  QApplication::processEvents();

  const auto rowHeight = list->sizeHintForRow(0);
  CHECK(list->viewport()->height() >= 4 * rowHeight);
  CHECK(scroll.verticalScrollBar()->maximum() > 0);
  CHECK(list->horizontalScrollBar()->maximum() == 0);

  for (auto i = 0; i < 10; ++i)
    list->addItem(QString{"materials_%1.wad"}.arg(i));
  QApplication::processEvents();
  CHECK(list->viewport()->height() >= 6 * rowHeight);
  CHECK(list->viewport()->height() <= 6 * rowHeight + 2);
  CHECK(list->verticalScrollBar()->maximum() > 0);

  auto largeFont = list->font();
  largeFont.setPointSize(22);
  list->setFont(largeFont);
  QApplication::processEvents();
  CHECK(list->viewport()->height() >= 6 * list->sizeHintForRow(0));
  const auto populatedHeight = list->height();

  list->clear();
  QApplication::processEvents();
  CHECK(list->height() < populatedHeight);
  CHECK(list->minimumSizeHint().height() >= 4 * list->fontMetrics().height());
}

TEST_CASE("PanelList")
{
  auto list = PanelList{};
  auto* first = new QLabel{"First"};
  auto* second = new QLabel{"Second"};
  auto* third = new QLabel{"Third"};
  list.addWidget(first);
  list.addWidget(second);
  list.insertWidget(1, third);
  CHECK(list.entries() == std::vector<QWidget*>{first, third, second});
  list.insertWidget(0, second);
  CHECK(list.entries() == std::vector<QWidget*>{second, first, third});

  auto retained = QPointer<QWidget>{first};
  list.removeWidget(first);
  first->hide();
  CHECK(retained);
  CHECK(first->parentWidget() == list.contentWidget());
  CHECK(list.entries() == std::vector<QWidget*>{second, third});
  list.addWidget(first);
  first->show();
  delete third;
  CHECK(list.entries() == std::vector<QWidget*>{second, first});

  list.resize(360, 600);
  list.show();
  REQUIRE(QTest::qWaitFor([&] { return first->isVisible(); }, 1000));
  CHECK(second->geometry().bottom() < first->geometry().top());
  CHECK(first->geometry().bottom() < list.viewport()->height() / 2);
  CHECK(list.horizontalScrollBar()->maximum() == 0);
  CHECK(list.verticalScrollBar()->maximum() == 0);
}

TEST_CASE("PanelLayout")
{
  auto list = PanelList{};
  auto* panel = new CollapsibleTitledPanel{"Terrain"};
  panel->setPresentation(CollapsibleTitledPanel::Presentation::Card);
  auto* column = panel->createContentLayout();
  auto* form = createPanelForm();
  auto* field = new QLineEdit{"16"};
  field->setMinimumWidth(100);
  form->addRow(QString::fromUtf8("地形最小面宽度 / Minimum face width"), field);
  form->addRow("Height", new QLineEdit{"128"});
  auto* button = new QPushButton{"Create terrain"};
  column->addLayout(form);
  column->addWidget(button);
  list.addWidget(panel);
  list.show();

  SECTION("fits dynamic content across narrow and wide containers")
  {
    for (auto cycle = 0; cycle < 8; ++cycle)
    {
      CAPTURE(cycle);
      const auto narrow = cycle % 2 == 0;
      list.resize(narrow ? 300 : 640, cycle % 3 == 0 ? 260 : 800);
      form->setRowVisible(1, cycle % 2 == 0);
      button->setVisible(cycle % 3 != 0);
      QApplication::processEvents();
      REQUIRE(QTest::qWaitFor(
        [&] {
          return list.horizontalScrollBar()->maximum() == 0
                 && form->geometry().isValid()
                 && panel->getPanel()->height() <= (column->hasHeightForWidth()
                      ? column->heightForWidth(panel->getPanel()->width()) : column->sizeHint().height());
        },
        1000));
      CHECK(field->width() >= field->minimumSizeHint().width());
      CHECK(field->geometry().right() <= form->geometry().right());
      auto* label = form->labelForField(field);
      REQUIRE(label);
      if (narrow)
        CHECK(field->geometry().top() > label->geometry().bottom());
      else
        CHECK(field->geometry().left() > label->geometry().right());
      if (button->isVisible())
      {
        const auto gap = button->geometry().top() - form->geometry().bottom();
        CHECK(gap > 0);
        CHECK(gap <= column->spacing() + 1);
      }
      panel->collapse();
      panel->expand();
    }
  }

  SECTION("scrolls growing content without clipping the last field")
  {
    for (auto row = 0; row < 20; ++row)
      form->addRow("Property", new QLineEdit{});
    list.resize(360, 260);
    REQUIRE(
      QTest::qWaitFor([&] { return list.verticalScrollBar()->maximum() > 0; }, 1000));
    list.ensureWidgetVisible(button);
    QApplication::processEvents();
    const auto position = button->mapTo(list.viewport(), QPoint{});
    CHECK(position.y() >= 0);
    CHECK(position.y() + button->height() <= list.viewport()->height());
    CHECK(list.horizontalScrollBar()->maximum() == 0);
  }
}

} // namespace tb::ui
