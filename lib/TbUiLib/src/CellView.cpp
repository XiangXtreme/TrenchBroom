/*
 Copyright (C) 2010 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "ui/CellView.h"

#include <QDrag>
#include <QMimeData>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QToolTip>

#include "base/PreferenceManager.h"
#include "gl/ActiveShader.h"
#include "gl/FontDescriptor.h"
#include "gl/FontManager.h"
#include "gl/GlInterface.h"
#include "gl/PrimType.h"
#include "gl/Shaders.h"
#include "gl/TextureFont.h"
#include "gl/VertexArray.h"
#include "gl/VertexType.h"
#include "mdl/BasicShapes.h"
#include "prefs/Preferences.h"
#include "render/Transformation.h"
#include "ui/CellLayout.h"
#include "ui/QColorUtils.h"
#include "ui/RenderView.h"
#include "ui/Theme.h"

#include "kd/flat_map.h"
#include "kd/ranges/repeat_view.h"
#include "kd/ranges/stride_view.h"
#include "kd/ranges/zip_view.h"
#include "kd/vector_utils.h"

#include "vm/mat_ext.h"

#include <algorithm>
#include <ranges>
#include <string>
#include <vector>

namespace tb::ui
{

void CellView::updateScrollBar()
{
  if (m_scrollBar)
  {
    const auto thumbSize = size().height();
    const auto range = int(m_layout.height());
    m_scrollBar->setMinimum(0);
    m_scrollBar->setMaximum(std::max(range - thumbSize, 0));
    m_scrollBar->setPageStep(thumbSize);
    m_scrollBar->setSingleStep(int(m_layout.minCellHeight()));
  }
}

void CellView::initLayout()
{
  doInitLayout(m_layout);
  m_layoutInitialized = true;
}

void CellView::reloadLayout()
{
  initLayout(); // always initialize the layout when reloading

  m_hoveredCell = nullptr;
  m_layout.clear();
  doReloadLayout(m_layout);
  updateScrollBar();

  if (m_focusedGroupTitle && groupWithTitle(*m_focusedGroupTitle) == nullptr)
  {
    m_focusedGroupTitle.reset();
  }

  m_valid = true;
}

void CellView::validate()
{
  if (!m_valid)
  {
    reloadLayout();
  }
}

const CellView::Group* CellView::groupTitleAt(const float x, const float y)
{
  const auto visible = visibleRect();
  const auto visibleY = float(visible.y());
  const auto visibleHeight = float(visible.height());

  for (const auto& group : m_layout.groups())
  {
    if (!isGroupCollapsible(group) || !group.intersectsY(visibleY, visibleHeight))
    {
      continue;
    }

    const auto titleBounds =
      m_layout.titleBoundsForVisibleRect(group, visibleY, visibleHeight);
    if (titleBounds.containsPoint(x, y))
    {
      return &group;
    }
  }

  return nullptr;
}

const CellView::Group* CellView::groupWithTitle(const std::string& title)
{
  const auto& groups = m_layout.groups();
  const auto it = std::ranges::find(groups, title, &Group::title);
  return it != groups.end() && isGroupCollapsible(*it) ? &*it : nullptr;
}

void CellView::scrollToGroup(const Group& group)
{
  if (!m_scrollBar)
  {
    return;
  }

  const auto visible = visibleRect();
  const auto& bounds = group.titleBounds();
  if (bounds.top() < float(visible.top()))
  {
    m_scrollBar->setValue(int(bounds.top()));
  }
  else if (bounds.bottom() > float(visible.bottom()))
  {
    m_scrollBar->setValue(int(bounds.bottom()) - visible.height());
  }
}

void CellView::toggleGroup(const Group& group)
{
  const auto title = group.title();
  auto titleViewportOffset = 0.0f;
  if (m_scrollBar)
  {
    const auto visible = visibleRect();
    const auto visibleTitleBounds = m_layout.titleBoundsForVisibleRect(
      group, float(visible.top()), float(visible.height()));
    titleViewportOffset = visibleTitleBounds.top() - float(visible.top());
  }

  doToggleGroup(group);
  validate();

  if (const auto* reloadedGroup = groupWithTitle(title))
  {
    m_focusedGroupTitle = title;
    if (m_scrollBar)
    {
      m_scrollBar->setValue(
        int(reloadedGroup->titleBounds().top() - titleViewportOffset));
    }
  }
  update();
}

bool CellView::selectAdjacentGroup(const int offset)
{
  const auto& groups = m_layout.groups();
  auto collapsibleGroups = std::vector<const Group*>{};
  for (const auto& group : groups)
  {
    if (isGroupCollapsible(group))
    {
      collapsibleGroups.push_back(&group);
    }
  }
  if (collapsibleGroups.empty())
  {
    return false;
  }

  auto index = offset > 0 ? 0 : int(collapsibleGroups.size()) - 1;
  if (m_focusedGroupTitle)
  {
    const auto it =
      std::ranges::find(collapsibleGroups, *m_focusedGroupTitle, [](const auto* group) {
        return group->title();
      });
    if (it != collapsibleGroups.end())
    {
      index = std::clamp(
        int(std::distance(collapsibleGroups.begin(), it)) + offset,
        0,
        int(collapsibleGroups.size()) - 1);
    }
  }

  const auto& group = *collapsibleGroups[size_t(index)];
  m_focusedGroupTitle = group.title();
  scrollToGroup(group);
  update();
  return true;
}

bool CellView::activateFocusedGroup()
{
  if (!m_focusedGroupTitle)
  {
    return false;
  }

  if (const auto* group = groupWithTitle(*m_focusedGroupTitle))
  {
    toggleGroup(*group);
    return true;
  }
  return false;
}

CellView::CellView(AppController& appController, QScrollBar* scrollBar)
  : RenderView{appController}
  , m_scrollBar{scrollBar}
{
  if (m_scrollBar)
  {
    connect(
      m_scrollBar,
      &QAbstractSlider::actionTriggered,
      this,
      &CellView::onScrollBarActionTriggered);
    connect(
      m_scrollBar,
      &QAbstractSlider::valueChanged,
      this,
      &CellView::onScrollBarValueChanged);
  }
}

void CellView::invalidate()
{
  m_hoveredCell = nullptr;
  m_hoveredGroupTitle.reset();
  m_pressedGroupTitle.reset();
  m_valid = false;
}

void CellView::clear()
{
  m_hoveredCell = nullptr;
  m_hoveredGroupTitle.reset();
  m_pressedGroupTitle.reset();
  m_focusedGroupTitle.reset();
  m_layout.clear();
  doClear();
  m_valid = true;
}

void CellView::resizeLayout(const float width)
{
  m_hoveredCell = nullptr;
  m_layout.setWidth(width);
  validate();
  updateScrollBar();
}

void CellView::resizeEvent(QResizeEvent* event)
{
  resizeLayout(float(size().width()));

  RenderView::resizeEvent(event);
}

void CellView::scrollToCellInternal(const Cell& cell)
{
  const auto visibleRect = this->visibleRect();
  const auto top = int(cell.cellBounds().top());
  const auto bottom = int(cell.cellBounds().bottom());

  if (top >= visibleRect.top() && bottom <= visibleRect.bottom())
  {
    return;
  }

  const auto rowMargin = int(m_layout.rowMargin());
  const auto newPosition = top < visibleRect.top()
                             ? top - rowMargin
                             : visibleRect.top() + bottom - visibleRect.bottom();

  auto* animation = new QPropertyAnimation{m_scrollBar, "sliderPosition"};
  animation->setDuration(300);
  animation->setEasingCurve(QEasingCurve::InOutQuad);
  animation->setStartValue(m_scrollBar->sliderPosition());
  animation->setEndValue(newPosition);
  animation->start();
}

void CellView::onScrollBarValueChanged()
{
  m_hoveredCell = nullptr;
  m_hoveredGroupTitle.reset();
  update();
}

/**
 * QAbstractSlider::actionTriggered listener. Overrides the default movement increments
 * for the scrollbar up/down /page up/page down arrows.
 */
