/*
 Copyright (C) 2026 XiangXtreme
 SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ui/AreaMapView.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QCursor>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QSettings>
#include <QSplitter>
#include <QStyleOption>
#include <QTimer>
#include <QVariantAnimation>

#include "ui/CyclingMapView.h"
#include "ui/MapViewActivationTracker.h"
#include "ui/MapViewAreaLayout.h"
#include "ui/MapViewBase.h"
#include "ui/WidgetState.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <utility>

namespace tb::ui
{
namespace
{
constexpr auto MaxAreas = areaGeometry::MaximumAreas;
constexpr auto MinimumAreaSize = areaGeometry::SplitMinimum;

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
      tr("Drag inward to split; drag across an edge to expand this view.\n"
         "Drag deeper to dock or replace. Right-click for commands. Escape cancels."));
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
    const auto text = property("label").toString();
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

class ClosedPreview : public QWidget
{
public:
  std::vector<QRect> regions;
  explicit ClosedPreview(QWidget* parent)
    : QWidget{parent}
  {
    setObjectName("MapViewAreaClosedPreview");
    setAttribute(Qt::WA_TransparentForMouseEvents);
    hide();
  }

protected:
  void paintEvent(QPaintEvent*) override
  {
    auto painter = QPainter{this};
    auto color = palette().color(QPalette::Window).darker(400);
    color.setAlpha(190);
    for (const auto& rect : regions)
      painter.fillRect(rect, color);
  }
};

// Brief commit feedback follows Blender's 150 ms area highlight fade.
class AreaFeedback : public QWidget
{
private:
  QRect m_highlight;
  std::vector<QRect> m_closed;
  QVariantAnimation m_fade;

public:
  AreaFeedback(QWidget* parent, const QRect& highlight, std::vector<QRect> closed)
    : QWidget{parent}
    , m_highlight{highlight}
    , m_closed{std::move(closed)}
  {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setObjectName("MapViewAreaFeedback");
    setGeometry(parent->rect());
    m_fade.setStartValue(1.0);
    m_fade.setEndValue(0.0);
    m_fade.setDuration(150);
    connect(&m_fade, &QVariantAnimation::valueChanged, this, [this]() { update(); });
    connect(&m_fade, &QVariantAnimation::finished, this, &QObject::deleteLater);
    show();
    raise();
    m_fade.start();
  }

protected:
  void paintEvent(QPaintEvent*) override
  {
    auto painter = QPainter{this};
    const auto opacity = m_fade.currentValue().toDouble();
    auto shade = palette().color(QPalette::Window).darker(400);
    shade.setAlpha(qRound(190 * opacity));
    for (const auto& rect : m_closed)
      painter.fillRect(rect, shade);
    auto accent = palette().color(QPalette::Accent);
    accent.setAlpha(qRound(45 * opacity));
    painter.fillRect(m_highlight, accent);
    accent.setAlpha(qRound(255 * opacity));
    painter.setPen(QPen{accent, 2});
    painter.drawRect(m_highlight.adjusted(1, 1, -1, -1));
  }
};

class AreaEdge : public QWidget
{
public:
  explicit AreaEdge(QWidget* parent)
    : QWidget{parent}
  {
    setObjectName("MapViewAreaEdge");
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_Hover);
    setToolTip(AreaMapView::tr("Drag to resize views. Escape restores the boundary."));
  }

protected:
  void paintEvent(QPaintEvent*) override
  {
    auto option = QStyleOption{};
    option.initFrom(this);
    option.state.setFlag(QStyle::State_Horizontal, property("axis").toInt() == 0);
    auto painter = QPainter{this};
    style()->drawControl(QStyle::CE_Splitter, &option, &painter, this);
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
  const bool restoreSaved,
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
  m_root = new QWidget{this};
  m_root->setObjectName("MapViewAreaSurface");
  m_root->setAttribute(Qt::WA_StyledBackground);
  m_areaLayout = new MapViewAreaLayout{m_root};
  m_root->installEventFilter(this);
  box->addWidget(m_root);
  const auto saved =
    QJsonDocument::fromJson(QSettings{}.value(settingsKey()).toByteArray());
  if (!restoreSaved || !restoreLayout(saved.object()))
    createPreset(restoreSaved);
  m_closedPreview = new ClosedPreview{m_root};
  m_preview = new AreaPreview{m_root};
  rebuildConnections();
  scheduleEdges();
}
AreaMapView::~AreaMapView()
{
  cancelGesture();
  saveLayout();
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
  auto* area = new CyclingMapView{m_appController, m_document, m_toolBox, mask, m_root};
  area->setMinimumSize(areaGeometry::MinimumWidth, areaGeometry::HeaderHeight);
  m_views.emplace(area, CyclingMapView::View_ALL);
  connect(area, &CyclingMapView::viewChanged, this, [this, area](int previous, int) {
    if (previous == CyclingMapView::View_3D)
      copyCamera(area->perspectiveCamera(), m_lastPerspective);
    if (m_preview && m_areaLayout->areas().contains(area))
      saveLayout();
  });
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
CyclingMapView* AreaMapView::copyArea(CyclingMapView* source)
{
  auto* copy = createArea(m_views.at(source));
  for (auto type = 1; type <= CyclingMapView::View_YZ; type <<= 1)
    copyCamera(
      source->mapViewForType(type)->camera(), copy->mapViewForType(type)->camera());
  copy->setViewType(source->viewType());
  return copy;
}
void AreaMapView::createPreset(const bool restoreLegacy)
{
  const auto ratio = [&](const QString& name) {
    if (!restoreLegacy)
      return 0.5;
    auto splitter = QSplitter{};
    splitter.setObjectName(name);
    splitter.addWidget(new QWidget{});
    splitter.addWidget(new QWidget{});
    restoreWidgetState(&splitter);
    const auto sizes = splitter.sizes();
    const auto total = sizes[0] + sizes[1];
    return total > 0 ? std::clamp(static_cast<double>(sizes[0]) / total, 0.05, 0.95)
                     : 0.5;
  };
  auto rects = std::map<QWidget*, QRectF>{};
  const auto area = [&](const int mask, const QRectF& bounds) {
    rects.emplace(createArea(mask), bounds);
  };
  switch (m_preset)
  {
  case MapViewLayout::OnePane:
    area(CyclingMapView::View_ALL, {0, 0, 1, 1});
    break;
  case MapViewLayout::TwoPanes: {
    const auto x = ratio("TwoPaneMapView_Splitter");
    area(CyclingMapView::View_3D, {0, 0, x, 1});
    area(CyclingMapView::View_2D, {x, 0, 1 - x, 1});
    break;
  }
  case MapViewLayout::ThreePanes: {
    const auto x = ratio("ThreePaneMapView_HorizontalSplitter");
    const auto y = ratio("ThreePaneMapView_VerticalSplitter");
    area(CyclingMapView::View_3D, {0, 0, x, 1});
    area(CyclingMapView::View_XY, {x, 0, 1 - x, y});
    area(CyclingMapView::View_ZZ, {x, y, 1 - x, 1 - y});
    break;
  }
  case MapViewLayout::FourPanes: {
    const auto x = ratio("FourPaneMapView_HorizontalSplitter");
    const auto y1 = ratio("FourPaneMapView_LeftVerticalSplitter");
    const auto y2 = ratio("FourPaneMapView_RightVerticalSplitter");
    area(CyclingMapView::View_3D, {0, 0, x, y1});
    area(CyclingMapView::View_YZ, {0, y1, x, 1 - y1});
    area(CyclingMapView::View_XY, {x, 0, 1 - x, y2});
    area(CyclingMapView::View_XZ, {x, y2, 1 - x, 1 - y2});
    break;
  }
  }
  m_areaLayout->setAreas(rects);
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
  for (auto* area : areas())
    area->show();
  m_areaLayout->invalidate();
  m_areaLayout->setGeometry(m_root->rect());
  for (auto* area : areas())
  {
    auto* grip = area->findChild<QWidget*>("MapViewAreaGrip");
    grip->move(area->width() - grip->width(), 0);
    grip->raise();
  }
  focus->firstMapViewBase()->setFocus();
  scheduleEdges();
  saveLayout();
}
areaGeometry::Rects AreaMapView::geometryRects() const
{
  auto rects = areaGeometry::Rects{};
  const auto all = areas();
  for (auto i = size_t{0}; i < all.size(); ++i)
    rects.emplace(static_cast<int>(i), m_areaLayout->areas().at(all[i]));
  return rects;
}
QRectF AreaMapView::areaBounds(CyclingMapView* area) const
{
  return m_areaLayout->areas().contains(area) ? m_areaLayout->areas().at(area) : QRectF{};
}
void AreaMapView::setRects(const areaGeometry::Rects& rects)
{
  auto widgets = std::map<QWidget*, QRectF>{};
  const auto all = areas();
  for (const auto& [id, rect] : rects)
    widgets.emplace(all.at(static_cast<size_t>(id)), rect);
  m_areaLayout->setAreas(widgets);
}
bool AreaMapView::splitArea(
  CyclingMapView* area,
  const Qt::Orientation orientation,
  const double ratio,
  const bool newFirst)
{
  if (
    !m_views.contains(area) || currentViewMaximized() || m_views.size() >= MaxAreas
    || !std::isfinite(ratio) || ratio <= 0 || ratio >= 1
    || (orientation != Qt::Horizontal && orientation != Qt::Vertical))
    return false;
  cancelGesture();
  const auto extent = orientation == Qt::Horizontal ? area->width() : area->height();
  if (extent * ratio < MinimumAreaSize || extent * (1 - ratio) < MinimumAreaSize)
    return false;
  cancelMouseDrag();
  if (m_tracker)
    m_tracker->clear();
  m_linkHelper.clear();
  auto first = areaBounds(area);
  auto second = first;
  if (orientation == Qt::Horizontal)
  {
    first.setWidth(first.width() * ratio);
    second.setLeft(first.right());
  }
  else
  {
    first.setHeight(first.height() * ratio);
    second.setTop(first.bottom());
  }
  auto rects = m_areaLayout->areas();
  auto* copy = copyArea(area);
  rects[area] = newFirst ? second : first;
  rects[copy] = newFirst ? first : second;
  m_areaLayout->setAreas(rects);
  finishLayoutChange(area);
  return true;
}
bool AreaMapView::canJoinAreas(CyclingMapView* source, CyclingMapView* target) const
{
  if (currentViewMaximized() || !m_views.contains(source) || !m_views.contains(target))
    return false;
  const auto all = areas();
  return areaGeometry::join(
           geometryRects(),
           static_cast<int>(std::ranges::find(all, source) - all.begin()),
           static_cast<int>(std::ranges::find(all, target) - all.begin()),
           m_root->size())
    .valid;
}
bool AreaMapView::applyPlan(const areaGeometry::Plan& plan, CyclingMapView* focus)
{
  if (!plan.valid || currentViewMaximized() || !m_views.contains(focus))
    return false;
  cancelMouseDrag();
  copyCamera(perspectiveCamera(), m_lastPerspective);
  if (m_tracker)
    m_tracker->clear();
  m_linkHelper.clear();
  const auto original = areas();
  auto widgets = std::map<QWidget*, QRectF>{};
  for (const auto& [id, rect] : plan.rects)
  {
    auto* area = id < static_cast<int>(original.size())
                   ? original.at(static_cast<size_t>(id))
                   : copyArea(original.at(static_cast<size_t>(plan.copies.at(id))));
    widgets.emplace(area, rect);
  }
  // Retain native focus and GL parents throughout edits. Delete only retired areas,
  // after every observer can see the surviving source view.
  focus->firstMapViewBase()->setFocus(Qt::OtherFocusReason);
  m_areaLayout->setAreas(widgets);
  for (auto* area : original)
    if (!widgets.contains(area))
    {
      removeMapView(area);
      m_views.erase(area);
      delete area;
    }
  finishLayoutChange(focus);
  auto closed = std::vector<QRect>{};
  for (const auto& rect : plan.closed)
    closed.push_back(m_areaLayout->pixelRect(rect));
  new AreaFeedback{m_root, m_areaLayout->pixelRect(plan.highlight), std::move(closed)};
  return true;
}
bool AreaMapView::joinAreas(CyclingMapView* source, CyclingMapView* target)
{
  if (!m_views.contains(source) || !m_views.contains(target))
    return false;
  cancelGesture();
  const auto all = areas();
  return applyPlan(
    areaGeometry::join(
      geometryRects(),
      static_cast<int>(std::ranges::find(all, source) - all.begin()),
      static_cast<int>(std::ranges::find(all, target) - all.begin()),
      m_root->size()),
    source);
}
bool AreaMapView::dockArea(
  CyclingMapView* source,
  CyclingMapView* target,
  const areaGeometry::Dock side,
  const double fraction)
{
  if (!m_views.contains(source) || !m_views.contains(target))
    return false;
  cancelGesture();
  const auto all = areas();
  return applyPlan(
    areaGeometry::dock(
      geometryRects(),
      static_cast<int>(std::ranges::find(all, source) - all.begin()),
      static_cast<int>(std::ranges::find(all, target) - all.begin()),
      side,
      fraction,
      m_root->size()),
    source);
}
const gl::PerspectiveCamera& AreaMapView::perspectiveCamera() const
{
  for (const auto& [area, mask] : m_views)
    if (area->viewType() == CyclingMapView::View_3D && area->isCurrent())
      return area->perspectiveCamera();
  for (const auto& [area, mask] : m_views)
    if (area->viewType() == CyclingMapView::View_3D)
      return area->perspectiveCamera();
  return m_lastPerspective;
}
void AreaMapView::toggleMaximizeCurrentView()
{
  cancelGesture();
  if (!currentViewMaximized())
    saveLayout();
  MultiPaneMapView::toggleMaximizeCurrentView();
  m_areaLayout->invalidate();
  m_areaLayout->setGeometry(m_root->rect());
  scheduleEdges();
}
void AreaMapView::maximizeView(MapView* view)
{
  for (auto* area : areas())
    area->setVisible(area == view);
}
void AreaMapView::restoreViews()
{
  for (auto* area : areas())
    area->show();
}
QString AreaMapView::settingsKey() const
{
  return QString{"Windows/MapViewAreas/%1"}.arg(static_cast<int>(m_preset));
}
void AreaMapView::saveLayout() const
{
  if (!m_areaLayout || currentViewMaximized())
    return;
  auto saved = QJsonArray{};
  for (const auto& [area, mask] : m_views)
  {
    const auto r = areaBounds(area);
    saved.append(QJsonObject{
      {"mask", mask},
      {"active", std::countr_zero(static_cast<unsigned int>(area->viewType()))},
      {"x", r.x()},
      {"y", r.y()},
      {"w", r.width()},
      {"h", r.height()}});
  }
  QSettings{}.setValue(
    settingsKey(),
    QJsonDocument{QJsonObject{{"version", 2}, {"areas", saved}}}.toJson(
      QJsonDocument::Compact));
}
bool AreaMapView::restoreLayout(const QJsonObject& saved)
{
  auto entries = QJsonArray{};
  if (saved.value("version").toInt() == 1)
  {
    const auto root = saved.value("root").toObject();
    auto count = 0;
    if (!validLayout(root, count))
      return false;
    const auto visit = [&](
                         const auto& self, QJsonObject node, const QRectF& rect) -> void {
      if (node.contains("mask"))
      {
        node["x"] = rect.x();
        node["y"] = rect.y();
        node["w"] = rect.width();
        node["h"] = rect.height();
        entries.append(node);
      }
      else
      {
        auto first = rect;
        auto second = rect;
        const auto ratio = node.value("ratio").toDouble();
        if (node.value("axis").toInt() == 0)
        {
          first.setWidth(rect.width() * ratio);
          second.setLeft(first.right());
        }
        else
        {
          first.setHeight(rect.height() * ratio);
          second.setTop(first.bottom());
        }
        self(self, node.value("first").toObject(), first);
        self(self, node.value("second").toObject(), second);
      }
    };
    visit(visit, root, {0, 0, 1, 1});
  }
  else if (saved.value("version").toInt() == 2)
    entries = saved.value("areas").toArray();
  else
    return false;
  auto rects = areaGeometry::Rects{};
  auto count = 0;
  for (const auto value : entries)
  {
    const auto node = value.toObject();
    if (!validLayout(node, count))
      return false;
    rects.emplace(
      static_cast<int>(rects.size()),
      QRectF{
        node.value("x").toDouble(-1),
        node.value("y").toDouble(-1),
        node.value("w").toDouble(),
        node.value("h").toDouble()});
  }
  if (!areaGeometry::validTiling(rects))
    return false;
  auto widgets = std::map<QWidget*, QRectF>{};
  for (auto i = qsizetype{0}; i < entries.size(); ++i)
  {
    const auto node = entries[i].toObject();
    auto* area = createArea(node.value("mask").toInt());
    auto active = node.value("active").toInt();
    for (auto type = 1; type <= CyclingMapView::View_YZ; type <<= 1)
      if (node.value("mask").toInt() & type)
        if (active-- == 0)
        {
          area->setViewType(type);
          break;
        }
    widgets.emplace(area, rects.at(static_cast<int>(i)));
  }
  m_areaLayout->setAreas(widgets);
  return true;
}

void AreaMapView::scheduleEdges()
{
  if (m_edgesPending)
    return;
  m_edgesPending = true;
  QTimer::singleShot(0, this, [this]() {
    m_edgesPending = false;
    updateEdges();
  });
}
void AreaMapView::updateEdges()
{
  if (m_edgeGrip)
    return;
  for (auto* edge : m_edges)
    delete edge;
  m_edges.clear();
  auto visible = 0;
  for (auto* area : areas())
    if (!area->isHidden())
      ++visible;
  if (visible <= 1 || currentViewMaximized())
    return;
  struct Edge
  {
    int axis;
    double position;
    double start;
    double end;
  };
  auto edges = std::vector<Edge>{};
  const auto rects = geometryRects();
  for (auto a = rects.begin(); a != rects.end(); ++a)
    for (auto b = std::next(a); b != rects.end(); ++b)
      for (const auto axis : {0, 1})
      {
        const auto& r = a->second;
        const auto& s = b->second;
        const auto position = axis == 0 ? r.right() : r.bottom();
        const auto opposite = axis == 0 ? s.left() : s.top();
        const auto reverse = axis == 0 ? r.left() : r.top();
        const auto reverseOpposite = axis == 0 ? s.right() : s.bottom();
        if (
          std::abs(position - opposite) > 0.000001
          && std::abs(reverse - reverseOpposite) > 0.000001)
          continue;
        const auto start =
          std::max(axis == 0 ? r.top() : r.left(), axis == 0 ? s.top() : s.left());
        const auto end = std::min(
          axis == 0 ? r.bottom() : r.right(), axis == 0 ? s.bottom() : s.right());
        if (end - start > 0.000001)
          edges.push_back(
            {axis,
             std::abs(position - opposite) < 0.000001 ? position : reverse,
             start,
             end});
      }
  // Connected collinear segments move as one edge, including T junctions.
  auto changed = true;
  while (changed)
  {
    changed = false;
    for (auto i = size_t{0}; i < edges.size() && !changed; ++i)
      for (auto j = i + 1; j < edges.size(); ++j)
        if (
          edges[i].axis == edges[j].axis
          && std::abs(edges[i].position - edges[j].position) < 0.000001
          && edges[i].start <= edges[j].end + 0.000001
          && edges[j].start <= edges[i].end + 0.000001)
        {
          edges[i].start = std::min(edges[i].start, edges[j].start);
          edges[i].end = std::max(edges[i].end, edges[j].end);
          edges.erase(edges.begin() + static_cast<std::ptrdiff_t>(j));
          changed = true;
          break;
        }
  }
  for (const auto& spec : edges)
  {
    auto* edge = new AreaEdge{m_root};
    edge->setProperty("axis", spec.axis);
    edge->setProperty("position", spec.position);
    edge->setProperty("start", spec.start);
    edge->setProperty("end", spec.end);
    edge->setCursor(spec.axis == 0 ? Qt::SplitHCursor : Qt::SplitVCursor);
    const auto extent = spec.axis == 0 ? m_root->width() : m_root->height();
    const auto cross = spec.axis == 0 ? m_root->height() : m_root->width();
    const auto width = m_areaLayout->handleWidth();
    const auto pos = qRound(spec.position * extent) - width / 2;
    const auto start = qRound(spec.start * cross);
    const auto length = qRound(spec.end * cross) - start;
    edge->setGeometry(
      spec.axis == 0 ? QRect{pos, start, width, length}
                     : QRect{start, pos, length, width});
    edge->installEventFilter(this);
    edge->show();
    edge->raise();
    m_edges.push_back(edge);
  }
  if (m_closedPreview)
    m_closedPreview->raise();
  if (m_preview)
    m_preview->raise();
}
bool AreaMapView::edgeEvent(QWidget* edge, QEvent* event)
{
  if (event->type() == QEvent::MouseButtonPress)
  {
    auto* mouse = static_cast<QMouseEvent*>(event);
    if (mouse->button() == Qt::RightButton && m_edgeGrip)
    {
      cancelGesture();
      return true;
    }
    if (mouse->button() != Qt::LeftButton)
      return false;
    cancelGesture();
    m_edgeGrip = edge;
    m_edgeStart = mouse->globalPosition().toPoint();
    m_edgeStartRects = geometryRects();
    edge->setFocus(Qt::MouseFocusReason);
    edge->grabMouse();
    return true;
  }
  if (!m_edgeGrip)
    return false;
  if (event->type() == QEvent::MouseMove)
  {
    const auto axis = edge->property("axis").toInt();
    const auto position = edge->property("position").toDouble();
    const auto start = edge->property("start").toDouble();
    const auto end = edge->property("end").toDouble();
    const auto extent = axis == 0 ? m_root->width() : m_root->height();
    if (extent <= 0)
      return true;
    const auto delta =
      static_cast<QMouseEvent*>(event)->globalPosition().toPoint() - m_edgeStart;
    auto offset = static_cast<double>(axis == 0 ? delta.x() : delta.y()) / extent;
    auto lower = -1.0;
    auto upper = 1.0;
    const auto minimum =
      static_cast<double>(
        (axis == 0 ? areaGeometry::MinimumWidth : areaGeometry::HeaderHeight)
        + m_areaLayout->handleWidth())
      / extent;
    auto affected = std::map<int, bool>{};
    for (const auto& [id, r] : m_edgeStartRects)
    {
      const auto low = axis == 0 ? r.left() : r.top();
      const auto high = axis == 0 ? r.right() : r.bottom();
      const auto crossLow = axis == 0 ? r.top() : r.left();
      const auto crossHigh = axis == 0 ? r.bottom() : r.right();
      if (crossHigh <= start + 0.000001 || crossLow >= end - 0.000001)
        continue;
      if (std::abs(high - position) < 0.000001)
      {
        affected[id] = false;
        lower = std::max(lower, low + minimum - position);
      }
      if (std::abs(low - position) < 0.000001)
      {
        affected[id] = true;
        upper = std::min(upper, high - minimum - position);
      }
    }
    if (lower > upper)
      return true;
    offset = std::clamp(offset, lower, upper);
    auto rects = m_edgeStartRects;
    for (const auto& [id, low] : affected)
    {
      auto& r = rects.at(id);
      if (axis == 0)
      {
        if (low)
          r.setLeft(position + offset);
        else
          r.setRight(position + offset);
      }
      else
      {
        if (low)
          r.setTop(position + offset);
        else
          r.setBottom(position + offset);
      }
    }
    if (areaGeometry::validTiling(rects))
      setRects(rects);
    const auto width = m_areaLayout->handleWidth();
    for (auto* other : m_edges)
    {
      const auto otherAxis = other->property("axis").toInt();
      const auto otherPosition = other->property("position").toDouble();
      if (otherAxis == axis || otherPosition < start || otherPosition > end)
        continue;
      auto from = other->property("start").toDouble();
      auto to = other->property("end").toDouble();
      if (std::abs(from - position) < 0.000001)
        from += offset;
      if (std::abs(to - position) < 0.000001)
        to += offset;
      const auto cross = axis == 0 ? m_root->height() : m_root->width();
      const auto fixed = qRound(otherPosition * cross) - width / 2;
      const auto begin = qRound(from * extent);
      const auto length = qRound(to * extent) - begin;
      other->setGeometry(
        otherAxis == 0 ? QRect{fixed, begin, width, length}
                       : QRect{begin, fixed, length, width});
    }
    if (axis == 0)
      edge->move(qRound((position + offset) * extent) - width / 2, edge->y());
    else
      edge->move(edge->x(), qRound((position + offset) * extent) - width / 2);
    return true;
  }
  const auto escape = event->type() == QEvent::KeyPress
                      && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape;
  const auto cancel = escape || event->type() == QEvent::WindowDeactivate
                      || event->type() == QEvent::UngrabMouse;
  if (cancel || event->type() == QEvent::MouseButtonRelease)
  {
    if (
      event->type() == QEvent::MouseButtonRelease
      && static_cast<QMouseEvent*>(event)->button() != Qt::LeftButton)
      return true;
    if (cancel)
      setRects(m_edgeStartRects);
    m_edgeGrip.clear();
    edge->releaseMouse();
    m_edgeStartRects.clear();
    saveLayout();
    scheduleEdges();
    return true;
  }
  return false;
}
void AreaMapView::cancelGesture()
{
  if (m_edgeGrip)
  {
    const auto edge = m_edgeGrip;
    m_edgeGrip.clear();
    setRects(m_edgeStartRects);
    m_edgeStartRects.clear();
    edge->releaseMouse();
    scheduleEdges();
  }
  const auto grip = m_dragGrip;
  m_dragGrip.clear();
  m_dragSource.clear();
  m_dragTarget.clear();
  m_dragMoved = false;
  m_gesturePlan = {};
  m_dock = areaGeometry::Dock::None;
  if (grip && QWidget::mouseGrabber() == grip)
    grip->releaseMouse();
  if (m_preview)
    m_preview->hide();
  if (m_closedPreview)
    m_closedPreview->hide();
}
void AreaMapView::updateGesture(
  const QPoint& globalPosition, const Qt::KeyboardModifiers modifiers)
{
  if (!m_dragSource)
    return;
  const auto delta = globalPosition - m_dragStart;
  m_dragMoved |= std::max(std::abs(delta.x()), std::abs(delta.y())) > 20;
  m_dragTarget.clear();
  m_gesturePlan = {};
  m_dock = areaGeometry::Dock::None;
  m_preview->hide();
  m_closedPreview->hide();
  if (
    !m_dragMoved || currentViewMaximized() || m_root->width() <= 0
    || m_root->height() <= 0)
    return;
  const auto position = m_root->mapFromGlobal(globalPosition);
  const auto point = QPointF{
    static_cast<double>(position.x()) / m_root->width(),
    static_cast<double>(position.y()) / m_root->height()};
  const auto all = areas();
  const auto sourceId =
    static_cast<int>(std::ranges::find(all, m_dragSource.data()) - all.begin());
  for (auto* area : all)
  {
    if (area == m_dragSource || !areaBounds(area).contains(point))
      continue;
    m_dragTarget = area;
    const auto targetId = static_cast<int>(std::ranges::find(all, area) - all.begin());
    auto fraction = 0.5;
    m_dock = areaGeometry::dockingTarget(
      areaBounds(m_dragSource),
      areaBounds(area),
      point,
      m_root->size(),
      modifiers.testFlag(Qt::AltModifier),
      modifiers.testFlag(Qt::ControlModifier),
      fraction);
    m_gesturePlan =
      m_dock == areaGeometry::Dock::None
        ? areaGeometry::join(geometryRects(), sourceId, targetId, m_root->size())
        : areaGeometry::dock(
            geometryRects(), sourceId, targetId, m_dock, fraction, m_root->size());
    if (!m_gesturePlan.valid)
      return;
    auto* closed = static_cast<ClosedPreview*>(m_closedPreview);
    closed->regions.clear();
    for (const auto& r : m_gesturePlan.closed)
      closed->regions.push_back(m_areaLayout->pixelRect(r));
    closed->setGeometry(m_root->rect());
    closed->show();
    closed->raise();
    closed->update();
    m_preview->setProperty("join", m_dock == areaGeometry::Dock::None);
    m_preview->setProperty(
      "label",
      m_dock == areaGeometry::Dock::None     ? tr("Expand this view")
      : m_dock == areaGeometry::Dock::Center ? tr("Move / replace view")
                                             : tr("Dock this view"));
    m_preview->setGeometry(m_areaLayout->pixelRect(m_gesturePlan.highlight));
    m_preview->show();
    m_preview->raise();
    m_preview->update();
    return;
  }
  auto* area = m_dragSource.data();
  if (!areaBounds(area).contains(point))
    return;
  m_splitOrientation =
    std::abs(delta.x()) >= std::abs(delta.y()) ? Qt::Horizontal : Qt::Vertical;
  const auto bounds = areaBounds(area);
  const auto horizontal = m_splitOrientation == Qt::Horizontal;
  const auto extent = horizontal ? area->width() : area->height();
  if (extent < 2 * MinimumAreaSize || m_views.size() >= MaxAreas)
    return;
  auto ratio = horizontal ? (point.x() - bounds.left()) / bounds.width()
                          : (point.y() - bounds.top()) / bounds.height();
  if (modifiers.testFlag(Qt::ControlModifier))
  {
    auto best = std::round(ratio * 12) / 12;
    for (const auto& [id, rect] : geometryRects())
      for (const auto edge :
           {horizontal ? rect.left() : rect.top(),
            horizontal ? rect.right() : rect.bottom()})
      {
        const auto candidate = (edge - (horizontal ? bounds.left() : bounds.top()))
                               / (horizontal ? bounds.width() : bounds.height());
        if (std::abs(candidate - ratio) < std::abs(best - ratio))
          best = candidate;
      }
    ratio = best;
  }
  else if (ratio >= 0.48 && ratio <= 0.52)
    ratio = 0.5;
  const auto minimum = static_cast<double>(MinimumAreaSize) / extent;
  m_splitRatio = std::clamp(ratio, minimum, 1 - minimum);
  auto preview = bounds;
  if (horizontal)
    preview.setLeft(bounds.left() + bounds.width() * m_splitRatio);
  else
    preview.setHeight(bounds.height() * m_splitRatio);
  m_preview->setProperty("join", false);
  m_preview->setProperty("label", tr("Split view"));
  m_preview->setGeometry(m_areaLayout->pixelRect(preview));
  m_preview->show();
  m_preview->raise();
  m_preview->update();
}
void AreaMapView::showAreaMenu(CyclingMapView* area, const QPoint& globalPosition)
{
  auto menu = QMenu{this};
  auto* horizontal = menu.addAction(tr("Split Left / Right"));
  auto* vertical = menu.addAction(tr("Split Top / Bottom"));
  horizontal->setEnabled(
    !currentViewMaximized() && m_views.size() < MaxAreas
    && area->width() >= 2 * MinimumAreaSize);
  vertical->setEnabled(
    !currentViewMaximized() && m_views.size() < MaxAreas
    && area->height() >= 2 * MinimumAreaSize);
  menu.addSeparator();
  auto joins = std::map<QAction*, QPointer<CyclingMapView>>{};
  for (auto* neighbor : areas())
  {
    if (!canJoinAreas(area, neighbor))
      continue;
    const auto dir =
      areaGeometry::direction(areaBounds(area), areaBounds(neighbor), m_root->size());
    const auto label = dir == 0   ? tr("Expand this view left")
                       : dir == 1 ? tr("Expand this view up")
                       : dir == 2 ? tr("Expand this view right")
                                  : tr("Expand this view down");
    joins.emplace(menu.addAction(label), neighbor);
  }
  const auto source = QPointer<CyclingMapView>{area};
  auto* selected = menu.exec(globalPosition);
  if (!selected)
    return;
  const auto joining = joins.contains(selected);
  const auto target = joining ? joins.at(selected) : QPointer<CyclingMapView>{};
  const auto axis = selected == horizontal ? Qt::Horizontal : Qt::Vertical;
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
  if (watched == m_root && event->type() == QEvent::Resize)
    scheduleEdges();
  if (watched->objectName() == "MapViewAreaEdge")
    return edgeEvent(static_cast<QWidget*>(watched), event);
  if (auto* area = qobject_cast<CyclingMapView*>(watched))
  {
    if (event->type() == QEvent::Resize || event->type() == QEvent::Show)
    {
      auto* grip = area->findChild<QWidget*>("MapViewAreaGrip");
      grip->move(area->width() - grip->width(), 0);
      grip->raise();
    }
    if (event->type() == QEvent::Hide || event->type() == QEvent::Show)
      scheduleEdges();
  }
  if (event->type() == QEvent::Show && qobject_cast<MapViewBase*>(watched))
    if (
      auto* grip = static_cast<QWidget*>(watched)->parentWidget()->findChild<QWidget*>(
        "MapViewAreaGrip"))
      grip->raise();
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
    const auto key = static_cast<QKeyEvent*>(event)->key();
    if (key == Qt::Key_Escape)
    {
      cancelGesture();
      return true;
    }
    if (
      !m_dragSource
      && (key == Qt::Key_Return || key == Qt::Key_Space || key == Qt::Key_Menu))
    {
      showAreaMenu(area, grip->mapToGlobal(grip->rect().center()));
      return true;
    }
  }
  if (event->type() == QEvent::MouseButtonPress)
  {
    auto* mouse = static_cast<QMouseEvent*>(event);
    if (mouse->button() == Qt::RightButton && m_dragSource)
    {
      cancelGesture();
      return true;
    }
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
  if (
    (event->type() == QEvent::MouseMove || event->type() == QEvent::KeyPress
     || event->type() == QEvent::KeyRelease)
    && m_dragSource)
  {
    const auto mouse =
      event->type() == QEvent::MouseMove ? static_cast<QMouseEvent*>(event) : nullptr;
    updateGesture(
      mouse ? mouse->globalPosition().toPoint() : QCursor::pos(),
      mouse ? mouse->modifiers() : static_cast<QKeyEvent*>(event)->modifiers());
    return true;
  }
  if (event->type() == QEvent::MouseButtonRelease && m_dragSource)
  {
    auto* mouse = static_cast<QMouseEvent*>(event);
    if (mouse->button() != Qt::LeftButton)
      return true;
    updateGesture(mouse->globalPosition().toPoint(), mouse->modifiers());
    const auto source = m_dragSource;
    const auto target = m_dragTarget;
    const auto commit = m_dragMoved && m_preview->isVisible();
    const auto axis = m_splitOrientation;
    const auto ratio = m_splitRatio;
    const auto plan = m_gesturePlan;
    const auto before = geometryRects();
    const auto beforeViews = areas();
    cancelGesture();
    if (commit)
      QTimer::singleShot(
        0,
        this,
        [this,
         source,
         target,
         axis,
         ratio,
         plan,
         before,
         beforeViews,
         transfer = bool(target)]() {
          if (!source || areas() != beforeViews || geometryRects() != before)
            return;
          if (transfer)
          {
            if (target)
              applyPlan(plan, source);
          }
          else
            splitArea(source, axis, ratio, axis == Qt::Vertical);
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
