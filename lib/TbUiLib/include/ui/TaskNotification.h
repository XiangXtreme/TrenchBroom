/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <QFrame>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

class QWidget;

namespace tb::ui
{

struct TaskNotification
{
  enum class State
  {
    Running,
    Succeeded,
    Failed
  };
  struct Action
  {
    QString text;
    std::function<void()> invoke;
  };

  State state = State::Running;
  QString title;
  QString detail;
  // -2 hides the bar; -1 is indeterminate; 0..100 is stage progress.
  int progress = -2;
  std::vector<Action> actions;
  bool dismissible = false;
  int showDelayMs = 0;
  int expireAfterMs = 3000;
};

/** A themed task card, also usable outside the overlay host. */
class TaskNotificationCard : public QFrame
{
  Q_OBJECT
public:
  explicit TaskNotificationCard(QWidget* parent = nullptr);
  void setNotification(const TaskNotification& notification);

signals:
  void dismissRequested();

private:
  class Impl;
  std::unique_ptr<Impl> m_impl;

public:
  ~TaskNotificationCard() override;
};

/** Places independent cards over a host without covering the area between them. */
class TaskNotificationCenter : public QObject
{
  Q_OBJECT
public:
  explicit TaskNotificationCenter(QWidget* host);
  ~TaskNotificationCenter() override;

  void update(const QString& id, const TaskNotification& notification);
  void remove(const QString& id);

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  class Impl;
  std::unique_ptr<Impl> m_impl;
  void reposition();
};
} // namespace tb::ui