void CellView::onScrollBarActionTriggered(int action)
{
  validate();
  const auto top = float(m_scrollBar->value());
  const auto height = float(size().height());

  // NOTE: We call setSliderPosition(), not setValue()
  // see: https://doc.qt.io/archives/qt-4.8/qabstractslider.html#actionTriggered
  switch (action)
  {
  case QAbstractSlider::SliderSingleStepAdd:
    m_scrollBar->setSliderPosition(int(m_layout.rowPosition(top, 1))); // line down
    break;
  case QAbstractSlider::SliderSingleStepSub:
    m_scrollBar->setSliderPosition(int(m_layout.rowPosition(top, -1))); // line up
    break;
  case QAbstractSlider::SliderPageStepAdd:
    m_scrollBar->setSliderPosition(
      int(m_layout.rowPosition(top + height, 0))); // page down
    break;
  case QAbstractSlider::SliderPageStepSub:
    m_scrollBar->setSliderPosition(int(m_layout.rowPosition(top - height, 0))); // page up
    break;
  default:
    break;
  }
}

// CellView

void CellView::mousePressEvent(QMouseEvent* event)
{
  validate();
  if (event->button() == Qt::LeftButton)
  {
    const auto top = m_scrollBar ? m_scrollBar->value() : 0;
    const auto x = float(event->position().x());
    const auto y = float(event->position().y() + top);
    if (const auto* group = groupTitleAt(x, y))
    {
      m_pressedGroupTitle = group->title();
      m_focusedGroupTitle = group->title();
      m_potentialDrag = false;
      setFocus(Qt::MouseFocusReason);
      update();
      event->accept();
      return;
    }

    m_focusedGroupTitle.reset();
    m_potentialDrag = true;
  }
  else if (event->button() == Qt::RightButton)
  {
    if (event->modifiers() & Qt::AltModifier)
    {
      m_lastMousePos = event->pos();
    }
  }
}

