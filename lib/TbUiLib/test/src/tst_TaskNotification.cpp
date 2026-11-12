/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QApplication>
#include <QLineEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QToolButton>
#include <QtTest/QTest>

#include "ui/AppController.h"
#include "ui/AppControllerFixture.h"
#include "ui/TaskNotification.h"
#include "ui/UpdateNotifications.h"
#include "update/Updater.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
TEST_CASE("TaskNotification")
{
  auto host = QWidget{};
  host.resize(800, 600);
  auto* edit = new QLineEdit{&host};
  edit->move(10, 10);
  host.show();
  edit->setFocus();
  if (qEnvironmentVariableIntValue("TB_TEST_SHOW_WINDOWS") == 1)
  {
    host.activateWindow();
    REQUIRE(QTest::qWaitFor([&] { return host.isActiveWindow(); }, 3000));
    edit->setFocus();
    REQUIRE(QTest::qWaitFor([&] { return edit->hasFocus(); }, 3000));
  }
  auto center = TaskNotificationCenter{&host};
  auto task = TaskNotification{};
  task.title = "Compiling";
  task.detail = "VIS";
  task.progress = 42;

  SECTION("coalesces updates and places cards without covering the host")
  {
    auto invoked = false;
    task.actions.push_back({"Cancel", [&] { invoked = true; }});
    auto* previousFocus = QApplication::focusWidget();
    center.update("compile", task);
    auto* card = host.findChild<TaskNotificationCard*>("TaskNotificationCard_compile");
    REQUIRE(card);
    CHECK_FALSE(card->isHidden());
    CHECK(host.childAt(5, 5) != card);
    CHECK(QApplication::focusWidget() == previousFocus);
    REQUIRE(card->findChild<QProgressBar*>());
    CHECK(card->findChild<QProgressBar*>()->value() == 42);
    task.progress = 65;
    center.update("compile", task);
    CHECK(host.findChildren<TaskNotificationCard*>().size() == 1);
    CHECK(card->findChild<QProgressBar*>()->value() == 65);
    card->findChild<QPushButton*>()->click();
    CHECK(invoked);

    center.update("assets", task);
    auto* other = host.findChild<TaskNotificationCard*>("TaskNotificationCard_assets");
    REQUIRE(other);
    CHECK(card->geometry().bottom() < other->geometry().top());
    host.resize(640, 480);
    QApplication::processEvents();
    CHECK(other->geometry().right() == host.width() - 13);
    CHECK(other->geometry().bottom() == host.height() - 13);
  }

  SECTION("long titles preserve the dismiss control")
  {
    task.title = QString(400, QLatin1Char('W'));
    task.dismissible = true;
    center.update("long", task);
    auto* card = host.findChild<TaskNotificationCard*>("TaskNotificationCard_long");
    REQUIRE(card);
    auto* dismiss = card->findChild<QToolButton*>("TaskNotificationDismiss");
    REQUIRE(dismiss);
    QApplication::processEvents();
    CHECK(card->width() <= 360);
    CHECK(card->rect().contains(dismiss->geometry()));
  }

  SECTION("quiet tasks do not flash when they finish before the delay")
  {
    task.showDelayMs = 150;
    center.update("quiet", task);
    auto* card = host.findChild<TaskNotificationCard*>("TaskNotificationCard_quiet");
    REQUIRE(card);
    CHECK(card->isHidden());
    QTest::qWait(30);
    center.update("quiet", task);
    task.state = TaskNotification::State::Succeeded;
    center.update("quiet", task);
    QTest::qWait(180);
    CHECK(host.findChild<TaskNotificationCard*>("TaskNotificationCard_quiet") == nullptr);
  }

  SECTION("persistent results appear even when the task finishes quickly")
  {
    task.showDelayMs = 1000;
    center.update("update", task);
    task.state = TaskNotification::State::Succeeded;
    task.expireAfterMs = -1;
    center.update("update", task);
    auto* card = host.findChild<TaskNotificationCard*>("TaskNotificationCard_update");
    REQUIRE(card);
    CHECK_FALSE(card->isHidden());
  }

  SECTION("centers in the same window keep their cards separate")
  {
    auto child = QWidget{&host};
    child.setGeometry(20, 20, 760, 560);
    child.show();
    auto childCenter = TaskNotificationCenter{&child};
    center.update("project", task);
    childCenter.update("map", task);
    auto* project = host.findChild<TaskNotificationCard*>("TaskNotificationCard_project");
    auto* map = child.findChild<TaskNotificationCard*>("TaskNotificationCard_map");
    REQUIRE(project);
    REQUIRE(map);
    const auto mapBounds = QRect{child.mapTo(&host, map->pos()), map->size()};
    CHECK_FALSE(project->geometry().intersects(mapBounds));
    center.remove("project");
    CHECK(map->geometry().bottom() == child.height() - 13);
  }

  SECTION("failures appear immediately and persist until dismissed")
  {
    task.showDelayMs = 200;
    center.update("failure", task);
    task.state = TaskNotification::State::Failed;
    task.dismissible = true;
    task.progress = -2;
    center.update("failure", task);
    auto* card = host.findChild<TaskNotificationCard*>("TaskNotificationCard_failure");
    REQUIRE(card);
    CHECK_FALSE(card->isHidden());
    QTest::qWait(250);
    CHECK_FALSE(card->isHidden());
    card->findChild<QToolButton*>("TaskNotificationDismiss")->click();
    CHECK(card->isHidden());
  }

  SECTION("completion expires and an immediate update can reveal delayed work")
  {
    task.showDelayMs = 1000;
    center.update("task", task);
    auto card = QPointer<TaskNotificationCard>{
      host.findChild<TaskNotificationCard*>("TaskNotificationCard_task")};
    REQUIRE(card);
    CHECK(card->isHidden());
    task.showDelayMs = 0;
    center.update("task", task);
    CHECK_FALSE(card->isHidden());
    task.state = TaskNotification::State::Succeeded;
    task.expireAfterMs = 20;
    center.update("task", task);
    REQUIRE(QTest::qWaitFor([&] { return card.isNull(); }, 2000));
  }

  SECTION("a failure cancels a completion fade")
  {
    center.update("task", task);
    task.state = TaskNotification::State::Succeeded;
    task.expireAfterMs = 10;
    center.update("task", task);
    QTest::qWait(50);
    task.state = TaskNotification::State::Failed;
    task.dismissible = true;
    center.update("task", task);
    QTest::qWait(350);
    auto* card = host.findChild<TaskNotificationCard*>("TaskNotificationCard_task");
    REQUIRE(card);
    CHECK_FALSE(card->isHidden());
  }

  SECTION("destroying the center removes its cards")
  {
    auto* otherCenter = new TaskNotificationCenter{&host};
    otherCenter->update("owned", task);
    REQUIRE(host.findChild<TaskNotificationCard*>("TaskNotificationCard_owned"));
    delete otherCenter;
    CHECK(host.findChild<TaskNotificationCard*>("TaskNotificationCard_owned") == nullptr);
  }
}

