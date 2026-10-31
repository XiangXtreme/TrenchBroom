/*
 Copyright (C) 2026 XiangXtreme
 SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ui/AreaMapView.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QSettings>
#include <QTimer>

#include "ui/CyclingMapView.h"
#include "ui/MapViewActivationTracker.h"
#include "ui/MapViewBase.h"
#include "ui/Splitter.h"
#include "ui/WidgetState.h"

#include <algorithm>
#include <cmath>

namespace tb::ui
{
namespace
{
constexpr auto MaxAreas = 16;
constexpr auto MinimumAreaSize = 100;

// Domain-specific layout affordances, drawn with the application palette rather than
// another implementation of a foundational Qt control.
class AreaGrip : public QWidget
{
public:
  explicit AreaGrip(QWidget* parent)
    : QWidget{parent}
  {
    setObjectName("MapViewAreaGrip");
    setFixedSize(18, 18);
    setCursor(Qt::SizeAllCursor);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tr("Split or join map views"));
    setToolTip(
      tr("Drag inward to split; drag onto an adjacent view to join.\n"
         "Right-click for layout commands. Escape cancels."));
    setAttribute(Qt::WA_Hover);
  }

protected:
  void paintEvent(QPaintEvent*) override
  {
    auto painter = QPainter{this};
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen{
      palette().color(underMouse() || hasFocus() ? QPalette::Accent : QPalette::Mid),
      1.5});
    for (auto offset = 4; offset <= 12; offset += 4)
    {
      painter.drawLine(width() - offset, 2, width() - 2, offset);
    }
  }
};

class AreaPreview : public QWidget
{
public:
  explicit AreaPreview(QWidget* parent)
    : QWidget{parent}
  {
    setObjectName("MapViewAreaPreview");
    setAttribute(Qt::WA_TransparentForMouseEvents);
    hide();
  }

protected:
  void paintEvent(QPaintEvent*) override
  {
    auto painter = QPainter{this};
    auto color = palette().color(QPalette::Accent);
    color.setAlpha(45);
    painter.fillRect(rect(), color);
    painter.setPen(QPen{palette().color(QPalette::Accent), 2});
    painter.drawRect(rect().adjusted(1, 1, -1, -1));
    const auto text = property("join").toBool() ? tr("Join views") : tr("Split view");
    if (width() > 100 && height() > 40)
    {
      auto badge = QRect{
        QPoint{},
        QSize{fontMetrics().horizontalAdvance(text) + 20, fontMetrics().height() + 10}};
      badge.moveCenter(rect().center());
      painter.setPen(Qt::NoPen);
      painter.setBrush(palette().color(QPalette::Window));
      painter.drawRoundedRect(badge, 4, 4);
      painter.setPen(palette().color(QPalette::WindowText));
      painter.drawText(badge, Qt::AlignCenter, text);
    }
  }
};

void copyCamera(const gl::Camera& source, gl::Camera& target)
{
  if (const auto* perspective = dynamic_cast<const gl::PerspectiveCamera*>(&source))
    if (auto* targetPerspective = dynamic_cast<gl::PerspectiveCamera*>(&target))
      targetPerspective->setFov(perspective->fov());
  target.moveTo(source.position());
  target.setDirection(source.direction(), source.up());
  target.setZoom(source.zoom());
}

bool validLayout(const QJsonObject& node, int& count, const int depth = 0)
{
  if (depth > MaxAreas)
    return false;
  if (node.contains("mask"))
  {
    const auto mask = node.value("mask").toInt();
    const auto active = node.value("active").toInt(-1);
    auto views = 0;
    for (auto bit = 1; bit <= CyclingMapView::View_YZ; bit <<= 1)
      views += (mask & bit) ? 1 : 0;
    return mask > 0 && mask <= CyclingMapView::View_ALL && active >= 0 && active < views
           && ++count <= MaxAreas;
  }
  const auto ratio = node.value("ratio").toDouble();
  const auto axis = node.value("axis").toInt(-1);
  return (axis == 0 || axis == 1) && std::isfinite(ratio) && ratio > 0 && ratio < 1
         && validLayout(node.value("first").toObject(), count, depth + 1)
         && validLayout(node.value("second").toObject(), count, depth + 1);
}
} // namespace

AreaMapView::AreaMapView(
  AppController& appController,
  MapDocument& document,
  MapViewToolBox& toolBox,
  const MapViewLayout preset,
  const bool restoreLayout,
  QWidget* parent)
  : MultiPaneMapView{parent}
  , m_appController{appController}
  , m_document{document}
  , m_toolBox{toolBox}
  , m_preset{preset}
{
  setObjectName("AreaMapView");
  auto* box = new QHBoxLayout{this};
  box->setContentsMargins(0, 0, 0, 0);
  box->setSpacing(0);
  const auto saved =
    QJsonDocument::fromJson(QSettings{}.value(settingsKey()).toByteArray());
  auto count = 0;
  if (
    restoreLayout && saved.object().value("version").toInt() == 1
    && validLayout(saved.object().value("root").toObject(), count))
    m_root = deserialize(saved.object().value("root").toObject());
  else
  {
    createPreset();
    // Reuse the proportions of the old fixed presets on the first launch.
    if (restoreLayout)
      for (auto* splitter : findChildren<QSplitter*>())
        if (!splitter->objectName().isEmpty())
          restoreWidgetState(splitter);
  }
  box->addWidget(m_root);
  m_preview = new AreaPreview{this};
  rebuildConnections();
}

AreaMapView::~AreaMapView()
{
  cancelGesture();
  saveLayout();
  // Disconnect before QWidget destroys any of the cameras in the splitter tree.
  m_linkHelper.clear();
}

std::vector<CyclingMapView*> AreaMapView::areas() const
{
  auto result = std::vector<CyclingMapView*>{};
  for (const auto& [area, mask] : m_views)
    result.push_back(area);
  return result;
}

CyclingMapView* AreaMapView::createArea(const int mask)
{
  auto* area = new CyclingMapView{m_appController, m_document, m_toolBox, mask, this};
  area->setMinimumSize(MinimumAreaSize, MinimumAreaSize);
  m_views.emplace(area, mask);
  addMapView(area);
  auto* grip = new AreaGrip{area};
  grip->installEventFilter(this);
  area->installEventFilter(this);
  for (auto* view : area->findChildren<MapViewBase*>())
    view->installEventFilter(this);
  grip->move(area->width() - grip->width(), 0);
  grip->raise();
  return area;
}

void AreaMapView::createPreset()
{
  const auto split = [&](Qt::Orientation axis, QWidget* first, QWidget* second) {
    auto* result = new Splitter{axis, DrawKnob::No, this};
    result->setChildrenCollapsible(false);
    result->addWidget(first);
    result->addWidget(second);
    result->setSizes({1, 1});
    return result;
  };
  switch (m_preset)
  {
  case MapViewLayout::OnePane:
    m_root = createArea(CyclingMapView::View_ALL);
    break;
  case MapViewLayout::TwoPanes:
    m_root = split(
      Qt::Horizontal,
      createArea(CyclingMapView::View_3D),
      createArea(CyclingMapView::View_2D));
    m_root->setObjectName("TwoPaneMapView_Splitter");
    break;
  case MapViewLayout::ThreePanes:
    m_root = split(
      Qt::Horizontal,
      createArea(CyclingMapView::View_3D),
      split(
        Qt::Vertical,
        createArea(CyclingMapView::View_XY),
        createArea(CyclingMapView::View_ZZ)));
    m_root->setObjectName("ThreePaneMapView_HorizontalSplitter");
    static_cast<QSplitter*>(m_root)->widget(1)->setObjectName(
      "ThreePaneMapView_VerticalSplitter");
    break;
  case MapViewLayout::FourPanes:
    m_root = split(
      Qt::Horizontal,
      split(
        Qt::Vertical,
        createArea(CyclingMapView::View_3D),
        createArea(CyclingMapView::View_YZ)),
      split(
        Qt::Vertical,
        createArea(CyclingMapView::View_XY),
        createArea(CyclingMapView::View_XZ)));
    m_root->setObjectName("FourPaneMapView_HorizontalSplitter");
    static_cast<QSplitter*>(m_root)->widget(0)->setObjectName(
      "FourPaneMapView_LeftVerticalSplitter");
    static_cast<QSplitter*>(m_root)->widget(1)->setObjectName(
      "FourPaneMapView_RightVerticalSplitter");
    break;
  }
}

void AreaMapView::installActivationTracker(MapViewActivationTracker& tracker)
{
  m_tracker = &tracker;
  MultiPaneMapView::installActivationTracker(tracker);
}

void AreaMapView::rebuildConnections()
{
  m_linkHelper.clear();
  for (const auto& [area, mask] : m_views)
    area->linkCamera(m_linkHelper);
  if (m_tracker)
  {
    m_tracker->clear();
    MultiPaneMapView::installActivationTracker(*m_tracker);
  }
}

void AreaMapView::finishLayoutChange(CyclingMapView* focus)
{
  rebuildConnections();
  layout()->activate();
  for (auto* area : areas())
  {
    area->show();
    auto* grip = area->findChild<QWidget*>("MapViewAreaGrip");
    grip->move(area->width() - grip->width(), 0);
    grip->raise();
  }
  focus->firstMapViewBase()->setFocus();
  saveLayout();
}

bool AreaMapView::splitArea(
  CyclingMapView* area, const Qt::Orientation orientation, const double ratio)
{
  if (
    !m_views.contains(area) || currentViewMaximized() || m_views.size() >= MaxAreas
    || !std::isfinite(ratio) || ratio <= 0 || ratio >= 1
    || (orientation != Qt::Horizontal && orientation != Qt::Vertical))
    return false;
  const auto extent = orientation == Qt::Horizontal ? area->width() : area->height();
  const auto firstSize = qRound(static_cast<double>(extent) * ratio);
  if (firstSize < MinimumAreaSize || extent - firstSize < MinimumAreaSize + 4)
    return false;
  cancelMouseDrag();
  if (m_tracker)
    m_tracker->clear();
  m_linkHelper.clear();
  auto* copy = createArea(m_views.at(area));
  const auto originals = area->findChildren<MapViewBase*>();
  const auto copies = copy->findChildren<MapViewBase*>();
  for (auto i = qsizetype{0}; i < originals.size(); ++i)
  {
    copyCamera(originals[i]->camera(), copies[i]->camera());
    if (originals[i] == area->firstMapViewBase())
      copy->switchToMapView(copies[i]);
  }
  auto* parent = qobject_cast<QSplitter*>(area->parentWidget());
  auto* splitter = new Splitter{orientation, DrawKnob::No, this};
  splitter->setChildrenCollapsible(false);
  if (parent)
  {
    const auto index = parent->indexOf(area);
    const auto sizes = parent->sizes();
    area->setParent(this);
    parent->insertWidget(index, splitter);
    parent->setSizes(sizes);
  }
  else
  {
    delete layout()->replaceWidget(area, splitter);
    m_root = splitter;
  }
  splitter->addWidget(area);
  splitter->addWidget(copy);
  splitter->setSizes({firstSize, extent - firstSize});
  splitter->show();
  finishLayoutChange(area);
  return true;
}

AreaMapView::AreaRects AreaMapView::areaRects() const
{
  auto result = AreaRects{};
  const auto visit = [&](const auto& self, QWidget* node, const QRectF& bounds) -> void {
    if (auto* area = qobject_cast<CyclingMapView*>(node))
      result.emplace(area, bounds);
    else if (auto* splitter = qobject_cast<QSplitter*>(node))
    {
      const auto sizes = splitter->sizes();
      const auto total = sizes[0] + sizes[1];
      const auto ratio = total > 0 ? static_cast<double>(sizes[0]) / total : 0.5;
      auto first = bounds;
      auto second = bounds;
      if (splitter->orientation() == Qt::Horizontal)
      {
        first.setWidth(bounds.width() * ratio);
        second.setLeft(first.right());
      }
      else
      {
        first.setHeight(bounds.height() * ratio);
        second.setTop(first.bottom());
      }
      self(self, splitter->widget(0), first);
      self(self, splitter->widget(1), second);
    }
  };
  visit(visit, m_root, QRectF{0, 0, 1, 1});
  return result;
}

AreaMapView::AreaRects AreaMapView::joinedRects(
  CyclingMapView* source, CyclingMapView* target) const
{
  if (source == target || !m_views.contains(source) || !m_views.contains(target))
    return {};
  auto rects = areaRects();
  const auto a = rects.at(source);
  const auto b = rects.at(target);
  const auto close = [](double x, double y) { return std::abs(x - y) < 0.00001; };
  const auto horizontal = close(a.top(), b.top()) && close(a.bottom(), b.bottom())
                          && (close(a.right(), b.left()) || close(b.right(), a.left()));
  const auto vertical = close(a.left(), b.left()) && close(a.right(), b.right())
                        && (close(a.bottom(), b.top()) || close(b.bottom(), a.top()));
  if (!horizontal && !vertical)
    return {};
  rects[target] = a.united(b);
  rects.erase(source);
  return rects;
}

QJsonObject AreaMapView::partition(const AreaRects& rects) const
{
  if (rects.empty())
    return {};
  if (rects.size() == 1)
  {
    const auto all = areas();
    const auto index =
      std::distance(all.begin(), std::ranges::find(all, rects.begin()->first));
    return QJsonObject{{"existing", static_cast<int>(index)}};
  }
  auto bounds = QRectF{};
  for (const auto& [area, rect] : rects)
    bounds = bounds.united(rect);
  for (const auto axis : {0, 1})
  {
    for (const auto& [candidate, rect] : rects)
    {
      const auto cut = axis == 0 ? rect.right() : rect.bottom();
      const auto start = axis == 0 ? bounds.left() : bounds.top();
      const auto extent = axis == 0 ? bounds.width() : bounds.height();
      const auto ratio = (cut - start) / extent;
      if (ratio < 0.00001 || ratio > 0.99999)
        continue;
      auto first = AreaRects{};
      auto second = AreaRects{};
      for (const auto& [area, r] : rects)
      {
        const auto end = axis == 0 ? r.right() : r.bottom();
        const auto begin = axis == 0 ? r.left() : r.top();
        if (end <= cut + 0.000001)
          first.emplace(area, r);
        else if (begin >= cut - 0.000001)
          second.emplace(area, r);
      }
      if (first.empty() || second.empty() || first.size() + second.size() != rects.size())
        continue;
      auto a = partition(first);
      auto b = partition(second);
      if (!a.empty() && !b.empty())
        return QJsonObject{{"axis", axis}, {"ratio", ratio}, {"first", a}, {"second", b}};
    }
  }
  return {};
}

bool AreaMapView::canJoinAreas(CyclingMapView* source, CyclingMapView* target) const
{
  return !currentViewMaximized() && !partition(joinedRects(source, target)).empty();
}

bool AreaMapView::joinAreas(CyclingMapView* source, CyclingMapView* target)
{
  if (currentViewMaximized())
    return false;
  const auto tree = partition(joinedRects(source, target));
  if (tree.empty())
    return false;
  cancelMouseDrag();
  copyCamera(perspectiveCamera(), m_lastPerspective);
  m_lastPerspective.setFov(perspectiveCamera().fov());
  if (m_tracker)
    m_tracker->clear();
  m_linkHelper.clear();
  auto* oldRoot = m_root;
  target->firstMapViewBase()->setFocus(Qt::OtherFocusReason);
  // All leaves stay under this window while moving into the new splitter hierarchy.
  for (auto* area : areas())
    area->setParent(this);
  m_root = deserialize(tree);
  layout()->removeWidget(oldRoot);
  layout()->addWidget(m_root);
  removeMapView(source);
  m_views.erase(source);
  delete source;
  delete oldRoot;
  m_root->show();
  finishLayoutChange(target);
  return true;
}

const gl::PerspectiveCamera& AreaMapView::perspectiveCamera() const
{
  for (const auto& [area, mask] : m_views)
    if (mask & CyclingMapView::View_3D)
      return area->perspectiveCamera();
  return m_lastPerspective;
}

void AreaMapView::toggleMaximizeCurrentView()
{
  if (!currentViewMaximized())
    saveLayout();
  MultiPaneMapView::toggleMaximizeCurrentView();
}

void AreaMapView::maximizeView(MapView* view)
{
  auto* widget = dynamic_cast<QWidget*>(view);
  const auto visit = [&](const auto& self, QWidget* node) -> void {
    node->setVisible(node == widget || node->isAncestorOf(widget));
    if (auto* splitter = qobject_cast<QSplitter*>(node))
      for (auto i = 0; i < splitter->count(); ++i)
        self(self, splitter->widget(i));
  };
  visit(visit, m_root);
}

void AreaMapView::restoreViews()
{
  const auto visit = [&](const auto& self, QWidget* node) -> void {
    node->show();
    if (auto* splitter = qobject_cast<QSplitter*>(node))
      for (auto i = 0; i < splitter->count(); ++i)
        self(self, splitter->widget(i));
  };
  visit(visit, m_root);
}

QString AreaMapView::settingsKey() const
{
  return QString{"Windows/MapViewAreas/%1"}.arg(static_cast<int>(m_preset));
}

QJsonObject AreaMapView::serialize(QWidget* node) const
{
  if (auto* area = qobject_cast<CyclingMapView*>(node))
  {
    const auto views = area->findChildren<MapViewBase*>();
    return {
      {"mask", m_views.at(area)},
      {"active", static_cast<int>(views.indexOf(area->firstMapViewBase()))}};
  }
  auto* splitter = qobject_cast<QSplitter*>(node);
  const auto sizes = splitter->sizes();
  const auto total = sizes[0] + sizes[1];
  return {
    {"axis", splitter->orientation() == Qt::Horizontal ? 0 : 1},
    {"ratio", total > 0 ? static_cast<double>(sizes[0]) / total : 0.5},
    {"first", serialize(splitter->widget(0))},
    {"second", serialize(splitter->widget(1))}};
}

void AreaMapView::saveLayout() const
{
  if (!m_root || currentViewMaximized())
    return;
  QSettings{}.setValue(
    settingsKey(),
    QJsonDocument{QJsonObject{{"version", 1}, {"root", serialize(m_root)}}}.toJson(
      QJsonDocument::Compact));
}

QWidget* AreaMapView::deserialize(const QJsonObject& node)
{
  if (node.contains("existing"))
    return areas().at(static_cast<size_t>(node.value("existing").toInt()));
  if (node.contains("mask"))
  {
    auto* area = createArea(node.value("mask").toInt());
    area->switchToMapView(
      area->findChildren<MapViewBase*>().at(node.value("active").toInt()));
    return area;
  }
  auto* splitter = new Splitter{
    node.value("axis").toInt() == 0 ? Qt::Horizontal : Qt::Vertical, DrawKnob::No, this};
  splitter->setChildrenCollapsible(false);
  splitter->addWidget(deserialize(node.value("first").toObject()));
  splitter->addWidget(deserialize(node.value("second").toObject()));
  const auto ratio = node.value("ratio").toDouble(0.5);
  splitter->setSizes({qRound(10000 * ratio), qRound(10000 * (1 - ratio))});
  return splitter;
}

void AreaMapView::cancelGesture()
{
  const auto grip = m_dragGrip;
  m_dragGrip.clear();
  m_dragSource.clear();
  m_dragTarget.clear();
  m_dragMoved = false;
  if (grip && QWidget::mouseGrabber() == grip)
    grip->releaseMouse();
  if (m_preview)
    m_preview->hide();
}

void AreaMapView::updateGesture(const QPoint& globalPosition)
{
  if (!m_dragSource)
    return;
  m_dragMoved |=
    (globalPosition - m_dragStart).manhattanLength() >= QApplication::startDragDistance();
  m_dragTarget.clear();
  m_preview->hide();
  if (!m_dragMoved || currentViewMaximized())
    return;
  for (auto* area : areas())
  {
    if (
      area != m_dragSource && area->rect().contains(area->mapFromGlobal(globalPosition))
      && canJoinAreas(m_dragSource, area))
    {
      m_dragTarget = area;
      m_preview->setProperty("join", true);
      m_preview->setGeometry(QRect{area->mapTo(this, QPoint{}), area->size()});
      m_preview->show();
      m_preview->raise();
      return;
    }
  }
  auto* area = m_dragSource.data();
  const auto position = area->mapFromGlobal(globalPosition);
  if (!area->rect().contains(position))
    return;
  const auto delta = globalPosition - m_dragStart;
  m_splitOrientation =
    std::abs(delta.x()) >= std::abs(delta.y()) ? Qt::Horizontal : Qt::Vertical;
  const auto extent =
    m_splitOrientation == Qt::Horizontal ? area->width() : area->height();
  const auto coordinate =
    m_splitOrientation == Qt::Horizontal ? position.x() : position.y();
  if (extent < 2 * MinimumAreaSize + 4 || m_views.size() >= MaxAreas)
    return;
  const auto cut = std::clamp(coordinate, MinimumAreaSize, extent - MinimumAreaSize - 4);
  m_splitRatio = static_cast<double>(cut) / extent;
  auto preview = QRect{area->mapTo(this, QPoint{}), area->size()};
  if (m_splitOrientation == Qt::Horizontal)
    preview.setWidth(cut);
  else
    preview.setHeight(cut);
  m_preview->setProperty("join", false);
  m_preview->setGeometry(preview);
  m_preview->show();
  m_preview->raise();
}

void AreaMapView::showAreaMenu(CyclingMapView* area, const QPoint& globalPosition)
{
  auto menu = QMenu{this};
  auto* horizontal = menu.addAction(tr("Split Left / Right"));
  auto* vertical = menu.addAction(tr("Split Top / Bottom"));
  horizontal->setEnabled(
    !currentViewMaximized() && m_views.size() < MaxAreas
    && area->width() >= 2 * MinimumAreaSize + 4);
  vertical->setEnabled(
    !currentViewMaximized() && m_views.size() < MaxAreas
    && area->height() >= 2 * MinimumAreaSize + 4);
  menu.addSeparator();
  auto joins = std::map<QAction*, QPointer<CyclingMapView>>{};
  for (auto* neighbor : areas())
  {
    if (!canJoinAreas(area, neighbor))
      continue;
    const auto delta = neighbor->mapTo(this, QPoint{}) - area->mapTo(this, QPoint{});
    const auto label =
      std::abs(delta.x()) > std::abs(delta.y())
        ? (delta.x() < 0 ? tr("Join into left view") : tr("Join into right view"))
        : (delta.y() < 0 ? tr("Join into upper view") : tr("Join into lower view"));
    joins.emplace(menu.addAction(label), neighbor);
  }
  const auto source = QPointer<CyclingMapView>{area};
  auto* selected = menu.exec(globalPosition);
  if (!selected)
    return;
  const auto target =
    joins.contains(selected) ? joins.at(selected) : QPointer<CyclingMapView>{};
  const auto axis = selected == horizontal ? Qt::Horizontal : Qt::Vertical;
  const auto joining = joins.contains(selected);
  QTimer::singleShot(0, this, [this, source, target, axis, joining]() {
    if (!source)
      return;
    if (joining)
    {
      if (target)
        joinAreas(source, target);
    }
    else
      splitArea(source, axis);
  });
}

bool AreaMapView::eventFilter(QObject* watched, QEvent* event)
{
  if (auto* area = qobject_cast<CyclingMapView*>(watched))
  {
    if (event->type() == QEvent::Resize || event->type() == QEvent::Show)
    {
      auto* grip = area->findChild<QWidget*>("MapViewAreaGrip");
      grip->move(area->width() - grip->width(), 0);
      grip->raise();
    }
  }
  if (event->type() == QEvent::Show && qobject_cast<MapViewBase*>(watched))
  {
    auto* view = static_cast<QWidget*>(watched);
    if (auto* grip = view->parentWidget()->findChild<QWidget*>("MapViewAreaGrip"))
      grip->raise();
  }
  if (watched->objectName() != "MapViewAreaGrip")
    return MultiPaneMapView::eventFilter(watched, event);
  auto* grip = static_cast<QWidget*>(watched);
  auto* area = qobject_cast<CyclingMapView*>(grip->parentWidget());
  if (event->type() == QEvent::ContextMenu)
  {
    cancelGesture();
    showAreaMenu(area, static_cast<QContextMenuEvent*>(event)->globalPos());
    return true;
  }
  if (event->type() == QEvent::KeyPress)
  {
    auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() == Qt::Key_Escape)
    {
      cancelGesture();
      return true;
    }
    if (
      key->key() == Qt::Key_Return || key->key() == Qt::Key_Space
      || key->key() == Qt::Key_Menu)
    {
      showAreaMenu(area, grip->mapToGlobal(grip->rect().center()));
      return true;
    }
  }
  if (event->type() == QEvent::MouseButtonPress)
  {
    auto* mouse = static_cast<QMouseEvent*>(event);
    if (mouse->button() == Qt::LeftButton && !currentViewMaximized())
    {
      cancelGesture();
      m_dragSource = area;
      m_dragGrip = grip;
      m_dragStart = mouse->globalPosition().toPoint();
      grip->setFocus();
      grip->grabMouse();
      return true;
    }
  }
  if (event->type() == QEvent::MouseMove && m_dragSource)
  {
    updateGesture(static_cast<QMouseEvent*>(event)->globalPosition().toPoint());
    return true;
  }
  if (event->type() == QEvent::MouseButtonRelease && m_dragSource)
  {
    auto* mouse = static_cast<QMouseEvent*>(event);
    if (mouse->button() != Qt::LeftButton)
      return true;
    updateGesture(mouse->globalPosition().toPoint());
    const auto source = m_dragSource;
    const auto target = m_dragTarget;
    const auto commit = m_dragMoved && m_preview->isVisible();
    const auto axis = m_splitOrientation;
    const auto ratio = m_splitRatio;
    cancelGesture();
    if (commit)
      QTimer::singleShot(
        0, this, [this, source, target, axis, ratio, joining = bool(target)]() {
          if (!source)
            return;
          if (joining)
          {
            if (target)
              joinAreas(source, target);
          }
          else
            splitArea(source, axis, ratio);
        });
    return true;
  }
  if (
    (event->type() == QEvent::UngrabMouse || event->type() == QEvent::Hide
     || event->type() == QEvent::WindowDeactivate)
    && m_dragSource)
    cancelGesture();
  return MultiPaneMapView::eventFilter(watched, event);
}
} // namespace tb::ui