void CellView::mouseReleaseEvent(QMouseEvent* event)
{
  validate();
  if (event->button() == Qt::LeftButton)
  {
    const auto top = m_scrollBar ? m_scrollBar->value() : 0;
    const auto x = float(event->position().x());
    const auto y = float(event->position().y() + top);

    if (m_pressedGroupTitle)
    {
      const auto pressedTitle = std::move(*m_pressedGroupTitle);
      m_pressedGroupTitle.reset();
      if (const auto* group = groupTitleAt(x, y); group && group->title() == pressedTitle)
      {
        toggleGroup(*group);
      }
      m_potentialDrag = false;
      update();
      event->accept();
      return;
    }

    m_potentialDrag = false;
    doLeftClick(m_layout, x, y);
  }
}

void CellView::mouseDoubleClickEvent(QMouseEvent* event)
{
  validate();
  if (event->button() == Qt::LeftButton)
  {
    const auto top = m_scrollBar ? m_scrollBar->value() : 0;
    const auto x = float(event->position().x());
    const auto y = float(event->position().y() + top);
    doDoubleClick(m_layout, x, y);
  }
}

void CellView::mouseMoveEvent(QMouseEvent* event)
{
  validate();
  if (event->buttons() & Qt::LeftButton)
  {
    if (m_potentialDrag)
    {
      startDrag(event);
      m_potentialDrag = false;
    }
  }
  else if ((event->buttons() & Qt::RightButton) && (event->modifiers() & Qt::AltModifier))
  {
    scroll(event);
  }

  {
    const auto top = m_scrollBar ? m_scrollBar->value() : 0;
    const auto x = float(event->position().x());
    const auto y = float(event->position().y() + top);
    const auto* hoveredGroup = groupTitleAt(x, y);
    const auto hoveredGroupTitle =
      hoveredGroup ? std::optional<std::string>{hoveredGroup->title()} : std::nullopt;
    const auto* hoveredCell = hoveredGroup ? nullptr : m_layout.cellAt(x, y);
    if (m_hoveredCell != hoveredCell || m_hoveredGroupTitle != hoveredGroupTitle)
    {
      m_hoveredCell = hoveredCell;
      m_hoveredGroupTitle = hoveredGroupTitle;
      update();
    }
    doMouseMove(m_layout, x, y);
  }

  m_lastMousePos = event->pos();
}

void CellView::leaveEvent(QEvent* event)
{
  if (m_hoveredCell != nullptr || m_hoveredGroupTitle)
  {
    m_hoveredCell = nullptr;
    m_hoveredGroupTitle.reset();
    update();
  }
  RenderView::leaveEvent(event);
}

void CellView::wheelEvent(QWheelEvent* event)
{
  const auto pixelDelta = event->pixelDelta();
  const auto angleDelta = event->angleDelta();

  if (!pixelDelta.isNull())
  {
    scrollBy(pixelDelta.y());
  }
  else if (!angleDelta.isNull())
  {
    scrollBy(angleDelta.y());
  }
  event->accept();
}