TEST_CASE("UpdateNotifications")
{
  auto fixture = AppControllerFixture{};
  auto& updater = fixture.appController().updater();
  auto host = QWidget{};
  host.resize(800, 600);
  host.show();
  auto center = TaskNotificationCenter{&host};
  updateUpdateNotification(center, updater, upd::CheckingForUpdatesState{nullptr});
  auto* card = host.findChild<TaskNotificationCard*>("TaskNotificationCard_update");
  REQUIRE(card);
  CHECK(card->isHidden());
  auto available = upd::UpdateAvailableState{};
  available.updateInfo.updateVersion = "2.0";
  updateUpdateNotification(center, updater, available);
  CHECK_FALSE(card->isHidden());
  REQUIRE(card->findChild<QPushButton*>());
  CHECK(card->findChild<QPushButton*>()->text() == "View update");

  updateUpdateNotification(center, updater, upd::UpdateErrorState{"Network error"});
  auto buttons = card->findChildren<QPushButton*>();
  REQUIRE(buttons.size() == 2);
  CHECK(buttons[1]->text() == "Retry");
  updateUpdateNotification(center, updater, upd::IdleState{});
  CHECK(card->isHidden());

  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  updater.checkForUpdates(true);
  updateUpdateNotification(center, updater, upd::CheckingForUpdatesState{nullptr});
  card = host.findChild<TaskNotificationCard*>("TaskNotificationCard_update");
  REQUIRE(card);
  CHECK_FALSE(card->isHidden());
  updateUpdateNotification(center, updater, upd::UpToDateState{"2.0"});
  CHECK_FALSE(card->isHidden());
}
} // namespace tb::ui
