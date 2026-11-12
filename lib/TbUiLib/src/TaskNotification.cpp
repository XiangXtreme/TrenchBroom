/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/TaskNotification.h"

#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "ui/ElidedLabel.h"

#include <algorithm>
#include <map>
#include <utility>

namespace tb::ui
{

namespace
{
std::vector<TaskNotificationCenter*>& notificationCenters()
{
  static auto centers = std::vector<TaskNotificationCenter*>{};
  return centers;
}
} // namespace

class TaskNotificationCard::Impl
{
public:
  TaskNotification notification;
  ElidedLabel* title = nullptr;
  ElidedLabel* detail = nullptr;
  QProgressBar* progress = nullptr;
  QToolButton* dismiss = nullptr;
  QHBoxLayout* actions = nullptr;
  std::vector<QPushButton*> buttons;
};

TaskNotificationCard::TaskNotificationCard(QWidget* parent)
  : QFrame{parent}
  , m_impl{std::make_unique<Impl>()}
{
  setObjectName("TaskNotificationCard");
  setProperty("tbPanelStyle", "card");
  setAttribute(Qt::WA_StyledBackground);
  setFocusPolicy(Qt::NoFocus);

  auto* layout = new QVBoxLayout{this};
  layout->setContentsMargins(12, 10, 12, 10);
  layout->setSpacing(6);
  auto* heading = new QHBoxLayout{};
  m_impl->title = new ElidedLabel{Qt::ElideRight, this};
  auto titleFont = m_impl->title->font();
  titleFont.setBold(true);
  m_impl->title->setFont(titleFont);
  heading->addWidget(m_impl->title, 1);
  m_impl->dismiss = new QToolButton{this};
  m_impl->dismiss->setObjectName("TaskNotificationDismiss");
  m_impl->dismiss->setProperty("tbControlRole", "compact");
  m_impl->dismiss->setText(QStringLiteral("×"));
  m_impl->dismiss->setAccessibleName(tr("Dismiss notification"));
  m_impl->dismiss->setToolTip(tr("Dismiss notification"));
  connect(
    m_impl->dismiss,
    &QToolButton::clicked,
    this,
    &TaskNotificationCard::dismissRequested);
  heading->addWidget(m_impl->dismiss);
  layout->addLayout(heading);
  m_impl->detail = new ElidedLabel{Qt::ElideRight, this};
  m_impl->detail->setProperty("tbTextRole", "secondary");
  layout->addWidget(m_impl->detail);
  m_impl->progress = new QProgressBar{this};
  m_impl->progress->setObjectName("TaskNotificationProgress");
  m_impl->progress->setProperty("tbControlRole", "compact");
  m_impl->progress->setTextVisible(false);
  layout->addWidget(m_impl->progress);
  m_impl->actions = new QHBoxLayout{};
  m_impl->actions->setSpacing(6);
  m_impl->actions->addStretch(1);
  layout->addLayout(m_impl->actions);
}

TaskNotificationCard::~TaskNotificationCard() = default;

void TaskNotificationCard::setNotification(const TaskNotification& notification)
{
  m_impl->notification = notification;
  m_impl->title->setText(notification.title);
  m_impl->title->setAccessibleName(notification.title);
  m_impl->progress->setAccessibleName(notification.title);
  m_impl->detail->setText(notification.detail);
  m_impl->detail->setVisible(!notification.detail.isEmpty());
  m_impl->progress->setVisible(notification.progress >= -1);
  if (notification.progress == -1)
    m_impl->progress->setRange(0, 0);
  else if (notification.progress >= 0)
  {
    m_impl->progress->setRange(0, 100);
    m_impl->progress->setValue(std::clamp(notification.progress, 0, 100));
  }
  m_impl->dismiss->setVisible(notification.dismissible);
  while (m_impl->buttons.size() < notification.actions.size())
  {
    const auto index = m_impl->buttons.size();
    auto* button = new QPushButton{this};
    button->setProperty("tbControlRole", "compact");
    m_impl->actions->insertWidget(static_cast<int>(index), button);
    connect(button, &QPushButton::clicked, this, [this, index] {
      if (index < m_impl->notification.actions.size())
      {
        auto invoke = m_impl->notification.actions[index].invoke;
        if (invoke)
          invoke();
      }
    });
    m_impl->buttons.push_back(button);
  }
  for (size_t i = 0; i < m_impl->buttons.size(); ++i)
  {
    const auto visible = i < notification.actions.size();
    m_impl->buttons[i]->setVisible(visible);
    if (visible)
      m_impl->buttons[i]->setText(notification.actions[i].text);
  }
  updateGeometry();
}

class TaskNotificationCenter::Impl
{
public:
  struct Entry
  {
    TaskNotificationCard* card;
    QTimer* showTimer;
    QTimer* expiryTimer;
    QPropertyAnimation* fade;
    TaskNotification::State state;
  };
  QWidget* host;
  std::map<QString, Entry> entries;
  std::vector<QString> order;
};

TaskNotificationCenter::TaskNotificationCenter(QWidget* host)
  : QObject{host}
  , m_impl{std::make_unique<Impl>()}
{
  m_impl->host = host;
  notificationCenters().push_back(this);
  host->installEventFilter(this);
}

TaskNotificationCenter::~TaskNotificationCenter()
{
  std::erase(notificationCenters(), this);
  for (const auto& entry : m_impl->entries)
    delete entry.second.card;
  reposition();
}

void TaskNotificationCenter::update(
  const QString& id, const TaskNotification& notification)
{
  auto it = m_impl->entries.find(id);
  const auto created = it == m_impl->entries.end();
  if (created)
  {
    auto* card = new TaskNotificationCard{m_impl->host};
    card->setObjectName("TaskNotificationCard_" + id);
    card->hide();
    auto* showTimer = new QTimer{card};
    auto* expiryTimer = new QTimer{card};
    showTimer->setSingleShot(true);
    expiryTimer->setSingleShot(true);
    auto* opacity = new QGraphicsOpacityEffect{card};
    opacity->setOpacity(1.0);
    card->setGraphicsEffect(opacity);
    auto* fade = new QPropertyAnimation{opacity, "opacity", card};
    fade->setDuration(250);
    fade->setStartValue(1.0);
    fade->setEndValue(0.0);
    it =
      m_impl->entries
        .emplace(id, Impl::Entry{card, showTimer, expiryTimer, fade, notification.state})
        .first;
    m_impl->order.push_back(id);
    connect(
      card, &TaskNotificationCard::dismissRequested, this, [this, id] { remove(id); });
    connect(showTimer, &QTimer::timeout, this, [this, id] {
      const auto found = m_impl->entries.find(id);
      if (found == m_impl->entries.end())
        return;
      found->second.card->show();
      reposition();
    });
    connect(expiryTimer, &QTimer::timeout, fade, [fade] { fade->start(); });
    connect(fade, &QPropertyAnimation::finished, this, [this, id] { remove(id); });
  }
  auto& entry = it->second;
  const auto previous = entry.state;
  entry.state = notification.state;
  entry.card->setNotification(notification);

  if (notification.state == TaskNotification::State::Running)
  {
    entry.expiryTimer->stop();
    entry.fade->stop();
    static_cast<QGraphicsOpacityEffect*>(entry.card->graphicsEffect())->setOpacity(1.0);
    if (created && notification.showDelayMs > 0)
      entry.showTimer->start(notification.showDelayMs);
    else if (created || (entry.showTimer->isActive() && notification.showDelayMs <= 0))
    {
      entry.showTimer->stop();
      entry.card->show();
    }
  }
  else if (created || previous != notification.state)
  {
    entry.expiryTimer->stop();
    entry.fade->stop();
    static_cast<QGraphicsOpacityEffect*>(entry.card->graphicsEffect())->setOpacity(1.0);
    if (
      notification.state == TaskNotification::State::Succeeded
      && entry.showTimer->isActive() && notification.expireAfterMs >= 0)
    {
      remove(id);
      return;
    }
    entry.showTimer->stop();
    entry.card->show();
    if (
      notification.state == TaskNotification::State::Succeeded
      && notification.expireAfterMs >= 0)
      entry.expiryTimer->start(notification.expireAfterMs);
  }
  reposition();
}

void TaskNotificationCenter::remove(const QString& id)
{
  const auto it = m_impl->entries.find(id);
  if (it == m_impl->entries.end())
    return;
  it->second.showTimer->stop();
  it->second.expiryTimer->stop();
  it->second.fade->stop();
  it->second.card->hide();
  it->second.card->deleteLater();
  m_impl->entries.erase(it);
  std::erase(m_impl->order, id);
  reposition();
}

bool TaskNotificationCenter::eventFilter(QObject* watched, QEvent* event)
{
  if (
    watched == m_impl->host
    && (event->type() == QEvent::Resize || event->type() == QEvent::Move
        || event->type() == QEvent::Show || event->type() == QEvent::Hide
        || event->type() == QEvent::ParentChange))
    reposition();
  return QObject::eventFilter(watched, event);
}

void TaskNotificationCenter::reposition()
{
  constexpr auto margin = 12;
  constexpr auto spacing = 8;
  auto occupied = std::map<QWidget*, std::vector<QRect>>{};
  for (auto* center : notificationCenters())
  {
    auto* host = center->m_impl->host;
    if (!host->isVisible())
      continue;
    auto* window = host->window();
    const auto origin = host->mapTo(window, QPoint{});
    const auto width = std::max(1, std::min(360, host->width() - 2 * margin));
    const auto x = std::max(0, host->width() - width - margin);
    auto y = host->height() - margin;
    auto& windowOccupied = occupied[window];
    for (auto it = center->m_impl->order.rbegin(); it != center->m_impl->order.rend();
         ++it)
    {
      auto* card = center->m_impl->entries.at(*it).card;
      if (card->isHidden())
        continue;
      card->setFixedWidth(width);
      card->adjustSize();
      y -= card->height();
      auto bounds = QRect{origin + QPoint{x, y}, card->size()};
      for (const auto& other : windowOccupied)
      {
        if (bounds.intersects(other))
        {
          y = other.top() - origin.y() - spacing - card->height();
          bounds.moveTop(origin.y() + y);
        }
      }
      y = std::max(0, y);
      card->move(x, y);
      card->raise();
      bounds.moveTop(origin.y() + y);
      windowOccupied.push_back(bounds);
      y -= spacing;
    }
  }
}
} // namespace tb::ui