bool CellView::event(QEvent* event)
{
  if (event->type() == QEvent::ToolTip)
  {
    return updateTooltip(static_cast<QHelpEvent*>(event));
  }
  return QWidget::event(event);
}

void CellView::contextMenuEvent(QContextMenuEvent* event)
{
  validate();
  const auto top = m_scrollBar ? m_scrollBar->value() : 0;
  const auto x = float(event->pos().x());
  const auto y = float(event->pos().y() + top);
  doContextMenu(m_layout, x, y, event);
}

void CellView::startDrag(const QMouseEvent* event)
{
  validate();
  if (dndEnabled())
  {
    const auto top = m_scrollBar ? m_scrollBar->value() : 0;
    const auto x = float(event->position().x());
    const auto y = float(event->position().y() + top);
    if (const Cell* cell = m_layout.cellAt(x, y))
    {
      /*
       wxImage* feedbackImage = dndImage(*cell);
       int xOffset = event.GetX() - int(cell->itemBounds().left());
       int yOffset = event.GetY() - int(cell->itemBounds().top()) + top;
       */

      doLeftClick(m_layout, x, y);

      const auto dropData = dndData(*cell);

      auto* mimeData = new QMimeData{};
      mimeData->setText(dropData);

      auto* drag = new QDrag{this};
      drag->setMimeData(mimeData);

      drag->exec(Qt::CopyAction);
    }
  }
}

void CellView::scroll(const QMouseEvent* event)
{
  const auto mousePosition = event->pos();
  const auto delta = mousePosition.y() - m_lastMousePos.y();

  scrollBy(delta);
}

void CellView::scrollBy(const int deltaY)
{
  validate();
  if (m_scrollBar)
  {
    const auto newThumbPosition = m_scrollBar->value() - deltaY;
    m_scrollBar->setValue(newThumbPosition);
    update();
  }
}

bool CellView::updateTooltip(QHelpEvent* event)
{
  validate();
  const auto top = m_scrollBar ? m_scrollBar->value() : 0;
  const auto x = float(event->pos().x());
  const auto y = float(event->pos().y() + top);

  // see: https://doc.qt.io/qt-5/qtwidgets-widgets-tooltips-example.html
  if (const auto* cell = m_layout.cellAt(x, y))
  {
    QToolTip::showText(event->globalPos(), tooltip(*cell));
  }
  else
  {
    QToolTip::hideText();
    event->ignore();
  }
  return true;
}

QRect CellView::visibleRect() const
{
  const auto top = m_scrollBar ? m_scrollBar->value() : 0;
  return QRect{QPoint{0, top}, size()};
}

void CellView::renderContents(gl::Gl& gl)
{
  validate();
  if (!m_layoutInitialized)
  {
    initLayout();
  }

  const auto r = devicePixelRatioF();
  const auto viewportWidth = int(width() * r);
  const auto viewportHeight = int(height() * r);
  gl.viewport(0, 0, viewportWidth, viewportHeight);

  setupGL(gl);

  // NOTE: These are in points, while the glViewport call above is
  // in pixels
  const auto visibleRect = this->visibleRect();

  const auto y = float(visibleRect.y());
  const auto h = float(visibleRect.height());

  renderCellBackgrounds(gl, y, h);
  doRender(gl, m_layout, y, h);

  const auto viewLeft = float(0);
  const auto viewTop = float(size().height());
  const auto viewRight = float(size().width());
  const auto viewBottom = float(0);

  const auto transformation = render::Transformation{
    gl,
    vm::ortho_matrix(-1.0f, 1.0f, viewLeft, viewTop, viewRight, viewBottom),
    vm::view_matrix(vm::vec3f{0, 0, -1}, vm::vec3f{0, 1, 0})
      * vm::translation_matrix(vm::vec3f{0.0f, 0.0f, 0.1f})};

  gl.disable(GL_DEPTH_TEST);
  gl.frontFace(GL_CCW);
  renderTitleBackgrounds(gl, y, h);
  renderTitleStrings(gl, y, h);
  if (!layoutHasCells())
  {
    renderEmptyMessage(gl);
  }
}

void CellView::setupGL(gl::Gl& gl)
{
  if (pref(Preferences::EnableMSAA))
  {
    gl.enable(GL_MULTISAMPLE);
  }
  else
  {
    gl.disable(GL_MULTISAMPLE);
  }
  gl.enable(GL_BLEND);
  gl.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  gl.enable(GL_CULL_FACE);
  gl.enable(GL_DEPTH_TEST);
  gl.depthFunc(GL_LEQUAL);
  gl.shadeModel(GL_SMOOTH);
}

