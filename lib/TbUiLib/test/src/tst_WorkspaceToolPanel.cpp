/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QApplication>
#include <QComboBox>
#include <QMouseEvent>
#include <QProxyStyle>
#include <QScopeGuard>
#include <QSettings>
#include <QStyleFactory>
#include <QVariantAnimation>
#include <QtTest/QTest>

#include "ui/WorkspaceToolPanel.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{
class DrawerTestStyle : public QProxyStyle
{
public:
  int duration = 200;
  DrawerTestStyle()
    : QProxyStyle{QStyleFactory::create("Fusion")}
  {
  }
  int styleHint(
    StyleHint hint,
    const QStyleOption* option,
    const QWidget* widget,
    QStyleHintReturn* result) const override
  {
    return hint == SH_Widget_Animation_Duration
             ? duration
             : QProxyStyle::styleHint(hint, option, widget, result);
  }
};
} // namespace

TEST_CASE("WorkspaceToolPanel")
{
  auto settings = QSettings{};
  const auto previous = loadToolLayout(settings, WorkspaceKind::Map);
  const auto restore =
    qScopeGuard([&]() { saveToolLayout(settings, WorkspaceKind::Map, previous); });
  saveToolLayout(settings, WorkspaceKind::Map, ToolLayout{});
  auto* workspace = new QWidget{};
  auto* content = new QWidget{};
  auto tools = WorkspaceToolPanel{WorkspaceKind::Map, workspace, content};
  auto* style = new DrawerTestStyle{};
  style->setParent(&tools);
  tools.setStyle(style);
  tools.addStatusWidget(new QWidget{});
  tools.resize(1000, 700);
  tools.show();
  QApplication::processEvents();
  auto* drawer = tools.findChild<QWidget*>("ProjectToolDrawer");
  auto* bar = tools.findChild<QWidget*>("ProjectToolBar");
  auto* animation = tools.findChild<QVariantAnimation*>("WorkspaceDrawerAnimation");
  REQUIRE(drawer);
  REQUIRE(bar);
  REQUIRE(animation);

  for (const auto pinned : {false, true})
  {
    DYNAMIC_SECTION((pinned ? "pushed drawer motion" : "overlay drawer motion"))
    {
      tools.findChild<QComboBox*>("ProjectToolsMode")->setCurrentIndex(pinned ? 1 : 0);
      QApplication::processEvents();
      const auto collapsed = drawer->height();
      const auto workspaceSize = workspace->size();
      const auto barParent = bar->parentWidget();
      tools.showContent();
      REQUIRE(tools.isAnimating());
      animation->setCurrentTime(80);
      const auto intermediate = drawer->height();
      CHECK(intermediate > collapsed);
      if (!pinned)
        CHECK(workspace->size() == workspaceSize);
      else
        CHECK(workspace->height() < workspaceSize.height());
      CHECK(bar->parentWidget() == barParent);
      CHECK(bar->mapTo(drawer, QPoint{}).y() <= 2);
      // Reverse an unfinished transition without jumping to either endpoint.
      tools.toggle();
      CHECK(drawer->height() == intermediate);
      animation->setCurrentTime(animation->duration());
      CHECK_FALSE(tools.isAnimating());
      CHECK(drawer->height() == collapsed);
      CHECK_FALSE(content->isVisible());
      tools.showContent();
      QTRY_VERIFY_WITH_TIMEOUT(!tools.isAnimating(), 1000);
      CHECK(content->isVisible());
      CHECK(drawer->height() > intermediate);
      tools.toggle();
      animation->setCurrentTime(animation->duration());
      CHECK(drawer->height() == collapsed);
      CHECK(workspace->size() == workspaceSize);
      CHECK(drawer->geometry().bottom() == drawer->parentWidget()->height() - 1);
    }
  }

  SECTION("drawer height handle resizes overlay without moving the map")
  {
    tools.showContent();
    animation->setCurrentTime(animation->duration());
    auto* handle = tools.findChild<QWidget*>("WorkspaceDrawerResize");
    REQUIRE(handle);
    REQUIRE(handle->isVisible());
    const auto workspaceSize = workspace->size();
    const auto before = drawer->height();
    const auto local = QPointF{handle->rect().center()};
    const auto global = QPointF{handle->mapToGlobal(local.toPoint())};
    auto press = QMouseEvent{
      QEvent::MouseButtonPress,
      local,
      global,
      Qt::LeftButton,
      Qt::LeftButton,
      Qt::NoModifier};
    QApplication::sendEvent(handle, &press);
    auto move = QMouseEvent{
      QEvent::MouseMove,
      local - QPointF{0, 70},
      global - QPointF{0, 70},
      Qt::NoButton,
      Qt::LeftButton,
      Qt::NoModifier};
    QApplication::sendEvent(handle, &move);
    CHECK(drawer->height() > before + 60);
    CHECK(workspace->size() == workspaceSize);
    auto release = QMouseEvent{
      QEvent::MouseButtonRelease,
      local,
      global - QPointF{0, 70},
      Qt::LeftButton,
      Qt::NoButton,
      Qt::NoModifier};
    QApplication::sendEvent(handle, &release);
    CHECK(QWidget::mouseGrabber() != handle);
    CHECK(
      loadToolLayout(settings, WorkspaceKind::Map).heightRatio
      > ToolLayout{}.heightRatio);
    tools.toggle();
    animation->setCurrentTime(animation->duration());
    CHECK_FALSE(handle->isVisible());
  }

  SECTION("disabled animations settle immediately")
  {
    style->duration = 0;
    const auto collapsed = drawer->height();
    tools.showContent();
    CHECK_FALSE(tools.isAnimating());
    CHECK(drawer->height() > collapsed);
    tools.toggle();
    CHECK_FALSE(tools.isAnimating());
    CHECK(drawer->height() == collapsed);
    CHECK_FALSE(content->isVisible());
  }
}
} // namespace tb::ui