namespace
{

void appendRoundedRect(
  std::vector<gl::VertexTypes::P2C4::Vertex>& vertices,
  const LayoutBounds& bounds,
  const float y,
  const float height,
  const RgbaF& color)
{
  constexpr auto CornerRadius = 5.0f;
  constexpr auto CornerSegments = size_t{3};

  const auto radius =
    std::min(CornerRadius, std::min(bounds.width, bounds.height) / 2.0f);
  const auto center = vm::vec2f{
    bounds.left() + bounds.width / 2.0f,
    height - (bounds.top() + bounds.height / 2.0f - y)};
  for (const auto& vertex :
       mdl::roundedRect2D(bounds.width, bounds.height, radius, CornerSegments))
  {
    vertices.emplace_back(center + vertex, color.toVec());
  }
}

} // namespace

void CellView::renderCellBackgrounds(gl::Gl& gl, const float y, const float height)
{
  using Vertex = gl::VertexTypes::P2C4::Vertex;
  auto vertices = std::vector<Vertex>{};
  const auto backgroundColor = browserCellBackgroundColor(palette()).to<RgbaF>();
  const auto hoverColor = browserCellHoverColor(palette()).to<RgbaF>();
  const auto selectedColor = browserCellSelectedColor(palette()).to<RgbaF>();

  for (const auto& group : m_layout.groups())
  {
    if (!group.intersectsY(y, height))
    {
      continue;
    }

    for (const auto& row : group.rows())
    {
      if (!row.intersectsY(y, height))
      {
        continue;
      }

      for (const auto& cell : row.cells())
      {
        appendRoundedRect(vertices, cell.cellBounds(), y, height, backgroundColor);
        if (isCellSelected(cell))
        {
          appendRoundedRect(vertices, cell.cellBounds(), y, height, selectedColor);
        }
        else if (m_hoveredCell == &cell)
        {
          appendRoundedRect(vertices, cell.cellBounds(), y, height, hoverColor);
        }
      }
    }
  }

  if (vertices.empty())
  {
    return;
  }

  const auto transformation = render::Transformation{
    gl,
    vm::ortho_matrix(
      -1.0f, 1.0f, 0.0f, float(size().height()), float(size().width()), 0.0f),
    vm::view_matrix(vm::vec3f{0, 0, -1}, vm::vec3f{0, 1, 0})
      * vm::translation_matrix(vm::vec3f{0.0f, 0.0f, 0.1f})};

  auto shader =
    gl::ActiveShader{gl, shaderManager(), gl::Shaders::MaterialBrowserBorderShader};
  auto vertexArray = gl::VertexArray::move(std::move(vertices));
  vertexArray.prepare(gl, vboManager());
  gl.disable(GL_CULL_FACE);
  if (vertexArray.setup(gl, shader.program()))
  {
    vertexArray.render(gl, gl::PrimType::Triangles);
    vertexArray.cleanup(gl, shader.program());
  }
  gl.enable(GL_CULL_FACE);
}

void CellView::renderTitleBackgrounds(gl::Gl& gl, float y, float height)
{
  using Vertex = gl::VertexTypes::P2C4::Vertex;
  auto backgroundVertices = std::vector<Vertex>{};
  auto indicatorVertices = std::vector<Vertex>{};

  const auto baseColor = browserGroupBackgroundColor(palette()).to<RgbaF>();
  const auto hoverColor = browserCellHoverColor(palette()).to<RgbaF>();
  const auto pressedColor = browserCellSelectedColor(palette()).to<RgbaF>();
  const auto dividerColor =
    fromQColor(palette().color(QPalette::Active, QPalette::Mid)).to<RgbaF>();
  const auto focusColor =
    fromQColor(palette().color(QPalette::Active, QPalette::Highlight)).to<RgbaF>();
  const auto indicatorColor = browserTextColor(palette()).to<RgbaF>();

  const auto appendQuad = [&](const LayoutBounds& bounds, const RgbaF& color) {
    backgroundVertices.emplace_back(
      vm::vec2f{bounds.left(), height - (bounds.top() - y)}, color.toVec());
    backgroundVertices.emplace_back(
      vm::vec2f{bounds.left(), height - (bounds.bottom() - y)}, color.toVec());
    backgroundVertices.emplace_back(
      vm::vec2f{bounds.right(), height - (bounds.bottom() - y)}, color.toVec());
    backgroundVertices.emplace_back(
      vm::vec2f{bounds.right(), height - (bounds.top() - y)}, color.toVec());
  };

  for (const auto& group : m_layout.groups())
  {
    if (group.intersectsY(y, height) && !group.title().empty())
    {
      const auto titleBounds = m_layout.titleBoundsForVisibleRect(group, y, height);
      const auto interactive = isGroupCollapsible(group);
      const auto pressed = interactive && m_pressedGroupTitle == group.title();
      const auto hovered = interactive && m_hoveredGroupTitle == group.title();
      appendQuad(titleBounds, pressed ? pressedColor : hovered ? hoverColor : baseColor);
      appendQuad(
        LayoutBounds{
          titleBounds.left(), titleBounds.bottom() - 1.0f, titleBounds.width, 1.0f},
        dividerColor);

      if (!interactive)
      {
        continue;
      }

      if (hasFocus() && m_focusedGroupTitle == group.title())
      {
        appendQuad(
          LayoutBounds{titleBounds.left(), titleBounds.top(), 2.0f, titleBounds.height},
          focusColor);
      }

      const auto centerX = titleBounds.left() + 12.0f;
      const auto centerY = height - (titleBounds.top() + titleBounds.height / 2.0f - y);
      if (isGroupCollapsed(group))
      {
        indicatorVertices.emplace_back(
          vm::vec2f{centerX - 2.5f, centerY + 4.0f}, indicatorColor.toVec());
        indicatorVertices.emplace_back(
          vm::vec2f{centerX - 2.5f, centerY - 4.0f}, indicatorColor.toVec());
        indicatorVertices.emplace_back(
          vm::vec2f{centerX + 2.5f, centerY}, indicatorColor.toVec());
      }
      else
      {
        indicatorVertices.emplace_back(
          vm::vec2f{centerX - 4.0f, centerY + 2.5f}, indicatorColor.toVec());
        indicatorVertices.emplace_back(
          vm::vec2f{centerX, centerY - 2.5f}, indicatorColor.toVec());
        indicatorVertices.emplace_back(
          vm::vec2f{centerX + 4.0f, centerY + 2.5f}, indicatorColor.toVec());
      }
    }
  }

  auto shader = gl::ActiveShader{gl, shaderManager(), gl::Shaders::VaryingPCShader};
  auto backgrounds = gl::VertexArray::move(std::move(backgroundVertices));
  backgrounds.prepare(gl, vboManager());

  if (backgrounds.setup(gl, shader.program()))
  {
    backgrounds.render(gl, gl::PrimType::Quads);
    backgrounds.cleanup(gl, shader.program());
  }

  auto indicators = gl::VertexArray::move(std::move(indicatorVertices));
  indicators.prepare(gl, vboManager());
  if (indicators.setup(gl, shader.program()))
  {
    indicators.render(gl, gl::PrimType::Triangles);
    indicators.cleanup(gl, shader.program());
  }
}

namespace
{

template <typename IsGroupCollapsible>
auto collectStringVertices(
  CellLayout& layout,
  const float y,
  const float height,
  gl::FontManager& fontManager,
  const QPalette& palette,
  IsGroupCollapsible&& isGroupCollapsible)
{
  using TextVertex = gl::VertexTypes::P2Uv2C4::Vertex;

  auto defaultFont = gl::FontDescriptor{
    pref(Preferences::RendererFontPath), size_t(pref(Preferences::BrowserFontSize))};

  const auto textColor = browserTextColor(palette);
  const auto secondaryTextColor =
    fromQColor(palette.color(QPalette::Active, QPalette::PlaceholderText));

  auto stringVertices = kdl::flat_map<gl::FontDescriptor, std::vector<TextVertex>>{};
  for (const auto& group : layout.groups())
  {
    if (group.intersectsY(y, height))
    {
      const auto& groupTitle = group.title();
      if (!groupTitle.empty())
      {
        const auto titleBounds = layout.titleBoundsForVisibleRect(group, y, height);
        const auto interactive = isGroupCollapsible(group);
        const auto leftPadding = interactive ? 24.0f : 8.0f;
        const auto rightPadding = 8.0f;
        const auto countText =
          group.itemCount() ? std::to_string(*group.itemCount()) : std::string{};
        auto& countFont = fontManager.font(defaultFont);
        const auto countSize = countFont.measure(countText);
        const auto countGap = countText.empty() ? 0.0f : 12.0f;
        const auto availableTitleWidth = std::max(
          titleBounds.width - leftPadding - rightPadding - countSize.x() - countGap,
          1.0f);
        const auto titleFontDescriptor =
          fontManager.selectFontSize(defaultFont, groupTitle, availableTitleWidth, 6);
        auto& font = fontManager.font(titleFontDescriptor);
        const auto textSize = font.measure(groupTitle);
        const auto offset = vm::vec2f{
          titleBounds.left() + leftPadding,
          height - (titleBounds.bottom() - y)
            + std::max((titleBounds.height - textSize.y()) / 2.0f, 0.0f)};
        const auto quads = font.quads(groupTitle, false, offset);

        const auto titleVertices = TextVertex::toList(kdl::views::zip(
          quads | kdl::views::stride(2),
          quads | std::views::drop(1) | kdl::views::stride(2),
          kdl::views::repeat(textColor.to<RgbaF>().toVec())));

        auto& vertices = stringVertices[titleFontDescriptor];
        vertices.insert(
          std::end(vertices), std::begin(titleVertices), std::end(titleVertices));

        if (!countText.empty())
        {
          const auto countOffset = vm::vec2f{
            titleBounds.right() - rightPadding - countSize.x(),
            height - (titleBounds.bottom() - y)
              + std::max((titleBounds.height - countSize.y()) / 2.0f, 0.0f)};
          const auto countQuads = countFont.quads(countText, false, countOffset);
          const auto countVertices = TextVertex::toList(kdl::views::zip(
            countQuads | kdl::views::stride(2),
            countQuads | std::views::drop(1) | kdl::views::stride(2),
            kdl::views::repeat(secondaryTextColor.to<RgbaF>().toVec())));
          kdl::vec_append(stringVertices[defaultFont], countVertices);
        }
      }

      for (const auto& row : group.rows())
      {
        if (row.intersectsY(y, height))
        {
          for (const auto& cell : row.cells())
          {
            const auto& title = cell.title();
            const auto bounds = cell.titleBounds();
            const auto fontDescriptor =
              fontManager.selectFontSize(defaultFont, title, bounds.width, 6);
            const auto& font = fontManager.font(fontDescriptor);
            const auto size = font.measure(title);

            const auto x =
              bounds.left() + std::max((bounds.width - size.x()) / 2.0f, 0.0f);

            // y is relative to top, but OpenGL coords are relative to bottom, so invert
            const auto yOffset = vm::vec2f{x, y + height - bounds.bottom()};

            const auto quads = font.quads(title, false, yOffset);
            const auto vertices = TextVertex::toList(kdl::views::zip(
              quads | kdl::views::stride(2),
              quads | std::views::drop(1) | kdl::views::stride(2),
              kdl::views::repeat(textColor.to<RgbaF>().toVec())));

            kdl::vec_append(stringVertices[fontDescriptor], vertices);
          }
        }
      }
    }
  }

  return stringVertices;
}

} // namespace

void CellView::renderTitleStrings(gl::Gl& gl, float y, float height)
{
  using StringRendererMap = kdl::flat_map<gl::FontDescriptor, gl::VertexArray>;
  auto stringRenderers = StringRendererMap{};

  for (const auto& [descriptor, vertices] : collectStringVertices(
         m_layout, y, height, fontManager(), palette(), [&](const auto& group) {
           return isGroupCollapsible(group);
         }))
  {
    stringRenderers[descriptor] = gl::VertexArray::ref(vertices);
    stringRenderers[descriptor].prepare(gl, vboManager());
  }

  auto shader = gl::ActiveShader{gl, shaderManager(), gl::Shaders::ColoredTextShader};
  shader.set("Texture", 0);

  for (auto&& [descriptor, vertexArray] : stringRenderers)
  {
    if (vertexArray.setup(gl, shader.program()))
    {
      auto& font = fontManager().font(descriptor);
      font.activate(gl);

      vertexArray.render(gl, gl::PrimType::Quads);
      vertexArray.cleanup(gl, shader.program());

      font.deactivate(gl);
    }
  }
}

bool CellView::layoutHasCells()
{
  return std::ranges::any_of(m_layout.groups(), [&](const auto& group) {
    return (isGroupCollapsible(group) && !group.title().empty())
           || std::ranges::any_of(
             group.rows(), [](const auto& row) { return !row.cells().empty(); });
  });
}

void CellView::renderEmptyMessage(gl::Gl& gl)
{
  const auto message = emptyMessage().toStdString();
  if (message.empty())
  {
    return;
  }

  using TextVertex = gl::VertexTypes::P2Uv2C4::Vertex;

  const auto defaultFont = gl::FontDescriptor{
    pref(Preferences::RendererFontPath), size_t(pref(Preferences::BrowserFontSize))};
  const auto maxTextWidth = std::max(float(width()) - 24.0f, 1.0f);
  const auto descriptor =
    fontManager().selectFontSize(defaultFont, message, maxTextWidth, 6);
  auto& font = fontManager().font(descriptor);
  const auto textSize = font.measure(message);
  const auto offset = vm::vec2f{
    std::max((float(width()) - textSize.x()) / 2.0f, 0.0f),
    std::max((float(height()) - textSize.y()) / 2.0f, 0.0f)};
  const auto color = RgbaF{browserTextColor(palette()).to<RgbF>(), 0.66f}.toVec();
  const auto quads = font.quads(message, false, offset);
  auto vertices = TextVertex::toList(kdl::views::zip(
    quads | kdl::views::stride(2),
    quads | std::views::drop(1) | kdl::views::stride(2),
    kdl::views::repeat(color)));

  auto vertexArray = gl::VertexArray::ref(vertices);
  vertexArray.prepare(gl, vboManager());

  auto shader = gl::ActiveShader{gl, shaderManager(), gl::Shaders::ColoredTextShader};
  shader.set("Texture", 0);
  if (vertexArray.setup(gl, shader.program()))
  {
    font.activate(gl);
    vertexArray.render(gl, gl::PrimType::Quads);
    vertexArray.cleanup(gl, shader.program());
    font.deactivate(gl);
  }
}

void CellView::doClear() {}
QString CellView::emptyMessage() const
{
  return {};
}
bool CellView::isGroupCollapsible(const Group&) const
{
  return false;
}
bool CellView::isGroupCollapsed(const Group&) const
{
  return false;
}
void CellView::doToggleGroup(const Group&) {}
void CellView::doLeftClick(Layout&, float, float) {}
void CellView::doDoubleClick(Layout&, float, float) {}
void CellView::doMouseMove(Layout&, float, float) {}
void CellView::doContextMenu(Layout&, float, float, QContextMenuEvent*) {}

bool CellView::dndEnabled()
{
  return false;
}

bool CellView::isCellSelected(const Cell&) const
{
  return false;
}

QPixmap CellView::dndImage(const Cell&)
{
  contract_assert(false);
  return QPixmap{};
}

QString CellView::dndData(const Cell&)
{
  contract_assert(false);
  return "";
}

QString CellView::tooltip(const Cell&)
{
  return "";
}

void CellView::processEvent(const KeyEvent& event)
{
  if (event.type != KeyEvent::Type::Down || event.modifiers != Qt::NoModifier)
  {
    return;
  }

  validate();
  if (event.key == Qt::Key_Down)
  {
    selectAdjacentGroup(1);
  }
  else if (event.key == Qt::Key_Up)
  {
    selectAdjacentGroup(-1);
  }
  else if (
    event.key == Qt::Key_Return || event.key == Qt::Key_Enter
    || event.key == Qt::Key_Space)
  {
    activateFocusedGroup();
  }
  else if (event.key == Qt::Key_Right && m_focusedGroupTitle)
  {
    if (const auto* group = groupWithTitle(*m_focusedGroupTitle);
        group && isGroupCollapsed(*group))
    {
      toggleGroup(*group);
    }
  }
  else if (event.key == Qt::Key_Left && m_focusedGroupTitle)
  {
    if (const auto* group = groupWithTitle(*m_focusedGroupTitle);
        group && !isGroupCollapsed(*group))
    {
      toggleGroup(*group);
    }
  }
}
void CellView::processEvent(const MouseEvent& /* event */) {}
void CellView::processEvent(const ScrollEvent& /* event */) {}
void CellView::processEvent(const GestureEvent& /* event */) {}
void CellView::processEvent(const CancelEvent& /* event */) {}

} // namespace tb::ui
